/**
 * ================================================================
 * ai_bridge.cpp — AI 桥接层（完整搜索流水线）
 * ================================================================
 *
 * 封装 Pela computer() 的完整逻辑:
 *   1. firstMove() — 开局策略
 *   2. try4(0)     — 能否立即取胜
 *   3. try4(1)     — 对手威胁检测+防守
 *   4. getBestEval — 启发式最优
 *   5. 迭代加深    — computer1: attack→test→defend
 *
 * 难度参数:
 *   EASY:   depth 2-4, 500ms, 高随机
 *   MEDIUM: depth 4-8, 2000ms, 中随机
 *   HARD:   depth 6-16, 5000ms, 低随机
 * ================================================================
 */

#include <string.h>
#include "ai_bridge.h"
#include "alfabeta.h"
#include "evaluate.h"

#ifdef TESTING
#else
  #include "esp_timer.h"
  #include "esp_log.h"
#endif

#ifndef TESTING
  static const char *TAG = "AI";
#else
  #define TAG "AI"
#endif

/* ================================================================
 * 难度参数预设表
 * ================================================================ */
static const ai_params_t g_params[] = {
    /* EASY   */ {  2,   4,   500, 0, 30 },
    /* MEDIUM */ {  4,   8,  2000, 1, 15 },
    /* HARD   */ {  6,  16,  5000, 1,  5 },
};

const ai_params_t *ai_get_params(difficulty_t diff)
{
    if (diff < 0 || diff > 2) diff = DIFF_MEDIUM;
    return &g_params[diff];
}

/* ================================================================
 * ai_find_best_move — 完整 AI 搜索流水线
 *
 * 复制原版 computer() 逻辑:
 *   1. 重置搜索状态
 *   2. firstMove() 开局处理
 *   3. try4(0) → 能赢就赢
 *   4. try4(1) → 检查对手威胁 → 防守
 *   5. getBestEval() → 启发式最优
 *   6. 迭代加深 (depth += 2) → computer1()
 *   7. 超时或找到必胜 → 返回结果
 * ================================================================ */
int ai_find_best_move(difficulty_t diff, int *out_x, int *out_y)
{
    const ai_params_t *p = ai_get_params(diff);

    /* --- 0. 全局状态刷新 --- */
    depth = 0;
    benchmark = 0;

    /* --- 1. 重置搜索状态 --- */
    UwinMoves = winMoves1;
    for (Psquare q = boardb; q < boardk; q++) {
        q->inWinMoves = winMoves1 + MWINMOVES;
    }
    resultMove    = nullptr;
    holdMove      = nullptr;
    highestEval   = nullptr;
    bestMove      = nullptr;
    loss4         = nullptr;
    terminateAI   = 0;
    benchmark     = 0;

    attackDone = defendDone = defendDone1 = testDone = false;
    depthReached = false;
    carefulAttack = carefulDefend = false;

    /* --- 2. 开局处理 --- */
    firstMove();
    if (resultMove) goto done;

    /* --- 3. try4(0): 我能立即赢吗？ --- */
    if (p->use_try4 && doMove(try4(0))) goto done;

    /* --- 4. try4(1): 对手能立即赢吗？防守他 --- */
    attackDone = defendDone = defendDone1 = testDone = false;
    carefulAttack = carefulDefend = false;
    loss4 = p->use_try4 ? try4(1) : nullptr;
    if (loss4) {
        if (doMove(loss4)) goto done;
    }

    /* --- 5. 启发式最优（兜底） --- */
    if (!resultMove) getBestEval();

    /* --- 6. 迭代加深 --- */
    {
        uint64_t deadline = esp_timer_get_time() / 1000 + p->timeout_ms;

        for (depth = p->start_depth;
             depth <= p->max_depth && !terminateAI;
             depth += 2)
        {
#ifndef TESTING
            ESP_LOGI(TAG, "搜索深度=%d, 限时=%dms", depth, p->timeout_ms);
#endif
            benchmark = 0;

            computer1();

            /* 检查是否该停了 */
            uint64_t now = esp_timer_get_time() / 1000;
            if (terminateAI) break;
            if (now >= deadline) {
                terminateAI = 2; /* 超时 */
                break;
            }
            /* 预估下一深度会超时则提前退出 */
            if (benchmark > 10000 && now > deadline / 2) break;
        }
    }

done:
    if (!resultMove) {
#ifndef TESTING
        ESP_LOGW(TAG, "AI 未找到着法，用 findMax 兜底");
#endif
        resultMove = findMax();
        if (resultMove) doMove(resultMove);
    }

    if (!resultMove) return -1;

    *out_x = resultMove->x;
    *out_y = resultMove->y;

#ifndef TESTING
    ESP_LOGI(TAG, "着法 (%d,%d), 深度=%d, 节点=%d, 耗时=--",
             *out_x, *out_y, depth, benchmark);
#endif
    return 0;
}
