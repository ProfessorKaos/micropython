# Board variant: ESP32_GENERIC_S3 with Opus audio encoder usermod
#
# Build:
#   idf.py -B build-OPUS \
#          -D MICROPY_BOARD=ESP32_GENERIC_S3 \
#          -D MICROPY_BOARD_VARIANT=OPUS \
#          build
#
# The base mpconfigboard.cmake already selects:
#   boards/sdkconfig.base + boards/sdkconfig.ble + boards/sdkconfig.spiram_sx
# This variant inherits those and adds the opus_enc C module.

list(APPEND MICROPY_DEF_BOARD
    MICROPY_HW_BOARD_NAME="Generic ESP32S3 module with Opus encoder"
)

# Locate the opus_enc usermod relative to this file:
# this file lives at ports/esp32/boards/ESP32_GENERIC_S3/
# micropython root is four levels up
get_filename_component(_MICROPY_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../.. ABSOLUTE)

set(USER_C_MODULES
    ${_MICROPY_ROOT}/usermod/opus_enc/micropython.cmake
)
