# micropython.cmake — QMA6100P accelerometer USER_C_MODULE for MicroPython ESP32
#
# Build with (ESP32-S3 example):
#   cd ports/esp32
#   idf.py -B build-QMA6100P -D MICROPY_BOARD=SEREN_S3 build
#
# Or with a custom board:
#   idf.py -B build -D MICROPY_BOARD=YOUR_BOARD \
#     -D USER_C_MODULES=/path/to/usermod/qma6100p/micropython.cmake build

if(NOT CMAKE_BUILD_EARLY_EXPANSION)

    # -------------------------------------------------------------------------
    # Create the MicroPython user module
    # -------------------------------------------------------------------------
    add_library(usermod_qma6100p INTERFACE)

    target_sources(usermod_qma6100p INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/micropython_qma6100p.c
        ${CMAKE_CURRENT_LIST_DIR}/qma6100p.c
    )

    target_include_directories(usermod_qma6100p INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    # Link both the wrapper and the low-level driver to the central usermod target.
    target_link_libraries(usermod INTERFACE usermod_qma6100p)

endif()
