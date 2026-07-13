/*
 * stop_mode.c - H750 STOP mode, woken by EXTI0 rising edge on PC0.
 * After STOP the system clock restarts on HSI, so SystemClock_Config()
 * (defined in main.c) is re-run before returning to the caller.
 */
#include "main.h"
#include "stop_mode.h"

extern void SystemClock_Config(void);

void stop_mode_init(void)
{
  RCC->AHB4ENR |= RCC_AHB4ENR_GPIOCEN;
  RCC->APB4ENR |= RCC_APB4ENR_SYSCFGEN;
  (void)RCC->APB4ENR;

  /* PC0: input with pulldown (idle low; RP2350/jumper drives it high) */
  GPIOC->MODER &= ~3U;
  GPIOC->PUPDR = (GPIOC->PUPDR & ~3U) | 2U;

  /* EXTI line 0 <- port C, rising edge, unmasked for D1 CPU */
  SYSCFG->EXTICR[0] = (SYSCFG->EXTICR[0] & ~0xFU) | 2U;
  EXTI->RTSR1 |= 1U;
  EXTI_D1->IMR1 |= 1U;

  /* K1 (PC13) as a second wake source, no-solder testing. EXTI line 13:
   * port C in EXTICR[3], rising edge (K1 pressed = high). PC13 is already
   * an input (KEY) via the CubeMX GPIO init. */
  SYSCFG->EXTICR[3] = (SYSCFG->EXTICR[3] & ~(0xFU << 4)) | (2U << 4);
  EXTI->RTSR1 |= (1U << 13);
  EXTI_D1->IMR1 |= (1U << 13);

  NVIC_SetPriority(EXTI0_IRQn, 5);
  NVIC_EnableIRQ(EXTI0_IRQn);
  NVIC_SetPriority(EXTI15_10_IRQn, 5);
  NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void EXTI0_IRQHandler(void)
{
  EXTI_D1->PR1 = 1U;   /* write-1-to-clear; waking is the side effect */
}

void EXTI15_10_IRQHandler(void)
{
  EXTI_D1->PR1 = (0x3FU << 10);   /* clear lines 10..15 (K1 = 13) */
}

void stop_mode_sleep(void)
{
  HAL_SuspendTick();
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
  /* --- asleep here until PC0 rising edge --- */
  HAL_ResumeTick();
  SystemClock_Config();   /* STOP exits on HSI: restore 480MHz PLL tree */
}
