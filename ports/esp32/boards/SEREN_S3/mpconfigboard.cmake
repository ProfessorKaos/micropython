set(IDF_TARGET esp32s3)
include(${CMAKE_CURRENT_LIST_DIR}/../SEREN/seren_common.cmake)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.ble
    boards/sdkconfig.spiram_sx
    boards/SEREN_S3/sdkconfig.board
    ${SEREN_COMMON_DIR}/sdkconfig.ota_8m
)

# Default profile: WiFi + BLE + config server (8MiB-flash modules).
seren_profile(WIFI_BLE)

# Override WLAN implementation
# set(MICROPY_SOURCE_BOARD
#     ${SEREN_COMMON_DIR}/network_wlan_clamped.c
# )

# QMA6100P accelerometer user C module
get_filename_component(_MICROPY_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../.. ABSOLUTE)
set(USER_C_MODULES
    ${_MICROPY_ROOT}/usermod/qma6100p/micropython.cmake
)