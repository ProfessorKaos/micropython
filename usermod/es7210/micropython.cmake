# micropython.cmake — ES7210 audio codec USER_C_MODULE for MicroPython ESP32
#
# Build with (ESP32-S3 example):
#   cd ports/esp32
#   idf.py -B build-ES7210 -D MICROPY_BOARD=ESP32_GENERIC_S3 \
#     -D MICROPY_BOARD_VARIANT=ES7210 build
#
# Or with a custom board:
#   idf.py -B build -D MICROPY_BOARD=YOUR_BOARD \
#     -D USER_C_MODULES=/path/to/usermod/es7210/micropython.cmake build

if(NOT CMAKE_BUILD_EARLY_EXPANSION)

    # -------------------------------------------------------------------------
    # Create the MicroPython user module
    # -------------------------------------------------------------------------
    add_library(usermod_es7210 INTERFACE)

    target_sources(usermod_es7210 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/micropython_es7210.c
        ${CMAKE_CURRENT_LIST_DIR}/es7210.c
    )

    target_include_directories(usermod_es7210 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    # Link both the wrapper and the low-level driver to the central usermod target.
    target_link_libraries(usermod INTERFACE usermod_es7210)

endif()
