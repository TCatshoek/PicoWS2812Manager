cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(wsmanager_project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(wsmanager
        ws2812.c
        )

pico_enable_stdio_usb(wsmanager 0)
pico_enable_stdio_uart(wsmanager 1)

pico_generate_pio_header(wsmanager ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/generated)

target_link_libraries(wsmanager
        pico_stdlib
        hardware_pio
        hardware_dma
        hardware_irq
        hardware_spi
        )

pico_add_extra_outputs(wsmanager)