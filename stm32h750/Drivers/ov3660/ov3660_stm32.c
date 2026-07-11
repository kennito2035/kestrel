/*
 * OV3660 driver for STM32 DCMI pipelines: implementation.
 *
 * The sensor-facing logic (register sequences, windowing math, binning
 * options, PLL profiles, AE targets) is a faithful port of esp32-camera's
 * sensors/ov3660.c (Apache-2.0); the surrounding structure is rewritten
 * for a callback-injected, RTOS-free environment. Fixed decisions vs the
 * reference driver: 4:3 aspect only, no JPEG, no V-flip/H-mirror.
 *
 * License: MIT for this file; included tables are Apache-2.0 (see notices).
 */
#include "ov3660_stm32.h"
#include "ov3660_tables.h"

/* ------------------------------------------------------------------ */
/* Register-write helpers (mirroring the reference driver's semantics) */
/* ------------------------------------------------------------------ */

static int write_regs(const ov3660_hal_t *hal, const uint16_t (*regs)[2])
{
    for (uint32_t i = 0; regs[i][0] != REGLIST_TAIL || regs[i][1] != 0; i++) {
        if (regs[i][0] == REG_DLY) {
            hal->delay_ms(regs[i][1]);
        } else if (hal->sccb_write(regs[i][0], (uint8_t)regs[i][1])) {
            return 1;
        }
    }
    return 0;
}

static int write_reg16(const ov3660_hal_t *hal, uint16_t reg, uint16_t value)
{
    return hal->sccb_write(reg, (uint8_t)(value >> 8)) ||
           hal->sccb_write(reg + 1, (uint8_t)value);
}

/* Write an X,Y register quad (H/L pairs at reg and reg+2). */
static int write_addr_reg(const ov3660_hal_t *hal, uint16_t reg,
                          uint16_t x_value, uint16_t y_value)
{
    return write_reg16(hal, reg, x_value) ||
           write_reg16(hal, reg + 2, y_value);
}

static int write_reg_bits(const ov3660_hal_t *hal, uint16_t reg,
                          uint8_t mask, int enable)
{
    uint8_t v;
    if (hal->sccb_read(reg, &v)) {
        return 1;
    }
    v = (uint8_t)((v & ~mask) | (enable ? mask : 0));
    return hal->sccb_write(reg, v);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int ov3660_probe(const ov3660_hal_t *hal, uint16_t *chip_id_out)
{
    uint8_t id_h, id_l;
    if (hal->sccb_read(0x300A, &id_h) || hal->sccb_read(0x300B, &id_l)) {
        return 1;
    }
    const uint16_t id = (uint16_t)((id_h << 8) | id_l);
    if (chip_id_out) {
        *chip_id_out = id;
    }
    return (id == OV3660_CHIP_ID) ? 0 : 1;
}

/* Neutral auto-exposure window (reference driver's set_ae_level(0)). */
static int set_ae_level0(const ov3660_hal_t *hal)
{
    const int target_level = 55;               /* ((0 + 5) * 10) + 5     */
    const int level_low = target_level * 23 / 25;
    const int level_high = target_level * 27 / 25;
    const int fast_low = level_low >> 1;
    int fast_high = level_high << 1;
    if (fast_high > 255) {
        fast_high = 255;
    }
    return hal->sccb_write(0x3a0f, (uint8_t)level_high) ||
           hal->sccb_write(0x3a10, (uint8_t)level_low) ||
           hal->sccb_write(0x3a1b, (uint8_t)level_high) ||
           hal->sccb_write(0x3a1e, (uint8_t)level_low) ||
           hal->sccb_write(0x3a11, (uint8_t)fast_high) ||
           hal->sccb_write(0x3a1f, (uint8_t)fast_low);
}

int ov3660_reset(const ov3660_hal_t *hal)
{
    if (hal->sccb_write(SYSTEM_CTROL0, 0x82)) { /* software reset */
        return 1;
    }
    hal->delay_ms(100);
    if (write_regs(hal, sensor_default_regs) || set_ae_level0(hal)) {
        return 1;
    }
    hal->delay_ms(100);
    return 0;
}

int ov3660_set_rgb565(const ov3660_hal_t *hal)
{
    return write_regs(hal, sensor_fmt_rgb565);
}

int ov3660_set_grayscale(const ov3660_hal_t *hal)
{
    return write_regs(hal, sensor_fmt_grayscale);
}

static int set_pll(const ov3660_hal_t *hal, int bypass, uint8_t multiplier,
                   uint8_t sys_div, uint8_t pre_div, int root_2x,
                   uint8_t seld5, int pclk_manual, uint8_t pclk_div)
{
    if (multiplier > 31 || sys_div > 15 || pre_div > 3 || pclk_div > 31 ||
        seld5 > 3) {
        return 1;
    }
    return hal->sccb_write(SC_PLLS_CTRL0, bypass ? 0x80 : 0x00) ||
           hal->sccb_write(SC_PLLS_CTRL1, multiplier & 0x1f) ||
           hal->sccb_write(SC_PLLS_CTRL2, 0x10 | (sys_div & 0x0f)) ||
           hal->sccb_write(SC_PLLS_CTRL3, (uint8_t)(((pre_div & 0x3) << 4) |
                                                    seld5 |
                                                    (root_2x ? 0x40 : 0x00))) ||
           hal->sccb_write(PCLK_RATIO, pclk_div & 0x1f) ||
           hal->sccb_write(VFIFO_CTRL0C, pclk_manual ? 0x22 : 0x20);
}

/* Binning/mirror/flip option registers; fixed vflip=0, hmirror=0, no JPEG.
 * Register values follow the reference driver's set_image_options(). */
static int set_image_options(const ov3660_hal_t *hal, int binning)
{
    const uint8_t reg20 = binning ? 0x01 : 0x40;
    const uint8_t reg21 = binning ? 0x01 : 0x00;
    const uint8_t reg4514 = binning ? 0xaa : 0x88;

    if (hal->sccb_write(TIMING_TC_REG20, reg20) ||
        hal->sccb_write(TIMING_TC_REG21, reg21) ||
        hal->sccb_write(0x4514, reg4514)) {
        return 1;
    }
    if (binning) {
        return hal->sccb_write(0x4520, 0x0b) ||
               hal->sccb_write(X_INCREMENT, 0x31) || /* odd:3, even:1 */
               hal->sccb_write(Y_INCREMENT, 0x31);
    }
    return hal->sccb_write(0x4520, 0xb0) ||
           hal->sccb_write(X_INCREMENT, 0x11) ||     /* odd:1, even:1 */
           hal->sccb_write(Y_INCREMENT, 0x11);
}

int ov3660_set_framesize(const ov3660_hal_t *hal, uint16_t w, uint16_t h)
{
    /* 4:3 only, first entry of the reference ratio table. */
    const ratio_settings_t settings = ratio_table[0];

    if (w > settings.max_width || h > settings.max_height || w == 0 ||
        h == 0 || (uint32_t)w * 3 != (uint32_t)h * 4) {
        return 1;
    }

    const int binning = (w <= settings.max_width / 2) &&
                        (h <= settings.max_height / 2);
    const int scale =
        !((w == settings.max_width && h == settings.max_height) ||
          (w == settings.max_width / 2 && h == settings.max_height / 2));

    if (write_addr_reg(hal, X_ADDR_ST_H, settings.start_x, settings.start_y) ||
        write_addr_reg(hal, X_ADDR_END_H, settings.end_x, settings.end_y) ||
        write_addr_reg(hal, X_OUTPUT_SIZE_H, w, h)) {
        return 1;
    }

    if (binning) {
        if (write_addr_reg(hal, X_TOTAL_SIZE_H, settings.total_x,
                           (uint16_t)(settings.total_y / 2 + 1)) ||
            write_addr_reg(hal, X_OFFSET_H, 8, 2)) {
            return 1;
        }
    } else {
        if (write_addr_reg(hal, X_TOTAL_SIZE_H, settings.total_x,
                           settings.total_y) ||
            write_addr_reg(hal, X_OFFSET_H, 16, 6)) {
            return 1;
        }
    }

    if (write_reg_bits(hal, ISP_CONTROL_01, 0x20, scale) ||
        set_image_options(hal, binning)) {
        return 1;
    }

    /* PLL profiles from the reference driver's non-JPEG branch, tuned for
     * 16MHz XCLK / 8MHz PCLK; PCLK scales linearly with XCLK. */
    if (h > 320) {
        /* > 480x320: 8MHz SYSCLK (~4.4 FPS) */
        return set_pll(hal, 0, 4, 1, 0, 0, 2, 1, 2);
    }
    if (w >= 320) {
        /* QVGA..HVGA: 16MHz SYSCLK (~10.3 FPS) */
        return set_pll(hal, 0, 8, 1, 0, 0, 2, 1, 4);
    }
    /* < QVGA (incl. Kestrel's 160x120): 32MHz SYSCLK (~17.8 FPS) */
    return set_pll(hal, 0, 8, 1, 0, 0, 0, 1, 8);
}
