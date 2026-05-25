# EHeating — ESP32-S3 Solar Water Heater Controller

ESP-IDF firmware for the **Guition ESP32-S3-4848S040** (480×480 capacitive touch display) that controls electric water heater relays based on solar panel output power and water temperature.

## Features

- **WiFi captive portal** — first-boot AP mode with auto-opening config page (Android, iOS, Windows detected)
- **MQTT solar power input** — subscribes to configurable topic, computes 10-minute rolling average
- **Temperature hysteresis** — Sensor1 controls Relay1 in 55–60 °C band (both limits configurable)
- **Safety lockout** — Sensor2 triggers permanent relay-off if temperature exceeds limit (default 65 °C)
- **Web UI** — status dashboard (auto-refresh), WiFi setup, MQTT config, settings, Relay2 manual toggle
- **OTA firmware update** — upload `.bin` via browser
- **480×480 LCD** — live status display: both temps, solar power, relay states, WiFi/MQTT indicators

## Hardware

| Item | Details |
|------|---------|
| Board | Guition ESP32-S3-4848S040 |
| Display | ST7701S 480×480 RGB, 16-bit parallel |
| Backlight | GPIO38 (LEDC PWM) |
| Temperature sensors | 2× DS18B20 on GPIO1 (1-Wire) |
| Relay 1 | GPIO40, active LOW |
| Relay 2 | GPIO2, active LOW |
| PSRAM | 8 MB Octal 80 MHz |

### Wiring

```
DS18B20 DATA  →  GPIO1  (add 4.7kΩ pull-up to 3.3V)
Relay 1       →  GPIO40 (LOW = ON)
Relay 2       →  GPIO2  (LOW = ON)
```

## Control Logic

### Relay 1 (solar heating)

```
Turn ON  when: solar_10min_avg > threshold  AND  sensor1_temp < temp_max
Turn OFF when: solar_10min_avg ≤ threshold  OR   sensor1_temp ≥ temp_max
Resume   when: sensor1_temp < temp_min  (hysteresis — prevents relay chatter)
```

### Safety lockout

If `sensor2_temp ≥ temp_safety` at any point, both relays turn off and stay off until the device reboots.

### Relay 2

Manual on/off via the web UI. State persists across reboots (NVS).

## Getting Started

### Requirements

- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
- Target: `esp32s3`

### Build & Flash

```bash
git clone https://github.com/kaar3l/EHeating-ESP32S3-ESPIDF.git
cd EHeating-ESP32S3-ESPIDF

idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

### First Boot

1. Device starts WiFi AP **`EHeating-Setup`** (open, no password)
2. Connect phone/laptop to that network
3. Config page opens automatically (captive portal)
4. Enter your WiFi credentials → Save
5. Device connects to your network and shuts down the AP
6. Open `http://<device-ip>/` for the status dashboard

## Web Interface

| URL | Description |
|-----|-------------|
| `/` | Status dashboard (auto-refreshes every 5 s) |
| `/wifi` | WiFi SSID / password |
| `/mqtt` | MQTT on/off, server, port, subscribe topic |
| `/settings` | Solar threshold (W), temp min/max/safety (°C) |
| `/relay2` | Toggle Relay 2 manually |
| `/ota` | Upload new firmware `.bin` |

## MQTT

The device subscribes to a configurable topic and expects **plain-text float values** (watts):

```
solar/power  →  "1250.5"
```

The 10-minute rolling average is recomputed every second. Relay 1 activates when this average exceeds the configured threshold.

## Default Settings

| Parameter | Default |
|-----------|---------|
| Solar threshold | 500 W |
| Temp min (hysteresis low) | 55 °C |
| Temp max (hysteresis high) | 60 °C |
| Safety temp (Sensor 2) | 65 °C |
| MQTT port | 1883 |

All values are configurable via `/settings` and `/mqtt` and stored in NVS flash.

## OTA Update

1. Build new firmware: `idf.py build`
2. Navigate to `http://<device-ip>/ota`
3. Select `build/EHeating.bin` and upload
4. Device flashes and reboots automatically

## Project Structure

```
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv          # dual OTA partition layout
└── main/
    ├── main.c              # boot sequence, FreeRTOS tasks
    ├── pin_config.h        # all GPIO defines
    ├── app_state.h         # global state (mutex-protected)
    ├── nvs_config.[ch]     # NVS persistence
    ├── wifi_manager.[ch]   # AP + STA + event handling
    ├── dns_server.[ch]     # captive portal DNS (UDP/53)
    ├── web_server.[ch]     # HTTP server + all page handlers
    ├── mqtt_manager.[ch]   # MQTT client + ring buffer
    ├── ds18b20.[ch]        # 1-Wire GPIO bitbang driver
    ├── relay_control.[ch]  # relay logic + safety lockout
    ├── display.[ch]        # ST7701S init + RGB panel + font renderer
    └── ota_manager.[ch]    # firmware upload endpoint
```

## License

MIT
