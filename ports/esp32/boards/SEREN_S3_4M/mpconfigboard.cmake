# SEREN_S3 modules with 4MiB flash: same chip and feature profiles as
# SEREN_S3, but a 4MiB flash layout (smaller OTA app partitions, ~1.7MiB).
# The flash size goes on the BOARD axis (hardware), not the variant axis.
#
# NB: the full WIFI_BLE image currently exceeds the 4M OTA app partition
# (~1.79MB vs ~1.77MB), so the default profile here is BLE; WIFI/WIFI_BLE
# builds fail loudly at the image-size check if they don't fit.
set(IDF_TARGET esp32s3)
set(SEREN_FLASH_TAG "4M")
include(${CMAKE_CURRENT_LIST_DIR}/../SEREN/seren_common.cmake)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.ble
    boards/sdkconfig.spiram_sx
    boards/SEREN_S3/sdkconfig.board
    ${SEREN_COMMON_DIR}/sdkconfig.ota_4m
    ${SEREN_COMMON_DIR}/sdkconfig.flash_4m
)

# Default profile: BLE + OTA-over-BLE (what 4M S3 products have shipped with).
seren_profile(BLE)

# QMA6100P accelerometer user C module
get_filename_component(_MICROPY_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../.. ABSOLUTE)
set(USER_C_MODULES
    ${_MICROPY_ROOT}/usermod/qma6100p/micropython.cmake
)
