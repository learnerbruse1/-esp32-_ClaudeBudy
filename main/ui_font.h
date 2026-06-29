/**
 * ui_font.h — 中文字体 (ASCII + 常用汉字, 16px)
 * 字体文件 font_cn_16.c 由 tools/gen_font.py 用系统中文字体生成,
 * 并加入 main/CMakeLists.txt 的 SRCS。
 */
#ifndef UI_FONT_H
#define UI_FONT_H
#include "lvgl.h"
extern const lv_font_t font_cn_16;
#define UI_FONT (&font_cn_16)
#endif
