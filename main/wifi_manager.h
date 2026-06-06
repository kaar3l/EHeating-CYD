#pragma once
#include <stdbool.h>
#include <stddef.h>

void wifi_manager_init(void);
bool wifi_manager_is_sta_connected(void);
void wifi_manager_start_ap(void);
void wifi_manager_stop_ap(void);
void wifi_manager_connect_sta(const char *ssid, const char *pass);
void wifi_manager_get_ip(char *buf, size_t len);
int  wifi_manager_get_rssi(void);
