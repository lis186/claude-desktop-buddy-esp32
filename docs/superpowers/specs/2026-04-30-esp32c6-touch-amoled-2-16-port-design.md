# Port Design: Waveshare ESP32-C6-Touch-AMOLED-2.16

**Date:** 2026-04-30
**Status:** Draft

## Background

The existing codebase targets two Waveshare ESP32-S3 boards via the
`src/boards/` capability-flag system: 1.8 (368×448 rectangular AMOLED, SH8601)
and 1.75C (466×466 round AMOLED, CO5300). Both have 8 MB OPI PSRAM, AXP2101
PMU, ES8311 audio codec, and PCF85063 / no-RTC variants.

This spec covers porting the same buddy experience to the **Waveshare
ESP32-C6-Touch-AMOLED-2.16**. Unlike the C6-LCD-1.47 port (which lives in a
sibling repository), the 2.16 ships with the full S3 peripheral set —
AXP2101, PCF85063, ES8311, QMI8658 — and only differs in MCU class (ESP32-C6,
no PSRAM, single-core 160 MHz). Adding it as a third PlatformIO env in this
project gives the highest reuse of the AXP/audio/RTC/IMU code paths.

## Goals

- Run the existing 18-buddy / 9-page UI on the 2.16 with full feature parity
  to the S3 boards (audio beeps, AXP power management, RTC, IMU gestures,
  approval flow, BLE NUS pairing).
- Express every hardware difference through capability flags in the new board
  header — no `#if BOARD_MCU_*` branches in `hw/*.cpp`.
- Leave 1.8 / 1.75C runtime behavior unchanged after the port lands.

## Out of Scope

- Custom GIF characters (no PSRAM → no decode buffers / character packs).
- Microphone capture via ES7210 (the chip is on the board but unused; can be
  added later as a separate feature).
- Tearing-effect synchronisation via panel TE pin.
- Light-sleep / deep-sleep power optimisation.

---

## Decisions Summary

| # | Decision | Choice |
|---|---|---|
| 1 | Project placement | Third env in this S3 project (vs sibling repo / new repo) |
| 2 | Feature scope | Full parity including audio (ES8311 via legacy I²S, same as S3) |
| 3 | Display geometry | 184×224 canvas → 2× integer upscale → 368×448 centred in 480×480 panel; 56 px L/R black border, 16 px T/B border. No bilinear scaling, no PSRAM-backed full-frame buffer. |
| 4 | Input model | Three independent buttons + capacitive touch. PWR (GPIO18, active-HIGH via MOSFET) = A. IO10 (GPIO10, active-low) = B. BOOT (GPIO9) synthesizes `BTN_A_LONG_PRESS` to enter menu. |
| 5 | Audio | Reuse S3 `audio.cpp` verbatim; gate the PA-enable line on a new `BOARD_HAS_PA_CTRL` flag (NC on 2.16). |
| 6 | NimBLE | Upgrade `^1.4` → `^2.0` for all envs (required for C6); regress 1.8 / 1.75C after the bump. |

---

## Architectural Rule (carried over from existing port system)

**All hardware variation must be expressed in `src/boards/<board>.h` via
capability flags or pin constants.** `hw/*.cpp` reads only those flags and
the `PIN_*` constants; it must not branch on MCU identity, PlatformIO
env name, or any indirect proxy. If a difference cannot be expressed with
existing flags, **add a new flag in the headers** with explicit defaults in
all existing boards before introducing the branch.

This rule was already followed by 1.8 ↔ 1.75C diffs (`BOARD_HAS_TCA9554`,
`BOARD_DISPLAY_CO5300`, `BOARD_DISPLAY_LETTERBOX`, `BOARD_TOUCH_CST92XX`,
`BOARD_BTN_SWAP_AB`). The 2.16 port preserves it.

---

## Hardware Reference

Derived from the official schematic
(`ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf`) and the XiaoZhi v2.2.5 board
definition shipped with the vendor SDK.

### MCU

ESP32-C6FH8 — single-core RISC-V, 160 MHz, ~512 KB HP SRAM (~256 KB usable
after radio + ROM), 8 MB flash, BLE 5.0 LE, WiFi 6, native USB CDC.
**No PSRAM.**

### Pin Mapping

| Signal | Pin | Notes |
|---|---|---|
| `PIN_LCD_SCLK` | GPIO0 | QSPI |
| `PIN_LCD_SDIO0..3` | GPIO1..4 | QSPI data |
| `PIN_LCD_CS` | GPIO15 | |
| LCD reset | (NC) | Driven by AXP ALDO3 power-cycle |
| `PIN_TP_INT` | GPIO5 | CST9217 |
| `PIN_TP_RESET` | GPIO11 | |
| `PIN_I2C_SDA` | GPIO8 | Shared bus: AXP2101, ES8311, ES7210, CST9217, QMI8658, PCF85063 |
| `PIN_I2C_SCL` | GPIO7 | |
| `PIN_I2S_MCLK` | GPIO19 | ES8311 |
| `PIN_I2S_BCLK` | GPIO20 | |
| `PIN_I2S_WS` | GPIO22 | |
| `PIN_I2S_DI` | GPIO21 | (ES7210 mic input — unused) |
| `PIN_I2S_DO` | GPIO23 | |
| `PIN_QMI_INT1` | GPIO16 | IMU interrupts (poll-only for now) |
| `PIN_QMI_INT2` | GPIO17 | |
| `PIN_KEY_PWR` | **GPIO18** | Key2 (PWR silkscreen). Active-HIGH (BSS138 inverts PWRON). Also wired to AXP2101 PWRON for 4s-hold hardware shutdown. |
| `PIN_KEY_IO10` | GPIO10 | Key3 (IO10 silkscreen). Active-low. |
| `PIN_KEY_BOOT` | GPIO9 | Key1 (BOOT silkscreen). Active-low. Also boot-mode strap during power-up. |
| (no PA enable) | — | No discrete amplifier control; codec drives speaker directly |

### Peripherals

| Peripheral | Part | Address | Notes |
|---|---|---|---|
| Display | SH8601 480×480 | — | QSPI, drawn via `Arduino_SH8601` (same library used on 1.8). LCD reset via AXP ALDO3 power cycle, not a GPIO. |
| Touch | CST9217 | 0x5A | I²C; same protocol family as the CST92xx on 1.75C, current `BOARD_TOUCH_CST92XX` driver path applies. |
| PMU | AXP2101 | 0x34 | Same chip and library (`XPowersLib`) as the S3 boards. |
| Codec | ES8311 (out) + ES7210 (mic, unused) | ES8311 default | I²S, same vendored `lib/ES8311/` as S3. |
| RTC | PCF85063 | 0x51 | Same as 1.8. |
| IMU | QMI8658 | 0x6B | Same as 1.75C; `SensorLib` covers it. |

### Three Buttons (empirically verified)

The schematic shows Key1 wired to both GPIO9 and a CHIP_PU pad, but
empirical testing on the physical board confirms pressing Key1 does not
reset the chip — the CHIP_PU connection on Key1 is a non-switching
mechanical pad, not a closing contact. All three buttons read independently
as standard GPIO inputs.

| Silkscreen | Schematic | Net | Software meaning |
|---|---|---|---|
| BOOT | Key1 | GPIO9, active-low | Menu shortcut (synthesises `BTN_A_LONG_PRESS`) |
| PWR | Key2 | GPIO18 (PWRON inverted via BSS138 MOSFET), active-HIGH | Button A. Also AXP PWRON: 4s hardware power-off configured in firmware. |
| IO10 | Key3 | GPIO10, active-low | Button B |

### Critical Differences vs S3

- No PSRAM (rules out the 1.75C bilinear-letterbox path entirely; a
  full-frame `MALLOC_CAP_SPIRAM` allocation would fail). The 2× integer
  upscale path used by 1.8 is reused, with a centring offset added.
- Single-core 160 MHz: expect ~15–20 FPS for the canvas push (vs 25–30 on
  S3). Acceptable for the buddy animation cadence.
- LCD reset not on a GPIO; instead AXP ALDO3 power-cycle in `hwInit()`.
- No discrete PA enable line; codec drives speaker directly.
- BLE stack must be NimBLE 2.x — 1.4 does not support C6.

---

## Architecture

### Project Layout

The 2.16 port adds files to the existing project; nothing moves.

```
claude-desktop-buddy/
├── platformio.ini                                            ← +1 env
├── src/
│   ├── boards/
│   │   ├── board_waveshare_esp32s3_touch_amoled_1_8.h        ← +flag defaults
│   │   ├── board_waveshare_esp32s3_touch_amoled_1_75c.h      ← +flag defaults
│   │   └── board_waveshare_esp32c6_touch_amoled_2_16.h       ← NEW
│   └── hw/
│       ├── pins.h                                            ← +1 #elif
│       ├── display.cpp                                       ← +offset reads
│       ├── input.cpp                                         ← +third button + active-high A read
│       ├── audio.cpp                                         ← gate PA on flag
│       ├── power.cpp                                         ← +ALDO3, +PWRON 4s config
│       └── (rtc.cpp, imu.cpp, expander.cpp, hw.cpp)          ← existing flags suffice
```

`buddies/`, `main.cpp`, `buddy.cpp`, `ble_bridge.cpp`, `data.h`, `stats.h`,
`character.cpp` (S3 only), and `xfer.h` are unchanged. The S3 envs continue
to compile in the GIF character path; the 2.16 env disables it via existing
`BOARD_HAS_PSRAM` runtime check sites where character buffers are allocated.

### `platformio.ini`

```ini
[env]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/53.03.13/platform-espressif32.zip
framework = arduino
monitor_speed = 115200
upload_speed = 921600

board_build.flash_mode = qio
board_upload.flash_size = 8MB
board_build.partitions = no_ota_8mb.csv
board_build.filesystem = littlefs

build_src_filter = +<*> +<buddies/> +<hw/> -<main.cpp.m5stick.bak> -<xfer.h>

lib_deps =
    moononournation/GFX Library for Arduino
    lewisxhe/XPowersLib
    lewisxhe/SensorLib
    adafruit/Adafruit BusIO
    bitbank2/AnimatedGIF @ ^2.1.1
    bblanchon/ArduinoJson @ ^7.0.0
    h2zero/NimBLE-Arduino @ ^2.0      ; bumped from ^1.4 for C6 support
    olikraus/U8g2 @ ^2.35

[env:waveshare-esp32s3-touch-amoled-1-8]
board = esp32-s3-devkitc-1
board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
board_build.psram_type = opi
board_build.arduino.memory_type = qio_opi
build_flags =
    -DCORE_DEBUG_LEVEL=0
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DXPOWERS_CHIP_AXP2101
    -DU8G2_USE_LARGE_FONTS
    -DU8G2_FONT_SUPPORT
    -DBOARD_WAVESHARE_ESP32S3_TOUCH_AMOLED_1_8

[env:waveshare-esp32s3-touch-amoled-1-75c]
board = esp32-s3-devkitc-1
board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
board_build.psram_type = opi
board_build.arduino.memory_type = qio_opi
build_flags = (same set as 1.8 with -DBOARD_WAVESHARE_ESP32S3_TOUCH_AMOLED_1_75C)

[env:waveshare-esp32c6-touch-amoled-2-16]
board = esp32-c6-devkitc-1
board_build.mcu = esp32c6
board_build.f_cpu = 160000000L
; no psram_type, no arduino.memory_type
build_flags =
    -DCORE_DEBUG_LEVEL=0
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DXPOWERS_CHIP_AXP2101
    -DU8G2_USE_LARGE_FONTS
    -DU8G2_FONT_SUPPORT
    -DBOARD_WAVESHARE_ESP32C6_TOUCH_AMOLED_2_16
```

The `BOARD_HAS_PSRAM` compiler macro stays in S3 envs only (PIO needs it for
linker decisions). The runtime header flag of the same name is for `.cpp`
consumers and is set independently per board header.

---

## Capability Flags

### Existing flags — 2.16 values

| Flag | 1.8 | 1.75C | **2.16** |
|---|---|---|---|
| `BOARD_HAS_TCA9554` | 1 | 0 | **0** |
| `BOARD_HAS_PCF85063` | 1 | 0 | **1** |
| `BOARD_DISPLAY_CO5300` | 0 | 1 | **0** |
| `BOARD_DISPLAY_LETTERBOX` | 0 | 1 | **0** |
| `BOARD_TOUCH_CST92XX` | 0 | 1 | **1** |
| `BOARD_BTN_SWAP_AB` | 0 | 1 | **1** |

### New flags introduced by this port (with 1.8 / 1.75C defaults)

| Flag | 1.8 | 1.75C | 2.16 | Purpose |
|---|---|---|---|---|
| `BOARD_HAS_PSRAM` | 1 | 1 | **0** | Header-side flag for `.cpp` (separate from PIO macro). Letterbox full-frame buf, JSON pool size, character GIF gating all read this. |
| `BOARD_DISPLAY_OFFSET_X` | 0 | 0 | **56** | Centring offset in non-letterbox push. |
| `BOARD_DISPLAY_OFFSET_Y` | 0 | 0 | **16** | Same, vertical. |
| `BOARD_HAS_PA_CTRL` | 1 | 1 | **0** | Whether `PIN_PA_CTRL` is a real driving pin (vs NC). |
| `BOARD_HAS_AXP2101` | 1 | 1 | **1** | Captures the (now universal) AXP presence; defaults preserve existing behavior even though all current boards have it. |
| `BOARD_LCD_RST_VIA_PMU` | 0 | 0 | **1** | When 1, `hwInit()` resets the panel by power-cycling AXP ALDO3 instead of toggling `PIN_LCD_RESET`. 2.16 has no LCD_RST GPIO; this is its reset path. |
| `BOARD_AXP_PWRON_4S_OFF` | 0 | 0 | **1** | When 1, `powerInit()` writes AXP regs 0x22/0x27 to enable 4 s PWRON-hold hardware power-off. Default 0 keeps AXP defaults intact on 1.8 / 1.75C. |
| `BOARD_BTN_THIRD` | 0 | 0 | **1** | Whether a third dedicated key (BOOT) feeds menu shortcut. |
| `BOARD_KEY1_ACTIVE_HIGH` | 0 | 0 | **1** | Active level of `PIN_KEY1` (the primary GPIO button). 2.16 reads PWR through the BSS138 inverter and the GPIO is HIGH when pressed. |
| `BOARD_HAS_KEY2` | 0 | 0 | **1** | When 1, `s_b` is populated by a second GPIO key (`PIN_KEY2`, active-low). When 0, `s_b` continues to come from AXP PEK via `scanAxp()` (existing 1.8 / 1.75C path). Avoids double-firing on 2.16 where PWR is wired to both GPIO18 and AXP PWRON. |

---

## Board Header

`src/boards/board_waveshare_esp32c6_touch_amoled_2_16.h`:

```c
#pragma once

// Display: SH8601 480×480 rounded-square AMOLED, 2.16" diagonal.
// 184×224 canvas → 2× integer upscale → 368×448, centred at (56, 16);
// rounded corners fall in the 56 px black border, never clipping content.
#define LCD_W_PHYS              480
#define LCD_H_PHYS              480
#define BOARD_HW_W              184
#define BOARD_HW_H              224
#define BOARD_SAFE_INSET        8
#define BOARD_DISPLAY_OFFSET_X  56
#define BOARD_DISPLAY_OFFSET_Y  16

// QSPI to SH8601
#define PIN_LCD_SDIO0  1
#define PIN_LCD_SDIO1  2
#define PIN_LCD_SDIO2  3
#define PIN_LCD_SDIO3  4
#define PIN_LCD_SCLK   0
#define PIN_LCD_CS     15
// LCD RST not on a GPIO — driven by AXP ALDO3 power-cycle in hwInit().

// I2C bus (shared)
#define PIN_I2C_SDA   8
#define PIN_I2C_SCL   7

// Touch (CST9217 family, same protocol as 1.75C)
#define PIN_TP_INT    5
#define PIN_TP_RESET  11

// I2S to ES8311
#define PIN_I2S_MCLK  19
#define PIN_I2S_BCLK  20
#define PIN_I2S_WS    22
#define PIN_I2S_DI    21
#define PIN_I2S_DO    23
#define PIN_PA_CTRL   -1   // placeholder; gated by BOARD_HAS_PA_CTRL

// IMU (poll-only; INT pins reserved but not wired to handlers)
#define PIN_QMI_INT1  16
#define PIN_QMI_INT2  17

// Three physical keys
// Naming follows existing input.cpp convention: KEY1 → s_a, KEY2 → s_b.
#define PIN_KEY1      18   // PWR silkscreen. Active-HIGH via BSS138 inverter. Also AXP PWRON.
#define PIN_KEY2      10   // IO10 silkscreen. Active-low.
#define PIN_KEY_BOOT  9    // BOOT silkscreen. Active-low. Synthesises BTN_A_LONG_PRESS.

// Capability flags
#define BOARD_HAS_PSRAM            0
#define BOARD_HAS_TCA9554          0
#define BOARD_HAS_PCF85063         1
#define BOARD_HAS_PA_CTRL          0
#define BOARD_HAS_AXP2101          1
#define BOARD_LCD_RST_VIA_PMU      1
#define BOARD_AXP_PWRON_4S_OFF     1
#define BOARD_DISPLAY_CO5300       0
#define BOARD_DISPLAY_LETTERBOX    0
#define BOARD_TOUCH_CST92XX        1
#define BOARD_BTN_SWAP_AB          0
#define BOARD_BTN_THIRD            1
#define BOARD_KEY1_ACTIVE_HIGH     1
#define BOARD_HAS_KEY2             1

// Credits page
#define BOARD_MODEL_LINE1  "Waveshare ESP32-C6"
#define BOARD_MODEL_LINE2  "Touch AMOLED 2.16"
```

`hw/pins.h` gains one branch:

```c
#elif defined(BOARD_WAVESHARE_ESP32C6_TOUCH_AMOLED_2_16)
  #include "../boards/board_waveshare_esp32c6_touch_amoled_2_16.h"
```

The 1.8 and 1.75C headers gain default values for every new flag so their
runtime behavior is unchanged.

---

## Display Pipeline

### Init

1. `hwInit()` resets the panel before any QSPI traffic. Path is gated by
   `BOARD_LCD_RST_VIA_PMU`:
   - `=1` (2.16): power-cycle AXP ALDO3 — `enable → 50 ms → disable →
     50 ms → enable`. No LCD_RST GPIO exists.
   - `=0` (1.8 / 1.75C, default): existing path via
     `hwExpanderResetSequence()` toggling `PIN_LCD_RESET` — unchanged.
2. `Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCLK, SDIO0..3)` — same as 1.8.
3. `Arduino_SH8601(s_bus, GFX_NOT_DEFINED, 0, 480, 480)` — same library as
   1.8, just different physical size. The library's internal init sequence
   targets generic SH8601 panels; if the 2.16 panel revision needs a custom
   gamma/window, fall back to `esp_lcd_sh8601` IDF component with the LVGL
   demo's 0..479 init table (see Risks).
4. Canvas allocation `Arduino_Canvas(184, 224, s_gfx)` — 82 KB on heap, fits
   in C6 SRAM.

### Push

`hwDisplayPush()` non-letterbox branch (existing 1.8 path) is amended to
read `BOARD_DISPLAY_OFFSET_X / Y`:

```c
for (int y = 0; y < HW_H; y++) {
  // ...build s_lineBuf via per-pixel 2× expansion (368 px wide)...
  int dy = y * 2 + BOARD_DISPLAY_OFFSET_Y;
  s_gfx->draw16bitRGBBitmap(BOARD_DISPLAY_OFFSET_X, dy,     s_lineBuf, HW_W*2, 1);
  s_gfx->draw16bitRGBBitmap(BOARD_DISPLAY_OFFSET_X, dy + 1, s_lineBuf, HW_W*2, 1);
}
```

`s_lineBuf` stays sized at `LCD_W_PHYS` (480 entries on 2.16, 368 on 1.8) —
internal RAM, ~960 bytes, negligible.

Black border is painted once on first frame and on every wake from sleep
via `s_gfx->fillRect()`; subsequent frames don't repaint it.

`borderAlert` (red attention pill at the top of the panel) draws directly
through `s_gfx->fillRoundRect()` and is independent of the canvas push,
so no PSRAM dependency.

### Sleep / wake

`hwDisplaySleep(true)` → `setBrightness(0)` + `displayOff()`. Wake reverses
and re-paints the black border + canvas on the first frame.

---

## Input

### Buttons

`hw/input.cpp::scanKey1()` reads `PIN_KEY1` with active level controlled by
`BOARD_KEY1_ACTIVE_HIGH`:

```c
static void scanKey1() {
#if BOARD_KEY1_ACTIVE_HIGH
  bool pressed = digitalRead(PIN_KEY1) == HIGH;
#else
  bool pressed = digitalRead(PIN_KEY1) == LOW;
#endif
  // ...existing edge-detect logic, populates s_a...
}
```

A new `scanKey2()`, gated by `BOARD_HAS_KEY2`, populates `s_b` from a second
GPIO key (active-low):

```c
#if BOARD_HAS_KEY2
static void scanKey2() {
  bool pressed = digitalRead(PIN_KEY2) == LOW;
  // ...same edge-detect, populates s_b...
}
#endif
```

`scanAxp()` (existing) is gated `#if !BOARD_HAS_KEY2` so on 2.16 the AXP PEK
events do not also fire `s_b` (avoiding double events from a single PWR
press, since PWR is wired to both GPIO18 and AXP PWRON).

`BOARD_BTN_SWAP_AB` stays at the `hwBtnA()` / `hwBtnB()` accessors
unchanged. On 2.16 the scanning already deposits the right values in `s_a`
/ `s_b`, so `BOARD_BTN_SWAP_AB=0`.

The third key is gated:

```c
#if BOARD_BTN_THIRD
static void scanBootKey() {
  static uint32_t pressedAt = 0;
  bool pressed = digitalRead(PIN_KEY_BOOT) == LOW;
  if (pressed && !pressedAt) pressedAt = millis();
  else if (!pressed && pressedAt) {
    uint32_t held = millis() - pressedAt;
    if (held > 30 && held < 1000) emit(BTN_A_LONG_PRESS);
    pressedAt = 0;
  }
}
#endif
```

`main.cpp`'s event handler tree is untouched — short-press BOOT just looks
like a long-press A from main's perspective.

### Touch

Existing `BOARD_TOUCH_CST92XX` driver path (SensorLib + CST92xx protocol)
covers the CST9217. Touch coordinates arrive in 0..479 / 0..479 screen
space; the canvas-coord remap subtracts `BOARD_DISPLAY_OFFSET_X / Y` (8
lines change in `input.cpp`'s touch handler — same flag the display reads).
Taps in the black border are no-ops.

Gestures (swipe up/down/left/right, taps) and approval upper-half / lower-half
mapping are unchanged from 1.75C.

### AXP 4-second power-off

Configured once in `powerInit()` by writing AXP registers; no runtime poll.
Pressing PWR for 4 s makes AXP cut power directly — no firmware path.

---

## Audio

`hw/audio.cpp` is reused with one change: the PA enable lines are gated:

```c
#if BOARD_HAS_PA_CTRL
  pinMode(PIN_PA_CTRL, OUTPUT);
  digitalWrite(PIN_PA_CTRL, HIGH);
#endif
```

I²S setup, ES8311 initialisation, and the `beepTask` queue are unchanged.
`PIN_I2S_*` come from the board header (different pins on 2.16 vs S3, but
the abstraction has always been correct).

ES7210 (microphone) is on the I²C bus but is not initialised. If voice
input is added later, ES7210 init lives behind a future
`BOARD_HAS_MIC_CODEC` flag.

---

## Power

`hw/power.cpp` `powerInit()` adds two AXP register writes inside the
existing `BOARD_HAS_AXP2101` block (universal flag, `1` on all boards):

```c
#if BOARD_HAS_AXP2101
  axp.enableALDO3();                  // panel power (idempotent)
#if BOARD_AXP_PWRON_4S_OFF
  axp.writeRegister(0x22, 0b110);     // PWRON > OFFLEVEL = power-off source
  axp.writeRegister(0x27, 0x10);      // 4s hold-to-off
#endif
#endif
```

The ALDO3 power-cycle for panel reset (only on `BOARD_LCD_RST_VIA_PMU=1`)
lives in `hwInit()`, not here. The plain `enableALDO3()` here just ensures
the rail is up at boot — idempotent on all three boards (ALDO3 is wired to
the panel rail on each).

The PWRON 4 s hold-to-off configuration is gated by
`BOARD_AXP_PWRON_4S_OFF`. 1.8 and 1.75C keep AXP defaults; 2.16 enables it
because the PWR key is the only software-configurable shutdown path on that
board.

Battery voltage (`getBattVoltage`), charge state (`isCharging`), and PEK
short-press / long-press detection all remain on the existing XPowersLib
path. Auto-off timers (5 min on clock, 30 s on other screens) are
unchanged.

---

## RTC + IMU

- **RTC**: `BOARD_HAS_PCF85063 = 1`. Existing `hw/rtc.cpp` path applies
  verbatim.
- **IMU**: QMI8658 at 0x6B via SensorLib — same code as 1.75C. INT pins
  routed but polling is sufficient for the dizzy / tilt gesture cadence.

---

## NimBLE 2.x Upgrade

Bumping `h2zero/NimBLE-Arduino @ ^1.4` → `^2.0` is required for C6 support
and lands as the first commit of the implementation, before any 2.16 code,
so 1.8 / 1.75C regressions are caught early.

API touch points in `ble_bridge.cpp`:

- `NimBLEDevice::init()` / `createServer()` / `createService()` /
  `createCharacteristic()` — signatures unchanged.
- `NimBLEServerCallbacks::onPassKeyRequest` → renamed
  `onPassKeyDisplay` in 2.x — small mechanical change.
- `NimBLEAdvertising::start()` / split-packet adv — 2.x adds opt-in
  controls; the C6 LCD sibling repo has already exercised the same upgrade
  (`claude-desktop-buddy-lcd` commit `5899ca7`) and that diff is the
  reference.

Regression after the bump:

1. 1.8 board boots, advertises, pairs with passkey, sends/receives a long
   `msg`, retains bonding across reboot.
2. 1.75C board: same.

---

## Memory Budget

ESP32-C6FH8 has roughly 256 KB usable HP SRAM after radio + IDF baseline.

| Allocation | Size | Placement |
|---|---|---|
| Arduino + FreeRTOS + WiFi/BT baseline | ~80 KB | static/heap |
| NimBLE 2.x + GATT + bonding | ~50 KB | heap |
| **Canvas 184×224×2 (RGB565)** | **82 KB** | heap |
| ArduinoJson document pool (8 KB, down from 16 KB on S3) | 8 KB | static |
| Transcript ring + buddy temps | < 4 KB | heap/stack |
| Main + BLE FreeRTOS task stacks | ~16 KB | static |
| **Total** | **~240 KB** | |

Headroom: ~16 KB. Tight. Three knobs if OOM appears at runtime:

1. ArduinoJson pool down to 4 KB (caps single-message size at ~2 KB; check
   wire-protocol peaks).
2. Drop unused `lib/Adafruit_XCA9554` and `lib/Arduino_DriveBus` from the
   2.16 build via per-env `lib_ignore`.
3. Last resort: switch canvas to grayscale (1 byte / pixel → 41 KB) and
   re-introduce colour at the buddy renderer — touches `buddies/`, larger
   change, defer.

---

## Risks

| # | Risk | Mitigation |
|---|---|---|
| 1 | NimBLE 2.x bump regresses 1.8 / 1.75C BLE | Land bump first, regress both S3 boards before 2.16 work begins. Passkey callback rename is the main grep target. |
| 2 | `Arduino_SH8601` library init fails on 480×480 (panel revision differences) | Plan B: switch to `esp_lcd_sh8601` IDF component with vendor cmd table from LVGL demo (col 0..479, row 0..479). The vendor demo files exist under `~/Downloads/ESP32-C6-Touch-AMOLED-2.16/02_Example/Arduino-v3.3.3/08_LVGL_V8_Test/`. |
| 3 | Memory budget too tight at runtime | JSON pool shrink → grayscale canvas; both staged behind feature flags. |
| 4 | C6 single-core 160 MHz frame rate too low | Accept ~15–20 FPS; if attention pulse drops below 4 Hz visibly, optimise the per-row 2× expansion (currently dominant cost is the SPI write, not CPU). |
| 5 | LVGL demo and XiaoZhi differ on SH8601 init (full 480 vs offset 22..431 window) | Use LVGL demo's full 480 init. Empirical verify on first boot — wrong window manifests as obvious image shift / clipping. |
| 6 | GPIO9 boot strap held low by 100 nF cap during cold boot prevents normal start | Schematic shows 10 K external pull-up (R8); confirmed boots normally. Holding BOOT through USB plug-in still enters download mode (a feature). |
| 7 | `MALLOC_CAP_SPIRAM` allocation in letterbox path executes on 2.16 if `BOARD_DISPLAY_LETTERBOX=1` is mistakenly set | Header has `BOARD_DISPLAY_LETTERBOX 0` and the alloc site is `#if`-gated. Misconfiguration would surface at runtime as the heap_caps_malloc returning NULL and the early-return in display.cpp hiding the panel; trivial to spot on first flash. |

---

## Testing Approach

Hardware port — no unit tests. Validation is empirical on the physical board.

**Build gates per commit:**

- `pio run -e waveshare-esp32c6-touch-amoled-2-16` clean compile.
- `pio run -e waveshare-esp32s3-touch-amoled-1-8` and `-1-75c` continue to
  compile cleanly after every commit that touches shared `.cpp`.

**Smoke tests on 2.16 after each implementation step:**

1. Panel lights up, full-screen colour fills render correctly (validates
   QSPI, SH8601 init, ALDO3 power-cycle).
2. Canvas blit lands at (56, 16) with clean 56 px L/R and 16 px T/B
   borders. No tearing or wrap.
3. Each of the three keys reads independently:
   - PWR short → A event
   - IO10 short → B event
   - BOOT short → menu opens
   - PWR 4 s → AXP cuts power, device off until next press
4. Touch swipe up/down cycles displayMode; tap upper/lower halves on
   approval prompt approves/denies.
5. BLE pairing: passkey on screen, desktop Hardware Buddy connects, bond
   persists across reboot.
6. Long `msg` from desktop renders CJK correctly via `chill7_h_cjk`.
7. Approval HUD pulses red, BOOT / touch / button respond.
8. Shake → dizzy animation (IMU polling).
9. RTC time displays and ticks.
10. Restart from menu reboots cleanly; first-boot flow runs again after
    NVS factory reset.

**Regression on S3:**

After NimBLE 2.x lands, on each of 1.8 and 1.75C: pair, bond persistence,
one long `msg` send, one approval. Three checks each.

---

## Implementation Sequence

The plan splits into three phases. Each ends with a flash + visual check
on the relevant boards before proceeding.

### Phase 0 — NimBLE bump (touches existing S3 envs)

1. Bump `lib_deps` to `h2zero/NimBLE-Arduino @ ^2.0`.
2. Adapt `ble_bridge.cpp` for renamed callbacks (`onPassKeyDisplay`) and
   any other 2.x API changes encountered.
3. Build 1.8 and 1.75C envs clean.
4. Flash 1.8: pair, send long message, bond persistence across reboot ✓
5. Flash 1.75C: same three checks ✓

### Phase 1 — Capability flag plumbing (touches existing boards' headers)

6. Add new flag defaults to 1.8 and 1.75C headers:
   `BOARD_HAS_PSRAM=1`,
   `BOARD_DISPLAY_OFFSET_X=0`,
   `BOARD_DISPLAY_OFFSET_Y=0`,
   `BOARD_HAS_PA_CTRL=1`,
   `BOARD_HAS_AXP2101=1`,
   `BOARD_LCD_RST_VIA_PMU=0`,
   `BOARD_AXP_PWRON_4S_OFF=0`,
   `BOARD_BTN_THIRD=0`,
   `BOARD_KEY1_ACTIVE_HIGH=0`,
   `BOARD_HAS_KEY2=0`.
7. Edit shared `.cpp` files to read the new flags (no behavior change on
   1.8 / 1.75C because their flags default to the existing values):
   - `hw/display.cpp` non-letterbox branch: add `OFFSET_X/Y` to blit
     coordinates.
   - `hw/input.cpp`: subtract `OFFSET_X/Y` in touch→canvas remap; gate
     `scanKey1()` polarity on `BOARD_KEY1_ACTIVE_HIGH`; add `scanKey2()`
     gated by `BOARD_HAS_KEY2` (and gate the existing `scanAxp()` with
     `#if !BOARD_HAS_KEY2` to keep `s_b` from double-firing); add
     `scanBootKey()` gated by `BOARD_BTN_THIRD`.
   - `hw/audio.cpp`: gate PA enable on `BOARD_HAS_PA_CTRL`.
   - `hw/hw.cpp`: gate LCD reset path on `BOARD_LCD_RST_VIA_PMU` (PMU
     ALDO3 power-cycle vs existing `hwExpanderResetSequence()`).
   - `hw/power.cpp::powerInit()`: ensure `enableALDO3()` (idempotent on all
     boards); gate the PWRON 4 s register writes on
     `BOARD_AXP_PWRON_4S_OFF`.
8. Build 1.8 and 1.75C, flash both, regression smoke test (canvas position,
   audio beep, AXP power button, BLE pair).

### Phase 2 — 2.16 board

9. Add `src/boards/board_waveshare_esp32c6_touch_amoled_2_16.h`.
10. Add `[env:waveshare-esp32c6-touch-amoled-2-16]` block in `platformio.ini`.
11. Add `#elif` branch in `hw/pins.h`.
12. Build 2.16 env clean.
13. Flash 2.16. Smoke test step (1) — panel lit, full-screen colour fills.
14. Smoke test (2) — canvas at (56, 16) without scrolling.
15. Smoke test (3) — three keys each emit distinct events; PWR 4 s power off.
16. Smoke test (4) — touch + gestures.
17. Smoke test (5)–(7) — BLE, approval, attention HUD.
18. Smoke test (8)–(10) — IMU, RTC, restart.
19. README: add 2.16 to the supported-boards table, controls matrix, pin
    table, NimBLE-2.x bump note.
