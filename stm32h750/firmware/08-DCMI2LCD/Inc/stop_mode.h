/*
 * stop_mode.h - STOP mode with PC0 EXTI rising-edge wake (RP2350 wake line;
 * jumper PC0->3V3 to fake it until Stage 3). Register-level, regen-proof.
 */
#ifndef STOP_MODE_H
#define STOP_MODE_H

/* Configure PC0 as EXTI0 rising-edge wake source (input, pulldown). */
void stop_mode_init(void);

/* Enter STOP (low-power regulator, WFI). Blocks until PC0 rises.
 * Restores the clock tree (SystemClock_Config) before returning. */
void stop_mode_sleep(void);

#endif /* STOP_MODE_H */
