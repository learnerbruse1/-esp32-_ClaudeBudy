/**
 * ================================================================
 * claude_api.c — Wi-Fi (STA/AP) + Anthropic Messages API (HTTPS)
 * ================================================================
 * - 通过 esp_http_client + 证书包(crt bundle) 访问 {base_url}/v1/messages
 * - 请求/响应均为 JSON, 用 cJSON 解析
 * - 端点(base_url)/密钥/模型 全部来自 config (支持自建中转)
 * ================================================================
 */
#include "claude_api.h"
#include "config.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "NET";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define MAX_RETRY           6

static EventGroupHandle_t s_wifi_evt;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif  = NULL;
static int s_retry = 0;
static bool s_net_inited = false;
static volatile bool s_connected = false;

/* ================================================================
 * Wi-Fi 事件
 * ================================================================ */
static void wifi_evt_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retry < MAX_RETRY) {
            s_retry++;
            ESP_LOGW(TAG, "重连 Wi-Fi (%d/%d)", s_retry, MAX_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_evt, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_evt, WIFI_CONNECTED_BIT);
    }
}

void net_init(void)
{
    if (s_net_inited) return;
    s_wifi_evt = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                        &wifi_evt_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                        &wifi_evt_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    s_net_inited = true;
}

esp_err_t net_sta_connect(int timeout_ms)
{
    const buddy_config_t *c = config_get();
    if (c->wifi_ssid[0] == '\0') return ESP_ERR_INVALID_STATE;

    net_init();
    s_retry = 0;
    xEventGroupClearBits(s_wifi_evt, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, c->wifi_ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, c->wifi_pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = c->wifi_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_evt,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
            pdMS_TO_TICKS(timeout_ms));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi 已连接");
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Wi-Fi 连接失败/超时");
    return ESP_FAIL;
}

bool net_sta_connected(void) { return s_connected; }

void net_get_ip(char *buf, size_t len)
{
    esp_netif_ip_info_t ip = { 0 };
    if (s_sta_netif) esp_netif_get_ip_info(s_sta_netif, &ip);
    snprintf(buf, len, IPSTR, IP2STR(&ip.ip));
}

int net_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.rssi;
    return 0;
}

esp_err_t net_ap_start(const char *ssid, const char *pass)
{
    net_init();
    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    if (pass && pass[0]) {
        strlcpy((char *)ap.ap.password, pass, sizeof(ap.ap.password));
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "SoftAP 已启动: %s", ssid);
    return ESP_OK;
}

/* ================================================================
 * HTTP 响应累积
 * ================================================================ */
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} resp_t;

#define RESP_MAX  (32 * 1024)

static esp_err_t http_evt(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_DATA && e->data_len > 0) {
        resp_t *r = (resp_t *)e->user_data;
        if (!r) return ESP_OK;
        if (r->len + e->data_len + 1 > r->cap) {
            size_t ncap = r->cap ? r->cap * 2 : 2048;
            while (ncap < r->len + e->data_len + 1) ncap *= 2;
            if (ncap > RESP_MAX) ncap = RESP_MAX;
            if (r->len + e->data_len + 1 > ncap) return ESP_OK; /* 超上限, 丢弃多余 */
            char *nb = realloc(r->buf, ncap);
            if (!nb) return ESP_OK;
            r->buf = nb; r->cap = ncap;
        }
        memcpy(r->buf + r->len, e->data, e->data_len);
        r->len += e->data_len;
        r->buf[r->len] = '\0';
    }
    return ESP_OK;
}

/* 从 Anthropic 成功响应里取出第一段 text */
static bool parse_reply(const char *json, char *out, size_t out_size,
                        char *err, size_t err_size)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    bool ok = false;

    /* 错误响应: {"type":"error","error":{"message":...}} */
    cJSON *errobj = cJSON_GetObjectItem(root, "error");
    if (errobj) {
        cJSON *msg = cJSON_GetObjectItem(errobj, "message");
        if (err && msg && cJSON_IsString(msg)) strlcpy(err, msg->valuestring, err_size);
        cJSON_Delete(root);
        return false;
    }

    /* 成功响应: content 数组中第一个 type=="text" 的块 */
    cJSON *content = cJSON_GetObjectItem(root, "content");
    if (cJSON_IsArray(content)) {
        cJSON *blk;
        cJSON_ArrayForEach(blk, content) {
            cJSON *type = cJSON_GetObjectItem(blk, "type");
            cJSON *text = cJSON_GetObjectItem(blk, "text");
            if (type && cJSON_IsString(type) && strcmp(type->valuestring, "text") == 0 &&
                text && cJSON_IsString(text)) {
                strlcpy(out, text->valuestring, out_size);
                ok = true;
                break;
            }
        }
    }
    cJSON_Delete(root);
    return ok;
}

claude_status_t claude_ask(const char *user_prompt, char *out, size_t out_size,
                           char *err, size_t err_size)
{
    if (err && err_size) err[0] = '\0';
    if (!net_sta_connected()) {
        if (err) strlcpy(err, "未连接 Wi-Fi", err_size);
        return CLAUDE_ERR_NOWIFI;
    }
    const buddy_config_t *c = config_get();

    /* ---- 构造请求 JSON ---- */
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", c->model);
    cJSON_AddNumberToObject(req, "max_tokens", c->max_tokens > 0 ? c->max_tokens : 512);
    if (c->system_prompt[0]) cJSON_AddStringToObject(req, "system", c->system_prompt);
    cJSON *msgs = cJSON_AddArrayToObject(req, "messages");
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", "user");
    cJSON_AddStringToObject(m, "content", user_prompt);
    cJSON_AddItemToArray(msgs, m);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) { if (err) strlcpy(err, "内存不足", err_size); return CLAUDE_ERR_MEM; }

    /* ---- URL = base_url + /v1/messages ---- */
    char url[CFG_URL_MAX + 16];
    snprintf(url, sizeof(url), "%s/v1/messages", c->api_base_url);

    resp_t resp = { 0 };
    esp_http_client_config_t hc = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_evt,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .buffer_size = 1024,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&hc);
    if (!client) { free(body); if (err) strlcpy(err, "HTTP 初始化失败", err_size); return CLAUDE_ERR_HTTP; }

    esp_http_client_set_header(client, "content-type", "application/json");
    esp_http_client_set_header(client, "x-api-key", c->api_key);
    esp_http_client_set_header(client, "anthropic-version", "2023-06-01");
    esp_http_client_set_post_field(client, body, strlen(body));

    claude_status_t st = CLAUDE_OK;
    esp_err_t e = esp_http_client_perform(client);
    int code = esp_http_client_get_status_code(client);

    if (e != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 失败: %s", esp_err_to_name(e));
        if (err) snprintf(err, err_size, "网络错误: %s", esp_err_to_name(e));
        st = CLAUDE_ERR_HTTP;
    } else if (code < 200 || code >= 300) {
        ESP_LOGE(TAG, "HTTP 状态 %d, body=%.200s", code, resp.buf ? resp.buf : "");
        char apimsg[160] = { 0 };
        if (resp.buf) parse_reply(resp.buf, out, out_size, apimsg, sizeof(apimsg));
        if (err) {
            if (apimsg[0]) snprintf(err, err_size, "API %d: %s", code, apimsg);
            else snprintf(err, err_size, "API 返回状态 %d", code);
        }
        st = CLAUDE_ERR_STATUS;
    } else if (!resp.buf || !parse_reply(resp.buf, out, out_size, err, err_size)) {
        ESP_LOGE(TAG, "解析失败: %.200s", resp.buf ? resp.buf : "(空)");
        if (err && !err[0]) strlcpy(err, "无法解析回复", err_size);
        st = CLAUDE_ERR_PARSE;
    }

    esp_http_client_cleanup(client);
    free(resp.buf);
    free(body);
    return st;
}
