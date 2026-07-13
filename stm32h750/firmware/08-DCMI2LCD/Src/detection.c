/*
 * detection.c - YOLOv2-style anchor decode + NMS for st_yolo_lc_v1 (1 class).
 * Layout assumption per cell: anchor-major [a][tx,ty,tw,th,tobj,tclass]
 * (standard darknet/ST head layout). If detections are ever garbage while the
 * pipeline is proven, this interleave is the first thing to re-check.
 */
#include <math.h>
#include "detection.h"

/* ST model-zoo default anchors, normalized (w,h) pairs. */
static const float det_anchors[DET_NB_ANCHORS][2] = {
  {0.076023f, 0.258508f},
  {0.163031f, 0.413531f},
  {0.234769f, 0.702585f},
  {0.427054f, 0.715999f},
  {0.748376f, 0.857993f},
};

#define DET_MAX_CAND  24

/* Highest objectness seen in the last decode, BEFORE thresholding;
 * lets the UI show "how close" the model is even when no box fires. */
float det_last_max_score = 0.0f;
/* Raw p[2],p[3] logits of the highest-objectness anchor (debug). */
float det_last_tw = 0.0f, det_last_th = 0.0f;

static inline float sigmoidf_(float x) { return 1.0f / (1.0f + expf(-x)); }

static float iou(const det_box_t *a, const det_box_t *b)
{
  float ax0 = a->x - a->w * 0.5f, ax1 = a->x + a->w * 0.5f;
  float ay0 = a->y - a->h * 0.5f, ay1 = a->y + a->h * 0.5f;
  float bx0 = b->x - b->w * 0.5f, bx1 = b->x + b->w * 0.5f;
  float by0 = b->y - b->h * 0.5f, by1 = b->y + b->h * 0.5f;

  float ix = fminf(ax1, bx1) - fmaxf(ax0, bx0);
  float iy = fminf(ay1, by1) - fmaxf(ay0, by0);
  if (ix <= 0.0f || iy <= 0.0f)
    return 0.0f;

  float inter = ix * iy;
  float uni = a->w * a->h + b->w * b->h - inter;
  return (uni > 0.0f) ? inter / uni : 0.0f;
}

int detection_decode(const float *net_out, det_box_t *boxes, int max_boxes)
{
  det_box_t cand[DET_MAX_CAND];
  int n_cand = 0;

  det_last_max_score = 0.0f;

  for (int row = 0; row < DET_GRID; row++)
  {
    for (int col = 0; col < DET_GRID; col++)
    {
      const float *cell = net_out + ((row * DET_GRID) + col) * (DET_NB_ANCHORS * 6);
      for (int a = 0; a < DET_NB_ANCHORS; a++)
      {
        const float *p = cell + a * 6;
        float score = sigmoidf_(p[4]);   /* NC=1: softmax over 1 class == 1 */
        if (score > det_last_max_score)
        {
          det_last_max_score = score;
          det_last_tw = p[2];
          det_last_th = p[3];
        }
        if (score < DET_CONF_THRESH)
          continue;
        if (n_cand >= DET_MAX_CAND)
          break;

        det_box_t *d = &cand[n_cand++];
        d->x = ((float)col + sigmoidf_(p[0])) / (float)DET_GRID;
        d->y = ((float)row + sigmoidf_(p[1])) / (float)DET_GRID;
        /* st_yolo_lc_v1's lightweight head outputs sizes as DIRECT sigmoid
         * fractions of the image, not darknet exp(t)*anchor log-scales.
         * Proven on-target: raw size logits 1.3-2.5 on a frame-filling person
         * (exp explodes to w=2.6/h=7.0; sigmoid gives the true ~0.8/0.9). */
        d->w = sigmoidf_(p[2]) * DET_W_SCALE;
        d->h = sigmoidf_(p[3]) * DET_H_SCALE;
        (void)det_anchors[a];
        d->score = score;
      }
    }
  }

  /* Sort candidates by score, descending (tiny N: selection sort). */
  for (int i = 0; i < n_cand - 1; i++)
  {
    int best = i;
    for (int j = i + 1; j < n_cand; j++)
      if (cand[j].score > cand[best].score)
        best = j;
    if (best != i)
    {
      det_box_t tmp = cand[i];
      cand[i] = cand[best];
      cand[best] = tmp;
    }
  }

  /* Greedy NMS. */
  int n_out = 0;
  for (int i = 0; i < n_cand && n_out < max_boxes; i++)
  {
    int keep = 1;
    for (int k = 0; k < n_out; k++)
    {
      if (iou(&cand[i], &boxes[k]) > DET_IOU_THRESH)
      {
        keep = 0;
        break;
      }
    }
    if (keep)
      boxes[n_out++] = cand[i];
  }

  return n_out;
}
