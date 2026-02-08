/*
 * ESP32-4848S040 - V1: Hello World
 *
 * Minimal sketch to verify the display works.
 * Initializes the ST7701 480x480 RGB panel and shows "Hello World".
 * No touch, no WiFi, no LVGL - just the display.
 *
 * Board Settings (Arduino IDE):
 *   Board:            ESP32S3 Dev Module
 *   Flash Size:       16MB (128Mb)
 *   Flash Mode:       QIO 80MHz
 *   PSRAM:            OPI PSRAM
 *   Partition Scheme: Default 4MB with spiffs (or "Max APP 8MB" if available)
 *   CPU Frequency:    240MHz (WiFi)
 *   Upload Speed:     921600
 */

#include <Arduino_GFX_Library.h>

// Backlight pin
#define GFX_BL 38

// Display bus - RGB parallel interface
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    39 /* CS */, 48 /* SCK */, 47 /* SDA */,
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0 /* R4 */,
    8 /* G0 */, 20 /* G1 */, 3 /* G2 */, 46 /* G3 */, 9 /* G4 */, 10 /* G5 */,
    4 /* B0 */, 5 /* B1 */, 6 /* B2 */, 7 /* B3 */, 15 /* B4 */
);

// ST7701 display driver - 480x480 IPS panel
Arduino_ST7701_RGBPanel *gfx = new Arduino_ST7701_RGBPanel(
    bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */,
    true /* IPS */, 480 /* width */, 480 /* height */,
    st7701_type1_init_operations, sizeof(st7701_type1_init_operations),
    true /* BGR */,
    10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */
);

void setup() {
    Serial.begin(115200);
    Serial.println("V1: Hello World - Starting...");

    // Initialize display
    gfx->begin(16000000);
    gfx->fillScreen(BLACK);

    // Turn on backlight
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    // Draw text
    gfx->setTextSize(4);
    gfx->setTextColor(WHITE);
    gfx->setCursor(100, 200);
    gfx->println("Hello World!");

    gfx->setTextSize(2);
    gfx->setTextColor(GREEN);
    gfx->setCursor(100, 260);
    gfx->println("ESP32-4848S040 is alive");

    gfx->setTextSize(2);
    gfx->setTextColor(YELLOW);
    gfx->setCursor(100, 300);
    gfx->println("Display: 480x480 ST7701");

    Serial.println("V1: Hello World - Display ready!");
}

void loop() {
    // Nothing to do - just keep the display on
    delay(1000);
}
