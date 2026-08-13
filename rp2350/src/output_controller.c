/*
 * Core 1: detection-event consumer -> physical response.
 *
 * Wire protocol from the H750 (newline-terminated ASCII, one event per
 * line, deliberately human-readable so a UART terminal doubles as a
 * debugger):
 *
 *     DET,<class>,<confidence_pct>\n      e.g.  DET,person,91\n
 *     gate,<total>,<enabled>,<skipped>,<changed>,<state>,<det_ms>\n
 *                                         (skip-rate telemetry, ignored here)
 *
 * A `person` detection starts a continuous 0-180 degree servo sweep that
 * runs for as long as detections keep arriving, then returns to REST.
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

#define SERVO_REST_US       1000
#define SERVO_SWEEP_MIN_US  500   /* ~0 degrees at the servo's full range */
#define SERVO_SWEEP_MAX_US  2500  /* ~180 degrees */
#define SWEEP_TICK_MS       15    /* one step per tick */
#define SWEEP_STEP_US       40    /* full sweep in ~0.75 s per direction */
/* Presence window: longer than the H750's 2 s DET rate limit, so a person
 * who stays in frame keeps the sweep alive (each DET extends the deadline);
 * the servo rests ~2.5 s after the last detection. */
#define PRESENCE_HOLD_MS    2500
#define LINE_MAX            64

/* 50Hz servo PWM: divide sysclk down to a 1MHz tick, wrap 20000 -> 20ms. */
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

/* Sweep state: all deadlines are checked from the main loop, never a
 * blocking sleep, so the UART keeps draining during the sweep (a blocking
 * wait overflows the RX FIFO in ~3 ms of continuous traffic). */
static bool sweeping = false;
static absolute_time_t presence_end;
static absolute_time_t next_step;
static int32_t sweep_pos_us = SERVO_REST_US;
static int32_t sweep_dir = 1;

static void handle_line(const char *line)
{
    /* DET,<class>,<confidence_pct> */
    if (strncmp(line, "DET,", 4) != 0) {
        return;
    }
    if (strncmp(line + 4, "person,", 7) == 0) {
        presence_end = make_timeout_time_ms(PRESENCE_HOLD_MS);
        if (!sweeping) {
            sweeping = true;
            next_step = get_absolute_time();
        }
    }
}

void output_controller_task(void)
{
    char line[LINE_MAX];
    unsigned len = 0;
    bool discard = false;

    uart_init(KESTREL_UART, KESTREL_UART_BAUD);
    gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_UART_RX, GPIO_FUNC_UART);
    servo_init();

    while (true) {
        if (sweeping) {
            if (time_reached(presence_end)) {
                sweeping = false;
                servo_pulse_us(SERVO_REST_US);
            } else if (time_reached(next_step)) {
                next_step = make_timeout_time_ms(SWEEP_TICK_MS);
                sweep_pos_us += sweep_dir * SWEEP_STEP_US;
                if (sweep_pos_us >= SERVO_SWEEP_MAX_US) {
                    sweep_pos_us = SERVO_SWEEP_MAX_US;
                    sweep_dir = -1;
                }
                if (sweep_pos_us <= SERVO_SWEEP_MIN_US) {
                    sweep_pos_us = SERVO_SWEEP_MIN_US;
                    sweep_dir = 1;
                }
                servo_pulse_us((uint16_t)sweep_pos_us);
            }
        }
        if (!uart_is_readable(KESTREL_UART)) {
            continue;
        }
        const char c = uart_getc(KESTREL_UART);
        if (c == '\n' || c == '\r') {
            if (discard) {
                discard = false; /* overlong line ends here: drop it whole */
            } else if (len > 0) {
                line[len] = '\0';
                handle_line(line);
            }
            len = 0;
        } else if (discard) {
            /* still inside the overlong line: keep dropping */
        } else if (len < LINE_MAX - 1) {
            line[len++] = c;
        } else {
            len = 0;
            discard = true; /* overlong line: discard through its terminator */
        }
    }
}
