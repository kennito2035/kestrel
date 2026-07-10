/*
 * Bilinear image resize for ML input preprocessing on the RP2350,
 * with and without the hardware interpolator (INTERP) unit.
 *
 * Both paths are bit-exact with each other: the INTERP blend equation is
 *   result = base0 + (((base1 - base0) * (accum1 & 0xFF)) >> 8)
 * (RP2350 datasheet §3.1.10) and the software path uses the identical
 * 8-bit-alpha blend, so outputs can be diffed byte-for-byte on target.
 *
 * Precision note: the blend alpha is 8-bit, so a weight of exactly 1.0 is
 * unrepresentable (max 255/256). Far-edge samples can therefore land 1 LSB
 * below the exact source value; overall error vs a float bilinear reference
 * is <= 3 LSB (asserted in test/test_resize.c). This is the INTERP unit's
 * native precision, acceptable for ML input preprocessing, where inputs
 * are quantized to INT8 anyway.
 *
 * Grayscale 8-bit, align-corners sampling. Destination is always
 * KESTREL_DST_W x KESTREL_DST_H (96x96 by default, a typical tiny-detector
 * input). Source must be at least 2x2.
 *
 * License: MIT (see repository root).
 */
#ifndef KESTREL_HW_INTERPOLATOR_RESIZE_H
#define KESTREL_HW_INTERPOLATOR_RESIZE_H

#include <stdint.h>

#ifndef KESTREL_DST_W
#define KESTREL_DST_W 96
#endif
#ifndef KESTREL_DST_H
#define KESTREL_DST_H 96
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Portable software path: fixed-point bilinear, runs anywhere (host-testable). */
void resize_bilinear_sw(const uint8_t *src, uint16_t src_w, uint16_t src_h,
                        uint8_t *dst);

#if !defined(KESTREL_HOST_BUILD)
/* INTERP-assisted path: INTERP0 blends the top pixel pair, INTERP1 the
 * bottom pair, final vertical blend in software. RP2040/RP2350 only. */
void resize_bilinear_interp(const uint8_t *src, uint16_t src_w, uint16_t src_h,
                            uint8_t *dst);
#endif

#ifdef __cplusplus
}
#endif

#endif /* KESTREL_HW_INTERPOLATOR_RESIZE_H */
