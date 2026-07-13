/*
 * img_preproc.c - RGB565 -> RGB888 bilinear stretch resize (16.16 fixed point),
 * with optional 90/270-degree input rotation (see PREPROC_ROTATE).
 * Stretch (no letterbox) matches the model zoo's default training preprocessing
 * (aspect_ratio: fit). ~37k output pixels: a few ms at 480MHz.
 */
#include "img_preproc.h"

static inline void unpack565(uint16_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
#if PREPROC_SWAP_BYTES
  v = (uint16_t)((v >> 8) | (v << 8));
#endif
#if PREPROC_BGR565
  { uint8_t *t = r; r = b; b = t; }   /* camera 565 is B-G-R ordered */
#endif
  /* 5/6/5 expanded to 8-bit with bit replication */
  *r = (uint8_t)(((v >> 11) & 0x1F) << 3); *r |= *r >> 5;
  *g = (uint8_t)(((v >> 5)  & 0x3F) << 2); *g |= *g >> 6;
  *b = (uint8_t)(( v        & 0x1F) << 3); *b |= *b >> 5;
}

/* Bilinear sample of the physical src at 16.16 fixed-point coords. */
static inline void sample_bilinear(const uint16_t *src, int src_w, int src_h,
                                   uint32_t pxf, uint32_t pyf, uint8_t *out)
{
  int x0 = (int)(pxf >> 16);
  int y0 = (int)(pyf >> 16);
  int x1 = (x0 + 1 < src_w) ? x0 + 1 : x0;
  int y1 = (y0 + 1 < src_h) ? y0 + 1 : y0;
  uint32_t fx = pxf & 0xFFFF;
  uint32_t fy = pyf & 0xFFFF;

  const uint16_t *row0 = src + y0 * src_w;
  const uint16_t *row1 = src + y1 * src_w;

  uint8_t r00, g00, b00, r01, g01, b01, r10, g10, b10, r11, g11, b11;
  unpack565(row0[x0], &r00, &g00, &b00);
  unpack565(row0[x1], &r01, &g01, &b01);
  unpack565(row1[x0], &r10, &g10, &b10);
  unpack565(row1[x1], &r11, &g11, &b11);

  int32_t r0 = r00 + (int32_t)((fx * (int32_t)(r01 - r00)) >> 16);
  int32_t g0 = g00 + (int32_t)((fx * (int32_t)(g01 - g00)) >> 16);
  int32_t b0 = b00 + (int32_t)((fx * (int32_t)(b01 - b00)) >> 16);
  int32_t r1 = r10 + (int32_t)((fx * (int32_t)(r11 - r10)) >> 16);
  int32_t g1 = g10 + (int32_t)((fx * (int32_t)(g11 - g10)) >> 16);
  int32_t b1 = b10 + (int32_t)((fx * (int32_t)(b11 - b10)) >> 16);

  out[0] = (uint8_t)(r0 + (int32_t)((fy * (int32_t)(r1 - r0)) >> 16));
  out[1] = (uint8_t)(g0 + (int32_t)((fy * (int32_t)(g1 - g0)) >> 16));
  out[2] = (uint8_t)(b0 + (int32_t)((fy * (int32_t)(b1 - b0)) >> 16));
}

void preproc_rgb565_to_rgb888(const uint16_t *src, int src_w, int src_h,
                              uint8_t *dst)
{
  /* Logical (model-facing) image dims: swapped when rotating. */
#if PREPROC_ROTATE == 0
  const int lw = src_w, lh = src_h;
#else
  const int lw = src_h, lh = src_w;
#endif

  const uint32_t x_step = ((uint32_t)(lw - 1) << 16) / (PREPROC_DST_W - 1);
  const uint32_t y_step = ((uint32_t)(lh - 1) << 16) / (PREPROC_DST_H - 1);

  uint32_t ly = 0;
  for (int dy = 0; dy < PREPROC_DST_H; dy++, ly += y_step)
  {
    uint32_t lx = 0;
    for (int dx = 0; dx < PREPROC_DST_W; dx++, lx += x_step)
    {
      uint32_t pxf, pyf;
#if PREPROC_ROTATE == 0
      pxf = lx;
      pyf = ly;
#elif PREPROC_ROTATE == 90
      pxf = ly;
      pyf = (((uint32_t)(src_h - 1)) << 16) - lx;
#elif PREPROC_ROTATE == 270
      pxf = (((uint32_t)(src_w - 1)) << 16) - ly;
      pyf = lx;
#else
#error "PREPROC_ROTATE must be 0, 90 or 270"
#endif
      sample_bilinear(src, src_w, src_h, pxf, pyf, dst);
      dst += 3;
    }
  }
}
