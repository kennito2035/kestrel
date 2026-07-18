/*
 * img_preproc.h - camera frame (RGB565, row-major) -> model input
 * (uint8 RGB888 192x192, stretch resize, bilinear fixed-point).
 */
#ifndef IMG_PREPROC_H
#define IMG_PREPROC_H

#include <stdint.h>

#define PREPROC_DST_W 192
#define PREPROC_DST_H 192

/* If detections never fire on an otherwise-proven pipeline, flip this:
 * it selects how the DCMI-captured RGB565 halfwords are byte-ordered. */
#define PREPROC_SWAP_BYTES 1

/* WeAct inits the LCD as ST7735_FORMAT_RBG565 (non-standard order), which
 * implies the camera 565 data is R/B-swapped and the LCD compensates.
 * 1 = unpack as BGR565 (R and B swapped vs standard). A/B this against 0:
 * the correct setting gives higher person scores + fewer false fires. */
/* CONFIRMED 0 by on-target red/blue channel test (red bright, blue dark
 * only when 0): camera 565 data is standard RGB; the LCD's "RBG565" init
 * was a red herring. */
#define PREPROC_BGR565 0

/* Rotates the MODEL INPUT (not the display): 0, 90 or 270 degrees.
 * Use 90/270 when the board is physically rotated sideways so the model
 * still sees upright people (person detectors are rotation-variant).
 * Set back to 0 for normal landscape deployment. */
#define PREPROC_ROTATE 0

void preproc_rgb565_to_rgb888(const uint16_t *src, int src_w, int src_h,
                              uint8_t *dst);

/* Same, but samples only the window (wx,wy,ww,wh) of the source, used to
 * crop the gate's motion ROI into the model input ("attention" zoom).
 * PREPROC_ROTATE must be 0 for windowed mode. */
void preproc_rgb565_window(const uint16_t *src, int src_w, int src_h,
                           int wx, int wy, int ww, int wh, uint8_t *dst);

#endif /* IMG_PREPROC_H */
