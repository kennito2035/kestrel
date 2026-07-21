# Troubleshooting & Gotchas

Every issue below was actually encountered during Kestrel's development.
If you're evaluating or reproducing this project, read this first; it will
save you hours. Ordered by when you'd hit them.

## Building the H750 firmware

- **X-CUBE-AI paths are absolute.** The `.cproject` references the ST pack at
  `C:/Users/<user>/STM32Cube/Repository/Packs/STMicroelectronics/X-CUBE-AI/10.2.1/`
  (one include path + one library path). Install X-CUBE-AI 10.2.1 via CubeMX's
  pack manager, or fix those two paths in Project Properties → C/C++ Build →
  Settings.
- **The `network*.c/h` files are pre-generated** with `stedgeai generate`
  (ST Edge AI Core 2.2.0) and committed. Regenerating them from CubeMX's GUI
  does NOT work as expected: the CubeMX "ApplicationTemplate" emits only empty
  `MX_X_CUBE_AI_Init/Process` stubs and silently skips the network files when
  its (internal-flash-only) size check fails. Use `stedgeai.exe generate
  --target stm32h7 --name network -m <model.tflite>` directly if needed.
- **The AI runtime requires the CRC peripheral clock** (`__HAL_RCC_CRC_CLK_ENABLE()`,
  done in `ai_infer.c`). Without it, `ai_network_create_and_init` fails.

## CubeMX code regeneration (the recurring trap)

Any **GENERATE CODE** in CubeMX silently reverts two lines in
`Src/main.c:SystemClock_Config()`; re-apply both every time:

1. Re-add `__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);`
   (before the VOSRDY wait). Without VOS0 the chip cannot sustain 480 MHz.
2. Change `FLASH_LATENCY_0` back to `FLASH_LATENCY_1`.

Symptom if forgotten: dead board, no LED, no display (crashes inside clock
config; the Error_Handler blinks the blue LED at ~4 Hz if reached).

Worse: **enabling a software pack can silently reset the RCC section of the
`.ioc` to ST board defaults** (8 MHz HSE-BYPASS; this board has a 25 MHz
crystal). This happened once and bricked the app until fixed. After any
CubeMX dialog, verify: HSE = Crystal/Ceramic Resonator, HSE input = 25 MHz,
CPU = 480 MHz, `HSE_VALUE` in `stm32h7xx_hal_conf.h` = 25000000.

## Flashing / boot chain

- Internal flash holds the WeAct `11-ExtMem_Boot_USB` bootloader; the app
  lives in QSPI at `0x90000000`. Flash the app over USB HID: **hold K1 →
  plug USB → release K1 at the slow LED blink** → run
  `WeAct_HID_Flash_NETFW.exe` (WeAct MiniSTM32H7xx SDK) → select the hex.
- **LED language of the bootloader:** slow blink while K1 held = release now
  for HID mode; keep holding → fast blink = DFU mode; **breathing/fading LED
  = bootloader idle, meaning no valid app** (or the app was rejected).
- **The bootloader validates the app's initial stack pointer** with
  `(SP & 0x2FF80000) == 0x24000000`. A textbook `_estack` at the exact
  end of RAM (0x24080000) is REJECTED; the linker script deliberately sets
  `_estack = ORIGIN + LENGTH - 8`. Don't "fix" that back.
- After CubeProgrammer DFU programming, the chip **stays in DFU**; the app
  only runs after a clean unplug/replug without buttons.
- **DFU cannot read RAM.** The H750 ROM bootloader refuses memory reads of
  SRAM (both 0x30000000 and 0x20000000 return "Data read failed"); only
  flash/option bytes are readable. The in-RAM log (`.ram_log` in DTCM) is
  therefore only extractable over UART once headers are soldered.

## Runtime / performance invariants

- **MPU: the AXI-SRAM region must stay `MPU_ACCESS_NOT_SHAREABLE`.** On a
  single-core M7, marking normal memory Shareable disables the D-cache for
  it; this exact bug (inherited from vendor example code) made inference
  ~5× slower (1462 ms vs 306 ms) with no other symptom.
- **Never raise HCLK above 120 MHz.** The QSPI execute-in-place clock rides
  D1 HCLK; doubling it exceeds the W25Q64's rating and the CPU hard-faults
  on instruction fetch instantly (solid LED, dead screen). CPU runs at
  480 MHz via PLLP=1 with AHB /4; that is the intended configuration.
- **D-cache vs DCMI DMA:** the camera DMA writes `pic[]` while the CPU reads
  it; `SCB_InvalidateDCache_by_Addr` runs before each read. Removing it
  brings back horizontal-tear artifacts.
- **D2 SRAM (0x30000000) needs its RCC clock enabled before use**;
  peripheral SRAMs are unclocked at reset on the H7.
- `stop_mode.c` defines `EXTI0_IRQHandler`/`EXTI15_10_IRQHandler`. If you
  later enable any EXTI0/10..15 line in CubeMX, you'll get duplicate-symbol
  errors; merge the handlers.
- **Do not "optimize" weights into RAM** by copying from
  `ai_network_data_weights_get()`; it returns a descriptor table, not the
  blob. Binding a copy silently runs the net on garbage (all-zero outputs,
  plausible timing). Weights belong in cached QSPI via the default NULL
  binding.

## Camera & preprocessing

- Pixel format flags (in `Inc/img_preproc.h`), validated on hardware with a
  red/blue phone-screen test viewed through the model-input debug view:
  `PREPROC_SWAP_BYTES=1`, `PREPROC_BGR565=0`. The LCD being initialized as
  "RBG565" is a red herring; the DCMI byte stream is standard RGB565 with
  swapped bytes per halfword.
- `PREPROC_ROTATE` (0/90/270) rotates the **model input** for sideways board
  mounting; the display stays unrotated. With `270`, rotating the board 90°
  clockwise (viewed from behind) yields an upright model view.

## Detection behavior (expectations, not bugs)

- The model is a **COCO-person detector, not a face detector**. Full bodies
  at 1.5–4 m are its design regime. A face filling the lens correctly
  produces a near-full-frame box.
- **A visible hand/arm scores 60–85% as "person"**; partial persons are in
  the training distribution. For a presence detector this is a feature.
- **Rotation-variant:** sideways people are not detected (expected for
  upright-person training data).
- Far/small figures below roughly 15 px in the 192×192 input are under the
  model's recall floor; boxes on a 12×12 grid are approximate by nature
  (34.7% AP model). Box sizes use an empirically-established sigmoid decode
  with aspect correction; see the header comment in `Src/detection.c`.

## Measuring skip rate (do this right)

- **Keep monitors/TVs out of the camera's view.** A screen changes hundreds
  to thousands of pixels per frame (refresh + content) and legitimately
  holds the gate open; an early "idle" run measured only 24% skip until a
  laptop screen was removed from frame; the same scene then reached 98–99%.
  The HUD shows the live changed-pixel count (`c<n>`) for verification:
  a genuinely still scene reads c0–c2.
- The stats card (hold K1 ≥1.5 s in video mode) shows session totals and
  per-stage timings; photograph it as evidence.
- `SLEEP_IDLE_MS` in `Src/main.c`: 10000 (10 s) is the demo/power setting;
  set 600000 while running long skip-rate benchmarks so STOP mode doesn't
  interrupt the measurement.
- Stats-card **uptime freezes during STOP** (HAL tick suspended); it counts
  awake-time, which is what the frame counters are relative to.

## Power measurement methodology (FNIRSI FNB-C2)

- Meter: FNIRSI FNB-C2 inline on the 5 V USB-C feed. 20-bit ADC, 1 µA
  display resolution, manufacturer-published current accuracy **±0.05% + 2
  counts** (treated conservatively as better-than-±1% class). Single
  instrument; no second-meter cross-check performed.
- Protocol: ≥10 s settle per state, highest observed reading recorded.
  States: GATE OFF always-on / gate+inference (motion in view) /
  gate-idle (still scene) / STOP sleep.
- Expect always-on ≈ gate+inference (sanity check: motion opens the gate
  every frame). The ~82 mA STOP floor is the regulator chain and board,
  not the MCU or the camera (hardware PWDN standby); see the honesty note in the benchmark report.

## Sleep/wake specifics

- Sleep blanks the **panel** to black before cutting brightness: the
  soft-PWM backlight can freeze in an ON state during STOP, so a black
  panel is the guarantee of a dark screen. The panel additionally enters
  **SLPIN** (sleep-in, booster off) before STOP and gets SLPOUT + 120 ms
  on wake, worth ~5 mA at the sleep floor (99 → 94 mA before camera PWDN).
- **Do NOT put the OV2640 into software standby (COM2 bit4) around STOP
  mode.** Tried and reverted: the sensor does not resume streaming after
  wake; video stays frozen/black even after a full `Camera_Init_Device()
  ` re-init on wake (its XCLK also halts during STOP, and register-level
  standby exit is not reliable from that state). The correct camera
  power-down is the hardware PWDN line: solder bridge **SB1** (bridged)
  routes PA7 → DVP_PWDN; silkscreen, schematic and WeAct's OpenMV port
  all confirm PA7 (NOT PD4, which is SB2/MicroSD_SW). Driving PA7 high in
  STOP drops the floor 94 → 82 mA.
- **Wake from PWDN needs a full `Camera_Init_Device()` re-init.** Bare
  PWDN release does resume streaming (which makes quick tests pass), but
  the sensor's black-level/AWB state can come back wedged: persistently
  dark video with purple-magenta light sources. Re-running the init
  table (soft reset included) on wake restores boot-identical quality at
  ~0.4 s wake cost. A brief exposure settle in the first second after
  wake is normal AEC convergence, not this bug.
- Wake sources: PC0 rising edge (RP2350 in Stage 3; jumper to 3V3 to fake
  it) or K1. A K1 wake-press is swallowed so it doesn't toggle gating.

## RP2350 INTERP blend mode

- **Configure BOTH lanes or blend silently breaks.** The blend alpha is
  lane 1's shifted-and-masked result, not raw ACCUM1, and CTRL_LANE1
  resets with a bit0-only mask: alpha truncates to 1 bit and "bilinear"
  quietly becomes nearest-neighbor (95% of blends wrong on our first
  hardware run). Give lane 1 a default pass-through config; see
  `rp2350/src/hw_interpolator_resize.c`. Host builds cannot catch this;
  the INTERP path only compiles on-device.
- Do not expect the INTERP path to be faster for byte-wide bilinear: at
  -O2 it measured 0.90× vs software (see the interpolator guide). The
  artifact's value is the bit-exactness proof and this gotcha.
