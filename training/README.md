# Model

Kestrel's deployed model is **st_yololcv1 192×192 INT8** ("YOLO
low-complexity v1") from the
[ST Model Zoo](https://github.com/STMicroelectronics/stm32ai-modelzoo),
pretrained on **COCO 2017 person**, purpose-built for STM32 MCUs.

Download (one file, ~331KB; lands in this folder, which gitignores
`*.tflite`):

```bash
curl -L -o training/st_yololcv1_192_int8.tflite \
  "https://github.com/STMicroelectronics/stm32ai-modelzoo/raw/main/object_detection/st_yololcv1/ST_pretrainedmodel_public_dataset/coco_2017_person/st_yololcv1_192/st_yololcv1_192_int8.tflite"
```

## Why this model

ST's published benchmarks (STM32H747I-DISCO @ 400MHz, single core):

| Variant | Flash | RAM | Inference |
|---|---|---|---|
| **192×192 int8 (deployed)** | **307.9 KB** | **166.3 KB** | **179.3 ms** |
| 224×224 int8 | 307.9 KB | 217.3 KB | 245.1 ms |
| 256×256 int8 | 307.9 KB | 278.3 KB | 321.4 ms |

At the H750's 480MHz, expect roughly **~150ms** per inference; measure it
with `benchmark.c` and record the real number here: **[TBM]**.

Budget check for the H750: weights (308KB) → QSPI flash (8MB available);
activations (166KB) → AXI SRAM (512KB), leaving ample room for the ~38KB of
gate frame buffers in DTCM. Confirm all of this with **X-CUBE-AI → Analyze**
before generating code, and paste the Analyze report into
`benchmarks/benchmark_report.md`: **[TBM]**.

## Post-processing note

st_yololcv1 outputs raw YOLO-style tensors; `detection.c` must decode
anchors + apply NMS. ST publishes reference C post-processing for this
model family in
[stm32ai-modelzoo-services](https://github.com/STMicroelectronics/stm32ai-modelzoo-services)
(object detection application code); adapt that rather than writing the
decoder from scratch.

## Custom classes (optional path)

To detect classes beyond `person`, retrain st_yololcv1 at your chosen input
size with the zoo services' scripted pipeline (train → quantize INT8 →
evaluate → deploy). Smaller inputs (e.g. 96–160px) trade accuracy for
proportionally faster inference; if retraining anyway, benchmark 160×160
against 192×192; it may suit the 160×120 camera better.
