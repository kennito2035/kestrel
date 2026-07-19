/*
 * Kestrel RP2350 interpolator bench, Arduino IDE version.
 *
 * Same measurement as the pico-sdk interp_resize_bench: INTERP-assisted
 * vs pure-software bilinear resize, 100-rep averages, bit-exactness
 * check. Flash, open the Serial Monitor at 115200, and copy the CSV
 * block into benchmarks/interpolator_results.csv (the table reprints
 * every 10 s so a late-opened monitor still catches it).
 *
 * Board: Tools > Board > "Generic RP2350", Flash Size 16MB
 * (arduino-pico core by Earle Philhower). The pragma below pins the
 * whole sketch to -O2 so results match the pico-sdk build regardless
 * of the IDE's Optimize menu setting.
 *
 * License: MIT (see repository root).
 */
#pragma GCC optimize("O2")

#include <pico/stdlib.h>
#include "hw_interpolator_resize.h"
#include "hw_interpolator_resize_impl.h"

#define REPS 100

static uint8_t src_buf[200 * 150];
static uint8_t dst_sw[KESTREL_DST_W * KESTREL_DST_H];
static uint8_t dst_hw[KESTREL_DST_W * KESTREL_DST_H];

struct BenchCase { uint16_t w, h; const char *label; };
static const BenchCase cases[] = {
  { 160, 120, "full frame 160x120" },
  { 120, 120, "75% ROI 120x120" },
  {  80,  80, "50% ROI 80x80" },
  {  48,  48, "25% ROI 48x48 (upscale)" },
};

static void fill_pseudo_random(uint8_t *buf, uint32_t n) {
  uint32_t rng = 0xC0FFEE01u;
  for (uint32_t i = 0; i < n; i++) {
    rng = rng * 1664525u + 1013904223u;
    buf[i] = (uint8_t)(rng >> 24);
  }
}

static uint64_t time_reps(void (*fn)(const uint8_t *, uint16_t, uint16_t,
                                     uint8_t *),
                          uint16_t w, uint16_t h, uint8_t *dst) {
  const uint64_t t0 = time_us_64();
  for (int r = 0; r < REPS; r++) {
    fn(src_buf, w, h, dst);
  }
  return (time_us_64() - t0) / REPS;
}

static void run_bench() {
  Serial.printf("# RP2350 bilinear resize -> %dx%d, avg of %d reps\n",
                KESTREL_DST_W, KESTREL_DST_H, REPS);
  Serial.println("case,sw_us,interp_us,speedup,bit_exact");
  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    const uint16_t w = cases[i].w, h = cases[i].h;
    fill_pseudo_random(src_buf, (uint32_t)w * h);
    const uint64_t us_sw = time_reps(resize_bilinear_sw, w, h, dst_sw);
    const uint64_t us_hw = time_reps(resize_bilinear_interp, w, h, dst_hw);
    const bool exact = memcmp(dst_sw, dst_hw, sizeof dst_sw) == 0;
    Serial.printf("%s,%lu,%lu,%.2f,%s\n", cases[i].label,
                  (unsigned long)us_sw, (unsigned long)us_hw,
                  us_hw ? (double)us_sw / (double)us_hw : 0.0,
                  exact ? "yes" : "NO");
  }
  Serial.println("# done");
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) { delay(10); }
  delay(500);
}

void loop() {
  run_bench();
  delay(10000);
}
