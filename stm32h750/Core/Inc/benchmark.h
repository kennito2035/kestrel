/*
 * DWT cycle-counter benchmark harness for the STM32H750 (Cortex-M7).
 *
 * Every performance number in the Kestrel README comes from this harness;
 * no simulator estimates, no projections. Wrap any stage:
 *
 *     bench_init();                      // once, after clock config
 *     uint32_t t0 = bench_cycles();
 *     gate_state_t s = gate_check(...);
 *     bench_log("gate", bench_cycles() - t0);
 *
 * Output goes over UART as CSV: `stage,cycles,us`; capture it with any
 * serial terminal and drop it into benchmarks/.
 *
 * License: MIT (see repository root).
 */
#ifndef KESTREL_BENCHMARK_H
#define KESTREL_BENCHMARK_H

#include <stdint.h>

/* Master switches (0 disables all benchmark code paths/output). */
#define BENCHMARK_ENABLE    1
/* Log gate state + changed-pixel count every N frames for skip-rate
 * measurement (0 disables). Feed the CSV to benchmarks/summarize.py. */
#define BENCHMARK_GATE_LOG  100

void bench_init(void);
uint32_t bench_cycles(void);
void bench_log(const char *stage, uint32_t cycles);

#endif /* KESTREL_BENCHMARK_H */
