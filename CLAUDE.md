# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

学而思小喵掌机 Claude Buddy — firmware that turns a XUEERSI "XiaoMiao" educational programming handheld (ESP32-D0WD WROVER, Quad PSRAM, 4MB Flash, ST7735S 160×128 LCD, 6 buttons) into a Claude AI chatbot companion with screen casting, toolbox utilities, and WiFi configuration via SoftAP captive portal.

## Build & Flash

ESP-IDF v5.4 is required, installed at `C:\esp\esp-idf-v5.4`. Use **native cmd.exe** (not git-bash).

```bat
C:\esp\esp-idf-v5.4\export.bat
cd /d E:\vibecoding\22222\xiaomiao_buddy
idf.py set-target esp32
idf.py build
python tools\assemble_image.py
python tools\flash_all.py
```

Flash a pre-built image: double-click `一键刷机.bat` (auto-detects CP210x/CH340 COM port, falls back to COM8), or `一键刷机.bat COM5` for a specific port.

The flash flow: `assemble_image.py` stitches bootloader (0x1000) + partition table (0x8000) + app binary (0x10000) into a 4MB unified image (gaps filled with 0xFF), then `flash_all.py` writes it at 0x0 via esptool (460800 baud).

### Encoding strategy

- `一键刷机.bat`: Pure ASCII content — immune to GBK/UTF-8 encoding conflicts that cause the window to crash on open.
- `flash_all.py` / `assemble_image.py`: Auto-detect terminal UTF-8 support at startup via `chcp 65001`. If UTF-8 works → Chinese output (box-drawing chars + emoji). If it fails → ASCII English fallback. No garbled output on any Windows terminal.
- All user-facing strings use `_t(chinese, english)` for bilingual support.

## Architecture

**4 layers, all event-driven by LVGL:**

```
Layer 1: board.c/h       — ST7735S LCD (SPI2/VSPI 40MHz), 6-btn GPIO edge detection, LVGL v9 init, mutex
Layer 2: Services        — config (NVS), claude_api (WiFi+HTTPS), web_portal (SoftAP HTTP), sdcard (FAT32)
Layer 3: Screens         — app_menu, app_claude, app_settings, app_tools, app_cast (each is an LVGL screen)
Layer 4: main.c          — init NVS→config→board, then show menu (or settings if B held at boot)
```

**Screen transitions:** Each app_* module creates a full LVGL screen via `ui_make_screen()`, populates it, then calls `ui_load_screen()` which replaces the active screen and frees the old one. Navigation back to the menu is done by setting a B-button callback with `ui_add_back()`.

**Claude API flow:** User picks a preset question in the LVGL list → `app_claude` spawns a **background FreeRTOS task** calling `claude_ask()` (blocking HTTPS POST to Anthropic Messages API) → response JSON parsed for `content[0].text` → result displayed in an LVGL textarea. API calls MUST NOT run on the LVGL task — they block for seconds.

**Configuration:** Priority order: NVS "cfg" partition (saved via SoftAP portal) → `secrets.h` compile-time defaults. `secrets.h` is gitignored; `secrets.h.example` is the template. The "cfg" partition lives at flash offset 0x3D0000, separate from the default "nvs" partition used for WiFi PHY calibration.

**LVGL access:** All LVGL object manipulation must be wrapped in `board_lock(portMAX_DELAY)` / `board_unlock()`. Background tasks that need to update the UI should take the lock only briefly to set values on already-created objects.

## Hardware Pin Map

| Signal   | GPIO | Notes                          |
|----------|------|--------------------------------|
| LCD MOSI | 23   | VSPI, 40MHz                    |
| LCD SCLK | 18   |                                |
| LCD DC   | 4    |                                |
| LCD CS   | 5    |                                |
| BTN UP   | 2    | Active-low, edge detection     |
| BTN DOWN | 13   |                                |
| BTN LEFT | 27   |                                |
| BTN RIGHT| 35   | Input-only (no pull-down avail)|
| BTN A    | 34   | Input-only, confirm action     |
| BTN B    | 12   | Back / ESC (maps to LVGL key Esc) |
| SD CS    | 22   | Shares SPI bus with LCD        |
| SD MISO  | 19   |                                |
| Buzzer   | 14   | PWM                            |

## Chinese Font

The font is in `main/font_cn_16.c` (~2MB), generated from SimHei via `lv_font_conv` (npm, v1.5.3, see `tools/package.json`). It covers GB2312. To regenerate: run `tools/gen_symbols.py` to produce the symbol list, then `lv_font_conv` with the SimHei TTF font. Missing characters show as empty glyphs — the font does not cover rare/historical characters.

## Key Constraints

- **WiFi 2.4GHz only** — ESP32 cannot connect to 5GHz networks.
- **API endpoint** must be reachable from the device; domestic Chinese users typically need a relay/proxy since `api.anthropic.com` may be blocked.
- **Screen casting and SoftAP portal** both bind port 80 — they cannot run simultaneously.
- **4MB flash** with a single factory app partition (no OTA). The cfg partition at 0x3D0000 stores user config separately.
- **No automated tests** — testing is done manually on the physical device.
- `board_lock()`/`board_unlock()` is NOT reentrant — do not nest lock calls on the same task.
