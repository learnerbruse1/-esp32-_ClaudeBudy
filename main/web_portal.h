/**
 * web_portal.h — SoftAP 配网门户 (手机连热点, 网页填写 Wi-Fi/API 配置)
 */
#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H
#ifdef __cplusplus
extern "C" {
#endif

/** 启动 SoftAP + 配置网页 (非阻塞)。保存后设备自动重启。 */
void web_portal_start(void);

/** 当前 SoftAP 的名称 (用于在屏幕上显示) */
const char *web_portal_ssid(void);

#ifdef __cplusplus
}
#endif
#endif
