/*
 * modnrf_pdm.c — PDM microphone capture for ZENRF (nRF52840).
 *
 * Drives the hardware PDM peripheral with EasyDMA: sampling runs at zero CPU
 * cost and uses NO machine.Timer (LED engine owns timers 1-4; timer 0 is the
 * SoftDevice's). The CPU only wakes on the PDM IRQ to recycle buffers.
 *
 * Buffering: two DMA buffers ping-pong inside nrfx; every completed chunk is
 * copied (in the IRQ) into a small software ring so a late reader only ever
 * loses the oldest chunk (counted in `overruns`) and never stalls capture.
 *
 * Python API:
 *   from nrf_pdm import PDM
 *   pdm = PDM(rate=16000, clk=32, data=16, chunk=512)   # chunk in samples
 *   pdm.start() / pdm.stop() / pdm.deinit()
 *   pdm.readinto(buf) -> bytes copied (0 = no fresh chunk yet); oldest first
 *   pdm.rms()         -> mean square of the samples copied by last readinto
 *   pdm.overruns()    -> number of chunks dropped because the reader lagged
 *
// Sample rate (MONO): fs = PDM_CLK / RATIO (stereo halves it); nRF52840 only
// allows RATIO 64/80, and MDK clock options top out at 1.333 MHz, so:
//   rate >= 12000 -> PDM_CLK 1.032 MHz, RATIO 64 -> fs ~ 16125 Hz (default)
//   otherwise     -> PDM_CLK 1.032 MHz, RATIO 80 -> fs ~ 12900 Hz (lower CPU)
// 1.032 MHz is inside the MP34DT05's 1.0-3.25 MHz clock spec.
// Python uses measured dt, so the ~1% rate offset is irrelevant.
 */

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "nrfx_pdm.h"

#if NRFX_PDM_ENABLED

#define PDM_MAX_SAMPLES 1024   // upper bound for chunk (samples per read)
#define PDM_RING_DEPTH  3      // completed chunks held for the reader

typedef struct _nrf_pdm_obj_t {
    mp_obj_base_t base;
    bool initialized;
    volatile bool running;
    uint16_t chunk;                       // samples per chunk
    uint32_t rate;                        // requested sample rate (Hz)
    uint32_t clk_pin;                     // PDM CLK pin
    uint32_t data_pin;                    // PDM DATA pin
    volatile uint8_t r_pop;               // ring: next slot for the reader
    volatile uint8_t r_push;              // ring: next slot for the IRQ
    volatile uint8_t r_count;             // ring: slots holding data
    volatile uint32_t overruns;           // dropped chunk counter
    volatile float last_ms;               // mean square of last readinto
} nrf_pdm_obj_t;

static nrf_pdm_obj_t nrf_pdm_singleton;
static nrfx_pdm_t const pdm_instance = NRFX_PDM_INSTANCE(0);

// Static (BSS) DMA storage: outside the GC heap so capture buffers are never
// collected while EasyDMA is writing into them.
static int16_t pdm_dma[2][PDM_MAX_SAMPLES];
static int16_t pdm_ring[PDM_RING_DEPTH][PDM_MAX_SAMPLES];
static bool pdm_dma_in_hw[2];             // slot currently owned by nrfx

// --- Section: PDM IRQ event handler (runs in interrupt context) ---
static void pdm_event_handler(nrfx_pdm_evt_t const *evt) {
    nrf_pdm_obj_t *self = &nrf_pdm_singleton;

    // Ignore late events once capture is stopped (stop() is asynchronous:
    // nrfx finishes the abort in a later IRQ; the ring is reset on start()).
    if (!self->running) {
        return;
    }

    if (evt->error == NRFX_PDM_ERROR_OVERFLOW) {
        self->overruns++;
    }

    int16_t *released = (int16_t *)evt->buffer_released;
    if (released != NULL) {
        int idx = (released == pdm_dma[1]) ? 1 : 0;
        pdm_dma_in_hw[idx] = false;

        // Publish the completed chunk: drop the oldest if the reader lags.
        if (self->r_count == PDM_RING_DEPTH) {
            self->r_pop = (self->r_pop + 1) % PDM_RING_DEPTH;
            self->r_count--;
            self->overruns++;
        }
        memcpy(pdm_ring[self->r_push], released, self->chunk * sizeof(int16_t));
        self->r_push = (self->r_push + 1) % PDM_RING_DEPTH;
        self->r_count++;
    }

    if (evt->buffer_requested) {
        // Hand nrfx a free DMA buffer immediately so capture never starves.
        int16_t *give = NULL;
        if (released != NULL) {
            give = released;                       // just freed above
        } else if (!pdm_dma_in_hw[0]) {
            give = pdm_dma[0];
        } else if (!pdm_dma_in_hw[1]) {
            give = pdm_dma[1];
        }
        if (give != NULL && nrfx_pdm_buffer_set(&pdm_instance, give, self->chunk) == NRFX_SUCCESS) {
            pdm_dma_in_hw[(give == pdm_dma[1]) ? 1 : 0] = true;
        }
    }
}

// Vector wiring: nrfx_irqs_nrf52840.h already #defines
// nrfx_pdm_0_irq_handler -> PDM_IRQHandler, so NRFX_INSTANCE_IRQ_HANDLERS
// inside nrfx_pdm.c IS the vector handler. Nothing to add here.

// --- Section: configuration helpers ---
static void pdm_configure(nrf_pdm_obj_t *self, mp_int_t rate, mp_int_t clk, mp_int_t data) {
    nrfx_pdm_config_t cfg = NRFX_PDM_DEFAULT_CONFIG((uint32_t)clk, (uint32_t)data);
    // 6 == same app IRQ priority machine.Timer uses on this port (SD-safe).
    cfg.interrupt_priority = NRFX_PDM_DEFAULT_CONFIG_IRQ_PRIORITY;
    cfg.clock_freq = NRF_PDM_FREQ_1032K;              // valid MP34DT05 clock
    cfg.ratio = (rate >= 12000) ? NRF_PDM_RATIO_64X : NRF_PDM_RATIO_80X;

    // Clear any stale events left by a previous run so the first IRQ after
    // init cannot be delivered against a half-configured driver.
    NRF_PDM->EVENTS_STARTED = 0;
    NRF_PDM->EVENTS_STOPPED = 0;
    NRF_PDM->EVENTS_END = 0;

    nrfx_err_t err = nrfx_pdm_init(&pdm_instance, &cfg, pdm_event_handler);
    if (err != NRFX_SUCCESS && err != NRFX_ERROR_ALREADY) {
        mp_raise_msg(&mp_type_RuntimeError, "PDM init failed");
    }
    self->rate = (uint32_t)rate;
    self->clk_pin = (uint32_t)clk;
    self->data_pin = (uint32_t)data;
    self->initialized = true;
}

// --- Section: Python constructor ---
static mp_obj_t nrf_pdm_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_rate, ARG_clk, ARG_data, ARG_chunk };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_rate,  MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 16000} },
        { MP_QSTR_clk,   MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 32} },
        { MP_QSTR_data,  MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 16} },
        { MP_QSTR_chunk, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 512} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_int_t chunk = args[ARG_chunk].u_int;
    if (chunk < 16 || chunk > PDM_MAX_SAMPLES) {
        mp_raise_ValueError(MP_ERROR_TEXT("chunk out of range"));
    }

    nrf_pdm_obj_t *self = &nrf_pdm_singleton;
    if (self->running) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("PDM busy"));
    }
    if (self->initialized) {
        // Existing instance: reconfigure chunk/rate/pins.
        nrfx_pdm_uninit(&pdm_instance);
        self->initialized = false;
    }
    self->chunk = (uint16_t)chunk;
    self->r_pop = self->r_push = self->r_count = 0;
    pdm_configure(self, args[ARG_rate].u_int, args[ARG_clk].u_int, args[ARG_data].u_int);
    return MP_OBJ_FROM_PTR(self);
}

// --- Section: start / stop / deinit ---
static mp_obj_t nrf_pdm_start(mp_obj_t self_in) {
    nrf_pdm_obj_t *self = (nrf_pdm_obj_t *)MP_OBJ_TO_PTR(self_in);
    if (self->running) {
        return mp_const_none;
    }
    if (self->chunk == 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("PDM not configured"));
    }
    // Fresh driver state each start: cleanly absorbs a previous async stop.
    if (self->initialized) {
        nrfx_pdm_uninit(&pdm_instance);
        self->initialized = false;
    }
    // Fresh ring so stale pre-start chunks are never served.
    mp_uint_t irq_state = MICROPY_BEGIN_ATOMIC_SECTION();
    self->r_pop = self->r_push = self->r_count = 0;
    pdm_dma_in_hw[0] = pdm_dma_in_hw[1] = false;
    MICROPY_END_ATOMIC_SECTION(irq_state);
    pdm_configure(self, self->rate, self->clk_pin, self->data_pin);
    self->running = true;
    nrfx_pdm_start(&pdm_instance);
    return mp_const_none;
}

static mp_obj_t nrf_pdm_stop(mp_obj_t self_in) {
    nrf_pdm_obj_t *self = (nrf_pdm_obj_t *)MP_OBJ_TO_PTR(self_in);
    if (!self->running) {
        return mp_const_none;
    }
    // Asynchronous on purpose: gate the handler first, kick the abort, and
    // return immediately - any busy-wait here would block uasyncyio/REPL.
    self->running = false;
    nrfx_pdm_stop(&pdm_instance);
    mp_uint_t irq_state2 = MICROPY_BEGIN_ATOMIC_SECTION();
    self->r_pop = self->r_push = self->r_count = 0;
    MICROPY_END_ATOMIC_SECTION(irq_state2);
    return mp_const_none;
}

static mp_obj_t nrf_pdm_deinit(mp_obj_t self_in) {
    nrf_pdm_obj_t *self = (nrf_pdm_obj_t *)MP_OBJ_TO_PTR(self_in);
    nrf_pdm_stop(self_in);
    if (self->initialized) {
        nrfx_pdm_uninit(&pdm_instance);
        self->initialized = false;
    }
    return mp_const_none;
}

// --- Section: reader path (called from Python thread context) ---
static mp_obj_t nrf_pdm_readinto(mp_obj_t self_in, mp_obj_t buf_in) {
    nrf_pdm_obj_t *self = (nrf_pdm_obj_t *)MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer(buf_in, &bufinfo, MP_BUFFER_WRITE);

    uint16_t max_samples = (uint16_t)(bufinfo.len / sizeof(int16_t));
    uint16_t n = (max_samples < self->chunk) ? max_samples : self->chunk;
    int16_t *dst = (int16_t *)bufinfo.buf;

    mp_uint_t irq_state = MICROPY_BEGIN_ATOMIC_SECTION();
    uint16_t copied = 0;
    if (self->r_count > 0) {
        int16_t *src = pdm_ring[self->r_pop];
        // Copy + sum-of-squares in one pass (float add uses the FPU).
        float acc = 0.0f;
        for (uint16_t i = 0; i < n; i++) {
            int16_t s = src[i];
            dst[i] = s;
            acc += (float)s * (float)s;
        }
        self->last_ms = acc / (float)n;
        self->r_pop = (uint8_t)((self->r_pop + 1) % PDM_RING_DEPTH);
        self->r_count--;
        copied = n;
    }
    MICROPY_END_ATOMIC_SECTION(irq_state);
    return mp_obj_new_int((mp_int_t)copied * (mp_int_t)sizeof(int16_t));
}

static mp_obj_t nrf_pdm_rms(mp_obj_t self_in) {
    nrf_pdm_obj_t *self = (nrf_pdm_obj_t *)MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_float(self->last_ms);
}

static mp_obj_t nrf_pdm_overruns(mp_obj_t self_in) {
    nrf_pdm_obj_t *self = (nrf_pdm_obj_t *)MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->overruns);
}

// --- Section: method table ---
static MP_DEFINE_CONST_FUN_OBJ_1(nrf_pdm_start_obj, nrf_pdm_start);
static MP_DEFINE_CONST_FUN_OBJ_1(nrf_pdm_stop_obj, nrf_pdm_stop);
static MP_DEFINE_CONST_FUN_OBJ_1(nrf_pdm_deinit_obj, nrf_pdm_deinit);
static MP_DEFINE_CONST_FUN_OBJ_2(nrf_pdm_readinto_obj, nrf_pdm_readinto);
static MP_DEFINE_CONST_FUN_OBJ_1(nrf_pdm_rms_obj, nrf_pdm_rms);
static MP_DEFINE_CONST_FUN_OBJ_1(nrf_pdm_overruns_obj, nrf_pdm_overruns);

static const mp_rom_map_elem_t nrf_pdm_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_start),    MP_ROM_PTR(&nrf_pdm_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop),     MP_ROM_PTR(&nrf_pdm_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),   MP_ROM_PTR(&nrf_pdm_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&nrf_pdm_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_rms),      MP_ROM_PTR(&nrf_pdm_rms_obj) },
    { MP_ROM_QSTR(MP_QSTR_overruns), MP_ROM_PTR(&nrf_pdm_overruns_obj) },
};
static MP_DEFINE_CONST_DICT(nrf_pdm_locals_dict, nrf_pdm_locals_dict_table);

// --- Section: type / module registration ---
MP_DEFINE_CONST_OBJ_TYPE(
    nrf_pdm_type,
    MP_QSTR_PDM,
    MP_TYPE_FLAG_NONE,
    make_new, nrf_pdm_make_new,
    locals_dict, &nrf_pdm_locals_dict
    );

static const mp_rom_map_elem_t mp_module_nrf_pdm_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_nrf_pdm) },
    { MP_ROM_QSTR(MP_QSTR_PDM),      MP_ROM_PTR(&nrf_pdm_type) },
};
static MP_DEFINE_CONST_DICT(mp_module_nrf_pdm_globals, mp_module_nrf_pdm_globals_table);

const mp_obj_module_t mp_module_nrf_pdm = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_nrf_pdm_globals,
};

MP_REGISTER_MODULE(MP_QSTR_nrf_pdm, mp_module_nrf_pdm);

#endif // NRFX_PDM_ENABLED
