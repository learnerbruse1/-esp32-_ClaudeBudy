/**
 * ================================================================
 * config.h — Claude Buddy 配置 (Wi-Fi / API 端点 / 密钥 / 模型 / 预设)
 * ================================================================
 * 优先级: NVS(cfg 分区) 已保存的值 > secrets.h 编译期默认值。
 * 通过 SoftAP 配网门户(web_portal) 可在运行时修改并保存到 NVS。
 * ================================================================
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_SSID_MAX     33
#define CFG_PASS_MAX     65
#define CFG_URL_MAX      200
#define CFG_KEY_MAX      256
#define CFG_MODEL_MAX    64
#define CFG_PROMPT_MAX   512

typedef struct {
    char wifi_ssid[CFG_SSID_MAX];
    char wifi_pass[CFG_PASS_MAX];
    char api_base_url[CFG_URL_MAX];   /* 形如 https://api.anthropic.com 或你的中转地址 */
    char api_key[CFG_KEY_MAX];        /* Anthropic / 中转 的密钥 */
    char model[CFG_MODEL_MAX];
    char system_prompt[CFG_PROMPT_MAX];
    int  max_tokens;
} buddy_config_t;

/* 预设(选项): 每个对应一句固定提示词 */
typedef struct {
    const char *title;    /* 菜单里显示的标题 */
    const char *prompt;   /* 发送给 Claude 的提示词 */
} preset_t;

/** 初始化 NVS(cfg 分区) 并载入配置 */
void config_init(void);

/** 当前配置 (只读) */
const buddy_config_t *config_get(void);

/** 保存配置到 NVS */
esp_err_t config_save(const buddy_config_t *c);

/** 用 secrets.h 默认值填充 */
void config_load_defaults(buddy_config_t *c);

/** 是否已具备联网+API 所需的最少配置 (有 SSID 且有 key) */
bool config_is_ready(void);

/** 预设列表 */
int config_preset_count(void);
const preset_t *config_preset(int i);

#ifdef __cplusplus
}
#endif
#endif /* CONFIG_H */
