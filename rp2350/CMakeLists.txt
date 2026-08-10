cmake_minimum_required(VERSION 3.13)

# Default to the RP2350 board: a plain `cmake ..` would otherwise silently
# configure for the RP2040 "pico" and the bootrom would reject the UF2.
if(NOT PICO_BOARD AND NOT DEFINED ENV{PICO_BOARD})
    set(PICO_BOARD pico2)
endif()

# Standard pico-sdk bootstrap: reads PICO_SDK_PATH from the environment or
# the -DPICO_SDK_PATH=... cmake argument.
if(DEFINED PICO_SDK_PATH)
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
elseif(DEFINED ENV{PICO_SDK_PATH})
    include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
else()
    message(FATAL_ERROR "PICO_SDK_PATH is not set: pass -DPICO_SDK_PATH=/path/to/pico-sdk or export it in the environment")
endif()

project(kestrel_rp2350 C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

# Main firmware: PIR pre-screen (core 0) + output controller (core 1)
add_executable(kestrel_rp2350
    src/main.c
    src/pir_trigger.c
    src/output_controller.c
)
target_link_libraries(kestrel_rp2350
    pico_stdlib
    pico_multicore
    hardware_uart
    hardware_pwm
)
pico_enable_stdio_usb(kestrel_rp2350 1)
# uart0 carries the H750 protocol link; keep stdio (and panic output) off it.
pico_enable_stdio_uart(kestrel_rp2350 0)
pico_add_extra_outputs(kestrel_rp2350)

# Standalone benchmark: INTERP vs software bilinear resize
add_executable(interp_resize_bench
    src/interp_resize_bench.c
    src/hw_interpolator_resize.c
)
target_link_libraries(interp_resize_bench
    pico_stdlib
    hardware_interp
)
pico_enable_stdio_usb(interp_resize_bench 1)
# Same rationale as the main firmware: keep stdio off the GP0/GP1 UART.
pico_enable_stdio_uart(interp_resize_bench 0)
pico_add_extra_outputs(interp_resize_bench)
