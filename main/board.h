/**
 * ================================================================
 * board.h — 学而思小喵掌机 板级支持 (显示 + 按键 + LVGL)
 * ================================================================
 * 硬件 (已在实机验证, 见 _research/board/main.c):
 *   - MCU : ESP32-D0WD (WROVER, Quad PSRAM, 4MB)
 *   - LCD : ST7735S 128x160 → 旋转 90° → 160x128 横屏
 *           SPI2(VSPI) 40MHz: MOSI=23 SCLK=18 DC=4 CS=5 (无 RST/背光控制)
 *   - 按键: 6 个, 低电平有效, UP=2 DOWN=13 LEFT=27 RIGHT=35 A=34 B=12
 * ================================================================
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 逻辑屏幕分辨率 (横屏) */
#define BOARD_LCD_H_RES   160
#define BOARD_LCD_V_RES   128

/* 按键 GPIO */
#define BOARD_BTN_UP      2
#define BOARD_BTN_DOWN    13
#define BOARD_BTN_LEFT    27
#define BOARD_BTN_RIGHT   35
#define BOARD_BTN_A       34
#define BOARD_BTN_B       12

typedef enum {
    BTN_NONE = 0,
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B
} board_btn_t;

/**
 * 初始化显示 + LVGL + 按键输入设备。
 * 完成后即可在 board_lock()/board_unlock() 之间创建 LVGL 控件。
 */
void board_init(void);

/** 获取 LVGL 互斥锁 (操作任何 LVGL 对象前必须持有) */
bool board_lock(int timeout_ms);
void board_unlock(void);

/** 默认输入组 — 新建的可聚焦控件应加入此组以便方向键导航 */
lv_group_t *board_group(void);

/** LVGL 键盘输入设备句柄 (一般无需直接使用) */
lv_indev_t *board_indev(void);

/** 直接读取某个按键当前是否按下 (低电平=按下), 用于非 LVGL 场景 */
bool board_btn_is_down(int gpio);

#ifdef __cplusplus
}
#endif
#endif /* BOARD_H */
