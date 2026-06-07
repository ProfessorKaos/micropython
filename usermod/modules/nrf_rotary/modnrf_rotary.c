#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "shared/runtime/mpirq.h"
#include "extmod/machine_pinbase.c"

typedef struct _nrf_rotary_obj_t {
    mp_obj_base_t base;
    mp_obj_t pin_a_obj;
    mp_obj_t pin_b_obj;
    mp_hal_pin_obj_t pin_a;
    mp_hal_pin_obj_t pin_b;
    volatile int32_t value;
    volatile uint8_t prev_state;
    volatile uint8_t changed;
    bool invert;
    uint8_t divisor;
} nrf_rotary_obj_t;

static inline int pin_read(mp_hal_pin_obj_t pin) {
    return mp_hal_pin_read(pin) ? 1 : 0;
}

static inline uint8_t rotary_state(mp_hal_pin_obj_t a, mp_hal_pin_obj_t b) {
    return (pin_read(a) << 1) | pin_read(b);
}

// Transition table for quadrature decoding.
// index = (old_state << 2) | new_state
static const int8_t trans_table[16] = {
    0, -1,  1,  0,
    1,  0,  0, -1,
   -1,  0,  0,  1,
    0,  1, -1,  0
};

static void nrf_rotary_process(nrf_rotary_obj_t *self) {
    uint8_t new_state = rotary_state(self->pin_a, self->pin_b);
    uint8_t idx = (self->prev_state << 2) | new_state;
    int8_t delta = trans_table[idx];
    if (delta) {
        if (self->invert) {
            delta = -delta;
        }
        self->value += delta;
        self->changed = 1;
    }
    self->prev_state = new_state;
}

static mp_obj_t nrf_rotary_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)n_args;
    (void)n_kw;
    (void)args;
    nrf_rotary_process(MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}

static mp_obj_t nrf_rotary_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum {
        ARG_pin_a,
        ARG_pin_b,
        ARG_divisor,
        ARG_invert,
        ARG_pullups,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_pin_a, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_pin_b, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_divisor, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 4} },
        { MP_QSTR_invert, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_pullups, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    nrf_rotary_obj_t *self = mp_obj_malloc(nrf_rotary_obj_t, type);

    self->pin_a_obj = args[ARG_pin_a].u_obj;
    self->pin_b_obj = args[ARG_pin_b].u_obj;
    self->pin_a = MP_OBJ_TO_PTR(args[ARG_pin_a].u_obj);
    self->pin_b = MP_OBJ_TO_PTR(args[ARG_pin_b].u_obj);
    self->value = 0;
    self->changed = 0;
    self->invert = args[ARG_invert].u_bool;
    self->divisor = args[ARG_divisor].u_int ? args[ARG_divisor].u_int : 4;

    nrf_gpio_pin_pull_t pull = args[ARG_pullups].u_bool ? NRF_GPIO_PIN_PULLUP : NRF_GPIO_PIN_NOPULL;
    nrf_gpio_cfg(self->pin_a->pin,
        NRF_GPIO_PIN_DIR_INPUT,
        NRF_GPIO_PIN_INPUT_CONNECT,
        pull,
        NRF_GPIO_PIN_S0S1,
        NRF_GPIO_PIN_NOSENSE);
    nrf_gpio_cfg(self->pin_b->pin,
        NRF_GPIO_PIN_DIR_INPUT,
        NRF_GPIO_PIN_INPUT_CONNECT,
        pull,
        NRF_GPIO_PIN_S0S1,
        NRF_GPIO_PIN_NOSENSE);

    // Optional pullups are configured here if requested.
    self->prev_state = rotary_state(self->pin_a, self->pin_b);

    mp_obj_t dest[3];
    mp_load_method(self->pin_a_obj, MP_QSTR_irq, dest);
    dest[2] = MP_OBJ_FROM_PTR(self);
    mp_call_method_n_kw(1, 0, dest);

    mp_load_method(self->pin_b_obj, MP_QSTR_irq, dest);
    dest[2] = MP_OBJ_FROM_PTR(self);
    mp_call_method_n_kw(1, 0, dest);

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t nrf_rotary_value(size_t n_args, const mp_obj_t *args) {
    nrf_rotary_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    if (n_args == 2) {
        self->value = mp_obj_get_int(args[1]) * self->divisor;
        return mp_const_none;
    }

    return mp_obj_new_int(self->value / self->divisor);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(nrf_rotary_value_obj, 1, 2, nrf_rotary_value);

static mp_obj_t nrf_rotary_raw(mp_obj_t self_in) {
    nrf_rotary_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->value);
}
static MP_DEFINE_CONST_FUN_OBJ_1(nrf_rotary_raw_obj, nrf_rotary_raw);

static mp_obj_t nrf_rotary_reset(mp_obj_t self_in) {
    nrf_rotary_obj_t *self = MP_OBJ_TO_PTR(self_in);
    self->value = 0;
    self->changed = 0;
    self->prev_state = rotary_state(self->pin_a, self->pin_b);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(nrf_rotary_reset_obj, nrf_rotary_reset);

static mp_obj_t nrf_rotary_changed(mp_obj_t self_in) {
    nrf_rotary_obj_t *self = MP_OBJ_TO_PTR(self_in);
    bool c = self->changed;
    self->changed = 0;
    return mp_obj_new_bool(c);
}
static MP_DEFINE_CONST_FUN_OBJ_1(nrf_rotary_changed_obj, nrf_rotary_changed);

static void nrf_rotary_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    nrf_rotary_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Rotary(value=%ld, raw=%ld)", (long)(self->value / self->divisor), (long)self->value);
}

static const mp_rom_map_elem_t nrf_rotary_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_value), MP_ROM_PTR(&nrf_rotary_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_raw), MP_ROM_PTR(&nrf_rotary_raw_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&nrf_rotary_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_changed), MP_ROM_PTR(&nrf_rotary_changed_obj) },
};
static MP_DEFINE_CONST_DICT(nrf_rotary_locals_dict, nrf_rotary_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    nrf_rotary_type,
    MP_QSTR_Rotary,
    MP_TYPE_FLAG_NONE,
    print, nrf_rotary_print,
    call, nrf_rotary_call,
    make_new, nrf_rotary_make_new,
    locals_dict, &nrf_rotary_locals_dict
    );

static const mp_rom_map_elem_t mp_module_nrf_rotary_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_nrf_rotary) },
    { MP_ROM_QSTR(MP_QSTR_Rotary), MP_ROM_PTR(&nrf_rotary_type) },
};

static MP_DEFINE_CONST_DICT(mp_module_nrf_rotary_globals, mp_module_nrf_rotary_globals_table);

const mp_obj_module_t mp_module_nrf_rotary = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_nrf_rotary_globals,
};

MP_REGISTER_MODULE(MP_QSTR_nrf_rotary, mp_module_nrf_rotary);