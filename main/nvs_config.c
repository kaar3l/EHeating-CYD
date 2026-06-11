#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "nvs_config.h"

static const char *TAG = "nvs_cfg";
#define NVS_NS "eheating"

config_t g_cfg;

void config_load_defaults(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.mqtt_port       = 1883;
    g_cfg.solar_threshold = 500.0f;
    g_cfg.temp_min        = 55.0f;
    g_cfg.temp_max        = 60.0f;
    g_cfg.temp_safety     = 65.0f;
    g_cfg.temp_safety1    = 65.0f;
    g_cfg.mqtt_enabled    = false;
    g_cfg.relay2_manual   = false;
    g_cfg.lcd_brightness  = 100;
    // Measured calibration: 0x90=physical X (left=3742,right=260), 0xD0=physical Y (top=3667,bot=367)
    g_cfg.touch_x0   = 3742;
    g_cfg.touch_x319 = 260;
    g_cfg.touch_y0   = 3667;
    g_cfg.touch_y239 = 367;
    strncpy(g_cfg.mqtt_topic, "solar/power", sizeof(g_cfg.mqtt_topic) - 1);
    strncpy(g_cfg.ntp_server, "pool.ntp.org", sizeof(g_cfg.ntp_server) - 1);
    strncpy(g_cfg.tz, "EET-2EEST,M3.5.0/3,M10.5.0/4", sizeof(g_cfg.tz) - 1);  // Estonia, w/ DST
    static const char *pub_defaults[PUB_COUNT] = {
        "eheating/sensor1", "eheating/sensor2",
        "eheating/solar_power", "eheating/solar_threshold",
        "eheating/relay1", "eheating/relay2",
    };
    for (int i = 0; i < PUB_COUNT; i++) {
        g_cfg.pub_en[i] = false;
        strncpy(g_cfg.pub_topic[i], pub_defaults[i], sizeof(g_cfg.pub_topic[i]) - 1);
    }
}

esp_err_t config_load(void)
{
    config_load_defaults();

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no config in NVS, using defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

#define LOAD_STR(key, dst) do { \
    size_t len = sizeof(dst); \
    nvs_get_str(h, key, dst, &len); \
} while(0)

#define LOAD_I32(key, dst) do { \
    int32_t v; \
    if (nvs_get_i32(h, key, &v) == ESP_OK) dst = (int)v; \
} while(0)

#define LOAD_FLT(key, dst) do { \
    uint32_t raw; \
    if (nvs_get_u32(h, key, &raw) == ESP_OK) memcpy(&dst, &raw, 4); \
} while(0)

#define LOAD_U8(key, dst) do { \
    uint8_t v; \
    if (nvs_get_u8(h, key, &v) == ESP_OK) dst = (bool)v; \
} while(0)

    LOAD_STR("wifi_ssid",  g_cfg.wifi_ssid);
    LOAD_STR("wifi_pass",  g_cfg.wifi_pass);
    LOAD_U8 ("mqtt_en",    g_cfg.mqtt_enabled);
    LOAD_STR("mqtt_srv",   g_cfg.mqtt_server);
    LOAD_I32("mqtt_port",  g_cfg.mqtt_port);
    LOAD_STR("mqtt_topic", g_cfg.mqtt_topic);
    LOAD_STR("ntp_srv",    g_cfg.ntp_server);
    LOAD_STR("tz",         g_cfg.tz);
    LOAD_FLT("solar_thr",  g_cfg.solar_threshold);
    LOAD_FLT("temp_min",   g_cfg.temp_min);
    LOAD_FLT("temp_max",   g_cfg.temp_max);
    LOAD_FLT("temp_safe",  g_cfg.temp_safety);
    LOAD_FLT("temp_safe1", g_cfg.temp_safety1);
    LOAD_U8 ("rly2_man",   g_cfg.relay2_manual);
    { uint8_t v;  if (nvs_get_u8 (h, "lcd_bright",  &v) == ESP_OK) g_cfg.lcd_brightness = (int)v; }
    LOAD_I32("lo_sensor",  g_cfg.last_lockout_sensor);
    LOAD_STR("lo_time",    g_cfg.last_lockout_time);
    LOAD_I32("tc2_x0",   g_cfg.touch_x0);
    LOAD_I32("tc2_x319", g_cfg.touch_x319);
    LOAD_I32("tc2_y0",   g_cfg.touch_y0);
    LOAD_I32("tc2_y239", g_cfg.touch_y239);
    { char key[16];
      for (int i = 0; i < PUB_COUNT; i++) {
        snprintf(key, sizeof(key), "pub_en%d", i);
        LOAD_U8(key, g_cfg.pub_en[i]);
        snprintf(key, sizeof(key), "pub_tp%d", i);
        LOAD_STR(key, g_cfg.pub_topic[i]);
      }
    }

    nvs_close(h);
    ESP_LOGI(TAG, "config loaded");
    return ESP_OK;
}

esp_err_t config_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

#define SAVE_STR(key, src) nvs_set_str(h, key, src)
#define SAVE_I32(key, val) nvs_set_i32(h, key, (int32_t)(val))
#define SAVE_FLT(key, val) do { uint32_t raw; float _v=(val); memcpy(&raw,&_v,4); nvs_set_u32(h,key,raw); } while(0)
#define SAVE_U8(key, val)  nvs_set_u8(h, key, (uint8_t)(val))

    SAVE_STR("wifi_ssid",  g_cfg.wifi_ssid);
    SAVE_STR("wifi_pass",  g_cfg.wifi_pass);
    SAVE_U8 ("mqtt_en",    g_cfg.mqtt_enabled);
    SAVE_STR("mqtt_srv",   g_cfg.mqtt_server);
    SAVE_I32("mqtt_port",  g_cfg.mqtt_port);
    SAVE_STR("mqtt_topic", g_cfg.mqtt_topic);
    SAVE_STR("ntp_srv",    g_cfg.ntp_server);
    SAVE_STR("tz",         g_cfg.tz);
    SAVE_FLT("solar_thr",  g_cfg.solar_threshold);
    SAVE_FLT("temp_min",   g_cfg.temp_min);
    SAVE_FLT("temp_max",   g_cfg.temp_max);
    SAVE_FLT("temp_safe",  g_cfg.temp_safety);
    SAVE_FLT("temp_safe1", g_cfg.temp_safety1);
    SAVE_U8 ("rly2_man",   g_cfg.relay2_manual);
    nvs_set_u8(h, "lcd_bright", (uint8_t)g_cfg.lcd_brightness);
    SAVE_I32("lo_sensor",  g_cfg.last_lockout_sensor);
    SAVE_STR("lo_time",    g_cfg.last_lockout_time);
    SAVE_I32("tc2_x0",   g_cfg.touch_x0);
    SAVE_I32("tc2_x319", g_cfg.touch_x319);
    SAVE_I32("tc2_y0",   g_cfg.touch_y0);
    SAVE_I32("tc2_y239", g_cfg.touch_y239);
    { char key[16];
      for (int i = 0; i < PUB_COUNT; i++) {
        snprintf(key, sizeof(key), "pub_en%d", i);
        SAVE_U8(key, g_cfg.pub_en[i]);
        snprintf(key, sizeof(key), "pub_tp%d", i);
        SAVE_STR(key, g_cfg.pub_topic[i]);
      }
    }

    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "config saved");
    return err;
}
