#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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
    ".ok,.err{display:inline-block;padding:3px 11px;border-radius:20px;font-weight:600}"
    ".ok{background:#c8e6c9;color:#1b5e20}"
    ".err{background:#ffcdd2;color:#b71c1c}"
    "table{width:100%;border-collapse:collapse;margin-top:16px;font-size:.9rem}"
    "th,td{padding:7px 10px;border:1px solid #ddd;text-align:left}"
    "th{background:#f5f5f5;font-weight:600}"
    "label{display:block;margin-top:14px;font-size:.9rem;color:#555}"
    "input,select{width:100%;box-sizing:border-box;padding:7px;margin-top:4px;"
        "border:1px solid #ccc;border-radius:6px;font-size:1rem}"
    "select{background:#fff}"
    "button,input[type=submit]{margin-top:20px;width:100%;padding:10px;"
        "background:#1565c0;color:#fff;border:none;border-radius:6px;"
        "font-size:1rem;cursor:pointer}"
    "button:hover,input[type=submit]:hover{background:#0d47a1}"
    ".note{color:#666;font-size:.82rem;margin-top:8px}"
    "</style></head><body>"
    "<div class='nav'>"
    "<a href='/'>\xF0\x9F\x93\x8A Status</a>"
    "<a href='/wifi'>\xF0\x9F\x93\xB6 WiFi</a>"
    "<a href='/mqtt'>\xF0\x9F\x93\xA1 MQTT</a>"
    "<a href='/settings'>\xE2\x9A\x99\xEF\xB8\x8F Settings</a>"
    "<a href='/ota'>\xF0\x9F\x94\x84 OTA</a>"
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
    int   lock_sensor = g_state.lockout_sensor;
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

    char lock_msg[256] = "";
    if (lock) {
        snprintf(lock_msg, sizeof(lock_msg),
                 "<p class='err'><b>!! SAFETY LOCKOUT - Sensor%d overheated !!</b></p>"
                 "<form method='GET' action='/lockout/reset'>"
                 "<input type='submit' value='Reset Lockout'></form>",
                 lock_sensor);
    } else if (g_cfg.last_lockout_sensor) {
        snprintf(lock_msg, sizeof(lock_msg),
                 "<p class='note'>Last lockout: Sensor%d at %s</p>",
                 g_cfg.last_lockout_sensor, g_cfg.last_lockout_time);
    }

    char body[3000];
    snprintf(body, sizeof(body),
        "<h2>EHeating Status</h2>"
        "<p>WiFi: <b class='%s'>%s</b> (%s, %s) &nbsp; MQTT: <b class='%s'>%s</b></p>"
        "<div id='lockmsg'>%s</div>"
        "<table>"
        "<tr><td>Sensor1 (water):</td><td><b id='t1'>%s</b></td></tr>"
        "<tr><td>Sensor2 (safety):</td><td><b id='t2'>%s</b></td></tr>"
        "<tr><td>Solar power (10min avg):</td><td><b id='sol' class='%s'>%.0f W</b></td></tr>"
        "<tr><td>Solar threshold:</td><td><b id='thr'>%.0f W</b></td></tr>"
        "<tr><td>Relay1 (solar heat):</td><td><b id='r1' class='%s'>%s</b></td></tr>"
        "<tr><td>Relay2 (manual):</td><td><b id='r2' class='%s'>%s</b></td></tr>"
        "<tr><td colspan=2><hr></td></tr>"
        "<tr><td>T1 address:</td><td><code>%s</code></td></tr>"
        "<tr><td>T2 address:</td><td><code>%s</code></td></tr>"
        "<tr><td>MQTT server:</td><td><code>%s</code></td></tr>"
        "<tr><td>MQTT topic:</td><td><code>%s</code></td></tr>"
        "</table>"
        "<br><form method='GET' action='/relay2'>"
        "<input type='hidden' id='r2state' name='state' value='%s'>"
        "<input type='submit' value='Toggle Relay2'></form>"
        "<script>"
        "function upd(){fetch('/status.json').then(r=>r.json()).then(d=>{"
        "document.getElementById('t1').textContent=d.s1ok?(d.t1.toFixed(1)+' C'):'ERROR';"
        "document.getElementById('t2').textContent=d.s2ok?(d.t2.toFixed(1)+' C'):'ERROR';"
        "var sol=document.getElementById('sol');"
        "sol.textContent=d.sol.toFixed(0)+' W';"
        "sol.className=d.sol>d.thr?'ok':'err';"
        "document.getElementById('thr').textContent=d.thr.toFixed(0)+' W';"
        "var r1=document.getElementById('r1');"
        "r1.textContent=d.r1?'ON':'OFF'; r1.className=d.r1?'ok':'err';"
        "var r2=document.getElementById('r2');"
        "r2.textContent=d.r2?'ON':'OFF'; r2.className=d.r2?'ok':'err';"
        "document.getElementById('r2state').value=d.r2?'off':'on';"
        "var lm=document.getElementById('lockmsg');"
        "if(d.lock){lm.innerHTML=\"<p class='err'><b>!! SAFETY LOCKOUT - Sensor\"+d.lock_sensor+\" overheated !!</b></p>"
            "<form method='GET' action='/lockout/reset'><input type='submit' value='Reset Lockout'></form>\";}"
        "else if(d.last_lock_sensor){lm.innerHTML=\"<p class='note'>Last lockout: Sensor\"+d.last_lock_sensor+\" at \"+d.last_lock_time+\"</p>\";}"
        "else lm.innerHTML='';"
        "});}"
        "setInterval(upd,5000);"
        "</script>",
        wifi ? "ok" : "err", wifi ? "Connected" : "Disconnected", ip,
        g_cfg.wifi_ssid[0] ? g_cfg.wifi_ssid : "---",
        mqtt ? "ok" : "err", mqtt ? "Connected" : "Disconnected",
        lock_msg,
        t1_str, t2_str,
        sol > g_cfg.solar_threshold ? "ok" : "err", sol, g_cfg.solar_threshold,
        r1 ? "ok" : "err", r1 ? "ON" : "OFF",
        r2 ? "ok" : "err", r2 ? "ON" : "OFF",
        s1addr, s2addr,
        g_cfg.mqtt_server[0] ? g_cfg.mqtt_server : "not set",
        g_cfg.mqtt_topic[0]  ? g_cfg.mqtt_topic  : "not set",
        r2 ? "off" : "on"
    );
    return send_html(req, body);
}

// ---- Status JSON (for AJAX polling) ----

static esp_err_t status_json_handler(httpd_req_t *req)
{
    state_lock();
    float t1   = g_state.sensor1_temp;
    float t2   = g_state.sensor2_temp;
    float sol  = g_state.solar_avg_10min;
    bool  r1   = g_state.relay1_state;
    bool  r2   = g_state.relay2_state;
    bool  lock = g_state.error_lockout;
    int   lock_sensor = g_state.lockout_sensor;
    bool  s1ok = g_state.sensor1_ok;
    bool  s2ok = g_state.sensor2_ok;
    state_unlock();

    char body[384];
    snprintf(body, sizeof(body),
        "{\"t1\":%.1f,\"s1ok\":%s,\"t2\":%.1f,\"s2ok\":%s,"
        "\"sol\":%.0f,\"thr\":%.0f,\"r1\":%s,\"r2\":%s,"
        "\"lock\":%s,\"lock_sensor\":%d,"
        "\"last_lock_sensor\":%d,\"last_lock_time\":\"%s\"}",
        t1, s1ok ? "true" : "false", t2, s2ok ? "true" : "false",
        sol, g_cfg.solar_threshold, r1 ? "true" : "false", r2 ? "true" : "false",
        lock ? "true" : "false", lock_sensor,
        g_cfg.last_lockout_sensor, g_cfg.last_lockout_time);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
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

static const char *s_pub_names[PUB_COUNT] = {
    "Sensor 1 (water &deg;C)", "Sensor 2 (safety &deg;C)",
    "Solar power (W)", "Solar threshold (W)",
    "Relay 1", "Relay 2"
};

static esp_err_t mqtt_page_handler(httpd_req_t *req)
{
    char body[2048];
    int pos = snprintf(body, sizeof(body),
        "<h2>MQTT Configuration</h2>"
        "<style>input[type=checkbox]{width:auto}</style>"
        "<form method='POST' action='/mqtt'>"
        "<label>MQTT Enabled"
        "<select name='en'>"
        "<option value='1'%s>Yes</option>"
        "<option value='0'%s>No</option>"
        "</select></label>"
        "<label>Server<input name='srv' value='%s'></label>"
        "<label>Port<input name='port' value='%d' type='number'></label>"
        "<label>Subscribe Topic (incoming solar power W)<input name='topic' value='%s'></label>"
        "<h2>Publish Topics</h2>"
        "<table><tr><th>Channel</th><th>Enable</th><th>Topic</th></tr>",
        g_cfg.mqtt_enabled ? " selected" : "",
        g_cfg.mqtt_enabled ? "" : " selected",
        g_cfg.mqtt_server, g_cfg.mqtt_port, g_cfg.mqtt_topic);

    for (int i = 0; i < PUB_COUNT; i++) {
        pos += snprintf(body + pos, sizeof(body) - pos,
            "<tr><td>%s</td>"
            "<td style='text-align:center'>"
            "<input type='checkbox' name='pen%d' value='1'%s></td>"
            "<td><input name='ptp%d' value='%s'></td></tr>",
            s_pub_names[i], i,
            g_cfg.pub_en[i] ? " checked" : "",
            i, g_cfg.pub_topic[i]);
    }

    snprintf(body + pos, sizeof(body) - pos,
        "</table>"
        "<input type='submit' value='Save'>"
        "</form>");

    return send_html(req, body);
}

static esp_err_t mqtt_save_handler(httpd_req_t *req)
{
    char body[1024];
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

    char key[8], val[128];
    for (int i = 0; i < PUB_COUNT; i++) {
        snprintf(key, sizeof(key), "pen%d", i);
        char en_i[4]; get_field(body, key, en_i, sizeof(en_i));
        g_cfg.pub_en[i] = (en_i[0] == '1');

        snprintf(key, sizeof(key), "ptp%d", i);
        get_field(body, key, val, sizeof(val));
        if (val[0]) strncpy(g_cfg.pub_topic[i], val, sizeof(g_cfg.pub_topic[i]) - 1);
    }

    config_save();
    mqtt_manager_restart();

    return send_html(req, "<p class='ok'>MQTT settings saved.</p>"
                          "<meta http-equiv='refresh' content='2;url=/mqtt'>");
}

// ---- Settings page ----

static esp_err_t settings_page_handler(httpd_req_t *req)
{
    char body[1536];
    snprintf(body, sizeof(body),
        "<h2>Heating Settings</h2>"
        "<form method='POST' action='/settings'>"
        "<label>Solar threshold (W) - Relay1 turns ON above this"
        "<input name='thr' value='%.0f' type='number' step='10'></label>"
        "<label>Temp min (C) - turn relay1 back on below this"
        "<input name='tmin' value='%.1f' type='number' step='0.5'></label>"
        "<label>Temp max (C) - turn relay1 off above this"
        "<input name='tmax' value='%.1f' type='number' step='0.5'></label>"
        "<label>Safety temp (C) - lockout if Sensor1 exceeds this"
        "<input name='tsafe1' value='%.1f' type='number' step='0.5'></label>"
        "<label>Safety temp (C) - lockout if Sensor2 exceeds this"
        "<input name='tsafe' value='%.1f' type='number' step='0.5'></label>"
        "<label>NTP server - for clock sync"
        "<input name='ntp' value='%s'></label>"
        "<label>Timezone (POSIX TZ string, e.g. Estonia: EET-2EEST,M3.5.0/3,M10.5.0/4)"
        "<input name='tz' value='%s'></label>"
        "<input type='submit' value='Save'>"
        "</form>",
        g_cfg.solar_threshold, g_cfg.temp_min, g_cfg.temp_max,
        g_cfg.temp_safety1, g_cfg.temp_safety,
        g_cfg.ntp_server, g_cfg.tz);
    return send_html(req, body);
}

static esp_err_t settings_save_handler(httpd_req_t *req)
{
    char body[512];
    recv_body(req, body, sizeof(body));

    char thr[16], tmin[16], tmax[16], tsafe[16], tsafe1[16], ntp[64], tz[64];
    get_field(body, "thr",    thr,    sizeof(thr));
    get_field(body, "tmin",   tmin,   sizeof(tmin));
    get_field(body, "tmax",   tmax,   sizeof(tmax));
    get_field(body, "tsafe1", tsafe1, sizeof(tsafe1));
    get_field(body, "tsafe",  tsafe,  sizeof(tsafe));
    get_field(body, "ntp",    ntp,    sizeof(ntp));
    get_field(body, "tz",     tz,     sizeof(tz));

    float new_thr    = thr[0]    ? strtof(thr,    NULL) : g_cfg.solar_threshold;
    float new_tmin   = tmin[0]   ? strtof(tmin,   NULL) : g_cfg.temp_min;
    float new_tmax   = tmax[0]   ? strtof(tmax,   NULL) : g_cfg.temp_max;
    float new_tsafe1 = tsafe1[0] ? strtof(tsafe1, NULL) : g_cfg.temp_safety1;
    float new_tsafe  = tsafe[0]  ? strtof(tsafe,  NULL) : g_cfg.temp_safety;

    if (new_thr < 0 || new_tmin >= new_tmax || new_tmax >= new_tsafe1 || new_tsafe <= 0) {
        return send_html(req,
            "<p class='err'>Invalid settings: require threshold &gt;= 0, "
            "temp min &lt; temp max &lt; safety temp (sensor1), and safety temp (sensor2) &gt; 0. "
            "Nothing saved.</p>"
            "<meta http-equiv='refresh' content='3;url=/settings'>");
    }

    g_cfg.solar_threshold = new_thr;
    g_cfg.temp_min        = new_tmin;
    g_cfg.temp_max        = new_tmax;
    g_cfg.temp_safety1    = new_tsafe1;
    g_cfg.temp_safety     = new_tsafe;
    if (ntp[0])    strncpy(g_cfg.ntp_server, ntp, sizeof(g_cfg.ntp_server) - 1);
    if (tz[0]) {
        strncpy(g_cfg.tz, tz, sizeof(g_cfg.tz) - 1);
        setenv("TZ", g_cfg.tz, 1);
        tzset();
    }
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

// ---- Lockout reset ----

static esp_err_t lockout_reset_confirm_handler(httpd_req_t *req)
{
    return send_html(req,
        "<h2>Reset Safety Lockout</h2>"
        "<p>This clears the active safety lockout and re-enables relay control. "
        "Only do this after confirming the overheat cause is resolved.</p>"
        "<form method='POST' action='/lockout/reset'>"
        "<input type='submit' value='Confirm Reset'></form>");
}

static esp_err_t lockout_reset_handler(httpd_req_t *req)
{
    state_lock();
    g_state.error_lockout  = false;
    g_state.lockout_sensor = 0;
    state_unlock();

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
    REG("/status.json",         HTTP_GET,  status_json_handler);
    REG("/wifi",                HTTP_GET,  wifi_page_handler);
    REG("/wifi",                HTTP_POST, wifi_save_handler);
    REG("/mqtt",                HTTP_GET,  mqtt_page_handler);
    REG("/mqtt",                HTTP_POST, mqtt_save_handler);
    REG("/settings",            HTTP_GET,  settings_page_handler);
    REG("/settings",            HTTP_POST, settings_save_handler);
    REG("/relay2",              HTTP_GET,  relay2_handler);
    REG("/lockout/reset",       HTTP_GET,  lockout_reset_confirm_handler);
    REG("/lockout/reset",       HTTP_POST, lockout_reset_handler);

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
