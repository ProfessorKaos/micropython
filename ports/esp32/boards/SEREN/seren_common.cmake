# Shared SEREN board configuration: single source of truth for all SEREN boards.
#
# Layering:
#   BOARD         -> hardware: chip + flash layout
#                    (SEREN_C3 / SEREN_C6 / SEREN_S3 / SEREN_S3_4M)
#   BOARD_VARIANT -> feature profile (WIFI / BLE / WIFI_BLE) or a legacy alias
#                    (OTA, SPIRAM_OCT).
#
# Profiles select frozen content via boards/SEREN/profiles/<name>.py and are
# declared with seren_profile().

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
# Boards whose modules have a different flash layout set SEREN_FLASH_TAG
# (e.g. "4M") BEFORE including this file; the tag is appended to the baked
# variant so 4M and 8M images publish/fetch distinct manifests (e.g. a
# SEREN_S3_4M BLE build fetches s3_dev_ble_4m.json, the 8M board s3_dev_ble.json).
# build.sh derives the same suffix from the *_4M board name - keep in sync.
set(_seren_variant "${MICROPY_BOARD_VARIANT}")
if(SEREN_FLASH_TAG)
    if(_seren_variant)
        string(APPEND _seren_variant "_")
    endif()
    string(APPEND _seren_variant "${SEREN_FLASH_TAG}")
endif()
file(WRITE ${CMAKE_BINARY_DIR}/build_variant.py "VARIANT = \"${_seren_variant}\"\n")
set(MICROPY_MANIFEST_BUILD_DIR ${CMAKE_BINARY_DIR})

# seren_profile(<name>): select the feature profile for this build. <name>
# must match a file in boards/SEREN/profiles/.
macro(seren_profile name)
    set(MICROPY_MANIFEST_SEREN_PROFILE ${name})
endmacro()
