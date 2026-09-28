set(IDF_TARGET esp32c3)
set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)

# Default workspace path for the frozen manifest. The build scripts (build_c3.sh)
# override this via CMAKE_ARGS so the correct worktree is always used.
# When building from the ESP-IDF VS Code extension (which bypasses the scripts),
# fall back to the dev worktree so the frozen content step doesn't fail.
if(NOT MICROPY_MANIFEST_WORKSPACE_PATH)
    set(MICROPY_MANIFEST_WORKSPACE_PATH "/home/alexx/Zen-Specs/Code_device.worktrees/dev")
endif()

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.ble
    boards/ESP32_GENERIC_C3/sdkconfig.c3usb
    boards/SEREN_C3/sdkconfig.ota
)


# Override WLAN implementation
# set(MICROPY_SOURCE_BOARD
#     ${MICROPY_BOARD_DIR}/network_wlan_clamped.c
# )