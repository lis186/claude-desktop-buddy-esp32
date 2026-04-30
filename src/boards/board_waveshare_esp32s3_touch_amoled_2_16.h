// src/boards/board_waveshare_esp32s3_touch_amoled_2_16.h
#pragma once

// Display: 466×466 round AMOLED (CO5300), same as 1.75c. PSRAM-backed
// bilinear letterbox (184×224 → 276×336 centred in the 466 frame).
#define LCD_W_PHYS  466
#define LCD_H_PHYS  466

#define BOARD_HW_W        184
#define BOARD_HW_H        224
// Round panel with rounded-square bezel — same SAFE_INSET as 1.75c.
#define BOARD_SAFE_INSET  35

// QSPI to CO5300 (LCD_RESET / TP_RESET pins differ from 1.75c)
#define PIN_LCD_SDIO0  4
#define PIN_LCD_SDIO1  5
#define PIN_LCD_SDIO2  6
#define PIN_LCD_SDIO3  7
#define PIN_LCD_SCLK   38
#define PIN_LCD_CS     12
#define PIN_LCD_RESET  39   // direct GPIO, not the GPIO 1 of 1.75c
#define PIN_TP_RESET   40   // direct GPIO, not the GPIO 2 of 1.75c

// I2C bus (shared: AXP2101, ES8311, ES7210, CST92xx, QMI8658, PCF85063)
#define PIN_I2C_SDA   15
#define PIN_I2C_SCL   14

// FT3168/CST9217 family touch interrupt
#define PIN_TP_INT    11

// I2S to ES8311 codec — MCLK on GPIO42 (different from 1.75c's GPIO16)
#define PIN_I2S_MCLK  42
#define PIN_I2S_BCLK  9
#define PIN_I2S_WS    45
#define PIN_I2S_DI    10
#define PIN_I2S_DO    8
#define PIN_PA_CTRL   46

// Three physical keys
#define PIN_KEY1      16   // PWR silkscreen, middle. Active-HIGH via BSS138 inverter (PWRON gate).
#define PIN_KEY2      18   // IO18 silkscreen, left. Active-low.
#define PIN_KEY_BOOT  0    // BOOT silkscreen, right. Active-low. Synthesises BTN_A_LONG_PRESS.

// Capability flags
#define BOARD_HAS_PSRAM            1
#define BOARD_HAS_TCA9554          0
#define BOARD_HAS_PCF85063         1
#define BOARD_HAS_PA_CTRL          1
#define BOARD_HAS_AXP2101          1
#define BOARD_LCD_RST_VIA_PMU      0   // has its own GPIO reset
#define BOARD_AXP_PWRON_4S_OFF     1   // PWR key powers off via AXP after 4 s hold
#define BOARD_AXP_ENABLE_AUX_LDOS  0   // DSI_PWR_EN is on VCC3V3 (R17 pull-up), not ALDO2
#define BOARD_DISPLAY_CO5300       1
#define BOARD_DISPLAY_LETTERBOX    1
#define BOARD_DISPLAY_DEST_W       276
#define BOARD_DISPLAY_DEST_H       336
#define BOARD_DISPLAY_SH8601_VENDOR_INIT  0
#define BOARD_DISPLAY_OFFSET_X     0   // letterbox path uses its own centring math
#define BOARD_DISPLAY_OFFSET_Y     0
#define BOARD_DISPLAY_SCALE        1   // letterbox path uses its own scale; touch falls back to /1
#define BOARD_DISPLAY_PUSH_STREAMED 0
#define BOARD_TOUCH_CST92XX        1
#define BOARD_BTN_SWAP_AB          0   // scanning lands keys in correct slots; no swap
#define BOARD_BTN_THIRD            1
#define BOARD_KEY1_ACTIVE_HIGH     1
#define BOARD_HAS_KEY2             1

// Credits page
#define BOARD_MODEL_LINE1  "Waveshare ESP32-S3"
#define BOARD_MODEL_LINE2  "Touch AMOLED 2.16"
