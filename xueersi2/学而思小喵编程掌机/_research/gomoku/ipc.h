/**
 * ================================================================
 * ipc.h — 双核通信协议（IPC: Inter-Processor Communication）
 * ================================================================
 *
 * 架构:
 *   Core 0 — AI 搜索任务（ai_task, 优先级高）
 *   Core 1 — LVGL UI 任务（esp_lvgl_port 管理, 优先级低）
 *
 * 通信:
 *   UI → AI:  ai_request_t 通过 ai_queue 发送
 *   AI → UI:  ai_response_t 通过 ai_result_queue 返回
 *
 * 这是 C 兼容头文件，可被 .c 和 .cpp 文件包含。
 * ================================================================
 */

#ifndef IPC_H
#define IPC_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "game.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 通信协议
 * ================================================================ */

/** AI 命令类型 */
typedef enum {
    AI_CMD_FIND_MOVE = 0,   /**< 请求 AI 搜索一步棋 */
    AI_CMD_RESET     = 1,   /**< 重置 AI 搜索状态（新对局） */
    AI_CMD_QUIT      = 99,  /**< 终止 AI 任务（关机/退出） */
} ai_cmd_t;

/** 发送给 AI 核心的请求 */
typedef struct {
    ai_cmd_t     cmd;         /**< 命令 */
    difficulty_t difficulty;  /**< 难度等级（仅 AI_CMD_FIND_MOVE 有效） */
} ai_request_t;

/** AI 核心返回的结果 */
typedef struct {
    int x;          /**< 落子 x 坐标 (0-14) */
    int y;          /**< 落子 y 坐标 (0-14) */
    int depth;      /**< 实际搜索深度 */
    int success;    /**< 0=成功, -1=失败（无着法/超时） */
} ai_response_t;

/* ================================================================
 * 全局队列句柄
 * ================================================================ */

/** UI → AI 请求队列（容量 4） */
extern QueueHandle_t ai_queue;

/** AI → UI 结果队列（容量 2） */
extern QueueHandle_t ai_result_queue;

/* ================================================================
 * 接口函数
 * ================================================================ */

/**
 * 初始化双核 IPC 框架
 * - 创建队列
 * - 创建 AI 搜索任务（绑定 Core 0）
 * - 由主任务在 TEST_MODE=0 时调用
 */
void ipc_init(void);

/**
 * 向 AI 核心发送请求并阻塞等待结果
 * @param diff  难度等级
 * @param x     [out] 落子 x
 * @param y     [out] 落子 y
 * @param timeout_ms  等待超时（毫秒），0=无限等待
 * @return 0=成功, -1=失败
 */
int ipc_ai_find_move(difficulty_t diff, int *x, int *y, int timeout_ms);

/**
 * 释放 IPC 资源（测试/退出时调用）
 */
void ipc_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* IPC_H */
