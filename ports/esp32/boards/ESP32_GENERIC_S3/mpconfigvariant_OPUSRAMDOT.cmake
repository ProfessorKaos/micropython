set(SDKCONFIG_DEFAULTS
    ${SDKCONFIG_DEFAULTS}
    boards/sdkconfig.240mhz
    boards/sdkconfig.spiram_oct
    boards/ESP32_GENERIC/sdkconfig.ota
)


list(APPEND MICROPY_DEF_BOARD
    MICROPY_HW_BOARD_NAME="Generic ESP32S3 module with Opus encoder and Octal-SPIRAM with Microdot"
)

# Locate the opus_enc usermod relative to this file:
# this file lives at ports/esp32/boards/ESP32_GENERIC_S3/
# micropython root is four levels up
get_filename_component(_MICROPY_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../.. ABSOLUTE)

set(USER_C_MODULES
    ${_MICROPY_ROOT}/usermod/opus_enc/micropython.cmake
)
