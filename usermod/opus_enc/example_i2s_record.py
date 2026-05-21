# example_i2s_record.py
#
# Record audio from an INMP441 I2S MEMS microphone and encode it with Opus
# in real time on an ESP32-S3.
#
# Wiring (adjust pins to match your board):
#   INMP441 SCK  → GPIO4  (bit clock)
#   INMP441 WS   → GPIO5  (word select / LR clock)
#   INMP441 SD   → GPIO6  (data out from mic)
#   INMP441 L/R  → GND    (selects left-channel slot — required for mono)
#   INMP441 VDD  → 3.3 V
#   INMP441 GND  → GND
#
# Output file format:
#   Each frame written as: [uint16 LE packet_len][opus_packet_data]
#   The file is a simple concatenation of length-prefixed Opus packets.
#   To decode on a PC (requires opus-tools):
#     python3 strip_to_raw.py out.opus | opusdec --rate 16000 - out.wav
#   Or use the companion decode_stream.py script.
#
# Tested: ESP32-S3 with quad PSRAM, MicroPython built with opus_enc module.

from machine import I2S, Pin
import opus_enc
import time

# ---------------------------------------------------------------------------
# Pin configuration — edit to match your board
# ---------------------------------------------------------------------------
SCK_PIN = 4   # BCLK
WS_PIN  = 5   # LRCLK
SD_PIN  = 6   # Data in from mic

# ---------------------------------------------------------------------------
# Audio configuration
# ---------------------------------------------------------------------------
SAMPLE_RATE    = 16000         # Hz — must match Opus encoder
CHANNELS       = 1             # Mono — INMP441 L/R pin to GND = left channel
BITS           = 16            # 16-bit captures top 16-bits of INMP441's 24-bit word
RECORD_SECONDS = 10            # How long to record

# Encoder — created first so FRAME_BYTES is derived from it, not hardcoded.
# Default frame_ms=40 (40 ms frames → encode() called half as often vs 20 ms).
enc = opus_enc.OpusEncoder(SAMPLE_RATE, CHANNELS)
enc.set_bitrate(BITRATE)
enc.set_complexity(COMPLEXITY)

FRAME_BYTES    = enc.frame_bytes()   # e.g. 1280 at 16kHz mono 40ms

# I2S internal DMA buffer: 4 frames keeps the DMA from starving while
# we do the Opus encode.
I2S_IBUF_BYTES = FRAME_BYTES * 4

# ---------------------------------------------------------------------------
# Opus encoder configuration
# ---------------------------------------------------------------------------
BITRATE    = 16000   # bits/second — 16 kbps gives excellent voice quality
COMPLEXITY = 5       # 0 (fastest/worst) … 10 (slowest/best); 3–6 for real-time

# ---------------------------------------------------------------------------
# Set up I2S receiver
# ---------------------------------------------------------------------------
mic = I2S(
    0,
    sck=Pin(SCK_PIN),
    ws=Pin(WS_PIN),
    sd=Pin(SD_PIN),
    mode=I2S.RX,
    bits=BITS,
    format=I2S.MONO,
    rate=SAMPLE_RATE,
    ibuf=I2S_IBUF_BYTES,
)

# ---------------------------------------------------------------------------
# Set up Opus encoder
# ---------------------------------------------------------------------------
enc.set_dtx(True)          # Emit tiny packets during silence — saves space
enc.set_inband_fec(False)  # FEC adds overhead; enable if transmitting over lossy WiFi/UDP

# ---------------------------------------------------------------------------
# Allocate reusable buffers (avoids GC pressure in the hot loop)
# ---------------------------------------------------------------------------
pcm_buf = bytearray(FRAME_BYTES)
out_buf  = bytearray(512)   # Max Opus packet is 1275 bytes; 512 is plenty for 16kbps voice

# ---------------------------------------------------------------------------
# Record loop
# ---------------------------------------------------------------------------
OUTPUT_FILE    = "recording.opus"
total_frames   = RECORD_SECONDS * (SAMPLE_RATE // enc.frame_size())
total_pcm_bytes = 0
total_enc_bytes = 0

print(f"Recording {RECORD_SECONDS}s at {SAMPLE_RATE}Hz mono to '{OUTPUT_FILE}' ...")
print(f"  Frame size : {FRAME_SAMPLES} samples = {FRAME_BYTES} bytes PCM per encode()")
print(f"  Bitrate    : {BITRATE} bps")
print(f"  Frames     : {total_frames}")

t0 = time.ticks_ms()

with open(OUTPUT_FILE, "wb") as f:
    for i in range(total_frames):
        # Block until one full 20 ms frame has arrived from the DMA buffer.
        mic.readinto(pcm_buf)

        # Encode the PCM frame to an Opus packet.
        # n includes the 2-byte length prefix written at the start of out_buf.
        n = enc.encode(pcm_buf, out_buf)

        # Write the length-prefixed packet to the file.
        f.write(out_buf[:n])

        total_pcm_bytes += FRAME_BYTES
        total_enc_bytes += n

elapsed_ms = time.ticks_diff(time.ticks_ms(), t0)

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
mic.deinit()
enc.__del__()  # free encoder memory immediately

ratio = total_pcm_bytes / total_enc_bytes if total_enc_bytes else 0
print(f"\nDone in {elapsed_ms} ms.")
print(f"  Raw PCM   : {total_pcm_bytes} bytes")
print(f"  Opus      : {total_enc_bytes} bytes  ({ratio:.1f}x compression)")
print(f"  Saved to  : {OUTPUT_FILE}")
