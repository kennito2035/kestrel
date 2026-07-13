/*
 * detection.h - YOLOv2-style decode + NMS for st_yolo_lc_v1 192x192 (COCO person).
 * Output tensor: f32 [12][12][30] HWC, 30 = 5 anchors x (x,y,w,h,obj,1 class).
 * Anchors: ST model-zoo defaults (normalized), cross-validated against
 * stm32-hotspot/STM32N6-AI-Assistant-People-Detection postprocess_conf.h
 * (their grid-unit values = these x13, matching to 4 decimals).
 */
#ifndef DETECTION_H
#define DETECTION_H

#include <stdint.h>

#define DET_GRID          12
#define DET_NB_ANCHORS    5
#define DET_CONF_THRESH   0.68f
#define DET_IOU_THRESH    0.45f
#define DET_MAX_BOXES     8
/* Empirical size calibration: head predicts slightly oversized boxes */
#define DET_W_SCALE       0.55f
#define DET_H_SCALE       0.80f

typedef struct
{
  float x, y;     /* box center, normalized 0..1 (full model frame) */
  float w, h;     /* box size,  normalized 0..1 */
  float score;    /* objectness * class prob */
} det_box_t;

/* Decodes the raw 12x12x30 output and applies NMS.
 * Returns number of boxes written to `boxes` (0..max_boxes). */
int detection_decode(const float *net_out, det_box_t *boxes, int max_boxes);

/* Max objectness of the last decode, pre-threshold (0..1). */
extern float det_last_max_score;
extern float det_last_tw, det_last_th;   /* raw size logits (debug) */

#endif /* DETECTION_H */
