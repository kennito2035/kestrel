/*
 * temp_sensor.c - H750 die (junction) temperature via ADC3 IN18.
 * Register-level, lazy-init, no CubeMX/HAL-ADC dependency.
 * Factory 2-point calibration: TS_CAL1 @30C (0x1FF1E820),
 * TS_CAL2 @110C (0x1FF1E840), both 16-bit @ VDDA=3.3V (matches board).
 * Kernel clock: per_ck (HSI 64MHz) / 16 = 4MHz. Kept <=6.25MHz on purpose so
 * the ADC_CR BOOST field may stay at its reset value 0 (valid for <=6.25MHz on
 * rev V); at 8MHz BOOST=1 would be required for rated accuracy. SMP=810.5
 * cycles ~= 202us >> 9us minimum sampling for VSENSE. One-shot, cost irrelevant.
 * NOTE: die-temp is telemetry only (shown on the stats card), not a benchmark
 * input; sanity-check the on-screen value is ~30-55C before citing it.
 */
#include "main.h"
#include "temp_sensor.h"

#define TS_CAL1  (*(const uint16_t *)0x1FF1E820)   /* raw @ 30C  */
#define TS_CAL2  (*(const uint16_t *)0x1FF1E840)   /* raw @ 110C */

static uint8_t ts_ready = 0;

static void ts_init(void)
{
  RCC->AHB4ENR |= RCC_AHB4ENR_ADC3EN;
  (void)RCC->AHB4ENR;

  /* ADC kernel clock <- per_ck (HSI 64MHz); async mode, presc /8 */
  RCC->D3CCIPR = (RCC->D3CCIPR & ~RCC_D3CCIPR_ADCSEL_Msk)
               | (2U << RCC_D3CCIPR_ADCSEL_Pos);
  ADC3_COMMON->CCR = (7U << ADC_CCR_PRESC_Pos) | ADC_CCR_TSEN; /* /16 = 4MHz */

  /* wake from deep-power-down, enable regulator, settle */
  ADC3->CR &= ~ADC_CR_DEEPPWD;
  ADC3->CR |= ADC_CR_ADVREGEN;
  for (volatile uint32_t i = 0; i < 5000; i++) {}   /* >10us @480MHz */

  /* offset calibration (single-ended) */
  ADC3->CR |= ADC_CR_ADCAL;
  while (ADC3->CR & ADC_CR_ADCAL) {}

  /* channel 18: preselect (H7 requirement) + long sample time */
  ADC3->PCSEL |= (1U << 18);
  ADC3->SMPR2 |= (7U << ADC_SMPR2_SMP18_Pos);       /* 810.5 cycles */
  ADC3->SQR1 = (18U << ADC_SQR1_SQ1_Pos);           /* L=0: 1 conversion */

  ADC3->ISR = ADC_ISR_ADRDY;
  ADC3->CR |= ADC_CR_ADEN;
  while (!(ADC3->ISR & ADC_ISR_ADRDY)) {}

  ts_ready = 1;
}

int temp_read_c(void)
{
  if (!ts_ready)
    ts_init();

  ADC3->ISR = ADC_ISR_EOC | ADC_ISR_EOS;
  ADC3->CR |= ADC_CR_ADSTART;
  while (!(ADC3->ISR & ADC_ISR_EOC)) {}
  uint32_t raw = ADC3->DR;                          /* 16-bit */

  int32_t num = (int32_t)(raw - TS_CAL1) * (110 - 30);
  int32_t den = (int32_t)TS_CAL2 - (int32_t)TS_CAL1;
  return (den != 0) ? (int)(30 + num / den) : -99;
}
