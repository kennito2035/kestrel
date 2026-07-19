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

static void blend_hw_setup() {
  interp_config cfg = interp_default_config();
  interp_config_set_blend(&cfg, true);
  interp_set_config(interp0, 0, &cfg);
  /* Blend alpha = LANE1's shifted-and-masked result; the reset-state
   * lane 1 mask is bit0-only, truncating alpha to 0/1. Pass ACCUM1
   * through: */
  cfg = interp_default_config();
  interp_set_config(interp0, 1, &cfg);
}

static uint8_t blend_hw(uint8_t a, uint8_t b, uint8_t alpha) {
  interp0->accum[1] = alpha;
  interp0->base[0] = a;
  interp0->base[1] = b;
  return (uint8_t)interp0->peek[1];
}

/* Directly characterize the silicon's blend equation vs the SW model. */
static void equation_probe() {
  blend_hw_setup();
  uint32_t total = 0, mism = 0;
  int maxd = 0, printed = 0;
  for (int a = 0; a < 256; a += 5) {
    for (int b = 0; b < 256; b += 5) {
      for (int al = 0; al < 256; al += 3) {
        total++;
        const uint8_t sw = blend8((uint8_t)a, (uint8_t)b, (uint8_t)al);
        const uint8_t hw = blend_hw((uint8_t)a, (uint8_t)b, (uint8_t)al);
        if (sw != hw) {
          mism++;
          const int d = (sw > hw) ? sw - hw : hw - sw;
          if (d > maxd) maxd = d;
          if (printed < 8) {
            Serial.printf("# probe mismatch a=%d b=%d alpha=%d sw=%d hw=%d\n",
                          a, b, al, sw, hw);
            printed++;
          }
        }
      }
    }
  }
  Serial.printf("# blend probe: %lu mismatches of %lu, max |diff| %d\n",
                (unsigned long)mism, (unsigned long)total, maxd);
}

/* Variant 2: BASE_1AND0 packed write, one MMIO store for both operands. */
static void resize_bilinear_interp2(const uint8_t *src, uint16_t src_w,
                                    uint16_t src_h, uint8_t *dst) {
  blend_hw_setup();
  const uint32_t x_step = (((uint32_t)(src_w - 1)) << 16) / (KESTREL_DST_W - 1);
  const uint32_t y_step = (((uint32_t)(src_h - 1)) << 16) / (KESTREL_DST_H - 1);
  uint32_t y_fp = 0;
  for (uint16_t dy = 0; dy < KESTREL_DST_H; dy++, y_fp += y_step) {
    const uint32_t sy = y_fp >> 16;
    const uint8_t fy = (uint8_t)(y_fp >> 8);
    const uint8_t *row0 = src + sy * src_w;
    const uint8_t *row1 = (sy + 1 < src_h) ? row0 + src_w : row0;
    uint32_t x_fp = 0;
    for (uint16_t dx = 0; dx < KESTREL_DST_W; dx++, x_fp += x_step) {
      const uint32_t sx = x_fp >> 16;
      const uint8_t fx = (uint8_t)(x_fp >> 8);
      const uint32_t sx1 = (sx + 1 < src_w) ? sx + 1 : sx;
      interp0->accum[1] = fx;
      interp0->base01 = ((uint32_t)row0[sx1] << 16) | row0[sx];
      const uint8_t top = (uint8_t)interp0->peek[1];
      interp0->base01 = ((uint32_t)row1[sx1] << 16) | row1[sx];
      const uint8_t bot = (uint8_t)interp0->peek[1];
      dst[(uint32_t)dy * KESTREL_DST_W + dx] = blend8(top, bot, fy);
    }
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

static uint32_t diff_report(const uint8_t *a, const uint8_t *b, uint32_t n,
                            bool print_first) {
  uint32_t mism = 0;
  int printed = 0;
  for (uint32_t i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      mism++;
      if (print_first && printed < 4) {
        Serial.printf("#   dst[%lu]: sw=%d hw=%d\n", (unsigned long)i,
                      a[i], b[i]);
        printed++;
      }
    }
  }
  return mism;
}

static void run_bench() {
  equation_probe();
  Serial.printf("# RP2350 bilinear resize -> %dx%d, avg of %d reps\n",
                KESTREL_DST_W, KESTREL_DST_H, REPS);
  Serial.println("case,sw_us,interp_us,interp2_us,speedup2,mism1,mism2");
  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    const uint16_t w = cases[i].w, h = cases[i].h;
    fill_pseudo_random(src_buf, (uint32_t)w * h);
    const uint64_t us_sw = time_reps(resize_bilinear_sw, w, h, dst_sw);
    const uint64_t us_hw = time_reps(resize_bilinear_interp, w, h, dst_hw);
    const uint32_t mism1 = diff_report(dst_sw, dst_hw, sizeof dst_sw, i == 0);
    const uint64_t us_h2 = time_reps(resize_bilinear_interp2, w, h, dst_hw);
    const uint32_t mism2 = diff_report(dst_sw, dst_hw, sizeof dst_sw, false);
    Serial.printf("%s,%lu,%lu,%lu,%.2f,%lu,%lu\n", cases[i].label,
                  (unsigned long)us_sw, (unsigned long)us_hw,
                  (unsigned long)us_h2,
                  us_h2 ? (double)us_sw / (double)us_h2 : 0.0,
                  (unsigned long)mism1, (unsigned long)mism2);
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
