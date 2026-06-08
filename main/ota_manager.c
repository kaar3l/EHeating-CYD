#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "ota_manager.h"

static const char *TAG = "ota";

static const char *OTA_PAGE_HTML =
    "<!DOCTYPE html><html><head><title>EHeating - OTA</title>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<link rel='icon' href='data:image/svg+xml,"
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
    "<text y=\".9em\" font-size=\"90\">\xE2\x98\x80\xEF\xB8\x8F</text></svg>'>"
    "<style>"
    "body{font-family:system-ui,sans-serif;max-width:680px;margin:30px auto;"
         "padding:0 16px;color:#222}"
    "h2{font-size:1.1rem;margin:22px 0 4px;padding-top:16px;"
        "border-top:1px solid #ddd;color:#444}"
    ".nav{margin-bottom:14px;font-size:.88rem}"
    ".nav a{color:#1565c0;margin-right:12px;text-decoration:none}"
    ".nav a:hover{text-decoration:underline}"
    "hr{border:none;border-top:1px solid #ddd;margin:14px 0}"
    "input,select{width:100%;box-sizing:border-box;padding:7px;margin-top:4px;"
        "border:1px solid #ccc;border-radius:6px;font-size:1rem}"
    "button,input[type=submit]{margin-top:20px;width:100%;padding:10px;"
        "background:#1565c0;color:#fff;border:none;border-radius:6px;"
        "font-size:1rem;cursor:pointer}"
    "button:hover,input[type=submit]:hover{background:#0d47a1}"
    "</style></head><body>"
    "<div class='nav'>"
    "<a href='/'>\xF0\x9F\x93\x8A Status</a>"
    "<a href='/wifi'>\xF0\x9F\x93\xB6 WiFi</a>"
    "<a href='/mqtt'>\xF0\x9F\x93\xA1 MQTT</a>"
    "<a href='/settings'>\xE2\x9A\x99\xEF\xB8\x8F Settings</a>"
    "<a href='/ota'>\xF0\x9F\x94\x84 OTA</a>"
    "</div><hr>"
    "<h2>Firmware Update</h2>"
    "<form method='POST' action='/ota/upload' enctype='multipart/form-data'>"
    "<input type='file' name='firmware' accept='.bin'>"
    "<input type='submit' value='Upload &amp; Flash'>"
    "</form></body></html>";

static esp_err_t ota_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, OTA_PAGE_HTML, strlen(OTA_PAGE_HTML));
}

static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    esp_ota_handle_t ota_handle;
    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA partition");
        return ESP_FAIL;
    }

    esp_err_t err = esp_ota_begin(update_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin failed");
        return ESP_FAIL;
    }

    char buf[1024];
    int remaining = req->content_len;
    bool ok = true;

    while (remaining > 0) {
        int recv = httpd_req_recv(req, buf, sizeof(buf) < (size_t)remaining ? sizeof(buf) : (size_t)remaining);
        if (recv <= 0) {
            ESP_LOGE(TAG, "recv error %d", recv);
            ok = false;
            break;
        }
        err = esp_ota_write(ota_handle, buf, recv);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ota_write error: %s", esp_err_to_name(err));
            ok = false;
            break;
        }
        remaining -= recv;
    }

    if (!ok || esp_ota_end(ota_handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_part);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set_boot_partition failed");
        return ESP_FAIL;
    }

    const char *resp = "<html><body><h2>Update OK - rebooting in 3s...</h2></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, strlen(resp));

    ESP_LOGI(TAG, "OTA complete, restarting");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return ESP_OK;
}

void ota_register_handlers(httpd_handle_t server)
{
    httpd_uri_t page = {
        .uri      = "/ota",
        .method   = HTTP_GET,
        .handler  = ota_page_handler,
    };
    httpd_register_uri_handler(server, &page);

    httpd_uri_t upload = {
        .uri      = "/ota/upload",
        .method   = HTTP_POST,
        .handler  = ota_upload_handler,
    };
    httpd_register_uri_handler(server, &upload);
}
