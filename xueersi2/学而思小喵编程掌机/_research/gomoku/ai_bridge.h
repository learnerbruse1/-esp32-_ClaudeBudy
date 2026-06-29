/**
 * ================================================================
 * ai_bridge.h — AI 桥接层
 * ================================================================
 *
 * 封装 Pela 搜索引擎的完整调用流水线:
 *   firstMove → try4(0) → try4(1) → getBestEval
 *   → 迭代加深(computer1: attack→test→defend)
 *
 * 难度等级控制搜索深度、超时和 aggressiveness。
 * ================================================================
 */

#ifndef AI_BRIDGE_H
#define AI_BRIDGE_H

#include "game.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * AI 搜索参数（按难度预设）
 */
typedef struct {
    int  start_depth;    /* 初始搜索深度 */
    int  max_depth;      /* 最大搜索深度 */
    int  timeout_ms;     /* 单步时限（毫秒） */
    int  use_try4;       /* 是否使用 try4 强制攻防 */
    int  random_factor;  /* 随机因子（越大越随机 → 越弱） */
} ai_params_t;

/**
 * 获取难度对应的搜索参数
 */
const ai_params_t *ai_get_params(difficulty_t diff);

/**
 * AI 搜索最佳落子
 * @param diff  难度等级
 * @param out_x 输出: 落子 x 坐标
 * @param out_y 输出: 落子 y 坐标
 * @return 0=成功, -1=失败
 */
int ai_find_best_move(difficulty_t diff, int *out_x, int *out_y);

#ifdef __cplusplus
}
#endif

#endif /* AI_BRIDGE_H */
