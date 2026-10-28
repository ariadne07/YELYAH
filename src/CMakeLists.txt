# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(
    SRCS ${app_sources}
    REQUIRES
        esp_lcd
        esp_driver_gpio
        esp_driver_spi
        esp_driver_i2s
        esp_driver_sdspi
        esp_adc
        esp_driver_usb_serial_jtag
        esp_timer
        fatfs
        sdmmc
)
