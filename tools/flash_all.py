#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
flash_all.py — 一键把 Claude Buddy 系统烧录到掌机。

用法:
    python tools/flash_all.py [COM口]      默认 COM8
    python tools/flash_all.py COM5          指定 COM5

若 flash_unified.bin 不存在，会自动用 build/ 产物拼装。

编码策略:
    检测控制台是否支持 UTF-8 → 支持则输出中文 → 否则切换英文。
"""

import os
import sys
import subprocess
import platform

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
IMG  = os.path.join(PROJ, "flash_unified.bin")
PORT = sys.argv[1] if len(sys.argv) > 1 else "COM8"

# ── 编码检测与降级 ──────────────────────────────────────────────
def _console_supports_utf8():
    """检测当前控制台是否能够正确显示 UTF-8 字符。"""
    # 1. 检查环境变量
    if os.environ.get("PYTHONIOENCODING", "").lower() in ("utf-8", "utf8"):
        return True
    # 2. 检查 stdout 编码
    if hasattr(sys.stdout, "encoding") and sys.stdout.encoding:
        enc = sys.stdout.encoding.lower()
        if enc in ("utf-8", "utf8"):
            return True
    # 3. 尝试在 Windows 上切换到 UTF-8
    if platform.system() == "Windows":
        try:
            result = subprocess.run(
                ["chcp.com", "65001"],
                capture_output=True, text=True, timeout=5
            )
            if result.returncode == 0:
                # 重新配置 stdout
                if hasattr(sys.stdout, "reconfigure"):
                    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
                return True
        except Exception:
            pass
    return False


_UTF8 = _console_supports_utf8()


def _t(cn_text, en_text):
    """根据编码能力返回中文或英文文本。"""
    return cn_text if _UTF8 else en_text


# ── 工具函数 ────────────────────────────────────────────────────
def _list_windows_ports():
    """尝试列出 Windows 上的可用 COM 端口。"""
    try:
        import winreg
        ports = []
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                             r"HARDWARE\DEVICEMAP\SERIALCOMM")
        i = 0
        while True:
            try:
                name, value, _ = winreg.EnumValue(key, i)
                ports.append((name, value))
                i += 1
            except OSError:
                break
        return ports
    except Exception:
        return []


def _banner():
    print()
    if _UTF8:
        print("  ╔══════════════════════════════════════════════════════╗")
        print("  ║     🐱 学而思小喵掌机 Claude Buddy — 系统烧录       ║")
        print("  ╚══════════════════════════════════════════════════════╝")
    else:
        print("  ============================================================")
        print("    XiaoMiao Buddy - Claude AI Firmware Flasher")
        print("  ============================================================")
    print()


# ── 镜像检查 ────────────────────────────────────────────────────
def ensure_img():
    """确保 flash_unified.bin 存在，否则拼装。"""
    if os.path.exists(IMG):
        size_mb = os.path.getsize(IMG) / (1024 * 1024)
        print(_t(
            f"  [OK] 找到镜像: flash_unified.bin ({size_mb:.1f} MB)",
            f"  [OK] Found image: flash_unified.bin ({size_mb:.1f} MB)",
        ))
        return True

    print(_t(
        "  [*] flash_unified.bin 不存在，正在拼装...",
        "  [*] flash_unified.bin not found, assembling...",
    ))
    print()

    result = subprocess.run(
        [sys.executable, os.path.join(HERE, "assemble_image.py")],
        cwd=PROJ,
        env={**os.environ, "PYTHONIOENCODING": "utf-8"},
    )
    if result.returncode != 0:
        print()
        print("  +-------------------------------------------------------+")
        print(_t(
            "  |  X  镜像拼装失败                                     |",
            "  |  X  Image assembly failed                            |",
        ))
        print(_t(
            "  |  原因: build\\ 目录缺少编译产物                      |",
            "  |  Cause: missing build artifacts in build\\             |",
        ))
        print("  +-------------------------------------------------------+")
        print(_t(
            "    解决: 在 ESP-IDF 命令行中运行 idf.py build",
            "    Fix: run idf.py build in ESP-IDF command prompt",
        ))
        return False

    if not os.path.exists(IMG):
        print()
        print(_t(
            "  X 拼装完成但未生成镜像文件，未知错误。",
            "  X  Assembly finished but image file was not created.",
        ))
        return False

    print(_t("  [OK] 镜像拼装完成", "  [OK] Assembly complete"))
    return True


# ── 主逻辑 ──────────────────────────────────────────────────────
def main():
    _banner()

    # Step 1: check image
    print(_t("  === Step 1/2: Check image ===", "  === Step 1/2: Check image ==="))
    if not ensure_img():
        sys.exit(1)

    # Step 2: list ports & flash
    print()
    print(_t("  === Step 2/2: Flash device ===", "  === Step 2/2: Flash device ==="))

    if platform.system() == "Windows":
        ports = _list_windows_ports()
        if ports:
            print(_t("  [i] 系统中检测到的 COM 端口:", "  [i] COM ports detected:"))
            for name, value in ports:
                marker = _t(" <-- target", " <-- target") if value == PORT else ""
                print(f"       {value}  ->  {name}{marker}")
        else:
            print(_t("  [!] 未能检测到 COM 端口", "  [!] No COM ports detected"))

    print()
    print(_t(
        f"  Target port : {PORT}",
        f"  Target port : {PORT}",
    ))
    print(_t(
        "  Baud rate   : 460800 bps",
        "  Baud rate   : 460800 bps",
    ))
    print(_t(
        f"  Image size  : {os.path.getsize(IMG) / (1024 * 1024):.1f} MB",
        f"  Image size  : {os.path.getsize(IMG) / (1024 * 1024):.1f} MB",
    ))
    print(_t(
        "  Est. time   : ~1 minute",
        "  Est. time   : ~1 minute",
    ))
    print()

    print("  " + "-" * 55)
    print(_t("  Flashing...", "  Flashing..."))
    print()

    result = subprocess.run(
        [
            sys.executable, "-m", "esptool",
            "--chip", "esp32",
            "-p", PORT,
            "-b", "460800",
            "write-flash", "0x0", IMG,
        ],
        env={**os.environ, "PYTHONIOENCODING": "utf-8"},
    )

    print()
    print("  " + "-" * 55)

    # ── Result ──
    if result.returncode == 0:
        print()
        if _UTF8:
            print("  ╔══════════════════════════════════════════════════════╗")
            print("  ║               OK  烧录成功！                        ║")
            print("  ╚══════════════════════════════════════════════════════╝")
        else:
            print("  ============================================================")
            print("                    OK  Flash SUCCESS")
            print("  ============================================================")
        print()
        print(_t(
            "  掌机会自动重启并进入主菜单。",
            "  Device will reboot into the main menu automatically.",
        ))
        print(_t(
            "  如屏幕无反应，请按掌机侧面的 RESET 按钮。",
            "  If screen stays blank, press the RESET button.",
        ))
    else:
        print()
        if _UTF8:
            print("  ╔══════════════════════════════════════════════════════╗")
            print("  ║               XX  烧录失败                          ║")
            print("  ╚══════════════════════════════════════════════════════╝")
        else:
            print("  ============================================================")
            print("                    XX  Flash FAILED")
            print("  ============================================================")
        print()
        print(_t(f"  返回码: {result.returncode}", f"  Return code: {result.returncode}"))
        print()

        # Troubleshooting guide
        print(_t(
            "  +-- Troubleshooting Guide -------------------------------+",
            "  +-- Troubleshooting Guide -------------------------------+",
        ))
        items_cn = [
            ("端口错误 / 设备未连接", [
                "掌机是否通过 USB 数据线连接到电脑？",
                "设备管理器 -> 端口(COM和LPT) 找 CP210x 设备",
                "指定端口: python tools/flash_all.py COM5",
            ]),
            ("端口被占用", [
                "关闭 Arduino IDE / 串口监视器 / putty",
                "关闭其他占用该 COM 口的程序",
            ]),
            ("数据线问题", [
                "确保使用数据线，而非仅充电线",
                "换一根 USB 数据线重试",
            ]),
            ("ESP32 未进入下载模式", [
                "按住掌机 BOOT 按钮不放",
                "按一下 EN/RESET 按钮，然后松开 BOOT",
                "芯片进入下载模式后重试烧录",
            ]),
            ("驱动未安装", [
                "CP210x 驱动:",
                "https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers",
            ]),
            ("权限不足", [
                "右键 一键刷机.bat -> 以管理员身份运行",
            ]),
        ]
        items_en = [
            ("Wrong COM port / device not connected", [
                "Is device connected via a DATA cable?",
                "Device Manager -> Ports (COM & LPT) -> CP210x",
                "Specify port: python tools/flash_all.py COM5",
            ]),
            ("Port in use", [
                "Close Arduino IDE / serial monitor / putty",
                "Close other apps using that COM port",
            ]),
            ("Cable issue", [
                "Use a DATA cable, not charge-only",
                "Try a different USB cable",
            ]),
            ("Chip not in download mode", [
                "Hold BOOT button on the device",
                "Press EN/RESET once, then release BOOT",
                "Chip now in download mode - retry flash",
            ]),
            ("Driver not installed", [
                "CP210x driver:",
                "https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers",
            ]),
            ("Permission denied", [
                "Right-click flash.bat -> Run as Administrator",
            ]),
        ]
        items = items_cn if _UTF8 else items_en

        for i, (title, steps) in enumerate(items, 1):
            print(f"  |  {i}. {title}")
            for s in steps:
                print(f"  |      - {s}")
            if i < len(items):
                print("  |")

        print(_t(
            "  +-------------------------------------------------------+",
            "  +-------------------------------------------------------+",
        ))
        print()
        if _UTF8:
            print("  如以上方法均无效，尝试:")
            print("    - 以管理员身份运行")
            print("    - 换一个 USB 接口")
            print("    - 重启电脑后重试")
            print()
            print("  Tip: 也可使用 Espressif Flash Download Tool (GUI):")
            print("    https://www.espressif.com/zh-hans/support/download/other-tools")
        else:
            print("  If none of the above helps, try:")
            print("    - Run as Administrator")
            print("    - Use a different USB port")
            print("    - Reboot your PC and retry")
            print()
            print("  Tip: Try Espressif Flash Download Tool (GUI):")
            print("    https://www.espressif.com/en/support/download/other-tools")

    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
