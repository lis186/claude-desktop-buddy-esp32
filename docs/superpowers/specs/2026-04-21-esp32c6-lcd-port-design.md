# Port Design: Waveshare ESP32-C6-Touch-LCD-1.47

**Date:** 2026-04-21
**Status:** Approved
**Target directory:** `~/Documents/claude-desktop-buddy-lcd` (new sibling project)

## Background

The existing codebase targets two Waveshare ESP32-S3 AMOLED boards (1.8 rectangular 368×448, 1.75C round 466×466). Both have 8 MB OPI PSRAM, AXP2101 PMU, ES8311 audio codec, and QSPI AMOLED panels. This spec covers porting the same buddy experience to the **Waveshare ESP32-C6-Touch-LCD-1.47**, which is a different class of board: single-core RISC-V, no PSRAM, no PMU, no audio, smaller LCD (172×320 ST7789 over ordinary SPI).

Rather than extending the existing board-abstraction pattern in the S3 project, the C6 port lives in a new independent PlatformIO project. The BLE NUS wire protocol is unchanged; the macOS/Windows Hardware Buddy desktop app connects to the device exactly as it does to the S3 boards.

## Goals

- Ship a working C6 firmware that pairs over BLE, receives messages, renders the 9-page UI, and animates the 18 ASCII buddies with their 7 states
- Reuse the existing board-agnostic canvas (184×224) so buddy/font/BLE/JSON code ports verbatim
- Establish a single-env PlatformIO project at `~/Documents/claude-desktop-buddy-lcd`
- Leave the S3 project completely untouched

## Out of Scope

- Custom GIF characters (no PSRAM → no room for decode buffers or 1.8 MB character packs)
- Audio / prompt tones (no ES8311 codec or speaker on the board)
- Hardware power-off (no PMU; software-reset instead)
- Light sleep / deep sleep (MVP keeps MCU always on; LCD panel sleep only)
- Sharing code between the S3 and C6 projects via git submodule or library — full copy at start, independent evolution

---

## Decisions Summary

| # | Decision | Choice |
|---|---|---|
| 1 | Feature scope | Non-audio / non-GIF full feature set (18 buddies, full menus, sleep/wake, IMU gestures, 9-page nav) |
| 2 | Canvas → screen mapping | Crop 184×224 canvas to 172×224 (left/right 6 px each, lands inside `BOARD_SAFE_INSET=8`), place at y=96; 96 px top band is a dedicated status bar |
| 3 | Input mapping | BOOT key (GPIO9) = "A"; touch replaces "B" (approval lower half, transcript scroll, pagination); long-press BOOT = menu; ~5 s BOOT = `ESP.restart()` |
| 4 | Code sharing with S3 project | Full copy of the existing tree, then trim; independent evolution |
| 5 | Status bar content | Clock + BLE indicator + battery % (top row) + activity HUD (busy session count, red attention-pending pulse) |
| 6 | Sleep strategy | LCD panel sleep + backlight off only; MCU and BLE stay online always |

---

## Hardware Reference

Derived from the ESP32-C6-Touch-LCD-1.47 schematic and the vendor demo code at `~/Downloads/ESP32-C6-Touch-LCD-1.47-Demo/`.

### MCU

ESP32-C6FH8 — single-core RISC-V, 160 MHz, 512 KB HP SRAM, 8 MB flash, BLE 5.0 LE, WiFi 6, 802.15.4. **No PSRAM.** Native USB CDC on GPIO12/13.

### Pin Mapping

| Signal | Pin | Notes |
|---|---|---|
| `PIN_LCD_SCLK` | GPIO1 | ST7789 via ordinary HWSPI |
| `PIN_LCD_MOSI` | GPIO2 | SPI also shared with SD card (unused) |
| `PIN_LCD_DC` | GPIO15 | |
| `PIN_LCD_CS` | GPIO14 | |
| `PIN_LCD_RST` | GPIO22 | Direct GPIO (no expander) |
| `PIN_LCD_BL` | GPIO23 | Backlight via SS8050 NPN driver; PWM-capable |
| `PIN_TP_SDA` | GPIO18 | Shared I²C bus with IMU |
| `PIN_TP_SCL` | GPIO19 | Shared I²C bus with IMU |
| `PIN_TP_RST` | GPIO20 | |
| `PIN_TP_INT` | GPIO21 | |
| `PIN_IMU_INT1` | GPIO5 | |
| `PIN_IMU_INT2` | GPIO6 | |
| `PIN_BOOT_KEY` | GPIO9 | Key2; also BOOT strapping (must be high at power-on) |
| `PIN_BAT_ADC` | GPIO0 | Via 200K/100K divider (`voltage = raw_mv * 3`) |
| Key1 | → CHIP_PU | **Hardware reset only; not readable as GPIO** |

### Peripherals

| Peripheral | Part | Notes |
|---|---|---|
| Display | ST7789 1.47" 172×320 | QSPI replaced by ordinary SPI; needs 34-px column offset; demo ships a vendor init sequence |
| Touch | AXS5106L @ I²C | New driver — vendor from demo (`esp_lcd_touch_axs5106l` Arduino wrapper) |
| IMU | QMI8658A @ 0x6B | Same family as S3 boards; `SensorLib` supports it directly |
| Battery | GPIO0 ADC | 200K/100K divider; 3.0–4.2 V linear → 0–100 % |
| USB detect | VBUS line (no MCU connection) | Heuristic: ADC voltage > 4.0 V ⇒ charging |
| SD card | MUP M617-2 | Present but unused in this port |

### Critical Absences vs S3

- No PSRAM
- No AXP2101 PMU (no battery gauge IC, no PEK button, no programmable shutdown)
- No ES8311 audio codec or amplifier or speaker
- No TCA9554 I²C GPIO expander
- No PCF85063 RTC
- Only one user button (GPIO9); Key1 is wired to CHIP_PU (hardware reset)

---

## Architecture

### Project Layout

```
claude-desktop-buddy-lcd/
├── platformio.ini                 ← single env, mcu=esp32c6
├── src/
│   ├── main.cpp                   ← (copied verbatim, then targeted edits)
│   ├── buddy.{cpp,h}              ← copied verbatim
│   ├── buddies/                   ← copied verbatim (all 18 species × 7 states)
│   ├── buddy_common.h             ← copied verbatim
│   ├── ble_bridge.{cpp,h}         ← copied verbatim (NimBLE NUS)
│   ├── data.h                     ← copied verbatim (JSON + CJK matrixifier)
│   ├── stats.h                    ← copied verbatim (NVS)
│   ├── boards/
│   │   └── board_waveshare_esp32c6_touch_lcd_1_47.h   ← NEW
│   └── hw/
│       ├── pins.h                 ← dispatcher points at the C6 header only
│       ├── display.{cpp,h}        ← rewritten (ST7789 + crop + statusbar push)
│       ├── input.{cpp,h}          ← rewritten (single key + touch→B synthesis)
│       ├── power.{cpp,h}          ← rewritten (ADC battery, no AXP)
│       ├── rtc.{cpp,h}            ← copied (software-clock path from 1.75C reused)
│       ├── imu.{cpp,h}            ← copied (QMI8658 same as S3)
│       ├── hw.{cpp,h}             ← trimmed (audio/expander init removed)
│       └── statusbar.{cpp,h}      ← NEW (96 px top band)
└── lib/
    └── esp_lcd_touch_axs5106l/    ← vendored from demo
```

### Files Deleted vs S3 Project

```
src/character.{cpp,h}              — custom GIF characters (no PSRAM)
src/xfer.h                         — character-pack BLE transfer
src/main.cpp.m5stick.bak           — irrelevant legacy
src/hw/audio.{cpp,h}               — ES8311 + speaker
src/hw/expander.{cpp,h}            — TCA9554
src/hw/border.{cpp,h}              — circular bezel mask (1.75C only)
src/boards/board_waveshare_esp32s3_touch_amoled_*.h
lib/ES8311/
lib/Arduino_DriveBus/              — FT3168 touch (S3 1.8 only)
lib/Adafruit_XCA9554/
characters/                        — sample GIF packs
tools/flash_character.py           — character upload tool
```

### `main.cpp` Edits

`main.cpp` was "board-agnostic" on the S3 project. On the C6 port, targeted edits are needed:

- Remove all `character.*` / GIF-mode call sites (~6–10 lines across settings menu, reset flow, state dispatch). Replace with `// GIF characters not available on C6`.
- Remove all `audio.*` call sites (hover tones, confirmation blips). Replace with no-ops.
- Status-bar rendering delegated to `hw/display.cpp::hwDisplayPush` internally — `main.cpp` does not draw the status bar directly, so page state machine stays as-is.
- Settings menu item list trimmed: no "sound volume", no "custom character", no "hard power off". New item: "brightness" (PWM on `PIN_LCD_BL`).
- Input event stream: the existing `BTN_A_*` / `BTN_B_*` abstraction is preserved. `hw/input.cpp` synthesizes `BTN_B_PRESS` from touch events (bottom 32 px tap, approval lower-half tap), so main.cpp's event handlers do not change.

### `platformio.ini`

```ini
[env]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/53.03.13/platform-espressif32.zip
board = esp32-c6-devkitc-1
framework = arduino
monitor_speed = 115200
upload_speed = 921600

board_build.mcu = esp32c6
board_build.f_cpu = 160000000L
board_build.flash_mode = qio
board_upload.flash_size = 8MB
board_build.partitions = no_ota_8mb.csv
board_build.filesystem = littlefs

build_src_filter = +<*> +<buddies/> +<hw/>

lib_deps =
    moononournation/GFX Library for Arduino
    lewisxhe/SensorLib                 ; for QMI8658 only
    bblanchon/ArduinoJson @ ^7.0.0
    h2zero/NimBLE-Arduino @ ^2.0       ; bumped from ^1.4 for C6 support
    olikraus/U8g2 @ ^2.35

[env:waveshare-esp32c6-touch-lcd-1-47]
build_flags =
    -DCORE_DEBUG_LEVEL=0
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DU8G2_USE_LARGE_FONTS
    -DU8G2_FONT_SUPPORT
    -DBOARD_WAVESHARE_ESP32C6_TOUCH_LCD_1_47
```

Removed vs S3: `-DBOARD_HAS_PSRAM`, `-DXPOWERS_CHIP_AXP2101`, OPI PSRAM settings, QIO_OPI memory type, two env blocks → one.

---

## Board Header

`src/boards/board_waveshare_esp32c6_touch_lcd_1_47.h` collects every hardware parameter:

```c
#pragma once

// MCU marker (for #if branches in hw/*.cpp)
#define BOARD_MCU_ESP32C6  1

// Logical canvas — unchanged from S3 boards, keeps buddy code board-agnostic
#define BOARD_HW_W        184
#define BOARD_HW_H        224
#define BOARD_SAFE_INSET  8

// Physical screen (new fields for this board class)
#define BOARD_SCREEN_W          172
#define BOARD_SCREEN_H          320
#define BOARD_STATUSBAR_H       96   // top band reserved for HUD
#define BOARD_CANVAS_CROP_X     6    // crop 6 px off each side of the 184-wide canvas
#define BOARD_CANVAS_OFFSET_Y   96   // canvas starts at y=96 on the panel

// Display driver
#define BOARD_DISPLAY_ST7789      1
#define BOARD_DISPLAY_CO5300      0
#define BOARD_DISPLAY_QSPI        0
#define BOARD_DISPLAY_LETTERBOX   0
#define BOARD_ST7789_COL_OFFSET   34

#define PIN_LCD_SCLK   1
#define PIN_LCD_MOSI   2
#define PIN_LCD_DC     15
#define PIN_LCD_CS     14
#define PIN_LCD_RST    22
#define PIN_LCD_BL     23

// Touch
#define BOARD_TOUCH_AXS5106L  1
#define BOARD_TOUCH_FT3168    0
#define BOARD_TOUCH_CST92XX   0
#define PIN_TP_SDA   18
#define PIN_TP_SCL   19
#define PIN_TP_RST   20
#define PIN_TP_INT   21

// IMU (QMI8658, shared I²C bus with touch)
#define PIN_IMU_INT1  5
#define PIN_IMU_INT2  6

// Single button
#define BOARD_BTN_SINGLE  1
#define BOARD_BTN_SWAP_AB 0
#define PIN_BOOT_KEY      9

// Battery via ADC
#define BOARD_HAS_BAT_ADC   1
#define PIN_BAT_ADC         0
#define BOARD_BAT_ADC_MULT  3   // 200K/100K divider

// Absent peripherals
#define BOARD_HAS_PSRAM           0
#define BOARD_HAS_TCA9554         0
#define BOARD_HAS_AXP2101         0
#define BOARD_HAS_PCF85063        0
#define BOARD_HAS_AUDIO           0
#define BOARD_HAS_CHARACTER_GIF   0

// Credits page
#define BOARD_MODEL_LINE1  "Waveshare ESP32-C6"
#define BOARD_MODEL_LINE2  "Touch LCD 1.47"
```

`hw/pins.h` becomes a single-branch include of this header (no need for a dispatcher on a single-board project).

---

## Display Pipeline

### Init (`hwDisplayInit`)

1. `Arduino_HWSPI(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI)`
2. `Arduino_ST7789(bus, PIN_LCD_RST, 0, false, 172, 320, 34, 0, 34, 0)`
3. `gfx->begin()`
4. Run the vendor init sequence (`lcd_reg_init` from `01_gfx_helloworld.ino`, ~100 lines of `batchOperation`). Copy verbatim — it configures gamma, power rails, colour mode, windows for the 172×320 variant.
5. `gfx->setRotation(0)`, `gfx->fillScreen(RGB565_BLACK)`
6. `ledcAttach(PIN_LCD_BL, 5000, 8)` + `ledcWrite(PIN_LCD_BL, 255)` for PWM-dimmable backlight (default full brightness; brightness menu item writes a duty 0–255)

### Push (`hwDisplayPush`)

Per frame:

1. Extract the 172-wide middle band from the 184-wide canvas:
   `gfx->draw16bitRGBBitmap(0, BOARD_CANVAS_OFFSET_Y, canvas_row_ptr(6), 172, 224)` — the `Arduino_GFX` bitmap blitter accepts arbitrary subregions by incrementing the pointer by `BOARD_CANVAS_CROP_X*2` bytes per row; if the library's blitter requires contiguous rows, copy into a 172×224 scratch once per frame (32 KB extra on the stack or a static buffer).
2. Call `statusbarRender()` which independently maintains its own 172×96 framebuffer and blits to `(0, 0, 172, 96)` only when dirty.

### Sleep (`hwDisplaySleep(bool)`)

- ON: `gfx->displayOff()` → ST7789 `0x28 DISPOFF` + `0x10 SLPIN`; `ledcWrite(PIN_LCD_BL, 0)` to cut backlight
- OFF: `ledcWrite(PIN_LCD_BL, savedBrightness)`; `gfx->displayOn()` → `0x11 SLPOUT` + `0x29 DISPON`; force a full redraw on next frame (mark both canvas and statusbar dirty)

### Status Bar (`hw/statusbar.cpp`)

A `static uint16_t sb_buf[172 * 96]` in DRAM (32 KB). Split into two regions:

- **Row 0–23** (top strip): clock `HH:MM` on the left (U8g2 bold font), a 6-px BLE indicator dot centred, battery `NN%` right-justified. Indicator states: filled green = connected + bonded; blue pulsing = pairing in progress (passkey on screen); hollow grey = advertising but no client; dim red = no client and not advertising.
- **Row 24–95** (HUD strip): state-dependent. `busy` → "● 3 sessions" text plus a horizontal token progress bar. `attention` → entire strip filled red at 2 Hz pulse with white "approval pending" text. `idle` / `sleep` / `celebrate` / `dizzy` / `heart` → left blank (inherits last-state black fill).

Refresh cadence:

- Clock: once per second
- BLE / battery: every 5 seconds
- Busy HUD: 4 Hz
- Attention HUD: 4 Hz (for the pulse)

Dirty tracking means the SPI blit to the top 96 rows only happens when something actually changed. Canvas blits (rows 96–319) happen every buddy animation frame (~20 Hz target).

---

## Input

`hw/input.cpp` implements a single-button state machine and a touch event dispatcher. It emits the same `BTN_A_PRESS`, `BTN_B_PRESS`, `BTN_A_LONG_PRESS`, `GESTURE_SWIPE_*`, etc. events that `main.cpp` already dispatches on, so main.cpp's handler tree is untouched.

### Button (GPIO9)

Active-low, 10 K external pull-up, 100 nF debounce cap.

- Press released in <500 ms → `BTN_A_PRESS`
- Press released in 1–5 s → `BTN_A_LONG_PRESS` (enters menu)
- Press still held at 5 s → immediate `ESP.restart()` without waiting for release (C6 has no hardware-off equivalent; restart is the strongest action available)

### Touch (AXS5106L)

`bsp_touch_init(&Wire, PIN_TP_RST, PIN_TP_INT, 0, 172, 320)` from the vendored driver. Polled each loop iteration or on TP_INT falling edge.

Touch coordinates arrive in screen space (0–171, 0–319). Before feeding to canvas-aware handlers, subtract `BOARD_CANVAS_OFFSET_Y=96` from y and add `BOARD_CANVAS_CROP_X=6` to x, so they become canvas-space (0–183, 0–223). Taps where y < 96 go to the status-bar layer (currently no-op; reserved for future shortcuts).

Touch gestures mapped (canvas-space y after offset):

| Gesture | Emits |
|---|---|
| Swipe up | `GESTURE_SWIPE_UP` (cycle displayMode forward) |
| Swipe down | `GESTURE_SWIPE_DOWN` (cycle displayMode backward) |
| Swipe left | `GESTURE_SWIPE_LEFT` (species cycle on clock home) |
| Swipe right | `GESTURE_SWIPE_RIGHT` (species cycle on clock home) |
| Tap upper half on approval screen | `BTN_A_PRESS` (approve) |
| Tap lower half on approval screen | `BTN_B_PRESS` (deny) |
| Tap bottom 32 px on Normal screen | `BTN_B_PRESS` (transcript scroll) |
| Tap top-right corner on Info/Pet pages | `BTN_A_PRESS` (cycle pages) |
| Tap menu row | `BTN_A_PRESS` (select + confirm) |

---

## Power

`hw/power.cpp` reimplemented without AXP:

- `powerInit()`: configure `PIN_BAT_ADC` as `analogRead` input, 12-bit resolution, 11 dB attenuation.
- `powerBatteryPercent()`: `raw_mv = analogReadMilliVolts(PIN_BAT_ADC)`, `vbat = raw_mv * 3 / 1000.0` (the 200K/100K divider), then linear map 3.0 V → 0 %, 4.2 V → 100 %, clamp.
- `powerIsCharging()`: heuristic — if vbat > 4.0 V or a moving average trending up, assume USB plugged. True signal would require wiring the ETA6098 `STAT` pin, which the schematic does not connect to any MCU GPIO.
- `powerIsUSB()`: same heuristic as isCharging (used to gate the auto-off timer).
- `powerShutdown()`: no-op / `ESP.restart()`. The device cannot truly power itself off.
- `powerPEKPressed()` / AXP IRQ polling: unused stubs.

Auto-off timing unchanged from S3:

| State | Timeout |
|---|---|
| USB plugged | never |
| Battery + clock visible | 5 min |
| Battery + other screens | 30 s |
| Approval prompt up | never |

On timeout, call `hwDisplaySleep(true)`. Any button press or touch wakes.

---

## Memory Budget

ESP32-C6FH8 has roughly 512 KB of HP SRAM available to the application after ROM / IRAM.

| Allocation | Size | Placement |
|---|---|---|
| NimBLE stack + GATT + bonding table | ~40 KB | heap |
| Arduino + FreeRTOS + radio driver baseline | ~60 KB | static/heap |
| LittleFS mount + NVS | ~10 KB | heap |
| **Canvas 184×224×2 (RGB565)** | **82 KB** | heap |
| **Status bar 172×96×2** | **32 KB** | static DRAM |
| Optional row-scratch 172×224×2 (if GFX bitmap needs contiguous rows) | 77 KB (dynamic, per push) | heap or static |
| Transcript ring buffer (~12 lines × 128 B) | ~2 KB | heap |
| ArduinoJson document pool | 8 KB | static |
| ASCII pet render temps | <1 KB | stack |
| Main + BLE FreeRTOS task stacks | ~16 KB | static |

Conservative total: ~250 KB; comfortable ~260 KB headroom. ArduinoJson shrinks from 16 KB (S3 PSRAM) to 8 KB, which limits max single-message size to ~4 KB — fine for current wire protocol usage.

Row-scratch is the swing allocation: if `draw16bitRGBBitmap` happily accepts a pointer into the middle of the 184-wide canvas with a manual stride, no extra buffer is needed; if it requires contiguous rows, a one-shot `static uint16_t scratch[172*224]` goes into DRAM.

---

## Wire Protocol

Unchanged. Same NUS UUIDs, same JSON schemas, same line-buffered framing, same pairing passkey flow, same `bleClearBonds`. The only observable difference to the desktop app is the `status` ack's `model` field, which becomes `"waveshare-c6-lcd-1.47"`. The app's board-gating logic (if any) treats unknown models as generic.

---

## Risks & Unknowns

1. **pioarduino ESP32-C6 + NimBLE-Arduino compatibility.** `h2zero/NimBLE-Arduino @ ^1.4` may not support C6; bumping to `^2.0` is likely but untested in this project. Fallback: call ESP-IDF NimBLE APIs directly via Arduino as Component. First-flash BLE pairing is the earliest test to surface this.
2. **AXS5106L Arduino driver portability.** The vendored `esp_lcd_touch_axs5106l` depends on `esp_lcd_touch` (an IDF component). Inside pioarduino arduino-framework builds, IDF components are available under `esp32-hal-*`. If the driver fails to link, rewrite as a thin `Wire`-based poller using the public AXS5106L protocol (register 0x01 returns N touches, 0x02+ returns `[x_hi, x_lo, y_hi, y_lo]` tuples — reverse-engineerable from the demo's `bsp_touch_read` source).
3. **GPIO9 is a BOOT strapping pin.** Must be high at power-on for normal boot; 10 K external pull-up handles it. Holding GPIO9 while plugging USB enters ROM download mode (useful for flashing, not a bug). Once booted, reading GPIO9 as a regular input works normally.
4. **Single-core 160 MHz render performance.** Expect ~15–20 FPS for the buddy canvas (vs 25–30 on S3). High-rate animations (attention pulse, dizzy spin) may drop from 8 Hz to 4 Hz. Acceptable.
5. **Auto-off timer on battery with BLE always on.** Estimated ~30–50 mA average draw on battery with screen off; 500 mAh cell ≈ 10–15 h standby. Not great but acceptable MVP; light-sleep is a post-MVP optimisation.
6. **USB / charging detection without STAT pin.** The voltage-threshold heuristic is crude and may flap around 4.0 V. If it becomes a problem, add a small hysteresis (4.1 V to enter charging, 3.9 V to exit) and a moving average.
7. **Column-offset correctness for ST7789 172 variant.** Demo uses 34/0/34/0; this is specific to this panel. Wrong offsets show a slipped image. First-boot visual check catches it immediately.

---

## Testing Approach

No unit tests — this is a hardware port. Validation is empirical on the physical board.

**Build gates (per commit):**

- `pio run -e waveshare-esp32c6-touch-lcd-1-47` must compile clean.
- `pio run -e waveshare-esp32c6-touch-lcd-1-47 -t size` — confirm flash usage < 2 MB and DRAM usage leaves > 100 KB headroom.

**Smoke tests (after flashing):**

1. Panel lights up, shows a full-screen black fill, no garbled image (verifies ST7789 init sequence and column offsets).
2. `hello-world` canvas blit at `(0, 96)` renders within the visible area, no wrap.
3. Status bar shows clock ticking, battery %, and "BLE:off" indicator.
4. BOOT short-press advances the buddy display mode; long-press opens menu.
5. Touch: swipe up/down cycles pages; tap upper half of approval screen approves.
6. BLE pairing: desktop Hardware Buddy sees `Claude-XXXX`, 6-digit passkey appears on screen, bonding completes, subsequent connect reconnects instantly.
7. Send a long `msg` from desktop — transcript renders CJK correctly via `chill7_h_cjk`, scroll works.
8. Approval prompt from desktop — attention HUD turns red and pulses on the status bar, BOOT approves, touch lower-half denies.
9. Shake device — dizzy animation plays (IMU wired through QMI8658).
10. Idle 30 s on non-clock page → panel sleeps; BOOT or touch wakes, redraws cleanly.
11. 5 s BOOT hold → device restarts and boots back to bonded state automatically.
12. Factory reset from menu → NVS wiped, first-boot flow runs again.

**Regression gate against S3:** the S3 project is not touched. If the C6 code later needs to consume a buddy bug-fix, manual cherry-pick from the S3 repo.

---

## Implementation Sequence (Preview)

Detailed task breakdown will live in the separate implementation plan. Logical order:

1. Scaffold new project, copy tree, delete the cut list, rewrite `platformio.ini` for C6 single-env. Ensure the S3 entry points compile-check one last time before divergence.
2. Board header + minimal `pins.h`. Stub out all `hw/*` functions so the project links.
3. Display: ST7789 init + vendor sequence, solid-colour fills, verify panel geometry.
4. Canvas push path without status bar (paint middle 172×224 from canvas, leave top 96 black).
5. Touch: vendor AXS5106L driver, verify raw coordinates, add coordinate remapping.
6. Input: single-button state machine + touch→event synthesis.
7. Status bar: independent 172×96 framebuffer, clock + battery + BLE indicator.
8. Power: ADC battery read + auto-off timer.
9. IMU: QMI8658 via SensorLib (reuse S3 code unchanged, verify I²C address).
10. BLE bridge: first-pair end-to-end with desktop.
11. Main loop: full 9-page UI, gesture handling, approval flow.
12. Attention HUD in status bar + pulse.
13. Menu trim (remove audio/character items, add brightness).
14. README rewrite for the new project — controls matrix, pin table, limitations vs S3.

Each step ends with a physical flash + visual check before moving on.
