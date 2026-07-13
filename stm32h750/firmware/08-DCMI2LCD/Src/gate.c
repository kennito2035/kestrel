/*
 * Kestrel motion gate: implementation.
 *
 * Two-pass design, optimized for the common case:
 *
 *   Pass 1  counts changed pixels per row (SIMD fast path available).
 *           On an unchanged scene this is the ONLY work done per frame,
 *           so it is the number that must stay sub-millisecond.
 *   Pass 2  runs only when the gate opens: scans just the changed rows
 *           for the horizontal bounds of the motion region. Gate-open
 *           frames pay for a full inference anyway, so this scalar scan
 *           is noise by comparison.
 *
 * License: MIT (see repository root).
 */
/* Kestrel firmware copy: enable the M7 SIMD fast path by default. */
#ifndef KESTREL_GATE_SIMD
#define KESTREL_GATE_SIMD 1
#endif

#include "gate.h"

#if defined(KESTREL_GATE_SIMD) && KESTREL_GATE_SIMD && defined(__ARM_FEATURE_SIMD32)
#include <arm_acle.h>
#define GATE_HAVE_SIMD 1
#else
#define GATE_HAVE_SIMD 0
#endif

/* Number of pixels in `row` whose |curr-prev| exceeds thr (thr <= 254). */
static uint32_t row_changed_scalar(const uint8_t *curr, const uint8_t *prev,
                                   uint16_t width, uint8_t thr)
{
    uint32_t count = 0;
    for (uint16_t x = 0; x < width; x++) {
        int d = (int)curr[x] - (int)prev[x];
        if (d < 0) {
            d = -d;
        }
        if (d > (int)thr) {
            count++;
        }
    }
    return count;
}

#if GATE_HAVE_SIMD
/*
 * 4 pixels per iteration:
 *   absdiff per byte:  d = UQSUB8(a,b) | UQSUB8(b,a)
 *   changed per byte:  USUB8(d, thr+1) sets the GE flag for bytes where
 *                      d >= thr+1  (i.e. d > thr), SEL then picks 0x01
 *                      for changed bytes and 0x00 otherwise
 *   count:             USADA8 accumulates the 0x01 bytes
 * Bit-exact with row_changed_scalar.
 */
static uint32_t row_changed_simd(const uint8_t *curr, const uint8_t *prev,
                                 uint16_t width, uint8_t thr)
{
    const uint32_t *a = (const uint32_t *)(const void *)curr;
    const uint32_t *b = (const uint32_t *)(const void *)prev;
    const uint32_t thr1 = ((uint32_t)thr + 1u) * 0x01010101u;
    const uint32_t ones = 0x01010101u;
    uint32_t count = 0;

    for (uint16_t i = 0; i < width / 4u; i++) {
        uint32_t d = __uqsub8(a[i], b[i]) | __uqsub8(b[i], a[i]);
        (void)__usub8(d, thr1);              /* sets GE flags per byte */
        count = __usada8(__sel(ones, 0u), 0u, count);
    }
    return count;
}
#endif

static uint32_t row_changed(const uint8_t *curr, const uint8_t *prev,
                            uint16_t width, uint8_t thr)
{
#if GATE_HAVE_SIMD
    if ((width % 4u) == 0u &&
        (((uintptr_t)curr | (uintptr_t)prev) & 3u) == 0u) {
        return row_changed_simd(curr, prev, width, thr);
    }
#endif
    return row_changed_scalar(curr, prev, width, thr);
}

/* Pad the raw bbox, grow it to a square, clamp it inside the frame. */
static void build_roi(const gate_config_t *cfg,
                      int32_t x_min, int32_t x_max,
                      int32_t y_min, int32_t y_max,
                      gate_roi_t *out)
{
    const int32_t frame_w = cfg->width;
    const int32_t frame_h = cfg->height;

    const int32_t x0 = x_min - cfg->roi_pad;
    const int32_t y0 = y_min - cfg->roi_pad;
    const int32_t x1 = x_max + cfg->roi_pad;
    const int32_t y1 = y_max + cfg->roi_pad;

    int32_t side = (x1 - x0 > y1 - y0) ? (x1 - x0 + 1) : (y1 - y0 + 1);
    const int32_t max_side = (frame_w < frame_h) ? frame_w : frame_h;
    if (side > max_side) {
        side = max_side;
    }

    /* Center the square on the (padded) bbox center, then clamp in-frame. */
    int32_t rx = (x0 + x1) / 2 - side / 2;
    int32_t ry = (y0 + y1) / 2 - side / 2;
    if (rx < 0) {
        rx = 0;
    } else if (rx + side > frame_w) {
        rx = frame_w - side;
    }
    if (ry < 0) {
        ry = 0;
    } else if (ry + side > frame_h) {
        ry = frame_h - side;
    }

    out->x = (uint16_t)rx;
    out->y = (uint16_t)ry;
    out->w = (uint16_t)side;
    out->h = (uint16_t)side;
}

gate_state_t gate_check(const gate_config_t *cfg,
                        const uint8_t *curr,
                        const uint8_t *prev,
                        gate_roi_t *roi_out,
                        uint32_t *changed_count_out)
{
    /* A threshold of 255 can never be exceeded; clamp so thr+1 fits a byte. */
    const uint8_t thr = (cfg->pixel_threshold > 254u) ? 254u
                                                      : cfg->pixel_threshold;
    uint32_t total = 0;
    int32_t y_min = -1;
    int32_t y_max = -1;

    /* Pass 1: changed-pixel count + vertical bounds. */
    for (uint16_t y = 0; y < cfg->height; y++) {
        const uint32_t offset = (uint32_t)y * cfg->width;
        const uint32_t rc = row_changed(curr + offset, prev + offset,
                                        cfg->width, thr);
        if (rc != 0u) {
            if (y_min < 0) {
                y_min = y;
            }
            y_max = y;
            total += rc;
        }
    }

    if (changed_count_out != 0) {
        *changed_count_out = total;
    }
    if (total == 0u || total < cfg->open_count) {
        return GATE_CLOSED;
    }

    /* Pass 2: horizontal bounds, scanning only the rows known to change. */
    int32_t x_min = cfg->width;
    int32_t x_max = -1;
    for (int32_t y = y_min; y <= y_max; y++) {
        const uint8_t *ca = curr + (uint32_t)y * cfg->width;
        const uint8_t *pa = prev + (uint32_t)y * cfg->width;
        for (int32_t x = 0; x < x_min; x++) {
            int d = (int)ca[x] - (int)pa[x];
            if ((d < 0 ? -d : d) > (int)thr) {
                x_min = x;
                break;
            }
        }
        for (int32_t x = cfg->width - 1; x > x_max; x--) {
            int d = (int)ca[x] - (int)pa[x];
            if ((d < 0 ? -d : d) > (int)thr) {
                x_max = x;
                break;
            }
        }
    }

    build_roi(cfg, x_min, x_max, y_min, y_max, roi_out);
    return GATE_OPEN;
}
