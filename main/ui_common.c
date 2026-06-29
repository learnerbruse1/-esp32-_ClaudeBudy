/**
 * ui_common.c — 共用 UI 工具
 */
#include "ui_common.h"
#include "ui_font.h"

lv_obj_t *ui_make_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_text_font(scr, UI_FONT, 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_pad_all(scr, 4, 0);
    return scr;
}

void ui_load_screen(lv_obj_t *scr)
{
    lv_obj_t *old = lv_screen_active();
    lv_screen_load(scr);
    if (old && old != scr) lv_obj_del(old);
}

static void back_event_cb(lv_event_t *e)
{
    if (*((uint32_t *)lv_event_get_param(e)) == LV_KEY_ESC) {
        void (*cb)(void) = (void (*)(void))lv_event_get_user_data(e);
        if (cb) cb();
    }
}

void ui_add_back(lv_obj_t *obj, void (*cb)(void))
{
    lv_obj_add_event_cb(obj, back_event_cb, LV_EVENT_KEY, (void *)cb);
}
