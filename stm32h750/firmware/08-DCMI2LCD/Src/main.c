/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dcmi.h"
#include "dma.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "gpio.h"
#include "app_x-cube-ai.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "camera.h"
#include "lcd.h"
#include "ai_infer.h"
#include "detection.h"
#include "img_preproc.h"
#include "gate.h"
#include "uart_log.h"
#include "stop_mode.h"
#include "temp_sensor.h"
#include <math.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
extern ST7735_IO_t st7735_pIO;   /* defined in lcd.c, not exported by lcd.h */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
#ifdef TFT96
// QQVGA
#define FrameWidth 160
#define FrameHeight 120
#elif TFT18
// QQVGA2
#define FrameWidth 128
#define FrameHeight 160
#endif
// picture buffer (32-byte aligned so it can be D-cache invalidated cleanly:
// DCMI DMA fills it, so the CPU must invalidate before reading now that AXI SRAM is cacheable)
uint16_t pic[FrameWidth][FrameHeight] __attribute__((aligned(32)));
/* Written in the DCMI frame-event ISR, polled in the main loop; must be
 * volatile so the poll re-reads memory (correct at -O0, required at -O2). */
volatile uint32_t DCMI_FrameIsReady;
volatile uint32_t Camera_FPS=0;
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();
	
  /* Configure the MPU attributes for the QSPI 256MB without instruction access */
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress      = QSPI_BASE;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_256MB;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
  MPU_InitStruct.SubRegionDisable = 0x00;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);
	
  /* Configure the MPU attributes for the QSPI 8MB (QSPI Flash Size) to Cacheable WT */
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.Number           = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress      = QSPI_BASE;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_8MB;
  MPU_InitStruct.AccessPermission = MPU_REGION_PRIV_RO;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
  MPU_InitStruct.SubRegionDisable = 0x00;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);
	
  /* Setup AXI SRAM in Cacheable WB.
   * NOT_SHAREABLE is critical on single-core M7: a Shareable normal-memory region
   * is treated as non-cacheable by the D-cache, which silently ~5x'd inference
   * (all activations/weights ran uncached). WeAct's default was SHAREABLE. */
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress      = D1_AXISRAM_BASE;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.Number           = MPU_REGION_NUMBER2;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);
	
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static void CPU_CACHE_Enable(void)
{
  /* Enable I-Cache */
  SCB_EnableICache();

  /* Enable D-Cache */
  SCB_EnableDCache();
}

void LED_Blink(uint32_t Hdelay, uint32_t Ldelay)
{
  HAL_GPIO_WritePin(PE3_GPIO_Port, PE3_Pin, GPIO_PIN_SET);
  HAL_Delay(Hdelay - 1);
  HAL_GPIO_WritePin(PE3_GPIO_Port, PE3_Pin, GPIO_PIN_RESET);
  HAL_Delay(Ldelay - 1);
}

/* --- motion gate: grayscale frame pair in DTCM (fast, STOP-retained) --- */
static uint8_t gate_gray_a[160 * 120] __attribute__((section(".dtcm_bss")));
static uint8_t gate_gray_b[160 * 120] __attribute__((section(".dtcm_bss")));

static const gate_config_t gate_cfg = {
  .width = 160, .height = 120,
  .pixel_threshold = 30,   /* reject AE-breathing / lamp flicker amplitude */
  .open_count = 320,       /* ~1.7% of frame; a walking person is >>1000px */
  .roi_pad = 8,
};

/* RGB565 frame -> 8-bit gray (green channel as luma proxy, per gate guide) */
__attribute__((optimize("O2")))
static void gate_gray_convert(const uint16_t *src, uint8_t *dst)
{
  for (int i = 0; i < 160 * 120; i++)
  {
    uint16_t v = src[i];
    v = (uint16_t)((v >> 8) | (v << 8));      /* DCMI byte order */
    dst[i] = (uint8_t)(((v >> 5) & 0x3F) << 2);
  }
}

/* 2x-scaled text from the 8x16 font table (32px tall) for the splash. */
extern const unsigned char asc2_1608[][16];
static void draw_char2x(int x, int y, char c, uint16_t color)
{
  const unsigned char *g = asc2_1608[c - ' '];
  for (int t = 0; t < 16; t++)
  {
    unsigned char bits = g[t];
    int col = t / 2, half = (t & 1) * 8;
    for (int b = 0; b < 8; b++)
      if (bits & (0x80 >> b))
        ST7735_LCD_Driver.FillRect(&st7735_pObj, x + col * 2,
                                   y + (half + b) * 2, 2, 2, color);
  }
}

/* Per-stage DWT timings (microseconds), captured live in the video loop.
 * DWT->CYCCNT is already running (enabled by ai_infer_init at boot). */
static uint32_t st_gate_us, st_gray_us, st_resize_us, st_inf_ms;
static inline uint32_t dwt_us_since(uint32_t start)
{
  return (DWT->CYCCNT - start) / (SystemCoreClock / 1000000U);
}

/* Session stats card: shown while K1 is long-held in video mode.
 * Photograph this screen = skip-rate + per-stage-timing evidence, no UART.
 * 6 lines at 13px pitch to fit the 80px-tall viewport. */
static void show_stats_card(uint32_t frames, uint32_t skipped,
                            uint32_t infers, uint8_t gating, uint32_t inf_ms)
{
  uint8_t buf[32];
  uint32_t up = HAL_GetTick() / 1000;
  ST7735_LCD_Driver.FillRect(&st7735_pObj, 0, 0, ST7735Ctx.Width,
                             ST7735Ctx.Height, BLACK);
  sprintf((char *)buf, "kestrel  die %dC", temp_read_c());
  LCD_ShowString(2, 1, 156, 16, 12, buf);
  sprintf((char *)buf, "up %lu:%02lu  gate %s", up / 60, up % 60,
          gating ? "ON" : "OFF");
  LCD_ShowString(2, 14, 156, 16, 12, buf);
  sprintf((char *)buf, "frames %lu", frames);
  LCD_ShowString(2, 27, 156, 16, 12, buf);
  sprintf((char *)buf, "skip %lu (%lu%%)", skipped,
          frames ? skipped * 100 / frames : 0);
  LCD_ShowString(2, 40, 156, 16, 12, buf);
  sprintf((char *)buf, "gate %luus gray %luus", st_gate_us, st_gray_us);
  LCD_ShowString(2, 53, 156, 16, 12, buf);
  sprintf((char *)buf, "resize %luus inf %lums", st_resize_us, inf_ms);
  LCD_ShowString(2, 66, 156, 16, 12, buf);
  (void)infers;
}

/* Draws a 1px rectangle outline on the LCD viewport, clipped to 160x80. */
static void draw_box_px(int x0, int y0, int x1, int y1, uint16_t color)
{
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > 159) x1 = 159;
  if (y1 > 79)  y1 = 79;
  if (x1 <= x0 || y1 <= y0) return;

  int w = x1 - x0 + 1, h = y1 - y0 + 1;
  ST7735_LCD_Driver.FillRect(&st7735_pObj, x0, y0, w, 1, color);
  ST7735_LCD_Driver.FillRect(&st7735_pObj, x0, y1, w, 1, color);
  ST7735_LCD_Driver.FillRect(&st7735_pObj, x0, y0, 1, h, color);
  ST7735_LCD_Driver.FillRect(&st7735_pObj, x1, y0, 1, h, color);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

#ifdef W25Qxx
  SCB->VTOR = QSPI_BASE;
#endif
  MPU_Config();
  CPU_CACHE_Enable();

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_DCMI_Init();
  MX_I2C1_Init();
  MX_SPI4_Init();
  MX_TIM1_Init();
  MX_X_CUBE_AI_Init();
  /* USER CODE BEGIN 2 */
  uint8_t text[32];

  /* SB1 is bridged: PA7 drives the OV2640 PWDN line (active high; R18
   * 10K pulls it low). Schematic and WeAct's OpenMV port both confirm
   * PA7; PD4 is SB2/MicroSD_SW, not the camera. Preload the output latch
   * LOW before switching the pin to output so the camera never sees a
   * power-down glitch at boot. */
  {
    GPIO_InitTypeDef pwdn = {0};
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);
    pwdn.Pin = GPIO_PIN_7;
    pwdn.Mode = GPIO_MODE_OUTPUT_PP;
    pwdn.Pull = GPIO_NOPULL;
    pwdn.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &pwdn);
  }

  /* Minimal LCD bring-up (init part of WeAct's LCD_Test, without the
   * logo/countdown boot show), then a clean "Booting... %" sequence. */
  #ifdef TFT96
  ST7735Ctx.Orientation = ST7735_ORIENTATION_LANDSCAPE_ROT180;
  ST7735Ctx.Panel = HannStar_Panel;
  ST7735Ctx.Type = ST7735_0_9_inch_screen;
  #elif TFT18
  ST7735Ctx.Orientation = ST7735_ORIENTATION_PORTRAIT;
  ST7735Ctx.Panel = BOE_Panel;
  ST7735Ctx.Type = ST7735_1_8a_inch_screen;
  #endif
  ST7735_RegisterBusIO(&st7735_pObj, &st7735_pIO);
  ST7735_LCD_Driver.Init(&st7735_pObj, ST7735_FORMAT_RBG565, &ST7735Ctx);
  ST7735_LCD_Driver.ReadID(&st7735_pObj, &st7735_id);
  LCD_SetBrightness(100);
  POINT_COLOR = WHITE;
  BACK_COLOR  = BLACK;
  ST7735_LCD_Driver.FillRect(&st7735_pObj, 0, 0, ST7735Ctx.Width,
                             ST7735Ctx.Height, BLACK);

  /* Kestrel splash, faithful to the WeActStudio LCD_Test animation:
   * dark screen with centered name -> 1s brightness fade-in -> smooth
   * progress-bar sweep along the bottom -> 300ms fade-out -> inits. */
  LCD_SetBrightness(0);
  {
    const char *name = "kestrel";
    for (int i = 0; name[i]; i++)
    {
      /* faux-bold: second pass offset 2px thickens every stroke */
      draw_char2x(24 + i * 16, 20, name[i], WHITE);
      draw_char2x(26 + i * 16, 20, name[i], WHITE);
    }

    uint32_t tick = HAL_GetTick(), t;
    while ((t = HAL_GetTick() - tick) <= 1000)
    {
      LCD_SetBrightness(t * 100 / 1000);
      HAL_Delay(10);
    }
    /* rainbow progress sweep, WeAct-logo palette feel */
    static const uint16_t bar_pal[5] =
      { 0x001F, 0x07FF, 0x07E0, 0xFFE0, 0xF800 }; /* B C G Y R */
    int last_w = 0;
    tick = HAL_GetTick();
    while ((t = HAL_GetTick() - tick) <= 1500)
    {
      int w = (int)(t * ST7735Ctx.Width / 1500);
      for (int x = last_w; x < w; x++)
        ST7735_LCD_Driver.FillRect(&st7735_pObj, x, ST7735Ctx.Height - 4,
                                   1, 4, bar_pal[x * 5 / ST7735Ctx.Width]);
      last_w = w;
      HAL_Delay(10);
    }
  }
  LCD_Light(0, 300);
  ST7735_LCD_Driver.FillRect(&st7735_pObj, 0, 0, ST7735Ctx.Width,
                             ST7735Ctx.Height, BLACK);

  if (ai_infer_init() == 0)
  {
    ai_infer_fill_dummy_input();
    ai_infer_run();                /* warmup: primes caches */
  }
  else
  {
    sprintf((char *)&text, "AI FAIL");
    LCD_ShowString(4, 20, ST7735Ctx.Width, 16, 16, text);
    HAL_Delay(2000);
  }


  //	HAL_TIM_PWM_Start(&htim1,TIM_CHANNEL_1);
  //	HAL_Delay(10);
  #ifdef TFT96
	Camera_Init_Device(&hi2c1, FRAMESIZE_QQVGA);
	#elif TFT18
	Camera_Init_Device(&hi2c1, FRAMESIZE_QQVGA2);
	#endif
  /* centered ready screen: 6px/char at size 12 */
  /* two centered lines, tight 14px pitch, vertically centered as a group */
  sprintf((char *)&text, "Camera: 0x%x", hcamera.device_id);
  LCD_ShowString((160 - 14 * 6) / 2, 27, ST7735Ctx.Width, 16, 12, text);
  sprintf((char *)&text, "Long press K1 now.");
  LCD_ShowString((160 - 17 * 6) / 2, 41, ST7735Ctx.Width, 16, 12, text);
  LCD_Light(600, 300);   /* WeAct-style fade-in of the ready screen */

  uart_log_init();
  stop_mode_init();
  uart_printf("kestrel,boot\r\ncsv,frame,gated,skipped,changed,state,infer_ms\r\n");

  HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS, (uint32_t)&pic,
                     FrameWidth * FrameHeight * 2 / 4);

  /* require K1 held for a full 2 seconds to start */
  for (uint8_t k1_go = 0; !k1_go; )
  {
    if (HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == GPIO_PIN_SET)
    {
      uint32_t t0 = HAL_GetTick();
      while (HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == GPIO_PIN_SET)
      {
        if (HAL_GetTick() - t0 >= 1000)
        {
          k1_go = 1;
          break;
        }
      }
    }
    else
    {
      LED_Blink(5, 500);
    }
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

  MX_X_CUBE_AI_Process();
    /* USER CODE BEGIN 3 */
    if (DCMI_FrameIsReady)
    {
      DCMI_FrameIsReady = 0;

      /* DMA wrote a fresh frame straight to SRAM; drop the stale cached copy
       * before the LCD reads it (fixes the tearing/horizontal-line artifacts). */
      SCB_InvalidateDCache_by_Addr((uint32_t *)pic, sizeof(pic));
			
      #ifdef TFT96
			ST7735_FillRGBRect(&st7735_pObj,0,0,(uint8_t *)&pic[20][0], ST7735Ctx.Width, 80);
			#elif TFT18
			ST7735_FillRGBRect(&st7735_pObj,0,0,(uint8_t *)&pic[0][0], ST7735Ctx.Width, ST7735Ctx.Height);
			#endif
      /* --- motion gate: only spend the 180ms inference on motion --- */
      static uint8_t *gray_curr = gate_gray_a, *gray_prev = gate_gray_b;
      static uint8_t gate_prev_valid = 0;   /* fail-open until first frame */
      static uint8_t gating_enabled = 1, k1_last = 1; /* K1 held from start */
      static uint32_t g_total = 0, g_skipped = 0;
      /* true sliding 32-frame window: skip%% updates EVERY frame */
      static uint8_t w_hist[32];
      static uint32_t w_idx = 0, w_sum = 0, w_pct = 0;

      /* K1: short tap (on release) toggles gating; hold >=1.5s shows the
       * session stats card until released. */
      static uint32_t k1_t0 = 0;
      static uint32_t s_frames = 0, s_skipped = 0, s_infer = 0; /* never reset */
      uint8_t k1 = (HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == GPIO_PIN_SET);
      if (k1 && !k1_last)
        k1_t0 = HAL_GetTick();
      if (k1 && k1_last && HAL_GetTick() - k1_t0 >= 1500)
      {
        show_stats_card(s_frames, s_skipped, s_infer, gating_enabled, st_inf_ms);
        while (HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == GPIO_PIN_SET) {}
        HAL_Delay(50);
        k1 = 0;                       /* consumed: no toggle on this release */
        DCMI_FrameIsReady = 0;        /* resume video cleanly */
      }
      else if (!k1 && k1_last && HAL_GetTick() - k1_t0 < 1500)
      {
        gating_enabled ^= 1;
        g_total = 0;
        g_skipped = 0;   /* restart per-mode stats for a clean A/B */
        for (int i = 0; i < 32; i++) w_hist[i] = 0;
        w_idx = 0; w_sum = 0; w_pct = 0;
      }
      k1_last = k1;

      uint32_t t_dwt = DWT->CYCCNT;
      gate_gray_convert((const uint16_t *)pic, gray_curr);
      st_gray_us = dwt_us_since(t_dwt);

      gate_state_t gs = GATE_OPEN;          /* fail-open rule */
      gate_roi_t roi;
      uint32_t changed = 0;
      if (gating_enabled && gate_prev_valid)
      {
        t_dwt = DWT->CYCCNT;
        gs = gate_check(&gate_cfg, gray_curr, gray_prev, &roi, &changed);
        st_gate_us = dwt_us_since(t_dwt);
      }
      g_total++;
      s_frames++;
      if (gs != GATE_OPEN) s_skipped++; else s_infer++;
      {
        uint8_t closed = (gs != GATE_OPEN) ? 1 : 0;
        w_sum += closed;
        w_sum -= w_hist[w_idx];
        w_hist[w_idx] = closed;
        w_idx = (w_idx + 1) & 31;
        w_pct = w_sum * 100 / 32;
      }

      /* --- person detection ---
       * Draw the PREVIOUS inference's boxes right after the blit so the
       * overlay persists the whole ~180ms cycle (no blink), then run this
       * frame's inference to refresh the results for the next iteration. */
      static det_box_t det[DET_MAX_BOXES];
      static int n_det = 0;
      static uint32_t det_ms = 0;

      /* Smoothed single-box overlay: EMA position/size of the top box,
       * shown only after 2 consecutive hits (kills jitter + 1-frame ghosts),
       * kept alive briefly through single missed frames. */
      static float sx, sy, sw, sh;
      static int vis = 0;   /* confidence/TTL counter */
      if (vis >= 2)
      {
        int cx = (int)(sx * 160.0f);
        int cy = (int)(sy * 120.0f) - 15;
        int hw = (int)(sw * 80.0f);
        int hh = (int)(sh * 60.0f);
        draw_box_px(cx - hw, cy - hh, cx + hw, cy + hh, RED);
      }
      /* detection status, synced to the SAME visibility rule as the box
       * so text and box appear/disappear together */
      static int shown_n = 0, shown_conf = 0;
      if (n_det > 0)
      {
        shown_n = n_det;
        shown_conf = (int)(det[0].score * 100.0f);
      }
      if (vis >= 2)
        sprintf((char *)&text, "P: %d, %d%% conf, %lums",
                shown_n, shown_conf, det_ms);
      else
        sprintf((char *)&text, "P: 0, %lums", det_ms);
      LCD_ShowString(2, 52, 158, 16, 12, text);

      /* processing FPS (frames actually displayed/processed per second);
       * unlike the camera rate this visibly drops when gating is OFF */
      static uint32_t fps_count = 0, fps_tick = 0, fps_shown = 0;
      fps_count++;
      if (HAL_GetTick() - fps_tick >= 1000)
      {
        fps_shown = fps_count;
        fps_count = 0;
        fps_tick = HAL_GetTick();
      }
      sprintf((char *)&text, "%luFPS", fps_shown);
      LCD_ShowString(5, 5, 60, 16, 12, text);

      /* gate HUD, bottom line: mode + live skip rate */
      if (gating_enabled)
        sprintf((char *)&text, "GATE: ON, %lu%%, c%lu", w_pct, changed);
      else
        sprintf((char *)&text, "GATE: OFF");
      LCD_ShowString(2, 66, 155, 16, 12, text);

      if (gs == GATE_OPEN)
      {
        /* Attention: when the gate localized the motion, crop its (square)
         * ROI into the model input; distant subjects get far more pixels.
         * Fail-open / gating-off frames still use the full frame. */
        int rx = 0, ry = 0, rw = 160, rh = 120;
        if (gating_enabled && gate_prev_valid)
        {
          rx = roi.x; ry = roi.y; rw = roi.w; rh = roi.h;
        }
        t_dwt = DWT->CYCCNT;
        preproc_rgb565_window((const uint16_t *)pic, 160, 120,
                              rx, ry, rw, rh, ai_infer_input_buffer(NULL));
        st_resize_us = dwt_us_since(t_dwt);
        det_ms = ai_infer_run();
        st_inf_ms = det_ms;
        n_det = detection_decode(ai_infer_output_buffer(NULL),
                                 det, DET_MAX_BOXES);
        /* map ROI-space boxes back to full-frame normalized coords */
        for (int i = 0; i < n_det; i++)
        {
          det[i].x = ((float)rx + det[i].x * (float)rw) / 160.0f;
          det[i].y = ((float)ry + det[i].y * (float)rh) / 120.0f;
          det[i].w = det[i].w * (float)rw / 160.0f;
          det[i].h = det[i].h * (float)rh / 120.0f;
        }

        /* update the smoothed box state */
        if (n_det > 0)
        {
          if (vis == 0)
          {
            sx = det[0].x; sy = det[0].y; sw = det[0].w; sh = det[0].h;
          }
          else
          {
            sx += 0.4f * (det[0].x - sx);
            sy += 0.4f * (det[0].y - sy);
            sw += 0.4f * (det[0].w - sw);
            sh += 0.4f * (det[0].h - sh);
          }
          if (vis < 4) vis++;
        }
        else if (vis > 0)
        {
          vis--;   /* fade out: survives single missed frames */
        }
      }
      else
      {
        g_skipped++;   /* scene unchanged: inference skipped entirely */
      }

      /* skip-rate CSV over UART, one line every 16 processed frames
       * (~1/s when idle); capture with CoolTerm for the benchmark data */
      if ((g_total & 0xF) == 0)
        uart_printf("gate,%lu,%u,%lu,%lu,%u,%lu\r\n",
                    g_total, gating_enabled, g_skipped, changed,
                    (unsigned)gs, det_ms);

      /* swap gray frames; from now on the gate has real history */
      { uint8_t *t = gray_curr; gray_curr = gray_prev; gray_prev = t; }
      gate_prev_valid = 1;

      /* --- STOP mode: 10s with zero motion -> deep sleep, PC0 wakes --- */
      static uint32_t last_motion_tick = 0;
      if (gs == GATE_OPEN || !gating_enabled)
      {
        last_motion_tick = HAL_GetTick();
      }
      /* Idle time before STOP. 10000 (10s) for the demo/power story;
       * 600000 (10min) while running skip-rate benchmarks so sleep doesn't
       * interrupt the measurement. */
      #define SLEEP_IDLE_MS 10000
      else if (HAL_GetTick() - last_motion_tick > SLEEP_IDLE_MS)
      {
        ST7735_LCD_Driver.FillRect(&st7735_pObj, 0, 0, ST7735Ctx.Width,
                                   ST7735Ctx.Height, BLACK);
        sprintf((char *)&text, "Press K1 to wake up.");
        LCD_ShowString((160 - 20 * 6) / 2, 34, 156, 16, 12, text);
        uart_printf("stop,enter,%lu\r\n", HAL_GetTick());
        HAL_Delay(700);

        HAL_DCMI_Stop(&hdcmi);
        /* blank the panel itself: even if the soft-PWM backlight freezes
         * in an ON state during STOP, the screen shows black */
        ST7735_LCD_Driver.FillRect(&st7735_pObj, 0, 0, ST7735Ctx.Width,
                                   ST7735Ctx.Height, BLACK);
        LCD_SetBrightness(0);
        HAL_GPIO_WritePin(PE3_GPIO_Port, PE3_Pin, GPIO_PIN_RESET);

        /* Panel deep sleep: SLPIN (display + booster off) */
        {
          uint8_t zero = 0;
          ST7735_LCD_Driver.DisplayOff(&st7735_pObj);
          st7735_write_reg(&st7735_pObj.Ctx, ST7735_SLEEP_IN, &zero, 0);
          HAL_Delay(120);                              /* SLPIN settle */
        }

        /* Camera hard power-down: PWDN high via PA7 through bridged SB1.
         * Registers are retained (supply stays on, PWDN gates the core).
         * NOTE: software COM2 standby was tried instead and does NOT
         * survive STOP (frozen video even after full re-init); only this
         * hardware path is reliable, see docs/troubleshooting.md. */
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET);

        stop_mode_sleep();   /* blocks in STOP until PC0 or K1 rises */

        k1_last = 1;         /* swallow a K1 wake-press: no gating toggle */
        uart_printf("stop,wake,%lu\r\n", HAL_GetTick());

        /* Camera back on first (PWDN low); the panel's 120ms SLPOUT
         * settle below doubles as the sensor's resume time */
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);

        /* Panel wake: SLPOUT needs 120ms before further commands */
        {
          uint8_t zero = 0;
          st7735_write_reg(&st7735_pObj.Ctx, ST7735_SLEEP_OUT, &zero, 0);
          HAL_Delay(120);
          ST7735_LCD_Driver.DisplayOn(&st7735_pObj);
        }

        LCD_SetBrightness(100);
        HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS, (uint32_t)&pic,
                           FrameWidth * FrameHeight * 2 / 4);
        last_motion_tick = HAL_GetTick();
        /* gray_prev survives in DTCM: first post-wake gate check is real */
      }

			LED_Blink(1, 1);
    }
    /* Core sleeps (clock-gated) until the next interrupt (camera frame,
     * SysTick) instead of busy-spinning at 480MHz between frames.
     * Cuts awake-idle power/heat; wakes in nanoseconds, zero latency cost. */
    HAL_PWR_EnterSLEEPMode(PWR_LOWPOWERREGULATOR_ON, PWR_SLEEPENTRY_WFI);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 96;
  /* PLLP 2->1: SYSCLK 240->480MHz so CortexM7 core runs at 480 (rev.V + VOS0).
   * AHBCLKDivider below is bumped /2->/4 to keep HCLK/AXI at 120MHz, so every
   * peripheral + QSPI-exec clock stays identical; only the CPU speeds up. */
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  /* HCLK 120MHz (480/4). Tried HCLK=240 (AXI bus 2x) for more AI speed but it
   * doubles the QSPI code-exec SCLK past the W25Q limit -> instant hard-fault.
   * Reverted. Revisit only after moving QSPI to its own PLL clock or verifying
   * the SCLK margin; not worth it (165ms is already at kernel efficiency). */
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSI48, RCC_MCODIV_4);
}

/* USER CODE BEGIN 4 */

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
	static uint32_t count = 0,tick = 0;
	
	if(HAL_GetTick() - tick >= 1000)
	{
		tick = HAL_GetTick();
		Camera_FPS = count;
		count = 0;
	}
	count ++;
	
  DCMI_FrameIsReady = 1;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  while (1)
  {
    LED_Blink(5, 250);
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
