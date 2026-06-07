#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define SOLAR_RING_SIZE     600   // 10 minutes at 1 sample/sec

typedef struct {
    float sensor1_temp;
    float sensor2_temp;
    float solar_power;
    float solar_avg_10min;
    bool  relay1_state;
    bool  relay2_state;
    bool  error_lockout;
    bool  mqtt_connected;
    bool  wifi_sta_connected;
    bool  sensor1_ok;
    bool  sensor2_ok;
    char  sensor1_addr[20];   // hex ROM "28FF001122334455", "N/A" if not found
    char  sensor2_addr[20];

    // solar ring buffer for 10-min average
    float solar_ring[SOLAR_RING_SIZE];
    uint32_t solar_ring_idx;
    uint32_t solar_ring_count;

    // hysteresis state for temp control
    bool temp_ok;   // false = too hot, must cool below temp_min before relay1 can use solar

    SemaphoreHandle_t mutex;
} app_state_t;

extern app_state_t g_state;

static inline void state_lock(void)   { xSemaphoreTake(g_state.mutex, portMAX_DELAY); }
static inline void state_unlock(void) { xSemaphoreGive(g_state.mutex); }
