# Integrating the Motion Gate into an STM32 Vision Pipeline

The gate module itself is documented in
[`modules/motion_gate/README.md`](../modules/motion_gate/README.md) (API,
tuning, tests). This guide covers the Kestrel-specific integration on the
STM32H750: buffers, the main loop, STOP mode, and measurement.

## Buffers

Two 8-bit grayscale frames at gate resolution (160×120 → 19,200 bytes each,
~38KB total). Convert from the DCMI's RGB565 during the copy, extracting
the green channel's 6 bits (`(px >> 5) & 0x3F`, scaled) is a fine luma proxy
and costs one shift per pixel; full ITU-R 601 luma is not required for
difference detection.

Place both gate buffers in DTCM (fastest, and the gate is the code that runs
on every frame); model activations live in AXI SRAM, weights in QSPI.

## Main loop shape

```c
for (;;) {
    wait_for_frame();                       /* DCMI+DMA double buffer   */
    grayscale_convert(frame_rgb565, gray_curr);

    uint32_t t0 = bench_cycles();
    gate_state_t s = gate_check(&cfg, gray_curr, gray_prev, &roi, &changed);
    bench_log("gate", bench_cycles() - t0);

    if (s == GATE_OPEN) {
        crop_resize_to_input(frame_rgb565, &roi, ai_input);   /* H750-side */
        run_inference_and_draw();           /* X-CUBE-AI + NMS + TFT     */
        uart_send_detections();             /* -> RP2350 output ctrl     */
    }
    swap(gray_curr, gray_prev);

    if (idle_long_enough())
        enter_stop_mode();                  /* wake: PC0 EXTI <- RP2350  */
}
```

Detection coordinates come back in ROI space; map to frame space with
`frame_x = roi.x + det_x * roi.w / MODEL_W` before drawing or reporting.

**Demo switch:** wire the board's K1 button (PC13, pulled down) to toggle a
`gating_enabled` flag that bypasses the gate check. The HUD then shows
average latency with and without gating on the same live scene, the
single most persuasive 10 seconds available for the demo video.

## STOP mode notes (H750)

- Wake source is EXTI on PC0, driven by the RP2350 on PIR motion
  (see wiring in the top-level README; the RP2350 wakes the H750,
  not the other way around).
- After STOP, the clock tree restarts on HSI; re-run the PLL/clock config
  before touching DCMI, and re-arm the DMA double buffer.
- Keep the previous grayscale frame across STOP (it's in DTCM, which is
  retained) so the first post-wake gate check is meaningful.

## Measuring

- `BENCHMARK_ENABLE 1`, per-stage CSV over UART (gate / grayscale /
  crop-resize / inference / display), DWT cycle counts.
- `BENCHMARK_GATE_LOG 100`, gate state + changed-pixel count every 100
  frames; run it on your real scene for ten minutes and you have your skip
  rate and your threshold tuning data in one capture.
