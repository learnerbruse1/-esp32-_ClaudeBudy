/**
 * sdcard.h — TF 卡格式化为 FAT32
 */
#ifndef SDCARD_H
#define SDCARD_H
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * 阻塞式: 挂载并把 TF 卡格式化为 FAT32。
 * 请在后台任务里调用 (会做较慢的 SPI I/O)。
 * @param errbuf 出错说明 (可为 NULL)
 * @return ESP_OK 成功
 */
esp_err_t sdcard_format_fat32(char *errbuf, size_t errlen);

/** 只读获取 TF 卡容量 (字节)。无卡 / 非 FAT 时返回非 ESP_OK。 */
esp_err_t sdcard_get_info(uint64_t *total_bytes, uint64_t *free_bytes);

#ifdef __cplusplus
}
#endif
#endif
