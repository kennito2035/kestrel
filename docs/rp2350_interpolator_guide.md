# Using the RP2350 Hardware Interpolator for ML Image Preprocessing

The RP2350's two INTERP units (per core) are small fixed-function blocks
wired into the SIO, designed for texture mapping and audio DSP. This guide
documents using their **blend mode** for bilinear image resize, the
resize-to-model-input step of an embedded vision pipeline. We found no prior
published use of INTERP in an ML preprocessing pipeline (as of July 2026);
if you know of one, please open an issue so we can cite it.

## What blend mode actually does

One thing, quickly: a linear interpolation between two 32-bit **values**
with an 8-bit alpha, computed combinatorially so the result is available the
cycle after the inputs are written:

```
PEEK1 = BASE0 + (((BASE1 - BASE0) * (ACCUM1 & 0xFF)) >> 8)
```

Three things it does **not** do, which trip people up:

1. **It does not walk memory.** BASE0/BASE1 are pixel *values* you load,
   not pointers it dereferences. Your loop still does the address math and
   the loads.
2. **Blend mode exists on INTERP0 only.** INTERP1 has clamp mode instead
   (datasheet §3.1.10, same split as the RP2040). You cannot run two blends
   in parallel on one core.
3. **Alpha is 8-bit and cannot express 1.0** (max 255/256). Far-edge
   samples land up to 1 LSB below the exact source pixel. For ML inputs
   (which get quantized to INT8 anyway) this is irrelevant, but it is why
   our tests assert ≤3 LSB total error vs a float reference rather than
   exactness.

## Mapping bilinear resize onto INTERP

Bilinear needs three lerps per output pixel: top pixel pair, bottom pixel
pair (both with the X fraction), then those two results with the Y fraction.
Since only INTERP0 can blend, it does the two X-lerps back-to-back (the
result is combinatorial, valid immediately after the writes), and the final
Y-lerp stays in software:

```c
interp0->accum[1] = fx;                                  /* X fraction  */
interp0->base[0] = row0[sx];  interp0->base[1] = row0[sx1];
uint8_t top = interp0->peek[1];                          /* top lerp    */
interp0->base[0] = row1[sx];  interp0->base[1] = row1[sx1];
uint8_t bot = interp0->peek[1];                          /* bottom lerp */
dst[..] = blend8(top, bot, fy);   /* same equation, in software */
```

Because the software path ([`hw_interpolator_resize.c`](../rp2350/src/hw_interpolator_resize.c))
uses the identical `>>8` blend, **HW and SW outputs are byte-identical**;
the on-device benchmark asserts this with `memcmp` on every run.

## What to expect from the benchmark

Per output pixel the hardware saves two multiply-shift sequences but costs
six SIO register writes and two reads. SIO accesses are single-cycle, so
there is a real win, but it is **a small integer factor, not an order of
magnitude**; run `interp_resize_bench` and trust the printed numbers over
any intuition, ours included. Results: `benchmarks/interpolator_results.csv`
**[TBM, populated from device output before submission]**.

## Reproducing

```bash
cd rp2350
mkdir build && cd build
cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_BOARD=waveshare_rp2350_one
make interp_resize_bench
# BOOTSEL-flash interp_resize_bench.uf2, then open the USB serial port:
# prints case,sw_us,interp_us,speedup,bit_exact for four ROI sizes
```

Host-side correctness tests (no hardware): `rp2350/test/test_resize.c`.

## Where this is useful

In Kestrel itself the frame lives on the STM32H750, so this artifact is not
on Kestrel's critical path; we ship it as a standalone characterization.
It **is** on the critical path for projects that host the camera on the
RP2350 directly (PIO parallel-camera captures, person-sensor style boards),
where every core cycle spent on resize competes with the model itself.

Other preprocessing ops that fit the same blend-mode pattern: grayscale
conversion with weighted channels, brightness normalization, and alpha
compositing of a detection overlay onto a display buffer.
