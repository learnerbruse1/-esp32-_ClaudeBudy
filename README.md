# 学而思小喵掌机 · Claude Buddy 系统

把学而思「小喵」编程掌机（ESP32）刷成一个**开机带菜单**的 Claude AI 助手。

```
┌──────────────────────┐
│        小喵掌机        │
│  ▸ Claude 伙伴        │   联网问 Claude (预设/选项式)
│  ▸ 投屏               │   把屏幕镜像到手机/电脑浏览器 (WiFi)
│  ▸ 工具箱             │   手电筒 / 秒表
│  ▸ 设置               │   配网 / 设备信息 / 格式化TF卡 / 重置
└──────────────────────┘
```

> 说明：受 ESP32 资源限制，「Claude 伙伴」是一个**联网聊天客户端**（口袋版 Claude），
> 不是能在电脑上跑的 Claude Code CLI。

---

## 一、功能总览

| 功能 | 说明 |
|---|---|
| **Claude 伙伴** | 连 Wi-Fi → HTTPS 调用 Anthropic Messages API → 屏幕显示回复。预设问题选项式操作，回复可上下滚动。端点(支持自建中转)/密钥/模型可配置。 |
| **投屏** | 用 `lv_snapshot` 抓当前屏幕 → BMP → 内置 HTTP 服务，浏览器约 2fps 刷新镜像。 |
| **工具箱** | 手电筒（全白屏照明）、秒表（A 启停 / 左右清零）。 |
| **设置** | 配网设置(SoftAP 网页配置)、设备信息(可上下浏览的高亮列表)、格式化TF卡(双重确认)、重置为默认。 |

---

## 二、硬件规格 (实测)

| 项目 | 说明 |
|---|---|
| 主控 | ESP32-D0WD (WROVER, 双核 240MHz, **Quad PSRAM**, 4MB Flash) |
| 屏幕 | ST7735S 128×160 → 旋转后 **160×128** 横屏 (SPI2 40MHz: MOSI23/SCLK18/DC4/CS5) |
| 按键 | 6 键, 低电平: 上=2 下=13 左=27 右=35 A=34 B=12 |
| TF 卡 | SPI 共用总线, CS=22, MISO19 |
| 蜂鸣器 | GPIO14 (PWM) |
| 烧录口 | GD32 USB-CDC 桥, 本机 **COM8**, 支持自动复位 |

---

## 三、系统架构

**4MB Flash 分区**（单应用，简洁布局）：

```
0x001000  bootloader
0x008000  分区表
0x009000  nvs / phy_init
0x010000  factory      我们的 Claude+投屏+工具箱+设置 应用 (~3.75MB)
0x3D0000  cfg          Buddy 配置 (独立 nvs)
```

开机直接进入主菜单，无需 OTA 切换。

---

## 四、目录结构

```
xiaomiao_buddy/
├─ 一键刷机.bat                   双击即刷
├─ CMakeLists.txt  partitions.csv  sdkconfig.defaults
├─ flash_unified.bin              拼好的 4MB 镜像
├─ main/
│  ├─ main.c                入口
│  ├─ board.c/.h            ST7735 显示 + 6键(A确认/B返回, 边沿检测) + LVGL
│  ├─ ui_common.c/.h        建屏/切屏/返回键 工具
│  ├─ app_menu.c/.h         主菜单
│  ├─ app_claude.c/.h       Claude 界面 (预设/回复)
│  ├─ app_settings.c/.h     设置 (配网/设备信息/格式化/重置)
│  ├─ app_cast.c/.h         WiFi 投屏
│  ├─ app_tools.c/.h        工具箱 (手电筒/秒表)
│  ├─ claude_api.c/.h       Wi-Fi + Anthropic API (HTTPS)
│  ├─ config.c/.h           配置 (NVS cfg 分区)
│  ├─ web_portal.c/.h       SoftAP 配网网页
│  ├─ sdcard.c/.h           TF 卡格式化(FAT32)/容量读取
│  ├─ secrets.h             编译期默认凭据 (可留空)
│  └─ font_cn_16.c          中文字体 (SimHei→lv_font_conv)
└─ tools/
   ├─ flash_all.py          一键烧录系统
   ├─ assemble_image.py     拼装统一镜像
   └─ gen_symbols.py        生成字体字符集
```

---

## 五、快速开始（已有 flash_unified.bin）

掌机用 USB 连电脑，**双击 `一键刷机.bat`**（默认 COM8；其它口：命令行 `python tools\flash_all.py COM5`）。
约 1 分钟，自动重启进入主菜单。

---

## 六、从源码构建

ESP-IDF **v5.4** 装在 `C:\esp\esp-idf-v5.4`。**用原生 cmd（不要 git-bash）**：

```bat
C:\esp\esp-idf-v5.4\export.bat
cd /d E:\vibecoding\22222\xiaomiao_buddy
idf.py set-target esp32
idf.py build
python tools\assemble_image.py
python tools\flash_all.py
```

---

## 七、首次配网

主菜单 **设置 → 配网设置**（或选 Claude 伙伴时若未配置会提示「去配网」）：
1. 屏幕显示热点名 `ClaudeBuddy-XXXX`，手机连它（开放、无密码）。
2. 浏览器开 `http://192.168.4.1`。
3. 填：家里 **2.4G Wi-Fi**、API 地址（官方 `https://api.anthropic.com` 或你的中转）、密钥、模型 → 保存自动重启。

---

## 八、按键

| 场景 | 上/下 | 左/右 | A | B |
|---|---|---|---|---|
| 菜单/列表 | 移动高亮 | — | 确认 | 返回 |
| 看回复/信息 | 滚动/移动 | — | — | 返回 |
| 秒表 | — | 清零 | 启/停 | 返回 |

---

## 九、投屏（WiFi 屏幕镜像）

主菜单 **投屏** → 连 Wi-Fi → 屏幕显示一个 `http://掌机IP/`。手机/电脑浏览器打开即可看到
掌机画面（约 2fps），之后正常操作掌机画面会同步。**B 返回**（服务继续后台运行）。

---

## 十、脚本

| 脚本 | 作用 |
|---|---|
| `一键刷机.bat` → `tools/flash_all.py` | 拼装(若需要)+烧录系统到 COM 口 |
| `tools/assemble_image.py` | 把 build 产物拼成 4MB 镜像 |
| `tools/gen_symbols.py` | 生成 GB2312 字符集(重做字体用) |

---

## 十一、恢复原厂 / 备份

- 本机出厂固件备份：`..\学而思小喵编程掌机\_research\backup_current_4MB.bin`
- 厂商原镜像：`..\学而思小喵编程掌机\firmware.bin`

```bat
python -m esptool --chip esp32 -p COM8 write-flash 0x0 backup_current_4MB.bin
```

---

## 十二、故障排查

- **连不上 COM8**：换端口号（`python tools\flash_all.py COM5`）；本桥支持自动复位，一般无需按键。
- **进不去主菜单**：关机再开机（冷启动总是先进菜单）。
- **SD Card Error / mount failed**：卡不是 FAT32 → 设置→格式化TF卡。
- **联网失败**：ESP32 只支持 **2.4GHz** Wi-Fi；中转地址要兼容 `/v1/messages`。
- **回复缺字**：字体只含 GB2312，生僻字可能缺。
- **投屏打不开**：确认掌机已联网，且没同时在用配网门户(都占 80 端口)。

---

## 十三、已知限制

- 投屏只镜像本机界面。
- Claude 端点在国内需可达的中转。

---

## 十四、致谢 / 许可

- 板级参考 **xuyuejia/XUEERSI_ESP32_Board**、**pysn2012/xueersi-xiaomiao**。
- 字体 SimHei（系统字体，仅本地生成点阵，未分发字体文件）。
- Claude API by Anthropic。
