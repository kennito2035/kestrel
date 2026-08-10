/*
 * DWT cycle-counter benchmark harness for the STM32H750 (Cortex-M7).
 *
 * Every performance number in the Kestrel README comes from DWT cycle
 * counts taken this way (the shipping firmware embeds the same pattern in
 * ai_infer.c; this standalone harness packages it for reuse); no simulator
 * estimates, no projections. Wrap any stage:
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

/* Master switch (0 disables all benchmark code paths/output). Skip-rate
 * measurement needs no switch here: the shipping firmware unconditionally
 * streams a `gate,...` CSV line every 16 frames; capture that and feed it
 * to benchmarks/summarize.py. */
#define BENCHMARK_ENABLE    1

void bench_init(void);
uint32_t bench_cycles(void);
void bench_log(const char *stage, uint32_t cycles);

#endif /* KESTREL_BENCHMARK_H */
