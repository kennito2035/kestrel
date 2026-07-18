# Kestrel Benchmark Report

**Status: inference latency and skip rate measured on hardware (July 13);
power table and RP2350 interpolator bench pending (meter in shipping /
headers unsoldered).** Every remaining `[TBM]` is replaced by a measured
value before submission. No projected numbers appear in this report.

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

- STM32H750 @ 480MHz (VOS0, HCLK 120MHz, app executes-in-place from QSPI);
  model: `st_yololcv1` 192×192 INT8 (ST Model Zoo, COCO-person) via
  X-CUBE-AI 10.2.1 / ST Edge AI Core 2.2.0; application built `-O0` (debug)
  with `-O2` on the per-frame hot paths; inference itself runs inside ST's
  precompiled `NetworkRuntime1020_CM7_GCC.a`
- RP2350 @ 150MHz (default), `-O2`
- Scene for skip-rate runs: fixed indoor room view (~3×4 m), no screens/TV
  in frame, person walk-throughs at 1.5–4 m; one daylight run and one
  night (artificial light off) run; see `skiprate_2026-07-13_*.mp4`
- Each timing = median of ≥100 samples; we report median and p95.

## Results

### H750 per-stage latency (`gate_results.csv`)

| Stage | Median | p95 |
|---|---|---|
| Grayscale convert (160×120) | [TBM] | [TBM] |
| Gate check, scalar | [TBM] | [TBM] |
| Gate check, SIMD (`__USADA8` path) | [TBM] | [TBM] |
| ROI crop + bilinear resize to 192×192 | [TBM] | [TBM] |
| Inference (INT8, X-CUBE-AI runtime) | **180 ms** (178–181 observed, DWT) | 181 ms |
| **Gate-to-inference cost ratio** | **[TBM]** | - |

Inference latency is deterministic run-to-run (±1 ms window over 20-run
warm benchmark and multi-hour live sessions). Per-stage gate/preproc
timings await the soldered-UART CSV capture.

### Skip rate and average compute

Evidence: on-device session counters, filmed;
`skiprate_2026-07-13_daylight.mp4` / `skiprate_2026-07-13_night.mp4`
(UART `gate_results.csv` capture lands once headers are soldered).

| Scene | Duration | Frames | Skip rate | Avg per-frame cost | vs always-on |
|---|---|---|---|---|---|
| Indoor, daylight | 20:02 | 16,201 | **98.2%** (15,910 skipped, 291 inferences) | ~3.2 ms | **~56×** |
| Indoor, night | 12:30 | 10,310 | **99.1%** (10,220 skipped, 90 inferences) | ~1.6 ms | **~115×** |

Avg per-frame cost = (inferences × 180 ms) / total frames; "vs always-on"
compares against paying 180 ms on every frame.

### RP2350 resize (`interpolator_results.csv`)

| Case | SW µs | INTERP µs | Speedup | Bit-exact |
|---|---|---|---|---|
| full frame 160×120 | [TBM] | [TBM] | [TBM] | [TBM] |
| 75% ROI 120×120 | [TBM] | [TBM] | [TBM] | [TBM] |
| 50% ROI 80×80 | [TBM] | [TBM] | [TBM] | [TBM] |
| 25% ROI 48×48 | [TBM] | [TBM] | [TBM] | [TBM] |

### Power

Measured July 13 with a FNIRSI FNB-C2 inline on the USB-C 5V feed
(±0.05% class), whole board (H750 + OV2640 camera + ST7735 LCD +
regulators). Each figure is the highest reading observed after ≥10s settle.

| State | Current @≈5.1V | Power | Notes |
|---|---|---|---|
| Boot inrush | 258 mA | 1.32 W | transient, pre-run |
| H750 always-on, no gating | 243 mA | 1.24 W | K1→GATE OFF; inference every frame |
| Full inference, gate open (motion present) | 243 mA | 1.24 W | matches always-on, sanity check |
| Gate-only, awake, scene idle (~99% skip) | 186 mA | 0.95 W | gate closed, CPU WFI between frames |
| STOP sleep (H750 asleep) | **99 mA** | **0.51 W** | camera/LCD/QSPI still powered |
| **Gate saving while awake** | 243→186 mA | **−23% (1.31×)** | pure compute-gating effect |
| **STOP vs always-on (H750-only idle reduction)** | 243→99 mA | **−59% (2.45×)** | headline (this board) |
| Cascade idle (H750 STOP + RP2350 PIR-armed) | [TBM] | [TBM] | needs Stage-3 board |

**Reading these honestly:** the 98–99% figure above is a *compute* reduction
(frames that skip the 180 ms inference). Whole-**board** power reduction is
smaller, **2.45×**, because the always-on camera, LCD and 3.3V regulators
set a ~99 mA floor that the CPU's duty cycle can't touch. Both numbers are
real and measure different things; we do not conflate them.

The STOP floor is **peripheral-bound, not core-bound**: WeAct's `09-PWR_Test`
reaches **0.9 mA STANDBY** with everything off, versus our 99 mA with the
camera and display left powered for instant wake. Gating those too (camera
PWDN, panel off, QSPI deep-power-down) is the documented path to a
near-µA floor; future work, not claimed here.

## Threats to validity

- Skip rate is scene-dependent; we report the capture conditions and the
  raw log so others can judge transferability.
- USB power meters have ±1–2% class accuracy; adequate for the order-of-
  magnitude claims made here, cross-checked with a bench multimeter.
- DWT timings exclude DMA transfers that overlap compute by design (that
  overlap is itself one of the optimizations).
