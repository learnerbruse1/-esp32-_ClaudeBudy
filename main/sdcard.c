/**
 * ================================================================
 * sdcard.c — TF 卡格式化为 FAT32
 * ================================================================
 * TF 卡与 LCD 共用 SPI2 总线 (SCK=18 MOSI=23 MISO=19), SD 片选 CS=22。
 * SPI2 总线已在 board.c 的 display_init 里初始化, 这里只在总线上加入
 * SD 设备进行挂载/格式化, 完成后卸载。
 * ================================================================
 */
#include "sdcard.h"

#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"

static const char *TAG = "SDCARD";

#define MOUNT_POINT  "/sdfmt"
#define PIN_SD_CS    22
#define SD_SPI_HOST  SPI2_HOST

esp_err_t sdcard_format_fat32(char *errbuf, size_t errlen)
{
    if (errbuf && errlen) errbuf[0] = '\0';

    /* SDSPI 主机 (复用已初始化的 SPI2 总线) */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = 20000;   /* 20MHz, 共用总线稳妥 */

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs  = PIN_SD_CS;
    slot.host_id  = SD_SPI_HOST;

    esp_vfs_fat_mount_config_t mcfg = {
        .format_if_mount_failed = true,    /* 卡是 exFAT/未格式化时自动格式化 */
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot, &mcfg, &card);
    if (err != ESP_OK) {
        if (errbuf) snprintf(errbuf, errlen, "找不到或无法初始化TF卡 (%s)", esp_err_to_name(err));
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 强制一次干净的 FAT32 格式化 (确保"重置") */
    err = esp_vfs_fat_sdcard_format_cfg(MOUNT_POINT, card, &mcfg);
    if (err != ESP_OK) {
        if (errbuf) snprintf(errbuf, errlen, "格式化失败 (%s)", esp_err_to_name(err));
        ESP_LOGE(TAG, "format failed: %s", esp_err_to_name(err));
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
        return err;
    }

    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    ESP_LOGI(TAG, "TF卡已格式化为FAT32");
    return ESP_OK;
}

esp_err_t sdcard_get_info(uint64_t *total_bytes, uint64_t *free_bytes)
{
    if (total_bytes) *total_bytes = 0;
    if (free_bytes)  *free_bytes = 0;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = 20000;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = SD_SPI_HOST;

    esp_vfs_fat_mount_config_t mcfg = {
        .format_if_mount_failed = false,   /* 只读取, 不格式化 */
        .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdspi_mount("/sdinfo", &host, &slot, &mcfg, &card);
    if (err != ESP_OK) return err;   /* 无卡或非 FAT32 */

    err = esp_vfs_fat_info("/sdinfo", total_bytes, free_bytes);
    esp_vfs_fat_sdcard_unmount("/sdinfo", card);
    return err;
}
