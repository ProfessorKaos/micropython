/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Glenn Ruben Bakke
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "py/runtime.h"
#include "extmod/modmachine.h"
#include "timer.h"
#include "nrfx_timer.h"

#if MICROPY_PY_MACHINE_TIMER_NRF

enum {
    TIMER_MODE_ONESHOT,
    TIMER_MODE_PERIODIC,
};

typedef struct _machine_timer_obj_t {
    mp_obj_base_t base;
    nrfx_timer_t p_instance;
} machine_timer_obj_t;

// Callback storage: one callback per CC channel per timer.
// Sized at runtime based on the number of static timer objects below.

static const machine_timer_obj_t machine_timer_obj[] = {
    {{&machine_timer_type}, NRFX_TIMER_INSTANCE(0)},
    #if MICROPY_PY_MACHINE_SOFT_PWM
    { },
    #else
    {{&machine_timer_type}, NRFX_TIMER_INSTANCE(1)},
    #endif
    {{&machine_timer_type}, NRFX_TIMER_INSTANCE(2)},
    #if defined(NRF52_SERIES)
    {{&machine_timer_type}, NRFX_TIMER_INSTANCE(3)},
    {{&machine_timer_type}, NRFX_TIMER_INSTANCE(4)},
    #endif
};

// 4 compare channels per timer (CC0..CC3). Initialize to NULL.
// 4 compare channels per timer (CC0..CC3).
typedef struct _timer_callback_t {
    mp_obj_t func;
    mp_obj_t arg;
} timer_callback_t;

// Initialize to {NULL, NULL}.
static timer_callback_t machine_timer_callbacks[MP_ARRAY_SIZE(machine_timer_obj)][4] = {{{NULL, NULL}}};

void timer_init0(void) {
    for (int i = 0; i < MP_ARRAY_SIZE(machine_timer_obj); i++) {
        nrfx_timer_uninit(&machine_timer_obj[i].p_instance);
    }
}

static int timer_find(mp_obj_t id) {
    // given an integer id
    int timer_id = mp_obj_get_int(id);
    if (timer_id >= 0 && timer_id < MP_ARRAY_SIZE(machine_timer_obj)) {
        return timer_id;
    }
    mp_raise_ValueError(MP_ERROR_TEXT("Timer doesn't exist"));
}

static void timer_print(const mp_print_t *print, mp_obj_t o, mp_print_kind_t kind) {
    machine_timer_obj_t *self = o;
    mp_printf(print, "Timer(%u)", self->p_instance.instance_id);
}

static void timer_event_handler(nrf_timer_event_t event_type, void *p_context) {
    machine_timer_obj_t *self = p_context;
    uint8_t timer_id = self->p_instance.instance_id;

    // Map compare events to CC channel index (COMPARE0..COMPARE3)
    if (event_type >= NRF_TIMER_EVENT_COMPARE0 && event_type <= NRF_TIMER_EVENT_COMPARE3) {
        /* NRF_TIMER_EVENT_COMPAREn are defined as register offsets (bytes),
           so compute index by dividing by 4 (size of 32-bit register). */
        uint8_t channel = (uint8_t)((event_type - NRF_TIMER_EVENT_COMPARE0) / sizeof(uint32_t));
        if (channel < 4) {
            timer_callback_t *cb = &machine_timer_callbacks[timer_id][channel];
            if (cb->func != NULL) {
                if (cb->arg != mp_const_none) {
                    mp_call_function_2(cb->func, self, cb->arg);
                } else {
                    mp_call_function_1(cb->func, self);
                }
            }
        }
    }
}

/******************************************************************************/
/* MicroPython bindings for machine API                                       */

static mp_obj_t machine_timer_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_id, ARG_period, ARG_mode, ARG_callback, ARG_callback_arg };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id,       MP_ARG_OBJ, {.u_obj = MP_OBJ_NEW_SMALL_INT(-1)} },
        { MP_QSTR_period,   MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1000000} }, // 1 second
        { MP_QSTR_mode,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = TIMER_MODE_PERIODIC} },
        { MP_QSTR_callback, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_callback_arg, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // get static peripheral object
    int timer_id = timer_find(args[ARG_id].u_obj);

    #if BLUETOOTH_SD
    if (timer_id == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("Timer reserved by Bluetooth LE stack"));
    }
    #endif

    #if MICROPY_PY_MACHINE_SOFT_PWM
    if (timer_id == 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("Timer reserved by ticker driver"));
    }
    #endif

    machine_timer_obj_t *self = (machine_timer_obj_t *)&machine_timer_obj[timer_id];

    if (mp_obj_is_fun(args[ARG_callback].u_obj)) {
        machine_timer_callbacks[timer_id][0].func = args[ARG_callback].u_obj;
        machine_timer_callbacks[timer_id][0].arg = args[ARG_callback_arg].u_obj;
    } else if (args[ARG_callback].u_obj == mp_const_none) {
        machine_timer_callbacks[timer_id][0].func = NULL;
        machine_timer_callbacks[timer_id][0].arg = mp_const_none;
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("callback must be a function"));
    }

    // Timer peripheral usage:
    // Every timer instance has a number of capture/compare (CC) registers.
    // These can store either the value to compare against (to trigger an
    // interrupt or a shortcut) or store a value returned from a
    // capture/compare event.
    // We use channel 0 for comparing (to trigger the callback and clear
    // shortcut) and channel 1 for capturing the current time.

    const nrfx_timer_config_t config = {
        .frequency = 1000000,
        .mode = NRF_TIMER_MODE_TIMER,
        .bit_width = NRF_TIMER_BIT_WIDTH_24,
        #ifdef NRF51
        .interrupt_priority = 3,
        #else
        .interrupt_priority = 6,
        #endif
        .p_context = self,
    };

    // Initialize the drive.
    // When it is already initialized, this is a no-op.
    nrfx_timer_init(&self->p_instance, &config, timer_event_handler);

    // Configure channel 0.
    nrf_timer_short_mask_t short_mask = NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK |
        ((args[ARG_mode].u_int == TIMER_MODE_ONESHOT) ? NRF_TIMER_SHORT_COMPARE0_STOP_MASK : 0);
    bool enable_interrupts = true;
    nrfx_timer_extended_compare(
        &self->p_instance,
        NRF_TIMER_CC_CHANNEL0,
        args[ARG_period].u_int,
        short_mask,
        enable_interrupts);

    return MP_OBJ_FROM_PTR(self);
}

/// \method period()
/// Return counter value, which is currently in us.
///
static mp_obj_t machine_timer_period(mp_obj_t self_in) {
    machine_timer_obj_t *self = MP_OBJ_TO_PTR(self_in);

    uint32_t period = nrfx_timer_capture(&self->p_instance, NRF_TIMER_CC_CHANNEL1);

    return MP_OBJ_NEW_SMALL_INT(period);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_timer_period_obj, machine_timer_period);

/// \method start()
/// Start the timer.
///
static mp_obj_t machine_timer_start(mp_obj_t self_in) {
    machine_timer_obj_t *self = MP_OBJ_TO_PTR(self_in);

    nrfx_timer_enable(&self->p_instance);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_timer_start_obj, machine_timer_start);

/// \method stop()
/// Stop the timer.
///
static mp_obj_t machine_timer_stop(mp_obj_t self_in) {
    machine_timer_obj_t *self = MP_OBJ_TO_PTR(self_in);

    nrfx_timer_disable(&self->p_instance);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_timer_stop_obj, machine_timer_stop);

/// \method deinit()
/// Free resources associated with the timer.
///
static mp_obj_t machine_timer_deinit(mp_obj_t self_in) {
    machine_timer_obj_t *self = MP_OBJ_TO_PTR(self_in);

    nrfx_timer_uninit(&self->p_instance);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_timer_deinit_obj, machine_timer_deinit);

/// \method compare(channel, value, callback)
/// Configure a compare channel with callback
///
static mp_obj_t machine_timer_compare(size_t n_args, const mp_obj_t *args) {
    enum { ARG_self, ARG_channel, ARG_value, ARG_callback };
    machine_timer_obj_t *self = MP_OBJ_TO_PTR(args[ARG_self]);
    uint8_t channel = mp_obj_get_int(args[ARG_channel]);
    uint32_t value = mp_obj_get_int(args[ARG_value]);
    mp_obj_t callback = args[ARG_callback];
    mp_obj_t callback_arg = mp_const_none;

    if (n_args >= 5) {
        callback_arg = args[4];
    }

    // Validate channel (0-3)
    if (channel > 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("Channel must be 0-3"));
    }

    // Store callback
    if (mp_obj_is_fun(callback)) {
        machine_timer_callbacks[self->p_instance.instance_id][channel].func = callback;
        machine_timer_callbacks[self->p_instance.instance_id][channel].arg = callback_arg;
    } else if (callback == mp_const_none) {
        machine_timer_callbacks[self->p_instance.instance_id][channel].func = NULL;
        machine_timer_callbacks[self->p_instance.instance_id][channel].arg = mp_const_none;
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("Callback must be a function"));
    }

    // Configure the compare channel and enable interrupts
    nrfx_timer_extended_compare(
        &self->p_instance,
        (nrf_timer_cc_channel_t)channel,
        value,
        0, // no shortcuts by default for non-primary channels
        true);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_timer_compare_obj, 4, 5, machine_timer_compare);

static const mp_rom_map_elem_t machine_timer_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_time),     MP_ROM_PTR(&machine_timer_period_obj) }, // alias
    { MP_ROM_QSTR(MP_QSTR_period),   MP_ROM_PTR(&machine_timer_period_obj) },
    { MP_ROM_QSTR(MP_QSTR_start),    MP_ROM_PTR(&machine_timer_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop),     MP_ROM_PTR(&machine_timer_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),   MP_ROM_PTR(&machine_timer_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_compare),  MP_ROM_PTR(&machine_timer_compare_obj) },  // NEW

    // constants
    { MP_ROM_QSTR(MP_QSTR_ONESHOT),  MP_ROM_INT(TIMER_MODE_ONESHOT) },
    { MP_ROM_QSTR(MP_QSTR_PERIODIC), MP_ROM_INT(TIMER_MODE_PERIODIC) },
};

static MP_DEFINE_CONST_DICT(machine_timer_locals_dict, machine_timer_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_timer_type,
    MP_QSTR_Timer,
    MP_TYPE_FLAG_NONE,
    make_new, machine_timer_make_new,
    print, timer_print,
    locals_dict, &machine_timer_locals_dict
    );

#endif // MICROPY_PY_MACHINE_TIMER_NRF
