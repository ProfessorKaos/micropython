set(IDF_TARGET esp32c6)
include(${CMAKE_CURRENT_LIST_DIR}/../SEREN/seren_common.cmake)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.c6
    boards/sdkconfig.ble
    ${SEREN_COMMON_DIR}/sdkconfig.ota_4m
    ${MICROPY_BOARD_DIR}/sdkconfig.board
)

# Default profile: BLE + OTA-over-BLE (all SEREN_C6 modules have 4MiB flash).
seren_profile(BLE)

# Override WLAN implementation
# set(MICROPY_SOURCE_BOARD
#     ${SEREN_COMMON_DIR}/network_wlan_clamped.c
# )