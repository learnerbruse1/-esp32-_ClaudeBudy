/**
 * ================================================================
 * claude_api.h — 网络(Wi-Fi) + Anthropic Messages API 客户端
 * ================================================================
 */
#ifndef CLAUDE_API_H
#define CLAUDE_API_H

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 网络 ---- */
void      net_init(void);                       /* 幂等: netif/事件循环/wifi 初始化 */
esp_err_t net_sta_connect(int timeout_ms);      /* 用配置中的 SSID 连接, 阻塞直到拿到 IP 或超时 */
bool      net_sta_connected(void);
void      net_get_ip(char *buf, size_t len);    /* "192.168.x.x" 或 "0.0.0.0" */
int       net_rssi(void);                       /* dBm, 未连接返回 0 */
esp_err_t net_ap_start(const char *ssid, const char *pass);  /* 启动 SoftAP (pass 为空=开放) */

/* ---- Claude ---- */
typedef enum {
    CLAUDE_OK = 0,
    CLAUDE_ERR_NOWIFI,
    CLAUDE_ERR_HTTP,
    CLAUDE_ERR_STATUS,   /* HTTP 状态非 2xx */
    CLAUDE_ERR_PARSE,
    CLAUDE_ERR_MEM,
} claude_status_t;

/**
 * 向 Claude 提问 (阻塞, 请在后台任务中调用, 不要在 LVGL 任务里调用)。
 * @param user_prompt 用户/预设 提示词
 * @param out         回复文本输出缓冲
 * @param out_size    缓冲大小
 * @param err         出错时的简短说明 (可为 NULL)
 * @param err_size    err 缓冲大小
 */
claude_status_t claude_ask(const char *user_prompt,
                           char *out, size_t out_size,
                           char *err, size_t err_size);

#ifdef __cplusplus
}
#endif
#endif /* CLAUDE_API_H */
