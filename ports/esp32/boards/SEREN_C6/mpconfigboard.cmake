set(IDF_TARGET esp32c6)
set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.c6
    boards/sdkconfig.ble
)

# Override WLAN implementation
# set(MICROPY_SOURCE_BOARD
#     ${MICROPY_BOARD_DIR}/network_wlan_clamped.c
# )