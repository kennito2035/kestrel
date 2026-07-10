/*
 * Bilinear resize with and without the RP2350 hardware interpolator.
 *
 * The INTERP unit's blend mode computes, in one register read:
 *     PEEK1 = BASE0 + (((BASE1 - BASE0) * (ACCUM1 & 0xFF)) >> 8)
 * i.e. a linear interpolation between two VALUES with an 8-bit alpha.
 * (It does not walk memory for you; bases are values, not pointers.)
 *
 * Bilinear needs three lerps per output pixel: two horizontal (top and
 * bottom source pixel pairs) and one vertical. Blend mode is only present
 * on INTERP0 (INTERP1 has clamp mode instead), so INTERP0 performs both
 * horizontal lerps back-to-back and the vertical lerp stays in software,
 * making the hardware path bit-exact with resize_bilinear_sw below.
 *
 * Sampling is align-corners: source step = (src-1)/(dst-1) in 16.16 fixed
 * point, so the four frame corners map exactly and a 96x96 -> 96x96 resize
 * is an exact copy (a property the host tests rely on).
 *
 * License: MIT (see repository root).
 */
#include "hw_interpolator_resize.h"

/* The exact blend equation implemented by INTERP blend mode. */
static inline uint8_t blend8(uint8_t a, uint8_t b, uint8_t alpha)
{
    return (uint8_t)(a + ((((int32_t)b - (int32_t)a) * alpha) >> 8));
}

static inline uint32_t step_fp16(uint16_t src_dim, uint16_t dst_dim)
{
    /* align-corners: (src-1)/(dst-1) in 16.16; dst_dim is >= 2 here */
    return ((uint32_t)(src_dim - 1) << 16) / (uint32_t)(dst_dim - 1);
}

void resize_bilinear_sw(const uint8_t *src, uint16_t src_w, uint16_t src_h,
                        uint8_t *dst)
{
    const uint32_t x_step = step_fp16(src_w, KESTREL_DST_W);
    const uint32_t y_step = step_fp16(src_h, KESTREL_DST_H);

    uint32_t y_fp = 0;
    for (uint16_t dy = 0; dy < KESTREL_DST_H; dy++, y_fp += y_step) {
        const uint32_t sy = y_fp >> 16;
        const uint8_t fy = (uint8_t)(y_fp >> 8);
        const uint8_t *row0 = src + sy * src_w;
        /* sy+1 may fall past the last row only when fy == 0 (align-corners
         * puts the final sample exactly on the last row); clamp is safe. */
        const uint8_t *row1 = (sy + 1 < src_h) ? row0 + src_w : row0;

        uint32_t x_fp = 0;
        for (uint16_t dx = 0; dx < KESTREL_DST_W; dx++, x_fp += x_step) {
            const uint32_t sx = x_fp >> 16;
            const uint8_t fx = (uint8_t)(x_fp >> 8);
            const uint32_t sx1 = (sx + 1 < src_w) ? sx + 1 : sx;

            const uint8_t top = blend8(row0[sx], row0[sx1], fx);
            const uint8_t bot = blend8(row1[sx], row1[sx1], fx);
            dst[(uint32_t)dy * KESTREL_DST_W + dx] = blend8(top, bot, fy);
        }
    }
}

#if !defined(KESTREL_HOST_BUILD)

#include "hardware/interp.h"

void resize_bilinear_interp(const uint8_t *src, uint16_t src_w, uint16_t src_h,
                            uint8_t *dst)
{
    /* Blend mode exists on INTERP0 ONLY; INTERP1 has clamp mode instead
     * (RP2350 datasheet §3.1.10, same as RP2040). So INTERP0 is reused for
     * both horizontal lerps: top pixel pair, then bottom pair. The result
     * is combinatorial (valid the cycle after the writes), so back-to-back
     * use costs only the register accesses. */
    interp_config cfg = interp_default_config();
    interp_config_set_blend(&cfg, true);
    interp_set_config(interp0, 0, &cfg);

    const uint32_t x_step = step_fp16(src_w, KESTREL_DST_W);
    const uint32_t y_step = step_fp16(src_h, KESTREL_DST_H);

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
            interp0->base[0] = row0[sx];
            interp0->base[1] = row0[sx1];
            const uint8_t top = (uint8_t)interp0->peek[1];
            interp0->base[0] = row1[sx];
            interp0->base[1] = row1[sx1];
            const uint8_t bot = (uint8_t)interp0->peek[1];

            dst[(uint32_t)dy * KESTREL_DST_W + dx] = blend8(top, bot, fy);
        }
    }
}

#endif /* !KESTREL_HOST_BUILD */
