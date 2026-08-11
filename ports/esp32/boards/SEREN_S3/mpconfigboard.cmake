set(IDF_TARGET esp32s3)
set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)



set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.ble
    boards/sdkconfig.spiram_sx
    boards/SEREN_S3/sdkconfig.board
    boards/SEREN_S3/sdkconfig.ota
)

# Override WLAN implementation
# set(MICROPY_SOURCE_BOARD
#     ${MICROPY_BOARD_DIR}/network_wlan_clamped.c
# )

# QMA6100P accelerometer user C module
get_filename_component(_MICROPY_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../.. ABSOLUTE)
set(USER_C_MODULES
    ${_MICROPY_ROOT}/usermod/qma6100p/micropython.cmake
)