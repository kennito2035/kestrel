/*
 * Core 0: PIR motion pre-screen -> H750 wake pulse.
 *
 * The HC-SR501 holds its output high for seconds per event, so a simple
 * rising-edge interrupt with a short wake pulse is enough; the H750's
 * frame-difference gate handles fine-grained activity from there. Between
 * events this core just sleeps in WFE via __wfe()-backed sleep functions,
 * keeping the always-on current in the low milliamps.
 *
 * License: MIT (see repository root).
 */
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "kestrel_pins.h"

#define WAKE_PULSE_MS      50
#define RETRIGGER_HOLD_MS  2000  /* ignore PIR re-fires while H750 is awake */

static volatile bool pir_fired;

static void pir_irq(uint gpio, uint32_t events)
{
    (void)events;
    if (gpio == PIN_PIR) {
        pir_fired = true;
    }
}

void pir_trigger_task(void)
{
    gpio_init(PIN_WAKE_OUT);
    gpio_set_dir(PIN_WAKE_OUT, GPIO_OUT);
    gpio_put(PIN_WAKE_OUT, 0);

    gpio_init(PIN_PIR);
    gpio_set_dir(PIN_PIR, GPIO_IN);
    gpio_pull_down(PIN_PIR);
    gpio_set_irq_enabled_with_callback(PIN_PIR, GPIO_IRQ_EDGE_RISE, true,
                                       pir_irq);

    while (true) {
        if (pir_fired) {
            pir_fired = false;
            gpio_put(PIN_WAKE_OUT, 1);
            sleep_ms(WAKE_PULSE_MS);
            gpio_put(PIN_WAKE_OUT, 0);
            sleep_ms(RETRIGGER_HOLD_MS);
        }
        __wfe();
    }
}
