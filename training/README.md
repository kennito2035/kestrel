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

Measured on Kestrel's H750 at 480 MHz executing from QSPI flash:
**178-181 ms, deterministic** (multi-hour window; see
`benchmarks/benchmark_report.md`). The QSPI execute-in-place overhead
offsets the clock advantage over ST's 179.3 ms internal-flash figure at
400 MHz.

Budget, confirmed on target: weights stay in QSPI flash via X-CUBE-AI's
default NULL binding; the generated activation arena is 151.5 KiB in AXI
SRAM (512 KB available); the ~38 KB of gate frame buffers live in DTCM.
The deployed footprint is inspectable in the committed
`network.c`/`network_data_params.c` under `stm32h750/firmware/`.

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
