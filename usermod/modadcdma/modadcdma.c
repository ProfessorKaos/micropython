// adcdma: background DMA ADC sampling (ADC1, esp_adc/adc_continuous) with
// ISR-side RMS accumulation for CPU-free loudness extraction.
//
// The on_conv_done callback runs in ISR context and must only touch static
// memory: no Python-runtime calls, no allocation, no printing.

#include "py/obj.h"
#include "py/runtime.h"

#include "esp_adc/adc_continuous.h"
#include "esp_attr.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#define ADCDMA_ATTEN ADC_ATTEN_DB_12
#else
#define ADCDMA_ATTEN ADC_ATTEN_DB_11
#endif

#define ADCDMA_STORE_BUF_SIZE (4096)
#define ADCDMA_FRAME_SIZE (256)

// Ring buffer of packed uint16 raw samples, filled by the ISR and drained by
// adcdma.readinto().  Single-producer/single-consumer: head is written only by
// the ISR, tail only by the Python side, so no locking is needed here.
#define ADCDMA_RING_BITS (13)
#define ADCDMA_RING_LEN (1u << ADCDMA_RING_BITS)
#define ADCDMA_RING_MASK (ADCDMA_RING_LEN - 1)

static adc_continuous_handle_t adc_handle;

static volatile uint16_t adcdma_ring[ADCDMA_RING_LEN];
static volatile uint32_t ring_head;
static volatile uint32_t ring_tail;

// Sum-of-squares accumulators: written by the ISR, atomically read+reset by
// adcdma.level().
static portMUX_TYPE acc_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint64_t acc_sum_sq;
static volatile uint32_t acc_count;
static volatile uint32_t total_samples;
static volatile uint32_t ovf_count;
// Liveness diagnostics for stats(): ISR invocations vs. pool-read failures.
static volatile uint32_t isr_cnt;
static volatile uint32_t rd_err_cnt;

static uint32_t isqrt64(uint64_t x) {
    // Digit-by-digit base-2 square root: exact for the full uint64 range,
    // no division (safe in any context).
    uint64_t rem = 0;
    uint64_t root = 0;
    for (int i = 0; i < 32; ++i) {
        root <<= 1;
        rem = (rem << 2) | (x >> 62);
        x <<= 2;
        if (root < rem) {
            rem -= root | 1;
            root += 2;
        }
    }
    return (uint32_t)(root >> 1);
}

// ISR context: accumulates directly from the conversion frame handed to the
// callback (the driver cache-syncs it before invoking us), so sampling keeps
// running even if the internal pool read fails.  The pool copy is drained
// best-effort with timeout 0 so it never overflows.  Never blocks, no Python
// runtime, no allocation.  Returns false: no task woken up.
static bool IRAM_ATTR adcdma_on_conv_done(adc_continuous_handle_t handle,
    const adc_continuous_evt_data_t *edata, void *user_data) {
    (void)user_data;

    ++isr_cnt;

    uint64_t sum_sq = 0;
    uint32_t count = 0;
    uint32_t head = ring_head;
    uint32_t tail = ring_tail;

    const uint8_t *frame = edata->conv_frame_buffer;
    for (uint32_t off = 0;
         frame != NULL && off + sizeof(adc_digi_output_data_t) <= edata->size;
         off += sizeof(adc_digi_output_data_t)) {
        adc_digi_output_data_t item;
        memcpy(&item, frame + off, sizeof(item));
        uint32_t v = item.type2.data; // 12-bit sample
        sum_sq += (uint64_t)v * v;
        ++count;

        uint32_t next = (head + 1) & ADCDMA_RING_MASK;
        if (next != tail) {
            adcdma_ring[head] = (uint16_t)v;
            head = next;
        }
        // Ring full: drop the raw copy but keep the sample in the RMS sum.
    }

    ring_head = head;
    if (count != 0) {
        portENTER_CRITICAL_ISR(&acc_lock);
        acc_sum_sq += sum_sq;
        acc_count += count;
        total_samples += count;
        portEXIT_CRITICAL_ISR(&acc_lock);
    }

    uint8_t raw[ADCDMA_FRAME_SIZE] __attribute__((aligned(4)));
    uint32_t n = 0;
    if (adc_continuous_read(handle, raw, sizeof(raw), &n, 0) != ESP_OK) {
        ++rd_err_cnt; // pool empty right after a push: read path trouble
    } else {
        while (adc_continuous_read(handle, raw, sizeof(raw), &n, 0) == ESP_OK) {
            // Discard older pool frames: already accumulated above.
        }
    }
    return false;
}

static bool IRAM_ATTR adcdma_on_pool_ovf(adc_continuous_handle_t handle,
    const adc_continuous_evt_data_t *edata, void *user_data) {
    (void)handle;
    (void)edata;
    (void)user_data;

    ++ovf_count;
    return false;
}

static void adcdma_stop(void) {
    if (adc_handle != NULL) {
        adc_continuous_stop(adc_handle);
        adc_continuous_deinit(adc_handle);
        adc_handle = NULL;
    }
    ring_head = 0;
    ring_tail = 0;
    acc_sum_sq = 0;
    acc_count = 0;
    total_samples = 0;
    ovf_count = 0;
    isr_cnt = 0;
    rd_err_cnt = 0;
}

static mp_obj_t adcdma_init(mp_obj_t pin_in, mp_obj_t freq_in) {
    mp_int_t pin = mp_obj_get_int(pin_in);
    mp_int_t freq = mp_obj_get_int(freq_in);
    if (freq <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("freq must be positive"));
    }

    // Re-entrant init: a second call tears down the running instance first.
    adcdma_stop();

    adc_unit_t unit;
    adc_channel_t channel;
    if (adc_continuous_io_to_channel((int)pin, &unit, &channel) != ESP_OK
        || unit != ADC_UNIT_1) {
        mp_raise_ValueError(MP_ERROR_TEXT("pin not on ADC1"));
    }

    esp_err_t err;
    adc_continuous_handle_cfg_t hcfg = {
        .max_store_buf_size = ADCDMA_STORE_BUF_SIZE,
        .conv_frame_size = ADCDMA_FRAME_SIZE,
        // Self-recover the internal pool if the drain ever lags: the driver
        // then flushes it via its own ISR-safe path.
        .flags = { .flush_pool = 1 },
    };
    err = adc_continuous_new_handle(&hcfg, &adc_handle);
    if (err == ESP_OK) {
        adc_digi_pattern_config_t pattern = {
            .atten = ADCDMA_ATTEN,
            .channel = channel,
            .unit = ADC_UNIT_1,
            .bit_width = ADC_BITWIDTH_12,
        };
        adc_continuous_config_t cfg = {
            .pattern_num = 1,
            .adc_pattern = &pattern,
            .sample_freq_hz = (uint32_t)freq,
            .conv_mode = ADC_CONV_SINGLE_UNIT_1,
            .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
        };
        err = adc_continuous_config(adc_handle, &cfg);
    }
    if (err == ESP_OK) {
        adc_continuous_evt_cbs_t cbs = {
            .on_conv_done = adcdma_on_conv_done,
            .on_pool_ovf = adcdma_on_pool_ovf,
        };
        err = adc_continuous_register_event_callbacks(adc_handle, &cbs, NULL);
    }
    if (err == ESP_OK) {
        err = adc_continuous_start(adc_handle);
    }
    if (err != ESP_OK) {
        adcdma_stop();
        mp_raise_OSError(err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(adcdma_init_obj, adcdma_init);

static mp_obj_t adcdma_level(void) {
    uint64_t sum_sq;
    uint32_t count;
    portENTER_CRITICAL(&acc_lock);
    sum_sq = acc_sum_sq;
    count = acc_count;
    acc_sum_sq = 0;
    acc_count = 0;
    portEXIT_CRITICAL(&acc_lock);

    if (count == 0) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    return MP_OBJ_NEW_SMALL_INT(isqrt64(sum_sq / count));
}
static MP_DEFINE_CONST_FUN_OBJ_0(adcdma_level_obj, adcdma_level);

// Copy up to len(mv)//2 packed uint16 samples out of the raw ring buffer into
// mv; returns the number of samples copied.
static mp_obj_t adcdma_readinto(mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);
    uint16_t *dst = (uint16_t *)bufinfo.buf;
    uint32_t max = bufinfo.len / sizeof(uint16_t);
    uint32_t count = 0;
    while (count < max) {
        uint32_t tail = ring_tail;
        if (tail == ring_head) {
            break;
        }
        dst[count++] = adcdma_ring[tail];
        ring_tail = (tail + 1) & ADCDMA_RING_MASK;
    }
    return MP_OBJ_NEW_SMALL_INT(count);
}
static MP_DEFINE_CONST_FUN_OBJ_1(adcdma_readinto_obj, adcdma_readinto);

static mp_obj_t adcdma_deinit(void) {
    adcdma_stop();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(adcdma_deinit_obj, adcdma_deinit);
static MP_DEFINE_CONST_FUN_OBJ_0(adcdma_drop_obj, adcdma_deinit);

static mp_obj_t adcdma_stats(void) {
    mp_obj_t dict = mp_obj_new_dict(4);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_ovf), mp_obj_new_int_from_uint(ovf_count));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_samples), mp_obj_new_int_from_uint(total_samples));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_isr), mp_obj_new_int_from_uint(isr_cnt));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_rderr), mp_obj_new_int_from_uint(rd_err_cnt));
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_0(adcdma_stats_obj, adcdma_stats);

static const mp_rom_map_elem_t adcdma_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&adcdma_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_level), MP_ROM_PTR(&adcdma_level_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&adcdma_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&adcdma_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_drop), MP_ROM_PTR(&adcdma_drop_obj) },
    { MP_ROM_QSTR(MP_QSTR_stats), MP_ROM_PTR(&adcdma_stats_obj) },
};
static MP_DEFINE_CONST_DICT(adcdma_module_globals, adcdma_module_globals_table);

const mp_obj_module_t adcdma_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&adcdma_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_adcdma, adcdma_module);