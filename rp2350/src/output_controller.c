/*
 * Core 1: detection-event consumer -> physical response.
 *
 * Wire protocol from the H750 (newline-terminated ASCII, one event per
 * line, deliberately human-readable so a UART terminal doubles as a
 * debugger):
 *
 *     DET,<class>,<confidence_pct>\n      e.g.  DET,person,91\n
 *     GATE,<OPEN|CLOSED>,<changed_px>\n   (telemetry, ignored here)
 *
 * A `person` detection swings the servo to STRIKE for a moment, then
 * returns to REST, the kestrel's dive.
 *
 * License: MIT (see repository root).
 */
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "kestrel_pins.h"

#define SERVO_REST_US    1000
#define SERVO_STRIKE_US  2000
#define STRIKE_HOLD_MS   600
#define LINE_MAX         64

/* 50Hz servo PWM: 1MHz tick (125MHz sysclk / 125), wrap 20000 -> 20ms. */
static void servo_init(void)
{
    gpio_set_function(PIN_SERVO, GPIO_FUNC_PWM);
    const uint slice = pwm_gpio_to_slice_num(PIN_SERVO);
    pwm_set_clkdiv(slice, (float)clock_get_hz(clk_sys) / 1000000.0f);
    pwm_set_wrap(slice, 20000 - 1);
    pwm_set_gpio_level(PIN_SERVO, SERVO_REST_US);
    pwm_set_enabled(slice, true);
}

static void servo_pulse_us(uint16_t us)
{
    pwm_set_gpio_level(PIN_SERVO, us);
}

static void handle_line(const char *line)
{
    /* DET,<class>,<confidence_pct> */
    if (strncmp(line, "DET,", 4) != 0) {
        return;
    }
    if (strncmp(line + 4, "person,", 7) == 0) {
        servo_pulse_us(SERVO_STRIKE_US);
        sleep_ms(STRIKE_HOLD_MS);
        servo_pulse_us(SERVO_REST_US);
    }
}

void output_controller_task(void)
{
    char line[LINE_MAX];
    unsigned len = 0;

    uart_init(KESTREL_UART, KESTREL_UART_BAUD);
    gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_UART_RX, GPIO_FUNC_UART);
    servo_init();

    while (true) {
        const char c = uart_getc(KESTREL_UART);
        if (c == '\n' || c == '\r') {
            if (len > 0) {
                line[len] = '\0';
                handle_line(line);
                len = 0;
            }
        } else if (len < LINE_MAX - 1) {
            line[len++] = c;
        } else {
            len = 0; /* overlong line: discard and resync */
        }
    }
}
