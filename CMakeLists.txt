cmake_minimum_required(VERSION 3.13)

# Native tests do not need the SDK or the ARM toolchain.
option(TORINO_TESTS "Build native tests instead of firmware" OFF)
if(TORINO_TESTS)
    project(torino_tests C)
    set(CMAKE_C_STANDARD 11)
    enable_testing()

    foreach(name tone preset controls)
        add_executable(test_${name} tests/test_${name}.c src/controls.c)
        if(NOT name STREQUAL "controls")
            target_sources(test_${name} PRIVATE src/preset.c)
        endif()
        if(name STREQUAL "tone")
            target_sources(test_${name} PRIVATE src/tone.c)
        endif()
        target_include_directories(test_${name} PRIVATE src)
        if(UNIX)
            target_link_libraries(test_${name} PRIVATE m)
        endif()
        target_compile_options(test_${name} PRIVATE -Wall -Wextra -Werror -pedantic -UNDEBUG)
        add_test(NAME ${name} COMMAND test_${name})
    endforeach()
    return()
endif()

set(PICO_BOARD pico2 CACHE STRING "RP2350 ARM target")
set(PICO_FLASH_SIZE_BYTES 2097152 CACHE STRING "Measured board flash size")
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(torino C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

if(NOT TARGET tinyusb_pico_pio_usb)
    message(FATAL_ERROR "Set PICO_PIO_USB_PATH to the pinned Pico-PIO-USB checkout")
endif()

add_executable(torino
    src/main.c src/probe.c src/usb.c src/controller.c src/tone.c
    src/preset.c src/store.c src/controls.c src/gain.c src/native.c
    src/fast_host.c src/trace.c)
set(TORINO_STORE_LIMIT 24 CACHE STRING "Highest writable preset slot")
target_compile_definitions(torino PRIVATE TORINO_RELAY=1 TORINO_STORE_LIMIT=${TORINO_STORE_LIMIT})
target_include_directories(torino PRIVATE src)
target_compile_options(torino PRIVATE -Wall -Wextra)
target_link_libraries(torino PRIVATE
    pico_stdlib pico_multicore hardware_watchdog
    tinyusb_device tinyusb_host tinyusb_pico_pio_usb)
pico_add_extra_outputs(torino)
