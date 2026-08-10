/*
 * stop_mode.h - STOP mode with PC0 EXTI rising-edge wake (driven by the
 * RP2350's PIR wake pulse; jumper PC0->3V3 to fake it with no RP2350
 * attached). Register-level, regen-proof.
 */
#ifndef STOP_MODE_H
#define STOP_MODE_H

#include <stdint.h>

/* Configure PC0 as EXTI0 rising-edge wake source (input, pulldown). */
void stop_mode_init(void);

/* Enter STOP (low-power regulator, WFI). Blocks until PC0 or K1 rises.
 * Restores the clock tree (SystemClock_Config) before returning. If a
 * wake edge already fired during the shutdown sequence, returns
 * immediately instead of sleeping through the event. */
void stop_mode_sleep(void);

/* Latched by the wake-source ISRs when a wake edge fires while awake;
 * consumed by stop_mode_sleep(). */
extern volatile uint8_t stop_mode_wake_pending;

#endif /* STOP_MODE_H */
