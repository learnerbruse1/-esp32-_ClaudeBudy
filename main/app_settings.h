#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H
#ifdef __cplusplus
extern "C" {
#endif

/** 显示设置菜单 (配网 / 设备信息 / 重置为默认 / 返回) */
void app_settings_show(void);

/** 显示配网界面 (启动 SoftAP + 网页) */
void app_settings_show_portal(void);

#ifdef __cplusplus
}
#endif
#endif
