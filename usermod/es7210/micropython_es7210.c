// micropython_es7210.c — MicroPython C module wrapping the ES7210 audio codec driver
//
// Provides:  import es7210
//
//   codec = es7210.ES7210(scl=18, sda=19)
//   codec.init(sample_rate=48000, ...)
//   codec.volume(0)
//   codec.mic_gain(es7210.MIC_GAIN_30DB, channel=1)
//   codec.mic_bias(es7210.MIC_BIAS_2V87)
//   codec.reset()
//   codec.deinit()
//
// The ES7210 is an I2C-configurable 4-channel audio ADC.
// Audio data flows through the separate machine.I2S class.

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mperrno.h"

#include "driver/i2c.h"
#include "esp_err.h"

#include "es7210.h"
#include "es7210_reg.h"

// ---------------------------------------------------------------------------
// Object structure
// ---------------------------------------------------------------------------

typedef struct _es7210_obj_t {
    mp_obj_base_t base;
    es7210_dev_handle_t handle;
    i2c_port_t i2c_port;
    uint8_t i2c_addr;
    bool i2c_driver_installed;
    // Cached config for query methods
    uint32_t sample_rate;
    es7210_i2s_bits_t bit_width;
    es7210_i2s_fmt_t i2s_format;
} es7210_obj_t;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

extern const mp_obj_type_t es7210_type;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void check_esp_err(esp_err_t err) {
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("ES7210 ESP-IDF error 0x%x"), err);
    }
}

// Write a single ES7210 register via I2C (for functions not exposed in the
// low-level driver's public API, e.g. per-channel mic gain).
static void es7210_i2c_write(es7210_obj_t *self, uint8_t reg, uint8_t val) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        mp_raise_OSError(MP_ENOMEM);
    }
    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, (self->i2c_addr << 1) | I2C_MASTER_WRITE, true));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, reg, true));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, val, true));
    ESP_ERROR_CHECK(i2c_master_stop(cmd));
    esp_err_t err = i2c_master_cmd_begin(self->i2c_port, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    check_esp_err(err);
}

// ---------------------------------------------------------------------------
// ES7210 constructor:  ES7210(scl, sda, *, i2c_port=0, i2c_addr=0x40)
// ---------------------------------------------------------------------------

static mp_obj_t es7210_make_new(const mp_obj_type_t *type,
                                 size_t n_args, size_t n_kw,
                                 const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 2, MP_OBJ_FUN_ARGS_MAX, true);

    enum { ARG_scl, ARG_sda, ARG_i2c_port, ARG_i2c_addr };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_scl, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_sda, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_i2c_port, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_i2c_addr, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = ES7210_ADDRRES_00} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    int scl = parsed[ARG_scl].u_int;
    int sda = parsed[ARG_sda].u_int;
    int i2c_port = parsed[ARG_i2c_port].u_int;
    int i2c_addr = parsed[ARG_i2c_addr].u_int;

    // Validate I2C port
    if (i2c_port < 0 || i2c_port >= I2C_NUM_MAX) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("invalid i2c_port: %d"), i2c_port);
    }

    // Validate address
    if (i2c_addr != ES7210_ADDRRES_00 && i2c_addr != ES7210_ADDRESS_01 &&
        i2c_addr != ES7210_ADDRESS_10 && i2c_addr != ES7210_ADDRESS_11) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("invalid i2c_addr: 0x%02x"), i2c_addr);
    }

    // Allocate object with finaliser
    es7210_obj_t *self = mp_obj_malloc_with_finaliser(es7210_obj_t, type);
    self->handle = NULL;
    self->i2c_port = (i2c_port_t)i2c_port;
    self->i2c_addr = (uint8_t)i2c_addr;
    self->i2c_driver_installed = false;
    self->sample_rate = 0;
    self->bit_width = ES7210_I2S_BITS_16B;
    self->i2s_format = ES7210_I2S_FMT_I2S;

    // Install I2C driver (master mode, 400 kHz)
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    check_esp_err(i2c_param_config(self->i2c_port, &i2c_conf));
    check_esp_err(i2c_driver_install(self->i2c_port, I2C_MODE_MASTER, 0, 0, 0));
    self->i2c_driver_installed = true;

    // Create codec handle (stores I2C address; does NOT touch hardware yet)
    es7210_i2c_config_t codec_i2c = {
        .i2c_port = self->i2c_port,
        .i2c_addr = self->i2c_addr,
    };
    check_esp_err(es7210_new_codec(&codec_i2c, &self->handle));

    return MP_OBJ_FROM_PTR(self);
}

// ---------------------------------------------------------------------------
// ES7210.__del__  (finaliser)
// ---------------------------------------------------------------------------

static mp_obj_t es7210_del(mp_obj_t self_in) {
    es7210_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->handle != NULL) {
        es7210_del_codec(self->handle);
        self->handle = NULL;
    }
    if (self->i2c_driver_installed) {
        i2c_driver_delete(self->i2c_port);
        self->i2c_driver_installed = false;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(es7210_del_obj, es7210_del);

// ---------------------------------------------------------------------------
// ES7210.init(*, sample_rate=48000, mclk_ratio=256, i2s_format=...,
//             bit_width=..., mic_bias=..., mic_gain=..., tdm_enable=False)
//
// Full codec initialisation.  Safe to call again to reconfigure.
// ---------------------------------------------------------------------------

static mp_obj_t es7210_init(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_sample_rate, ARG_mclk_ratio, ARG_i2s_format, ARG_bit_width,
           ARG_mic_bias, ARG_mic_gain, ARG_tdm_enable };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_sample_rate, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 48000} },
        { MP_QSTR_mclk_ratio, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 256} },
        { MP_QSTR_i2s_format, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = ES7210_I2S_FMT_I2S} },
        { MP_QSTR_bit_width, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = ES7210_I2S_BITS_16B} },
        { MP_QSTR_mic_bias, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = ES7210_MIC_BIAS_2V87} },
        { MP_QSTR_mic_gain, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = ES7210_MIC_GAIN_30DB} },
        { MP_QSTR_tdm_enable, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };

    es7210_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    uint32_t sample_rate = parsed[ARG_sample_rate].u_int;
    uint32_t mclk_ratio = parsed[ARG_mclk_ratio].u_int;
    es7210_i2s_fmt_t i2s_fmt = (es7210_i2s_fmt_t)parsed[ARG_i2s_format].u_int;
    es7210_i2s_bits_t bit_width = (es7210_i2s_bits_t)parsed[ARG_bit_width].u_int;
    es7210_mic_bias_t mic_bias = (es7210_mic_bias_t)parsed[ARG_mic_bias].u_int;
    es7210_mic_gain_t mic_gain = (es7210_mic_gain_t)parsed[ARG_mic_gain].u_int;
    bool tdm_enable = parsed[ARG_tdm_enable].u_bool;

    if (self->handle == NULL) {
        mp_raise_OSError(MP_EBADF);
    }

    es7210_codec_config_t codec_conf = {
        .sample_rate_hz = sample_rate,
        .mclk_ratio = mclk_ratio,
        .i2s_format = i2s_fmt,
        .bit_width = bit_width,
        .mic_bias = mic_bias,
        .mic_gain = mic_gain,
        .flags.tdm_enable = tdm_enable ? 1 : 0,
    };

    check_esp_err(es7210_config_codec(self->handle, &codec_conf));

    // Cache for property queries
    self->sample_rate = sample_rate;
    self->bit_width = bit_width;
    self->i2s_format = i2s_fmt;

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(es7210_init_obj, 1, es7210_init);

// ---------------------------------------------------------------------------
// ES7210.volume(db)
//
// Set output volume, range -95 .. +32 dB.
// ---------------------------------------------------------------------------

static mp_obj_t es7210_volume(mp_obj_t self_in, mp_obj_t db_in) {
    es7210_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->handle == NULL) {
        mp_raise_OSError(MP_EBADF);
    }
    int db = mp_obj_get_int(db_in);
    check_esp_err(es7210_config_volume(self->handle, (int8_t)db));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(es7210_volume_obj, es7210_volume);

// ---------------------------------------------------------------------------
// ES7210.mic_gain(gain, channel=0)
//
//   gain    : one of the es7210.MIC_GAIN_* constants (0..14)
//   channel : 0 = all four channels, 1-4 = specific MIC channel
// ---------------------------------------------------------------------------

static mp_obj_t es7210_mic_gain(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_gain, ARG_channel };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_gain, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_channel, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
    };

    es7210_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    int gain = parsed[ARG_gain].u_int;
    int channel = parsed[ARG_channel].u_int;

    if (self->handle == NULL) {
        mp_raise_OSError(MP_EBADF);
    }
    if (gain < 0 || gain > 14) {
        mp_raise_ValueError(MP_ERROR_TEXT("gain must be 0-14"));
    }

    uint8_t val = (uint8_t)(gain) | 0x10;

    if (channel == 0) {
        es7210_i2c_write(self, ES7210_MIC1_GAIN_REG43, val);
        es7210_i2c_write(self, ES7210_MIC2_GAIN_REG44, val);
        es7210_i2c_write(self, ES7210_MIC3_GAIN_REG45, val);
        es7210_i2c_write(self, ES7210_MIC4_GAIN_REG46, val);
    } else if (channel >= 1 && channel <= 4) {
        static const uint8_t gain_regs[] = {
            ES7210_MIC1_GAIN_REG43,
            ES7210_MIC2_GAIN_REG44,
            ES7210_MIC3_GAIN_REG45,
            ES7210_MIC4_GAIN_REG46,
        };
        es7210_i2c_write(self, gain_regs[channel - 1], val);
    } else {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("channel must be 0-4, got %d"), channel);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(es7210_mic_gain_obj, 1, es7210_mic_gain);

// ---------------------------------------------------------------------------
// ES7210.mic_bias(bias)
//
// Set MIC bias voltage for all channels.
//   bias : one of the es7210.MIC_BIAS_* constants
// ---------------------------------------------------------------------------

static mp_obj_t es7210_mic_bias(mp_obj_t self_in, mp_obj_t bias_in) {
    es7210_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->handle == NULL) {
        mp_raise_OSError(MP_EBADF);
    }
    int bias = mp_obj_get_int(bias_in);

    // Validate: must be one of the 8 valid bias values (0x00, 0x10, ... 0x70)
    if ((bias & 0x0F) != 0 || bias < 0 || bias > 0x70) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid mic_bias value"));
    }

    uint8_t val = (uint8_t)bias;
    es7210_i2c_write(self, ES7210_MIC12_BIAS_REG41, val);
    es7210_i2c_write(self, ES7210_MIC34_BIAS_REG42, val);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(es7210_mic_bias_obj, es7210_mic_bias);

// ---------------------------------------------------------------------------
// ES7210.reset()
// ---------------------------------------------------------------------------

static mp_obj_t es7210_reset(mp_obj_t self_in) {
    es7210_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->handle == NULL) {
        mp_raise_OSError(MP_EBADF);
    }
    check_esp_err(es7210_reset(self->handle));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(es7210_reset_obj, es7210_reset);

// ---------------------------------------------------------------------------
// ES7210.deinit()
// ---------------------------------------------------------------------------

static mp_obj_t es7210_deinit(mp_obj_t self_in) {
    return es7210_del(self_in);
}
static MP_DEFINE_CONST_FUN_OBJ_1(es7210_deinit_obj, es7210_deinit);

// ---------------------------------------------------------------------------
// Property-style getters
// ---------------------------------------------------------------------------

static mp_obj_t es7210_get_sample_rate(mp_obj_t self_in) {
    es7210_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->sample_rate);
}
static MP_DEFINE_CONST_FUN_OBJ_1(es7210_get_sample_rate_obj, es7210_get_sample_rate);

static mp_obj_t es7210_get_bit_width(mp_obj_t self_in) {
    es7210_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->bit_width);
}
static MP_DEFINE_CONST_FUN_OBJ_1(es7210_get_bit_width_obj, es7210_get_bit_width);

static mp_obj_t es7210_get_i2s_format(mp_obj_t self_in) {
    es7210_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->i2s_format);
}
static MP_DEFINE_CONST_FUN_OBJ_1(es7210_get_i2s_format_obj, es7210_get_i2s_format);

// ---------------------------------------------------------------------------
// ES7210 locals dict (method table)
// ---------------------------------------------------------------------------

static const mp_rom_map_elem_t es7210_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&es7210_del_obj) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&es7210_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_volume), MP_ROM_PTR(&es7210_volume_obj) },
    { MP_ROM_QSTR(MP_QSTR_mic_gain), MP_ROM_PTR(&es7210_mic_gain_obj) },
    { MP_ROM_QSTR(MP_QSTR_mic_bias), MP_ROM_PTR(&es7210_mic_bias_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&es7210_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&es7210_deinit_obj) },
    // Property-style getters
    { MP_ROM_QSTR(MP_QSTR_sample_rate), MP_ROM_PTR(&es7210_get_sample_rate_obj) },
    { MP_ROM_QSTR(MP_QSTR_bit_width), MP_ROM_PTR(&es7210_get_bit_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2s_format), MP_ROM_PTR(&es7210_get_i2s_format_obj) },
};
static MP_DEFINE_CONST_DICT(es7210_locals_dict, es7210_locals_dict_table);

// ---------------------------------------------------------------------------
// ES7210 type object
// ---------------------------------------------------------------------------

MP_DEFINE_CONST_OBJ_TYPE(
    es7210_type,
    MP_QSTR_ES7210,
    MP_TYPE_FLAG_NONE,
    make_new, es7210_make_new,
    locals_dict, &es7210_locals_dict
);

// ---------------------------------------------------------------------------
// Module globals table
// ---------------------------------------------------------------------------

static const mp_rom_map_elem_t mp_module_es7210_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_es7210) },

    // Class
    { MP_ROM_QSTR(MP_QSTR_ES7210), MP_ROM_PTR(&es7210_type) },

    // ── I2S format constants ──
    { MP_ROM_QSTR(MP_QSTR_I2S_FMT_I2S),   MP_ROM_INT(ES7210_I2S_FMT_I2S) },
    { MP_ROM_QSTR(MP_QSTR_I2S_FMT_LJ),    MP_ROM_INT(ES7210_I2S_FMT_LJ) },
    { MP_ROM_QSTR(MP_QSTR_I2S_FMT_DSP_A), MP_ROM_INT(ES7210_I2S_FMT_DSP_A) },
    { MP_ROM_QSTR(MP_QSTR_I2S_FMT_DSP_B), MP_ROM_INT(ES7210_I2S_FMT_DSP_B) },

    // ── I2S bit-width constants ──
    { MP_ROM_QSTR(MP_QSTR_I2S_BITS_16B), MP_ROM_INT(ES7210_I2S_BITS_16B) },
    { MP_ROM_QSTR(MP_QSTR_I2S_BITS_18B), MP_ROM_INT(ES7210_I2S_BITS_18B) },
    { MP_ROM_QSTR(MP_QSTR_I2S_BITS_20B), MP_ROM_INT(ES7210_I2S_BITS_20B) },
    { MP_ROM_QSTR(MP_QSTR_I2S_BITS_24B), MP_ROM_INT(ES7210_I2S_BITS_24B) },
    { MP_ROM_QSTR(MP_QSTR_I2S_BITS_32B), MP_ROM_INT(ES7210_I2S_BITS_32B) },

    // ── MIC gain constants (0 dB .. 37.5 dB) ──
    { MP_ROM_QSTR(MP_QSTR_MIC_GAIN_0DB),     MP_ROM_INT(ES7210_MIC_GAIN_0DB) },
    { MP_ROM_QSTR(MP_QSTR_MIC_GAIN_3DB),     MP_ROM_INT(ES7210_MIC_GAIN_3DB) },
    { MP_ROM_QSTR(MP_QSTR_MIC_GAIN_6DB),     MP_ROM_INT(ES7210_MIC_GAIN_6DB) },
    { MP_ROM_QSTR(MP_QSTR_MIC_GAIN_9DB),     MP_ROM_INT(ES7210_MIC_GAIN_9DB) },
    { MP_ROM_QSTR(MP_QSTR_MIC_GAIN_12DB),    MP_ROM_INT(ES7210_MIC_GAIN_12DB) },
    { MP_ROM_QSTR(MP_QSTR_MIC_GAIN_15DB),    MP_ROM_INT(ES7210_MIC_GAIN_15DB) },
    { MP_ROM_QSTR(MP_QSTR_MIC_GAIN_18DB),    MP_ROM_INT(ES7210_MIC_GAIN_18DB) },
    { MP_ROM_QSTR(MP_QSTR_MIC_GAIN_21DB),    MP_ROM_INT(ES7210_MIC_GAIN_21DB) },
    { MP_ROM_QSTR(MP_QSTR_MIC_GAIN_24DB),    MP_ROM_INT(ES7210_MIC_GAIN_24DB) },
    { MP_ROM_QSTR(MP_QSTR_MIC_GAIN_27DB),    MP_ROM_INT(ES7210_MIC_GAIN_27DB) },
    { MP_ROM_QSTR(MP_QSTR_MIC_GAIN_30DB),    MP_ROM_INT(ES7210_MIC_GAIN_30DB) },
    { MP_ROM_QSTR(MP_QSTR_MIC_GAIN_33DB),    MP_ROM_INT(ES7210_MIC_GAIN_33DB) },
    { MP_ROM_QSTR(MP_QSTR_MIC_GAIN_34_5DB),  MP_ROM_INT(ES7210_MIC_GAIN_34_5DB) },
    { MP_ROM_QSTR(MP_QSTR_MIC_GAIN_36DB),    MP_ROM_INT(ES7210_MIC_GAIN_36DB) },
    { MP_ROM_QSTR(MP_QSTR_MIC_GAIN_37_5DB),  MP_ROM_INT(ES7210_MIC_GAIN_37_5DB) },

    // ── MIC bias constants ──
    { MP_ROM_QSTR(MP_QSTR_MIC_BIAS_2V18), MP_ROM_INT(ES7210_MIC_BIAS_2V18) },
    { MP_ROM_QSTR(MP_QSTR_MIC_BIAS_2V26), MP_ROM_INT(ES7210_MIC_BIAS_2V26) },
    { MP_ROM_QSTR(MP_QSTR_MIC_BIAS_2V36), MP_ROM_INT(ES7210_MIC_BIAS_2V36) },
    { MP_ROM_QSTR(MP_QSTR_MIC_BIAS_2V45), MP_ROM_INT(ES7210_MIC_BIAS_2V45) },
    { MP_ROM_QSTR(MP_QSTR_MIC_BIAS_2V55), MP_ROM_INT(ES7210_MIC_BIAS_2V55) },
    { MP_ROM_QSTR(MP_QSTR_MIC_BIAS_2V66), MP_ROM_INT(ES7210_MIC_BIAS_2V66) },
    { MP_ROM_QSTR(MP_QSTR_MIC_BIAS_2V78), MP_ROM_INT(ES7210_MIC_BIAS_2V78) },
    { MP_ROM_QSTR(MP_QSTR_MIC_BIAS_2V87), MP_ROM_INT(ES7210_MIC_BIAS_2V87) },
};
static MP_DEFINE_CONST_DICT(mp_module_es7210_globals, mp_module_es7210_globals_table);

// ---------------------------------------------------------------------------
// Module object
// ---------------------------------------------------------------------------

const mp_obj_module_t mp_module_es7210 = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_es7210_globals,
};

// Register the module so it can be imported as `import es7210`.
MP_REGISTER_MODULE(MP_QSTR_es7210, mp_module_es7210);
