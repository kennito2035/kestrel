# motion_gate: drop-in inference gating for Cortex-M camera pipelines

A self-contained motion gate + attention-ROI module: decide in well under a
millisecond whether a frame is worth running inference on, and if so, which
region of it deserves the model's full input resolution.

- **Pure C99**, no HAL, no malloc, no float, two files (`gate.c`, `gate.h`)
- **Resolution-independent**; configure width/height at runtime
- **Bit-exact SIMD fast path** for Cortex-M cores with the SIMD32/DSP
  extension (M4, M7, M33+DSP): `-DKESTREL_GATE_SIMD=1`
- **Host-testable**; `test/` runs on any PC, no hardware required

## Integration

```c
#include "gate.h"

gate_config_t cfg = {
    .width = 160, .height = 120,
    .pixel_threshold = 25,  /* per-pixel |diff| must exceed this          */
    .open_count = 96,       /* ~0.5% of a 160x120 frame opens the gate    */
    .roi_pad = 8,           /* context padding around the motion bbox     */
};

/* per frame, with 8-bit grayscale buffers: */
gate_roi_t roi;
uint32_t changed;
if (gate_check(&cfg, frame_curr, frame_prev, &roi, &changed) == GATE_OPEN) {
    crop_and_resize(frame_curr, &roi, model_input, 96, 96);
    run_inference(model_input);
} /* else: reuse the previous result, inference skipped */
swap(frame_curr, frame_prev);
```

The returned ROI is always **square** (aspect-correct resize into square
model inputs), padded by `roi_pad`, clamped inside the frame, side ≤
min(width, height). Map detection coordinates back to the frame with
`frame_x = roi.x + det_x * roi.w / model_w` (likewise for y).

### Tuning

- `pixel_threshold`: raise until sensor noise on a static scene yields
  `changed == 0`; typical CMOS sensors indoors: 20–30.
- `open_count`: the smallest object you care about, in pixels, at your
  resolution. 0.5–1% of the frame is a good start.
- Log `changed_count_out` over a few minutes of your real scene to pick both
  (Kestrel's `BENCHMARK_GATE_LOG` does exactly this).

### Cost model

Pass 1 (always runs) is one pass over the frame: ~2 ops/pixel scalar, ~0.5
ops/pixel with SIMD. Pass 2 (bbox refinement) runs only when the gate opens;
those frames pay for a full inference anyway, so its cost is negligible in
context.

## Tests

```bash
cd test
cc -Wall -Wextra -Werror -O2 -o test_gate test_gate.c ../gate.c && ./test_gate
python3 golden.py   # cross-language golden check (3rd independent impl)
```

`test_gate` runs 6 deterministic cases plus 500 seeded fuzz frames compared
against an independent single-pass reference implementation; it then dumps
`results.csv`, which `golden.py` re-derives in pure Python and diffs.

The SIMD path is bit-exact with the scalar path by construction and is
verified on target hardware (see `stm32h750/Core/Src/benchmark.c`), since
SIMD32 intrinsics cannot execute on a PC host.
