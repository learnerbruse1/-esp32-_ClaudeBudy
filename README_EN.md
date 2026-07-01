# XUEERSI XiaoMiao Handheld · Claude Buddy

[![zh-CN](https://img.shields.io/badge/lang-简体中文-red.svg)](README.md)
[![en](https://img.shields.io/badge/lang-English-blue.svg)](README_EN.md)

Transform a XUEERSI "XiaoMiao" educational programming handheld (ESP32) into a **menu-driven** Claude AI assistant.

```
┌──────────────────────────┐
│       XiaoMiao Device     │
│  ▸ Claude Buddy           │    Ask Claude over WiFi (preset/selectable prompts)
│  ▸ Screen Cast            │    Mirror the LCD to a phone/PC browser (WiFi)
│  ▸ Toolbox                │    Flashlight / Stopwatch
│  ▸ Settings               │    WiFi config / Device info / Format SD / Reset
└──────────────────────────┘
```

> Note: Due to ESP32 resource constraints, "Claude Buddy" is a **networked chat client** (a pocket Claude),
> not the Claude Code CLI that runs on a computer.

---

## 1. Features

| Feature | Description |
|---|---|
| **Claude Buddy** | Connect WiFi → HTTPS call to Anthropic Messages API → display reply on screen. Preset question prompts, scrollable replies. Endpoint (supports self-hosted relay), API key, and model are all configurable. |
| **Screen Cast** | Uses `lv_snapshot` to capture the current screen → BMP → built-in HTTP server. Browser refreshes at ~2fps. |
| **Toolbox** | Flashlight (full white screen for illumination), Stopwatch (A to start/stop, Left/Right to reset). |
| **Settings** | WiFi setup (SoftAP captive portal), Device info (scrollable highlighted list), Format SD card (double-confirm), Reset to defaults. |

---

## 2. Hardware Specs (Measured)

| Item | Details |
|---|---|
| MCU | ESP32-D0WD (WROVER, dual-core 240MHz, **Quad PSRAM**, 4MB Flash) |
| Display | ST7735S 128×160 → rotated to **160×128** landscape (SPI2 40MHz: MOSI23/SCLK18/DC4/CS5) |
| Buttons | 6 buttons, active-low: Up=2 Down=13 Left=27 Right=35 A=34 B=12 |
| SD Card | Shared SPI bus, CS=22, MISO19 |
| Buzzer | GPIO14 (PWM) |
| USB Bridge | GD32 USB-CDC bridge, default **COM8**, supports auto-reset |

---

## 3. System Architecture

**4MB Flash Partition** (single app, clean layout):

```
0x001000  bootloader
0x008000  partition table
0x009000  nvs / phy_init
0x010000  factory      Our Claude+Cast+Toolbox+Settings app (~3.75MB)
0x3D0000  cfg          Buddy user config (separate nvs partition)
```

Boots directly into the main menu — no OTA switching needed.

---

## 4. Directory Structure

```
xiaomiao_buddy/
├─ 一键刷机.bat                   Double-click to flash
├─ CMakeLists.txt  partitions.csv  sdkconfig.defaults
├─ flash_unified.bin              Pre-assembled 4MB image
├─ main/
│  ├─ main.c                Entry point
│  ├─ board.c/.h            ST7735 display + 6 buttons (A=confirm, B=back, edge detection) + LVGL
│  ├─ ui_common.c/.h        Screen creation/switching/back-button utilities
│  ├─ app_menu.c/.h         Main menu
│  ├─ app_claude.c/.h       Claude interface (presets/replies)
│  ├─ app_settings.c/.h     Settings (WiFi/device info/format/reset)
│  ├─ app_cast.c/.h         WiFi screen casting
│  ├─ app_tools.c/.h        Toolbox (flashlight/stopwatch)
│  ├─ claude_api.c/.h       WiFi + Anthropic API (HTTPS)
│  ├─ config.c/.h           Config (NVS cfg partition)
│  ├─ web_portal.c/.h       SoftAP WiFi configuration portal
│  ├─ sdcard.c/.h           SD card format (FAT32) / capacity read
│  ├─ secrets.h             Compile-time default credentials (can be left empty)
│  └─ font_cn_16.c          Chinese font (SimHei → lv_font_conv)
└─ tools/
   ├─ flash_all.py          One-click flash tool
   ├─ assemble_image.py     Assemble unified firmware image
   └─ gen_symbols.py        Generate font character set
```

---

## 5. Quick Start (with pre-built flash_unified.bin)

Connect the device to your computer via USB, then **double-click `一键刷机.bat`** (defaults to COM8; for other ports: `python tools\flash_all.py COM5`).
Takes about 1 minute — the device reboots into the main menu automatically.

---

## 6. Build from Source

ESP-IDF **v5.4** must be installed at `C:\esp\esp-idf-v5.4`. **Use native cmd.exe (not git-bash)**:

```bat
C:\esp\esp-idf-v5.4\export.bat
cd /d E:\vibecoding\22222\xiaomiao_buddy
idf.py set-target esp32
idf.py build
python tools\assemble_image.py
python tools\flash_all.py
```

---

## 7. First-Time WiFi Setup

Main menu → **Settings → WiFi Setup** (or when selecting Claude Buddy without config, it will prompt you):

1. The screen shows hotspot name `ClaudeBuddy-XXXX` — connect your phone to it (open, no password).
2. Open a browser to `http://192.168.4.1`.
3. Fill in: your home **2.4G WiFi** credentials, API endpoint (`https://api.anthropic.com` or your relay), API key, model → Save. Device reboots automatically.

---

## 8. Button Map

| Context | Up/Down | Left/Right | A | B |
|---|---|---|---|---|
| Menu/List | Move highlight | — | Confirm | Back |
| View Reply/Info | Scroll/Navigate | — | — | Back |
| Stopwatch | — | Reset | Start/Stop | Back |

---

## 9. Screen Cast (WiFi screen mirroring)

Main menu → **Screen Cast** → connects WiFi → the screen shows a URL like `http://<device-IP>/`. Open it in any browser on your phone or computer to see the device screen (~2fps). Normal device operation continues while mirroring. **Press B to return** (cast continues in the background).

---

## 10. Scripts

| Script | Purpose |
|---|---|
| `一键刷机.bat` → `tools/flash_all.py` | Assemble (if needed) + flash firmware to COM port |
| `tools/assemble_image.py` | Stitch build artifacts into a 4MB unified image |
| `tools/gen_symbols.py` | Generate GB2312 character set (for font regeneration) |

---

## 11. Restore Original Firmware / Backup

- Factory firmware backup for this device: `..\学而思小喵编程掌机\_research\backup_current_4MB.bin`
- Manufacturer original image: `..\学而思小喵编程掌机\firmware.bin`

```bat
python -m esptool --chip esp32 -p COM8 write-flash 0x0 backup_current_4MB.bin
```

---

## 12. Troubleshooting

- **Can't connect to COM8**: Try a different port (`python tools\flash_all.py COM5`). The bridge supports auto-reset — no button press needed in most cases.
- **Won't enter main menu**: Power off and on again (cold boot always enters the menu first).
- **SD Card Error / mount failed**: Card is not FAT32 → Settings → Format SD Card.
- **Network failure**: ESP32 only supports **2.4GHz** WiFi. Relay endpoints must support the `/v1/messages` path.
- **Missing characters in replies**: The font only covers GB2312 — rare or historical characters may not render.
- **Screen cast won't open**: Make sure the device is connected to WiFi and not simultaneously running the WiFi setup portal (both use port 80).

---

## 13. Known Limitations

- Screen casting only mirrors the local device display.
- Claude API endpoint must be reachable from the device; users in mainland China typically need an accessible relay/proxy.

---

## 14. Credits / License

- Board-level references: **xuyuejia/XUEERSI_ESP32_Board**, **pysn2012/xueersi-xiaomiao**.
- Font: SimHei (system font, used locally for bitmap generation only; font file is not distributed).
- Claude API by Anthropic.
