/*
 * uart_log.h - USART1 (PA9 TX / PA10 RX) @115200, register-level init so
 * CubeMX regeneration can never touch it. Blocking printf-style logging
 * for benchmark CSV capture (CoolTerm etc.); later doubles as the RP2350
 * link on the same pins.
 */
#ifndef UART_LOG_H
#define UART_LOG_H

void uart_log_init(void);
void uart_printf(const char *fmt, ...);

#endif /* UART_LOG_H */
