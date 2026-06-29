/**
 * ================================================================
 * app_tools.c — 工具箱 (手电筒 / 秒表)
 * ================================================================
 */
#include "app_tools.h"
#include "app_menu.h"
#include "ui_common.h"
#include "ui_font.h"
#include "board.h"

#include "esp_timer.h"

/* ---------------- 手电筒 (全白屏) ---------------- */
static void flashlight_key(lv_event_t *e)
{
    if (*((uint32_t *)lv_event_get_param(e)) == LV_KEY_ESC) app_tools_show();
}

static void show_flashlight(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xffffff), 0);   /* 全白照明 */
    lv_obj_set_style_text_font(scr, UI_FONT, 0);

    lv_obj_t *l = lv_label_create(scr);
    lv_label_set_text(l, "手电筒  B 返回");
    lv_obj_set_style_text_color(l, lv_color_hex(0x888888), 0);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_add_flag(l, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(l, flashlight_key, LV_EVENT_KEY, NULL);
    lv_group_add_obj(board_group(), l);
    lv_group_focus_obj(l);

    ui_load_screen(scr);
}

/* ---------------- 秒表 ---------------- */
static lv_obj_t  *s_sw_label = NULL;
static lv_timer_t *s_sw_timer = NULL;
static bool      s_sw_run = false;
static int64_t   s_sw_base = 0;       /* 累计起点 (us) */
static int64_t   s_sw_acc = 0;        /* 暂停时累计 (us) */

static void sw_update_label(int64_t us)
{
    if (!s_sw_label || !lv_obj_is_valid(s_sw_label)) return;
    int t = (int)(us / 100000);          /* 0.1s 单位 */
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d.%d", t / 600, (t / 10) % 60, t % 10);
    lv_label_set_text(s_sw_label, buf);
}

static void sw_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_sw_run)
        sw_update_label(s_sw_acc + (esp_timer_get_time() - s_sw_base));
}

static void sw_key(lv_event_t *e)
{
    uint32_t key = *((uint32_t *)lv_event_get_param(e));
    if (key == LV_KEY_ENTER) {                 /* A: 启/停 */
        if (s_sw_run) { s_sw_acc += esp_timer_get_time() - s_sw_base; s_sw_run = false; }
        else { s_sw_base = esp_timer_get_time(); s_sw_run = true; }
    } else if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {  /* 左右: 清零 */
        s_sw_run = false; s_sw_acc = 0; sw_update_label(0);
    } else if (key == LV_KEY_ESC) {            /* B: 返回 */
        if (s_sw_timer) { lv_timer_delete(s_sw_timer); s_sw_timer = NULL; }
        s_sw_label = NULL;
        app_tools_show();
    }
}

static void show_stopwatch(void)
{
    lv_obj_t *scr = ui_make_screen();
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "秒表");
    lv_obj_set_style_text_color(t, lv_color_hex(0xf59e0b), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 2);

    s_sw_label = lv_label_create(scr);
    lv_label_set_text(s_sw_label, "00:00.0");
    lv_obj_set_style_text_font(s_sw_label, UI_FONT, 0);
    lv_obj_align(s_sw_label, LV_ALIGN_CENTER, 0, -4);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "A 启/停   左右 清零   B 返回");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6b7280), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, 0);

    s_sw_run = false; s_sw_acc = 0;
    lv_obj_add_flag(hint, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(hint, sw_key, LV_EVENT_KEY, NULL);
    lv_group_add_obj(board_group(), hint);
    lv_group_focus_obj(hint);

    if (s_sw_timer) lv_timer_delete(s_sw_timer);
    s_sw_timer = lv_timer_create(sw_timer_cb, 100, NULL);

    ui_load_screen(scr);
}

/* ---------------- 工具箱主菜单 ---------------- */
static void tools_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == 0) show_flashlight();
    else if (idx == 1) show_stopwatch();
    else app_menu_show();
}

void app_tools_show(void)
{
    lv_obj_t *scr = ui_make_screen();
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "工具箱");
    lv_obj_set_style_text_color(t, lv_color_hex(0xf59e0b), 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, 152, 100);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x161b22), 0);
    lv_obj_set_style_text_font(list, UI_FONT, 0);

    const char *items[] = { "手电筒", "秒表", "返回主菜单" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_list_add_button(list, NULL, items[i]);
        lv_obj_set_style_text_font(b, UI_FONT, 0);
        lv_obj_add_event_cb(b, tools_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_add_back(b, app_menu_show);
        lv_group_add_obj(board_group(), b);
        if (i == 0) lv_group_focus_obj(b);
    }
    ui_load_screen(scr);
}
