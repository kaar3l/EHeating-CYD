#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "mdns.h"

#include "pin_config.h"
#include "app_state.h"
#include "nvs_config.h"
#include "wifi_manager.h"
#include "dns_server.h"
#include "web_server.h"
#include "mqtt_manager.h"
#include "ds18b20.h"
#include "relay_control.h"
#include "display.h"
#include "touch.h"
#include "pin_config.h"

static const char *TAG = "main";

// Global state instance
app_state_t g_state = {0};

// DS18B20 addresses discovered at boot
static ds18b20_addr_t s_sensors[2];
static int            s_sensor_count = 0;

static void update_sensor_addrs(void)
{
    for (int i = 0; i < 2; i++) {
        char buf[20];
        if (i < s_sensor_count)
            snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X%02X%02X",
                s_sensors[i].rom[0], s_sensors[i].rom[1],
                s_sensors[i].rom[2], s_sensors[i].rom[3],
                s_sensors[i].rom[4], s_sensors[i].rom[5],
                s_sensors[i].rom[6], s_sensors[i].rom[7]);
        else
            strcpy(buf, "N/A");
        memcpy(i == 0 ? g_state.sensor1_addr : g_state.sensor2_addr, buf, sizeof(buf));
    }
}

// ---- Sensor task ----
// Reads both DS18B20s every 2 seconds.
// Retries each read up to 3 times on CRC/bus errors before marking sensor bad.
// Triggers safety lockout if sensor1 or sensor2 exceeds its threshold.
static void sensor_task(void *arg)
{
    esp_err_t err;
    int t2_fail_streak = 0;   // consecutive T2 read failures
    int rescan_ticks   = 0;   // counts 2-s cycles toward next forced rescan

    while (1) {
        /* Re-scan bus if T2 is missing or has been failing for 10 cycles (20 s) */
        bool need_rescan = (s_sensor_count < 2) || (t2_fail_streak >= 10);
        if (need_rescan) {
            rescan_ticks++;
            if (rescan_ticks >= 10) {   // attempt every 10 × 2 s = 20 s
                rescan_ticks = 0;
                int found = ds18b20_scan(s_sensors, 2);
                if (found != s_sensor_count) {
                    ESP_LOGI(TAG, "rescan: %d → %d sensor(s)", s_sensor_count, found);
                    s_sensor_count = found;
                }
                update_sensor_addrs();
                t2_fail_streak = 0;
            }
        }

        float t1 = 0, t2 = 0;
        bool ok1 = false, ok2 = false;

        err = ds18b20_convert_all();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "1-wire convert failed, retrying");
            vTaskDelay(pdMS_TO_TICKS(500));
            err = ds18b20_convert_all();
        }

        if (err == ESP_OK) {
            if (s_sensor_count >= 1) {
                for (int r = 0; r < 3 && !ok1; r++) {
                    if (r) vTaskDelay(pdMS_TO_TICKS(50));
                    ok1 = (ds18b20_read_temp(&s_sensors[0], &t1) == ESP_OK);
                    if (!ok1) ESP_LOGW(TAG, "T1 read attempt %d failed", r + 1);
                }
            }
            if (s_sensor_count >= 2) {
                for (int r = 0; r < 3 && !ok2; r++) {
                    if (r) vTaskDelay(pdMS_TO_TICKS(50));
                    ok2 = (ds18b20_read_temp(&s_sensors[1], &t2) == ESP_OK);
                    if (!ok2) ESP_LOGW(TAG, "T2 read attempt %d failed", r + 1);
                }
            }
        } else {
            ESP_LOGE(TAG, "1-wire convert failed after retry");
        }

        t2_fail_streak = ok2 ? 0 : (t2_fail_streak + 1);

        state_lock();
        if (ok1) { g_state.sensor1_temp = t1; g_state.sensor1_ok = true; }
        else      g_state.sensor1_ok = false;

        if (ok2) { g_state.sensor2_temp = t2; g_state.sensor2_ok = true; }
        else      g_state.sensor2_ok = false;

        // safety lockout — skip first 5 reads (10 s) to ignore DS18B20 power-on 85°C value
        static int s_startup_reads = 0;
        if (s_startup_reads < 5) {
            s_startup_reads++;
        } else if (!g_state.error_lockout) {
            if (ok1 && t1 >= g_cfg.temp_safety1) {
                ESP_LOGE(TAG, "SAFETY LOCKOUT: sensor1 temp %.1f >= %.1f", t1, g_cfg.temp_safety1);
                g_state.error_lockout  = true;
                g_state.lockout_sensor = 1;
            } else if (ok2 && t2 >= g_cfg.temp_safety) {
                ESP_LOGE(TAG, "SAFETY LOCKOUT: sensor2 temp %.1f >= %.1f", t2, g_cfg.temp_safety);
                g_state.error_lockout  = true;
                g_state.lockout_sensor = 2;
            }
        }
        state_unlock();

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ---- Relay task ----
// Evaluates relay logic every 5 seconds.
static void relay_task(void *arg)
{
    while (1) {
        relay_evaluate();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// ---- Solar ring fill task ----
// Pushes current solar power into the ring buffer every second so the
// 10-minute average always reflects time, not message rate.
static void solar_ring_task(void *arg)
{
    while (1) {
        state_lock();
        float current_power = g_state.solar_power;
        uint32_t idx = g_state.solar_ring_idx;
        g_state.solar_ring[idx] = current_power;
        g_state.solar_ring_idx  = (idx + 1) % SOLAR_RING_SIZE;
        if (g_state.solar_ring_count < SOLAR_RING_SIZE)
            g_state.solar_ring_count++;

        float sum = 0;
        for (uint32_t i = 0; i < g_state.solar_ring_count; i++)
            sum += g_state.solar_ring[i];
        g_state.solar_avg_10min = sum / g_state.solar_ring_count;
        state_unlock();

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ---- Display task ----
static void display_task(void *arg)
{
    while (1) {
        display_update_status();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ---- Touch task ----
// Status screen : any tap → settings.
// Settings screen: left half (x<160, y<178) -10%, right half +10%, y>178 back.
//                  Auto-returns to status after 30 s of no touch.
static void touch_task(void *arg)
{
    bool prev_touched = false;
    int64_t settings_last_touch_us = 0;

    while (1) {
        int tx = 0, ty = 0;
        screen_mode_t scr = display_get_screen();

        // 30-second timeout on settings screen
        if (scr == SCREEN_SETTINGS) {
            int64_t now = esp_timer_get_time();
            if (settings_last_touch_us > 0 &&
                (now - settings_last_touch_us) > 30LL * 1000000LL) {
                display_set_screen(SCREEN_STATUS);
                settings_last_touch_us = 0;
                prev_touched = false;
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
        }

        bool touched = touch_read(&tx, &ty);

        if (touched && !prev_touched) {
            scr = display_get_screen();

            if (scr == SCREEN_STATUS) {
                settings_last_touch_us = esp_timer_get_time();
                display_set_screen(SCREEN_SETTINGS);

            } else if (scr == SCREEN_SETTINGS) {
                settings_last_touch_us = esp_timer_get_time();

                if (ty > 178) {
                    display_set_screen(SCREEN_STATUS);
                    settings_last_touch_us = 0;
                } else if (ty < 130) {
                    // brightness ±10% buttons
                    int pct = g_cfg.lcd_brightness;
                    pct += (tx < LCD_WIDTH / 2) ? -10 : +10;
                    if (pct < 0)   pct = 0;
                    if (pct > 100) pct = 100;
                    g_cfg.lcd_brightness = pct;
                    display_set_brightness(pct);
                    config_save();
                    display_show_settings();
                }
                // 130-178: info zone, no action
            }
        }

        prev_touched = touched;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ---- app_main ----

void app_main(void)
{
    // NVS init
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // State init
    g_state.mutex = xSemaphoreCreateMutex();
    g_state.temp_ok = true;

    // Load config
    config_load();
    setenv("TZ", g_cfg.tz, 1);
    tzset();
    display_set_brightness(g_cfg.lcd_brightness);
    touch_apply_cal(g_cfg.touch_x0, g_cfg.touch_x319,
                    g_cfg.touch_y0, g_cfg.touch_y239);

    // Display init
    display_init();
    display_boot_screen();
    vTaskDelay(pdMS_TO_TICKS(2000));

    // DS18B20 init and scan
    if (ds18b20_init(ONE_WIRE_BUS) == ESP_OK) {
        s_sensor_count = ds18b20_scan(s_sensors, 2);
        ESP_LOGI(TAG, "found %d DS18B20 sensor(s)", s_sensor_count);
        if (s_sensor_count < 2)
            ESP_LOGW(TAG, "expected 2 sensors, found %d", s_sensor_count);
    } else {
        ESP_LOGE(TAG, "DS18B20 init failed");
    }
    update_sensor_addrs();

    // Touch init
    touch_init();

    // Relay init
    relay_init();

    // WiFi init
    wifi_manager_init();

    // mDNS hostname: eheating-XXYYZZ (last 3 MAC bytes)
    {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char hostname[24];
        snprintf(hostname, sizeof(hostname), "eheating-%02x%02x%02x", mac[3], mac[4], mac[5]);
        ESP_ERROR_CHECK(mdns_init());
        ESP_ERROR_CHECK(mdns_hostname_set(hostname));
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        ESP_LOGI(TAG, "mDNS hostname: %s.local", hostname);
    }

    bool first_boot = (g_cfg.wifi_ssid[0] == '\0');
    if (first_boot) {
        ESP_LOGI(TAG, "first boot - starting AP only");
        display_status_msg("WiFi Setup", "Connect: EHeating-Setup", COLOR_CYAN);
        dns_server_start();
        wifi_manager_start_ap();
    } else {
        display_status_msg("WiFi", "Connecting...", COLOR_ORANGE);
        dns_server_start();
        wifi_manager_start_ap();
        if (wifi_manager_is_sta_connected()) {
            char ip[20];
            wifi_manager_get_ip(ip, sizeof(ip));
            display_status_msg("WiFi Connected", ip, COLOR_ORANGE);
            vTaskDelay(pdMS_TO_TICKS(2000));
        } else {
            display_status_msg("WiFi Failed", "Check settings", COLOR_RED);
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
    }

    // Web server (always on)
    web_server_start();

    // MQTT (if configured and STA connected)
    if (g_cfg.mqtt_enabled && wifi_manager_is_sta_connected()) {
        mqtt_manager_start();
    }

    // Start tasks
    xTaskCreate(sensor_task,    "sensors",    4096, NULL, 6, NULL);
    xTaskCreate(relay_task,     "relay",      2048, NULL, 5, NULL);
    xTaskCreate(solar_ring_task,"solar_ring", 2048, NULL, 4, NULL);
    xTaskCreate(display_task,   "display",    4096, NULL, 3, NULL);
    xTaskCreate(touch_task,     "touch",      4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "EHeating started");

    // Monitor: when STA connects (after boot), start MQTT and stop DNS/AP
    while (1) {
        bool connected = wifi_manager_is_sta_connected();
        state_lock();
        g_state.wifi_sta_connected = connected;
        state_unlock();

        if (connected && g_cfg.mqtt_enabled) {
            static bool mqtt_started = false;
            if (!mqtt_started) {
                mqtt_started = true;
                dns_server_stop();
                mqtt_manager_start();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
