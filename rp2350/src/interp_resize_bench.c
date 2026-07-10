/*
 * On-device benchmark: INTERP-assisted vs pure-software bilinear resize.
 *
 * Flash interp_resize_bench.uf2, open the USB serial port, and the board
 * prints a CSV-ish table of average resize times plus a bit-exactness
 * check between the two paths. Copy the output into
 * benchmarks/interpolator_results.csv.
 *
 * License: MIT (see repository root).
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hw_interpolator_resize.h"

#define REPS 100

static uint8_t src[200 * 150];
static uint8_t dst_sw[KESTREL_DST_W * KESTREL_DST_H];
static uint8_t dst_hw[KESTREL_DST_W * KESTREL_DST_H];

static const struct { uint16_t w, h; const char *label; } cases[] = {
    { 160, 120, "full frame 160x120" },
    { 120, 120, "75% ROI 120x120" },
    {  80,  80, "50% ROI 80x80" },
    {  48,  48, "25% ROI 48x48 (upscale)" },
};

static void fill_pseudo_random(uint8_t *buf, uint32_t n)
{
    uint32_t rng = 0xC0FFEE01u;
    for (uint32_t i = 0; i < n; i++) {
        rng = rng * 1664525u + 1013904223u;
        buf[i] = (uint8_t)(rng >> 24);
    }
}

static uint64_t time_reps(void (*fn)(const uint8_t *, uint16_t, uint16_t,
                                     uint8_t *),
                          uint16_t w, uint16_t h, uint8_t *dst)
{
    const uint64_t t0 = time_us_64();
    for (int r = 0; r < REPS; r++) {
        fn(src, w, h, dst);
    }
    return (time_us_64() - t0) / REPS;
}

int main(void)
{
    stdio_init_all();
    sleep_ms(3000); /* give the USB serial host time to attach */

    printf("# RP2350 bilinear resize -> %dx%d, avg of %d reps\n",
           KESTREL_DST_W, KESTREL_DST_H, REPS);
    printf("case,sw_us,interp_us,speedup,bit_exact\n");

    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const uint16_t w = cases[i].w, h = cases[i].h;
        fill_pseudo_random(src, (uint32_t)w * h);

        const uint64_t us_sw = time_reps(resize_bilinear_sw, w, h, dst_sw);
        const uint64_t us_hw = time_reps(resize_bilinear_interp, w, h, dst_hw);
        const bool exact =
            memcmp(dst_sw, dst_hw, sizeof dst_sw) == 0;

        printf("%s,%llu,%llu,%.2f,%s\n", cases[i].label,
               (unsigned long long)us_sw, (unsigned long long)us_hw,
               us_hw ? (double)us_sw / (double)us_hw : 0.0,
               exact ? "yes" : "NO");
    }

    printf("# done\n");
    while (true) {
        sleep_ms(1000);
    }
}
