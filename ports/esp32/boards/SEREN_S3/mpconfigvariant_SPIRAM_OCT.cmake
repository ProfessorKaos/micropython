# Octal-SPIRAM modules (8MiB flash): full feature set.
seren_profile(WIFI_BLE)

set(SDKCONFIG_DEFAULTS
    ${SDKCONFIG_DEFAULTS}
    boards/sdkconfig.240mhz
    boards/sdkconfig.spiram_oct
)

list(APPEND MICROPY_DEF_BOARD
    MICROPY_HW_BOARD_NAME="Generic SerenS3 module with Octal-SPIRAM"
)
