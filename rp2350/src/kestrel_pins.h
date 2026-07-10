/* Pin assignments for the Kestrel RP2350-ONE firmware (see README wiring). */
#ifndef KESTREL_PINS_H
#define KESTREL_PINS_H

#define PIN_UART_TX    0   /* UART0 TX -> H750 PA10 (UART1 RX) */
#define PIN_UART_RX    1   /* UART0 RX <- H750 PA9  (UART1 TX) */
#define PIN_WAKE_OUT   15  /* GPIO out -> H750 PC0 (EXTI wake) */
#define PIN_PIR        16  /* HC-SR501 output, rising edge on motion */
#define PIN_SERVO      18  /* SG90 signal, 50Hz PWM */

#define KESTREL_UART        uart0
#define KESTREL_UART_BAUD   115200

void pir_trigger_task(void);        /* core 0: never returns */
void output_controller_task(void);  /* core 1: never returns */

#endif /* KESTREL_PINS_H */
