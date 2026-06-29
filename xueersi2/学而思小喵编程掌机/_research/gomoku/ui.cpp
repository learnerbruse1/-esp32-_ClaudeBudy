/**
 * ================================================================
 * ui.cpp — 五子棋 UI 渲染层实现（中文注释版）
 * ================================================================
 *
 * 【架构】LVGL v9.2 原生多屏 + 共享状态栏
 *   - 状态栏：lv_layer_top() 全局共享层，始终可见
 *   - 页面：lv_obj_create(NULL) 创建独立 Screen，lv_screen_load() 切换
 *   - 四屏流转：标题 → 设置 → 游戏 → 结果
 *
 * 【布局常量】参见 ui.h（CANVAS_SIZE=100, CANVAS_X=30, CANVAS_Y=24 等）
 * 【依赖模块】lvgl.h、audio.h、game.h
 * 【创建日期】2026-06-25  |  【最后修改】2026-06-27
 *
 * ⚠️ 踩坑备忘：
 *   - 喇叭图标：曾用 lv_obj 纯色矩形 → 无轮廓 → 改为 lv_image + RGB565 位图
 *   - 喇叭层级：曾放 lv_layer_top() → 被状态栏遮挡 → 改为 statusbar_obj 子对象
 *   - 音效复位：曾仅 B 长按路径 → 遗漏其他路径 → 统一到 ui_show_title() 处理
 * ================================================================
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "ui.h"
#include "audio.h"

static const char *TAG = "UI";

/* ---- 共享状态 ---- */
static lv_obj_t  *status_label  = NULL;
static lv_obj_t  *statusbar_obj = NULL;
static lv_obj_t  *canvas_board  = NULL;
static lv_obj_t  *cursor_obj    = NULL;
static ui_screen_t current_screen = UI_SCREEN_TITLE;
static int cursor_x = -1, cursor_y = -1;
static bool cursor_visible = true;

/* 当前活跃的 LVGL 屏幕 */
static lv_obj_t  *active_scr    = NULL;

/* 设置页状态 */
typedef struct {
    const char *label;
    const char *opts[5];
    int         n_opts, value;
    lv_obj_t   *label_obj, *btn_objs[5];
} settings_item_t;
static settings_item_t g_items[3];
static int settings_row = 0;
static bool g_sound_on = true;
static lv_obj_t *settings_start_btn = NULL;

/* Canvas 缓冲 */
static lv_color_t canvas_buf[CANVAS_SIZE * CANVAS_SIZE];

/* ================================================================
 * Canvas 底层绘制函数
 *
 * 基于 lv_canvas_set_px() 逐像素绘制。
 * canvas_fill_circle 使用 Bresenham 中点圆算法（整数运算，无浮点）。
 * ================================================================ */
static void canvas_clear(lv_color_t c)  { lv_canvas_fill_bg(canvas_board, c, LV_OPA_COVER); }
static void canvas_hline(int x0, int x1, int y, lv_color_t c) {
    for (int x = x0; x <= x1; x++) lv_canvas_set_px(canvas_board, x, y, c, LV_OPA_COVER);
}
static void canvas_vline(int x, int y0, int y1, lv_color_t c) {
    for (int y = y0; y <= y1; y++) lv_canvas_set_px(canvas_board, x, y, c, LV_OPA_COVER);
}
/**
 * canvas_fill_circle — Bresenham 中点圆算法填充实心圆
 * @param cx,cy  圆心 (Canvas 坐标系)
 * @param r      半径 (px)
 * @param c      颜色
 */
static void canvas_fill_circle(int cx, int cy, int r, lv_color_t c) {
    int x = 0, y = r, d = 3 - 2 * r;
    while (y >= x) {
        for (int i = cx - x; i <= cx + x; i++) {
            lv_canvas_set_px(canvas_board, i, cy - y, c, LV_OPA_COVER);
            lv_canvas_set_px(canvas_board, i, cy + y, c, LV_OPA_COVER);
        }
        for (int i = cx - y; i <= cx + y; i++) {
            lv_canvas_set_px(canvas_board, i, cy - x, c, LV_OPA_COVER);
            lv_canvas_set_px(canvas_board, i, cy + x, c, LV_OPA_COVER);
        }
        x++; if (d > 0) { y--; d += 4 * (x - y) + 10; } else d += 4 * x + 6;
    }
}
/**
 * canvas_draw_grid — 绘制 15×15 棋盘网格
 * 格距 7px，含 9 个星位标记点（天元+四隅）。
 */
static void canvas_draw_grid(void) {
    canvas_clear(UI_CLR_BOARD_BG);
    int p0 = GRID_PAD, p1 = GRID_PAD + BOARD_GRID_PX;
    for (int r = 0; r < 15; r++) {
        int y = GRID_PAD + r * BOARD_CELL_SIZE;
        canvas_hline(p0, p1, y, UI_CLR_BOARD);
    }
    for (int c = 0; c < 15; c++) {
        int x = GRID_PAD + c * BOARD_CELL_SIZE;
        canvas_vline(x, p0, p1, UI_CLR_BOARD);
    }
    static const int star[][2] = {{3,3},{3,7},{3,11},{7,3},{7,7},{7,11},{11,3},{11,7},{11,11}};
    for (int i = 0; i < 9; i++)
        canvas_fill_circle(GRID_PAD + star[i][0]*BOARD_CELL_SIZE,
                           GRID_PAD + star[i][1]*BOARD_CELL_SIZE, 1, UI_CLR_BOARD);
}

/* ================================================================
 * 光标（独立 LVGL 对象，放在游戏屏上）
 * ================================================================ */
static void cursor_create(lv_obj_t *parent) {
    cursor_obj = lv_obj_create(parent);
    lv_obj_set_size(cursor_obj, 5, 5);
    lv_obj_set_style_border_color(cursor_obj, UI_CLR_CURSOR, 0);
    lv_obj_set_style_border_width(cursor_obj, 1, 0);
    lv_obj_set_style_bg_opa(cursor_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(cursor_obj, 0, 0);
    lv_obj_add_flag(cursor_obj, LV_OBJ_FLAG_HIDDEN);
}
static void cursor_update_pos(void) {
    if (!cursor_obj || cursor_x < 0) { if (cursor_obj) lv_obj_add_flag(cursor_obj, LV_OBJ_FLAG_HIDDEN); return; }
    int cx = CANVAS_X + STONE_CX(cursor_x), cy = CANVAS_Y + STONE_CY(cursor_y);
    lv_obj_set_pos(cursor_obj, cx - 2, cy - 2);
    lv_obj_clear_flag(cursor_obj, LV_OBJ_FLAG_HIDDEN);
}
static void cursor_blink_cb(lv_timer_t *t) {
    if (!cursor_obj) return;
    cursor_visible = !cursor_visible;
    if (cursor_visible && cursor_x >= 0) lv_obj_clear_flag(cursor_obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(cursor_obj, LV_OBJ_FLAG_HIDDEN);
}

/* ================================================================
 * 喇叭图标位图（10×10 RGB565，状态栏子对象）
 * ================================================================ */
#define ICON_BG  ((uint16_t)0x00C7)  /* 状态栏深蓝 #001A3E → RGB565 */
#define ICON_W   ((uint16_t)0xFFFF)  /* 纯白 */
#define ICON_G   ((uint16_t)0x8C71)  /* 中灰 #8C8C8C → RGB565 */
#define ICON_R   ((uint16_t)0xF800)  /* 纯红 */

static const uint16_t icon_sound_on_data[100] = {
    ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG,
    ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG,
    ICON_BG, ICON_BG, ICON_W,  ICON_W,  ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG,
    ICON_BG, ICON_BG, ICON_W,  ICON_W,  ICON_W,  ICON_W,  ICON_W,  ICON_BG, ICON_BG, ICON_BG,
    ICON_BG, ICON_BG, ICON_W,  ICON_W,  ICON_W,  ICON_W,  ICON_W,  ICON_W,  ICON_BG, ICON_BG,
    ICON_BG, ICON_BG, ICON_W,  ICON_W,  ICON_W,  ICON_W,  ICON_W,  ICON_W,  ICON_BG, ICON_BG,
    ICON_BG, ICON_BG, ICON_W,  ICON_W,  ICON_W,  ICON_W,  ICON_W,  ICON_BG, ICON_BG, ICON_BG,
    ICON_BG, ICON_BG, ICON_W,  ICON_W,  ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG,
    ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG,
    ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG,
};

static const uint16_t icon_sound_off_data[100] = {
    ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_R,
    ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_R,  ICON_BG,
    ICON_BG, ICON_BG, ICON_G,  ICON_G,  ICON_BG, ICON_BG, ICON_BG, ICON_R,  ICON_BG, ICON_BG,
    ICON_BG, ICON_BG, ICON_G,  ICON_G,  ICON_G,  ICON_G,  ICON_R,  ICON_BG, ICON_BG, ICON_BG,
    ICON_BG, ICON_BG, ICON_G,  ICON_G,  ICON_G,  ICON_R,  ICON_G,  ICON_G,  ICON_BG, ICON_BG,
    ICON_BG, ICON_BG, ICON_G,  ICON_G,  ICON_R,  ICON_G,  ICON_G,  ICON_G,  ICON_BG, ICON_BG,
    ICON_BG, ICON_BG, ICON_G,  ICON_R,  ICON_G,  ICON_G,  ICON_G,  ICON_BG, ICON_BG, ICON_BG,
    ICON_BG, ICON_BG, ICON_R,  ICON_G,  ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG,
    ICON_BG, ICON_R,  ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG,
    ICON_R,  ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG, ICON_BG,
};

static const lv_image_dsc_t icon_sound_on = {
    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                .flags = 0, .w = 10, .h = 10, .stride = 20, .reserved_2 = 0 },
    .data_size = sizeof(icon_sound_on_data),
    .data = (const uint8_t *)icon_sound_on_data,
    .reserved = NULL,
};
static const lv_image_dsc_t icon_sound_off = {
    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                .flags = 0, .w = 10, .h = 10, .stride = 20, .reserved_2 = 0 },
    .data_size = sizeof(icon_sound_off_data),
    .data = (const uint8_t *)icon_sound_off_data,
    .reserved = NULL,
};

static lv_obj_t *sound_icon = NULL;

void ui_sound_toggle(void) {
    g_sound_on = !g_sound_on;
    g_items[2].value = g_sound_on ? 0 : 1;
    audio_set_enabled(g_sound_on);
    ui_update_sound_icon();
}

void ui_update_sound_icon(void) {
    if (!sound_icon) {
        sound_icon = lv_image_create(statusbar_obj);
        lv_obj_align(sound_icon, LV_ALIGN_RIGHT_MID, -4, 0);
        lv_obj_set_style_pad_all(sound_icon, 0, 0);
    }
    lv_image_set_src(sound_icon, g_sound_on ? &icon_sound_on : &icon_sound_off);
}
static void statusbar_create(void) {
    lv_obj_t *top = lv_layer_top();
    statusbar_obj = lv_obj_create(top);
    lv_obj_t *bar = statusbar_obj;
    lv_obj_set_size(bar, 160, STATUSBAR_HEIGHT);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, UI_CLR_STATUSBAR, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    status_label = lv_label_create(bar);
    lv_obj_set_style_text_color(status_label, UI_CLR_TEXT, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_center(status_label);
    lv_obj_set_style_pad_top(status_label, 2, 0);
}

void ui_set_status(const char *text) { if (status_label) lv_label_set_text(status_label, text); }
void ui_statusbar_set_text(const char *fmt, ...) {
    if (!status_label) return;
    char buf[64]; va_list a; va_start(a, fmt); vsnprintf(buf, sizeof(buf), fmt, a); va_end(a);
    lv_label_set_text(status_label, buf);
}

/* ================================================================
 * 辅助：加载屏幕
 * ================================================================ */
static void load_screen(lv_obj_t *scr) {
    if (scr && scr != active_scr) {
        lv_screen_load(scr);
        active_scr = scr;
    }
}

/* ================================================================
 * 标题页 Screen
 * ================================================================ */
static lv_obj_t *create_title_screen(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_CLR_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "GOMOKU");
    lv_obj_set_style_text_color(t, UI_CLR_ACCENT, 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *s = lv_label_create(scr);
    lv_label_set_text(s, "Human vs AI");
    lv_obj_set_style_text_color(s, UI_CLR_TEXT, 0);
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 8);

    lv_obj_t *h = lv_label_create(scr);
    lv_label_set_text(h, "Press A");
    lv_obj_set_style_text_color(h, lv_color_hex(0x44CC88), 0);
    lv_obj_align(h, LV_ALIGN_CENTER, 0, 40);
    return scr;
}

void ui_show_title(void) {
    current_screen = UI_SCREEN_TITLE;
    /* 回到标题时强制复位音效为开启 */
    g_sound_on = true;
    g_items[2].value = 0;
    audio_set_enabled(true);
    ui_update_sound_icon();

    lv_obj_t *scr = create_title_screen();
    load_screen(scr);
    if (status_label) lv_label_set_text(status_label, "");
}

/* ================================================================
 * 设置页 Screen
 * ================================================================ */
static void settings_refresh(void);

static void settings_create_row(settings_item_t *it, int y, lv_obj_t *parent) {
    it->label_obj = lv_label_create(parent);
    lv_label_set_text(it->label_obj, it->label);
    lv_obj_set_style_text_font(it->label_obj, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(it->label_obj, UI_CLR_TEXT, 0);
    lv_obj_set_pos(it->label_obj, 12, y + 1);

    int opt_x = 60;
    for (int j = 0; j < it->n_opts; j++) {
        lv_obj_t *btn = lv_label_create(parent);
        lv_label_set_text(btn, it->opts[j]);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_hor(btn, 4, 0);
        lv_obj_set_style_pad_ver(btn, 2, 0);
        lv_obj_set_pos(btn, opt_x, y);
        int w = (it->n_opts == 5) ? 14 : (j == 0) ? 48 : (it->opts[j][0] == 'O') ? 22 : 18;
        opt_x += w + 4;
        it->btn_objs[j] = btn;
    }
}

static lv_obj_t *create_settings_screen(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_CLR_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    /* "Settings" 标题 */
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "Settings");
    lv_obj_set_style_text_color(t, UI_CLR_TEXT, 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 18);

    /* 分隔线 */
    lv_obj_t *sep = lv_obj_create(scr);
    lv_obj_set_size(sep, 136, 2);
    lv_obj_set_pos(sep, 12, 32);
    lv_obj_set_style_bg_color(sep, UI_CLR_TEXT, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);

    /* 三行设置: y=38, 59, 80 (行距 21px, 框高 18px, 间隔 3px) */
    g_items[0] = (settings_item_t){"Level", {"1","2","3","4","5"},5,2,NULL,{NULL,NULL,NULL,NULL,NULL}};
    g_items[1] = (settings_item_t){"First", {"Player","AI"},2,0,NULL,{NULL,NULL,NULL,NULL,NULL}};
    g_items[2] = (settings_item_t){"Sound", {"On","Off"},2,0,NULL,{NULL,NULL,NULL,NULL,NULL}};
    for (int i = 0; i < 3; i++)
        settings_create_row(&g_items[i], 38 + i * 21, scr);

    /* START 按钮: y=106, 作为第4个"行" */
    settings_start_btn = lv_label_create(scr);
    lv_label_set_text(settings_start_btn, "START");
    lv_obj_set_style_text_font(settings_start_btn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(settings_start_btn, UI_CLR_INVERT, 0);
    lv_obj_set_style_bg_opa(settings_start_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(settings_start_btn, UI_CLR_INVERT_TEXT, 0);
    lv_obj_set_style_pad_hor(settings_start_btn, 20, 0);
    lv_obj_set_style_pad_ver(settings_start_btn, 2, 0);
    lv_obj_set_style_radius(settings_start_btn, 0, 0);
    lv_obj_align(settings_start_btn, LV_ALIGN_BOTTOM_MID, 0, -6);

    return scr;
}

static void settings_refresh(void) {
    for (int i = 0; i < 3; i++) {
        settings_item_t *it = &g_items[i];
        bool active = (i == settings_row);
        lv_obj_set_style_text_color(it->label_obj, UI_CLR_TEXT, 0);
        lv_obj_set_style_bg_opa(it->label_obj, LV_OPA_TRANSP, 0);
        for (int j = 0; j < it->n_opts; j++) {
            bool selected = (j == it->value);
            bool focus_sel = (active && selected);
            /* 焦点选中: 反色 */
            lv_obj_set_style_text_color(it->btn_objs[j], focus_sel ? UI_CLR_INVERT_TEXT : UI_CLR_TEXT, 0);
            lv_obj_set_style_bg_color(it->btn_objs[j], focus_sel ? UI_CLR_INVERT : UI_CLR_BG, 0);
            lv_obj_set_style_bg_opa(it->btn_objs[j], focus_sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            /* 常驻选中(非焦点行): 下划线 */
            if (selected && !active) {
                lv_obj_set_style_border_side(it->btn_objs[j], LV_BORDER_SIDE_BOTTOM, 0);
                lv_obj_set_style_border_width(it->btn_objs[j], 1, 0);
                lv_obj_set_style_border_color(it->btn_objs[j], UI_CLR_TEXT, 0);
            } else {
                lv_obj_set_style_border_side(it->btn_objs[j], LV_BORDER_SIDE_NONE, 0);
                lv_obj_set_style_border_width(it->btn_objs[j], 0, 0);
            }
        }
    }
    if (settings_start_btn) {
        bool on = (settings_row == 3);
        lv_obj_set_style_text_color(settings_start_btn, on ? UI_CLR_INVERT_TEXT : UI_CLR_TEXT, 0);
        lv_obj_set_style_bg_color(settings_start_btn, on ? UI_CLR_INVERT : UI_CLR_BG, 0);
        lv_obj_set_style_bg_opa(settings_start_btn, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
}

void ui_show_settings(void) {
    current_screen = UI_SCREEN_DIFF_SEL;
    settings_row = 0;
    lv_obj_t *scr = create_settings_screen();
    settings_refresh();
    load_screen(scr);
    if (status_label) lv_label_set_text(status_label, "");
}

void ui_settings_move_row(int d)  {
    settings_row += d;
    if (settings_row < 0) settings_row = 3;
    if (settings_row > 3) settings_row = 0;
    settings_refresh();
}

void ui_settings_change_val(int d) {
    if (settings_row == 3) return;
    g_items[settings_row].value += d;
    if (g_items[settings_row].value < 0) g_items[settings_row].value = g_items[settings_row].n_opts-1;
    if (g_items[settings_row].value >= g_items[settings_row].n_opts) g_items[settings_row].value = 0;
    if (settings_row == 2) {
        g_sound_on = (g_items[2].value == 0);
        audio_set_enabled(g_sound_on);
        ui_update_sound_icon();  /* 刷新图标 */
    }
    settings_refresh();
}

int ui_settings_get_selected_row(void) { return settings_row; }
difficulty_t ui_settings_get_difficulty(void){ return (difficulty_t)g_items[0].value; }
bool ui_settings_player_is_black(void)        { return g_items[1].value == 0; }
bool ui_settings_sound_on(void)               { return g_sound_on; }

/* ================================================================
 * 游戏 Screen
 * ================================================================ */
static lv_obj_t *create_game_screen(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_CLR_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    canvas_board = lv_canvas_create(scr);
    lv_canvas_set_buffer(canvas_board, canvas_buf, CANVAS_SIZE, CANVAS_SIZE, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas_board, CANVAS_X, CANVAS_Y);
    lv_obj_set_style_border_width(canvas_board, 0, 0);
    lv_obj_clear_flag(canvas_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(canvas_board, LV_SCROLLBAR_MODE_OFF);
    canvas_draw_grid();

    cursor_create(scr);
    lv_timer_create(cursor_blink_cb, 500, NULL);
    return scr;
}

/* ================================================================
 * 结果 Screen
 * ================================================================ */
static lv_obj_t *create_result_screen(game_state_t state) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_CLR_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    const char *msg; lv_color_t clr;
    if (state == STATE_WIN)      { msg = "You Win!";  clr = UI_CLR_WIN; }
    else if (state == STATE_LOSE){ msg = "AI Wins";   clr = UI_CLR_CURSOR; }
    else                         { msg = "Draw";      clr = UI_CLR_ACCENT; }

    lv_obj_t *l = lv_label_create(scr);
    lv_label_set_text(l, msg);
    lv_obj_set_style_text_color(l, clr, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *h = lv_label_create(scr);
    lv_label_set_text(h, "A: Retry  B: Title");
    lv_obj_set_style_text_color(h, UI_CLR_TEXT, 0);
    lv_obj_align(h, LV_ALIGN_CENTER, 0, 20);
    return scr;
}

/* ================================================================
 * 公开 API
 * ================================================================ */
void ui_init(void) {
    ESP_LOGI(TAG, "init start");
    statusbar_create();
    ui_update_sound_icon();
    ui_show_title();
    ESP_LOGI(TAG, "init done");
}

void ui_show_game(void) {
    current_screen = UI_SCREEN_GAME;
    lv_obj_t *scr = create_game_screen();
    load_screen(scr);
    cursor_x = cursor_y = 7;
    cursor_visible = true;
    cursor_update_pos();
}

void ui_show_result(game_state_t state) {
    current_screen = UI_SCREEN_RESULT;
    lv_obj_t *scr = create_result_screen(state);
    load_screen(scr);
}

void ui_update_board(void) {
    if (!canvas_board) return;
    canvas_draw_grid();
    for (int y = 0; y < 15; y++)
        for (int x = 0; x < 15; x++) {
            Psquare sq = Square(x, y);
            if (!sq || sq->z == 0) continue;
            int cx = STONE_CX(x), cy = STONE_CY(y);
            canvas_fill_circle(cx, cy, BOARD_STONE_RADIUS,
                sq->z == 1 ? UI_CLR_BLACK_STONE : UI_CLR_WHITE_STONE);
        }
    lv_obj_invalidate(canvas_board);
}

void ui_draw_stone(int x, int y, bool black) {
    if (!canvas_board || x < 0 || x >= 15 || y < 0 || y >= 15) return;
    int cx = STONE_CX(x), cy = STONE_CY(y);
    canvas_fill_circle(cx, cy, BOARD_STONE_RADIUS,
        black ? UI_CLR_BLACK_STONE : UI_CLR_WHITE_STONE);
    lv_obj_invalidate(canvas_board);
}

void ui_move_cursor(int x, int y) {
    cursor_x = x; cursor_y = y;
    cursor_visible = true;
    cursor_update_pos();
}

ui_screen_t ui_get_current_screen(void) { return current_screen; }
lv_obj_t *ui_get_screen(void) { return active_scr; }
