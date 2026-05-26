// opus_enc.c — MicroPython C module wrapping the Opus encoder
//
// Exposes:
//   import opus_enc
//   enc = opus_enc.OpusEncoder(sample_rate, channels, application=opus_enc.VOIP, frame_ms=40)
//   enc.set_bitrate(16000)
//   enc.set_complexity(5)
//   enc.set_dtx(True)
//   n = enc.encode(pcm_buf, out_buf)  # returns bytes written (2-byte length prefix + packet)
//
// Output format: [uint16 LE packet_len][packet_data ...]
// pcm_buf must be exactly frame_size * channels * 2 bytes (16-bit signed samples).
// frame_ms must be 10, 20, 40, or 60.  frame_size = sample_rate * frame_ms / 1000.

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objarray.h"
#include "py/mperrno.h"
#include "py/binary.h"

#include <string.h>
#include <stdint.h>

// codec-opus/arduino-libopus public header
#include "opus.h"

// ---------------------------------------------------------------------------
// Instance structure
// ---------------------------------------------------------------------------

typedef struct _opus_encoder_obj_t {
    mp_obj_base_t base;
    OpusEncoder  *enc;
    int           sample_rate;
    int           channels;
    int           frame_size;   // samples per frame
} opus_encoder_obj_t;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

extern const mp_obj_type_t opus_encoder_type;

// ---------------------------------------------------------------------------
// Helper: map an Opus error code to a MicroPython exception
// ---------------------------------------------------------------------------

static void check_opus_error(int err) {
    if (err < 0) {
        const char *msg = opus_strerror(err);
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("Opus error: %s"), msg);
    }
}

// ---------------------------------------------------------------------------
// OpusEncoder.__new__ / __init__
//
//   OpusEncoder(sample_rate, channels, application=VOIP, frame_ms=20, signal=OPUS_AUTO)
//
//   sample_rate  : 8000 | 12000 | 16000 | 24000 | 48000
//   channels     : 1 (mono) or 2 (stereo)
//   application  : opus_enc.VOIP | opus_enc.AUDIO | opus_enc.RESTRICTED_LOWDELAY
//   frame_ms     : 10 | 20 | 40 | 60  (Opus frame duration in milliseconds)
//   signal       : opus_enc.SIGNAL_VOICE | opus_enc.SIGNAL_MUSIC | opus_enc.AUTO
// ---------------------------------------------------------------------------

static mp_obj_t opus_encoder_make_new(const mp_obj_type_t *type,
                                       size_t n_args, size_t n_kw,
                                       const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 2, 5, false);

    int sample_rate  = mp_obj_get_int(args[0]);
    int channels     = mp_obj_get_int(args[1]);
    int application  = (n_args >= 3) ? mp_obj_get_int(args[2]) : OPUS_APPLICATION_VOIP;
    int frame_ms     = (n_args >= 4) ? mp_obj_get_int(args[3]) : 40;
    int signal       = (n_args >= 5) ? mp_obj_get_int(args[4]) : OPUS_AUTO;

    // Validate sample rate
    if (sample_rate != 8000  && sample_rate != 12000 &&
        sample_rate != 16000 && sample_rate != 24000 &&
        sample_rate != 48000) {
        mp_raise_ValueError(MP_ERROR_TEXT("sample_rate must be 8000/12000/16000/24000/48000"));
    }

    // Validate channels
    if (channels != 1 && channels != 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("channels must be 1 or 2"));
    }

    // Validate application
    if (application != OPUS_APPLICATION_VOIP &&
        application != OPUS_APPLICATION_AUDIO &&
        application != OPUS_APPLICATION_RESTRICTED_LOWDELAY) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid application type"));
    }

    // Validate signal type
    if (signal != OPUS_AUTO &&
        signal != OPUS_SIGNAL_VOICE &&
        signal != OPUS_SIGNAL_MUSIC) {
        mp_raise_ValueError(MP_ERROR_TEXT("signal must be AUTO, SIGNAL_VOICE, or SIGNAL_MUSIC"));
    }

    // Validate frame duration
    if (frame_ms != 10 && frame_ms != 20 && frame_ms != 40 && frame_ms != 60) {
        mp_raise_ValueError(MP_ERROR_TEXT("frame_ms must be 10/20/40/60"));
    }

    int err = OPUS_OK;
    OpusEncoder *enc = opus_encoder_create(sample_rate, channels, application, &err);
    if (err != OPUS_OK || enc == NULL) {
        check_opus_error(err);
        mp_raise_OSError(MP_ENOMEM);
    }

    opus_encoder_obj_t *self = mp_obj_malloc(opus_encoder_obj_t, type);
    self->enc         = enc;
    self->sample_rate = sample_rate;
    self->channels    = channels;
    self->frame_size  = sample_rate * frame_ms / 1000;

    opus_encoder_ctl(enc, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(0));
    opus_encoder_ctl(enc, OPUS_SET_SIGNAL(signal));

    return MP_OBJ_FROM_PTR(self);
}

// ---------------------------------------------------------------------------
// OpusEncoder.__del__
// ---------------------------------------------------------------------------

static mp_obj_t opus_encoder_del(mp_obj_t self_in) {
    opus_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->enc != NULL) {
        opus_encoder_destroy(self->enc);
        self->enc = NULL;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(opus_encoder_del_obj, opus_encoder_del);

// ---------------------------------------------------------------------------
// OpusEncoder.encode(pcm_buf, out_buf) -> int
//
//   pcm_buf  : buffer of exactly frame_size * channels * 2 bytes
//              (16-bit signed PCM, interleaved if stereo)
//   out_buf  : writable buffer, at least frame_size bytes (256+ recommended)
//
//   Returns the number of bytes written to out_buf, which includes a 2-byte
//   little-endian length prefix followed by the raw Opus packet:
//
//     out_buf[0:2]  = uint16 LE  packet length N
//     out_buf[2:2+N] = Opus packet
//
//   Raises OSError on encoder failure.
// ---------------------------------------------------------------------------

static mp_obj_t opus_encoder_encode(mp_obj_t self_in,
                                     mp_obj_t pcm_in,
                                     mp_obj_t out_in) {
    opus_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->enc == NULL) {
        mp_raise_OSError(MP_EBADF);   // encoder was already destroyed
    }

    // --- pcm input buffer (read-only accepted) ---
    mp_buffer_info_t pcm_buf;
    mp_get_buffer_raise(pcm_in, &pcm_buf, MP_BUFFER_READ);

    size_t expected_pcm = (size_t)(self->frame_size * self->channels * 2);
    if (pcm_buf.len != expected_pcm) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("pcm_buf must be %d bytes (%d samples * %d ch * 2)"),
            (int)expected_pcm, self->frame_size, self->channels);
    }

    // --- output buffer (must be writable) ---
    mp_buffer_info_t out_buf;
    mp_get_buffer_raise(out_in, &out_buf, MP_BUFFER_WRITE);

    // Need at least 2 bytes for the length prefix plus some packet space.
    if (out_buf.len < 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("out_buf too small"));
    }

    uint8_t *out_data = (uint8_t *)out_buf.buf;
    opus_int32 max_data_bytes = (opus_int32)(out_buf.len - 2);

    // Encode — the PCM input is treated as 16-bit signed integers.
    opus_int32 encoded = opus_encode(self->enc,
                                     (const opus_int16 *)pcm_buf.buf,
                                     self->frame_size,
                                     out_data + 2,          // leave room for length prefix
                                     max_data_bytes);

    if (encoded < 0) {
        check_opus_error((int)encoded);
    }

    // Write 2-byte little-endian length prefix.
    out_data[0] = (uint8_t)(encoded & 0xFF);
    out_data[1] = (uint8_t)((encoded >> 8) & 0xFF);

    return mp_obj_new_int(encoded + 2);
}
static MP_DEFINE_CONST_FUN_OBJ_3(opus_encoder_encode_obj, opus_encoder_encode);

// ---------------------------------------------------------------------------
// OpusEncoder.set_bitrate(bps)
//
//   bps : target bitrate in bits/second, or -1 for auto, -1000 for max
//         Typical voice: 8000–24000. OPUS_AUTO = -1000.
// ---------------------------------------------------------------------------

static mp_obj_t opus_encoder_set_bitrate(mp_obj_t self_in, mp_obj_t bps_in) {
    opus_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    opus_int32 bps = (opus_int32)mp_obj_get_int(bps_in);
    int err = opus_encoder_ctl(self->enc, OPUS_SET_BITRATE(bps));
    check_opus_error(err);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(opus_encoder_set_bitrate_obj, opus_encoder_set_bitrate);

// ---------------------------------------------------------------------------
// OpusEncoder.set_complexity(level)
//
//   level : 0 (lowest CPU, worst quality) … 10 (highest CPU, best quality)
//           Default is 9. For real-time on ESP32-S3, 3–6 is a safe range.
// ---------------------------------------------------------------------------

static mp_obj_t opus_encoder_set_complexity(mp_obj_t self_in, mp_obj_t level_in) {
    opus_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    opus_int32 level = (opus_int32)mp_obj_get_int(level_in);
    if (level < 0 || level > 10) {
        mp_raise_ValueError(MP_ERROR_TEXT("complexity must be 0-10"));
    }
    int err = opus_encoder_ctl(self->enc, OPUS_SET_COMPLEXITY(level));
    check_opus_error(err);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(opus_encoder_set_complexity_obj, opus_encoder_set_complexity);

// ---------------------------------------------------------------------------
// OpusEncoder.set_signal(signal_type)
//
//   signal_type : opus_enc.SIGNAL_VOICE — tells the encoder to use the SILK
//                 voice path; opus_enc.SIGNAL_MUSIC — prefers CELT music path;
//                 opus_enc.AUTO — let the encoder auto-detect (default).
// ---------------------------------------------------------------------------

static mp_obj_t opus_encoder_set_signal(mp_obj_t self_in, mp_obj_t signal_in) {
    opus_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    opus_int32 val = (opus_int32)mp_obj_get_int(signal_in);
    if (val != OPUS_AUTO && val != OPUS_SIGNAL_VOICE && val != OPUS_SIGNAL_MUSIC) {
        mp_raise_ValueError(MP_ERROR_TEXT("signal must be AUTO, SIGNAL_VOICE, or SIGNAL_MUSIC"));
    }
    int err = opus_encoder_ctl(self->enc, OPUS_SET_SIGNAL(val));
    check_opus_error(err);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(opus_encoder_set_signal_obj, opus_encoder_set_signal);

// ---------------------------------------------------------------------------
// OpusEncoder.set_dtx(enable)
//
//   enable : True to enable Discontinuous Transmission (silence suppression).
//            When enabled, the encoder outputs very small packets during silence
//            — ideal for voice transmission over a network.
// ---------------------------------------------------------------------------

static mp_obj_t opus_encoder_set_dtx(mp_obj_t self_in, mp_obj_t enable_in) {
    opus_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    opus_int32 val = mp_obj_is_true(enable_in) ? 1 : 0;
    int err = opus_encoder_ctl(self->enc, OPUS_SET_DTX(val));
    check_opus_error(err);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(opus_encoder_set_dtx_obj, opus_encoder_set_dtx);

// ---------------------------------------------------------------------------
// OpusEncoder.set_inband_fec(enable)
//
//   enable : True to embed forward-error-correction redundancy in each packet.
//            Helps recover from packet loss in lossy networks (e.g. UDP/WiFi).
// ---------------------------------------------------------------------------

static mp_obj_t opus_encoder_set_inband_fec(mp_obj_t self_in, mp_obj_t enable_in) {
    opus_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    opus_int32 val = mp_obj_is_true(enable_in) ? 1 : 0;
    int err = opus_encoder_ctl(self->enc, OPUS_SET_INBAND_FEC(val));
    check_opus_error(err);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(opus_encoder_set_inband_fec_obj, opus_encoder_set_inband_fec);

// ---------------------------------------------------------------------------
// OpusEncoder.frame_size (read-only property via __repr__ shortcut)
//
//   Returns the number of PCM samples per encode() call (20 ms worth).
//   At 16kHz this is 320; at 48kHz it is 960.
// ---------------------------------------------------------------------------

static mp_obj_t opus_encoder_get_frame_size(mp_obj_t self_in) {
    opus_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->frame_size);
}
static MP_DEFINE_CONST_FUN_OBJ_1(opus_encoder_get_frame_size_obj, opus_encoder_get_frame_size);

// ---------------------------------------------------------------------------
// OpusEncoder.frame_bytes (read-only property)
//
//   Convenience: frame_size * channels * 2  — the exact number of bytes
//   that encode()'s pcm_buf argument must be.
// ---------------------------------------------------------------------------

static mp_obj_t opus_encoder_get_frame_bytes(mp_obj_t self_in) {
    opus_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->frame_size * self->channels * 2);
}
static MP_DEFINE_CONST_FUN_OBJ_1(opus_encoder_get_frame_bytes_obj, opus_encoder_get_frame_bytes);

// ---------------------------------------------------------------------------
// OpusEncoder locals dict (methods table)
// ---------------------------------------------------------------------------

static const mp_rom_map_elem_t opus_encoder_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__),        MP_ROM_PTR(&opus_encoder_del_obj) },
    { MP_ROM_QSTR(MP_QSTR_encode),         MP_ROM_PTR(&opus_encoder_encode_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_bitrate),    MP_ROM_PTR(&opus_encoder_set_bitrate_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_complexity), MP_ROM_PTR(&opus_encoder_set_complexity_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_signal),      MP_ROM_PTR(&opus_encoder_set_signal_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_dtx),        MP_ROM_PTR(&opus_encoder_set_dtx_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_inband_fec), MP_ROM_PTR(&opus_encoder_set_inband_fec_obj) },
    { MP_ROM_QSTR(MP_QSTR_frame_size),     MP_ROM_PTR(&opus_encoder_get_frame_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_frame_bytes),    MP_ROM_PTR(&opus_encoder_get_frame_bytes_obj) },
};
static MP_DEFINE_CONST_DICT(opus_encoder_locals_dict, opus_encoder_locals_dict_table);

// ---------------------------------------------------------------------------
// OpusEncoder type object
// ---------------------------------------------------------------------------

MP_DEFINE_CONST_OBJ_TYPE(
    opus_encoder_type,
    MP_QSTR_OpusEncoder,
    MP_TYPE_FLAG_NONE,
    make_new, opus_encoder_make_new,
    locals_dict, &opus_encoder_locals_dict
);

// ---------------------------------------------------------------------------
// Module globals table
// ---------------------------------------------------------------------------

static const mp_rom_map_elem_t mp_module_opus_enc_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),            MP_ROM_QSTR(MP_QSTR_opus_enc) },

    // Class
    { MP_ROM_QSTR(MP_QSTR_OpusEncoder),         MP_ROM_PTR(&opus_encoder_type) },

    // Application-type constants (passed to OpusEncoder constructor)
    { MP_ROM_QSTR(MP_QSTR_VOIP),
      MP_ROM_INT(OPUS_APPLICATION_VOIP) },
    { MP_ROM_QSTR(MP_QSTR_AUDIO),
      MP_ROM_INT(OPUS_APPLICATION_AUDIO) },
    { MP_ROM_QSTR(MP_QSTR_RESTRICTED_LOWDELAY),
      MP_ROM_INT(OPUS_APPLICATION_RESTRICTED_LOWDELAY) },

    // Convenience bitrate constants
    { MP_ROM_QSTR(MP_QSTR_BITRATE_AUTO),        MP_ROM_INT(OPUS_AUTO) },
    { MP_ROM_QSTR(MP_QSTR_BITRATE_MAX),         MP_ROM_INT(OPUS_BITRATE_MAX) },

    // Signal type constants (for set_signal / constructor signal arg)
    { MP_ROM_QSTR(MP_QSTR_AUTO),                MP_ROM_INT(OPUS_AUTO) },
    { MP_ROM_QSTR(MP_QSTR_SIGNAL_VOICE),        MP_ROM_INT(OPUS_SIGNAL_VOICE) },
    { MP_ROM_QSTR(MP_QSTR_SIGNAL_MUSIC),        MP_ROM_INT(OPUS_SIGNAL_MUSIC) },
};
static MP_DEFINE_CONST_DICT(mp_module_opus_enc_globals, mp_module_opus_enc_globals_table);

// ---------------------------------------------------------------------------
// Module object
// ---------------------------------------------------------------------------

const mp_obj_module_t mp_module_opus_enc = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_opus_enc_globals,
};

// Register the module so it can be imported as `import opus_enc`.
MP_REGISTER_MODULE(MP_QSTR_opus_enc, mp_module_opus_enc);
