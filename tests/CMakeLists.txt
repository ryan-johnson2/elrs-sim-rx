cmake_minimum_required(VERSION 3.13)
project(elrs_sim_rx_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()

set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/core)

add_library(core STATIC
    ${CORE}/crc.c
    ${CORE}/crsf.c
    ${CORE}/mapping.c
    ${CORE}/failsafe.c
    ${CORE}/led_seq.c
    ${CORE}/button.c
)
target_include_directories(core PUBLIC ${CORE})
target_compile_options(core PRIVATE -Wall -Wextra -Werror)

function(add_core_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} core)
    target_compile_options(${name} PRIVATE -Wall -Wextra)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_core_test(test_crc)
add_core_test(test_crsf)
add_core_test(test_mapping)
add_core_test(test_failsafe)
add_core_test(test_led_seq)
add_core_test(test_button)
