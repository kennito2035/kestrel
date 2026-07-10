# Kestrel Benchmark Report

**Status: methodology defined, measurements pending hardware runs.**
Every `[TBM]` below is replaced by a measured value (with the CSV committed
next to this file) before submission. No projected numbers appear in this
report.

## Instruments

| What | Instrument | Why it's the right tool |
|---|---|---|
| Per-stage latency (H750) | DWT cycle counter @ 480MHz (`benchmark.c`) | Cycle-exact, zero-overhead reads, on the device under test |
| Per-stage latency (RP2350) | `time_us_64()` over 100-rep loops | µs resolution amortized over reps |
| Skip rate | Gate log over ≥10 min of a real scene | Captures real sensor noise + real activity patterns |
| Current draw | USB inline power meter + bench multimeter | Whole-board truth, includes regulators and peripherals |

We do not use Arm Performix: it targets Arm64 Linux (Neoverse-class) systems
and cannot attach to bare-metal Cortex-M. Cycle counters and ammeters are
the correct instruments at this scale.

## Measurement conditions

- STM32H750 @ 480MHz, `-O2`, INT8 model via X-CUBE-AI (exact model + version
  recorded here when measured: [TBM])
- RP2350 @ 150MHz (default), `-O2`
- Scene for skip-rate runs: [TBM, describe scene, lighting, distance]
- Each timing = median of ≥100 samples; we report median and p95.

## Results

### H750 per-stage latency (`gate_results.csv`)

| Stage | Median | p95 |
|---|---|---|
| Grayscale convert (160×120) | [TBM] | [TBM] |
| Gate check, scalar | [TBM] | [TBM] |
| Gate check, SIMD (`__USADA8` path) | [TBM] | [TBM] |
| ROI crop + resize to 96×96 | [TBM] | [TBM] |
| Inference (INT8, CMSIS-NN) | [TBM] | [TBM] |
| **Gate-to-inference cost ratio** | **[TBM]** | - |

### Skip rate and average compute (`gate_results.csv`)

| Scene | Frames | Skip rate | Avg per-frame cost | vs always-on |
|---|---|---|---|---|
| [TBM] | [TBM] | [TBM] | [TBM] | [TBM]× |

### RP2350 resize (`interpolator_results.csv`)

| Case | SW µs | INTERP µs | Speedup | Bit-exact |
|---|---|---|---|---|
| full frame 160×120 | [TBM] | [TBM] | [TBM] | [TBM] |
| 75% ROI 120×120 | [TBM] | [TBM] | [TBM] | [TBM] |
| 50% ROI 80×80 | [TBM] | [TBM] | [TBM] | [TBM] |
| 25% ROI 48×48 | [TBM] | [TBM] | [TBM] | [TBM] |

### Power

| State | Current | Notes |
|---|---|---|
| Cascade idle (RP2350 + PIR armed, H750 in STOP) | [TBM] | |
| Gate-only duty (H750 awake, gate closed) | [TBM] | |
| Full inference | [TBM] | |
| Baseline: H750 always-on, no gating | [TBM] | |
| **Idle power reduction** | **[TBM]×** | headline |

## Threats to validity

- Skip rate is scene-dependent; we report the capture conditions and the
  raw log so others can judge transferability.
- USB power meters have ±1–2% class accuracy; adequate for the order-of-
  magnitude claims made here, cross-checked with a bench multimeter.
- DWT timings exclude DMA transfers that overlap compute by design (that
  overlap is itself one of the optimizations).
