/*
 * DWT cycle-counter benchmark harness: implementation.
 *
 * The Cortex-M7 Data Watchpoint and Trace unit has a free-running cycle
 * counter (DWT->CYCCNT). At 480MHz it wraps every ~8.9s, far longer than
 * any stage we measure, so plain unsigned subtraction of two readings is
 * exact.
 *
 * License: MIT (see repository root).
 */
#include <stdio.h>
#include "stm32h7xx.h"
#include "benchmark.h"

void bench_init(void)
{
#if BENCHMARK_ENABLE
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55;              /* unlock DWT on Cortex-M7 */
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif
}

uint32_t bench_cycles(void)
{
#if BENCHMARK_ENABLE
    return DWT->CYCCNT;
#else
    return 0;
#endif
}

void bench_log(const char *stage, uint32_t cycles)
{
#if BENCHMARK_ENABLE
    /* printf retargeted to UART1 via _write() in syscalls/main. */
    printf("%s,%lu,%lu\r\n", stage, (unsigned long)cycles,
           (unsigned long)(cycles / (SystemCoreClock / 1000000u)));
#else
    (void)stage;
    (void)cycles;
#endif
}
