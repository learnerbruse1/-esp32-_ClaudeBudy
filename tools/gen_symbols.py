#!/usr/bin/env python3
# 生成 GB2312 全部汉字+符号 的 Unicode 字符集, 供 lv_font_conv 用 --symbols 取用。
# ASCII 由 lv_font_conv 的 --range 0x20-0x7F 单独覆盖。
import sys
chars = set()
# 区16-87: 一级+二级汉字 (0xB0A1 - 0xF7FE)
for hi in range(0xB0, 0xF8):
    for lo in range(0xA1, 0xFF):
        try:
            chars.add(bytes([hi, lo]).decode('gb2312'))
        except Exception:
            pass
# 区1-9: 常用符号/全角/希腊/俄文/拼音 (0xA1A1 - 0xA9FE)
for hi in range(0xA1, 0xAA):
    for lo in range(0xA1, 0xFF):
        try:
            ch = bytes([hi, lo]).decode('gb2312')
            if ch and not ch.isspace():
                chars.add(ch)
        except Exception:
            pass
s = ''.join(sorted(chars))
with open('symbols.txt', 'w', encoding='utf-8') as f:
    f.write(s)
print(f'{len(s)} chars written to symbols.txt')
