#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
flash_all.py — 一键把 Claude Buddy 系统烧录到掌机。
用法:  python tools/flash_all.py [COM口]     (默认 COM8)
若 flash_unified.bin 不存在, 会先用 build/ 的产物自动拼装。
"""
import os, sys, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
IMG = os.path.join(PROJ, "flash_unified.bin")
PORT = sys.argv[1] if len(sys.argv) > 1 else "COM8"

def ensure_img():
    if os.path.exists(IMG):
        return
    print("flash_unified.bin 不存在, 尝试拼装...")
    subprocess.run([sys.executable, os.path.join(HERE, "assemble_image.py")], check=True)

def main():
    ensure_img()
    if not os.path.exists(IMG):
        sys.exit("拼装失败: 找不到 flash_unified.bin (先 idf.py build)")
    print(f"烧录 {IMG}  ->  {PORT} @0x0  (约 1 分钟)\n")
    r = subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32",
                        "-p", PORT, "-b", "460800", "write-flash", "0x0", IMG])
    if r.returncode == 0:
        print("\n完成! 掌机会自动重启进入主菜单。")
    else:
        print("\n烧录失败。检查数据线/端口号 (python tools/flash_all.py COM5)。")
    sys.exit(r.returncode)

if __name__ == "__main__":
    main()
