#pragma once
#include <stdbool.h>

/* Indices for per-channel publish configuration */
enum {
    PUB_SENSOR1    = 0,
    PUB_SENSOR2    = 1,
    PUB_SOLAR      = 2,
    PUB_SOLAR_THR  = 3,
    PUB_RELAY1     = 4,
    PUB_RELAY2     = 5,
    PUB_COUNT      = 6
};

typedef struct {
    char  wifi_ssid[64];
    char  wifi_pass[64];
    bool  mqtt_enabled;
    char  mqtt_server[128];
    int   mqtt_port;
    char  mqtt_topic[128];
    char  ntp_server[64];
    char  tz[64];            // POSIX TZ string (local time + DST rule)
    float solar_threshold;   // watts; relay1 turns on above this
    float temp_min;          // °C; hysteresis low (default 55)
    float temp_max;          // °C; hysteresis high (default 60)
    float temp_safety;       // °C; sensor2 lockout (default 65)
    float temp_safety1;      // °C; sensor1 lockout (default 65)
    bool  relay2_manual;     // manual state for relay2
    int   lcd_brightness;    // 0-100 %
    // Last safety lockout, persisted across reboot for diagnostics
    int   last_lockout_sensor;   // 1 or 2, 0 = never
    char  last_lockout_time[32]; // human-readable timestamp
    // Per-channel MQTT publish config
    bool  pub_en[PUB_COUNT];
    char  pub_topic[PUB_COUNT][64];
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
