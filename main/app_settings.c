/**
 * ================================================================
 * app_settings.c — 设置 (配网 / 设备信息 / 重置为默认)
 * ================================================================
 */
#include "app_settings.h"
#include "app_menu.h"
#include "ui_common.h"
#include "ui_font.h"
#include "board.h"
#include "config.h"
#include "claude_api.h"
#include "web_portal.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "sdcard.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_mac.h"

static const char *TAG = "SETTINGS";

/* ---------- 配网界面 ---------- */
void app_settings_show_portal(void)
{
    web_portal_start();   /* 幂等: 启动 SoftAP + 网页 */

    lv_obj_t *scr = ui_make_screen();
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "配网设置");
    lv_obj_set_style_text_color(t, lv_color_hex(0xf59e0b), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *box = lv_label_create(scr);
    lv_label_set_long_mode(box, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(box, 150);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 2);
    char buf[220];
    snprintf(buf, sizeof(buf),
             "手机连接热点:\n%s\n浏览器打开:\nhttp://192.168.4.1\n填好保存后自动重启\n\nB 返回",
             web_portal_ssid());
    lv_label_set_text(box, buf);

    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    ui_add_back(box, app_settings_show);
    lv_group_add_obj(board_group(), box);
    lv_group_focus_obj(box);

    ui_load_screen(scr);
}

/* ---------- 设备信息 (高亮列表, 上下浏览) ---------- */
static lv_obj_t *s_info_sd = NULL;   /* TF 卡那一行的 label, 异步更新 */

static void info_sd_task(void *arg)
{
    char line[72];
    uint64_t total = 0, freeb = 0;
    esp_err_t err = sdcard_get_info(&total, &freeb);
    if (err == ESP_OK && total > 0) {
        unsigned tot_mb = (unsigned)(total / (1024 * 1024));
        unsigned use_mb = (unsigned)((total - freeb) / (1024 * 1024));
        snprintf(line, sizeof(line), "TF卡  共%uMB 用%uMB", tot_mb, use_mb);
    } else {
        snprintf(line, sizeof(line), "TF卡  无卡/未格式化");
    }
    if (board_lock(2000)) {
        if (s_info_sd && lv_obj_is_valid(s_info_sd))   /* 离开界面后不写已释放对象 */
            lv_label_set_text(s_info_sd, line);
        board_unlock();
    }
    vTaskDelete(NULL);
}

static void info_back_click(lv_event_t *e) { (void)e; app_settings_show(); }

static void info_row(lv_obj_t *list, const char *text)
{
    lv_obj_t *b = lv_list_add_button(list, NULL, text);
    lv_obj_set_style_text_font(b, UI_FONT, 0);
    ui_add_back(b, app_settings_show);     /* 每行都能 B 返回 */
    lv_group_add_obj(board_group(), b);
}

static void show_info(void)
{
    lv_obj_t *scr = ui_make_screen();
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "设备信息 (上下浏览 B返回)");
    lv_obj_set_style_text_color(t, lv_color_hex(0xf59e0b), 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, 156, 108);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x161b22), 0);
    lv_obj_set_style_text_font(list, UI_FONT, 0);

    esp_chip_info_t ci; esp_chip_info(&ci);
    uint32_t flash_sz = 0; esp_flash_get_size(NULL, &flash_sz);
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *run = esp_ota_get_running_partition();
    const buddy_config_t *c = config_get();
    char ip[16]; net_get_ip(ip, sizeof(ip));
    uint8_t mac[6] = {0}; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char r[96];

    snprintf(r, sizeof(r), "芯片  ESP32 rev%d", ci.revision); info_row(list, r);
    snprintf(r, sizeof(r), "核心  %d核 240MHz", ci.cores); info_row(list, r);
    snprintf(r, sizeof(r), "Flash  %uMB", (unsigned)(flash_sz / (1024 * 1024))); info_row(list, r);
    snprintf(r, sizeof(r), "内存  %u/%uKB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024)); info_row(list, r);
    snprintf(r, sizeof(r), "PSRAM  %u/%uKB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024)); info_row(list, r);
    snprintf(r, sizeof(r), "MAC %02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); info_row(list, r);
    snprintf(r, sizeof(r), "WiFi  %s", net_sta_connected() ? "已连接" : "未连接"); info_row(list, r);
    snprintf(r, sizeof(r), "IP  %s", ip); info_row(list, r);
    snprintf(r, sizeof(r), "信号  %ddBm", net_rssi()); info_row(list, r);
    snprintf(r, sizeof(r), "模型  %s", c->model[0] ? c->model : "(未设)"); info_row(list, r);
    snprintf(r, sizeof(r), "应用  %uKB", (unsigned)(run ? run->size / 1024 : 0)); info_row(list, r);
    snprintf(r, sizeof(r), "固件  %s", app ? app->version : "?"); info_row(list, r);
    snprintf(r, sizeof(r), "构建  %s", app ? app->date : "?"); info_row(list, r);

    lv_obj_t *sdb = lv_list_add_button(list, NULL, "TF卡  读取中...");
    lv_obj_set_style_text_font(sdb, UI_FONT, 0);
    ui_add_back(sdb, app_settings_show);
    lv_group_add_obj(board_group(), sdb);
    s_info_sd = lv_obj_get_child(sdb, 0);   /* 该 button 的 label 子对象 */

    lv_obj_t *bk = lv_list_add_button(list, NULL, "返回");
    lv_obj_set_style_text_font(bk, UI_FONT, 0);
    lv_obj_add_event_cb(bk, info_back_click, LV_EVENT_CLICKED, NULL);
    ui_add_back(bk, app_settings_show);
    lv_group_add_obj(board_group(), bk);

    lv_group_focus_obj(lv_obj_get_child(list, 0));
    ui_load_screen(scr);

    xTaskCreatePinnedToCore(info_sd_task, "sdinfo", 6144, NULL, 5, NULL, 0);
}

/* ---------- 重置为默认 ---------- */
static void do_reset(void)
{
    buddy_config_t c;
    config_load_defaults(&c);
    config_save(&c);
    ESP_LOGW(TAG, "配置已重置为默认, 重启");
    esp_restart();
}

static void reset_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == 0) do_reset();
    else app_settings_show();
}

static void show_reset_confirm(void)
{
    lv_obj_t *scr = ui_make_screen();
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "重置为默认?");
    lv_obj_set_style_text_color(t, lv_color_hex(0xef4444), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 2);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(sub, 150);
    lv_label_set_text(sub, "将清除已保存的 Wi-Fi 和 API 配置");
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, 150, 56);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(list, UI_FONT, 0);
    const char *items[] = { "确定重置", "取消" };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *b = lv_list_add_button(list, NULL, items[i]);
        lv_obj_set_style_text_font(b, UI_FONT, 0);
        lv_obj_add_event_cb(b, reset_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_add_back(b, app_settings_show);
        lv_group_add_obj(board_group(), b);
        if (i == 1) lv_group_focus_obj(b);   /* 默认聚焦"取消", 更安全 */
    }
    ui_load_screen(scr);
}

/* ---------- 格式化 TF 卡 (双重确认) ---------- */
static lv_obj_t *s_fmt_status = NULL;
static lv_obj_t *s_fmt_spin   = NULL;
static char s_fmt_err[128];
static volatile bool s_formatting = false;

static void fmt_back(void)
{
    if (s_formatting) return;   /* 格式化进行中禁止返回 */
    app_settings_show();
}

static void fmt_task(void *arg)
{
    s_fmt_err[0] = '\0';
    esp_err_t err = sdcard_format_fat32(s_fmt_err, sizeof(s_fmt_err));
    if (board_lock(3000)) {
        if (s_fmt_spin) { lv_obj_del(s_fmt_spin); s_fmt_spin = NULL; }
        if (err == ESP_OK) {
            lv_label_set_text(s_fmt_status, "格式化完成!\n\nB 返回");
            lv_obj_set_style_text_color(s_fmt_status, lv_color_hex(0x22c55e), 0);
        } else {
            char m[200];
            snprintf(m, sizeof(m), "失败:\n%s\n\nB 返回", s_fmt_err[0] ? s_fmt_err : "未知错误");
            lv_label_set_text(s_fmt_status, m);
            lv_obj_set_style_text_color(s_fmt_status, lv_color_hex(0xef4444), 0);
        }
        board_unlock();
    }
    s_formatting = false;
    vTaskDelete(NULL);
}

static void show_formatting(void)
{
    s_formatting = true;
    lv_obj_t *scr = ui_make_screen();

    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "格式化中");
    lv_obj_set_style_text_color(t, lv_color_hex(0xf59e0b), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);

    s_fmt_spin = lv_spinner_create(scr);
    lv_obj_set_size(s_fmt_spin, 22, 22);
    lv_obj_align(s_fmt_spin, LV_ALIGN_TOP_RIGHT, -2, 0);

    s_fmt_status = lv_label_create(scr);
    lv_label_set_long_mode(s_fmt_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_fmt_status, 150);
    lv_label_set_text(s_fmt_status, "正在格式化为 FAT32\n请勿断电…");
    lv_obj_align(s_fmt_status, LV_ALIGN_CENTER, 0, 4);
    lv_obj_add_flag(s_fmt_status, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    ui_add_back(s_fmt_status, fmt_back);
    lv_group_add_obj(board_group(), s_fmt_status);
    lv_group_focus_obj(s_fmt_status);

    ui_load_screen(scr);
    xTaskCreatePinnedToCore(fmt_task, "sdfmt", 8192, NULL, 5, NULL, 0);
}

/* 通用两项确认列表 */
static void build_confirm(const char *title, const char *sub, const char *ok_text,
                          lv_event_cb_t on_click)
{
    lv_obj_t *scr = ui_make_screen();
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_hex(0xef4444), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 2);

    lv_obj_t *s = lv_label_create(scr);
    lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s, 150);
    lv_label_set_text(s, sub);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 22);

    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, 150, 52);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(list, UI_FONT, 0);
    const char *items[] = { ok_text, "取消" };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *b = lv_list_add_button(list, NULL, items[i]);
        lv_obj_set_style_text_font(b, UI_FONT, 0);
        lv_obj_add_event_cb(b, on_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_add_back(b, app_settings_show);
        lv_group_add_obj(board_group(), b);
        if (i == 1) lv_group_focus_obj(b);   /* 默认聚焦"取消" */
    }
    ui_load_screen(scr);
}

static void fmt_confirm2_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == 0) show_formatting();   /* 第二次确认 → 开始格式化 */
    else app_settings_show();
}

static void fmt_confirm1_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == 0)   /* 第一次确认 → 再问一次 */
        build_confirm("再次确认", "真的要格式化? 卡上所有数据将被清空!", "确定格式化", fmt_confirm2_click);
    else
        app_settings_show();
}

static void fmt_format_card(void)
{
    build_confirm("格式化TF卡", "格式化为 FAT32，会清空卡上所有数据。", "继续", fmt_confirm1_click);
}

/* ---------- 设置主菜单 ---------- */
static void settings_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    switch (idx) {
        case 0: app_settings_show_portal(); break;
        case 1: show_info(); break;
        case 2: fmt_format_card(); break;
        case 3: show_reset_confirm(); break;
        default: app_menu_show(); break;
    }
}

void app_settings_show(void)
{
    lv_obj_t *scr = ui_make_screen();
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "设置");
    lv_obj_set_style_text_color(t, lv_color_hex(0xf59e0b), 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, 152, 104);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x161b22), 0);
    lv_obj_set_style_text_font(list, UI_FONT, 0);

    const char *items[] = { "配网设置", "设备信息", "格式化TF卡", "重置为默认", "返回主菜单" };
    for (int i = 0; i < 5; i++) {
        lv_obj_t *b = lv_list_add_button(list, NULL, items[i]);
        lv_obj_set_style_text_font(b, UI_FONT, 0);
        lv_obj_add_event_cb(b, settings_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_add_back(b, app_menu_show);
        lv_group_add_obj(board_group(), b);
        if (i == 0) lv_group_focus_obj(b);
    }
    ui_load_screen(scr);
}
