/**
 * ================================================================
 * ui.h — 五子棋 UI 渲染层（中文注释版）
 * ================================================================
 *
 * 【屏幕】ST7735 160×128 横屏（LVGL 软件旋转，非硬件旋转）
 * 【架构】LVGL 多屏 + lv_layer_top() 共享状态栏
 * 【创建日期】2026-06-25  |  【最后修改】2026-06-27
 *
 * 布局（160×128 横屏）：
 *   ┌──────────────────── 160px ────────────────────┐
 *   │ 状态栏 20px — 回合提示 + 喇叭图标              │
 *   ├────────────────────────────────────────────────┤
 *   │         棋盘 100×100px Canvas                  │
 *   │         15×15 网格（格距 7px）                 │
 *   │         光标 5×5 红色闪烁                      │
 *   └────────────────────────────────────────────────┘
 *
 * 棋盘参数：
 *   - 格距 7px，网格占用 14×7+1=99px，Canvas 100×100 含 1px 边距
 *   - 棋子半径 3px，Canvas 缓冲 20KB SRAM
 *   - Canvas 位于 (30,24)，左右各 30px 边距，上方状态栏 20px
 * ================================================================
 */

#ifndef UI_H
#define UI_H

#include "lvgl.h"
#include "board.h"
#include "game.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 颜色常量 (RGB565) — 深海军蓝主题
 * ================================================================ */
#define UI_CLR_BG           lv_color_hex(0x002A58)  /* 深海军蓝底 */
#define UI_CLR_BOARD_BG      lv_color_hex(0xB8A080)  /* 棋盘底加深 */
#define UI_CLR_BOARD         lv_color_hex(0x6B5335)  /* 深木色网格线 */
#define UI_CLR_BLACK_STONE   lv_color_hex(0x181818)
#define UI_CLR_WHITE_STONE   lv_color_hex(0xFFFFFF)
#define UI_CLR_CURSOR        lv_color_hex(0xFF4444)
#define UI_CLR_STATUSBAR     lv_color_hex(0x001A3E)
#define UI_CLR_TEXT          lv_color_hex(0xFFFFFF)  /* 纯白文字 */
#define UI_CLR_ACCENT        lv_color_hex(0xFFCC00)  /* 金色强调 */
#define UI_CLR_WIN           lv_color_hex(0x44FF44)
#define UI_CLR_INVERT        lv_color_hex(0xFFFFFF)  /* 反色白底 */
#define UI_CLR_INVERT_TEXT   lv_color_hex(0x002A58)  /* 反色蓝字 */

/* ================================================================
 * 布局常量
 * ================================================================ */
#define BOARD_CELL_SIZE     7
#define BOARD_STONE_RADIUS  3
#define BOARD_GRID_PX        98
#define GRID_PAD             1
#define CANVAS_SIZE          100

#define STATUSBAR_HEIGHT     20
#define CANVAS_X             30
#define CANVAS_Y             24

#define SOUND_ICON_SZ        10

#define STONE_CX(x)          (GRID_PAD + (x) * BOARD_CELL_SIZE)
#define STONE_CY(y)          (GRID_PAD + (y) * BOARD_CELL_SIZE)

/* ================================================================
 * UI 画面枚举
 * ================================================================ */
typedef enum {
    UI_SCREEN_TITLE = 0,
    UI_SCREEN_DIFF_SEL,
    UI_SCREEN_GAME,
    UI_SCREEN_RESULT,
} ui_screen_t;

/* ================================================================
 * 接口函数
 * ================================================================ */

/** 初始化 UI（创建 canvas、状态栏等） */
void ui_init(void);

/** 切换到标题画面 */
void ui_show_title(void);

/** 切换到设置画面（难度+先后手+音效） */
void ui_show_settings(void);

/** 设置页：上/下移动当前行 */
void ui_settings_move_row(int delta);

/** 设置页：左/右切换当前行选项 */
void ui_settings_change_val(int delta);

/** 设置页：获取难度值 */
difficulty_t ui_settings_get_difficulty(void);

/** 设置页：获取先后手 */
bool ui_settings_player_is_black(void);

/** 设置页：获取音效开关 */
bool ui_settings_sound_on(void);

/** 切换音效开关（原子操作：状态+图标+硬件） */
void ui_sound_toggle(void);

/** 设置页：获取当前焦点行 (0-2=选项, 3=START) */
int ui_settings_get_selected_row(void);

/** 刷新全局喇叭图标 */
void ui_update_sound_icon(void);

/** 切换到游戏画面（棋盘+状态栏） */
void ui_show_game(void);

/** 显示结果画面 */
void ui_show_result(game_state_t state);

/** 刷新整张棋盘 */
void ui_update_board(void);

/** 在 (x,y) 落子 */
void ui_draw_stone(int x, int y, bool black);

/** 移动光标到 (x,y)，(-1,-1)=隐藏 */
void ui_move_cursor(int x, int y);

/** 设置状态栏文字 */
void ui_set_status(const char *text);

/** 格式化状态栏 */
void ui_statusbar_set_text(const char *fmt, ...);

/** 获取主屏幕对象 */
lv_obj_t *ui_get_screen(void);

/** 获取当前画面 */
ui_screen_t ui_get_current_screen(void);

/** 设置难度选择回调 */
typedef void (*ui_diff_callback_t)(difficulty_t diff);
void ui_set_diff_callback(ui_diff_callback_t cb);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
