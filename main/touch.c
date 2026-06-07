#include "touch.h"
#include "pin_config.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "touch";
static spi_device_handle_t s_spi = NULL;

// Runtime calibration: raw ADC value at each screen edge.
// 0x90 = physical X axis (left≈3742, right≈260).
// 0xD0 = physical Y axis (top≈3667, bottom≈367).
static float s_x0   = 3742, s_x319 = 260;
static float s_y0   = 3667, s_y239 = 367;

void touch_apply_cal(int x0, int x319, int y0, int y239)
{
    s_x0   = (float)x0;
    s_x319 = (float)x319;
    s_y0   = (float)y0;
    s_y239 = (float)y239;
    ESP_LOGI(TAG, "cal: x0=%d x319=%d y0=%d y239=%d", x0, x319, y0, y239);
}

void touch_init(void)
{
    gpio_config_t irq = {
        .pin_bit_mask = (1ULL << TOUCH_IRQ_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&irq);

    spi_bus_config_t bus = {
        .mosi_io_num     = TOUCH_MOSI_PIN,
        .miso_io_num     = TOUCH_MISO_PIN,
        .sclk_io_num     = TOUCH_CLK_PIN,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TOUCH_SPI_HOST, &bus, SPI_DMA_DISABLED));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 2 * 1000 * 1000,
        .mode           = 0,
        .spics_io_num   = TOUCH_CS_PIN,
        .queue_size     = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(TOUCH_SPI_HOST, &dev, &s_spi));
    ESP_LOGI(TAG, "XPT2046 init done");
}

static uint16_t read_raw_ch(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 24,
        .flags  = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
    };
    t.tx_data[0] = cmd;
    t.tx_data[1] = 0;
    t.tx_data[2] = 0;
    spi_device_polling_transmit(s_spi, &t);
    return ((uint16_t)t.rx_data[1] << 8 | t.rx_data[2]) >> 3;
}

static bool sample_raw(int *out_rx, int *out_ry)
{
    if (!s_spi) return false;
    if (gpio_get_level(TOUCH_IRQ_PIN) != 0) return false;
    int32_t rx = 0, ry = 0;
    for (int i = 0; i < 8; i++) {
        rx += read_raw_ch(0x90);  // 0x90 = physical X axis on this panel
        ry += read_raw_ch(0xD0);  // 0xD0 = physical Y axis on this panel
    }
    *out_rx = (int)(rx / 8);
    *out_ry = (int)(ry / 8);
    return true;
}

bool touch_read_raw(int *out_rx, int *out_ry)
{
    return sample_raw(out_rx, out_ry);
}

bool touch_read(int *out_x, int *out_y)
{
    int rx, ry;
    if (!sample_raw(&rx, &ry)) return false;

    float fx = (float)(rx - s_x0) / (s_x319 - s_x0);
    float fy = (float)(ry - s_y0) / (s_y239 - s_y0);

    int sx = (int)(fx * (LCD_WIDTH  - 1) + 0.5f);
    int sy = (int)(fy * (LCD_HEIGHT - 1) + 0.5f);

    if (sx < 0) sx = 0;
    if (sx >= LCD_WIDTH)  sx = LCD_WIDTH  - 1;
    if (sy < 0) sy = 0;
    if (sy >= LCD_HEIGHT) sy = LCD_HEIGHT - 1;

    *out_x = sx;
    *out_y = sy;
    return true;
}
