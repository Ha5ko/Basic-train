# ESP32-4848S040 Device Reference Guide

## Device Overview

**Manufacturer:** Shenzhen Jingcai Intelligent Co., Ltd (JCZN)
**Model:** ESP32-4848S040C_I_Y_1 (1 relay) / ESP32-4848S040C_I_Y_3 (3 relay)
**MCU:** ESP32-S3-WROOM-1 (Dual-core Tensilica LX7, 240MHz)
**Form Factor:** 86mm x 86mm wall switch panel (standard EU/CN wall switch size)

### Core Specifications

| Parameter | Value |
|---|---|
| MCU | ESP32-S3-WROOM-1, Dual-core @ 240MHz |
| SRAM | 512KB |
| ROM | 384KB |
| PSRAM | 8MB (OPI) |
| Flash | 16MB (QIO 80MHz) |
| Display | 4.0" TFT, 480x480, 16-bit RGB 65K color |
| Display Driver | ST7701 (RGB parallel interface) |
| Touch | GT911 Capacitive (I2C) |
| WiFi | 802.11 b/g/n 2.4GHz |
| Bluetooth | BLE 4.2 / Classic Bluetooth |
| Audio IC | NS4168 (I2S DAC) |
| Operating Voltage | 5V (USB-C) |
| Power Consumption | ~260mA |
| Operating Temp | -20C to 70C |
| Module Dimensions | 86.5 x 86.5 x 37.8 mm |
| Effective Display Area | 71.8 x 70.2 mm |
| Weight | ~200g |

### Physical Interfaces (Side Panel)
- **TF (MicroSD) Card Slot** - top
- **Lithium Battery Connector** (JST 1.25 2P) - middle
- **USB Type-C** - bottom (programming + power)

### Expansion Connectors
- **P2 Header (7-pin):** GND, relay1/3, relay2/5, relay3/7, IO9, RST, U0TXD, U0RXD, GND, 3.3V, 5V
- **P1 Header (4-pin):** VIN, U0TXD, U0RXD, GND (HC-1.25 connector)

---

## Pin Mapping (Complete)

### Display - ST7701 RGB Parallel Interface

| Function | GPIO |
|---|---|
| CS | 39 |
| SCK (SPI Clock) | 48 |
| SDA (SPI Data) | 47 |
| DE (Data Enable) | 18 |
| VSYNC | 17 |
| HSYNC | 16 |
| PCLK (Pixel Clock) | 21 |
| Backlight (BL) | 38 |
| **Red Channel** | |
| R0 | 11 |
| R1 | 12 |
| R2 | 13 |
| R3 | 14 |
| R4 | 0 |
| **Green Channel** | |
| G0 | 8 |
| G1 | 20 |
| G2 | 3 |
| G3 | 46 |
| G4 | 9 |
| G5 | 10 |
| **Blue Channel** | |
| B0 | 4 |
| B1 | 5 |
| B2 | 6 |
| B3 | 7 |
| B4 | 15 |

### Touch - GT911 (I2C)

| Function | GPIO |
|---|---|
| SDA | 19 |
| SCL | 45 |
| INT | -1 (not connected) |
| RST | -1 (not connected) |

### Relay Outputs

| Function | GPIO | Notes |
|---|---|---|
| Relay 1 / Light 1 | 40 | Shared with I2S audio (DIN) via R25/R21 |
| Relay 2 / Light 2 | 2 | Shared with I2S audio (LRCLK) via R26/R22 |
| Relay 3 / Light 3 | 1 | Shared with I2S audio (BCLK) via R27/R23 |

**Important:** Relay GPIOs (IO1, IO2, IO40) are shared with I2S audio pins. To use audio instead of relays, move 0-ohm resistors R25->R21, R26->R22, R27->R23.

### I2S Audio (NS4168 DAC) - Optional

| Function | GPIO | Notes |
|---|---|---|
| DIN (Data) | 40 | Requires R21 (instead of R25 for relay) |
| LRCLK | 2 | Requires R22 (instead of R26 for relay) |
| BCLK | 1 | Requires R23 (instead of R27 for relay) |

### SD Card (SPI)

| Function | GPIO |
|---|---|
| CS (TF_CS) | 42 |
| MOSI (MCU_MOSI) | 47 |
| CLK (TF_CLK) | 48 |
| MISO (MCU_MISO) | 41 |

### USB-to-TTL (CH340C)

| Function | GPIO |
|---|---|
| U0TXD | 43 |
| U0RXD | 44 |

---

## Arduino IDE Setup

### Requirements
- Arduino IDE 1.8.19+ (or Arduino IDE 2.x)
- ESP32 Board Package 2.0.6+ (via Boards Manager)

### Board Manager URL
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

### Arduino IDE Board Settings

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Upload Speed | 921600 |
| USB Mode | Hardware CDC and JTAG |
| USB CDC On Boot | Disabled |
| USB Firmware MSC On Boot | Disabled |
| USB DFU On Boot | Disabled |
| Upload Mode | UART0 / Hardware CDC |
| CPU Frequency | 240MHz (WiFi) |
| Flash Mode | QIO 80MHz |
| Flash Size | 16MB (128Mb) |
| Partition Scheme | Max APP (8MB) |
| Core Debug Level | None |
| PSRAM | OPI PSRAM |
| Arduino Runs On | Core 1 |
| Events Run On | Core 1 |
| Programmer | Esptool |

### Required Libraries
1. **Arduino_GFX** (GFX Library for Arduino by Moon On Our Nation) - Display driver
2. **LVGL** (v8.3.3) - GUI framework
3. **ArduinoJson** - JSON parsing for API responses
4. **ArduinoZlib** - Gzip decompression for weather API
5. **ESPAsyncWebServer** - Async HTTP server (for relay web control)
6. **Time** - Time functions

### Custom Partition Table
Replace `huge_app.csv` at:
```
{Arduino15}/packages/esp32/hardware/esp32/{version}/tools/partitions/
```

### LVGL Configuration
Copy `lv_conf.h` from `1_2_4.0_LvglWidgets/LVGL configuration replacement file/` to your Arduino libraries root directory (same level as `lvgl/` folder).

Key settings in lv_conf.h:
```c
#define LV_COLOR_DEPTH 16
#define LV_TICK_CUSTOM 1
#define LV_COLOR_16_SWAP 1
```

---

## Code Architecture Patterns

### Display Initialization
```cpp
#include <Arduino_GFX_Library.h>

Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    39, 48, 47, 18, 17, 16, 21,  // CS, SCK, SDA, DE, VSYNC, HSYNC, PCLK
    11, 12, 13, 14, 0,           // R0-R4
    8, 20, 3, 46, 9, 10,         // G0-G5
    4, 5, 6, 7, 15               // B0-B4
);

Arduino_ST7701_RGBPanel *gfx = new Arduino_ST7701_RGBPanel(
    bus, GFX_NOT_DEFINED, 0, true, 480, 480,
    st7701_type1_init_operations, sizeof(st7701_type1_init_operations),
    true, 10, 8, 50, 10, 8, 20
);
```

### Backlight Control (PWM)
```cpp
#define GFX_BL 38
ledcSetup(0, 600, 8);           // Channel 0, 600Hz, 8-bit resolution
ledcAttachPin(GFX_BL, 0);
ledcWrite(0, 150);              // Brightness 0-255
```

### Touch Initialization (GT911)
```cpp
#include <Wire.h>
#include <TAMC_GT911.h>

TAMC_GT911 ts = TAMC_GT911(19, 45, -1, -1, 480, 480);

void touch_init() {
    Wire.begin(19, 45);
    ts.begin();
    ts.setRotation(ROTATION_NORMAL);
}

bool touch_touched() {
    ts.read();
    if (ts.isTouched) {
        touch_last_x = map(ts.points[0].x, 480, 0, 0, 479);
        touch_last_y = map(ts.points[0].y, 480, 0, 0, 479);
        return true;
    }
    return false;
}
```

### LVGL Setup Pattern
```cpp
#include <lvgl.h>

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf;

void setup() {
    lv_init();

    // Allocate display buffer in PSRAM
    buf = (lv_color_t *)heap_caps_malloc(
        sizeof(lv_color_t) * 480 * 480,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, 480 * 480);

    // Register display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 480;
    disp_drv.ver_res = 480;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Register touch input
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);
}

void loop() {
    lv_timer_handler();
    delay(5);
}
```

### Relay Control
```cpp
void init_relays() {
    pinMode(40, OUTPUT);   // Relay 1
    pinMode(2, OUTPUT);    // Relay 2
    pinMode(1, OUTPUT);    // Relay 3
    digitalWrite(40, LOW);
    digitalWrite(2, LOW);
    digitalWrite(1, LOW);
}

void relay_on(int pin) { digitalWrite(pin, HIGH); }
void relay_off(int pin) { digitalWrite(pin, LOW); }
```

### WiFi Station Mode
```cpp
#include <WiFi.h>

WiFi.begin("SSID", "password");
WiFi.setAutoReconnect(true);
while (!WiFi.isConnected()) { delay(500); }
Serial.println(WiFi.localIP());
```

### WiFi Web Server (Async)
```cpp
#include <ESPAsyncWebServer.h>

AsyncWebServer server(80);

server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", html_page);
});
server.begin();
```

### BLE Service
```cpp
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

BLEDevice::init("ESP32-BLE");
BLEServer *pServer = BLEDevice::createServer();
BLEService *pService = pServer->createService("UUID");
BLECharacteristic *pChar = pService->createCharacteristic("UUID",
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
pChar->setValue("Hello");
pService->start();
BLEDevice::startAdvertising();
```

### FreeRTOS Task Pattern
```cpp
xTaskCreatePinnedToCore(
    taskFunction,    // Function
    "taskName",      // Name
    4096,            // Stack size
    NULL,            // Parameters
    1,               // Priority
    NULL,            // Task handle
    1                // Core (0 or 1)
);

// Inside task function, delete when done:
vTaskDelete(NULL);
```

---

## Demo Programs Summary

| # | Demo | Description | Key Features |
|---|---|---|---|
| 1_1 | 86switch_onoff | Full home switch UI | LVGL GUI, 3 relay control, WiFi, weather API, NTP clock |
| 1_2 | 4.0_LvglWidgets | LVGL widget showcase | Touch + display with LVGL widget demos |
| 1_3 | switch86_lvgl_music | Music player UI | LVGL music demo with display |
| 2_1 | Helloworld | Serial hello world | Basic serial output test |
| 2_2 | Uart | Serial formatting | Number format printing (BIN/OCT/DEC/HEX) |
| 3_1 | Wifi_AP | WiFi Access Point | ESP32 as hotspot |
| 3_2 | Wifi_STA | WiFi Station | Connect to existing network |
| 3_3 | Wifi_SmartConfig | SmartConfig | Phone-based WiFi provisioning |
| 3_4 | Wifi_STA_TCP_Server | TCP Server | WiFi station + TCP echo server on port 10000 |
| 3_5 | WIFI_STA_TCP_Client | TCP Client | WiFi station + TCP client |
| 3_6 | WIFI_STA_UDP | UDP | WiFi station + UDP communication |
| 3_7 | WIFI Web Servers LED | Web LED Control | HTTP web server controlling LEDs |
| 3_8 | WIFI Web Servers Relay | Web Relay Control | Async web server controlling relays with toggle UI |
| 4_1 | BleService | BLE Service | BLE GATT server with read/write characteristic |

---

## Project Ideas for Home Automation

Based on this device's capabilities, here are potential projects:

1. **Smart Light Switch Panel** - Control 1-3 lights with touch UI, WiFi remote control, scheduling
2. **Home Dashboard** - Display weather, time, calendar, sensor data on the 480x480 screen
3. **MQTT Smart Switch** - Integrate with Home Assistant / MQTT broker for smart home ecosystem
4. **Thermostat Controller** - Add temperature sensor, control HVAC relay with schedule
5. **Security Panel** - BLE proximity detection, WiFi camera integration, alarm relay
6. **Scene Controller** - One-touch scenes (movie mode, sleep mode) controlling multiple relays
7. **Energy Monitor** - Display power consumption data from CT sensors via WiFi
8. **Intercom Panel** - Audio (NS4168) + WiFi for room-to-room communication
9. **Irrigation Controller** - Relay-controlled valves with weather-based scheduling
10. **Garage Door Controller** - Relay control + status monitoring via web/BLE
