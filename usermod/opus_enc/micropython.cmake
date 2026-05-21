# micropython.cmake — Opus encoder USER_C_MODULE for MicroPython ESP32-S3
#
# Build with:
#   cd ports/esp32
#   idf.py -B build-OPUS -D MICROPY_BOARD=ESP32_GENERIC_S3 \
#     -D USER_C_MODULES=/home/profkaos/micropython/usermod/opus_enc/micropython.cmake build

# FetchContent / add_subdirectory must be skipped during ESP-IDF's first-pass
# (component discovery) when CMAKE_BUILD_EARLY_EXPANSION is set.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)

    # -------------------------------------------------------------------------
    # 1. Pull in the codec-opus library
    #    We use the local clone (see README: git clone codec-opus/ first).
    #    If you prefer FetchContent over a local clone, replace the block below:
    #
    #    include(FetchContent)
    #    FetchContent_Declare(codec_opus
    #        GIT_REPOSITORY "https://github.com/pschatzmann/codec-opus.git"
    #        GIT_TAG main)
    #    FetchContent_GetProperties(codec_opus)
    #    if(NOT codec_opus_POPULATED)
    #        FetchContent_Populate(codec_opus)
    #        add_subdirectory(${codec_opus_SOURCE_DIR} ${codec_opus_BINARY_DIR})
    #    endif()
    # -------------------------------------------------------------------------

    if(NOT TARGET arduino_libopus)
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/codec-opus
                         ${CMAKE_BINARY_DIR}/usermod_codec_opus_build)
    endif()

    # Enable fixed-point mode and suppress float API.
    # codec-opus ships an embedded opus_config.h activated by HAVE_CONFIG_H.
    # That config already sets FIXED_POINT=1, DISABLE_FLOAT_API=1,
    # NONTHREADSAFE_PSEUDOSTACK=1, GLOBAL_STACK_SIZE=60000.
    target_compile_options(arduino_libopus PRIVATE
        -DARDUINO
        -DHAVE_CONFIG_H
        -Wno-stringop-overread
        -Wno-unused-function
        -O2
    )

    # -------------------------------------------------------------------------
    # 2. Create the MicroPython user module
    # -------------------------------------------------------------------------
    add_library(usermod_opus_enc INTERFACE)

    target_sources(usermod_opus_enc INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/opus_enc.c
    )

    target_include_directories(usermod_opus_enc INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/codec-opus/src
    )

    # Link both the wrapper and the codec to the MicroPython usermod target.
    target_link_libraries(usermod INTERFACE usermod_opus_enc arduino_libopus)

endif()
