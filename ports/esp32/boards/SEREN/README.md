# Shared SEREN board layer

Single source of truth for all SEREN boards (`SEREN_C3`, `SEREN_C6`,
`SEREN_S3`). Change firmware content here once and every board picks it up.

## Layering

| Axis | Mechanism | Values |
| --- | --- | --- |
| Chip | `BOARD` (thin `boards/SEREN_Cx/` dir) | `SEREN_C3`, `SEREN_C6`, `SEREN_S3` |
| Features | `BOARD_VARIANT` -> `seren_profile()` | `WIFI`, `BLE`, `WIFI_BLE` |
| Flash size | `seren_flash_4m()` (S3 only) | 8MiB default, 4MiB via `_4M` variants |

Profiles control the frozen manifest (and thus which app modules, web assets
and micropython-lib packages go into the image):

| Profile | WiFi stack | BLE | config server + static |
| --- | --- | --- | --- |
| `WIFI` | yes | no | yes |
| `BLE` | no | yes | no |
| `WIFI_BLE` | yes | yes | yes |

## Files

- `seren_common.cmake` -- shared cmake: `WORKSPACE_PATH` default, manifest
  entry point, `seren_profile()` / `seren_flash_4m()` helpers
- `manifest_common.py` -- parameterized frozen-content manifest
- `profiles/<name>.py` -- feature flags for each profile
- `sdkconfig.ota_4m` / `sdkconfig.ota_8m` -- OTA partition tables + size trims
- `sdkconfig.flash_4m` -- 4MiB flash override (partition table + flash size)
- `network_wlan_clamped.c` -- alternative WLAN implementation (enabled via
  `MICROPY_SOURCE_BOARD` in the board's `mpconfigboard.cmake` if ever needed)
- `check_configs.py` -- matrix validation (see below)

Chip-specific bits stay in `boards/SEREN_Cx/`: `IDF_TARGET`, `mpconfigboard.h`,
chip sdkconfig quirks and user C modules.

## Building

From `ports/esp32`, with an exported ESP-IDF environment:

```sh
make BOARD=SEREN_C3                    # WiFi + config server (default profile)
make BOARD=SEREN_C3 BOARD_VARIANT=BLE

make BOARD=SEREN_C6                    # BLE (default profile)
make BOARD=SEREN_C6 BOARD_VARIANT=WIFI

make BOARD=SEREN_S3                    # WiFi + BLE + config server (default profile)
make BOARD=SEREN_S3 BOARD_VARIANT=WIFI # WiFi + config server, no BLE
make BOARD=SEREN_S3 BOARD_VARIANT=BLE  # BLE only
make BOARD=SEREN_S3 BOARD_VARIANT=FLASH_4M   # 4MiB-flash modules (BLE profile)
make BOARD=SEREN_S3 BOARD_VARIANT=SPIRAM_OCT # octal-SPIRAM modules
```

Each combination builds into its own `build-<BOARD>` / `build-<BOARD>-<BOARD_VARIANT>`
directory, so switching profiles never contaminates a previous build. Firmware
images end up in that directory as `micropython.bin` etc., ready for the usual
`idf.py -B <build-dir> flash` / esptool flow.

## Adding a profile

1. Create `profiles/<NAME>.py` with the desired `include("../manifest_common.py", ...)`
   flags.
2. Add `boards/SEREN_Cx/mpconfigvariant_<NAME>.cmake` containing
   `seren_profile(<NAME>)` (plus `seren_flash_4m()` for 4MiB-flash builds).
3. List `<NAME>` in that board's `board.json` `"variants"`.
4. Run `check_configs.py` -- it verifies all of the above stay in sync.

## Legacy variant names

`OTA` is kept in each board as an alias for its default profile so existing
build scripts (`BOARD_VARIANT=OTA`) keep working. `FLASH_4M` (SEREN_S3) is the
legacy name for a BLE-profile build on 4MiB flash. 4MiB-flash builds use
~1.7MiB app partitions; if a feature combination's image exceeds that, use a
leaner profile.

## Validation

```sh
python3 ports/esp32/boards/SEREN/check_configs.py            # static checks
python3 ports/esp32/boards/SEREN/check_configs.py --manifests # + execute every manifest combination (needs `make -C mpy-cross`)
python3 ports/esp32/boards/SEREN/check_configs.py --configure # + cmake configure smoke (needs exported ESP-IDF)
```
