#pragma once
#include <stdbool.h>

typedef struct {
    char  wifi_ssid[64];
    char  wifi_pass[64];
    bool  mqtt_enabled;
    char  mqtt_server[128];
    int   mqtt_port;
    char  mqtt_topic[128];
    char  ntp_server[64];
    float solar_threshold;   // watts; relay1 turns on above this
    float temp_min;          // °C; hysteresis low (default 55)
    float temp_max;          // °C; hysteresis high (default 60)
    float temp_safety;       // °C; sensor2 lockout (default 65)
    float temp_safety1;      // °C; sensor1 lockout (default 65)
    bool  relay2_manual;     // manual state for relay2
    int   lcd_brightness;    // 0-100 %
    // Touch calibration: raw ADC values at screen edges
    int   touch_x0;          // raw X when screen_x = 0
    int   touch_x319;        // raw X when screen_x = 319
    int   touch_y0;          // raw Y when screen_y = 0
    int   touch_y239;        // raw Y when screen_y = 239
} config_t;

extern config_t g_cfg;

void config_load_defaults(void);
esp_err_t config_load(void);
esp_err_t config_save(void);
