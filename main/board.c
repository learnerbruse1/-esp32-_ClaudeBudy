/**
 * ================================================================
 * board.c — 显示(ST7735S) + LVGL + 6 键输入
 * ================================================================
 * 显示初始化序列移植自实机验证过的 xuyuejia/XUEERSI_ESP32_Board。
 * 按键以 LVGL "keypad" 输入设备形式接入, 配合默认输入组实现方向键导航:
 *   UP→PREV  DOWN→NEXT  A→ENTER  B→ESC  LEFT→LEFT  RIGHT→RIGHT
 * ================================================================
 */
#include "board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"   /* ST7789 驱动, 指令集兼容 ST7735 */
#include "esp_lvgl_port.h"

static const char *TAG = "BOARD";

/* ---- 引脚 (实机验证) ---- */
#define PIN_SPI_MOSI    23
#define PIN_SPI_SCLK    18
#define PIN_SPI_MISO    19   /* 与 LCD RST 共用; ST7735 不回读, 仅占位 */
#define PIN_TFT_DC      4
#define PIN_TFT_CS      5
#define PIN_TFT_RST     -1   /* 本板无独立复位线 */
#define LCD_SPI_HOST    SPI2_HOST
#define LCD_PCLK_HZ     (40 * 1000 * 1000)

/* 物理 128x160 竖屏 */
#define LCD_PHYS_W      128
#define LCD_PHYS_H      160

static lv_indev_t *s_indev = NULL;
static lv_group_t *s_group = NULL;

/* ================================================================
 * 显示初始化 (ST7735S via esp_lcd_panel_st7789 + 专属寄存器序列)
 * ================================================================ */
static void display_init(esp_lcd_panel_io_handle_t *io_out, esp_lcd_panel_handle_t *panel_out)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .sclk_io_num = PIN_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOARD_LCD_H_RES * BOARD_LCD_V_RES * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_TFT_CS,
        .dc_gpio_num = PIN_TFT_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &io));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_TFT_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel));

    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    /* ST7735S 专属: 帧率 / 反转 / 电源 / Gamma (来自实机验证序列) */
    esp_lcd_panel_io_tx_param(io, 0xB1, (uint8_t[]){0x01, 0x2C, 0x2D}, 3);
    esp_lcd_panel_io_tx_param(io, 0xB2, (uint8_t[]){0x01, 0x2C, 0x2D}, 3);
    esp_lcd_panel_io_tx_param(io, 0xB3, (uint8_t[]){0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}, 6);
    esp_lcd_panel_io_tx_param(io, 0xB4, (uint8_t[]){0x07}, 1);
    esp_lcd_panel_io_tx_param(io, 0xC0, (uint8_t[]){0xA2, 0x02, 0x84}, 3);
    esp_lcd_panel_io_tx_param(io, 0xC1, (uint8_t[]){0xC5}, 1);
    esp_lcd_panel_io_tx_param(io, 0xC2, (uint8_t[]){0x0A, 0x00}, 2);
    esp_lcd_panel_io_tx_param(io, 0xC3, (uint8_t[]){0x8A, 0x2A}, 2);
    esp_lcd_panel_io_tx_param(io, 0xC4, (uint8_t[]){0x8A, 0xEE}, 2);
    esp_lcd_panel_io_tx_param(io, 0xC5, (uint8_t[]){0x0E}, 1);
    esp_lcd_panel_io_tx_param(io, 0xE0, (uint8_t[]){
        0x0F,0x1A,0x0F,0x18,0x2F,0x28,0x20,0x22,0x1F,0x1B,0x23,0x37,0x00,0x07,0x02,0x10}, 16);
    esp_lcd_panel_io_tx_param(io, 0xE1, (uint8_t[]){
        0x0F,0x1B,0x0F,0x17,0x33,0x2C,0x29,0x2E,0x30,0x30,0x39,0x3F,0x00,0x07,0x03,0x10}, 16);
    esp_lcd_panel_io_tx_param(io, 0x13, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* 旋转 90° → 横屏 160x128 (MADCTL = MX|MV = 0x60) */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    ESP_LOGI(TAG, "显示初始化完成: %dx%d", BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    *io_out = io;
    *panel_out = panel;
}

/* ================================================================
 * 按键 LVGL 输入设备
 * ================================================================ */
bool board_btn_is_down(int gpio)
{
    return gpio_get_level((gpio_num_t)gpio) == 0;   /* 低电平 = 按下 */
}

static void btn_gpio_init(void)
{
    /* 普通 GPIO (2/13/27/12): 启用内部上拉 */
    gpio_config_t pu = {
        .pin_bit_mask = (1ULL << BOARD_BTN_UP)   | (1ULL << BOARD_BTN_DOWN) |
                        (1ULL << BOARD_BTN_LEFT)  | (1ULL << BOARD_BTN_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pu);

    /* 仅输入引脚 (34=A / 35=右): 无内部上拉, 依赖板载外部上拉 */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_BTN_A) | (1ULL << BOARD_BTN_RIGHT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

/* LVGL keypad read_cb:
 *   - A=确认(ENTER) / B=返回(ESC): 边沿检测, 一次短按只触发一次
 *   - 方向键: 按住可连续 (翻列表/滚动)
 */
static void keypad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static bool a_latched = false, b_latched = false;
    static int  a_rel = 0, b_rel = 0;

    bool a = board_btn_is_down(BOARD_BTN_A);
    bool b = board_btn_is_down(BOARD_BTN_B);

    /* 释放消抖: 连续读到松开才解除锁存, 防止抖动重复触发 */
    if (!a) { if (++a_rel > 2) a_latched = false; } else a_rel = 0;
    if (!b) { if (++b_rel > 2) b_latched = false; } else b_rel = 0;

    uint32_t key = 0;
    if (a && !a_latched)      { key = LV_KEY_ENTER; a_latched = true; }  /* 短按A = 确认 */
    else if (b && !b_latched) { key = LV_KEY_ESC;   b_latched = true; }  /* 短按B = 返回 */
    else if (board_btn_is_down(BOARD_BTN_UP))    key = LV_KEY_PREV;
    else if (board_btn_is_down(BOARD_BTN_DOWN))  key = LV_KEY_NEXT;
    else if (board_btn_is_down(BOARD_BTN_LEFT))  key = LV_KEY_LEFT;
    else if (board_btn_is_down(BOARD_BTN_RIGHT)) key = LV_KEY_RIGHT;

    if (key != 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = key;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ================================================================
 * LVGL 初始化 (esp_lvgl_port) + 注册显示与输入
 * ================================================================ */
static void lvgl_setup(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel)
{
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 8192,
        .task_affinity = 1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    /* 局部缓冲 (内部 SRAM, 给 Wi-Fi/TLS 让出更多内存): 160x40 双缓冲 ≈ 25KB */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = BOARD_LCD_H_RES * 40,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = true, .mirror_x = true, .mirror_y = false },
        .flags = { .buff_dma = true, .buff_spiram = false, .swap_bytes = true },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (!disp) { ESP_LOGE(TAG, "LVGL 显示添加失败"); abort(); }

    /* 注册键盘输入设备 + 默认输入组 */
    lvgl_port_lock(0);
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_indev, keypad_read_cb);
    lv_indev_set_display(s_indev, disp);

    s_group = lv_group_create();
    lv_group_set_default(s_group);
    lv_indev_set_group(s_indev, s_group);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "LVGL 初始化完成");
}

/* ================================================================
 * 公共接口
 * ================================================================ */
void board_init(void)
{
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    btn_gpio_init();
    display_init(&io, &panel);
    lvgl_setup(io, panel);
}

bool board_lock(int timeout_ms)   { return lvgl_port_lock(timeout_ms); }
void board_unlock(void)           { lvgl_port_unlock(); }
lv_group_t *board_group(void)     { return s_group; }
lv_indev_t *board_indev(void)     { return s_indev; }
