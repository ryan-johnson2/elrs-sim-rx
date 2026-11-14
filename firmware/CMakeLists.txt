cmake_minimum_required(VERSION 3.13)

# Board: Waveshare RP2040-Zero (and its clones). Override with -DPICO_BOARD=pico etc.
set(PICO_BOARD waveshare_rp2040_zero CACHE STRING "Board type")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(elrs_sim_rx C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# tools/build.sh passes the real version, worked out from git by tools/version.sh.
set(FW_VERSION "0.0.0-unversioned" CACHE STRING "Firmware version string")
set(PRODUCT_NAME "ELRS Sim RX" CACHE STRING "USB product string and console banner")

# Receiver settings over the CRSF parameter protocol (console: rxinfo, rxparam).
# Costs about 4 KB of flash and 0.4 KB of RAM; turn off with -DELRS_RX_PARAMS=OFF.
option(ELRS_RX_PARAMS "Read and change receiver settings over CRSF" ON)

# Two builds from the same sources:
#   elrs_sim_rx        full: receiver power switch, front-panel button and LED ring
#   elrs_sim_rx_lite   lite: RP2040-Zero wired straight to the receiver (5V, GND, GP0, GP1).
#                      The receiver is on whenever USB is; the onboard LED shows link state.
function(add_firmware target lite)
    add_executable(${target}
        src/main.c
        src/usb_descriptors.c
        src/cli.c
        src/rx_uart.c
        src/rx_power.c
        src/config_store.c
        src/status_led.c
        core/crc.c
        core/crsf.c
        core/mapping.c
        core/profiles.c
        core/failsafe.c
        core/led_seq.c
        core/button.c
    )
    pico_generate_pio_header(${target} ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

    target_include_directories(${target} PRIVATE src core)

    target_compile_definitions(${target} PRIVATE
        FW_VERSION="${FW_VERSION}"
        PRODUCT_NAME="${PRODUCT_NAME}"
        ELRS_LITE=${lite}
    )

    if(ELRS_RX_PARAMS)
        target_sources(${target} PRIVATE core/crsf_params.c)
        target_compile_definitions(${target} PRIVATE ELRS_RX_PARAMS=1)
    endif()

    target_compile_options(${target} PRIVATE -Wall -Wextra $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>)

    target_link_libraries(${target}
        pico_stdlib
        pico_unique_id
        pico_bootrom
        hardware_uart
        hardware_irq
        hardware_gpio
        hardware_flash
        hardware_sync
        hardware_watchdog
        hardware_pio
        hardware_clocks
        tinyusb_device
        tinyusb_board
    )

    # UART0 is the CRSF link, not a console.
    pico_enable_stdio_uart(${target} 0)
    pico_enable_stdio_usb(${target} 0)

    pico_add_extra_outputs(${target})
endfunction()

add_firmware(elrs_sim_rx 0)
add_firmware(elrs_sim_rx_lite 1)
