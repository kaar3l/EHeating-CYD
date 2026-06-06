#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "mqtt_manager.h"
#include "app_state.h"
#include "nvs_config.h"

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_client = NULL;

static void push_solar(float watts)
{
    state_lock();
    g_state.solar_power = watts;
    // push into ring buffer (one sample per MQTT message, capped at ring size)
    uint32_t idx = g_state.solar_ring_idx;
    g_state.solar_ring[idx] = watts;
    g_state.solar_ring_idx  = (idx + 1) % SOLAR_RING_SIZE;
    if (g_state.solar_ring_count < SOLAR_RING_SIZE)
        g_state.solar_ring_count++;

    // recompute average
    float sum = 0;
    for (uint32_t i = 0; i < g_state.solar_ring_count; i++) {
        sum += g_state.solar_ring[i];
    }
    g_state.solar_avg_10min = sum / g_state.solar_ring_count;
    state_unlock();
}

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected");
        state_lock();
        g_state.mqtt_connected = true;
        state_unlock();
        if (g_cfg.mqtt_topic[0])
            esp_mqtt_client_subscribe(s_client, g_cfg.mqtt_topic, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        state_lock();
        g_state.mqtt_connected = false;
        state_unlock();
        break;

    case MQTT_EVENT_DATA:
        if (ev->data_len > 0) {
            char val[32] = {0};
            int n = ev->data_len < (int)sizeof(val) - 1 ? ev->data_len : (int)sizeof(val) - 1;
            memcpy(val, ev->data, n);
            float watts = -strtof(val, NULL); /* negative = producing → negate to positive */
            ESP_LOGI(TAG, "solar power: %.1f W", watts);
            push_solar(watts);
        }
        break;

    default:
        break;
    }
}

void mqtt_manager_start(void)
{
    if (!g_cfg.mqtt_enabled || g_cfg.mqtt_server[0] == '\0') return;

    char uri[160];
    snprintf(uri, sizeof(uri), "mqtt://%s:%d", g_cfg.mqtt_server, g_cfg.mqtt_port);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
    };
    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "MQTT client started -> %s", uri);
}

void mqtt_manager_stop(void)
{
    if (!s_client) return;
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    state_lock();
    g_state.mqtt_connected = false;
    state_unlock();
}

void mqtt_manager_restart(void)
{
    mqtt_manager_stop();
    mqtt_manager_start();
}
