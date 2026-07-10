cmake_minimum_required(VERSION 3.13)

# Standard pico-sdk bootstrap: reads PICO_SDK_PATH from the environment or
# the -DPICO_SDK_PATH=... cmake argument.
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

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
pico_add_extra_outputs(interp_resize_bench)
