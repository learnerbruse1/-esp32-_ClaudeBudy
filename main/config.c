/**
 * ================================================================
 * config.c — 配置加载/保存 (NVS "cfg" 分区, 单 blob 存储)
 * ================================================================
 */
#include "config.h"
#include "secrets.h"

#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "CFG";

#define CFG_PARTITION  "cfg"
#define CFG_NAMESPACE  "buddy"
#define CFG_BLOB_KEY   "cfg"

static buddy_config_t s_cfg;

/* ---- 默认预设(选项) ---- */
static const preset_t s_presets[] = {
    { "讲个程序员笑话",   "用中文给我讲一个简短的程序员笑话。" },
    { "今天学个新知识",   "用中文推荐一个适合青少年、有趣的编程或科技小知识, 简短一点。" },
    { "Python 小技巧",    "用中文分享一个实用的 Python 小技巧, 给出一个很短的示例。" },
    { "解释编程概念",     "用简单的中文给初学者解释什么是'函数', 并举一个生活中的例子。" },
    { "给我一句鼓励",     "我正在学习编程, 用中文给我一句简短有力的鼓励。" },
    { "脑筋急转弯",       "出一个简单有趣的脑筋急转弯, 用中文, 先只给题目。" },
    { "随机冷知识",       "用中文告诉我一个有趣的科学冷知识, 两三句话即可。" },
    { "排查报错思路",     "我写代码遇到报错, 用中文给我一个通用的排查 bug 的步骤清单, 简短。" },
};
#define PRESET_COUNT (sizeof(s_presets) / sizeof(s_presets[0]))

void config_load_defaults(buddy_config_t *c)
{
    memset(c, 0, sizeof(*c));
    strlcpy(c->wifi_ssid,     DEFAULT_WIFI_SSID,     sizeof(c->wifi_ssid));
    strlcpy(c->wifi_pass,     DEFAULT_WIFI_PASS,     sizeof(c->wifi_pass));
    strlcpy(c->api_base_url,  DEFAULT_API_BASE_URL,  sizeof(c->api_base_url));
    strlcpy(c->api_key,       DEFAULT_API_KEY,       sizeof(c->api_key));
    strlcpy(c->model,         DEFAULT_MODEL,         sizeof(c->model));
    strlcpy(c->system_prompt, DEFAULT_SYSTEM_PROMPT, sizeof(c->system_prompt));
    c->max_tokens = DEFAULT_MAX_TOKENS;
}

void config_init(void)
{
    /* 初始化独立的 cfg 分区 */
    esp_err_t err = nvs_flash_init_partition(CFG_PARTITION);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "cfg 分区需要擦除后重建");
        nvs_flash_erase_partition(CFG_PARTITION);
        err = nvs_flash_init_partition(CFG_PARTITION);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cfg 分区初始化失败: %s, 使用默认配置", esp_err_to_name(err));
        config_load_defaults(&s_cfg);
        return;
    }

    nvs_handle_t h;
    err = nvs_open_from_partition(CFG_PARTITION, CFG_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        size_t len = sizeof(s_cfg);
        err = nvs_get_blob(h, CFG_BLOB_KEY, &s_cfg, &len);
        nvs_close(h);
    }
    if (err != ESP_OK || /* 大小不符则视为无效 */ false) {
        ESP_LOGI(TAG, "未找到已保存配置, 载入编译期默认值");
        config_load_defaults(&s_cfg);
    } else {
        ESP_LOGI(TAG, "已从 NVS 载入配置 (ssid='%s', model='%s')", s_cfg.wifi_ssid, s_cfg.model);
    }
}

const buddy_config_t *config_get(void) { return &s_cfg; }

esp_err_t config_save(const buddy_config_t *c)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(CFG_PARTITION, CFG_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, CFG_BLOB_KEY, c, sizeof(*c));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        s_cfg = *c;   /* 更新内存副本 */
        ESP_LOGI(TAG, "配置已保存");
    }
    return err;
}

bool config_is_ready(void)
{
    return s_cfg.wifi_ssid[0] != '\0' && s_cfg.api_key[0] != '\0';
}

int config_preset_count(void) { return (int)PRESET_COUNT; }

const preset_t *config_preset(int i)
{
    if (i < 0 || i >= (int)PRESET_COUNT) return NULL;
    return &s_presets[i];
}
