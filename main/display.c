/*
 * display.c — ILI9341 driver for ESP-2432S028, landscape 320×240.
 * Raw spi_master, direct pixel writes, no framebuffer.
 */
#include "display.h"
#include "app_state.h"
#include "nvs_config.h"
#include "wifi_manager.h"
#include "pin_config.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "display";

/* ── 8×8 font (ASCII 32–126), bit0 = leftmost pixel ────────────────────── */
static const uint8_t s_font[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00},
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00},
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00},
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06},
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00},
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00},
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06},
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00},
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00},
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00},
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00},
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00},
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00},
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00},
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00},
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00},
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00},
    {0x38,0x30,0x30,0x3e,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x1E,0x33,0x3f,0x03,0x1E,0x00},
    {0x1C,0x36,0x06,0x0f,0x06,0x06,0x0F,0x00},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F},
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00},
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E},
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00},
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00},
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00},
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00},
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78},
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00},
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00},
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00},
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F},
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00},
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00},
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00},
};

/* ── ILI9341 commands ───────────────────────────────────────────────────── */
#define ILI_SWRESET  0x01
#define ILI_SLPOUT   0x11
#define ILI_INVOFF   0x20
#define ILI_DISPON   0x29
#define ILI_CASET    0x2A
#define ILI_RASET    0x2B
#define ILI_RAMWR    0x2C
#define ILI_MADCTL   0x36
#define ILI_COLMOD   0x3A
#define ILI_FRMCTR1  0xB1
#define ILI_DFUNCTR  0xB6
#define ILI_PWCTR1   0xC0
#define ILI_PWCTR2   0xC1
#define ILI_VMCTR1   0xC5
#define ILI_VMCTR2   0xC7
#define ILI_GMCTRP1  0xE0
#define ILI_GMCTRN1  0xE1

/* ── SPI pixel buffer ───────────────────────────────────────────────────── */
#define PIXBUF  2048
static uint16_t s_pixbuf[PIXBUF];
static spi_device_handle_t s_spi = NULL;

/* ── SPI helpers ────────────────────────────────────────────────────────── */

static void IRAM_ATTR spi_pre_cb(spi_transaction_t *t)
{
    gpio_set_level(LCD_DC_PIN, (int)(intptr_t)t->user);
}

static void lcd_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length  = 8,
        .flags   = SPI_TRANS_USE_TXDATA,
        .tx_data = {cmd},
        .user    = (void *)0,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void lcd_data(const uint8_t *data, int len)
{
    if (!len) return;
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = data,
        .user      = (void *)1,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void lcd_data1(uint8_t b) { lcd_data(&b, 1); }

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t buf[4];
    lcd_cmd(ILI_CASET);
    buf[0]=x0>>8; buf[1]=x0; buf[2]=x1>>8; buf[3]=x1;
    lcd_data(buf, 4);
    lcd_cmd(ILI_RASET);
    buf[0]=y0>>8; buf[1]=y0; buf[2]=y1>>8; buf[3]=y1;
    lcd_data(buf, 4);
    lcd_cmd(ILI_RAMWR);
}

/* colours are pre-swapped R/B; only need byte-swap for SPI big-endian */
static inline uint16_t enc(uint16_t c) { return (c >> 8) | (c << 8); }

static void fill_pixels(uint16_t color_enc, int count)
{
    int fill = count < PIXBUF ? count : PIXBUF;
    for (int i = 0; i < fill; i++) s_pixbuf[i] = color_enc;
    while (count > 0) {
        int n = count > PIXBUF ? PIXBUF : count;
        spi_transaction_t t = {
            .length    = n * 16,
            .tx_buffer = s_pixbuf,
            .user      = (void *)1,
        };
        spi_device_polling_transmit(s_spi, &t);
        count -= n;
    }
}

/* ── ILI9341 init ───────────────────────────────────────────────────────── */

static void ili9341_init(void)
{
    lcd_cmd(ILI_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Extended power-on sequence */
    lcd_cmd(0xEF); lcd_data((uint8_t[]){0x03,0x80,0x02}, 3);
    lcd_cmd(0xCF); lcd_data((uint8_t[]){0x00,0xC1,0x30}, 3);
    lcd_cmd(0xED); lcd_data((uint8_t[]){0x64,0x03,0x12,0x81}, 4);
    lcd_cmd(0xE8); lcd_data((uint8_t[]){0x85,0x00,0x78}, 3);
    lcd_cmd(0xCB); lcd_data((uint8_t[]){0x39,0x2C,0x00,0x34,0x02}, 5);
    lcd_cmd(0xF7); lcd_data1(0x20);
    lcd_cmd(0xEA); lcd_data((uint8_t[]){0x00,0x00}, 2);

    lcd_cmd(ILI_PWCTR1);  lcd_data1(0x23);
    lcd_cmd(ILI_PWCTR2);  lcd_data1(0x10);
    lcd_cmd(ILI_VMCTR1);  lcd_data((uint8_t[]){0x3E, 0x28}, 2);
    lcd_cmd(ILI_VMCTR2);  lcd_data1(0x86);
    lcd_cmd(ILI_MADCTL);  lcd_data1(LCD_MADCTL);
    lcd_cmd(ILI_COLMOD);  lcd_data1(0x55);
    lcd_cmd(ILI_FRMCTR1); lcd_data((uint8_t[]){0x00, 0x18}, 2);
    lcd_cmd(ILI_DFUNCTR); lcd_data((uint8_t[]){0x08, 0x82, 0x27}, 3);
    lcd_cmd(0xF2); lcd_data1(0x00);
    lcd_cmd(0x26); lcd_data1(0x01);

    lcd_cmd(ILI_GMCTRP1);
    lcd_data((uint8_t[]){0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,
                          0x37,0x07,0x10,0x03,0x0E,0x09,0x00}, 15);
    lcd_cmd(ILI_GMCTRN1);
    lcd_data((uint8_t[]){0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,
                          0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F}, 15);

    lcd_cmd(ILI_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_cmd(ILI_DISPON);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "ILI9341 init done");
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void display_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LCD_DC_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(LCD_DC_PIN, 1);

    spi_bus_config_t bus = {
        .mosi_io_num     = LCD_MOSI_PIN,
        .miso_io_num     = -1,
        .sclk_io_num     = LCD_SCLK_PIN,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = PIXBUF * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = LCD_SPI_FREQ_HZ,
        .mode           = 0,
        .spics_io_num   = LCD_CS_PIN,
        .queue_size     = 7,
        .pre_cb         = spi_pre_cb,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &dev, &s_spi));

    ili9341_init();

    /* Backlight via 8-bit LEDC */
    ledc_timer_config_t tmr = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&tmr);
    ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = LCD_BACKLIGHT_PIN,
        .duty       = 255,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);

    display_clear(COLOR_BLACK);
}

void display_clear(uint16_t color)
{
    display_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (!s_spi || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (x + w > LCD_WIDTH)  w = LCD_WIDTH  - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    set_window(x, y, x + w - 1, y + h - 1);
    fill_pixels(enc(color), w * h);
}

void display_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale)
{
    if (!s_spi || c < 32 || c > 126) return;
    const uint8_t *glyph = s_font[(uint8_t)c - 32];
    int w = 8 * scale, h = 8 * scale;
    uint16_t fg_e = enc(fg), bg_e = enc(bg);

    for (int row = 0; row < 8; row++) {
        uint8_t line = glyph[row];
        for (int rs = 0; rs < scale; rs++) {
            int buf_row = (row * scale + rs) * w;
            for (int col = 0; col < 8; col++) {
                uint16_t px = (line & (1 << col)) ? fg_e : bg_e;
                for (int cs = 0; cs < scale; cs++)
                    s_pixbuf[buf_row + col * scale + cs] = px;
            }
        }
    }

    set_window(x, y, x + w - 1, y + h - 1);
    spi_transaction_t t = {
        .length    = w * h * 16,
        .tx_buffer = s_pixbuf,
        .user      = (void *)1,
    };
    spi_device_polling_transmit(s_spi, &t);
}

void display_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale)
{
    while (*s) {
        display_draw_char(x, y, *s++, fg, bg, scale);
        x += 8 * scale;
        if (x >= LCD_WIDTH) break;
    }
}

/* ── Layout constants (shared by status + settings screens) ─────────────── */
#define ROW_H  (8 * FONT_SCALE + 4)
#define COL_X  8

/* ── Screen mode ────────────────────────────────────────────────────────── */

static screen_mode_t s_screen = SCREEN_STATUS;

screen_mode_t display_get_screen(void) { return s_screen; }

void display_set_screen(screen_mode_t mode)
{
    s_screen = mode;
    display_clear(COLOR_BLACK);
    if (mode == SCREEN_SETTINGS) display_show_settings();
}

void display_set_brightness(int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)(pct * 255 / 100));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void display_show_settings(void)
{
    if (!s_spi) return;
    char buf[48];
    int pct = g_cfg.lcd_brightness;

    /* Title */
    display_draw_string(COL_X, 4, "Settings        ",
        COLOR_CYAN, COLOR_BLACK, FONT_SCALE);
    display_fill_rect(0, 22, LCD_WIDTH, 1, COLOR_GRAY);

    /* Compact brightness section */
    display_draw_string(COL_X, 26, "Brightness", COLOR_WHITE, COLOR_BLACK, 1);

    int bar_w = pct * 300 / 100;
    display_fill_rect(10,       36, bar_w,     8, COLOR_GREEN);
    display_fill_rect(10+bar_w, 36, 300-bar_w, 8, 0x2104);

    snprintf(buf, sizeof(buf), "%3d%%", pct);
    display_draw_string((LCD_WIDTH - 4*16) / 2, 48, buf,
        COLOR_WHITE, COLOR_BLACK, FONT_SCALE);

    /* Separator before buttons */
    display_fill_rect(0, 68, LCD_WIDTH, 1, COLOR_GRAY);

    /* ±10% buttons (y=72, h=55 → ends y=127) — touch zone ty < 130 */
    display_fill_rect(5,   72, 145, 55, 0x2104);
    display_fill_rect(170, 72, 145, 55, 0x2104);
    display_draw_string(5   + (145 - 5*16) / 2, 88, "-10% ", COLOR_WHITE, 0x2104, FONT_SCALE);
    display_draw_string(170 + (145 - 5*16) / 2, 88, "+10% ", COLOR_WHITE, 0x2104, FONT_SCALE);

    /* Separator before info */
    display_fill_rect(0, 130, LCD_WIDTH, 1, COLOR_GRAY);

    /* DS18B20 addresses */
    state_lock();
    char s1[20], s2[20];
    memcpy(s1, g_state.sensor1_addr, sizeof(s1));
    memcpy(s2, g_state.sensor2_addr, sizeof(s2));
    state_unlock();

    snprintf(buf, sizeof(buf), "T1: %.34s", s1[0] ? s1 : "---");
    display_draw_string(COL_X, 134, buf, COLOR_WHITE, COLOR_BLACK, 1);
    snprintf(buf, sizeof(buf), "T2: %.34s", s2[0] ? s2 : "---");
    display_draw_string(COL_X, 144, buf, COLOR_WHITE, COLOR_BLACK, 1);

    display_fill_rect(0, 154, LCD_WIDTH, 1, COLOR_GRAY);

    /* MQTT info — truncate to fit 320px at scale 1 (40 chars from x=0) */
    snprintf(buf, sizeof(buf), "MQTT: %.33s",
        g_cfg.mqtt_server[0] ? g_cfg.mqtt_server : "---");
    display_draw_string(COL_X, 158, buf, COLOR_CYAN, COLOR_BLACK, 1);

    snprintf(buf, sizeof(buf), "Top:  %.34s",
        g_cfg.mqtt_topic[0] ? g_cfg.mqtt_topic : "---");
    display_draw_string(COL_X, 168, buf, COLOR_ORANGE, COLOR_BLACK, 1);

    /* Back button — touch zone ty > 178, button height = font + 4px padding each side */
    display_fill_rect(0, 178, LCD_WIDTH, 1, COLOR_GRAY);
    display_fill_rect(0, 179, LCD_WIDTH, 16 + 8, 0x4208);          // 16px font + 8px padding
    display_fill_rect(0, 179 + 16 + 8, LCD_WIDTH, LCD_HEIGHT - (179 + 16 + 8), COLOR_BLACK);
    display_draw_string((LCD_WIDTH - 4 * 16) / 2, 179 + 4,
        "Back", COLOR_WHITE, 0x4208, FONT_SCALE);
}


/* ── Status screen ──────────────────────────────────────────────────────── */

void display_status_msg(const char *line1, const char *line2)
{
    display_clear(COLOR_BLACK);
    int cy = LCD_HEIGHT / 2 - ROW_H;
    if (line1 && line1[0])
        display_draw_string(COL_X, cy, line1, COLOR_CYAN, COLOR_BLACK, FONT_SCALE);
    if (line2 && line2[0])
        display_draw_string(COL_X, cy + ROW_H + 4, line2, COLOR_WHITE, COLOR_BLACK, FONT_SCALE);
}

void display_update_status(void)
{
    if (!s_spi) return;
    if (s_screen != SCREEN_STATUS) return;

    state_lock();
    float t1   = g_state.sensor1_temp;
    float t2   = g_state.sensor2_temp;
    float sol  = g_state.solar_avg_10min;
    bool  r1   = g_state.relay1_state;
    bool  r2   = g_state.relay2_state;
    bool  lock = g_state.error_lockout;
    bool  s1ok = g_state.sensor1_ok;
    bool  s2ok = g_state.sensor2_ok;
    state_unlock();

    /* Clear once on first call to wipe boot screen, then draw in-place */
    static bool s_first = true;
    if (s_first) { display_clear(COLOR_BLACK); s_first = false; }

    char buf[48];
    char ip[20];
    wifi_manager_get_ip(ip, sizeof(ip));
    int rssi = wifi_manager_get_rssi();
    const int sw = 8 * FONT_SCALE;
    int y = 4;

    /* Title + clock on one line */
    display_draw_string(COL_X, y, "EHeating", COLOR_CYAN, COLOR_BLACK, FONT_SCALE);

    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char clock_buf[9];
    snprintf(clock_buf, sizeof(clock_buf), "%02d:%02d:%02d",
        tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    display_draw_string(LCD_WIDTH - sw * 8 - 4, y, clock_buf, COLOR_WHITE, COLOR_BLACK, FONT_SCALE);
    y += ROW_H;
    /* 4px space above and below separator line */
    display_fill_rect(0, y, LCD_WIDTH, 1, COLOR_GRAY);
    y += 5;

    snprintf(buf, sizeof(buf), s1ok ? "T1: %5.1f C" : "T1: ERROR  ", t1);
    display_draw_string(COL_X, y, buf,
        s1ok ? COLOR_WHITE : COLOR_RED, COLOR_BLACK, FONT_SCALE);
    y += ROW_H;

    snprintf(buf, sizeof(buf), s2ok ? "T2: %5.1f C" : "T2: ERROR  ", t2);
    display_draw_string(COL_X, y, buf,
        s2ok ? COLOR_WHITE : COLOR_RED, COLOR_BLACK, FONT_SCALE);
    y += ROW_H;

    snprintf(buf, sizeof(buf), "Solar: %6.0f W", sol);
    display_draw_string(COL_X, y, buf,
        sol > g_cfg.solar_threshold ? COLOR_GREEN : COLOR_RED,
        COLOR_BLACK, FONT_SCALE);
    y += ROW_H;

    snprintf(buf, sizeof(buf), "Thr:   %6.0f W", g_cfg.solar_threshold);
    display_draw_string(COL_X, y, buf, COLOR_YELLOW, COLOR_BLACK, FONT_SCALE);
    y += ROW_H;

    snprintf(buf, sizeof(buf), "Relay1: %s", r1 ? "ON " : "OFF");
    display_draw_string(COL_X, y, buf,
        r1 ? COLOR_GREEN : COLOR_RED, COLOR_BLACK, FONT_SCALE);
    y += ROW_H;

    snprintf(buf, sizeof(buf), "Relay2: %s", r2 ? "ON " : "OFF");
    display_draw_string(COL_X, y, buf,
        r2 ? COLOR_GREEN : COLOR_RED, COLOR_BLACK, FONT_SCALE);
    y += ROW_H;
    /* 4px space above and below separator line */
    display_fill_rect(0, y, LCD_WIDTH, 1, COLOR_GRAY);
    y += 5;

    snprintf(buf, sizeof(buf), "SSID: %.13s", g_cfg.wifi_ssid[0] ? g_cfg.wifi_ssid : "---");
    display_draw_string(COL_X, y, buf, COLOR_ORANGE, COLOR_BLACK, FONT_SCALE);
    y += ROW_H;

    snprintf(buf, sizeof(buf), "IP: %-15s", ip);
    display_draw_string(COL_X, y, buf, COLOR_ORANGE, COLOR_BLACK, FONT_SCALE);
    y += ROW_H;

    snprintf(buf, sizeof(buf), "RSSI: %d dBm   ", rssi);
    display_draw_string(COL_X, y, buf, COLOR_ORANGE, COLOR_BLACK, FONT_SCALE);
    y += ROW_H;

    /* Lockout at bottom — always drawn */
    if (lock) {
        display_fill_rect(0, y, LCD_WIDTH, ROW_H, COLOR_RED);
        display_draw_string(COL_X, y + (ROW_H - 8) / 2, "!! LOCKOUT: overheat !!",
            COLOR_WHITE, COLOR_RED, 1);
    } else {
        display_fill_rect(0, y, LCD_WIDTH, ROW_H, COLOR_BLACK);
    }
}
