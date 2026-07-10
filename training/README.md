# Model

Kestrel's primary model path is a **pretrained INT8 object-detection model
from the [ST Model Zoo](https://github.com/STMicroelectronics/stm32ai-modelzoo)**
(object_detection section) rather than a hand-trained network: the zoo
models come with published footprints and reproducible training scripts,
which removes the riskiest unknowns from an embedded deployment.

Selection criteria for the H750 (1MB SRAM, weights in 8MB QSPI):

1. Input ≤ 96×96 (or retrain at that size), keeps activations well under
   the AXI SRAM budget alongside the ~38KB of gate buffers.
2. INT8 post-training quantized `.tflite` available.
3. Run **X-CUBE-AI → Analyze** before committing: confirm activation RAM,
   weight flash, and per-inference MACC; record the report in
   `benchmarks/benchmark_report.md`.

The exact model, version, and Analyze output used for the submission are
recorded here once measured on hardware: **[TBM]**.

## Custom classes (optional path)

To detect classes beyond the zoo's pretrained sets, use the zoo's own
training services (`stm32ai-modelzoo-services`) with a COCO subset; the
pipeline (train → PTQ INT8 → validate → deploy) is scripted there and far
better tested than a bespoke Colab notebook.
