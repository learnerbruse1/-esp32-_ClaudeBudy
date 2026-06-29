/**
 * ================================================================
 * main.c — 学而思小喵掌机 Claude Buddy 固件入口
 * ================================================================
 * 启动流程:
 *   1. 载入配置 (NVS)
 *   2. 初始化显示/LVGL/按键
 *   3. 若 未配置 或 开机按住B → 进入 SoftAP 配网
 *      否则 → 显示开机主菜单 (Claude 伙伴 / 投屏 / 工具箱 / 设置)
 * ================================================================
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "board.h"
#include "config.h"
#include "app_menu.h"
#include "app_settings.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "==== 小喵掌机 Claude Buddy 启动 ====");

    /* 默认 nvs 分区 (Wi-Fi PHY 校准等需要) */
    esp_err_t nv = nvs_flash_init();
    if (nv == ESP_ERR_NVS_NO_FREE_PAGES || nv == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    config_init();   /* cfg 分区 + 载入配置 */
    board_init();    /* 显示 + LVGL + 按键 */

    /* 开机默认进主菜单; 长按 B 直接进设置 */
    bool to_settings = board_btn_is_down(BOARD_BTN_B);

    board_lock(0);
    if (to_settings) app_settings_show();
    else app_menu_show();
    board_unlock();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
