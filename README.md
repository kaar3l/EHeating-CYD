# EHeating CYD — Solar Water Heater Controller

ESP-IDF firmware for the **ESP32-2432S028** ("Cheap Yellow Display") that controls an electric water heater relay based on solar panel output power and water temperature.

## Features

- **WiFi captive portal** — first-boot AP mode with auto-opening config page (Android, iOS, Windows)
- **MQTT solar power input** — subscribes to configurable topic, computes 10-minute rolling average
- **Temperature hysteresis** — Sensor1 controls Relay1 in 55–60 °C band (both limits configurable)
- **Safety lockout** — Sensor1 or Sensor2 triggers permanent relay-off if its temperature exceeds its own limit (each configurable, default 65 °C); web UI / LCD show which sensor tripped it
- **Web UI** — status dashboard (auto-refresh), WiFi setup, MQTT config, settings, Relay2 manual toggle
- **OTA firmware update** — upload `.bin` via browser
- **320×240 ILI9341 LCD** — live status: temps, solar power vs threshold, relay states, WiFi/MQTT/RSSI/IP

## Hardware

| Item | Details |
|------|---------|
| Board | ESP32-2432S028 (ESP32-D0WD-V3, 4 MB flash) |
| Display | ILI9341 2.8″ 320×240 TFT (SPI2) |
| Temperature sensors | 2× DS18B20 on GPIO27 (1-Wire) |
| Relay 1 | GPIO16, active LOW |
| Relay 2 | GPIO4, active LOW |

### Wiring

```
DS18B20 DATA  →  GPIO27  (add 4.7 kΩ pull-up to 3.3 V)
Relay 1       →  GPIO16  (LOW = ON)
Relay 2       →  GPIO4   (LOW = ON)
```

## Control Logic

### Relay 1 (solar heating)

```
Turn ON  when: solar_10min_avg > threshold  AND  sensor1_temp < temp_max
Turn OFF when: solar_10min_avg ≤ threshold  OR   sensor1_temp ≥ temp_max
Resume   when: sensor1_temp < temp_min  (hysteresis — prevents relay chatter)
```

> **Note:** The MQTT broker is expected to publish **negative watts** when panels are producing (e.g. `-1000` = 1000 W production). The firmware negates the value internally so all comparisons use positive numbers.

### Safety lockout

If `sensor1_temp ≥ temp_safety1` or `sensor2_temp ≥ temp_safety` at any point, both relays turn off and stay off until the device reboots. The web status page and LCD lockout banner name the sensor that tripped it (e.g. "SAFETY LOCKOUT - Sensor1 overheated").

### Relay 2

Manual on/off via the web UI. State persists across reboots (NVS).

## Getting Started

### Requirements

- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/)
- Target: `esp32`

### Build & Flash

```bash
git clone https://github.com/kaar3l/EHeating-CYD.git
cd EHeating-CYD

idf.py build
idf.py -p /dev/ttyUSB0 flash
```

### First Boot

1. Device starts WiFi AP **`EHeating-Setup`** (open, no password)
2. Connect phone/laptop to that network — config page opens automatically
3. Enter your WiFi credentials → Save
4. Device connects to your network and shuts down the AP
5. Open `http://<device-ip>/` for the status dashboard

## Web Interface

| URL | Description |
|-----|-------------|
| `/` | Status dashboard (auto-refreshes every 5 s) |
| `/wifi` | WiFi SSID / password |
| `/mqtt` | MQTT on/off, server, port, subscribe topic |
| `/settings` | Solar threshold (W), temp min/max (°C), safety temps for Sensor1 & Sensor2 (°C) |
| `/relay2` | Toggle Relay 2 manually |
| `/ota` | Upload new firmware `.bin` |

## LCD Display

The status screen shows (updated every second, flicker-free):

| Row | Content | Colour |
|-----|---------|--------|
| Title | `EHeating` + WiFi/MQTT indicators | Cyan / Green / Gray |
| T1 | Water temperature (Sensor 1) | White / Red on error |
| T2 | Safety temperature (Sensor 2) | Yellow / Red on error |
| Solar | 10-min average solar power | Green if above threshold, Red if below |
| Thr | Configured solar threshold | Yellow |
| Relay1 | Heating relay state | Green ON / Red OFF |
| Relay2 | Manual relay state | Green ON / Red OFF |
| SSID | Connected WiFi network | Gray |
| IP | Device IP address | Gray |
| RSSI | WiFi signal strength (dBm) | Gray |
| Lockout | Safety lockout banner, names tripped sensor (bottom) | Red when active |

## MQTT

The device subscribes to a configurable topic and expects **plain-text float values** in watts (negative = producing):

```
solar/power  →  "-1250.5"   (means 1250.5 W being produced)
```

The 10-minute rolling average is recomputed every second. Relay 1 activates when this average exceeds the configured threshold.

## Default Settings

| Parameter | Default |
|-----------|---------|
| Solar threshold | 500 W |
| Temp min (hysteresis low) | 55 °C |
| Temp max (hysteresis high) | 60 °C |
| Safety temp (Sensor 1) | 65 °C |
| Safety temp (Sensor 2) | 65 °C |
| MQTT port | 1883 |

All values configurable via `/settings` and `/mqtt`, stored in NVS flash.

## OTA Update

1. Build: `idf.py build`
2. Navigate to `http://<device-ip>/ota`
3. Select `build/EHeating.bin` → upload
4. Device flashes and reboots automatically

## Project Structure

```
├── CMakeLists.txt
├── sdkconfig.defaults          # esp32 target, 4 MB flash
├── partitions.csv              # dual OTA partition layout
└── main/
    ├── main.c                  # boot sequence, FreeRTOS tasks
    ├── pin_config.h            # all GPIO and LCD defines
    ├── app_state.h             # global state (mutex-protected)
    ├── nvs_config.[ch]         # NVS persistence
    ├── wifi_manager.[ch]       # AP + STA + IP/RSSI helpers
    ├── dns_server.[ch]         # captive portal DNS (UDP/53)
    ├── web_server.[ch]         # HTTP server + all page handlers
    ├── mqtt_manager.[ch]       # MQTT client + 10-min ring buffer
    ├── ds18b20.[ch]            # 1-Wire GPIO bitbang driver
    ├── relay_control.[ch]      # relay logic + safety lockout
    ├── display.[ch]            # ILI9341 SPI driver + font renderer
    └── ota_manager.[ch]        # firmware upload endpoint
```

## License

MIT
