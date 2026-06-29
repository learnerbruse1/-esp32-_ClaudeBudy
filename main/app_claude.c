/**
 * ================================================================
 * app_claude.c — Claude Buddy 界面 (预设问题 / 回复 / 设备信息 / 配网)
 * ================================================================
 * 交互: 上/下=移动  A=确认  B=返回。
 * 网络请求在后台任务执行, 完成后持 LVGL 锁更新界面。
 * ================================================================
 */
#include "app_claude.h"
#include "app_menu.h"
#include "app_settings.h"
#include "board.h"
#include "config.h"
#include "claude_api.h"
#include "ui_font.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static lv_obj_t *s_status_lbl = NULL;   /* 预设页顶部状态行 */
static volatile bool s_busy = false;

static char s_reply[4096];
static char s_err[200];

/* 回复页控件 (供后台任务更新) */
static lv_obj_t *s_reply_scr   = NULL;
static lv_obj_t *s_reply_label = NULL;
static lv_obj_t *s_reply_status= NULL;
static lv_obj_t *s_reply_spin  = NULL;

/* 切屏 + 释放旧屏 */
static void load_screen(lv_obj_t *scr)
{
    lv_obj_t *old = lv_screen_active();
    lv_screen_load(scr);
    if (old && old != scr) lv_obj_del(old);
}

static lv_obj_t *make_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_text_font(scr, UI_FONT, 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_pad_all(scr, 4, 0);
    return scr;
}

/* ================================================================
 * 回复页
 * ================================================================ */
static void reply_key_cb(lv_event_t *e)
{
    uint32_t key = *((uint32_t *)lv_event_get_param(e));
    lv_obj_t *cont = lv_event_get_target(e);
    if (key == LV_KEY_NEXT || key == LV_KEY_RIGHT)        lv_obj_scroll_by(cont, 0, -28, LV_ANIM_ON);
    else if (key == LV_KEY_PREV || key == LV_KEY_LEFT)    lv_obj_scroll_by(cont, 0,  28, LV_ANIM_ON);
    else if (key == LV_KEY_ESC)                           app_claude_show();  /* 返回预设页 */
}

static void show_reply_screen(const char *title)
{
    lv_obj_t *scr = make_screen();
    s_reply_scr = scr;

    s_reply_status = lv_label_create(scr);
    lv_label_set_text(s_reply_status, title);
    lv_obj_set_style_text_color(s_reply_status, lv_color_hex(0xf59e0b), 0);
    lv_obj_align(s_reply_status, LV_ALIGN_TOP_LEFT, 0, 0);

    s_reply_spin = lv_spinner_create(scr);
    lv_obj_set_size(s_reply_spin, 18, 18);
    lv_obj_align(s_reply_spin, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 152, 86);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x161b22), 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 4, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);

    s_reply_label = lv_label_create(cont);
    lv_label_set_long_mode(s_reply_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_reply_label, 142);
    lv_label_set_text(s_reply_label, "正在思考…");

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "上/下滚动  B返回");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6b7280), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_add_event_cb(cont, reply_key_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(board_group(), cont);
    lv_group_focus_obj(cont);

    load_screen(scr);
}

/* 后台任务: 调用 Claude, 完成后更新回复页 */
static void ask_task(void *arg)
{
    char *prompt = (char *)arg;
    s_reply[0] = '\0'; s_err[0] = '\0';
    claude_status_t st = claude_ask(prompt, s_reply, sizeof(s_reply), s_err, sizeof(s_err));
    free(prompt);

    if (board_lock(2000)) {
        if (s_reply_spin && lv_obj_is_valid(s_reply_spin)) lv_obj_del(s_reply_spin);
        s_reply_spin = NULL;
        if (s_reply_status && lv_obj_is_valid(s_reply_status) &&
            s_reply_label && lv_obj_is_valid(s_reply_label)) {
            if (st == CLAUDE_OK) {
                lv_label_set_text(s_reply_status, "Claude");
                lv_label_set_text(s_reply_label, s_reply[0] ? s_reply : "(空回复)");
            } else {
                lv_label_set_text(s_reply_status, "出错");
                lv_obj_set_style_text_color(s_reply_status, lv_color_hex(0xef4444), 0);
                char msg[256];
                snprintf(msg, sizeof(msg), "请求失败:\n%s", s_err[0] ? s_err : "未知错误");
                lv_label_set_text(s_reply_label, msg);
            }
        }
        board_unlock();
    }
    s_busy = false;
    vTaskDelete(NULL);
}

static void start_ask(const char *prompt, const char *title)
{
    if (s_busy) return;
    s_busy = true;
    show_reply_screen(title);
    char *copy = strdup(prompt);
    /* 大栈: TLS 握手需要较多栈空间; 固定到 core 0, LVGL 在 core 1 */
    if (xTaskCreatePinnedToCore(ask_task, "ask", 8192, copy, 5, NULL, 0) != pdPASS) {
        free(copy);
        s_busy = false;
    }
}

/* ================================================================
 * 未配置提示页
 * ================================================================ */
static void needcfg_key(lv_event_t *e)
{
    if (*((uint32_t *)lv_event_get_param(e)) == LV_KEY_ESC) app_menu_show();
}

static void needcfg_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == 0) app_settings_show_portal();   /* 去配网 */
    else app_menu_show();
}

static void show_need_config(void)
{
    lv_obj_t *scr = make_screen();
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "Claude 未配置");
    lv_obj_set_style_text_color(t, lv_color_hex(0xf59e0b), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *box = lv_label_create(scr);
    lv_label_set_long_mode(box, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(box, 150);
    lv_label_set_text(box, "需要先连接 Wi-Fi 并填写 API 信息");
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 22);

    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, 150, 52);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(list, UI_FONT, 0);
    const char *items[] = { "去配网", "返回主菜单" };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *b = lv_list_add_button(list, NULL, items[i]);
        lv_obj_set_style_text_font(b, UI_FONT, 0);
        lv_obj_add_event_cb(b, needcfg_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, needcfg_key, LV_EVENT_KEY, NULL);
        lv_group_add_obj(board_group(), b);
        if (i == 0) lv_group_focus_obj(b);
    }
    load_screen(scr);
}

/* ================================================================
 * 预设问题主页
 * ================================================================ */
static void preset_key_cb(lv_event_t *e)
{
    if (*((uint32_t *)lv_event_get_param(e)) == LV_KEY_ESC) app_menu_show();   /* B → 回主菜单 */
}

static void preset_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    int n = config_preset_count();
    if (idx < n) {
        const preset_t *p = config_preset(idx);
        start_ask(p->prompt, p->title);
    } else {
        app_menu_show();   /* 返回主菜单 */
    }
}

/* 连接 Wi-Fi 的后台任务 */
static void connect_task(void *arg)
{
    esp_err_t err = net_sta_connect(20000);
    if (board_lock(2000)) {
        if (s_status_lbl) {
            if (err == ESP_OK) {
                char ip[16]; net_get_ip(ip, sizeof(ip));
                char s[40]; snprintf(s, sizeof(s), "已联网 %s", ip);
                lv_label_set_text(s_status_lbl, s);
                lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x22c55e), 0);
            } else {
                lv_label_set_text(s_status_lbl, "联网失败, 长按B重配");
                lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xef4444), 0);
            }
        }
        board_unlock();
    }
    vTaskDelete(NULL);
}

void app_claude_show(void)
{
    if (!config_is_ready()) { show_need_config(); return; }

    lv_obj_t *scr = make_screen();

    s_status_lbl = lv_label_create(scr);
    lv_label_set_text(s_status_lbl, net_sta_connected() ? "已联网" : "连接 Wi-Fi…");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x9ca3af), 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, 152, 104);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x161b22), 0);
    lv_obj_set_style_text_font(list, UI_FONT, 0);

    int n = config_preset_count();
    for (int i = 0; i < n; i++) {
        const preset_t *p = config_preset(i);
        lv_obj_t *btn = lv_list_add_button(list, NULL, p->title);
        lv_obj_set_style_text_font(btn, UI_FONT, 0);
        lv_obj_add_event_cb(btn, preset_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, preset_key_cb, LV_EVENT_KEY, NULL);
        lv_group_add_obj(board_group(), btn);
        if (i == 0) lv_group_focus_obj(btn);
    }
    /* 附加项: 返回主菜单 */
    lv_obj_t *bm = lv_list_add_button(list, NULL, "返回主菜单");
    lv_obj_set_style_text_font(bm, UI_FONT, 0);
    lv_obj_add_event_cb(bm, preset_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)n);
    lv_obj_add_event_cb(bm, preset_key_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(board_group(), bm);

    load_screen(scr);

    /* 若尚未联网, 启动连接任务 */
    if (!net_sta_connected()) {
        xTaskCreatePinnedToCore(connect_task, "wifi", 8192, NULL, 5, NULL, 0);
    }
}
