#!/usr/bin/env python3
"""
flash.py — 把统一镜像一次性烧录到小喵掌机。

用法: python tools/flash.py [COM口]   (默认 COM8)
前提: 先运行 tools/assemble_image.py 生成 flash_unified.bin
"""
import os, sys, subprocess

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG  = os.path.join(PROJ, "flash_unified.bin")
PORT = sys.argv[1] if len(sys.argv) > 1 else "COM8"

if not os.path.exists(IMG):
    sys.exit("先运行: python tools/assemble_image.py")

cmd = [sys.executable, "-m", "esptool", "--chip", "esp32", "-p", PORT,
       "-b", "460800", "write_flash", "0x0", IMG]
print("运行:", " ".join(cmd))
sys.exit(subprocess.run(cmd).returncode)
