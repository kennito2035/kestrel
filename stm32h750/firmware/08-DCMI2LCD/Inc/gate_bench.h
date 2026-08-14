/*
 * gate_bench.h - one-shot on-target scalar-vs-SIMD gate timing.
 * Prints a single "bench,gate,..." CSV line over UART shortly after
 * start; standing evidence for the scalar-vs-SIMD comparison in
 * benchmarks/benchmark_report.md.
 */
#ifndef GATE_BENCH_H
#define GATE_BENCH_H

#include "gate.h"

/* Times 100 reps of each path on the two live grayscale frames and
 * prints: bench,gate,scalar_us,<u>,simd_us,<u>,ratio_x100,<u> */
void gate_bench_run(const gate_config_t *cfg,
                    const uint8_t *curr, const uint8_t *prev);

#endif /* GATE_BENCH_H */
