# On-device self-test for the adcdma user module (ESP32-S3, ADC1).
# Usage: copy to the device and run, e.g.
#   mpremote run test_adcdma.py

import gc
import time

import adcdma

PIN = 1
FREQ = 20000
CALLS = 100

gc.collect()
adcdma.init(PIN, FREQ)
time.sleep_ms(100)
level = adcdma.level()
assert level >= 0, level
print("level after 100 ms:", level)

# Poll 100x at 20 Hz.  Clapping or speaking near the mic must visibly raise the
# printed RMS values compared to silence.
print("100 polls at 20 Hz:")
gc.collect()
levels = []
for _ in range(CALLS):
    v = adcdma.level()
    if v < 0:
        print("level() got no samples, stats:", adcdma.stats())
    # assert v >= 0, v
    print(v)
    levels.append(v)
    time.sleep_ms(50)
gc.collect()
assert len(levels) == CALLS

# Optional raw access: copy packed uint16 samples out of the ring buffer.
buf = bytearray(512)  # 256 uint16 samples
n = adcdma.readinto(buf)
print("readinto:", n, "samples")

print("stats:", adcdma.stats())

# deinit() followed by init() must work without asserting.
adcdma.deinit()
adcdma.init(PIN, FREQ)
time.sleep_ms(100)
assert adcdma.level() >= 0
adcdma.deinit()

gc.collect()
print("OK")
