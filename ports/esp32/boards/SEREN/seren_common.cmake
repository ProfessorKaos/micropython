# Shared SEREN board configuration: single source of truth for all SEREN boards.
#
# Layering:
#   BOARD         -> chip (SEREN_C3 / SEREN_C6 / SEREN_S3)
#   BOARD_VARIANT -> feature profile (WIFI / BLE / WIFI_BLE), optionally with a
#                    4MiB-flash suffix (_4M) or legacy aliases (OTA, FLASH_4M,
#                    SPIRAM_OCT).
#
# Profiles select frozen content via boards/SEREN/profiles/<name>.py and are
# declared with seren_profile().  4MiB-flash modules append sdkconfig.flash_4m
# via seren_flash_4m().

get_filename_component(SEREN_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR} ABSOLUTE)

# Default workspace path for the frozen manifest. The build scripts (build_c3.sh,
# build_s3.sh, ...) override this via CMAKE_ARGS so the correct worktree is
# always used. When building from the ESP-IDF VS Code extension (which bypasses
# the scripts), fall back to the dev worktree so the frozen content step doesn't
# fail.
if(NOT MICROPY_MANIFEST_WORKSPACE_PATH)
    set(MICROPY_MANIFEST_WORKSPACE_PATH "/home/alexx/Zen-Specs/Code_device.worktrees/dev")
endif()

# The frozen manifest entry point is always the thin per-board wrapper, which
# passes the chip name and forwards $(SEREN_PROFILE) to the shared manifests.
# A command-line MICROPY_FROZEN_MANIFEST still takes precedence (handled by the
# top-level CMakeLists.txt).
set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)

# Bake the build variant into the firmware: manifest_common.py freezes this as
# the build_variant module, and the app uses VARIANT to fetch its own OTA
# manifest ({model}_{branch}[_{variant}].json). Empty for the default profile.
file(WRITE ${CMAKE_BINARY_DIR}/build_variant.py "VARIANT = \"${MICROPY_BOARD_VARIANT}\"\n")
set(MICROPY_MANIFEST_BUILD_DIR ${CMAKE_BINARY_DIR})

# seren_profile(<name>): select the feature profile for this build. <name>
# must match a file in boards/SEREN/profiles/.
macro(seren_profile name)
    set(MICROPY_MANIFEST_SEREN_PROFILE ${name})
endmacro()

# seren_flash_4m(): target a 4MiB-flash module. Overrides the flash size and
# partition table selected by the board/OTA fragments (must be called after
# SDKCONFIG_DEFAULTS is populated).
macro(seren_flash_4m)
    list(APPEND SDKCONFIG_DEFAULTS ${SEREN_COMMON_DIR}/sdkconfig.flash_4m)
    list(APPEND MICROPY_DEF_BOARD MICROPY_HW_BOARD_NAME="4MB Flash")
endmacro()
