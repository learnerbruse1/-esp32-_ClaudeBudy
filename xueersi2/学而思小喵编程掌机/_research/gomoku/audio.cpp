/**
 * ================================================================
 * audio.cpp — 蜂鸣器音效实现（中文注释版）
 * ================================================================
 *
 * 【硬件平台】ESP32-WROVER-B + 无源蜂鸣器 @ GPIO14（小喵掌机固定引脚）
 * 【驱动方式】LEDC PWM 高速通道，13-bit 分辨率，5kHz 基频
 * 【依赖模块】driver/ledc.h、freertos/task.h
 * 【创建日期】2026-06-25  |  【最后修改】2026-06-27
 *
 * 音调设计（Hz）：C4=262 D4=294 E4=330 F4=349 G4=392 A4=440 B4=494 C5=523
 * 50% 占空比方波驱动，播放结束立即关断省电。
 * ================================================================
 */

#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio.h"

static const char *TAG = "AUDIO";

/* ---- 硬件引脚 & LEDC 通道常量 ---- */
#define BUZZER_PIN    14               /* 蜂鸣器 GPIO（小喵掌机固定） */
#define LEDC_MODE     LEDC_HIGH_SPEED_MODE   /* 高速模式，避免可闻载波噪声 */
#define LEDC_CHANNEL  LEDC_CHANNEL_0   /* LEDC 通道 0 */
#define LEDC_TIMER    LEDC_TIMER_0     /* LEDC 定时器 0 */
#define LEDC_DUTY_RES LEDC_TIMER_13_BIT /* 13-bit → 占空比 0~8191 */
#define LEDC_BASE_FREQ 5000            /* 基频 5kHz，播放时动态改频 */

/* ================================================================
 * audio_init — 初始化蜂鸣器 PWM 输出
 *
 * 调用时机：app_main 启动阶段，ui_init() 之后。
 * 配置 LEDC 定时器 + 通道，初始占空比 0（静音状态）。
 * ================================================================ */
void audio_init(void)
{
    /* LEDC 定时器：高速 + 13-bit + 5kHz，自动选择时钟源 */
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num       = LEDC_TIMER,
        .freq_hz         = LEDC_BASE_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK,
        .deconfigure     = false,
    };
    ledc_timer_config(&timer);

    /* LEDC 通道：绑定 GPIO14，初始占空比 0 = 完全静音 */
    ledc_channel_config_t ch = {
        .gpio_num       = BUZZER_PIN,
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .intr_type      = LEDC_INTR_DISABLE,  /* 无需 PWM 中断 */
        .timer_sel      = LEDC_TIMER,
        .duty           = 0,                  /* 0 = 静音 */
        .hpoint         = 0,
        .sleep_mode     = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,  /* 深度休眠关断 */
        .flags          = 0,
    };
    ledc_channel_config(&ch);

    /* 初始静音 */
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

    ESP_LOGI(TAG, "initialized (GPIO%d, LEDC ch%d)", BUZZER_PIN, LEDC_CHANNEL);
}

/* ================================================================
 * tone — 播放指定频率+持续时间的单音（阻塞式）
 *
 * @param freq_hz     音高 (Hz)，<=0 表示仅延时不发声
 * @param duration_ms 时长 (ms)
 *
 * 流程：设频→50%占空比→延时→静音。阻塞执行，控制在 150ms 内。
 * ================================================================ */
static void tone(int freq_hz, int duration_ms)
{
    if (freq_hz <= 0) {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        return;
    }

    ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq_hz);
    /* 50% 占空比 */
    uint32_t duty = (1 << 12);  /* 13-bit → 4096 = 50% */
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    /* 静音 */
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

/* ================================================================
 * 全局音效开关 — 跨页面状态同步（Single Source of Truth）
 *
 * g_audio_enabled: 唯一真值源。ui.cpp 设置页同步更新，
 * ui_show_title() 回标题时自动复位为 true。
 * ================================================================ */
static bool g_audio_enabled = true;

/** 设置音效总开关，即时生效，无阻塞 */
void audio_set_enabled(bool en) { g_audio_enabled = en; }

/**
 * audio_play — 播放预定义音效
 *
 * 受 g_audio_enabled 控制，关闭时静默返回。
 * 音效设计：短促(20~150ms)、音高区分度大、单/双音序列。
 */
void audio_play(sound_t snd)
{
    if (!g_audio_enabled) return;
    switch (snd) {
    case SND_PLACE:
        /* 短促高音 */
        tone(523, 60);
        break;

    case SND_AI_MOVE:
        /* 双音 */
        tone(330, 50);
        vTaskDelay(pdMS_TO_TICKS(40));
        tone(392, 50);
        break;

    case SND_WIN:
        /* 上行音阶 */
        tone(262, 80);
        vTaskDelay(pdMS_TO_TICKS(60));
        tone(330, 80);
        vTaskDelay(pdMS_TO_TICKS(60));
        tone(392, 80);
        vTaskDelay(pdMS_TO_TICKS(60));
        tone(523, 150);
        break;

    case SND_LOSE:
        /* 下行音阶 */
        tone(523, 100);
        vTaskDelay(pdMS_TO_TICKS(60));
        tone(392, 100);
        vTaskDelay(pdMS_TO_TICKS(60));
        tone(330, 100);
        vTaskDelay(pdMS_TO_TICKS(60));
        tone(262, 200);
        break;

    case SND_DRAW:
        /* 平调 */
        tone(330, 100);
        vTaskDelay(pdMS_TO_TICKS(50));
        tone(330, 100);
        break;

    case SND_MENU:
        /* 极短滴答 */
        tone(262, 20);
        break;

    default:
        break;
    }
}

/**
 * audio_mute — 紧急硬件静音
 *
 * 直接关断 PWM 输出，不影响 g_audio_enabled 逻辑状态。
 * 用于需立即停止所有声音的场合（如系统错误、休眠前）。
 */
void audio_mute(void)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}
