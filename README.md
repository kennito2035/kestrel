# Kestrel
### Motion-Gated Object Detection on Arm Cortex-M: spend compute only when, and where, something moves

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Arm%20Cortex--M7%20%2B%20M33-blue.svg)]()
[![Track](https://img.shields.io/badge/Track-Physical%20AI-green.svg)]()
[![Hackathon](https://img.shields.io/badge/Arm%20Create-AI%20Optimization%20Challenge%202026-orange.svg)]()

> **A kestrel hovers motionless, watching, spending nothing, then strikes
> only when something moves, only where it moved.**
> Event-driven object detection on Arm Cortex-M. Smarter, not just faster.

---

## Overview

Most embedded vision systems run full neural network inference on every frame, every time,
regardless of whether anything in the scene has changed. On a monitoring scene that is static
95% of the time, that means ~95% of the inference compute, and the power it burns, is wasted
re-detecting a scene that hasn't moved.

Kestrel eliminates that waste with a three-level attention cascade, each level orders of
magnitude cheaper than the one it gates:

| Level | Runs on | Cost | Question it answers |
|---|---|---|---|
| 1. PIR sensor | RP2350 (Cortex-M33), H750 asleep | ~2mA system idle | "Is anything happening at all?" |
| 2. Frame-difference gate | STM32H750 (Cortex-M7) | sub-millisecond | "Did the scene actually change?" |
| 3. INT8 detector (CMSIS-NN) | STM32H750 (Cortex-M7) | tens of ms | "What is it, and where?" |

When the gate opens, Kestrel doesn't just run the detector; it **crops the model's input to
the region that moved**. Because the model input resolution is fixed, this costs nothing extra,
and it acts as a digital zoom: a distant person who would occupy 8 pixels in a full-frame
downscale occupies 40+ pixels in a motion-centered crop, dramatically improving small-object
detection at identical inference cost.

### What makes it interesting

The standard approach to embedded AI optimization focuses on the model: quantize it, prune it,
shrink it. Those techniques are necessary; Kestrel applies them, but they optimize the cost
of *one inference*. Kestrel optimizes the *system*: it asks "should we even run inference right
now, and on which pixels?" On scenes with intermittent motion, average compute scales with the
**activity of the scene** rather than the frame rate, a reduction no amount of model
compression can reach on its own, with zero accuracy loss, because active frames still get
full inference.

### Why it should win

- **Motion-gated inference with ROI attention** has been described in research since 2017
  (Fast YOLO, arXiv:1709.05943; AmphibianDetector, arXiv:2011.07513), but we found no public
  bare-metal Cortex-M implementation (as of July 2026); it is absent from ST's FP-AI-VISION1
  function pack, X-CUBE-AI documentation, and published STM32 vision tutorials. Kestrel ships
  it as [`modules/motion_gate/`](modules/motion_gate/), a dependency-free, drop-in C module
  for any Arm Cortex-M camera pipeline, with host-runnable tests.
- **The RP2350 hardware interpolator characterized for ML preprocessing**: the INTERP unit is
  documented for graphics use (RP2350 datasheet §3.1.10), but we found no published use in an
  ML preprocessing pipeline. Kestrel contributes the first benchmark of hardware-assisted
  bilinear resize for model input preparation on this chip, as a standalone reusable artifact
  for the growing class of RP2350-hosted camera projects.
- **Honest, reproducible measurement.** Every number in this README is (or will be, before
  submission) a DWT cycle-counter or ammeter measurement, logged to CSV in
  [`benchmarks/`](benchmarks/), with the exact harness code included. No projected numbers.

---

## Measurement Methodology: A Note on Honesty

Numbers currently marked **[TBM]** (*to be measured*) are pending hardware measurement and will
be replaced with real data before submission. Where we state expectations, they are anchored to
published references: ST's model zoo reports st_yolo_x nano at 192×192 taking ~335ms on an
STM32H747 @ 400MHz, so we expect a 96×96-class INT8 detector on the H750 @ 480MHz to land in
the **60–150ms** range, meaning the sub-millisecond gate check is **~100–400× cheaper than one
inference**, and every gated frame recovers that cost.

**Instruments:** Cortex-M7 DWT cycle counter (`benchmark.c`) for all timing; USB inline power
meter + bench multimeter for current. We do not use Arm Performix here: Performix targets
Arm64 Linux systems (Neoverse/cloud) and cannot attach to bare-metal Cortex-M. For this class
of device, cycle counters and ammeters are the correct instruments.

---

## Hardware

| Component | Spec | Role |
|---|---|---|
| WeAct Studio STM32H750VBT6 | Cortex-M7 @ 480MHz, 1MB SRAM, 8MB QSPI flash | Main inference engine |
| OV2640 camera (onboard) | DCMI interface | Live image capture |
| 0.86" TFT display (onboard) | SPI | Bounding box + gate status display |
| Waveshare RP2350-ONE | Dual Cortex-M33 @ 150MHz, 520KB SRAM | Always-on pre-screen + output controller |
| PIR sensor HC-SR501 | 3–7m range | Level-1 motion pre-screen |
| Servo motor SG90 | 180° | Physical response to detection |

**Wiring (H750 ↔ RP2350-ONE):**

```
STM32H750  PA9  (UART1 TX)  ──────▶  RP2350 GP1  (UART0 RX)
STM32H750  PA10 (UART1 RX)  ◀──────  RP2350 GP0  (UART0 TX)
STM32H750  PC0  (EXTI wake) ◀──────  RP2350 GP15 (GPIO OUT)
STM32H750  GND              ─────┬─  RP2350 GND
                                 └─  PIR GND
PIR        OUT              ──────▶  RP2350 GP16 (EXTI)
RP2350     GP18             ──────▶  Servo signal
```

---

## System Architecture

```
╔══════════════════════════════════════════════════════════════════════╗
║  RP2350-ONE  (Always-on, Cortex-M33 @ 150MHz)                        ║
║                                                                      ║
║  Core 0:  PIR sensor ──▶ EXTI ──▶ GPIO HIGH ──▶ Wake H750           ║
║           System idle current: ~2mA [TBM]  (H750 active: ~180mA [TBM])
║                                                                      ║
║  Core 1:  UART RX ──▶ parse detection event ──▶ drive servo/LED     ║
╚══════════════════════════╦═══════════════════════════════════════════╝
                           ║ GPIO wake pulse
╔══════════════════════════▼═══════════════════════════════════════════╗
║  STM32H750  (Cortex-M7 @ 480MHz, 1MB SRAM)                           ║
║                                                                      ║
║  OV2640 ──DCMI+DMA──▶ Frame Buffer (160×120 RGB565)                  ║
║                └──▶ grayscale convert ──▶ 160×120 ×8bit (~19KB)      ║
║                                                                      ║
║  ┌──────────────────────────────────────────────────────────────┐   ║
║  │  GATE STAGE  (sub-ms [TBM])                                  │   ║
║  │                                                              │   ║
║  │  Abs diff: current grayscale frame vs previous               │   ║
║  │  Changed-pixel count ──▶ threshold check                     │   ║
║  │                                                              │   ║
║  │  No significant change? ──▶ keep last result, back to        │   ║
║  │                             STOP mode. Inference skipped.    │   ║
║  │                                                              │   ║
║  │  Change detected? ──▶ bounding box of changed region         │   ║
║  │                       (the "attention ROI")                  │   ║
║  └────────────────────────────┬─────────────────────────────────┘   ║
║                               │ attention ROI                        ║
║  ┌────────────────────────────▼─────────────────────────────────┐   ║
║  │  INFERENCE STAGE  (fixed cost per invocation)                │   ║
║  │                                                              │   ║
║  │  Crop ROI (padded to square) ──▶ resize to 96×96 input       │   ║
║  │    → same inference cost as full frame, but the moving       │   ║
║  │      object fills the model's field of view (digital zoom)   │   ║
║  │  INT8 detector, CMSIS-NN M7 kernels                          │   ║
║  │  Weights: QSPI flash (XIP)   Activations: AXI SRAM           │   ║
║  │  Per-inference latency: [TBM] (expected 60–150ms)            │   ║
║  └────────────────────────────┬─────────────────────────────────┘   ║
║                               │                                      ║
║  NMS ──▶ coords mapped ROI→frame ──▶ TFT (boxes + gate HUD)          ║
║      └──▶ UART TX ──▶ RP2350 Core 1 (detection event)                ║
║                                                                      ║
║  Saves grayscale frame for next diff, returns to STOP mode           ║
╚══════════════════════════════════════════════════════════════════════╝
```

**Where the savings come from (and where they don't):** the model input is a fixed 96×96
tensor, so a single inference always costs the same; cropping does *not* make inference
faster. The compute savings come entirely from the **gate skipping inference on unchanged
frames** and the **PIR cascade letting the H750 sleep**. The ROI crop is an *accuracy*
optimization at equal cost. (A planned extension, a "resolution ladder" of 96/64/48 model
variants dispatched by ROI size, would make inference cost genuinely scale with motion area;
see [Roadmap](#roadmap).)

---

## Novel Contributions

### 1. Motion Gate + Attention ROI (`modules/motion_gate/`)

A self-contained, dependency-free C module (no HAL, no malloc, no float) that runs before
every inference call:

- **Gate:** absolute-difference count between consecutive grayscale frames against a
  configurable threshold. Scene unchanged → inference skipped entirely, MCU returns to STOP
  mode having spent only the sub-millisecond gate check.
- **Attention ROI:** when the gate opens, the module returns the bounding box of the changed
  region, padded to square and clamped to frame bounds, ready to crop-and-resize into the
  model's input tensor. The detector sees the moving object at maximum resolution instead of
  a full-frame downscale.
- **Deployable anywhere:** pure C99, resolution-independent, ~150 lines, host-testable
  (`modules/motion_gate/test/` runs on a PC), with an optional Cortex-M7 SIMD path
  (`__USADA8`) behind a compile flag.

The underlying idea (motion-adaptive inference) appears in research from 2017 onward
(Shafiee et al., Fast YOLO; AmphibianDetector). What we could not find anywhere, ST function
packs, X-CUBE-AI docs, public repos (searched July 2026), is a reusable bare-metal Cortex-M
implementation. This module is our attempt to be that reference.

### 2. RP2350 Hardware Interpolator for ML Preprocessing (`rp2350/`)

The RP2350's INTERP units can blend adjacent pixels in hardware (datasheet §3.1.10). They are
publicly used for graphics effects (rotozoomers, scalers); we found no published use in an ML
preprocessing pipeline (as of July 2026).

Kestrel contributes a **correct bilinear image resize** built on INTERP blend mode, hardware
lerp along X for the two source rows, one software lerp along Y, plus an equivalent pure-
software implementation and a cycle-accurate benchmark harness comparing them
([`rp2350/src/hw_interpolator_resize.c`](rp2350/src/hw_interpolator_resize.c)).

| Method | ROI → 96×96 resize time | Speedup |
|---|---|---|
| Software bilinear (M33, fixed-point) | [TBM] | 1× |
| INTERP-assisted bilinear | [TBM] | expected 2–5× |

**Honest scope note:** in Kestrel's own pipeline the frame lives on the H750, which does its
own crop-resize, so this artifact is *not* on Kestrel's critical path. We ship it as a
standalone, reusable characterization for the many projects that host a camera directly on the
RP2350 (PIO parallel cameras, person-sensor boards), where model-input resize *is* on the
critical path. The benchmark, guide, and module stand alone; see
[`docs/rp2350_interpolator_guide.md`](docs/rp2350_interpolator_guide.md).

---

## Standard Optimizations Applied

Established techniques, included as correct practice (we do not claim them as novel):

- **INT8 post-training quantization** via the ST Model Zoo / X-CUBE-AI pipeline. Model size,
  RAM, and accuracy deltas will be reported from X-CUBE-AI Analyze output for the exact
  deployed model: **[TBM]**.
- **CMSIS-NN Cortex-M7 kernels**, selected by X-CUBE-AI; INT8 conv/depthwise ops use M7
  SIMD/DSP instructions.
- **Double-buffered DMA capture**: DCMI+DMA fills frame N+1 while the M7 processes frame N,
  removing capture latency from the critical path.
- **STOP-mode power management**: the H750 sleeps between PIR events; wake source is a GPIO
  EXTI line driven by the RP2350.

**Model choice:** the primary path uses a pretrained INT8 person-detection model from the
[ST Model Zoo](https://github.com/STMicroelectronics/stm32ai-modelzoo) (reproducible, known
memory footprint). [`training/`](training/) documents an optional retrain path for custom
classes.

---

## System Benchmark Plan

All values below are produced by the in-repo harnesses and logged to `benchmarks/*.csv`.
**[TBM]** markers are replaced with measured data before submission.

| Measurement | Instrument | Value |
|---|---|---|
| Gate check, per frame | DWT cycle counter | [TBM] (expected < 1ms) |
| Grayscale convert, per frame | DWT | [TBM] |
| Crop + resize to 96×96 | DWT | [TBM] |
| Full inference, per invocation | DWT | [TBM] (expected 60–150ms) |
| Gate-to-inference cost ratio | derived | [TBM] (expected 100–400×) |
| Skip rate, typical indoor scene, 10 min | gate log | [TBM] |
| **Average per-frame compute reduction** | derived | **[TBM]** |
| System idle current (PIR cascade armed) | ammeter | [TBM] (expected ~2mA) |
| H750-always-on baseline current | ammeter | [TBM] (expected ~130mA+) |
| **Idle power reduction** | derived | **[TBM]** |
| Small-object detection: full-frame vs ROI crop | eval script | [TBM] |

The headline claim is the pair in bold: *average compute scales with scene activity, and idle
power scales with the cheapest watcher in the cascade.*

---

## Project Structure

```
kestrel/
├── modules/
│   └── motion_gate/                # ← Reusable, dependency-free gate module
│       ├── gate.c / gate.h         #    pure C99, no HAL, host-testable
│       ├── README.md               #    integration guide
│       └── test/
│           ├── test_gate.c         #    host-runnable unit tests
│           └── golden.py           #    Python golden-reference generator
│
├── stm32h750/
│   ├── Core/
│   │   ├── Src/
│   │   │   ├── benchmark.c         # DWT cycle counter harness
│   │   │   ├── main.c              # (bring-up) main loop + STOP mode
│   │   │   ├── camera.c            # (bring-up) OV2640 DCMI + DMA driver
│   │   │   ├── display.c           # (bring-up) TFT SPI + bbox + gate HUD
│   │   │   ├── preprocess.c        # (bring-up) grayscale + ROI crop/resize
│   │   │   └── detection.c         # (bring-up) NMS + ROI→frame mapping
│   │   └── Inc/ (headers)
│   ├── X-CUBE-AI/App/              # Generated by X-CUBE-AI
│   └── STM32H750_Kestrel.ioc       # CubeIDE project config
│
├── rp2350/
│   ├── src/
│   │   ├── main.c                  # Dual-core entry point
│   │   ├── pir_trigger.c           # Core 0: PIR EXTI + H750 wake signal
│   │   ├── output_controller.c     # Core 1: UART RX + servo
│   │   ├── hw_interpolator_resize.c# ← INTERP bilinear + bit-exact SW baseline
│   │   └── interp_resize_bench.c   # On-device HW-vs-SW benchmark
│   ├── test/                       # Host-runnable resize tests
│   └── CMakeLists.txt
│
├── training/                       # Model selection + optional retrain path
├── benchmarks/                     # CSV results + benchmark_report.md (methodology)
├── docs/
│   ├── gate_module_guide.md        # STM32 pipeline integration + STOP mode
│   └── rp2350_interpolator_guide.md
├── .github/workflows/host-tests.yml# CI: host tests + Cortex-M cross-compile
├── LICENSE                         # MIT
└── README.md
```

Files marked *(bring-up)* are created during hardware bring-up (they depend
on CubeIDE code generation for this specific board); everything in
`modules/`, `rp2350/`, and the harnesses is written and host-tested in CI.

---

## Setup Instructions

### Prerequisites

- STM32CubeIDE 1.15+ with X-CUBE-AI 9.0+ (Embedded Software Packages)
- Raspberry Pi Pico SDK 2.x (for the RP2350)
- Python 3.10+ (host tests and tooling)
- Any C compiler on the host (only needed to run the gate module's unit tests)

```bash
git clone https://github.com/YOUR_USERNAME/kestrel.git
cd kestrel
```

### Step 1: Get the model

Download the pretrained INT8 person-detection model from the ST Model Zoo (see
`training/README.md` for the exact model and link), or retrain via the notebooks in
`training/` for custom classes.

### Step 2: Import model into X-CUBE-AI

1. Open `stm32h750/STM32H750_Kestrel.ioc` in STM32CubeIDE
2. **Middleware → X-CUBE-AI → Add network** → import the `.tflite` (compression: None)
3. **Analyze**; confirm activations fit SRAM and weights fit QSPI; record the report
   (it feeds the benchmark tables)
4. **Generate Code**

### Step 3: Build and flash the STM32H750

Build All (Ctrl+B), then Run → Debug (F11). Weights are placed in memory-mapped QSPI flash
by the linker script; verify the map file shows activations in AXI SRAM with room for the
two 19KB grayscale gate buffers.

### Step 4: Build and flash the RP2350-ONE

```bash
cd rp2350
mkdir build && cd build
cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_BOARD=waveshare_rp2350_one
make -j4
```

Hold **BOOTSEL**, connect USB, copy `kestrel_rp2350.uf2` to the `RPI-RP2` drive.

### Step 5: Validate

1. Power both boards
2. Static scene → TFT shows **GATE: CLOSED**, skip counter rising, H750 duty-cycling into STOP
3. Move an object into frame → **GATE: OPEN**, bounding box drawn, latency HUD updates
4. Remove the object → gate closes, system re-arms
5. On `person` detection → servo on the RP2350 actuates

### Step 6: Run the host-side gate tests (no hardware needed)

```bash
cd modules/motion_gate/test
cc -Wall -Wextra -O2 -o test_gate test_gate.c ../gate.c && ./test_gate
python golden.py   # regenerates golden vectors and cross-checks the C results
```

---

## Reproducing Benchmarks

- **Gate timing / inference timing (H750):** `benchmark.c` wraps each stage with DWT cycle
  counts and prints per-stage ms over UART (115200). Enable with `BENCHMARK_ENABLE 1`.
- **Skip rate:** set `BENCHMARK_GATE_LOG 1`; logs `CLOSED`/`OPEN` + ROI area % every 100
  frames; leave running on your scene for 10 minutes and feed the CSV to
  `benchmarks/summarize.py`.
- **Interpolator vs software resize (RP2350):** build with `-DUSE_HW_INTERPOLATOR=1` and
  `=0`; each prints cycle counts over UART.
- **Power:** USB inline meter on each board's supply; states: cascade idle, gate-only, full
  inference. Methodology details in `benchmarks/benchmark_report.md`.

---

## Reusable Artifacts

1. **`modules/motion_gate/`**, drop-in motion gate + attention-ROI module for any Cortex-M
   camera pipeline. Pure C99, no dependencies, host-tested, SIMD-optional.
2. **`rp2350/src/hw_interpolator_resize.c` + `docs/rp2350_interpolator_guide.md`**, first
   published characterization (to our knowledge, July 2026) of the RP2350 INTERP unit for ML
   input preprocessing, with software baseline and benchmark harness.
3. **`benchmarks/benchmark_report.md`**, a worked example of honest embedded-ML measurement
   methodology (DWT + ammeter) that other Cortex-M projects can copy.

---

## Roadmap

- **Resolution ladder:** export the detector at 96/64/48-px input sizes (X-CUBE-AI supports
  multiple networks) and dispatch by ROI size, making inference cost genuinely proportional
  to motion area. The dispatch pattern itself becomes a reusable artifact.
- **Early-exit head:** a secondary classification head after the backbone for high-confidence
  scenes. Established in research; no bare-metal Cortex-M implementation found.
- **OV2640 hardware windowing:** push the ROI crop into the sensor itself via SCCB, saving
  the DCMI bandwidth as well.

---

## License

MIT; see [LICENSE](LICENSE).

## Acknowledgements

- [ST Model Zoo](https://github.com/STMicroelectronics/stm32ai-modelzoo), pretrained, quantized STM32 vision models
- [X-CUBE-AI](https://www.st.com/en/embedded-software/x-cube-ai.html), STM32 neural network toolkit
- [CMSIS-NN](https://github.com/ARM-software/CMSIS-NN), Arm Cortex-M optimized NN kernels
- [Fast YOLO (Shafiee et al., 2017)](https://arxiv.org/abs/1709.05943), [AmphibianDetector](https://arxiv.org/abs/2011.07513), motion-adaptive inference concept
- [WeAct Studio](https://github.com/WeActStudio), STM32H750 board + OV2640 examples
- [Waveshare RP2350-ONE](https://www.waveshare.com/wiki/RP2350-One)
