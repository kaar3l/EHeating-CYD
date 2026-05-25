#pragma once
#include "esp_http_server.h"

// Register OTA upload endpoint on an existing httpd server
void ota_register_handlers(httpd_handle_t server);
