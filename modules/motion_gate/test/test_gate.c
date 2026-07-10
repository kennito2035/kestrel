/*
 * Host-side tests for the Kestrel motion gate module. No hardware needed:
 *
 *   cc -Wall -Wextra -O2 -o test_gate test_gate.c ../gate.c && ./test_gate
 *
 * Three layers of checking:
 *   1. Deterministic cases with hand-derived expected results.
 *   2. Fuzz: 500 seeded random frames compared against `naive_check`, an
 *      independent from-spec reimplementation (single full scan, no
 *      two-pass optimization); catches bugs in gate.c's fast paths.
 *   3. Dumps fuzz results to results.csv; golden.py recomputes the same
 *      seeded cases in Python and cross-checks, guarding against a shared
 *      misconception in the two C implementations.
 *
 * The Cortex-M SIMD path (KESTREL_GATE_SIMD) cannot run on the host; it is
 * bit-exact by construction and verified on target via benchmark.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../gate.h"

#define W 160
#define H 120

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

/* ------------------------------------------------------------------ */
/* Independent reference implementation (from the spec, single pass).  */
/* ------------------------------------------------------------------ */

static gate_state_t naive_check(const gate_config_t *cfg,
                                const uint8_t *curr, const uint8_t *prev,
                                gate_roi_t *roi, uint32_t *count_out)
{
    int thr = cfg->pixel_threshold > 254 ? 254 : cfg->pixel_threshold;
    uint32_t count = 0;
    long xmin = cfg->width, xmax = -1, ymin = cfg->height, ymax = -1;

    for (long y = 0; y < cfg->height; y++) {
        for (long x = 0; x < cfg->width; x++) {
            long i = y * cfg->width + x;
            int d = abs((int)curr[i] - (int)prev[i]);
            if (d > thr) {
                count++;
                if (x < xmin) xmin = x;
                if (x > xmax) xmax = x;
                if (y < ymin) ymin = y;
                if (y > ymax) ymax = y;
            }
        }
    }
    if (count_out) *count_out = count;
    if (count == 0 || count < cfg->open_count) return GATE_CLOSED;

    long x0 = xmin - cfg->roi_pad, x1 = xmax + cfg->roi_pad;
    long y0 = ymin - cfg->roi_pad, y1 = ymax + cfg->roi_pad;
    long side = (x1 - x0 > y1 - y0) ? x1 - x0 + 1 : y1 - y0 + 1;
    long maxside = cfg->width < cfg->height ? cfg->width : cfg->height;
    if (side > maxside) side = maxside;
    long rx = (x0 + x1) / 2 - side / 2;
    long ry = (y0 + y1) / 2 - side / 2;
    if (rx < 0) rx = 0;
    if (rx + side > cfg->width) rx = cfg->width - side;
    if (ry < 0) ry = 0;
    if (ry + side > cfg->height) ry = cfg->height - side;
    roi->x = (uint16_t)rx; roi->y = (uint16_t)ry;
    roi->w = (uint16_t)side; roi->h = (uint16_t)side;
    return GATE_OPEN;
}

/* ------------------------------------------------------------------ */
/* Deterministic cases                                                 */
/* ------------------------------------------------------------------ */

static gate_config_t default_cfg(void)
{
    gate_config_t cfg = { W, H, 25, 96, 8 };
    return cfg;
}

static void test_identical_frames(void)
{
    static uint8_t a[W * H];
    gate_config_t cfg = default_cfg();
    gate_roi_t roi;
    uint32_t count = 123;
    memset(a, 0x5A, sizeof a);
    CHECK(gate_check(&cfg, a, a, &roi, &count) == GATE_CLOSED, "gate open");
    CHECK(count == 0, "count %u != 0", count);
}

static void test_threshold_is_strict(void)
{
    static uint8_t a[W * H], b[W * H];
    gate_config_t cfg = default_cfg();
    gate_roi_t roi;
    uint32_t count;
    memset(a, 100, sizeof a);
    memcpy(b, a, sizeof a);

    b[60 * W + 50] = 100 + 25;          /* diff == thr: NOT counted */
    gate_check(&cfg, b, a, &roi, &count);
    CHECK(count == 0, "diff==thr counted (%u)", count);

    b[60 * W + 50] = 100 + 26;          /* diff == thr+1: counted */
    gate_check(&cfg, b, a, &roi, &count);
    CHECK(count == 1, "diff==thr+1 not counted (%u)", count);
}

static void test_single_pixel_opens_when_configured(void)
{
    static uint8_t a[W * H], b[W * H];
    gate_config_t cfg = default_cfg();
    gate_roi_t roi;
    cfg.open_count = 1;
    memset(a, 0, sizeof a);
    memcpy(b, a, sizeof a);
    b[60 * W + 50] = 255;

    CHECK(gate_check(&cfg, b, a, &roi, NULL) == GATE_OPEN, "gate closed");
    /* 1x1 bbox + 8px pad each side -> 17x17 square centered on (50,60) */
    CHECK(roi.w == 17 && roi.h == 17, "roi %ux%u != 17x17", roi.w, roi.h);
    CHECK(roi.x <= 50 && 50 < roi.x + roi.w &&
          roi.y <= 60 && 60 < roi.y + roi.h,
          "roi (%u,%u,%u,%u) misses (50,60)", roi.x, roi.y, roi.w, roi.h);
}

static void test_corner_blob_clamps_in_frame(void)
{
    static uint8_t a[W * H], b[W * H];
    gate_config_t cfg = default_cfg();
    gate_roi_t roi;
    cfg.open_count = 1;
    memset(a, 0, sizeof a);
    memcpy(b, a, sizeof a);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            b[y * W + x] = 200;

    CHECK(gate_check(&cfg, b, a, &roi, NULL) == GATE_OPEN, "gate closed");
    CHECK(roi.x == 0 && roi.y == 0, "not clamped to corner (%u,%u)",
          roi.x, roi.y);
    CHECK(roi.w == 20 && roi.h == 20, "roi %ux%u != 20x20", roi.w, roi.h);
}

static void test_full_frame_change(void)
{
    static uint8_t a[W * H], b[W * H];
    gate_config_t cfg = default_cfg();
    gate_roi_t roi;
    memset(a, 0, sizeof a);
    memset(b, 200, sizeof b);

    CHECK(gate_check(&cfg, b, a, &roi, NULL) == GATE_OPEN, "gate closed");
    CHECK(roi.w == H && roi.h == H, "side %u != min(W,H)=%u", roi.w, H);
    CHECK(roi.x + roi.w <= W && roi.y + roi.h <= H, "roi out of frame");
}

static void test_two_blobs_union_bbox(void)
{
    static uint8_t a[W * H], b[W * H];
    gate_config_t cfg = default_cfg();
    gate_roi_t roi, ref_roi;
    uint32_t count, ref_count;
    cfg.open_count = 1;
    memset(a, 0, sizeof a);
    memcpy(b, a, sizeof a);
    b[10 * W + 10] = 255;
    b[100 * W + 140] = 255;

    gate_state_t s = gate_check(&cfg, b, a, &roi, &count);
    gate_state_t rs = naive_check(&cfg, b, a, &ref_roi, &ref_count);
    CHECK(s == GATE_OPEN && s == rs, "state mismatch");
    CHECK(count == 2 && count == ref_count, "count %u", count);
    CHECK(memcmp(&roi, &ref_roi, sizeof roi) == 0,
          "roi (%u,%u,%u,%u) != ref (%u,%u,%u,%u)",
          roi.x, roi.y, roi.w, roi.h,
          ref_roi.x, ref_roi.y, ref_roi.w, ref_roi.h);
}

/* ------------------------------------------------------------------ */
/* Seeded fuzz vs naive reference + CSV dump for golden.py             */
/* ------------------------------------------------------------------ */

static uint32_t lcg_state;

static uint8_t rb(void)
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return (uint8_t)(lcg_state >> 24);
}

static uint8_t clamp_u8(int v)
{
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

/* Deterministic frame pair; golden.py replicates this bit-for-bit. */
static void make_frames(uint32_t seed, uint8_t *prev, uint8_t *curr)
{
    lcg_state = seed;
    for (long i = 0; i < W * H; i++)
        prev[i] = rb();
    for (long i = 0; i < W * H; i++)
        curr[i] = clamp_u8((int)prev[i] + (int)(rb() % 21) - 10);
    if (rb() % 4 != 0) {                       /* 75%: inject a motion blob */
        int bw = 4 + rb() % 40, bh = 4 + rb() % 40;
        int bx = rb() % (W - bw), by = rb() % (H - bh);
        for (int y = by; y < by + bh; y++)
            for (int x = bx; x < bx + bw; x++)
                if (rb() % 2)
                    curr[y * W + x] = clamp_u8((int)curr[y * W + x] + 60);
    }
}

static void test_fuzz_vs_naive_and_dump_csv(void)
{
    static uint8_t prev[W * H], curr[W * H];
    gate_config_t cfg = default_cfg();
    FILE *csv = fopen("results.csv", "w");
    CHECK(csv != NULL, "cannot open results.csv");
    if (csv)
        fprintf(csv, "seed,state,count,x,y,w,h\n");

    for (uint32_t seed = 1; seed <= 500; seed++) {
        gate_roi_t roi = { 0, 0, 0, 0 }, ref_roi = { 0, 0, 0, 0 };
        uint32_t count, ref_count;
        make_frames(seed, prev, curr);
        gate_state_t s = gate_check(&cfg, curr, prev, &roi, &count);
        gate_state_t rs = naive_check(&cfg, curr, prev, &ref_roi, &ref_count);

        CHECK(s == rs, "seed %u: state %d != ref %d", seed, s, rs);
        CHECK(count == ref_count, "seed %u: count %u != ref %u",
              seed, count, ref_count);
        if (s == GATE_OPEN && rs == GATE_OPEN) {
            CHECK(memcmp(&roi, &ref_roi, sizeof roi) == 0,
                  "seed %u: roi (%u,%u,%u,%u) != ref (%u,%u,%u,%u)", seed,
                  roi.x, roi.y, roi.w, roi.h,
                  ref_roi.x, ref_roi.y, ref_roi.w, ref_roi.h);
            CHECK(roi.w == roi.h, "seed %u: roi not square", seed);
            CHECK(roi.x + roi.w <= W && roi.y + roi.h <= H,
                  "seed %u: roi out of frame", seed);
        }
        if (csv)
            fprintf(csv, "%u,%d,%u,%u,%u,%u,%u\n", seed, (int)s, count,
                    roi.x, roi.y, roi.w, roi.h);
    }
    if (csv)
        fclose(csv);
}

int main(void)
{
    test_identical_frames();
    test_threshold_is_strict();
    test_single_pixel_opens_when_configured();
    test_corner_blob_clamps_in_frame();
    test_full_frame_change();
    test_two_blobs_union_bbox();
    test_fuzz_vs_naive_and_dump_csv();

    if (failures) {
        printf("%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("all tests passed (6 deterministic + 500 fuzz cases)\n");
    printf("wrote results.csv; run `python3 golden.py` to cross-check\n");
    return 0;
}
