/*
 * OV3660 driver for STM32 DCMI pipelines, a port of the sensor logic from
 * esp32-camera (Apache-2.0) to a dependency-injected C API.
 *
 * STATUS: EXPERIMENTAL, compile-verified, not yet validated on hardware.
 *   We found no public STM32/DCMI OV3660 driver (as of July 2026); this port
 *   exists so the OV3660 can join the OV2640/OV5640/OV7670/OV7725 sensors
 *   already supported by common STM32 camera boards. Validation checklist in
 *   README.md alongside this file.
 *
 * No HAL dependency: supply three callbacks. Example glue for a project
 * using I2C1 (SCCB is I2C-compatible; OV3660 8-bit address 0x78):
 *
 *   static int wr(uint16_t reg, uint8_t val) {
 *       uint8_t b[3] = { reg >> 8, reg & 0xFF, val };
 *       return HAL_I2C_Master_Transmit(&hi2c1, 0x78, b, 3, 100) != HAL_OK;
 *   }
 *   static int rd(uint16_t reg, uint8_t *val) {
 *       uint8_t a[2] = { reg >> 8, reg & 0xFF };
 *       if (HAL_I2C_Master_Transmit(&hi2c1, 0x78, a, 2, 100) != HAL_OK) return 1;
 *       return HAL_I2C_Master_Receive(&hi2c1, 0x79, val, 1, 100) != HAL_OK;
 *   }
 *   static const ov3660_hal_t hal = { wr, rd, HAL_Delay };
 *
 * Typical bring-up:
 *   ov3660_probe(&hal, NULL)                 -> confirms chip ID 0x3660
 *   ov3660_reset(&hal)                       -> defaults loaded
 *   ov3660_set_rgb565(&hal)                  -> DVP RGB565 output
 *   ov3660_set_framesize(&hal, 160, 120)     -> windowing + binning + PLL
 *
 * PLL profiles are tuned for a 16MHz XCLK (as in the reference driver);
 * PCLK scales proportionally with XCLK. On boards clocking XCLK from MCO1
 * at 12MHz, expect ~3/4 of the listed frame rates.
 *
 * License: MIT for this file (see repository root). The register tables it
 * loads (ov3660_tables.h / ov3660_regs.h) are Apache-2.0, derived from
 * esp32-camera; see the notice in those files.
 */
#ifndef KESTREL_OV3660_STM32_H
#define KESTREL_OV3660_STM32_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OV3660_SCCB_ADDR_WRITE 0x78 /* 8-bit form; 7-bit address is 0x3C */
#define OV3660_SCCB_ADDR_READ  0x79
#define OV3660_CHIP_ID         0x3660

typedef struct {
    int (*sccb_write)(uint16_t reg, uint8_t val); /* return 0 on success  */
    int (*sccb_read)(uint16_t reg, uint8_t *val); /* return 0 on success  */
    void (*delay_ms)(uint32_t ms);
} ov3660_hal_t;

/* Read the chip ID registers (0x300A/0x300B). Returns 0 iff the ID is
 * 0x3660; the raw ID is stored to *chip_id_out when non-NULL. */
int ov3660_probe(const ov3660_hal_t *hal, uint16_t *chip_id_out);

/* Software reset, load reference defaults (215 registers), neutral AE. */
int ov3660_reset(const ov3660_hal_t *hal);

/* Output format on the DVP port. */
int ov3660_set_rgb565(const ov3660_hal_t *hal);
int ov3660_set_grayscale(const ov3660_hal_t *hal);

/* Configure sensor windowing, binning/scaling and PLL for a 4:3 output.
 * Supported sizes (validated against the reference driver's ratio table):
 * 160x120, 320x240, 640x480, 1024x768, 2048x1536. */
int ov3660_set_framesize(const ov3660_hal_t *hal, uint16_t w, uint16_t h);

#ifdef __cplusplus
}
#endif

#endif /* KESTREL_OV3660_STM32_H */
