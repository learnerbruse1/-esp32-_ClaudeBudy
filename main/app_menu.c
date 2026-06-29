/**
 * ================================================================
 * app_menu.c — 开机主菜单
 * ================================================================
 */
#include "app_menu.h"
#include "app_claude.h"
#include "app_settings.h"
#include "app_cast.h"
#include "app_tools.h"
#include "board.h"
#include "ui_common.h"
#include "ui_font.h"

#include "esp_log.h"

static const char *TAG = "MENU";

static void menu_event_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    switch (idx) {
        case 0: app_claude_show(); break;        /* Claude 伙伴 */
        case 1: app_cast_show(); break;          /* 投屏 */
        case 2: app_tools_show(); break;         /* 工具箱 */
        default: app_settings_show(); break;     /* 设置 */
    }
}

void app_menu_show(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_text_font(scr, UI_FONT, 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(0xffffff), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "小喵掌机");
    lv_obj_set_style_text_color(title, lv_color_hex(0xf59e0b), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, 150, 100);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x161b22), 0);
    lv_obj_set_style_text_font(list, UI_FONT, 0);

    const char *items[] = { "Claude 伙伴", "投屏", "工具箱", "设置" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_list_add_button(list, NULL, items[i]);
        lv_obj_set_style_text_font(btn, UI_FONT, 0);
        lv_obj_add_event_cb(btn, menu_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_group_add_obj(board_group(), btn);
        if (i == 0) lv_group_focus_obj(btn);
    }

    ui_load_screen(scr);
    ESP_LOGI(TAG, "主菜单已显示");
}
