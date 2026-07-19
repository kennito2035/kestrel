/*
 * Kestrel RP2350-USB Mini firmware, always-on watcher and output controller.
 *
 * Core 0: PIR pre-screen. Waits (cheaply) for motion; on a PIR rising edge,
 *         pulses the H750's wake line so it can leave STOP mode and start
 *         its frame-difference gate.
 * Core 1: Output controller. Parses detection events arriving over UART
 *         from the H750 and drives the servo/LED response.
 *
 * License: MIT (see repository root).
 */
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "kestrel_pins.h"

int main(void)
{
    stdio_init_all();
    multicore_launch_core1(output_controller_task);
    pir_trigger_task(); /* never returns */
}
