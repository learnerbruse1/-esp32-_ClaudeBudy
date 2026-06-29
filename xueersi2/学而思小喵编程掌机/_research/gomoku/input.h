/**
 * ================================================================
 * input.h — 按键输入封装
 * ================================================================
 *
 * 6 键布局: 上下左右 + A/B
 * - 防抖: 150ms（ISR 级别）
 * - 长按: B > 500ms → BTN_B_LONG
 * - 事件: 边沿触发（按下瞬间），避免重复触发
 * ================================================================
 */

#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 按键事件类型
 * ================================================================ */
typedef enum {
    BTN_NONE    = 0,
    BTN_UP      = 1,
    BTN_DOWN    = 2,
    BTN_LEFT    = 3,
    BTN_RIGHT   = 4,
    BTN_A       = 5,
    BTN_B       = 6,
    BTN_B_LONG  = 7,   /* B 长按 > 500ms */
} btn_event_t;

/* ================================================================
 * 接口
 * ================================================================ */

/** 初始化按键 GPIO + ISR */
void input_init(void);

/** 轮询按键事件（非阻塞，无事件返回 BTN_NONE） */
btn_event_t input_poll(void);

/** 阻塞等待按键事件（timeout_ms=0 表示永久等待） */
btn_event_t input_wait(int timeout_ms);

/** 获取最后一次事件发生时的 tick（用于长按检测） */
uint32_t input_last_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_H */
