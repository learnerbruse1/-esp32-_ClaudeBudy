/**
 * ================================================================
 * main.c — 五子棋人机对弈 主入口（中文注释版）
 * ================================================================
 *
 * 【硬件平台】学而思编程机 / 小喵掌机
 *   - MCU:  ESP32-WROVER-B (双核 240MHz, 8MB PSRAM)
 *   - LCD:  ST7735S 128×160 竖屏 → LVGL 软件旋转 90° → 160×128 横屏
 *   - 接口: 4 线 SPI (MOSI=23, SCLK=18, DC=4, CS=5), MISO/RST 共用 GPIO19
 *   - 输入: 6 键 (↑↓←→ A B), 低电平触发
 *   - 音频: 无源蜂鸣器 GPIO14, LEDC PWM 驱动
 *
 * 【软件栈】ESP-IDF v5.4 + LVGL v9.2 + Pela AI 引擎
 * 【创建日期】2026-06-20  |  【最后修改】2026-06-27
 *
 * TEST_MODE=1 → 硬件测试模式（不启动 LVGL，仅串口输出测试结果）
 * TEST_MODE=0 → 正常游戏模式
 * ================================================================
 */

#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lvgl_port.h"
#include "board.h"
#include "evaluate.h"
#include "ipc.h"
#include "ui.h"
#include "input.h"
#include "audio.h"
#include "game.h"

/** 测试模式开关: 1=硬件测试 0=正常游戏。编译时切换，不依赖运行时配置。 */
#define TEST_MODE 0

static const char *TAG = "GOMOKU";

/* ---- 硬件引脚定义（小喵掌机固定 GPIO 映射）---- */
#define PIN_SPI_MOSI    23   /* SPI 主机输出 */
#define PIN_SPI_SCLK    18   /* SPI 时钟 */
#define PIN_SPI_MISO    19   /* SPI 输入（与 RST 复用） */
#define PIN_TFT_DC      4    /* TFT 数据/命令选择 */
#define PIN_TFT_CS      5    /* TFT 片选 */
#define PIN_TFT_RST     -1   /* 不使用硬件复位（MISO 复用） */
#define PIN_TFT_BL      -1   /* 无背光控制引脚 */

/* ---- 显示参数（物理 128×160 竖屏 → 逻辑 160×128 横屏）---- */
#define DISP_WIDTH      128   /* 物理宽（竖屏） */
#define DISP_HEIGHT     160   /* 物理高（竖屏） */
#define SCR_HOR_RES     DISP_HEIGHT  /* 逻辑宽 160（横屏用） */
#define SCR_VER_RES     DISP_WIDTH   /* 逻辑高 128（横屏用） */

#define SPI_HOST_ID     SPI2_HOST          /* 使用 SPI2 外设 */
#define SPI_CLK_HZ      (40 * 1000 * 1000) /* SPI 时钟 40MHz */

/* ================================================================
 * display_init — ST7735S 显示初始化
 *
 * 使用 esp_lcd 框架驱动 ST7735S。
 * ⚠️ 关键踩坑：ST7735 不做硬件旋转（swap_xy/mirror），
 *    旋转交给 LVGL 软件层处理，否则双重旋转导致撕裂。
 *    gap 必须为 0，设非零值会导致斜纹撕裂。
 * ================================================================ */
static void display_init(esp_lcd_panel_io_handle_t *io_out, esp_lcd_panel_handle_t *panel_out)
{
    ESP_LOGI(TAG, "初始化 SPI 总线...");

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .sclk_io_num = PIN_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SCR_HOR_RES * SCR_VER_RES * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST_ID, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_TFT_CS,
        .dc_gpio_num = PIN_TFT_DC,
        .spi_mode = 0,
        .pclk_hz = SPI_CLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };

    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI_HOST_ID, &io_cfg, &io_handle));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_TFT_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    esp_lcd_panel_handle_t panel_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    /* ST7735 专属初始化序列（Gamma/电源/帧率寄存器） */
    esp_lcd_panel_io_tx_param(io_handle, 0xB1, (uint8_t[]){0x01, 0x2C, 0x2D}, 3);
    esp_lcd_panel_io_tx_param(io_handle, 0xB2, (uint8_t[]){0x01, 0x2C, 0x2D}, 3);
    esp_lcd_panel_io_tx_param(io_handle, 0xB3, (uint8_t[]){0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}, 6);
    esp_lcd_panel_io_tx_param(io_handle, 0xB4, (uint8_t[]){0x07}, 1);
    esp_lcd_panel_io_tx_param(io_handle, 0xC0, (uint8_t[]){0xA2, 0x02, 0x84}, 3);
    esp_lcd_panel_io_tx_param(io_handle, 0xC1, (uint8_t[]){0xC5}, 1);
    esp_lcd_panel_io_tx_param(io_handle, 0xC2, (uint8_t[]){0x0A, 0x00}, 2);
    esp_lcd_panel_io_tx_param(io_handle, 0xC3, (uint8_t[]){0x8A, 0x2A}, 2);
    esp_lcd_panel_io_tx_param(io_handle, 0xC4, (uint8_t[]){0x8A, 0xEE}, 2);
    esp_lcd_panel_io_tx_param(io_handle, 0xC5, (uint8_t[]){0x0E}, 1);
    esp_lcd_panel_io_tx_param(io_handle, 0xE0, (uint8_t[]){
        0x0F, 0x1A, 0x0F, 0x18, 0x2F, 0x28, 0x20, 0x22,
        0x1F, 0x1B, 0x23, 0x37, 0x00, 0x07, 0x02, 0x10
    }, 16);
    esp_lcd_panel_io_tx_param(io_handle, 0xE1, (uint8_t[]){
        0x0F, 0x1B, 0x0F, 0x17, 0x33, 0x2C, 0x29, 0x2E,
        0x30, 0x30, 0x39, 0x3F, 0x00, 0x07, 0x03, 0x10
    }, 16);
    esp_lcd_panel_io_tx_param(io_handle, 0x13, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* ⚠️ 仅开显示，不做任何硬件旋转。gap 保持 0（设非零会斜纹撕裂）。 */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "显示初始化完成: %dx%d 横屏", SCR_HOR_RES, SCR_VER_RES);
    *io_out = io_handle;
    *panel_out = panel_handle;
}

/* ================================================================
 * lvgl_init — LVGL 图形库初始化
 *
 * 创建独立 LVGL 任务（Core 1, 优先级 5, 栈 8KB），
 * 配置双缓冲 (DMA) + RGB565 色深 + 软件旋转。
 *
 * 旋转参数说明：
 *   swap_xy=true   → 交换 XY 轴（竖→横）
 *   mirror_x=true  → X 轴镜像（修正方向）
 *   swap_bytes=true → 字节序交换（ESP32 小端 → LCD 大端）
 * ================================================================ */
static void lvgl_init(esp_lcd_panel_io_handle_t io_handle, esp_lcd_panel_handle_t panel)
{
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 5,
        .task_stack = 8192,
        .task_affinity = 1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel,
        .buffer_size = SCR_HOR_RES * SCR_VER_RES,
        .double_buffer = true,
        .hres = SCR_HOR_RES,
        .vres = SCR_VER_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .swap_bytes = true,
        },
    };

    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "LVGL 显示设备添加失败!");
        abort();
    }
    ESP_LOGI(TAG, "LVGL 初始化完成");
}

/* ================================================================
 * app_main — ESP-IDF 入口函数
 *
 * 启动顺序：
 *   1. 棋盘数据层初始化 (board_init)
 *   2. AI 模式表生成 (evaluate_init, 1MB PSRAM, ~0.3s)
 *   3. 双核 IPC 框架 (ipc_init)
 *   4. 显示+LVGL (display_init → lvgl_init → ui_init)
 *   5. 输入+音效 (input_init → audio_init)
 *   6. 进入主循环：标题→设置→对弈→结果→循环
 *
 * ⚠️ LVGL 操作必须在 lvgl_port_lock/unlock 之间执行（线程安全）。
 * ================================================================ */
void app_main(void)
{
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "XRS Gomoku — 五子棋人机对弈");
    ESP_LOGI(TAG, "ESP-IDF v5.4, ESP32-WROVER-B");
    ESP_LOGI(TAG, "AI: Pela engine (移植中)");
    ESP_LOGI(TAG, "==========================================");

#if TEST_MODE
    /* ================================================================
     * 测试模式：运行硬件测试
     * ================================================================ */
    ESP_LOGI(TAG, ">>> 进入测试模式 <<<");

    /* Phase 2 测试：棋盘 */
    extern void test_board_run(void);
    test_board_run();

    /* Phase 3 测试：评估引擎 */
    extern void test_evaluate_run(void);
    test_evaluate_run();

    /* Phase 4 测试：AI 搜索引擎 */
    extern void test_alfabeta_run(void);
    test_alfabeta_run();

    /* Phase 5 测试：游戏逻辑 */
    extern void test_game_run(void);
    test_game_run();

    /* Phase 6 测试：AI 桥接层 */
    extern void test_ai_bridge_run(void);
    test_ai_bridge_run();

    /* Phase 7 测试：双核 IPC 框架 */
    extern void test_ipc_run(void);
    test_ipc_run();

    /* Phase 9 测试：输入 + 音效 */
    extern void test_input_audio_run(void);
    test_input_audio_run();

    ESP_LOGI(TAG, "测试完成，进入空闲循环...");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#else
    /* ================================================================
     * 正常运行模式：完整五子棋游戏
     * ================================================================ */

    /* 0. 基础设施：棋盘 → AI模式表 → 双核通信 */
    ESP_LOGI(TAG, "Starting Gomoku...");
    board_init();           /* 分配 459 格 Tsquare 数组 */
    evaluate_init();        /* 生成 K[262144] 模式匹配表 (1MB PSRAM) */
    ipc_init();             /* 启动 Core 0 AI 搜索任务 */

    /* 1. 显示 + UI：SPI→ST7735→LVGL→界面 */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    display_init(&io_handle, &panel);
    lvgl_init(io_handle, panel);

    lvgl_port_lock(0);
    ui_init();
    lvgl_port_unlock();

    /* 2. 输入 + 音效：6键 GPIO ISR + 蜂鸣器 PWM */
    input_init();           /* GPIO 下降沿中断，150ms 防抖 */
    audio_init();           /* LEDC PWM @ GPIO14 */

    ESP_LOGI(TAG, "Ready — Press A");

    /* ================================================================
     * 主循环 — 事件驱动状态机
     *
     * 三页面流转：标题 → 设置 → 游戏 → 结果 → 标题/游戏
     * 每 100ms 轮询一次按键，BTN_NONE 时让出 CPU。
     * ================================================================ */
    difficulty_t diff = DIFF_MEDIUM;
    int cx = 7, cy = 7;  /* 光标初始在棋盘中心 */

    while (1) {
        ui_screen_t scr = ui_get_current_screen();
        btn_event_t evt = input_wait(100);

        /* ---- 标题画面：A 键进入设置 ---- */
        if (scr == UI_SCREEN_TITLE) {
            if (evt == BTN_A) {
                audio_play(SND_MENU);    /* 导航反馈音 */
                lvgl_port_lock(0);
                ui_show_settings();      /* 切换到设置页面 */
                lvgl_port_unlock();
            }
            continue;
        }

        /* ---- 设置画面：↑↓选行，←→切值，A=确认 START，B=回标题 ---- */
        if (scr == UI_SCREEN_DIFF_SEL) {
            switch (evt) {
            case BTN_UP:    audio_play(SND_MENU); lvgl_port_lock(0); ui_settings_move_row(-1); lvgl_port_unlock(); break;
            case BTN_DOWN:  audio_play(SND_MENU); lvgl_port_lock(0); ui_settings_move_row(+1); lvgl_port_unlock(); break;
            case BTN_LEFT:  audio_play(SND_MENU); lvgl_port_lock(0); ui_settings_change_val(-1); lvgl_port_unlock(); break;
            case BTN_RIGHT: audio_play(SND_MENU); lvgl_port_lock(0); ui_settings_change_val(+1); lvgl_port_unlock(); break;
            case BTN_A:
                if (ui_settings_get_selected_row() == 3) { /* 焦点在 START */
                    diff = (difficulty_t)(ui_settings_get_difficulty() / 2);
                    game_init(diff);
                    cx = cy = 7;
                    lvgl_port_lock(0);
                    ui_show_game();
                    ui_update_board();
                    ui_move_cursor(cx, cy);
                    ui_set_status("Your turn");
                    lvgl_port_unlock();
                    audio_play(SND_PLACE);
                }
                break;
            case BTN_B:
                audio_play(SND_MENU);
                lvgl_port_lock(0); ui_show_title(); lvgl_port_unlock();
                break;
            default: break;
            }
            continue;
        }

        /* ---- 游戏对弈：↑↓←→移光标，A=落子，B短=切音效，B长=回标题 ---- */
        if (scr == UI_SCREEN_GAME) {
            game_state_t gs = game_get_state();

            /* 玩家回合：处理方向键 + A/B 键 */
            if (gs == STATE_PLAYER_TURN) {
                switch (evt) {
                case BTN_UP:    if (cy > 0)  cy--; lvgl_port_lock(0); ui_move_cursor(cx, cy); lvgl_port_unlock(); break;
                case BTN_DOWN:  if (cy < 14) cy++; lvgl_port_lock(0); ui_move_cursor(cx, cy); lvgl_port_unlock(); break;
                case BTN_LEFT:  if (cx > 0)  cx--; lvgl_port_lock(0); ui_move_cursor(cx, cy); lvgl_port_unlock(); break;
                case BTN_RIGHT: if (cx < 14) cx++; lvgl_port_lock(0); ui_move_cursor(cx, cy); lvgl_port_unlock(); break;
                case BTN_A: {
                    int r = game_player_move(cx, cy);
                    if (r < 0) break;  /* 非法落子 */
                    audio_play(SND_PLACE);
                    lvgl_port_lock(0);
                    ui_draw_stone(cx, cy, true);
                    lvgl_port_unlock();
                    gs = game_get_state();  /* 可能变成 WIN/DRAW/AI_THINKING */
                    break;
                }
                case BTN_B:
                    lvgl_port_lock(0); ui_sound_toggle(); lvgl_port_unlock();
                    break;
                case BTN_B_LONG:
                    audio_set_enabled(true);
                    lvgl_port_lock(0); ui_update_sound_icon(); lvgl_port_unlock();
                    audio_play(SND_MENU);
                    lvgl_port_lock(0); ui_show_title(); lvgl_port_unlock();
                    break;
                default: break;
                }
            }

            /* AI 回合：调用 Pela 引擎搜索最佳着法（可能耗时 0.5~5s） */
            if (game_get_state() == STATE_AI_THINKING) {
                lvgl_port_lock(0);
                ui_set_status("AI thinking...");
                lvgl_port_unlock();

                int aix, aiy;
                int r = game_ai_move(&aix, &aiy);
                if (r == 0) {
                    audio_play(SND_AI_MOVE);
                    lvgl_port_lock(0);
                    ui_draw_stone(aix, aiy, false);
                    ui_move_cursor(aix, aiy);
                    ui_set_status("Your turn");
                    lvgl_port_unlock();
                }
            }

            /* 结果处理：显示结果画面 → 播放音效 → 等待重开/回标题 */
            gs = game_get_state();
            if (gs == STATE_WIN || gs == STATE_LOSE || gs == STATE_DRAW) {
                lvgl_port_lock(0);
                ui_show_result(gs);
                lvgl_port_unlock();
                if (gs == STATE_WIN) audio_play(SND_WIN);
                else if (gs == STATE_LOSE) audio_play(SND_LOSE);
                else audio_play(SND_DRAW);

                /* 等待玩家选择：A=重新开始  B=返回标题 */
                while (1) {
                    btn_event_t e = input_wait(200);
                    if (e == BTN_A) {
                        game_restart();
                        cx = cy = 7;
                        lvgl_port_lock(0);
                        ui_show_game();
                        ui_update_board();
                        ui_move_cursor(cx, cy);
                        ui_set_status("Your turn");
                        lvgl_port_unlock();
                        audio_play(SND_PLACE);
                        break;
                    }
                    if (e == BTN_B || e == BTN_B_LONG) {
                        lvgl_port_lock(0); ui_show_title(); lvgl_port_unlock();
                        audio_play(SND_MENU);
                        break;
                    }
                }
            }
            continue;
        }
    }
#endif
}
