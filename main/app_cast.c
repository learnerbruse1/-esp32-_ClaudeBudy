/**
 * ================================================================
 * app_cast.c — WiFi 投屏 (屏幕镜像到浏览器)
 * ================================================================
 * 用 lv_snapshot 抓取当前 LVGL 屏幕 → 转成 BMP → 通过内置 HTTP 服务
 * 提供给浏览器, 网页每 0.5s 刷新一次 (约 2fps)。
 * 启动后服务常驻, 在设备上正常操作, 浏览器画面会跟着同步。
 * (说明: 投屏镜像本机所有界面。)
 * ================================================================
 */
#include "app_cast.h"
#include "app_menu.h"
#include "ui_common.h"
#include "ui_font.h"
#include "board.h"
#include "claude_api.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"

static const char *TAG = "CAST";
static httpd_handle_t s_httpd = NULL;
static lv_obj_t *s_status = NULL;

static esp_err_t root_handler(httpd_req_t *req)
{
    static const char html[] =
        "<!doctype html><meta charset=utf-8><title>XiaoMiao Cast</title>"
        "<body style='margin:0;background:#000;text-align:center'>"
        "<img id=s style='width:100vw;max-width:480px;image-rendering:pixelated'>"
        "<script>setInterval(function(){document.getElementById('s').src='/screen.bmp?t='+Date.now()},500)</script>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

/* 抓屏 → BMP24 (top-down) */
static esp_err_t bmp_handler(httpd_req_t *req)
{
    if (!board_lock(1000)) { httpd_resp_send_500(req); return ESP_OK; }
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    board_unlock();
    if (!snap) { httpd_resp_send_500(req); return ESP_OK; }

    int w = snap->header.w, h = snap->header.h;
    uint32_t stride = snap->header.stride;
    int imgsz = w * 3 * h, total = 54 + imgsz;
    uint8_t *bmp = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!bmp) { lv_draw_buf_destroy(snap); httpd_resp_send_500(req); return ESP_OK; }

    memset(bmp, 0, 54);
    bmp[0] = 'B'; bmp[1] = 'M';
    *(uint32_t *)(bmp + 2)  = total;
    *(uint32_t *)(bmp + 10) = 54;
    *(uint32_t *)(bmp + 14) = 40;
    *(int32_t  *)(bmp + 18) = w;
    *(int32_t  *)(bmp + 22) = -h;        /* 负高度 = 自上而下 */
    *(uint16_t *)(bmp + 26) = 1;
    *(uint16_t *)(bmp + 28) = 24;
    *(uint32_t *)(bmp + 34) = imgsz;

    uint8_t *px = bmp + 54;
    for (int y = 0; y < h; y++) {
        uint16_t *rowp = (uint16_t *)((uint8_t *)snap->data + (size_t)y * stride);
        for (int x = 0; x < w; x++) {
            uint16_t c = rowp[x];
            *px++ = (uint8_t)((c & 0x1F) << 3);          /* B */
            *px++ = (uint8_t)(((c >> 5) & 0x3F) << 2);   /* G */
            *px++ = (uint8_t)(((c >> 11) & 0x1F) << 3);  /* R */
        }
    }
    lv_draw_buf_destroy(snap);

    httpd_resp_set_type(req, "image/bmp");
    esp_err_t r = httpd_resp_send(req, (char *)bmp, total);
    free(bmp);
    return r;
}

static void cast_start_server(void)
{
    if (s_httpd) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;
    if (httpd_start(&s_httpd, &cfg) == ESP_OK) {
        httpd_uri_t a = { .uri = "/",           .method = HTTP_GET, .handler = root_handler };
        httpd_uri_t b = { .uri = "/screen.bmp", .method = HTTP_GET, .handler = bmp_handler };
        httpd_register_uri_handler(s_httpd, &a);
        httpd_register_uri_handler(s_httpd, &b);
        ESP_LOGI(TAG, "投屏服务已启动");
    }
}

static void cast_task(void *arg)
{
    esp_err_t err = net_sta_connect(20000);
    char msg[128];
    if (err == ESP_OK) {
        cast_start_server();
        char ip[16]; net_get_ip(ip, sizeof(ip));
        snprintf(msg, sizeof(msg),
                 "投屏已开启!\n浏览器打开:\nhttp://%s/\n现在可正常使用设备,\n画面会同步。 B 返回", ip);
    } else {
        snprintf(msg, sizeof(msg), "联网失败\n请先到 设置→配网\n配置 Wi-Fi。 B 返回");
    }
    if (board_lock(2000)) {
        if (s_status && lv_obj_is_valid(s_status)) lv_label_set_text(s_status, msg);
        board_unlock();
    }
    vTaskDelete(NULL);
}

static void cast_key(lv_event_t *e)
{
    if (*((uint32_t *)lv_event_get_param(e)) == LV_KEY_ESC) app_menu_show();
}

void app_cast_show(void)
{
    lv_obj_t *scr = ui_make_screen();
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "WiFi 投屏");
    lv_obj_set_style_text_color(t, lv_color_hex(0xf59e0b), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);

    s_status = lv_label_create(scr);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_status, 150);
    lv_label_set_text(s_status, net_sta_connected() ? "启动中..." : "连接 Wi-Fi 中...");
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 2);
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(s_status, cast_key, LV_EVENT_KEY, NULL);
    lv_group_add_obj(board_group(), s_status);
    lv_group_focus_obj(s_status);

    ui_load_screen(scr);
    xTaskCreatePinnedToCore(cast_task, "cast", 8192, NULL, 5, NULL, 0);
}
