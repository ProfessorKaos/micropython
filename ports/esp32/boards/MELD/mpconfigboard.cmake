set(IDF_TARGET esp32s3)
set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.ble
    boards/sdkconfig.spiram_sx
    boards/sdkconfig.240mhz
    boards/sdkconfig.spiram_oct
    boards/MELD/sdkconfig.board
)

# Locate the es7210 usermod relative to this file:
# this file lives at ports/esp32/boards/ESP32_GENERIC_S3/
# micropython root is four levels up

get_filename_component(_MICROPY_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../.. ABSOLUTE)

set(USER_C_MODULES
    ${_MICROPY_ROOT}/usermod/es7210/micropython.cmake
    ${_MICROPY_ROOT}/usermod/opus_enc/micropython.cmake
    ${_MICROPY_ROOT}/usermod/es7210/micropython.cmake
)