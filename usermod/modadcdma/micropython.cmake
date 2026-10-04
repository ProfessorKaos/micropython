# micropython.cmake — adcdma DMA ADC sampling USER_C_MODULE for MicroPython ESP32
#
# Build with:
#   cd ports/esp32
#   make BOARD=ESP32_GENERIC_S3 \
#        USER_C_MODULES=/absolute/path/to/usermod/modadcdma/micropython.cmake

if(NOT CMAKE_BUILD_EARLY_EXPANSION)

    add_library(usermod_adcdma INTERFACE)

    target_sources(usermod_adcdma INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/modadcdma.c
    )

    target_include_directories(usermod_adcdma INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    # Require the esp_adc component (adc_continuous driver).  LINK_ONLY keeps
    # the link dependency out of usermod_gather_sources' include-dir walk,
    # which recurses INTERFACE_LINK_LIBRARIES and would feed IDF component
    # include paths into idf_component_register.
    target_link_libraries(usermod_adcdma INTERFACE $<LINK_ONLY:idf::esp_adc>)

    target_link_libraries(usermod INTERFACE usermod_adcdma)

endif()
