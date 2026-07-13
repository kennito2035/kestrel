/*
 * uart_log.c - USART1 @115200 8N1 on PA9(TX)/PA10(RX), AF7.
 * Pure register init: independent of CubeMX, HAL UART module and the .ioc.
 * Kernel clock: USART16SEL default = rcc_pclk2 = 120MHz (APB2).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "uart_log.h"

#define UARTLOG_PCLK   120000000UL
#define UARTLOG_BAUD   115200UL

void uart_log_init(void)
{
  RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN;
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
  (void)RCC->APB2ENR;

  /* PA9/PA10 -> AF7 (USART1), very high speed, no pull */
  GPIOA->MODER   = (GPIOA->MODER & ~((3U << (9 * 2)) | (3U << (10 * 2))))
                 | ((2U << (9 * 2)) | (2U << (10 * 2)));
  GPIOA->OSPEEDR |= (3U << (9 * 2)) | (3U << (10 * 2));
  GPIOA->AFR[1]  = (GPIOA->AFR[1] & ~((0xFU << ((9 - 8) * 4)) |
                                      (0xFU << ((10 - 8) * 4))))
                 | ((7U << ((9 - 8) * 4)) | (7U << ((10 - 8) * 4)));

  USART1->CR1 = 0;                              /* disable while configuring */
  USART1->BRR = (UARTLOG_PCLK + UARTLOG_BAUD / 2) / UARTLOG_BAUD;
  USART1->CR2 = 0;
  USART1->CR3 = 0;
  USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void uart_putc(char c)
{
  while (!(USART1->ISR & USART_ISR_TXE_TXFNF)) {}
  USART1->TDR = (uint8_t)c;
}

/* --- RAM log mirror: no-solder capture. Lives in RAM_D2 (.ram_log,
 * 0x30000000), survives reset. Extract after a session WITHOUT removing
 * power: hold BOOT0 + tap reset -> CubeProgrammer (USB DFU) -> read
 * device memory at 0x30000000 -> save .bin. Layout:
 *   [0] u32 magic 0x474F4C4B ("KLOG")  [1] u32 length  [8..] text data */
#define RAMLOG_CAP  (80u * 1024u)   /* DTCM budget: 128K - 38K gate buffers */
#define RAMLOG_MAGIC 0x474F4C4Bu

static struct
{
  uint32_t magic;
  uint32_t len;
  char data[RAMLOG_CAP];
} ramlog __attribute__((section(".ram_log")));

static void ramlog_putc(char c)
{
  if (ramlog.magic != RAMLOG_MAGIC)
  {
    ramlog.magic = RAMLOG_MAGIC;   /* first write this power-up */
    ramlog.len = 0;
  }
  if (ramlog.len < RAMLOG_CAP)
    ramlog.data[ramlog.len++] = c;
}

void uart_printf(const char *fmt, ...)
{
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0)
    return;
  if (n > (int)sizeof(buf) - 1)
    n = sizeof(buf) - 1;
  for (int i = 0; i < n; i++)
  {
    uart_putc(buf[i]);
    ramlog_putc(buf[i]);
  }
}
