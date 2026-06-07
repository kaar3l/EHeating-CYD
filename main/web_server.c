#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "web_server.h"
#include "ota_manager.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "app_state.h"
#include "nvs_config.h"

static const char *TAG = "web";
static httpd_handle_t s_server = NULL;

// ---- HTML helpers ----

static const char *HTML_HEAD =
    "<!DOCTYPE html><html><head><title>EHeating</title>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{font-family:sans-serif;margin:20px;max-width:500px}"
    "h2{color:#336}"
    "input,select{width:100%;padding:6px;margin:4px 0 12px;box-sizing:border-box}"
    "button,input[type=submit]{background:#336;color:#fff;border:none;padding:10px;cursor:pointer;width:auto}"
    ".nav a{margin-right:12px;color:#336}"
    ".ok{color:green} .err{color:red}"
    "</style></head><body>"
    "<div class='nav'>"
    "<a href='/'>Status</a>"
    "<a href='/wifi'>WiFi</a>"
    "<a href='/mqtt'>MQTT</a>"
    "<a href='/settings'>Settings</a>"
    "<a href='/ota'>OTA</a>"
    "</div><hr>";

static const char *HTML_FOOT = "</body></html>";

static esp_err_t send_html(httpd_req_t *req, const char *body)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, HTML_HEAD);
    httpd_resp_sendstr_chunk(req, body);
    httpd_resp_sendstr_chunk(req, HTML_FOOT);
    return httpd_resp_sendstr_chunk(req, NULL);
}

// ---- URL-decode helper ----

static void url_decode(char *dst, const char *src, size_t maxlen)
{
    size_t i = 0;
    while (*src && i < maxlen - 1) {
        if (*src == '%' && src[1] && src[2]) {
            char h[3] = {src[1], src[2], 0};
            dst[i++] = (char)strtol(h, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

static void get_field(const char *body, const char *key, char *dst, size_t maxlen)
{
    char search[64];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) { dst[0] = '\0'; return; }
    p += strlen(search);
    const char *end = strchr(p, '&');
    char raw[256] = {0};
    if (end) {
        size_t len = end - p < (int)sizeof(raw) - 1 ? (size_t)(end - p) : sizeof(raw) - 1;
        memcpy(raw, p, len);
    } else {
        strncpy(raw, p, sizeof(raw) - 1);
    }
    url_decode(dst, raw, maxlen);
}

static int recv_body(httpd_req_t *req, char *buf, size_t maxlen)
{
    int total = 0;
    int remaining = req->content_len < (int)maxlen - 1 ? req->content_len : (int)maxlen - 1;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf + total, remaining);
        if (r <= 0) break;
        total += r;
        remaining -= r;
    }
    buf[total] = '\0';
    return total;
}

// ---- Status page ----

static esp_err_t status_handler(httpd_req_t *req)
{
    state_lock();
    float t1   = g_state.sensor1_temp;
    float t2   = g_state.sensor2_temp;
    float sol  = g_state.solar_avg_10min;
    bool  r1   = g_state.relay1_state;
    bool  r2   = g_state.relay2_state;
    bool  lock = g_state.error_lockout;
    bool  s1ok = g_state.sensor1_ok;
    bool  s2ok = g_state.sensor2_ok;
    bool  mqtt = g_state.mqtt_connected;
    bool  wifi = g_state.wifi_sta_connected;
    char  s1addr[20], s2addr[20];
    strncpy(s1addr, g_state.sensor1_addr, sizeof(s1addr));
    strncpy(s2addr, g_state.sensor2_addr, sizeof(s2addr));
    state_unlock();

    char ip[20];
    wifi_manager_get_ip(ip, sizeof(ip));

    char t1_str[16], t2_str[16];
    if (s1ok) snprintf(t1_str, sizeof(t1_str), "%.1f C", t1); else strncpy(t1_str, "ERROR", sizeof(t1_str));
    if (s2ok) snprintf(t2_str, sizeof(t2_str), "%.1f C", t2); else strncpy(t2_str, "ERROR", sizeof(t2_str));

    char body[1800];
    snprintf(body, sizeof(body),
        "<h2>EHeating Status</h2>"
        "<p>WiFi: <b class='%s'>%s</b> (%s) &nbsp; MQTT: <b class='%s'>%s</b></p>"
        "%s"
        "<table>"
        "<tr><td>Sensor1 (water):</td><td><b>%s</b></td></tr>"
        "<tr><td>Sensor2 (safety):</td><td><b>%s</b></td></tr>"
        "<tr><td>Solar power (10min avg):</td><td><b>%.0f W</b></td></tr>"
        "<tr><td>Solar threshold:</td><td><b>%.0f W</b></td></tr>"
        "<tr><td>Relay1 (solar heat):</td><td><b class='%s'>%s</b></td></tr>"
        "<tr><td>Relay2 (manual):</td><td><b class='%s'>%s</b></td></tr>"
        "<tr><td colspan=2><hr></td></tr>"
        "<tr><td>T1 address:</td><td><code>%s</code></td></tr>"
        "<tr><td>T2 address:</td><td><code>%s</code></td></tr>"
        "<tr><td>MQTT server:</td><td><code>%s</code></td></tr>"
        "<tr><td>MQTT topic:</td><td><code>%s</code></td></tr>"
        "</table>"
        "<br><form method='GET' action='/relay2'>"
        "<input type='hidden' name='state' value='%s'>"
        "<input type='submit' value='Toggle Relay2'></form>"
        "<meta http-equiv='refresh' content='5'>",
        wifi ? "ok" : "err", wifi ? "Connected" : "Disconnected", ip,
        mqtt ? "ok" : "err", mqtt ? "Connected" : "Disconnected",
        lock ? "<p class='err'><b>!! SAFETY LOCKOUT - Sensor2 overheated !!</b></p>" : "",
        t1_str, t2_str, sol, g_cfg.solar_threshold,
        r1 ? "ok" : "err", r1 ? "ON" : "OFF",
        r2 ? "ok" : "err", r2 ? "ON" : "OFF",
        s1addr, s2addr,
        g_cfg.mqtt_server[0] ? g_cfg.mqtt_server : "not set",
        g_cfg.mqtt_topic[0]  ? g_cfg.mqtt_topic  : "not set",
        r2 ? "off" : "on"
    );
    return send_html(req, body);
}

// ---- WiFi config page ----

static esp_err_t wifi_page_handler(httpd_req_t *req)
{
    char body[512];
    snprintf(body, sizeof(body),
        "<h2>WiFi Setup</h2>"
        "<form method='POST' action='/wifi'>"
        "<label>SSID<input name='ssid' value='%s'></label>"
        "<label>Password<input type='password' name='pass' value='%s'></label>"
        "<input type='submit' value='Save &amp; Connect'>"
        "</form>",
        g_cfg.wifi_ssid, g_cfg.wifi_pass);
    return send_html(req, body);
}

static esp_err_t wifi_save_handler(httpd_req_t *req)
{
    char body[256];
    recv_body(req, body, sizeof(body));

    char ssid[64] = {0}, pass[64] = {0};
    get_field(body, "ssid", ssid, sizeof(ssid));
    get_field(body, "pass", pass, sizeof(pass));

    if (ssid[0] == '\0') {
        return send_html(req, "<p class='err'>SSID cannot be empty.</p>");
    }

    wifi_manager_connect_sta(ssid, pass);

    return send_html(req, "<p class='ok'>Saved. Connecting...</p>"
                          "<meta http-equiv='refresh' content='5;url=/'>");
}

// ---- MQTT config page ----

static esp_err_t mqtt_page_handler(httpd_req_t *req)
{
    char body[768];
    snprintf(body, sizeof(body),
        "<h2>MQTT Configuration</h2>"
        "<form method='POST' action='/mqtt'>"
        "<label>MQTT Enabled"
        "<select name='en'>"
        "<option value='1'%s>Yes</option>"
        "<option value='0'%s>No</option>"
        "</select></label>"
        "<label>Server<input name='srv' value='%s'></label>"
        "<label>Port<input name='port' value='%d' type='number'></label>"
        "<label>Subscribe Topic (solar power W)<input name='topic' value='%s'></label>"
        "<input type='submit' value='Save'>"
        "</form>",
        g_cfg.mqtt_enabled ? " selected" : "",
        g_cfg.mqtt_enabled ? "" : " selected",
        g_cfg.mqtt_server, g_cfg.mqtt_port, g_cfg.mqtt_topic);
    return send_html(req, body);
}

static esp_err_t mqtt_save_handler(httpd_req_t *req)
{
    char body[512];
    recv_body(req, body, sizeof(body));

    char en[4], srv[128], port_s[8], topic[128];
    get_field(body, "en",    en,    sizeof(en));
    get_field(body, "srv",   srv,   sizeof(srv));
    get_field(body, "port",  port_s,sizeof(port_s));
    get_field(body, "topic", topic, sizeof(topic));

    g_cfg.mqtt_enabled = (en[0] == '1');
    strncpy(g_cfg.mqtt_server, srv,   sizeof(g_cfg.mqtt_server) - 1);
    strncpy(g_cfg.mqtt_topic,  topic, sizeof(g_cfg.mqtt_topic)  - 1);
    if (port_s[0]) g_cfg.mqtt_port = atoi(port_s);

    config_save();
    mqtt_manager_restart();

    return send_html(req, "<p class='ok'>MQTT settings saved.</p>"
                          "<meta http-equiv='refresh' content='2;url=/mqtt'>");
}

// ---- Settings page ----

static esp_err_t settings_page_handler(httpd_req_t *req)
{
    char body[1024];
    snprintf(body, sizeof(body),
        "<h2>Heating Settings</h2>"
        "<form method='POST' action='/settings'>"
        "<label>Solar threshold (W) - Relay1 turns ON above this"
        "<input name='thr' value='%.0f' type='number' step='10'></label>"
        "<label>Temp min (C) - turn relay1 back on below this"
        "<input name='tmin' value='%.1f' type='number' step='0.5'></label>"
        "<label>Temp max (C) - turn relay1 off above this"
        "<input name='tmax' value='%.1f' type='number' step='0.5'></label>"
        "<label>Safety temp (C) - lockout if Sensor2 exceeds this"
        "<input name='tsafe' value='%.1f' type='number' step='0.5'></label>"
        "<label>NTP server - for clock sync"
        "<input name='ntp' value='%s'></label>"
        "<input type='submit' value='Save'>"
        "</form>",
        g_cfg.solar_threshold, g_cfg.temp_min, g_cfg.temp_max, g_cfg.temp_safety,
        g_cfg.ntp_server);
    return send_html(req, body);
}

static esp_err_t settings_save_handler(httpd_req_t *req)
{
    char body[512];
    recv_body(req, body, sizeof(body));

    char thr[16], tmin[16], tmax[16], tsafe[16], ntp[64];
    get_field(body, "thr",   thr,   sizeof(thr));
    get_field(body, "tmin",  tmin,  sizeof(tmin));
    get_field(body, "tmax",  tmax,  sizeof(tmax));
    get_field(body, "tsafe", tsafe, sizeof(tsafe));
    get_field(body, "ntp",   ntp,   sizeof(ntp));

    if (thr[0])   g_cfg.solar_threshold = strtof(thr,  NULL);
    if (tmin[0])  g_cfg.temp_min        = strtof(tmin, NULL);
    if (tmax[0])  g_cfg.temp_max        = strtof(tmax, NULL);
    if (tsafe[0]) g_cfg.temp_safety     = strtof(tsafe, NULL);
    if (ntp[0])   strncpy(g_cfg.ntp_server, ntp, sizeof(g_cfg.ntp_server) - 1);
    config_save();

    return send_html(req, "<p class='ok'>Settings saved.</p>"
                          "<meta http-equiv='refresh' content='2;url=/settings'>");
}

// ---- Relay2 toggle ----

static esp_err_t relay2_handler(httpd_req_t *req)
{
    char query[32] = {0};
    httpd_req_get_url_query_str(req, query, sizeof(query));
    char state[8] = {0};
    httpd_query_key_value(query, "state", state, sizeof(state));
    g_cfg.relay2_manual = (strcmp(state, "on") == 0);
    config_save();
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

// ---- Captive portal detection ----

static esp_err_t captive_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/wifi");
    return httpd_resp_send(req, NULL, 0);
}

// ---- Server start/stop ----

void web_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 20;
    cfg.uri_match_fn     = httpd_uri_match_wildcard;
    cfg.stack_size       = 8192;

    ESP_ERROR_CHECK(httpd_start(&s_server, &cfg));

#define REG(u, m, h) do { \
    httpd_uri_t _u = {.uri=u, .method=m, .handler=h}; \
    httpd_register_uri_handler(s_server, &_u); \
} while(0)

    REG("/",                    HTTP_GET,  status_handler);
    REG("/wifi",                HTTP_GET,  wifi_page_handler);
    REG("/wifi",                HTTP_POST, wifi_save_handler);
    REG("/mqtt",                HTTP_GET,  mqtt_page_handler);
    REG("/mqtt",                HTTP_POST, mqtt_save_handler);
    REG("/settings",            HTTP_GET,  settings_page_handler);
    REG("/settings",            HTTP_POST, settings_save_handler);
    REG("/relay2",              HTTP_GET,  relay2_handler);

    // Captive portal detection URLs
    REG("/generate_204",        HTTP_GET,  captive_redirect);
    REG("/hotspot-detect.html", HTTP_GET,  captive_redirect);
    REG("/ncsi.txt",            HTTP_GET,  captive_redirect);
    REG("/connecttest.txt",     HTTP_GET,  captive_redirect);
    REG("/redirect",            HTTP_GET,  captive_redirect);

    ota_register_handlers(s_server);

    ESP_LOGI(TAG, "HTTP server started");
}

void web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
