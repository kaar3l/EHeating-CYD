#include "driver/gpio.h"
#include "esp_log.h"
#include "relay_control.h"
#include "app_state.h"
#include "nvs_config.h"
#include "pin_config.h"

static const char *TAG = "relay";

// Relays are active-LOW (inverted)
static void set_gpio(int pin, bool on)
{
    gpio_set_level(pin, on ? 0 : 1);
}

void relay_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << RELAY1_PIN) | (1ULL << RELAY2_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    // start with relays OFF
    gpio_set_level(RELAY1_PIN, 1);
    gpio_set_level(RELAY2_PIN, 1);
}

void relay_set1(bool on)
{
    set_gpio(RELAY1_PIN, on);
    state_lock();
    g_state.relay1_state = on;
    state_unlock();
    ESP_LOGI(TAG, "relay1 -> %s", on ? "ON" : "OFF");
}

void relay_set2(bool on)
{
    set_gpio(RELAY2_PIN, on);
    state_lock();
    g_state.relay2_state = on;
    state_unlock();
    ESP_LOGI(TAG, "relay2 -> %s", on ? "ON" : "OFF");
}

void relay_evaluate(void)
{
    state_lock();
    bool lockout   = g_state.error_lockout;
    float t1       = g_state.sensor1_temp;
    bool  s1_ok    = g_state.sensor1_ok;
    float solar    = g_state.solar_avg_10min;
    bool  temp_ok  = g_state.temp_ok;
    state_unlock();

    if (lockout) {
        relay_set1(false);
        relay_set2(false);
        return;
    }

    // temperature hysteresis for relay1
    if (s1_ok) {
        if (t1 >= g_cfg.temp_max) {
            temp_ok = false;
        } else if (t1 < g_cfg.temp_min) {
            temp_ok = true;
        }
        // else: stay in current temp_ok state (hysteresis zone)
        state_lock();
        g_state.temp_ok = temp_ok;
        state_unlock();
    } else {
        // sensor fault: cannot heat safely
        temp_ok = false;
    }

    // relay1: solar-powered heating
    bool want_relay1 = false;
    if (g_cfg.mqtt_enabled) {
        want_relay1 = (solar > g_cfg.solar_threshold) && temp_ok;
    }

    relay_set1(want_relay1);

    // relay2: manual control
    relay_set2(g_cfg.relay2_manual);
}
