# QMA6100P — MicroPython Accelerometer Driver

3-axis accelerometer module for ESP32 (ESP-IDF).
Adapted from the [Espressif esp-bsp QMA6100P component](https://github.com/espressif/esp-bsp/tree/master/components/qma6100p).

## Files

| File                            | Purpose                                              |
| ------------------------------- | ---------------------------------------------------- |
| `qma6100p.c` / `qma6100p.h` | Low-level driver with I2C transport abstraction      |
| `micropython_qma6100p.c`      | MicroPython C wrapper exposing the`QMA6100P` class |
| `micropython.cmake`           | CMake build registration                             |
| `README.md`                   | This file                                            |

## Requirements

- **ESP32-S3** (or any ESP32 variant with I2C)
- **I2C** bus for sensor communication (provided by the caller via `machine.I2C`)
- No Arduino dependencies

## Python API

```python
from machine import I2C
import qma6100p

# Initialise I2C (SEREN_S3: SCL=9, SDA=8)
i2c = I2C(0, scl=9, sda=8)

# Create sensor object (default address 0x12, AD0 low)
sensor = qma6100p.QMA6100P(i2c)

# Or with AD0 high (address 0x13)
sensor = qma6100p.QMA6100P(i2c, addr=0x13)

# Configure with full-scale range and start reading
sensor.init(fs=qma6100p.ACCE_FS_4G)

# Read acceleration in g (floats)
x, y, z = sensor.acceleration()
print(f"X={x:.3f}g  Y={y:.3f}g  Z={z:.3f}g")

# Read raw acceleration (integer LSB counts)
rx, ry, rz = sensor.raw_acceleration()

# Other methods
print(f"Device ID: 0x{sensor.device_id():02X}")  # Should be 0x90
print(f"Sensitivity: {sensor.sensitivity()} LSB/g")

# Power management
sensor.sleep()
sensor.wake_up()

# Cleanup
sensor.deinit()
```

## Constants

| Constant        | Value | Description                          |
| --------------- | ----- | ------------------------------------ |
| `ACCE_FS_2G`  | 1     | ±2 g range (sensitivity 4096 LSB/g) |
| `ACCE_FS_4G`  | 2     | ±4 g range (sensitivity 2048 LSB/g) |
| `ACCE_FS_8G`  | 4     | ±8 g range (sensitivity 1024 LSB/g) |
| `ACCE_FS_16G` | 8     | ±16 g range (sensitivity 512 LSB/g) |
| `ACCE_FS_32G` | 15    | ±32 g range (sensitivity 256 LSB/g) |
| `ADDR_LOW`    | 0x12  | I2C address with AD0 pin low         |
| `ADDR_HIGH`   | 0x13  | I2C address with AD0 pin high        |

## Build

### Via board (SEREN_S3)

The SEREN_S3 board already includes this module. Just build:

```bash
cd ports/esp32
idf.py -B build -D MICROPY_BOARD=SEREN_S3 build
```

### Via command line

```bash
cd ports/esp32
idf.py -B build -D MICROPY_BOARD=YOUR_BOARD \
       -D USER_C_MODULES=/path/to/usermod/qma6100p/micropython.cmake build
```

### Via board's mpconfigboard.cmake

```cmake
get_filename_component(_MICROPY_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../.. ABSOLUTE)
set(USER_C_MODULES
    ${_MICROPY_ROOT}/usermod/qma6100p/micropython.cmake
)
```

## Design Notes

- Uses a **transport abstraction** (function pointers) for I2C read/write, decoupled from the ESP-IDF I2C master bus API. This allows the driver to work with MicroPython's `machine.I2C` protocol directly.
- The `init()` method performs the full startup sequence: wake up → NVM load → configure full-scale range.
- The `__del__` finaliser ensures the driver handle is freed if the user forgets to call `deinit()`.
- The I2C bus object is pinned for garbage collection to prevent use-after-free.
