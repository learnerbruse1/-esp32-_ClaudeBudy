/**
 * ui_common.h — 共用的小工具 (建屏 / 切屏 / 返回键)
 */
#ifndef UI_COMMON_H
#define UI_COMMON_H
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

/** 新建一块标准风格的屏幕 (深色背景 + 中文字体) */
lv_obj_t *ui_make_screen(void);

/** 加载新屏并释放旧屏 (避免泄漏) */
void ui_load_screen(lv_obj_t *scr);

/** 给可聚焦控件挂"B 返回"回调: 按 B(ESC) 时调用 cb() */
void ui_add_back(lv_obj_t *obj, void (*cb)(void));

#ifdef __cplusplus
}
#endif
#endif
