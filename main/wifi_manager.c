#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "app_state.h"
#include "nvs_config.h"

static const char *TAG = "wifi";

#define AP_SSID      "EHeating-Setup"
#define AP_PASS      ""
#define AP_MAX_CONN  4
#define STA_RETRY_MAX 5

static EventGroupHandle_t s_wifi_eg;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_count = 0;
static esp_netif_t *s_ap_netif  = NULL;
static esp_netif_t *s_sta_netif = NULL;
static bool s_ap_active = false;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_retry_count < STA_RETRY_MAX) {
                esp_wifi_connect();
                s_retry_count++;
                ESP_LOGI(TAG, "retry STA connect (%d/%d)", s_retry_count, STA_RETRY_MAX);
            } else {
                xEventGroupSetBits(s_wifi_eg, WIFI_FAIL_BIT);
                state_lock();
                g_state.wifi_sta_connected = false;
                state_unlock();
            }
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "device connected to AP");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "device left AP");
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
        state_lock();
        g_state.wifi_sta_connected = true;
        state_unlock();
    }
}

void wifi_manager_init(void)
{
    s_wifi_eg = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_ap_netif  = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));
}

void wifi_manager_start_ap(void)
{
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid           = AP_SSID,
            .ssid_len       = strlen(AP_SSID),
            .password       = AP_PASS,
            .max_connection = AP_MAX_CONN,
            .authmode       = WIFI_AUTH_OPEN,
        },
    };

    bool has_sta_creds = (g_cfg.wifi_ssid[0] != '\0');

    ESP_ERROR_CHECK(esp_wifi_set_mode(has_sta_creds ? WIFI_MODE_APSTA : WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));

    if (has_sta_creds) {
        wifi_config_t sta_cfg = {};
        strncpy((char *)sta_cfg.sta.ssid,     g_cfg.wifi_ssid, sizeof(sta_cfg.sta.ssid) - 1);
        strncpy((char *)sta_cfg.sta.password, g_cfg.wifi_pass, sizeof(sta_cfg.sta.password) - 1);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    s_ap_active = true;
    ESP_LOGI(TAG, "AP started: SSID=" AP_SSID);

    if (has_sta_creds) {
        EventBits_t bits = xEventGroupWaitBits(s_wifi_eg,
                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                               pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "STA connected, stopping AP");
            wifi_manager_stop_ap();
        } else {
            ESP_LOGW(TAG, "STA connect failed, keeping AP active");
        }
    }
}

void wifi_manager_stop_ap(void)
{
    if (!s_ap_active) return;
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_ap_active = false;
    ESP_LOGI(TAG, "AP stopped");
}

bool wifi_manager_is_sta_connected(void)
{
    return (xEventGroupGetBits(s_wifi_eg) & WIFI_CONNECTED_BIT) != 0;
}

void wifi_manager_connect_sta(const char *ssid, const char *pass)
{
    strncpy(g_cfg.wifi_ssid, ssid, sizeof(g_cfg.wifi_ssid) - 1);
    strncpy(g_cfg.wifi_pass, pass, sizeof(g_cfg.wifi_pass) - 1);
    config_save();

    esp_wifi_disconnect();
    s_retry_count = 0;
    xEventGroupClearBits(s_wifi_eg, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t sta_cfg = {};
    strncpy((char *)sta_cfg.sta.ssid,     ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password) - 1);

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_eg,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to %s, stopping AP", ssid);
        wifi_manager_stop_ap();
    } else {
        ESP_LOGW(TAG, "failed to connect to %s", ssid);
    }
}
