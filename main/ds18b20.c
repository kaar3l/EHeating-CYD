#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "ds18b20.h"

static const char *TAG = "ds18b20";
static int s_gpio = -1;

// ---- low-level 1-wire ----

static void ow_drive_low(void)
{
    gpio_set_direction(s_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(s_gpio, 0);
}

static void ow_release(void)
{
    gpio_set_direction(s_gpio, GPIO_MODE_INPUT);
}

static int ow_read_bit(void)
{
    return gpio_get_level(s_gpio);
}

// returns true if presence pulse detected
static bool ow_reset(void)
{
    ow_drive_low();
    esp_rom_delay_us(480);
    ow_release();
    esp_rom_delay_us(70);
    bool presence = (ow_read_bit() == 0);
    esp_rom_delay_us(410);
    return presence;
}

static void ow_write_bit(int bit)
{
    if (bit) {
        ow_drive_low();
        esp_rom_delay_us(6);
        ow_release();
        esp_rom_delay_us(64);
    } else {
        ow_drive_low();
        esp_rom_delay_us(60);
        ow_release();
        esp_rom_delay_us(10);
    }
}

static int ow_read_bit_timed(void)
{
    ow_drive_low();
    esp_rom_delay_us(3);
    ow_release();
    esp_rom_delay_us(10);
    int bit = ow_read_bit();
    esp_rom_delay_us(53);
    return bit;
}

static void ow_write_byte(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        ow_write_bit(b & 1);
        b >>= 1;
    }
}

static uint8_t ow_read_byte(void)
{
    uint8_t b = 0;
    for (int i = 0; i < 8; i++) {
        b |= (ow_read_bit_timed() << i);
    }
    return b;
}

static uint8_t crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (int j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ byte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            byte >>= 1;
        }
    }
    return crc;
}

// ---- ROM search ----

static uint8_t s_last_discrepancy;
static uint8_t s_last_device_flag;
static uint8_t s_rom_buf[8];

static void search_init(void)
{
    s_last_discrepancy = 0;
    s_last_device_flag = 0;
    memset(s_rom_buf, 0, 8);
}

// returns 1 if a device found, 0 if done
static int search_next(uint8_t *rom_out)
{
    if (s_last_device_flag) return 0;

    if (!ow_reset()) {
        search_init();
        return 0;
    }

    ow_write_byte(0xF0); // SEARCH ROM

    uint8_t rom_bit_num = 1;
    uint8_t last_zero = 0;
    uint8_t rom_byte_num = 0;
    uint8_t rom_byte_mask = 1;

    for (int i = 0; i < 64; i++) {
        int id_bit      = ow_read_bit_timed();
        int cmp_id_bit  = ow_read_bit_timed();
        int search_dir;

        if (id_bit && cmp_id_bit) {
            search_init();
            return 0;
        }

        if (!id_bit && !cmp_id_bit) {
            if (i + 1 == s_last_discrepancy) {
                search_dir = 1;
            } else if (i + 1 > s_last_discrepancy) {
                search_dir = 0;
            } else {
                search_dir = (s_rom_buf[rom_byte_num] & rom_byte_mask) ? 1 : 0;
            }
            if (search_dir == 0) last_zero = i + 1;
        } else {
            search_dir = id_bit;
        }

        if (search_dir)
            s_rom_buf[rom_byte_num] |= rom_byte_mask;
        else
            s_rom_buf[rom_byte_num] &= ~rom_byte_mask;

        ow_write_bit(search_dir);

        rom_bit_num++;
        rom_byte_mask <<= 1;
        if (rom_byte_mask == 0) {
            rom_byte_num++;
            rom_byte_mask = 1;
        }
    }

    s_last_discrepancy = last_zero;
    if (s_last_discrepancy == 0) s_last_device_flag = 1;

    if (crc8(s_rom_buf, 8) != 0) {
        search_init();
        return 0;
    }

    memcpy(rom_out, s_rom_buf, 8);
    return 1;
}

// ---- public API ----

esp_err_t ds18b20_init(int gpio_num)
{
    s_gpio = gpio_num;
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

int ds18b20_scan(ds18b20_addr_t *out, int max_count)
{
    search_init();
    int found = 0;
    while (found < max_count) {
        if (!search_next(out[found].rom)) break;
        if (out[found].rom[0] != 0x28) continue; // must be DS18B20 family
        ESP_LOGI(TAG, "found sensor %d: %02X%02X%02X%02X%02X%02X%02X%02X",
                 found,
                 out[found].rom[0], out[found].rom[1], out[found].rom[2], out[found].rom[3],
                 out[found].rom[4], out[found].rom[5], out[found].rom[6], out[found].rom[7]);
        found++;
    }
    return found;
}

esp_err_t ds18b20_convert_all(void)
{
    if (!ow_reset()) return ESP_ERR_NOT_FOUND;
    ow_write_byte(0xCC); // SKIP ROM (broadcast)
    ow_write_byte(0x44); // CONVERT T
    vTaskDelay(pdMS_TO_TICKS(750)); // max conversion time at 12-bit
    return ESP_OK;
}

esp_err_t ds18b20_read_temp(const ds18b20_addr_t *addr, float *temp_c)
{
    if (!ow_reset()) return ESP_ERR_NOT_FOUND;
    ow_write_byte(0x55); // MATCH ROM
    for (int i = 0; i < 8; i++) ow_write_byte(addr->rom[i]);
    ow_write_byte(0xBE); // READ SCRATCHPAD

    uint8_t sp[9];
    for (int i = 0; i < 9; i++) sp[i] = ow_read_byte();

    if (crc8(sp, 9) != 0) {
        ESP_LOGW(TAG, "scratchpad CRC error");
        return ESP_ERR_INVALID_CRC;
    }

    int16_t raw = (int16_t)((sp[1] << 8) | sp[0]);
    *temp_c = (float)raw / 16.0f;
    return ESP_OK;
}
