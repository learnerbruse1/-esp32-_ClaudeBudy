# XRS Gomoku — 五子棋人机对弈

> 基于 ESP32 + LVGL 9.2 的嵌入式五子棋游戏，搭载 Pela Alpha-Beta 搜索引擎，运行于 1.8 寸 ST7735 SPI 彩色屏幕的实体掌机。

[![Platform](https://img.shields.io/badge/platform-ESP32--WROVER--B-green)](https://www.espressif.com/)
[![Framework](https://img.shields.io/badge/framework-ESP--IDF%20v5.4-blue)](https://docs.espressif.com/projects/esp-idf/)
[![UI](https://img.shields.io/badge/UI-LVGL%209.2-orange)](https://lvgl.io/)
[![License](https://img.shields.io/badge/license-GPL%20v3-red)](LICENSE)

---

## 项目简介

XRS Gomoku 是一款运行在**学而思编程机（小喵掌机）**上的五子棋人机对弈游戏。玩家与 AI 在 15×15 棋盘上轮流落子，先形成五连者获胜。

### 核心亮点

- 🧠 **Pela AI 引擎**：Alpha-Beta 剪枝搜索 + 262144 条目模式匹配评估表，支持 5 级难度
- 🎨 **LVGL 9.2 图形界面**：Canvas 绘制棋盘、多屏架构（标题/设置/游戏/结果）、流畅动画
- 🔊 **LEDC PWM 音效**：6 种音效反馈（落子/AI/胜/负/平/菜单），全局开关即时生效
- 🎮 **实体按键操控**：6 键导航（↑↓←→ A B），B 键支持短按/长按双功能
- 📦 **紧凑固件**：~540KB，含完整 AI 引擎 + LVGL 框架

---

## 硬件环境

| 组件 | 规格 |
|------|------|
| **主控** | ESP32-WROVER-B（双核 240MHz, 8MB PSRAM） |
| **屏幕** | ST7735S 1.8" SPI LCD，物理 128×160 竖屏 → LVGL 旋转 90° → 逻辑 160×128 横屏 |
| **接口** | 4 线 SPI（MOSI=23, SCLK=18, DC=4, CS=5） |
| **按键** | 6 键：↑(2) ↓(13) ←(27) →(35) A(34) B(12)，低电平触发 |
| **音频** | 无源蜂鸣器 GPIO14，LEDC PWM 驱动 |
| **电源** | 3.3V / USB 供电 |

> **⚠️ 屏幕旋转说明**：物理屏为竖屏（128×160），通过 **LVGL 软件旋转**（非硬件旋转）转为横屏（160×128）。ST7735 端不做任何 `swap_xy`/`mirror` 配置，避免双重旋转导致画面撕裂。

### 硬件引脚映射

| 功能 | GPIO | 说明 |
|------|------|------|
| SPI MOSI | 23 | 数据输出 |
| SPI SCLK | 18 | 时钟 |
| SPI MISO | 19 | 数据输入（与 LCD RST 共用） |
| TFT DC | 4 | 数据/命令选择 |
| TFT CS | 5 | 片选 |
| BTN UP | 2 | 方向键上 → 光标上移 |
| BTN DOWN | 13 | 方向键下 → 光标下移 |
| BTN LEFT | 27 | 方向键左 → 光标左移 |
| BTN RIGHT | 35 | 方向键右 → 光标右移 |
| BTN A | 34 | 确认 / 落子 |
| BTN B | 12 | 返回 / 音效切换 |

---

## 软件架构

```
┌──────────────────────────────────────────────────────┐
│                    main.c (入口)                      │
│         初始化 → 主循环（事件驱动状态机）               │
├──────────────────────────────────────────────────────┤
│  UI 层 (ui.h/cpp)       │  输入层 (input.h/cpp)       │
│  - 多屏架构              │  - 6键 GPIO ISR            │
│  - Canvas 棋盘           │  - 150ms 防抖              │
│  - 光标/棋子/喇叭        │  - B 长按检测 >500ms       │
├──────────────────────────┼────────────────────────────┤
│  音效层 (audio.h/cpp)    │  游戏层 (game.h/cpp)       │
│  - LEDC PWM @ GPIO14     │  - 状态机                  │
│  - 6 种预定义音效        │  - 落子/悔棋/胜负判定      │
│  - 全局开关同步          │  - 棋谱栈                  │
├──────────────────────────┴────────────────────────────┤
│  AI 层 (Pela 引擎移植)                                 │
│  - board.h/cpp    棋盘数据结构 (459 Tsquare)           │
│  - evaluate.h/cpp 模式评估 (K[262144] 1MB PSRAM)      │
│  - alfabeta.h/cpp Alpha-Beta 搜索 + try4 攻防         │
│  - ai_bridge.h/cpp AI 桥接层 + 难度分级                │
│  - ipc.h/cpp      双核通信 (Core 0 AI, Core 1 UI)     │
└──────────────────────────────────────────────────────┘
```

---

## 功能清单

### 游戏功能
- ✅ 5 档 AI 难度（Level 1~5，影响搜索深度 2~16 + 超时 0.5~5s）
- ✅ 先后手选择（玩家执黑 / AI 执黑）
- ✅ 15×15 标准棋盘，含 9 星位标记点
- ✅ 红色闪烁光标导航（↑↓←→ 移动）
- ✅ A 键落子，即时 UI 反馈 + 音效
- ✅ 自动胜负判定（五连检测）+ 平局判定（满盘）

### 设置功能
- ✅ 音效开关（On/Off），B 键短按即时切换
- ✅ 10×10 RGB565 位图喇叭图标（白色=开，灰色+斜叉=关）
- ✅ 设置参数**全局同步**：游戏内切换后回设置页自动同步

### 交互设计
- ✅ 实体十字键导航，A=确认，B=返回/切换音效
- ✅ B 长按 >500ms 返回标题（自动复位音效为开）
- ✅ 设置页双视觉态：焦点行反色高亮 + 非焦点行下划线标记

---

## 操作说明

| 按键 | 标题画面 | 设置画面 | 游戏画面 | 结果画面 |
|------|---------|---------|---------|---------|
| **↑↓** | — | 切换选项行 | 移动光标 | — |
| **←→** | — | 切换当前行值 | 移动光标 | — |
| **A** | 进入设置 | 确认 START | 落子 | 重新开始 |
| **B 短按** | — | 返回标题 | 切换音效 | 返回标题 |
| **B 长按** | — | — | 返回标题 | — |

---

## 难度说明

| Level | 搜索深度 | 最大深度 | 超时 | 特点 |
|-------|---------|---------|------|------|
| 1 | 2 | 4 | 0.5s | 入门，故意随机 |
| 2 | 3 | 6 | 1.0s | 初级 |
| 3 | 4 | 8 | 2.0s | 中级（默认） |
| 4 | 5 | 12 | 3.5s | 高级 |
| 5 | 6 | 16 | 5.0s | 专家，尽力搜索 |

---

## 目录结构

```
xrs_gomoku/
├── main/                       # 应用源码
│   ├── main.c                  # 入口：初始化 + 主循环
│   ├── CMakeLists.txt          # ESP-IDF 编译配置
│   ├── ui.h / ui.cpp           # UI 渲染层（标题/设置/游戏/结果）
│   ├── input.h / input.cpp     # 按键输入（GPIO ISR + 防抖 + 长按）
│   ├── audio.h / audio.cpp     # 蜂鸣器音效（LEDC PWM）
│   ├── game.h / game.cpp       # 游戏逻辑（状态机 + 落子 + 胜负）
│   ├── board.h / board.cpp     # 棋盘数据结构（Tsquare 17×17）
│   ├── evaluate.h / evaluate.cpp  # AI 模式评估（K 表 262144）
│   ├── alfabeta.h / alfabeta.cpp  # AI 搜索（Alpha-Beta + try4）
│   ├── ai_bridge.h / ai_bridge.cpp # AI 桥接层 + 难度参数
│   └── ipc.h / ipc.cpp         # 双核通信（FreeRTOS Queue）
├── components/
│   └── lvgl/                   # LVGL 9.2 图形库（本地副本）
├── pela_source/                # Pela 引擎原始源码（C++ Builder）
├── build/                      # 编译输出（含烧录参数）
├── tests/                      # PC 端 C++ 单元测试
├── CMakeLists.txt              # 顶层 CMake 配置
├── sdkconfig                   # ESP-IDF 项目配置
├── sdkconfig.defaults          # 默认配置模板
├── PLAN.md                     # 开发计划（12 阶段详细分步）
├── DEVELOPMENT.md              # 开发复盘与经验总结
├── README.md                   # 本文件
├── build.py                    # 命令行构建脚本
├── flash.py                    # 命令行烧录脚本
└── run_test.py                 # 自动化测试脚本（构建+烧录+监控）
```

---

## 编译与烧录指南

### 开发环境要求

| 工具 | 版本 |
|------|------|
| ESP-IDF | v5.4 |
| Python | 3.8+ |
| ESP32 工具链 | xtensa-esp32-elf |
| 串口驱动 | CP210x / CH340 |

### 编译

```bash
# 1. 激活 ESP-IDF 环境
cd ~/esp-idf-v5.4
. ./export.sh

# 2. 进入项目目录并编译
cd xrs_gomoku
idf.py build
```

### 烧录

```bash
# 自动检测串口并烧录
idf.py -p /dev/ttyUSB0 flash

# 或使用项目脚本（Windows）
python flash.py
```

### 监控串口输出

```bash
idf.py -p /dev/ttyUSB0 monitor
```

### 一键测试

```bash
# 构建 + 烧录 + 串口监控（自动超时关闭）
python run_test.py

# 自定义监控时长
python run_test.py --timeout 60
```

---

## 后续扩展方向

- [ ] **双人对战模式**：A/B 玩家轮流落子，关闭 AI
- [ ] **悔棋功能**：B 短按改为悔棋（撤销 AI+玩家两步）
- [ ] **棋谱保存**：Flash 存储对局记录，支持回放
- [ ] **多游戏合集**：围棋/黑白棋/四子棋，复用 UI 框架
- [ ] **更多 AI 难度**：Level 6-8，搜索深度 20+，开局库扩展
- [ ] **联网对战**：ESP-NOW / WiFi 双机互联对弈
- [ ] **主题切换**：多套配色方案（夜间模式/木纹/简约）

---

## 跨平台移植指南

### 移植到其他硬件需修改的部分

| 组件 | 文件 | 修改内容 |
|------|------|---------|
| **屏幕驱动** | `main.c` display_init() | 替换为对应屏驱动 IC（ILI9341/ST7789 等） |
| **屏幕分辨率** | `main.c` DISP_WIDTH/HEIGHT | 修改物理分辨率 + 调整逻辑分辨率 |
| **LVGL 旋转** | `main.c` lvgl_init() | 根据屏方向调整 `swap_xy`/`mirror_x`/`mirror_y` |
| **按键映射** | `input.cpp` PIN_UP/DOWN/... | 修改 GPIO 编号 |
| **蜂鸣器引脚** | `audio.cpp` BUZZER_PIN | 修改 GPIO 编号 |
| **主循环适配** | `main.c` app_main() | 可去除 IPC 双核，主线程直接调用 AI |

### 分辨率适配公式

```
BOARD_CELL_SIZE = floor((屏幕宽 - 左右边距) / 14)
CANVAS_SIZE     = BOARD_CELL_SIZE * 14 + 2
CANVAS_X        = (屏幕宽 - CANVAS_SIZE) / 2

示例（160 宽 → 30+100+30）:
  BOARD_CELL_SIZE = floor((160 - 60) / 14) = 7
  CANVAS_SIZE     = 7 * 14 + 2 = 100
  CANVAS_X        = (160 - 100) / 2 = 30
```

---

## 许可证

本项目基于 [GNU General Public License v3.0](LICENSE) 开源。

Pela 引擎原始版权归 Petr Lastovicka 所有，本项目中的移植代码遵循 GPL v3 协议。
