#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "ota_manager.h"

static const char *TAG = "ota";

static const char *OTA_PAGE_HTML =
    "<!DOCTYPE html><html><head><title>OTA Update</title></head><body>"
    "<h2>Firmware Update</h2>"
    "<form method='POST' action='/ota/upload' enctype='multipart/form-data'>"
    "<input type='file' name='firmware' accept='.bin'><br><br>"
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
