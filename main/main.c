#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"

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

static const char *TAG = "main";

// Global state instance
app_state_t g_state = {0};

// DS18B20 addresses discovered at boot
static ds18b20_addr_t s_sensors[2];
static int            s_sensor_count = 0;

// ---- Sensor task ----
// Reads both DS18B20s every 2 seconds.
// Triggers safety lockout if sensor2 exceeds threshold.
static void sensor_task(void *arg)
{
    esp_err_t err;
    while (1) {
        err = ds18b20_convert_all();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "1-wire convert failed");
            state_lock();
            g_state.sensor1_ok = false;
            g_state.sensor2_ok = false;
            state_unlock();
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        float t1 = 0, t2 = 0;
        bool ok1 = false, ok2 = false;

        if (s_sensor_count >= 1) {
            err = ds18b20_read_temp(&s_sensors[0], &t1);
            ok1 = (err == ESP_OK);
        }
        if (s_sensor_count >= 2) {
            err = ds18b20_read_temp(&s_sensors[1], &t2);
            ok2 = (err == ESP_OK);
        }

        state_lock();
        if (ok1) { g_state.sensor1_temp = t1; g_state.sensor1_ok = true; }
        else        g_state.sensor1_ok = false;

        if (ok2) { g_state.sensor2_temp = t2; g_state.sensor2_ok = true; }
        else        g_state.sensor2_ok = false;

        // safety lockout
        if (ok2 && t2 >= g_cfg.temp_safety && !g_state.error_lockout) {
            ESP_LOGE(TAG, "SAFETY LOCKOUT: sensor2 temp %.1f >= %.1f", t2, g_cfg.temp_safety);
            g_state.error_lockout = true;
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

    // Display init
    display_init();
    display_status_msg("EHeating", "Booting...");

    // DS18B20 init and scan
    if (ds18b20_init(ONE_WIRE_BUS) == ESP_OK) {
        s_sensor_count = ds18b20_scan(s_sensors, 2);
        ESP_LOGI(TAG, "found %d DS18B20 sensor(s)", s_sensor_count);
        if (s_sensor_count < 2) {
            ESP_LOGW(TAG, "expected 2 sensors, found %d", s_sensor_count);
        }
    } else {
        ESP_LOGE(TAG, "DS18B20 init failed");
    }

    // Relay init
    relay_init();

    // WiFi init
    wifi_manager_init();

    bool first_boot = (g_cfg.wifi_ssid[0] == '\0');
    if (first_boot) {
        ESP_LOGI(TAG, "first boot - starting AP only");
        display_status_msg("WiFi Setup", "Connect: EHeating-Setup");
        dns_server_start();
        wifi_manager_start_ap();
    } else {
        display_status_msg("WiFi", "Connecting...");
        dns_server_start();
        wifi_manager_start_ap();
        if (wifi_manager_is_sta_connected()) {
            char ip[20];
            wifi_manager_get_ip(ip, sizeof(ip));
            display_status_msg("WiFi Connected", ip);
            vTaskDelay(pdMS_TO_TICKS(2000));
        } else {
            display_status_msg("WiFi Failed", "Check settings");
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
