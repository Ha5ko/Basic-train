/*
 * ESP32-4848S040 - V3: WiFi Connection
 *
 * Connects to WiFi with hardcoded credentials and displays
 * connection status, IP address, and signal strength on screen.
 *
 * >>> CHANGE THE SSID AND PASSWORD BELOW BEFORE UPLOADING <<<
 *
 * Required Libraries (in Arduino libraries folder):
 *   - Arduino_GFX-master  (from manufacturer)
 *
 * Board Settings: same as V1/V2
 */

#include <Arduino_GFX_Library.h>
#include <WiFi.h>

// ============================================================
// >>> CHANGE THESE TO YOUR WIFI CREDENTIALS <<<
// ============================================================
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASS = "YOUR_WIFI_PASSWORD";
// ============================================================

// ----- Display Setup -----
#define GFX_BL 38

Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    39, 48, 47, 18, 17, 16, 21,
    11, 12, 13, 14, 0,
    8, 20, 3, 46, 9, 10,
    4, 5, 6, 7, 15
);

Arduino_ST7701_RGBPanel *gfx = new Arduino_ST7701_RGBPanel(
    bus, GFX_NOT_DEFINED, 0, true, 480, 480,
    st7701_type1_init_operations, sizeof(st7701_type1_init_operations),
    true, 10, 8, 50, 10, 8, 20
);

// Track connection state for display updates
bool was_connected = false;

void drawHeader() {
    gfx->fillRect(0, 0, 480, 40, 0x1082);
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, 12);
    gfx->print("V3: WiFi Connection");
}

void drawStatus(const char *msg, uint16_t color) {
    gfx->fillRect(0, 60, 480, 30, BLACK);
    gfx->setTextSize(2);
    gfx->setTextColor(color);
    gfx->setCursor(20, 65);
    gfx->print(msg);
}

void drawField(int y, const char *label, const char *value, uint16_t color) {
    gfx->fillRect(0, y, 480, 28, BLACK);
    gfx->setTextSize(2);
    gfx->setTextColor(0x7BEF);  // Gray label
    gfx->setCursor(20, y + 4);
    gfx->print(label);
    gfx->setTextColor(color);
    gfx->setCursor(180, y + 4);
    gfx->print(value);
}

void drawConnectionInfo() {
    char buf[64];

    // SSID
    drawField(120, "SSID:", WIFI_SSID, WHITE);

    // IP Address
    snprintf(buf, sizeof(buf), "%s", WiFi.localIP().toString().c_str());
    drawField(160, "IP:", buf, GREEN);

    // Gateway
    snprintf(buf, sizeof(buf), "%s", WiFi.gatewayIP().toString().c_str());
    drawField(200, "Gateway:", buf, CYAN);

    // Signal strength
    int rssi = WiFi.RSSI();
    const char *quality;
    uint16_t color;
    if (rssi > -50)      { quality = "Excellent"; color = GREEN; }
    else if (rssi > -60) { quality = "Good";      color = GREEN; }
    else if (rssi > -70) { quality = "Fair";      color = YELLOW; }
    else                 { quality = "Weak";      color = RED; }

    snprintf(buf, sizeof(buf), "%d dBm (%s)", rssi, quality);
    drawField(240, "Signal:", buf, color);

    // MAC
    snprintf(buf, sizeof(buf), "%s", WiFi.macAddress().c_str());
    drawField(280, "MAC:", buf, 0x7BEF);

    // Channel
    snprintf(buf, sizeof(buf), "%d", WiFi.channel());
    drawField(320, "Channel:", buf, 0x7BEF);
}

void setup() {
    Serial.begin(115200);
    Serial.println("V3: WiFi Connection - Starting...");

    // Init display
    gfx->begin(16000000);
    gfx->fillScreen(BLACK);
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    drawHeader();
    drawStatus("Connecting to WiFi...", YELLOW);

    // Show which network we're connecting to
    drawField(120, "SSID:", WIFI_SSID, WHITE);

    // Animated connection attempt
    gfx->setTextSize(2);
    gfx->setTextColor(YELLOW);

    // Connect to WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    WiFi.setAutoReconnect(true);

    Serial.printf("Connecting to %s", WIFI_SSID);

    int attempts = 0;
    while (!WiFi.isConnected() && attempts < 40) {  // 20 second timeout
        delay(500);
        Serial.print(".");
        // Show progress dots on screen
        int dotX = 20 + (attempts % 20) * 22;
        gfx->fillCircle(dotX, 100, 4, (attempts % 2) ? YELLOW : 0x4208);
        attempts++;
    }

    // Clear progress dots
    gfx->fillRect(0, 90, 480, 25, BLACK);

    if (WiFi.isConnected()) {
        Serial.println("\nConnected!");
        Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
        drawStatus("Connected!", GREEN);
        drawConnectionInfo();
        was_connected = true;
    } else {
        Serial.println("\nConnection failed!");
        drawStatus("Connection FAILED!", RED);
        drawField(160, "Check:", "SSID & password in code", YELLOW);
        drawField(200, "Tip:", "Edit WIFI_SSID / WIFI_PASS", 0x7BEF);
    }
}

void loop() {
    bool connected = WiFi.isConnected();

    // Detect connection state changes
    if (connected && !was_connected) {
        Serial.println("WiFi reconnected!");
        drawStatus("Connected!", GREEN);
        drawConnectionInfo();
        was_connected = true;
    } else if (!connected && was_connected) {
        Serial.println("WiFi disconnected!");
        drawStatus("Disconnected! Reconnecting...", RED);
        was_connected = false;
    }

    // Update signal strength every 5 seconds when connected
    if (connected) {
        static unsigned long last_update = 0;
        if (millis() - last_update > 5000) {
            last_update = millis();
            int rssi = WiFi.RSSI();
            char buf[40];
            const char *quality;
            uint16_t color;
            if (rssi > -50)      { quality = "Excellent"; color = GREEN; }
            else if (rssi > -60) { quality = "Good";      color = GREEN; }
            else if (rssi > -70) { quality = "Fair";      color = YELLOW; }
            else                 { quality = "Weak";      color = RED; }
            snprintf(buf, sizeof(buf), "%d dBm (%s)", rssi, quality);
            drawField(240, "Signal:", buf, color);
        }
    }

    delay(100);
}
