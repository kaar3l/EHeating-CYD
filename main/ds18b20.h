#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define DS18B20_MAX_SENSORS 4

typedef struct {
    uint8_t rom[8];
} ds18b20_addr_t;

esp_err_t ds18b20_init(int gpio_num);
int       ds18b20_scan(ds18b20_addr_t *out, int max_count);
esp_err_t ds18b20_convert_all(void);
esp_err_t ds18b20_read_temp(const ds18b20_addr_t *addr, float *temp_c);
