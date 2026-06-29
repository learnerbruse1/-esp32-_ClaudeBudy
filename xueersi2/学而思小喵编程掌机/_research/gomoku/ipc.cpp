/**
 * ================================================================
 * ipc.cpp — 双核通信框架实现
 * ================================================================
 *
 * AI 搜索任务运行在 Core 0（240MHz，独占）:
 *   - 优先级: 8（高于 LVGL 的 1）
 *   - 栈大小: 12KB（递归 Alpha-Beta 需要较大栈）
 *   - 循环等待队列请求 → 调用 ai_find_best_move → 发回结果
 *
 * LVGL UI 任务由 esp_lvgl_port 管理在 Core 1。
 * ================================================================
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ipc.h"
#include "ai_bridge.h"
#include "board.h"

static const char *TAG = "IPC";

/* ================================================================
 * 全局队列句柄
 * ================================================================ */
QueueHandle_t ai_queue = NULL;
QueueHandle_t ai_result_queue = NULL;

/* ================================================================
 * AI 搜索任务参数
 * ================================================================ */
#define AI_TASK_STACK_SIZE  12288   /* 12KB — Alpha-Beta 递归 + K 表 */
#define AI_TASK_PRIORITY    8       /* 高于 LVGL(1), 低于 idle(0) */
#define AI_TASK_CORE        0       /* Core 0 — PRO CPU */

/* ================================================================
 * ai_task_fn — AI 搜索任务主循环
 * ================================================================ */
static void ai_task_fn(void *arg)
{
    ESP_LOGI(TAG, "AI 搜索任务启动 (Core %d, 栈 %d KB)",
             xPortGetCoreID(), AI_TASK_STACK_SIZE / 1024);

    ai_request_t req;
    ai_response_t resp;

    for (;;) {
        /* 阻塞等待 UI 请求 */
        if (xQueueReceive(ai_queue, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (req.cmd) {
        case AI_CMD_FIND_MOVE:
            ESP_LOGD(TAG, "收到搜索请求 — 难度=%d", req.difficulty);

            /* 调用 AI 桥接层 */
            resp.success = ai_find_best_move(req.difficulty, &resp.x, &resp.y);
            resp.depth = depth;  /* ai_bridge 设置的全局 depth */

            if (resp.success == 0) {
                ESP_LOGI(TAG, "搜索完成: (%d,%d), depth=%d",
                         resp.x, resp.y, resp.depth);
            } else {
                ESP_LOGW(TAG, "搜索失败: AI 未找到着法");
            }

            /* 发回结果（非阻塞，UI 已在等） */
            xQueueSend(ai_result_queue, &resp, 0);
            break;

        case AI_CMD_RESET:
            ESP_LOGI(TAG, "重置 AI 状态");
            /* 重置棋盘 + 搜索状态 */
            board_reset();
            /* 发空响应表示完成 */
            resp.success = 0;
            resp.x = resp.y = -1;
            resp.depth = 0;
            xQueueSend(ai_result_queue, &resp, 0);
            break;

        case AI_CMD_QUIT:
            ESP_LOGI(TAG, "AI 搜索任务退出");
            vTaskDelete(NULL);
            return;

        default:
            ESP_LOGW(TAG, "未知命令: %d", req.cmd);
            break;
        }
    }
}

/* ================================================================
 * ipc_init — 初始化双核 IPC 框架
 * ================================================================ */
void ipc_init(void)
{
    ESP_LOGI(TAG, "初始化双核 IPC 框架...");

    /* 创建队列 */
    ai_queue = xQueueCreate(4, sizeof(ai_request_t));
    ai_result_queue = xQueueCreate(2, sizeof(ai_response_t));

    if (!ai_queue || !ai_result_queue) {
        ESP_LOGE(TAG, "队列创建失败！");
        return;
    }

    /* 在 Core 0 创建 AI 搜索任务 */
    TaskHandle_t ai_task_handle = NULL;
    BaseType_t ret = xTaskCreatePinnedToCore(
        ai_task_fn,         /* 任务函数 */
        "ai_search",        /* 名称 */
        AI_TASK_STACK_SIZE, /* 栈（字） */
        NULL,               /* 参数 */
        AI_TASK_PRIORITY,   /* 优先级 */
        &ai_task_handle,    /* 句柄 */
        AI_TASK_CORE        /* 绑定核心 */
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "AI 任务创建失败！(err=%d)", ret);
    } else {
        ESP_LOGI(TAG, "AI 任务已创建 (Core %d, 优先级 %d)",
                 AI_TASK_CORE, AI_TASK_PRIORITY);
    }
}

/* ================================================================
 * ipc_ai_find_move — 向 AI 核心发请求并等结果
 * ================================================================ */
int ipc_ai_find_move(difficulty_t diff, int *x, int *y, int timeout_ms)
{
    if (!ai_queue || !ai_result_queue) {
        ESP_LOGE(TAG, "IPC 未初始化！");
        return -1;
    }

    /* 清空旧结果 */
    xQueueReset(ai_result_queue);

    /* 发送请求 */
    ai_request_t req = {
        .cmd = AI_CMD_FIND_MOVE,
        .difficulty = diff,
    };
    if (xQueueSend(ai_queue, &req, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "发送 AI 请求失败（队列满）");
        return -1;
    }

    /* 等待结果 */
    ai_response_t resp;
    TickType_t wait = (timeout_ms > 0) ? pdMS_TO_TICKS(timeout_ms)
                                        : portMAX_DELAY;
    if (xQueueReceive(ai_result_queue, &resp, wait) != pdTRUE) {
        ESP_LOGE(TAG, "等待 AI 结果超时 (%d ms)", timeout_ms);
        return -1;
    }

    *x = resp.x;
    *y = resp.y;
    return resp.success;
}

/* ================================================================
 * ipc_deinit — 释放 IPC 资源
 * ================================================================ */
void ipc_deinit(void)
{
    ESP_LOGI(TAG, "释放 IPC 资源...");

    /* 发送退出命令 */
    if (ai_queue) {
        ai_request_t quit = { .cmd = AI_CMD_QUIT, .difficulty = DIFF_EASY };
        xQueueSend(ai_queue, &quit, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* 删除队列 */
    if (ai_queue) {
        vQueueDelete(ai_queue);
        ai_queue = NULL;
    }
    if (ai_result_queue) {
        vQueueDelete(ai_result_queue);
        ai_result_queue = NULL;
    }
    ESP_LOGI(TAG, "IPC 资源已释放");
}
