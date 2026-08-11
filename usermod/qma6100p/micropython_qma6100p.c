// micropython_qma6100p.c — MicroPython C module wrapping the QMA6100P accelerometer driver
//
// Provides:  import qma6100p
//
//   from machine import I2C
//   import qma6100p
//
//   i2c = I2C(0, scl=9, sda=8)
//   sensor = qma6100p.QMA6100P(i2c)
//   sensor.init(fs=qma6100p.ACCE_FS_4G)
//   x, y, z = sensor.acceleration()      # g
//   rx, ry, rz = sensor.raw_acceleration() # raw LSB
//   sensor.sleep()
//   sensor.deinit()
//
// The QMA6100P is an I2C 3-axis accelerometer.
// I2C bus is provided by the caller via machine.I2C — the module
// does NOT manage its own I2C driver.

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mperrno.h"

#include "esp_err.h"

#include "extmod/modmachine.h"

#include "qma6100p.h"

// ---------------------------------------------------------------------------
// Object structure
// ---------------------------------------------------------------------------

typedef struct _qma6100p_obj_t {
    mp_obj_base_t base;
    qma6100p_handle_t handle;
    mp_obj_t i2c_obj;          /* machine.I2C Python object (pinned for GC) */
    uint8_t i2c_addr;
    qma6100p_acce_fs_t fs;     /* cached full-scale range */
} qma6100p_obj_t;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

extern const mp_obj_type_t qma6100p_type;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#undef check_esp_err
static void check_esp_err(esp_err_t err) {
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("QMA6100P ESP-IDF error 0x%x"), err);
    }
}

// ---------------------------------------------------------------------------
// I2C transport: delegates to machine.I2C (or machine.SoftI2C)
// ---------------------------------------------------------------------------

static esp_err_t qma6100p_mp_i2c_write(void *ctx, uint8_t dev_addr,
                                        uint8_t reg_addr,
                                        const uint8_t *data, uint8_t len)
{
    qma6100p_obj_t *self = (qma6100p_obj_t *)ctx;

    const mp_obj_type_t *type = mp_obj_get_type(self->i2c_obj);
    const mp_machine_i2c_p_t *i2c_p =
        (const mp_machine_i2c_p_t *)MP_OBJ_TYPE_GET_SLOT(type, protocol);
    if (i2c_p == NULL || i2c_p->transfer == NULL) {
        return ESP_FAIL;
    }

    mp_obj_base_t *i2c_base = (mp_obj_base_t *)MP_OBJ_TO_PTR(self->i2c_obj);

    /* Build scatter-gather buffer: register address + data bytes */
    mp_machine_i2c_buf_t bufs[2];
    bufs[0].len = 1;
    bufs[0].buf = &reg_addr;
    bufs[1].len = len;
    bufs[1].buf = (uint8_t *)data;

    int ret = i2c_p->transfer(i2c_base, (uint16_t)dev_addr, 2, bufs,
                               MP_MACHINE_I2C_FLAG_STOP);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t qma6100p_mp_i2c_read(void *ctx, uint8_t dev_addr,
                                       uint8_t reg_addr,
                                       uint8_t *data, uint8_t len)
{
    qma6100p_obj_t *self = (qma6100p_obj_t *)ctx;

    const mp_obj_type_t *type = mp_obj_get_type(self->i2c_obj);
    const mp_machine_i2c_p_t *i2c_p =
        (const mp_machine_i2c_p_t *)MP_OBJ_TYPE_GET_SLOT(type, protocol);
    if (i2c_p == NULL || i2c_p->transfer == NULL) {
        return ESP_FAIL;
    }

    mp_obj_base_t *i2c_base = (mp_obj_base_t *)MP_OBJ_TO_PTR(self->i2c_obj);

    /* Step 1: Write register address (no stop — keeps bus active) */
    mp_machine_i2c_buf_t wbuf = {.len = 1, .buf = &reg_addr};
    int ret = i2c_p->transfer(i2c_base, (uint16_t)dev_addr, 1, &wbuf, 0);
    if (ret < 0) {
        return ESP_FAIL;
    }

    /* Step 2: Read data bytes (with stop) */
    mp_machine_i2c_buf_t rbuf = {.len = len, .buf = data};
    ret = i2c_p->transfer(i2c_base, (uint16_t)dev_addr, 1, &rbuf,
                           MP_MACHINE_I2C_FLAG_READ | MP_MACHINE_I2C_FLAG_STOP);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

// ---------------------------------------------------------------------------
// QMA6100P constructor:  QMA6100P(i2c, *, addr=0x12)
//
//   i2c  — a machine.I2C or machine.SoftI2C object (must be initialised)
//   addr — 7-bit I2C slave address (default 0x12, AD0 low)
// ---------------------------------------------------------------------------

static mp_obj_t qma6100p_make_new(const mp_obj_type_t *type,
                                   size_t n_args, size_t n_kw,
                                   const mp_obj_t *args)
{
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);

    enum { ARG_i2c, ARG_addr };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_i2c, MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_addr, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = QMA6100P_I2C_ADDRESS} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    mp_obj_t i2c_obj = parsed[ARG_i2c].u_obj;
    int i2c_addr = parsed[ARG_addr].u_int;

    /* Validate address */
    if (i2c_addr != QMA6100P_I2C_ADDRESS && i2c_addr != QMA6100P_I2C_ADDRESS_1) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("invalid addr: 0x%02x"), i2c_addr);
    }

    /* Check that the passed object implements the I2C protocol */
    const mp_obj_type_t *obj_type = mp_obj_get_type(i2c_obj);
    const mp_machine_i2c_p_t *i2c_p =
        (const mp_machine_i2c_p_t *)MP_OBJ_TYPE_GET_SLOT(obj_type, protocol);
    if (i2c_p == NULL || i2c_p->transfer == NULL) {
        mp_raise_TypeError(
            MP_ERROR_TEXT("object does not implement the I2C protocol"));
    }

    /* Allocate object with finaliser */
    qma6100p_obj_t *self = mp_obj_malloc_with_finaliser(qma6100p_obj_t, type);
    self->handle = NULL;
    self->i2c_obj = i2c_obj;
    self->i2c_addr = (uint8_t)i2c_addr;
    self->fs = ACCE_FS_2G;

    /* Build transport and create driver */
    qma6100p_i2c_transport_t transport = {
        .ctx = self,
        .write = qma6100p_mp_i2c_write,
        .read  = qma6100p_mp_i2c_read,
    };
    check_esp_err(qma6100p_create(&transport, (uint8_t)i2c_addr, &self->handle));

    return MP_OBJ_FROM_PTR(self);
}

// ---------------------------------------------------------------------------
// QMA6100P.__del__  (finaliser)
// ---------------------------------------------------------------------------

static mp_obj_t qma6100p_del(mp_obj_t self_in) {
    qma6100p_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->handle != NULL) {
        qma6100p_delete(self->handle);
        self->handle = NULL;
    }
    self->i2c_obj = MP_OBJ_NULL;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(qma6100p_del_obj, qma6100p_del);

// ---------------------------------------------------------------------------
// QMA6100P.deinit()
// ---------------------------------------------------------------------------

static mp_obj_t qma6100p_deinit(mp_obj_t self_in) {
    return qma6100p_del(self_in);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qma6100p_deinit_obj, qma6100p_deinit);

// ---------------------------------------------------------------------------
// QMA6100P.init(*, fs=ACCE_FS_2G)
//
// Configure the accelerometer full-scale range. Safe to call again.
// ---------------------------------------------------------------------------

static mp_obj_t qma6100p_init(size_t n_args, const mp_obj_t *pos_args,
                               mp_map_t *kw_args)
{
    enum { ARG_fs };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_fs, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = ACCE_FS_2G} },
    };

    qma6100p_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    int fs = parsed[ARG_fs].u_int;

    if (self->handle == NULL) {
        mp_raise_OSError(MP_EBADF);
    }

    /* Validate full-scale value */
    if (fs != ACCE_FS_2G && fs != ACCE_FS_4G && fs != ACCE_FS_8G &&
        fs != ACCE_FS_16G && fs != ACCE_FS_32G) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid full-scale range"));
    }

    /* Wake up the sensor first */
    check_esp_err(qma6100p_wake_up(self->handle));

    /* Load NVM calibration */
    check_esp_err(qma6100p_nvm_load(self->handle));

    /* Configure full-scale range */
    check_esp_err(qma6100p_config(self->handle, (qma6100p_acce_fs_t)fs));
    self->fs = (qma6100p_acce_fs_t)fs;

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(qma6100p_init_obj, 1, qma6100p_init);

// ---------------------------------------------------------------------------
// QMA6100P.acceleration()  →  (x, y, z)  floats in g
// ---------------------------------------------------------------------------

static mp_obj_t qma6100p_acceleration(mp_obj_t self_in) {
    qma6100p_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->handle == NULL) {
        mp_raise_OSError(MP_EBADF);
    }

    qma6100p_acce_value_t acce;
    check_esp_err(qma6100p_get_acce(self->handle, &acce));

    mp_obj_t tuple[3];
    tuple[0] = mp_obj_new_float((double)acce.acce_x);
    tuple[1] = mp_obj_new_float((double)acce.acce_y);
    tuple[2] = mp_obj_new_float((double)acce.acce_z);
    return mp_obj_new_tuple(3, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qma6100p_acceleration_obj, qma6100p_acceleration);

// ---------------------------------------------------------------------------
// QMA6100P.raw_acceleration()  →  (x, y, z)  integers (raw LSB counts)
// ---------------------------------------------------------------------------

static mp_obj_t qma6100p_raw_acceleration(mp_obj_t self_in) {
    qma6100p_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->handle == NULL) {
        mp_raise_OSError(MP_EBADF);
    }

    qma6100p_raw_acce_value_t raw;
    check_esp_err(qma6100p_get_raw_acce(self->handle, &raw));

    mp_obj_t tuple[3];
    tuple[0] = mp_obj_new_int(raw.raw_acce_x);
    tuple[1] = mp_obj_new_int(raw.raw_acce_y);
    tuple[2] = mp_obj_new_int(raw.raw_acce_z);
    return mp_obj_new_tuple(3, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qma6100p_raw_acceleration_obj, qma6100p_raw_acceleration);

// ---------------------------------------------------------------------------
// QMA6100P.device_id()  →  int  (WHO_AM_I value, should be 0x90)
// ---------------------------------------------------------------------------

static mp_obj_t qma6100p_device_id(mp_obj_t self_in) {
    qma6100p_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->handle == NULL) {
        mp_raise_OSError(MP_EBADF);
    }

    uint8_t id;
    check_esp_err(qma6100p_get_deviceid(self->handle, &id));
    return mp_obj_new_int(id);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qma6100p_device_id_obj, qma6100p_device_id);

// ---------------------------------------------------------------------------
// QMA6100P.sensitivity()  →  float  (LSB/g for current full-scale range)
// ---------------------------------------------------------------------------

static mp_obj_t qma6100p_sensitivity(mp_obj_t self_in) {
    qma6100p_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->handle == NULL) {
        mp_raise_OSError(MP_EBADF);
    }

    float sens;
    check_esp_err(qma6100p_get_acce_sensitivity(self->handle, &sens));
    return mp_obj_new_float((double)sens);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qma6100p_sensitivity_obj, qma6100p_sensitivity);

// ---------------------------------------------------------------------------
// QMA6100P.sleep()
// ---------------------------------------------------------------------------

static mp_obj_t qma6100p_obj_sleep(mp_obj_t self_in) {
    qma6100p_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->handle == NULL) {
        mp_raise_OSError(MP_EBADF);
    }
    check_esp_err(qma6100p_sleep(self->handle));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(qma6100p_sleep_obj, qma6100p_obj_sleep);

// ---------------------------------------------------------------------------
// QMA6100P.wake_up()
// ---------------------------------------------------------------------------

static mp_obj_t qma6100p_obj_wake_up(mp_obj_t self_in) {
    qma6100p_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->handle == NULL) {
        mp_raise_OSError(MP_EBADF);
    }
    check_esp_err(qma6100p_wake_up(self->handle));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(qma6100p_wake_up_obj, qma6100p_obj_wake_up);

// ---------------------------------------------------------------------------
// QMA6100P locals dict (method table)
// ---------------------------------------------------------------------------

static const mp_rom_map_elem_t qma6100p_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__),       MP_ROM_PTR(&qma6100p_del_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),        MP_ROM_PTR(&qma6100p_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_init),          MP_ROM_PTR(&qma6100p_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_acceleration),  MP_ROM_PTR(&qma6100p_acceleration_obj) },
    { MP_ROM_QSTR(MP_QSTR_raw_acceleration), MP_ROM_PTR(&qma6100p_raw_acceleration_obj) },
    { MP_ROM_QSTR(MP_QSTR_device_id),     MP_ROM_PTR(&qma6100p_device_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_sensitivity),   MP_ROM_PTR(&qma6100p_sensitivity_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep),         MP_ROM_PTR(&qma6100p_sleep_obj) },
    { MP_ROM_QSTR(MP_QSTR_wake_up),       MP_ROM_PTR(&qma6100p_wake_up_obj) },
};
static MP_DEFINE_CONST_DICT(qma6100p_locals_dict, qma6100p_locals_dict_table);

// ---------------------------------------------------------------------------
// QMA6100P type object
// ---------------------------------------------------------------------------

MP_DEFINE_CONST_OBJ_TYPE(
    qma6100p_type,
    MP_QSTR_QMA6100P,
    MP_TYPE_FLAG_NONE,
    make_new, qma6100p_make_new,
    locals_dict, &qma6100p_locals_dict
);

// ---------------------------------------------------------------------------
// Module globals table
// ---------------------------------------------------------------------------

static const mp_rom_map_elem_t mp_module_qma6100p_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_qma6100p) },

    /* Class */
    { MP_ROM_QSTR(MP_QSTR_QMA6100P), MP_ROM_PTR(&qma6100p_type) },

    /* Full-scale range constants */
    { MP_ROM_QSTR(MP_QSTR_ACCE_FS_2G),  MP_ROM_INT(ACCE_FS_2G) },
    { MP_ROM_QSTR(MP_QSTR_ACCE_FS_4G),  MP_ROM_INT(ACCE_FS_4G) },
    { MP_ROM_QSTR(MP_QSTR_ACCE_FS_8G),  MP_ROM_INT(ACCE_FS_8G) },
    { MP_ROM_QSTR(MP_QSTR_ACCE_FS_16G), MP_ROM_INT(ACCE_FS_16G) },
    { MP_ROM_QSTR(MP_QSTR_ACCE_FS_32G), MP_ROM_INT(ACCE_FS_32G) },

    /* I2C address constants */
    { MP_ROM_QSTR(MP_QSTR_ADDR_LOW),  MP_ROM_INT(QMA6100P_I2C_ADDRESS) },
    { MP_ROM_QSTR(MP_QSTR_ADDR_HIGH), MP_ROM_INT(QMA6100P_I2C_ADDRESS_1) },
};
static MP_DEFINE_CONST_DICT(mp_module_qma6100p_globals, mp_module_qma6100p_globals_table);

// ---------------------------------------------------------------------------
// Module object
// ---------------------------------------------------------------------------

const mp_obj_module_t mp_module_qma6100p = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_qma6100p_globals,
};

// Register the module so it can be imported as `import qma6100p`.
MP_REGISTER_MODULE(MP_QSTR_qma6100p, mp_module_qma6100p);
