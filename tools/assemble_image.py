#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
assemble_image.py — 把 ESP32 构建产物拼装成一个 4MB 统一镜像。

布局 (见 partitions.csv):
  0x001000  bootloader.bin         (引导程序)
  0x008000  partition-table.bin    (分区表)
  0x010000  xiaomiao_buddy.bin     (Claude Buddy 固件)
  其余      (nvs / phy / cfg)      保持 0xFF (擦除态)

用法:
    python tools/assemble_image.py

输出:
    flash_unified.bin (项目根目录, 4MB)

前置条件:
    需要先完整编译: idf.py build

编码策略:
    检测控制台是否支持 UTF-8 → 支持则输出中文 → 否则切换英文。
"""

import os
import sys
import subprocess
import platform

PROJ  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(PROJ, "build")
OUT   = os.path.join(PROJ, "flash_unified.bin")
SIZE  = 0x400000  # 4MB

# ── 编码检测与降级 ──────────────────────────────────────────────
def _console_supports_utf8():
    if os.environ.get("PYTHONIOENCODING", "").lower() in ("utf-8", "utf8"):
        return True
    if hasattr(sys.stdout, "encoding") and sys.stdout.encoding:
        if sys.stdout.encoding.lower() in ("utf-8", "utf8"):
            return True
    if platform.system() == "Windows":
        try:
            result = subprocess.run(
                ["chcp.com", "65001"],
                capture_output=True, text=True, timeout=5
            )
            if result.returncode == 0:
                if hasattr(sys.stdout, "reconfigure"):
                    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
                return True
        except Exception:
            pass
    return False


_UTF8 = _console_supports_utf8()


def _t(cn_text, en_text):
    return cn_text if _UTF8 else en_text


# ── 组件布局 ────────────────────────────────────────────────────
COMPONENTS = [
    ("bootloader.bin",       "bootloader",           0x001000,
     ("引导程序", "Bootloader")),
    ("partition-table.bin",  "partition_table",      0x008000,
     ("分区表",   "Partition table")),
    ("xiaomiao_buddy.bin",   ".",                    0x010000,
     ("Claude Buddy 固件", "Claude Buddy firmware")),
]


def _format_size(n_bytes):
    if n_bytes >= 1024 * 1024:
        return f"{n_bytes / (1024 * 1024):.2f} MB"
    elif n_bytes >= 1024:
        return f"{n_bytes / 1024:.1f} KB"
    return f"{n_bytes} B"


def _banner():
    print()
    if _UTF8:
        print("  ╔══════════════════════════════════════════════════════╗")
        print("  ║        🔧 拼装 Claude Buddy 统一刷机镜像            ║")
        print("  ╚══════════════════════════════════════════════════════╝")
    else:
        print("  ============================================================")
        print("     Assemble Claude Buddy Unified Flash Image (4 MB)")
        print("  ============================================================")
    print()


def _check_prerequisites():
    missing = []
    for filename, subdir, offset, (desc_cn, desc_en) in COMPONENTS:
        path = os.path.join(BUILD, subdir, filename)
        if not os.path.exists(path):
            missing.append((path, desc_cn if _UTF8 else desc_en))

    if missing:
        print()
        print("  +-------------------------------------------------------+")
        print(_t(
            "  |  X  构建产物缺失                                     |",
            "  |  X  Build artifacts missing                          |",
        ))
        print(_t(
            "  |  以下文件未找到:                                     |",
            "  |  Missing files:                                      |",
        ))
        for path, desc in missing:
            print(f"  |    - {path}  ({desc})")
        print("  |                                                       |")
        print(_t(
            "  |  请先完成编译 (需要 ESP-IDF 环境):                   |",
            "  |  Please build first (requires ESP-IDF environment):   |",
        ))
        print(_t(
            "  |    1. 打开 ESP-IDF 命令行                            |",
            "  |    1. Open ESP-IDF command prompt                    |",
        ))
        print(_t(
            "  |    2. idf.py build                                   |",
            "  |    2. idf.py build                                   |",
        ))
        print("  +-------------------------------------------------------+")
        return False
    return True


def _place_component(img, filename, subdir, offset, desc_pair):
    desc_cn, desc_en = desc_pair
    desc = desc_cn if _UTF8 else desc_en

    path = os.path.join(BUILD, subdir, filename)
    with open(path, "rb") as f:
        data = f.read()

    data_size = len(data)
    end = offset + data_size

    if end > SIZE:
        print(_t(
            f"  X 错误: {filename} ({_format_size(data_size)}) "
            f"超出 4MB 镜像空间",
            f"  X  Error: {filename} ({_format_size(data_size)}) "
            f"exceeds 4 MB image space",
        ))
        sys.exit(5)

    img[offset:end] = data

    # Progress bar
    pct = end / SIZE * 100
    bar_len = 20
    filled = int(bar_len * pct / 100)
    bar = "#" * filled + "-" * (bar_len - filled)

    print(f"  | 0x{offset:06X}  {filename:28s}  {_format_size(data_size):>10s}  "
          f"{bar}  {pct:5.1f}%")
    print(f"  |           ({desc})")


def main():
    _banner()

    # Step 1: prerequisites
    print(_t("  === Check build artifacts ===", "  === Check build artifacts ==="))
    if not _check_prerequisites():
        sys.exit(1)

    print(_t("  [OK] All artifacts ready", "  [OK] All artifacts ready"))
    print()

    # Step 2: assemble
    print(_t("  === Assemble image ===", "  === Assemble image ==="))
    print("  +" + "-" * 62 + "+")
    header = _t(
        "  |  Offset     Component                         Size         Usage  |",
        "  |  Offset     Component                         Size         Usage  |",
    )
    print(header)
    print("  |" + "-" * 62 + "|")

    img = bytearray(b"\xFF" * SIZE)

    for filename, subdir, offset, desc_pair in COMPONENTS:
        _place_component(img, filename, subdir, offset, desc_pair)

    print("  +" + "-" * 62 + "+")

    # Step 3: write
    with open(OUT, "wb") as f:
        f.write(img)

    img_size = os.path.getsize(OUT)
    actual_data = SIZE - sum(1 for b in img if b == 0xFF)

    print()
    print(_t(
        f"  [OK] Image created: {OUT}",
        f"  [OK] Image created: {OUT}",
    ))
    print(_t(
        f"       Size: {_format_size(img_size)} (exactly {SIZE // (1024 * 1024)} MB)",
        f"       Size: {_format_size(img_size)} (exactly {SIZE // (1024 * 1024)} MB)",
    ))
    print(_t(
        f"       Actual data: {_format_size(actual_data)}",
        f"       Actual data: {_format_size(actual_data)}",
    ))
    print()
    print(_t(
        "  Next: python tools/flash_all.py [COM port]",
        "  Next: python tools/flash_all.py [COM port]",
    ))
    print(_t(
        "        or double-click OneKeyFlash.bat",
        "        or double-click OneKeyFlash.bat",
    ))


if __name__ == "__main__":
    main()
