/**
 * ================================================================
 * web_portal.c — SoftAP 配网门户
 * ================================================================
 * 手机连接热点 "ClaudeBuddy-XXXX" → 浏览器打开 http://192.168.4.1
 * → 填写 Wi-Fi / API 端点 / 密钥 / 模型 → 保存后自动重启。
 * ================================================================
 */
#include "web_portal.h"
#include "config.h"
#include "claude_api.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_system.h"

static const char *TAG = "PORTAL";
static char s_ap_ssid[24];
static httpd_handle_t s_httpd = NULL;

/* ---- 工具: URL 解码 / 表单取值 / HTML 转义 ---- */
static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *dst, const char *src, size_t dstsz)
{
    size_t di = 0;
    for (size_t i = 0; src[i] && di + 1 < dstsz; i++) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            int hi = hexv(src[i+1]), lo = hexv(src[i+2]);
            if (hi >= 0 && lo >= 0) { dst[di++] = (char)((hi << 4) | lo); i += 2; continue; }
        }
        dst[di++] = (src[i] == '+') ? ' ' : src[i];
    }
    dst[di] = '\0';
}

static bool form_get(const char *body, const char *key, char *out, size_t osz)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "%s=", key);
    const char *p = strstr(body, pat);
    while (p && !(p == body || p[-1] == '&')) p = strstr(p + 1, pat);
    if (!p) return false;
    p += strlen(pat);
    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    char tmp[600];
    if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
    memcpy(tmp, p, len);
    tmp[len] = '\0';
    url_decode(out, tmp, osz);
    return true;
}

static void html_attr(httpd_req_t *req, const char *s)
{
    /* 转义 HTML 属性中的特殊字符 */
    char buf[8];
    for (; *s; s++) {
        switch (*s) {
            case '&': httpd_resp_sendstr_chunk(req, "&amp;"); break;
            case '<': httpd_resp_sendstr_chunk(req, "&lt;");  break;
            case '>': httpd_resp_sendstr_chunk(req, "&gt;");  break;
            case '"': httpd_resp_sendstr_chunk(req, "&quot;"); break;
            default:  buf[0] = *s; buf[1] = '\0'; httpd_resp_sendstr_chunk(req, buf); break;
        }
    }
}

/* ---- GET / : 配置表单 ---- */
static esp_err_t root_get(httpd_req_t *req)
{
    const buddy_config_t *c = config_get();
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Claude Buddy 配置</title><style>"
        "body{font-family:sans-serif;max-width:480px;margin:0 auto;padding:1em;background:#101520;color:#eee}"
        "h1{font-size:1.3em} label{display:block;margin:.8em 0 .2em;font-size:.9em}"
        "input{width:100%;box-sizing:border-box;padding:.5em;border-radius:6px;border:1px solid #444;background:#1c2230;color:#fff}"
        "button{margin-top:1.2em;width:100%;padding:.7em;border:0;border-radius:8px;background:#d97706;color:#fff;font-size:1em}"
        ".hint{color:#89a;font-size:.78em;margin-top:.2em}</style></head><body>"
        "<h1>🤖 Claude Buddy 配置</h1><form method='POST' action='/save'>");

    httpd_resp_sendstr_chunk(req, "<label>Wi-Fi 名称 (2.4G)</label><input name='ssid' value='");
    html_attr(req, c->wifi_ssid);
    httpd_resp_sendstr_chunk(req, "'>");

    httpd_resp_sendstr_chunk(req, "<label>Wi-Fi 密码</label><input name='pass' type='text' value='");
    html_attr(req, c->wifi_pass);
    httpd_resp_sendstr_chunk(req, "'>");

    httpd_resp_sendstr_chunk(req, "<label>API 地址 (不带结尾/，需兼容 /v1/messages)</label><input name='url' value='");
    html_attr(req, c->api_base_url);
    httpd_resp_sendstr_chunk(req, "'><div class='hint'>官方: https://api.anthropic.com，或填你的中转地址</div>");

    httpd_resp_sendstr_chunk(req, "<label>API 密钥</label><input name='key' value='");
    html_attr(req, c->api_key);
    httpd_resp_sendstr_chunk(req, "'>");

    httpd_resp_sendstr_chunk(req, "<label>模型</label><input name='model' value='");
    html_attr(req, c->model);
    httpd_resp_sendstr_chunk(req, "'>");

    httpd_resp_sendstr_chunk(req,
        "<button type='submit'>保存并重启</button></form>"
        "<p class='hint'>保存后设备会重启并尝试联网。要重新配置：开机时长按 B 键。</p>"
        "</body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

/* 延时重启任务 (让 HTTP 响应先发出) */
static void reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

/* ---- POST /save ---- */
static esp_err_t save_post(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 4096) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad"); return ESP_FAIL; }
    char *body = malloc(total + 1);
    if (!body) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "mem"); return ESP_FAIL; }
    int rd = 0;
    while (rd < total) {
        int r = httpd_req_recv(req, body + rd, total - rd);
        if (r <= 0) { free(body); return ESP_FAIL; }
        rd += r;
    }
    body[total] = '\0';

    buddy_config_t c = *config_get();
    form_get(body, "ssid",  c.wifi_ssid,    sizeof(c.wifi_ssid));
    form_get(body, "pass",  c.wifi_pass,    sizeof(c.wifi_pass));
    form_get(body, "url",   c.api_base_url, sizeof(c.api_base_url));
    form_get(body, "key",   c.api_key,      sizeof(c.api_key));
    form_get(body, "model", c.model,        sizeof(c.model));
    free(body);

    esp_err_t err = config_save(&c);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "<meta charset='utf-8'><body style='font-family:sans-serif;background:#101520;color:#eee'>"
                                "<h2>✅ 已保存，正在重启…</h2><p>设备将尝试连接 Wi-Fi。</p></body>");
        xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    } else {
        httpd_resp_sendstr(req, "<meta charset='utf-8'><body><h2>❌ 保存失败</h2></body>");
    }
    return ESP_OK;
}

const char *web_portal_ssid(void) { return s_ap_ssid; }

void web_portal_start(void)
{
    if (s_httpd) return;   /* 已启动, 幂等 */
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "ClaudeBuddy-%02X%02X", mac[4], mac[5]);

    net_ap_start(s_ap_ssid, "");   /* 开放热点, 方便连接 */

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    if (httpd_start(&s_httpd, &cfg) == ESP_OK) {
        httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get };
        httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = save_post };
        httpd_register_uri_handler(s_httpd, &root);
        httpd_register_uri_handler(s_httpd, &save);
        ESP_LOGI(TAG, "配网门户已启动: 连接 %s → http://192.168.4.1", s_ap_ssid);
    }
}
