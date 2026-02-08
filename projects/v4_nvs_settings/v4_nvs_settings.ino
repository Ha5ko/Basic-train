/*
 * ESP32-4848S040 - V4: Persistent Settings (NVS)
 *
 * Saves WiFi SSID, password, and API key to ESP32 flash (NVS).
 * On boot, loads saved settings and connects to WiFi.
 * Touch the "RESET SETTINGS" button to clear saved data.
 *
 * For first test: edit INITIAL_SSID, INITIAL_PASS, INITIAL_API_KEY
 * below. They will be saved to NVS on first boot, then loaded
 * from flash on subsequent boots (survives power cycles).
 *
 * Required Libraries:
 *   - Arduino_GFX-master  (from manufacturer)
 *   - Touch_GT911         (from manufacturer)
 *
 * Board Settings: same as V1/V2/V3
 */

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <Touch_GT911.h>
#include <WiFi.h>
#include <Preferences.h>  // ESP32 NVS library (built-in)

// ============================================================
// >>> INITIAL VALUES - only used on very first boot <<<
// >>> After first boot, values are loaded from NVS <<<
// ============================================================
#define INITIAL_SSID    "YOUR_WIFI_SSID"
#define INITIAL_PASS    "YOUR_WIFI_PASSWORD"
#define INITIAL_API_KEY "YOUR_DARWIN_API_KEY"
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

// ----- Touch Setup -----
Touch_GT911 touch(19, 45, -1, -1, 480, 480);

// ----- NVS Storage -----
Preferences prefs;

// Settings stored in NVS
String saved_ssid;
String saved_pass;
String saved_api_key;
bool settings_exist = false;

// UI state
bool is_connected = false;

// Reset button bounds
#define BTN_X 140
#define BTN_Y 400
#define BTN_W 200
#define BTN_H 50

// ----- NVS Functions -----

void saveSettings(const char *ssid, const char *pass, const char *apiKey) {
    prefs.begin("trainboard", false);  // namespace "trainboard", read-write
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.putString("apikey", apiKey);
    prefs.putBool("configured", true);
    prefs.end();
    Serial.println("Settings saved to NVS");
}

bool loadSettings() {
    prefs.begin("trainboard", true);  // read-only
    bool configured = prefs.getBool("configured", false);
    if (configured) {
        saved_ssid = prefs.getString("ssid", "");
        saved_pass = prefs.getString("pass", "");
        saved_api_key = prefs.getString("apikey", "");
    }
    prefs.end();
    return configured;
}

void clearSettings() {
    prefs.begin("trainboard", false);
    prefs.clear();
    prefs.end();
    Serial.println("Settings cleared from NVS");
}

// ----- Display Functions -----

void drawHeader() {
    gfx->fillRect(0, 0, 480, 40, 0x1082);
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, 12);
    gfx->print("V4: NVS Persistent Settings");
}

void drawField(int y, const char *label, const char *value, uint16_t color) {
    gfx->fillRect(0, y, 480, 28, BLACK);
    gfx->setTextSize(2);
    gfx->setTextColor(0x7BEF);
    gfx->setCursor(20, y + 4);
    gfx->print(label);
    gfx->setTextColor(color);
    gfx->setCursor(180, y + 4);
    gfx->print(value);
}

void drawStatus(const char *msg, uint16_t color) {
    gfx->fillRect(0, 55, 480, 30, BLACK);
    gfx->setTextSize(2);
    gfx->setTextColor(color);
    gfx->setCursor(20, 60);
    gfx->print(msg);
}

void drawResetButton() {
    // Draw button
    gfx->fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, RED);
    gfx->drawRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, WHITE);
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(BTN_X + 15, BTN_Y + 17);
    gfx->print("RESET SETTINGS");
}

// Mask a string for display (show first 3 chars, rest as *)
String maskString(const String &s) {
    if (s.length() <= 3) return s;
    String masked = s.substring(0, 3);
    for (int i = 3; i < s.length() && i < 20; i++) masked += '*';
    return masked;
}

void drawSettingsInfo() {
    // Source
    const char *source = settings_exist ? "Loaded from NVS (flash)" : "Using initial values";
    uint16_t source_color = settings_exist ? GREEN : YELLOW;
    drawField(100, "Source:", source, source_color);

    // SSID
    drawField(140, "SSID:", saved_ssid.c_str(), WHITE);

    // Password (masked)
    String masked_pass = maskString(saved_pass);
    drawField(180, "Password:", masked_pass.c_str(), 0x7BEF);

    // API Key (masked)
    String masked_key = maskString(saved_api_key);
    drawField(220, "API Key:", masked_key.c_str(), 0x7BEF);
}

void drawWifiStatus() {
    if (WiFi.isConnected()) {
        drawStatus("WiFi Connected!", GREEN);

        char buf[64];
        snprintf(buf, sizeof(buf), "%s", WiFi.localIP().toString().c_str());
        drawField(280, "IP:", buf, GREEN);

        int rssi = WiFi.RSSI();
        const char *quality;
        uint16_t color;
        if (rssi > -50)      { quality = "Excellent"; color = GREEN; }
        else if (rssi > -60) { quality = "Good";      color = GREEN; }
        else if (rssi > -70) { quality = "Fair";      color = YELLOW; }
        else                 { quality = "Weak";      color = RED; }
        snprintf(buf, sizeof(buf), "%d dBm (%s)", rssi, quality);
        drawField(320, "Signal:", buf, color);

        // Confirmation that NVS works
        drawField(360, "NVS:", "Settings persist across reboots!", GREEN);
    } else {
        drawStatus("WiFi Connection Failed", RED);
        drawField(280, "Check:", "SSID and password", YELLOW);
    }
}

void connectWifi() {
    drawStatus("Connecting to WiFi...", YELLOW);

    WiFi.mode(WIFI_STA);
    WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
    WiFi.setAutoReconnect(true);

    Serial.printf("Connecting to '%s'...\n", saved_ssid.c_str());

    int attempts = 0;
    while (!WiFi.isConnected() && attempts < 40) {
        delay(500);
        Serial.print(".");
        // Progress dots
        int dotX = 20 + (attempts % 20) * 22;
        gfx->fillCircle(dotX, 270, 4, (attempts % 2) ? YELLOW : 0x4208);
        attempts++;
    }
    gfx->fillRect(0, 260, 480, 20, BLACK);

    Serial.println();
    is_connected = WiFi.isConnected();
}

// ----- Main -----

void setup() {
    Serial.begin(115200);
    Serial.println("V4: NVS Settings - Starting...");

    // Init display
    gfx->begin(16000000);
    gfx->fillScreen(BLACK);
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    // Init touch
    Wire.begin(19, 45);
    touch.begin();
    touch.setRotation(ROTATION_NORMAL);

    drawHeader();

    // Try to load settings from NVS
    settings_exist = loadSettings();

    if (settings_exist) {
        Serial.println("Settings loaded from NVS:");
        Serial.printf("  SSID: %s\n", saved_ssid.c_str());
        Serial.printf("  API Key: %s...\n", saved_api_key.substring(0, 8).c_str());
    } else {
        Serial.println("No saved settings found. Using initial values.");
        saved_ssid = INITIAL_SSID;
        saved_pass = INITIAL_PASS;
        saved_api_key = INITIAL_API_KEY;

        // Save initial values to NVS
        saveSettings(saved_ssid.c_str(), saved_pass.c_str(), saved_api_key.c_str());
        settings_exist = true;  // Now they exist
    }

    // Show settings info
    drawSettingsInfo();

    // Connect to WiFi
    connectWifi();
    drawWifiStatus();

    // Draw reset button
    drawResetButton();

    Serial.println("V4: Ready! Touch RESET SETTINGS to clear NVS.");
    Serial.println("Power cycle the device to test NVS persistence.");
}

void loop() {
    touch.read();

    if (touch.isTouched) {
        int x = map(touch.points[0].x, 480, 0, 0, 479);
        int y = map(touch.points[0].y, 480, 0, 0, 479);

        // Check if reset button was pressed
        if (x >= BTN_X && x <= BTN_X + BTN_W &&
            y >= BTN_Y && y <= BTN_Y + BTN_H) {

            Serial.println("Reset button pressed!");

            // Visual feedback
            gfx->fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, 0x7800);
            delay(200);

            // Clear NVS
            clearSettings();

            // Update display
            gfx->fillRect(0, 55, 480, 350, BLACK);
            drawStatus("Settings CLEARED!", YELLOW);
            drawField(140, "Action:", "NVS wiped", RED);
            drawField(180, "Next:", "Reboot to use INITIAL values", YELLOW);
            drawField(220, "Or:", "Upload new code with new values", 0x7BEF);

            // Redraw button
            drawResetButton();

            // Debounce
            delay(1000);
        }
    }

    delay(50);
}
