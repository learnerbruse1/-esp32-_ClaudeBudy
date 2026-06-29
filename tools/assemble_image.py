#!/usr/bin/env python3
"""
assemble_image.py — 把构建产物拼成一个 4MB 统一镜像。

布局 (见 partitions.csv):
  0x001000  bootloader.bin
  0x008000  partition-table.bin
  0x010000  xiaomiao_buddy.bin    (Claude 伙伴 + 投屏 + 工具箱 + 设置)
  其余(nvs/phy/cfg) 留空 0xFF

用法: python tools/assemble_image.py
输出: flash_unified.bin (项目根目录)
"""
import os, sys

PROJ   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD  = os.path.join(PROJ, "build")
OUT    = os.path.join(PROJ, "flash_unified.bin")
SIZE   = 0x400000  # 4MB

def place(img, path, off):
    with open(path, "rb") as f:
        d = f.read()
    if off + len(d) > SIZE:
        sys.exit(f"ERROR: {path} ({len(d)}B) 超出 0x{off:x} 处的空间")
    img[off:off+len(d)] = d
    print(f"  0x{off:06x}  {os.path.basename(path):28s} {len(d):>8d} B")

def main():
    for p in [os.path.join(BUILD, "bootloader", "bootloader.bin"),
              os.path.join(BUILD, "partition_table", "partition-table.bin"),
              os.path.join(BUILD, "xiaomiao_buddy.bin")]:
        if not os.path.exists(p):
            sys.exit(f"ERROR: 找不到 {p} (先 idf.py build)")

    img = bytearray(b"\xff" * SIZE)
    print("拼装统一镜像 (Claude Buddy):")
    place(img, os.path.join(BUILD, "bootloader", "bootloader.bin"), 0x1000)
    place(img, os.path.join(BUILD, "partition_table", "partition-table.bin"), 0x8000)
    place(img, os.path.join(BUILD, "xiaomiao_buddy.bin"), 0x10000)

    with open(OUT, "wb") as f:
        f.write(img)
    print(f"\n完成 -> {OUT}  ({SIZE} 字节 / 4MB)")
    print("下一步: python tools/flash_all.py")

if __name__ == "__main__":
    main()
