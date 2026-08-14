/*
 * gate_bench.c - one-shot scalar-vs-SIMD timing of the motion gate on
 * live camera frames.
 *
 * The scalar contender is the SAME source file as the shipping SIMD
 * gate: gate.c is included below with KESTREL_GATE_SIMD forced off and
 * the entry point renamed, so the two timed paths are guaranteed to be
 * the same code with only the SIMD switch different (they are bit-exact
 * by construction; this measures speed, not behavior).
 */
#pragma GCC optimize("O2")

#define KESTREL_GATE_SIMD 0
#define gate_check gate_check_scalar
#include "gate.c"
#undef gate_check

#include "stm32h7xx.h"
#include "uart_log.h"
#include "gate_bench.h"

/* The real (SIMD) gate_check from the shipping gate.c object. */
extern gate_state_t gate_check(const gate_config_t *cfg,
                               const uint8_t *curr, const uint8_t *prev,
                               gate_roi_t *roi_out,
                               uint32_t *changed_count_out);

#define GATE_BENCH_REPS 100u

/* Synthetic frames for the bit-exactness cross-check (RAM_D1 .bss). */
static uint8_t bench_a[160u * 120u];
static uint8_t bench_b[160u * 120u];

/* 1 when both paths agree on state, count, and (when open) the ROI. */
static uint32_t paths_agree(const gate_config_t *cfg,
                            const uint8_t *curr, const uint8_t *prev)
{
  gate_roi_t r1 = {0, 0, 0, 0}, r2 = {0, 0, 0, 0};
  uint32_t c1 = 0, c2 = 0;
  const gate_state_t s1 = gate_check(cfg, curr, prev, &r1, &c1);
  const gate_state_t s2 = gate_check_scalar(cfg, curr, prev, &r2, &c2);
  if (s1 != s2 || c1 != c2)
    return 0;
  if (s1 == GATE_OPEN &&
      (r1.x != r2.x || r1.y != r2.y || r1.w != r2.w || r1.h != r2.h))
    return 0;
  return 1;
}

static uint32_t bench_path(gate_state_t (*fn)(const gate_config_t *,
                                              const uint8_t *,
                                              const uint8_t *,
                                              gate_roi_t *, uint32_t *),
                           const gate_config_t *cfg,
                           const uint8_t *curr, const uint8_t *prev)
{
  gate_roi_t roi;
  uint32_t changed;
  (void)fn(cfg, curr, prev, &roi, &changed);   /* warm-up rep */
  const uint32_t t0 = DWT->CYCCNT;
  for (uint32_t i = 0; i < GATE_BENCH_REPS; i++)
    (void)fn(cfg, curr, prev, &roi, &changed);
  const uint32_t cyc = DWT->CYCCNT - t0;
  return cyc / (SystemCoreClock / 1000000u) / GATE_BENCH_REPS;
}

void gate_bench_run(const gate_config_t *cfg,
                    const uint8_t *curr, const uint8_t *prev)
{
  /* DWT already runs (ai_infer init); enable defensively anyway. */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  const uint32_t scalar_us = bench_path(gate_check_scalar, cfg, curr, prev);
  const uint32_t simd_us   = bench_path(gate_check, cfg, curr, prev);
  const uint32_t ratio_x100 = (simd_us > 0)
                                  ? (scalar_us * 100u) / simd_us : 0;

  /* Bit-exactness cross-check: live frames, then pseudorandom frames at
   * low/typical/max thresholds. The SIMD GE-flag idiom would fail here
   * as a miscount (match,0), never as a build error. */
  uint32_t match = paths_agree(cfg, curr, prev);
  uint32_t rng = 0x4B455354u;   /* "KEST" */
  for (uint32_t i = 0; i < (uint32_t)sizeof bench_a; i++) {
    rng = rng * 1664525u + 1013904223u;
    bench_a[i] = (uint8_t)(rng >> 24);
    rng = rng * 1664525u + 1013904223u;
    bench_b[i] = (uint8_t)(rng >> 24);
  }
  static const uint8_t thrs[3] = {5u, 30u, 254u};
  for (uint32_t t = 0; t < 3u; t++) {
    gate_config_t c;
    c.width = 160u;
    c.height = 120u;
    c.pixel_threshold = thrs[t];
    c.open_count = 1u;
    c.roi_pad = 8u;
    if (!paths_agree(&c, bench_a, bench_b))
      match = 0;
  }

  uart_printf("bench,gate,scalar_us,%lu,simd_us,%lu,ratio_x100,%lu,match,%lu\r\n",
              (unsigned long)scalar_us, (unsigned long)simd_us,
              (unsigned long)ratio_x100, (unsigned long)match);
}
