# ES7210 — MicroPython Audio ADC Driver

4-channel audio ADC configuration module for ESP32 (ESP-IDF).  
Audio data flows through the separate `machine.I2S` class — this module handles codec setup only.

## Files

| File | Purpose |
|------|---------|
| `es7210.c` / `es7210.h` | Low-level ESP-IDF driver (Espressif esp-extra-components), register read/write, clock divider tables |
| `es7210_reg.h` | Register address definitions |
| `micropython_es7210.c` | MicroPython C wrapper exposing the `ES7210` class |
| `micropython.cmake` | CMake build registration |
| `README.md` | This file |

## Requirements

- **ESP32 port** with `MICROPY_PY_MACHINE_I2S_MCK` enabled (added by this module's setup)
- **I2C** bus for codec configuration (provided by the caller via `machine.I2C`)
- **I2S** with MCK output for audio capture (via `machine.I2S(mck=...)`)
- No Arduino dependencies

## Build

### Via board variant (ESP32-S3)

```bash
cd ports/esp32
idf.py -B build-ES7210 -D MICROPY_BOARD=ESP32_GENERIC_S3 \
       -D MICROPY_BOARD_VARIANT=ES7210 build
```

### Via your own board

In your board's `mpconfigboard.cmake`:

```cmake
get_filename_component(_MICROPY_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../.. ABSOLUTE)
set(USER_C_MODULES
    ${_MICROPY_ROOT}/usermod/es7210/micropython.cmake
)
```

### Via command line

```bash
cd ports/esp32
idf.py -B build -D MICROPY_BOARD=YOUR_BOARD \
       -D USER_C_MODULES=/path/to/usermod/es7210/micropython.cmake build
```

---

## API Reference

```
import es7210
```

### `ES7210` class

#### Constructor

```python
codec = es7210.ES7210(i2c, *, addr=0x40)
```

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `i2c` | `machine.I2C` | *(required)* | Initialised `machine.I2C` or `machine.SoftI2C` object |
| `addr` | `int` | `0x40` | 7-bit I2C slave address (see table below) |

The caller is responsible for creating and managing the `machine.I2C` bus.
The module does **not** install or uninstall any I2C driver — it reuses the
provided bus for all communication with the ES7210.

Valid addresses (selected by AD1/AD0 pins):

| AD1 | AD0 | Address |
|-----|-----|---------|
| GND | GND | `0x40` |
| GND | 3.3V | `0x41` |
| 3.3V | GND | `0x42` |
| 3.3V | 3.3V | `0x43` |

#### `codec.init(...)`

Full initialisation. Performs software reset then configures all parameters. Re-entrant.

```python
codec.init(
    *,
    sample_rate=48000,
    mclk_ratio=256,
    i2s_format=es7210.I2S_FMT_I2S,
    bit_width=es7210.I2S_BITS_16B,
    mic_bias=es7210.MIC_BIAS_2V87,
    mic_gain=es7210.MIC_GAIN_30DB,
    tdm_enable=False,
)
```

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `sample_rate` | `int` | `48000` | 8k, 11.025k, 12k, 16k, 22.05k, 24k, 32k, 44.1k, 48k, 64k, 88.2k, 96k |
| `mclk_ratio` | `int` | `256` | MCLK ÷ sample_rate (must match a known coefficient) |
| `i2s_format` | `int` | `I2S_FMT_I2S` | See format constants below |
| `bit_width` | `int` | `I2S_BITS_16B` | See bit-width constants below |
| `mic_bias` | `int` | `MIC_BIAS_2V87` | See bias constants below |
| `mic_gain` | `int` | `MIC_GAIN_30DB` | See gain constants below |
| `tdm_enable` | `bool` | `False` | Enable 1×FS TDM for 4-channel capture |

Raises `OSError` if the sample rate / MCLK combination is unsupported.

#### `codec.volume(db)`

```python
codec.volume(db)    # db: -95 … +32  (0.5 dB steps, 0 = 0 dB)
```

Sets digital volume on all four ADC channels.

#### `codec.mic_gain(gain, *, channel=0)`

```python
codec.mic_gain(es7210.MIC_GAIN_30DB)          # all channels
codec.mic_gain(es7210.MIC_GAIN_30DB, channel=1)  # MIC1 only
```

| Argument | Range | Description |
|----------|-------|-------------|
| `gain` | 0–14 | One of the `MIC_GAIN_*` constants |
| `channel` | 0–4 | 0 = all (default), 1–4 = specific MIC |

#### `codec.mic_bias(bias)`

```python
codec.mic_bias(es7210.MIC_BIAS_2V87)
```

Sets bias voltage for all channels.

#### `codec.reset()`

Software reset — returns the codec to power-on state.

#### `codec.deinit()`

Deletes the codec handle.  The I2C bus is **not** touched — the caller
owns the `machine.I2C` object and must deinit it separately (or let the
garbage collector handle it).
Called automatically by the garbage collector (`__del__`).

#### Property getters

```python
codec.sample_rate()   # → int
codec.bit_width()     # → int
codec.i2s_format()    # → int
```

Returns values cached from the last `init()` call (0 if not yet called).

---

### Constants

#### I2S Formats

| Constant | Value | Description |
|----------|-------|-------------|
| `es7210.I2S_FMT_I2S` | `0x00` | Standard I2S (Philips) |
| `es7210.I2S_FMT_LJ` | `0x01` | Left-justified |
| `es7210.I2S_FMT_DSP_A` | `0x03` | DSP-A mode |
| `es7210.I2S_FMT_DSP_B` | `0x13` | DSP-B mode |

#### I2S Bit Widths

| Constant | Value |
|----------|-------|
| `es7210.I2S_BITS_16B` | 16 |
| `es7210.I2S_BITS_18B` | 18 |
| `es7210.I2S_BITS_20B` | 20 |
| `es7210.I2S_BITS_24B` | 24 |
| `es7210.I2S_BITS_32B` | 32 |

#### MIC Gain

| Constant | Gain | Constant | Gain |
|----------|------|----------|------|
| `MIC_GAIN_0DB` | 0 dB | `MIC_GAIN_18DB` | 18 dB |
| `MIC_GAIN_3DB` | 3 dB | `MIC_GAIN_21DB` | 21 dB |
| `MIC_GAIN_6DB` | 6 dB | `MIC_GAIN_24DB` | 24 dB |
| `MIC_GAIN_9DB` | 9 dB | `MIC_GAIN_27DB` | 27 dB |
| `MIC_GAIN_12DB` | 12 dB | `MIC_GAIN_30DB` | 30 dB |
| `MIC_GAIN_15DB` | 15 dB | `MIC_GAIN_33DB` | 33 dB |
| `MIC_GAIN_34_5DB` | 34.5 dB | `MIC_GAIN_36DB` | 36 dB |
| `MIC_GAIN_37_5DB` | 37.5 dB | | |

#### MIC Bias

| Constant | Voltage | Constant | Voltage |
|----------|---------|----------|---------|
| `MIC_BIAS_2V18` | 2.18 V | `MIC_BIAS_2V55` | 2.55 V |
| `MIC_BIAS_2V26` | 2.26 V | `MIC_BIAS_2V66` | 2.66 V |
| `MIC_BIAS_2V36` | 2.36 V | `MIC_BIAS_2V78` | 2.78 V |
| `MIC_BIAS_2V45` | 2.45 V | `MIC_BIAS_2V87` | 2.87 V |

---

## Usage Example

```python
import machine
import es7210
from machine import I2S, Pin

# Create and manage I2C bus at application level
# (share this bus with other I2C peripherals as needed)
i2c = machine.I2C(0, scl=18, sda=19, freq=400000)

# Pass the bus to the ES7210 module — no internal I2C driver management
codec = es7210.ES7210(i2c, addr=0x40)
codec.init(
    sample_rate=48000,
    mic_gain=es7210.MIC_GAIN_30DB,
    mic_bias=es7210.MIC_BIAS_2V87,
)

# Capture audio via I2S
audio = I2S(
    0,
    sck=Pin(5),
    ws=Pin(6),
    sd=Pin(7),
    mck=Pin(8),          # MCK output to ES7210
    mode=I2S.RX,
    bits=16,
    format=I2S.MONO,
    rate=48000,
    ibuf=4096,
)

buf = bytearray(2048)
audio.readinto(buf)
```

## Error Codes

| Code | ESP-IDF Constant | Meaning |
|------|------------------|---------|
| `0x101` | `ESP_ERR_INVALID_ARG` | Bad argument |
| `0x102` | `ESP_ERR_NO_MEM` | Heap exhausted |
| `0x103` | `ESP_ERR_INVALID_STATE` | I2C driver conflict |
| `0x107` | `ESP_ERR_TIMEOUT` | I2C timeout — check wiring |
| `0x10C` | `ESP_ERR_NOT_SUPPORTED` | Unsupported sample rate |
