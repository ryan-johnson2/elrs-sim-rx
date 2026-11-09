cmake_minimum_required(VERSION 3.13)

# Board: Waveshare RP2040-Zero (and its clones). Override with -DPICO_BOARD=pico etc.
set(PICO_BOARD waveshare_rp2040_zero CACHE STRING "Board type")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(elrs_sim_rx C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(FW_VERSION "0.1.0" CACHE STRING "Firmware version string")
set(PRODUCT_NAME "ELRS Sim RX" CACHE STRING "USB product string and console banner")

add_executable(elrs_sim_rx
    src/main.c
    src/usb_descriptors.c
    src/cli.c
    src/rx_uart.c
    src/rx_power.c
    src/config_store.c
    core/crc.c
    core/crsf.c
    core/mapping.c
    core/failsafe.c
    core/led_seq.c
    core/button.c
)

target_include_directories(elrs_sim_rx PRIVATE src core)

target_compile_definitions(elrs_sim_rx PRIVATE
    FW_VERSION="${FW_VERSION}"
    PRODUCT_NAME="${PRODUCT_NAME}"
)

target_compile_options(elrs_sim_rx PRIVATE -Wall -Wextra $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>)

target_link_libraries(elrs_sim_rx
    pico_stdlib
    pico_unique_id
    pico_bootrom
    hardware_uart
    hardware_irq
    hardware_gpio
    hardware_flash
    hardware_sync
    hardware_watchdog
    tinyusb_device
    tinyusb_board
)

# UART0 is the CRSF link, not a console.
pico_enable_stdio_uart(elrs_sim_rx 0)
pico_enable_stdio_usb(elrs_sim_rx 0)

pico_add_extra_outputs(elrs_sim_rx)
