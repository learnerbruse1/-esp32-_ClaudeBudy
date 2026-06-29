/**
 * ================================================================
 * input.cpp — 按键输入实现（中文注释版）
 * ================================================================
 *
 * 【硬件平台】ESP32-WROVER-B，6 键矩阵（小喵掌机标准布局）
 * 【引脚映射】UP=2  DOWN=13  LEFT=27  RIGHT=35  A=34  B=12
 * 【电气特性】低电平触发 + 内部上拉（按下键→GND）
 * 【依赖模块】driver/gpio.h、freertos/task.h
 * 【创建日期】2026-06-25  |  【最后修改】2026-06-27
 *
 * 核心机制：
 *   - ISR 防抖：下降沿后 150ms 内忽略重复触发
 *   - 长按检测：B 键持续低电平 > 500ms → BTN_B_LONG
 *   - 边沿触发：仅报告按下瞬间，避免重复触发同一事件
 * ================================================================
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "input.h"

static const char *TAG = "INPUT";

/* ---- 引脚定义（小喵掌机标准 6 键布局）---- */
#define PIN_UP     GPIO_NUM_2    /* 上 */
#define PIN_DOWN   GPIO_NUM_13   /* 下 */
#define PIN_LEFT   GPIO_NUM_27   /* 左 */
#define PIN_RIGHT  GPIO_NUM_35   /* 右 */
#define PIN_A      GPIO_NUM_34   /* A（确认/落子） */
#define PIN_B      GPIO_NUM_12   /* B（返回/音效切换） */

/** 6 键位掩码，gpio_config 一次性配置所有引脚 */
#define BTN_MASK  ((1ULL << 2) | (1ULL << 13) | (1ULL << 27) |  \
                   (1ULL << 35) | (1ULL << 34) | (1ULL << 12))

#define DEBOUNCE_MS   150   /* 防抖窗口 150ms */
#define LONG_PRESS_MS 500   /* B 长按阈值 500ms */

/* ---- ISR → 主循环共享状态（volatile 保证跨上下文可见性）---- */
static volatile uint32_t g_btn_gpio     = 0;    /* 最近按下的 GPIO 编号 */
static volatile uint32_t g_btn_tick     = 0;    /* 按下时刻 (FreeRTOS tick) */
static volatile bool     g_btn_pending  = false; /* true=主循环有待处理事件 */

/* ================================================================
 * btn_isr — GPIO 中断服务例程（IRAM 驻留，极速响应）
 *
 * 所有 6 键共用此 ISR，通过 arg 区分按键。
 * 仅记录下降沿（键按下→GND），忽略上升沿（键释放）。
 * 防抖：距上次触发不足 DEBOUNCE_MS 的触发直接丢弃。
 * ================================================================ */
static void IRAM_ATTR btn_isr(void *arg)
{
    uint32_t gpio = (uint32_t)arg;
    uint32_t now  = xTaskGetTickCountFromISR();

    /* 仅响应下降沿（按下→低电平），忽略上升沿 */
    if (gpio_get_level((gpio_num_t)gpio) == 1) return;

    /* 防抖判定：距上次触发 > 150ms 才接受 */
    if (now - g_btn_tick > pdMS_TO_TICKS(DEBOUNCE_MS)) {
        g_btn_tick    = now;
        g_btn_gpio    = gpio;
        g_btn_pending = true;  /* 通知主循环有新事件待处理 */
    }
}

/* ================================================================
 * gpio_to_event — GPIO 编号 → 逻辑按键事件
 *
 * ISR 记录原始 GPIO 号，主循环通过此纯函数转换为语义事件。
 * 分离 ISR 与业务逻辑，ISR 不包含任何业务判断。
 * ================================================================ */
static btn_event_t gpio_to_event(uint32_t gpio)
{
    switch (gpio) {
        case PIN_UP:    return BTN_UP;
        case PIN_DOWN:  return BTN_DOWN;
        case PIN_LEFT:  return BTN_LEFT;
        case PIN_RIGHT: return BTN_RIGHT;
        case PIN_A:     return BTN_A;
        case PIN_B:     return BTN_B;
        default:        return BTN_NONE;
    }
}

/* ================================================================
 * 公开接口 — 对上层模块暴露的按键 API
 * ================================================================ */

/**
 * input_init — 初始化 6 键 GPIO + ISR
 * 配置输入模式+内部上拉+下降沿中断，安装 gpio_isr_service。
 * 调用时机：app_main 启动阶段，ui_init() 之后。
 */
void input_init(void)
{
    /* GPIO 配置：输入 + 上拉 */
    gpio_config_t cfg = {
        .pin_bit_mask = BTN_MASK,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,  /* 下降沿触发 */
    };
    gpio_config(&cfg);

    /* 安装 ISR */
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_UP,    btn_isr, (void*)(uintptr_t)PIN_UP);
    gpio_isr_handler_add(PIN_DOWN,  btn_isr, (void*)(uintptr_t)PIN_DOWN);
    gpio_isr_handler_add(PIN_LEFT,  btn_isr, (void*)(uintptr_t)PIN_LEFT);
    gpio_isr_handler_add(PIN_RIGHT, btn_isr, (void*)(uintptr_t)PIN_RIGHT);
    gpio_isr_handler_add(PIN_A,     btn_isr, (void*)(uintptr_t)PIN_A);
    gpio_isr_handler_add(PIN_B,     btn_isr, (void*)(uintptr_t)PIN_B);

    ESP_LOGI(TAG, "initialized (UP=2 DN=13 L=27 R=35 A=34 B=12)");
}

/**
 * input_poll — 非阻塞轮询按键事件
 *
 * @return BTN_NONE 无事件，否则返回对应按键枚举。
 *
 * B 键特殊处理：检测到按下后延时 510ms 再测电平，
 * 若仍为低电平 → 返回 BTN_B_LONG（长按）；否则 → 返回 BTN_B（短按）。
 * 其他键即时返回，不阻塞。
 */
btn_event_t input_poll(void)
{
    if (!g_btn_pending) return BTN_NONE;

    /* 原子读取并清除 pending 标志 */
    uint32_t gpio = g_btn_gpio;
    g_btn_pending = false;

    btn_event_t evt = gpio_to_event(gpio);

    /* B 键长按检测：延时 510ms 后再次采样 */
    if (evt == BTN_B) {
        vTaskDelay(pdMS_TO_TICKS(LONG_PRESS_MS + 10));  /* 510ms */
        if (gpio_get_level(PIN_B) == 0) {
            /* 仍在按下 → 确认长按，等待释放后返回 */
            while (gpio_get_level(PIN_B) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            g_btn_pending = false;
            return BTN_B_LONG;
        }
        /* 已释放 → 短按 */
    }

    return evt;
}

/**
 * input_wait — 阻塞等待按键事件（带超时）
 *
 * @param timeout_ms 超时时间 (ms)，0 表示永久等待。
 * @return 按键事件，超时返回 BTN_NONE。
 *
 * 内部以 20ms 间隔轮询 input_poll()，避免 CPU 空转。
 */
btn_event_t input_wait(int timeout_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (timeout_ms == 0 || xTaskGetTickCount() < deadline) {
        btn_event_t evt = input_poll();
        if (evt != BTN_NONE) return evt;
        vTaskDelay(pdMS_TO_TICKS(20));  /* 20ms 轮询间隔，不空转 */
    }
    return BTN_NONE;
}

uint32_t input_last_tick(void)
{
    return g_btn_tick;
}
