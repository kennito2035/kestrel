/*
 * Host-side tests for the software bilinear path (the reference that the
 * INTERP hardware path must match byte-for-byte on target):
 *
 *   cc -DKESTREL_HOST_BUILD -Wall -Wextra -Werror -O2 \
 *      -o test_resize test_resize.c ../src/hw_interpolator_resize.c -lm
 *   ./test_resize
 *
 * HW/SW bit-exactness itself is asserted on device by the benchmark
 * (interp_resize_bench) via memcmp; it cannot be tested on a PC.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/hw_interpolator_resize.h"

#define DW KESTREL_DST_W
#define DH KESTREL_DST_H

static int failures = 0;

#define CHECK(cond, ...)                                            \
    do {                                                            \
        if (!(cond)) {                                              \
            failures++;                                             \
            printf("FAIL %s:%d: ", __func__, __LINE__);             \
            printf(__VA_ARGS__);                                    \
            printf("\n");                                           \
        }                                                           \
    } while (0)

static void test_identity_copy(void)
{
    static uint8_t src[DW * DH], dst[DW * DH];
    for (int i = 0; i < DW * DH; i++)
        src[i] = (uint8_t)(i * 7);
    resize_bilinear_sw(src, DW, DH, dst);
    CHECK(memcmp(src, dst, sizeof src) == 0,
          "96x96 -> 96x96 is not an exact copy");
}

static void test_constant_image(void)
{
    static uint8_t src[57 * 43], dst[DW * DH];
    memset(src, 137, sizeof src);
    resize_bilinear_sw(src, 57, 43, dst);
    for (int i = 0; i < DW * DH; i++) {
        if (dst[i] != 137) {
            CHECK(0, "constant image not preserved at %d: %u", i, dst[i]);
            return;
        }
    }
}

/* The INTERP blend alpha is 8-bit: 1.0 is unrepresentable (max 255/256),
 * so far-edge samples may land 1 LSB short of the exact source value.
 * The top-left corner (alpha 0) must be exact; the others get ±1. */
static void test_corners_align(void)
{
    static uint8_t dst[DW * DH];
    const uint8_t src[4] = { 10, 250, 60, 180 }; /* 2x2 */
    resize_bilinear_sw(src, 2, 2, dst);
    CHECK(dst[0] == 10, "top-left %u != 10", dst[0]);
    CHECK(abs(dst[DW - 1] - 250) <= 1, "top-right %u !~ 250", dst[DW - 1]);
    CHECK(abs(dst[(DH - 1) * DW] - 60) <= 1, "bottom-left %u !~ 60",
          dst[(DH - 1) * DW]);
    CHECK(abs(dst[DH * DW - 1] - 180) <= 1, "bottom-right %u !~ 180",
          dst[DH * DW - 1]);
}

/* Double-precision reference with the same align-corners mapping. */
static double ref_sample(const uint8_t *src, int w, int h, double x, double y)
{
    int x0 = (int)x, y0 = (int)y;
    int x1 = x0 + 1 < w ? x0 + 1 : x0;
    int y1 = y0 + 1 < h ? y0 + 1 : y0;
    double fx = x - x0, fy = y - y0;
    double top = src[y0 * w + x0] * (1 - fx) + src[y0 * w + x1] * fx;
    double bot = src[y1 * w + x0] * (1 - fx) + src[y1 * w + x1] * fx;
    return top * (1 - fy) + bot * fy;
}

static void test_against_float_reference(void)
{
    /* Odd source sizes to exercise fractional stepping, incl. upscale. */
    static const int sizes[][2] = { {160, 120}, {33, 129}, {200, 50},
                                    {48, 48},   {97, 95} };
    static uint8_t src[200 * 129], dst[DW * DH];
    unsigned rng = 12345;
    int max_err = 0;

    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        const int w = sizes[s][0], h = sizes[s][1];
        for (int i = 0; i < w * h; i++) {
            rng = rng * 1664525u + 1013904223u;
            src[i] = (uint8_t)(rng >> 24);
        }
        resize_bilinear_sw(src, (uint16_t)w, (uint16_t)h, dst);

        const double xs = (double)(w - 1) / (DW - 1);
        const double ys = (double)(h - 1) / (DH - 1);
        for (int dy = 0; dy < DH; dy++) {
            for (int dx = 0; dx < DW; dx++) {
                double want = ref_sample(src, w, h, dx * xs, dy * ys);
                int err = (int)fabs(want - dst[dy * DW + dx]);
                if (err > max_err)
                    max_err = err;
            }
        }
    }
    /* 8-bit alpha quantization + two truncating blends: <= 3 LSB. */
    CHECK(max_err <= 3, "max error vs float reference: %d LSB", max_err);
    printf("  float-reference max error: %d LSB (tolerance 3)\n", max_err);
}

int main(void)
{
    test_identity_copy();
    test_constant_image();
    test_corners_align();
    test_against_float_reference();

    if (failures) {
        printf("%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("all resize tests passed\n");
    return 0;
}
