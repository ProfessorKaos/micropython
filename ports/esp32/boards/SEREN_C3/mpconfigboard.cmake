set(IDF_TARGET esp32c3)
include(${CMAKE_CURRENT_LIST_DIR}/../SEREN/seren_common.cmake)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.ble
    boards/ESP32_GENERIC_C3/sdkconfig.c3usb
    ${SEREN_COMMON_DIR}/sdkconfig.ota_4m
)

# Default profile: WiFi + config server. All SEREN_C3 modules have 4MiB flash.
seren_profile(WIFI)

# Override WLAN implementation
# set(MICROPY_SOURCE_BOARD
#     ${SEREN_COMMON_DIR}/network_wlan_clamped.c
# )