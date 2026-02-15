/*
 * ESP32-4848S040 - V2: Touch Input Test
 *
 * Confirms display + GT911 touch panel are working.
 * Touch the screen to see coordinates and a dot where you touched.
 *
 * Required Libraries (in Arduino libraries folder):
 *   - Arduino_GFX-master  (from manufacturer)
 *   - Touch_GT911         (from manufacturer)
 *
 * Board Settings (Arduino IDE):
 *   Board:            ESP32S3 Dev Module
 *   Flash Mode:       QIO 80MHz
 *   Flash Size:       16MB (128Mb)
 *   PSRAM:            OPI PSRAM
 *   Partition Scheme: Default 4MB with spiffs
 *   CPU Frequency:    240MHz (WiFi)
 */

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <Touch_GT911.h>

// ----- Display Setup -----
#define GFX_BL 38

Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    39 /* CS */, 48 /* SCK */, 47 /* SDA */,
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0 /* R4 */,
    8 /* G0 */, 20 /* G1 */, 3 /* G2 */, 46 /* G3 */, 9 /* G4 */, 10 /* G5 */,
    4 /* B0 */, 5 /* B1 */, 6 /* B2 */, 7 /* B3 */, 15 /* B4 */
);

Arduino_ST7701_RGBPanel *gfx = new Arduino_ST7701_RGBPanel(
    bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */,
    true /* IPS */, 480 /* width */, 480 /* height */,
    st7701_type1_init_operations, sizeof(st7701_type1_init_operations),
    true /* BGR */,
    10, 8, 50,  // hsync: front_porch, pulse_width, back_porch
    10, 8, 20   // vsync: front_porch, pulse_width, back_porch
);

// ----- Touch Setup -----
#define TOUCH_SDA 19
#define TOUCH_SCL 45
#define TOUCH_INT -1
#define TOUCH_RST -1

Touch_GT911 touch(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, 480, 480);

// Previous touch position (for clearing old dot)
int prev_x = -1, prev_y = -1;

void drawStatusBar() {
    gfx->fillRect(0, 0, 480, 40, 0x1082);  // Dark blue bar
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, 12);
    gfx->print("V2: Touch Test");
    gfx->setTextColor(GREEN);
    gfx->setCursor(280, 12);
    gfx->print("Touch the screen");
}

void drawCoordinateBox(int x, int y) {
    // Clear the coordinate display area
    gfx->fillRect(0, 430, 480, 50, BLACK);

    // Show coordinates
    gfx->setTextSize(2);
    gfx->setTextColor(YELLOW);
    gfx->setCursor(20, 445);

    char buf[40];
    snprintf(buf, sizeof(buf), "X: %3d   Y: %3d", x, y);
    gfx->print(buf);

    // Show touch count
    gfx->setTextColor(CYAN);
    gfx->setCursor(300, 445);
    gfx->print("TOUCHED");
}

void setup() {
    Serial.begin(115200);
    Serial.println("V2: Touch Test - Starting...");

    // Init display
    gfx->begin(16000000);
    gfx->fillScreen(BLACK);

    // Backlight on
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    // Init touch
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    touch.begin();
    touch.setRotation(ROTATION_NORMAL);

    // Draw initial UI
    drawStatusBar();

    // Draw crosshair guidelines
    gfx->drawFastHLine(0, 240, 480, 0x2104);  // Faint horizontal center
    gfx->drawFastVLine(240, 40, 390, 0x2104);  // Faint vertical center

    // Instructions
    gfx->setTextSize(2);
    gfx->setTextColor(0x7BEF);  // Gray
    gfx->setCursor(100, 230);
    gfx->print("Touch anywhere...");

    Serial.println("V2: Touch Test - Ready!");
}

void loop() {
    touch.read();

    if (touch.isTouched) {
        // Map coordinates (touch is inverted on this panel)
        int x = map(touch.points[0].x, 480, 0, 0, 479);
        int y = map(touch.points[0].y, 480, 0, 0, 479);

        Serial.printf("Touch: x=%d, y=%d\n", x, y);

        // Clear previous dot
        if (prev_x >= 0) {
            gfx->fillCircle(prev_x, prev_y, 8, BLACK);
        }

        // Draw new dot at touch position
        gfx->fillCircle(x, y, 8, RED);
        gfx->drawCircle(x, y, 8, WHITE);

        // Update coordinate display
        drawCoordinateBox(x, y);

        prev_x = x;
        prev_y = y;
    }

    delay(20);  // ~50Hz polling
}
