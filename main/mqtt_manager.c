#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "mqtt_client.h"
#include "mqtt_manager.h"
#include "app_state.h"
#include "nvs_config.h"

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_client = NULL;

static void publish_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  /* publish every 10 s */
        if (!s_client) continue;

        state_lock();
        float t1   = g_state.sensor1_temp; bool s1ok = g_state.sensor1_ok;
        float t2   = g_state.sensor2_temp; bool s2ok = g_state.sensor2_ok;
        float sol  = g_state.solar_avg_10min;
        bool  r1   = g_state.relay1_state;
        bool  r2   = g_state.relay2_state;
        state_unlock();

        char val[32];

        if (g_cfg.pub_en[PUB_SENSOR1] && g_cfg.pub_topic[PUB_SENSOR1][0] && s1ok) {
            snprintf(val, sizeof(val), "%.1f", t1);
            esp_mqtt_client_publish(s_client, g_cfg.pub_topic[PUB_SENSOR1], val, 0, 0, 0);
        }
        if (g_cfg.pub_en[PUB_SENSOR2] && g_cfg.pub_topic[PUB_SENSOR2][0] && s2ok) {
            snprintf(val, sizeof(val), "%.1f", t2);
            esp_mqtt_client_publish(s_client, g_cfg.pub_topic[PUB_SENSOR2], val, 0, 0, 0);
        }
        if (g_cfg.pub_en[PUB_SOLAR] && g_cfg.pub_topic[PUB_SOLAR][0]) {
            snprintf(val, sizeof(val), "%.1f", sol);
            esp_mqtt_client_publish(s_client, g_cfg.pub_topic[PUB_SOLAR], val, 0, 0, 0);
        }
        if (g_cfg.pub_en[PUB_SOLAR_THR] && g_cfg.pub_topic[PUB_SOLAR_THR][0]) {
            snprintf(val, sizeof(val), "%.1f", g_cfg.solar_threshold);
            esp_mqtt_client_publish(s_client, g_cfg.pub_topic[PUB_SOLAR_THR], val, 0, 0, 0);
        }
        if (g_cfg.pub_en[PUB_RELAY1] && g_cfg.pub_topic[PUB_RELAY1][0]) {
            esp_mqtt_client_publish(s_client, g_cfg.pub_topic[PUB_RELAY1], r1 ? "ON" : "OFF", 0, 0, 0);
        }
        if (g_cfg.pub_en[PUB_RELAY2] && g_cfg.pub_topic[PUB_RELAY2][0]) {
            esp_mqtt_client_publish(s_client, g_cfg.pub_topic[PUB_RELAY2], r2 ? "ON" : "OFF", 0, 0, 0);
        }
    }
}

static void push_solar(float watts)
{
    state_lock();
    g_state.solar_power = watts;
    g_state.solar_last_rx_us = esp_timer_get_time();
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

// Publish Home Assistant MQTT discovery configs (retained) for enabled
// publish channels, so sensors/relays auto-appear in HA without manual YAML.
static void publish_ha_discovery(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char node_id[24];
    snprintf(node_id, sizeof(node_id), "eheating_%02x%02x%02x", mac[3], mac[4], mac[5]);

    static const char *obj_id[PUB_COUNT]    = {"sensor1", "sensor2", "solar", "solar_threshold", "relay1", "relay2"};
    static const char *names[PUB_COUNT]     = {"Water Temp", "Safety Temp", "Solar Power", "Solar Threshold", "Relay 1", "Relay 2"};
    static const char *component[PUB_COUNT] = {"sensor", "sensor", "sensor", "sensor", "binary_sensor", "binary_sensor"};
    static const char *unit[PUB_COUNT]      = {"\xC2\xB0""C", "\xC2\xB0""C", "W", "W", "", ""};
    static const char *dev_class[PUB_COUNT] = {"temperature", "temperature", "power", "power", "running", "running"};

    char topic[160];
    char payload[512];

    for (int i = 0; i < PUB_COUNT; i++) {
        if (!g_cfg.pub_en[i] || !g_cfg.pub_topic[i][0]) continue;

        snprintf(topic, sizeof(topic), "homeassistant/%s/%s/%s/config",
                 component[i], node_id, obj_id[i]);

        if (strcmp(component[i], "sensor") == 0) {
            snprintf(payload, sizeof(payload),
                "{\"name\":\"%s\",\"stat_t\":\"%s\",\"unit_of_meas\":\"%s\","
                "\"dev_cla\":\"%s\",\"uniq_id\":\"%s_%s\","
                "\"dev\":{\"ids\":[\"%s\"],\"name\":\"EHeating\",\"mf\":\"DIY\",\"mdl\":\"ESP32-S3\"}}",
                names[i], g_cfg.pub_topic[i], unit[i], dev_class[i], node_id, obj_id[i], node_id);
        } else {
            snprintf(payload, sizeof(payload),
                "{\"name\":\"%s\",\"stat_t\":\"%s\",\"pl_on\":\"ON\",\"pl_off\":\"OFF\","
                "\"uniq_id\":\"%s_%s\","
                "\"dev\":{\"ids\":[\"%s\"],\"name\":\"EHeating\",\"mf\":\"DIY\",\"mdl\":\"ESP32-S3\"}}",
                names[i], g_cfg.pub_topic[i], node_id, obj_id[i], node_id);
        }

        esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 1); // QoS1, retain
    }
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
        publish_ha_discovery();
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
    static bool s_publish_task_started = false;
    if (!s_publish_task_started) {
        s_publish_task_started = true;
        xTaskCreate(publish_task, "mqtt_pub", 2048, NULL, 3, NULL);
    }
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
