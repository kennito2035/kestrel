/*
 * Kestrel motion gate, frame-difference inference gating + attention ROI.
 *
 * Pure C99, no dependencies, no allocation, no float. Drop into any
 * Cortex-M (or other) camera pipeline that produces consecutive 8-bit
 * grayscale frames.
 *
 * Usage:
 *   gate_config_t cfg = {
 *       .width = 160, .height = 120,
 *       .pixel_threshold = 25,   // per-pixel |diff| must EXCEED this
 *       .open_count = 96,        // >= this many changed pixels opens the gate
 *       .roi_pad = 8,            // padding around the raw motion bbox
 *   };
 *   gate_roi_t roi;
 *   uint32_t changed;
 *   if (gate_check(&cfg, curr, prev, &roi, &changed) == GATE_OPEN) {
 *       // crop `roi` from the frame, resize to the model input, run inference
 *   } // else: skip inference entirely
 *
 * The returned ROI is always square (for aspect-correct resize into a square
 * model input), padded by roi_pad, and clamped inside the frame. Its side
 * never exceeds min(width, height).
 *
 * Fast path: on Cortex-M cores with the SIMD32 extension (M4/M7/M33 with
 * DSP), compile with -DKESTREL_GATE_SIMD=1 to process 4 pixels per cycle
 * batch using UQSUB8/USUB8/SEL/USADA8. Requires `width % 4 == 0` and
 * word-aligned frame buffers (true for DMA-filled buffers); otherwise the
 * module silently falls back to the scalar path. The scalar and SIMD paths
 * are bit-exact; pixel_threshold is clamped to 254 (a threshold of 255 can
 * never be exceeded anyway).
 *
 * License: MIT (see repository root).
 */
#ifndef KESTREL_GATE_H
#define KESTREL_GATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t width;           /* frame width in pixels                        */
    uint16_t height;          /* frame height in pixels                       */
    uint8_t  pixel_threshold; /* |curr - prev| must exceed this to count      */
    uint32_t open_count;      /* changed-pixel count that opens the gate      */
    uint16_t roi_pad;         /* padding (pixels) around the raw motion bbox  */
} gate_config_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} gate_roi_t;

typedef enum {
    GATE_CLOSED = 0, /* scene unchanged; skip inference        */
    GATE_OPEN   = 1  /* motion detected; roi_out is populated  */
} gate_state_t;

/*
 * Compare two grayscale frames (width*height bytes each).
 *
 * Returns GATE_OPEN and fills *roi_out when at least cfg->open_count pixels
 * changed; returns GATE_CLOSED otherwise (*roi_out untouched).
 * *changed_count_out (optional, may be NULL) always receives the number of
 * changed pixels, useful for threshold tuning and skip-rate logging.
 */
gate_state_t gate_check(const gate_config_t *cfg,
                        const uint8_t *curr,
                        const uint8_t *prev,
                        gate_roi_t *roi_out,
                        uint32_t *changed_count_out);

#ifdef __cplusplus
}
#endif

#endif /* KESTREL_GATE_H */
