# OV3660 driver for STM32 DCMI pipelines

**Status: EXPERIMENTAL, compile-verified (Cortex-M7, `-Wall -Wextra
-Werror`), not yet validated on hardware.** Hardware validation is a
Kestrel bonus-round task; the OV2640 is the project's primary camera.

## Why this exists

The OV3660 (3MP, DVP) ships in the same 24-pin FPC format as the
OV2640/OV5640 and is electrically compatible with common STM32 camera
boards (WeAct MiniSTM32H7xx included), but no public STM32/DCMI driver for
it existed that we could find (July 2026). The only open driver is
Espressif's, for ESP32. This is a port of that driver's sensor logic to a
dependency-injected C API usable from any STM32 (or any MCU with I²C).

- `ov3660_stm32.c/.h`, the port (MIT): probe, reset, RGB565/grayscale,
  windowing + binning + PLL for 4:3 sizes from 160×120 to 2048×1536
- `ov3660_regs.h`, `ov3660_tables.h`, register map and init tables derived
  from [esp32-camera](https://github.com/espressif/esp32-camera)
  (**Apache-2.0**, notices preserved; machine-copied, not hand-transcribed)

## Integration (WeAct MiniSTM32H7xx example)

The WeAct BSP's `Camera_WriteRegb2`/`Camera_ReadRegb2` already speak the
16-bit-register SCCB protocol this sensor needs (they exist for the
OV5640, which shares the OV3660's I²C address 0x78 and register style):

```c
static int wr(uint16_t reg, uint8_t val) { return Camera_WriteRegb2(&hcamera, reg, val); }
static int rd(uint16_t reg, uint8_t *val) { return Camera_ReadRegb2(&hcamera, reg, val); }
static const ov3660_hal_t hal = { wr, rd, HAL_Delay };

if (ov3660_probe(&hal, NULL) == 0) {      /* chip ID == 0x3660 */
    ov3660_reset(&hal);
    ov3660_set_rgb565(&hal);
    ov3660_set_framesize(&hal, 160, 120); /* Kestrel's gate resolution */
}
```

DCMI settings are the same as for the OV5640 path (8-bit DVP, HSYNC/VSYNC
polarities per the default register table). PLL profiles are tuned for a
16MHz XCLK; PCLK scales linearly with whatever XCLK you feed via MCO1.

## Hardware validation checklist (bonus round)

1. `ov3660_probe` returns 0 and reports chip ID 0x3660 over SCCB
2. After reset + RGB565 + 160×120: DCMI VSYNC/HSYNC/PCLK present (scope or
   DCMI IT flags), frame completes
3. Image sanity: colors correct (RGB565 byte order), no tearing; if the
   image is mirrored/flipped, adjust `set_image_options` (fixed
   vflip/hmirror in this port)
4. Measure actual FPS at 160×120 vs the reference driver's ~17.8 FPS @
   16MHz XCLK; record in `benchmarks/`
5. Exposure sane in indoor light (AE defaults ported at neutral level)
