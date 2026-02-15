/*
 * ESP32-4848S040 - V4: Persistent Settings (NVS)
 *
 * On first boot (no saved settings):
 *   - Screen prompts you to open Serial Monitor
 *   - You type your WiFi SSID, password, and API key via Serial
 *   - Device saves them to flash (NVS) and connects
 *
 * On subsequent boots:
 *   - Loads credentials from flash automatically
 *   - Connects to WiFi without asking
 *
 * Touch "RESET SETTINGS" button to clear saved data and re-enter.
 *
 * Serial input format (in Serial Monitor, set line ending to "Newline"):
 *   Step 1: type SSID, press Enter
 *   Step 2: type password, press Enter
 *   Step 3: type API key, press Enter
 *
 * Required Libraries:
 *   - Arduino_GFX-master  (from manufacturer)
 *   - Touch_GT911         (from manufacturer)
 *
 * Board Settings: same as V1/V2
 */

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <Touch_GT911.h>
#include <WiFi.h>
#include <Preferences.h>  // ESP32 NVS library (built-in)

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

// Settings
String saved_ssid;
String saved_pass;
String saved_api_key;
bool settings_exist = false;
bool is_connected = false;

// Reset button bounds
#define BTN_X 140
#define BTN_Y 410
#define BTN_W 200
#define BTN_H 50

// ----- NVS Functions -----

void saveSettings(const String &ssid, const String &pass, const String &apiKey) {
    prefs.begin("trainboard", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.putString("apikey", apiKey);
    prefs.putBool("configured", true);
    prefs.end();
    Serial.println("Settings saved to NVS!");
}

bool loadSettings() {
    prefs.begin("trainboard", true);
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
    Serial.println("Settings cleared from NVS!");
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
    gfx->fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, RED);
    gfx->drawRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, WHITE);
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(BTN_X + 15, BTN_Y + 17);
    gfx->print("RESET SETTINGS");
}

String maskString(const String &s) {
    if (s.length() <= 3) return s;
    String masked = s.substring(0, 3);
    for (unsigned int i = 3; i < s.length() && i < 20; i++) masked += '*';
    return masked;
}

// ----- Serial Input for Setup -----

String readSerialLine() {
    String input = "";
    while (true) {
        // Also check for touch reset during serial input
        touch.read();
        if (touch.isTouched) {
            int x = map(touch.points[0].x, 480, 0, 0, 479);
            int y = map(touch.points[0].y, 480, 0, 0, 479);
            if (x >= BTN_X && x <= BTN_X + BTN_W &&
                y >= BTN_Y && y <= BTN_Y + BTN_H) {
                // Ignore during setup
            }
        }

        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (input.length() > 0) {
                    return input;
                }
                // Skip empty lines (handles \r\n)
                continue;
            }
            input += c;
        }
        delay(10);
    }
}

void runSerialSetup() {
    drawStatus("Setup required - open Serial Monitor", YELLOW);

    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(20, 110);
    gfx->print("No saved settings found.");

    gfx->setTextColor(CYAN);
    gfx->setCursor(20, 150);
    gfx->print("Open Arduino Serial Monitor");
    gfx->setCursor(20, 175);
    gfx->print("(115200 baud, Newline ending)");

    gfx->setTextColor(YELLOW);
    gfx->setCursor(20, 215);
    gfx->print("Then follow the prompts to");
    gfx->setCursor(20, 240);
    gfx->print("enter your credentials.");

    gfx->setTextColor(0x7BEF);
    gfx->setCursor(20, 290);
    gfx->print("Waiting for Serial input...");

    // Prompt 1: SSID
    Serial.println();
    Serial.println("=== ESP32 Train Board Setup ===");
    Serial.println();
    Serial.println("Step 1/3: Enter your WiFi SSID:");
    saved_ssid = readSerialLine();
    Serial.printf("  SSID set to: %s\n", saved_ssid.c_str());
    drawField(330, "SSID:", saved_ssid.c_str(), GREEN);

    // Prompt 2: Password
    Serial.println();
    Serial.println("Step 2/3: Enter your WiFi password:");
    saved_pass = readSerialLine();
    Serial.println("  Password set.");
    drawField(360, "Password:", maskString(saved_pass).c_str(), GREEN);

    // Prompt 3: API Key
    Serial.println();
    Serial.println("Step 3/3: Enter your Darwin API key:");
    saved_api_key = readSerialLine();
    Serial.println("  API key set.");
    drawField(390, "API Key:", maskString(saved_api_key).c_str(), GREEN);

    // Save to NVS
    saveSettings(saved_ssid, saved_pass, saved_api_key);

    Serial.println();
    Serial.println("All settings saved to flash!");
    Serial.println("These will persist across power cycles.");
    Serial.println();

    settings_exist = true;
}

// ----- WiFi Connection -----

void connectWifi() {
    drawStatus("Connecting to WiFi...", YELLOW);
    Serial.printf("Connecting to '%s'...\n", saved_ssid.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
    WiFi.setAutoReconnect(true);

    int attempts = 0;
    while (!WiFi.isConnected() && attempts < 40) {
        delay(500);
        Serial.print(".");
        int dotX = 20 + (attempts % 20) * 22;
        gfx->fillCircle(dotX, 95, 4, (attempts % 2) ? YELLOW : 0x4208);
        attempts++;
    }
    gfx->fillRect(0, 88, 480, 16, BLACK);
    Serial.println();

    is_connected = WiFi.isConnected();
}

void drawConnectedScreen() {
    // Clear the setup area
    gfx->fillRect(0, 55, 480, 350, BLACK);

    if (is_connected) {
        drawStatus("WiFi Connected!", GREEN);

        char buf[64];

        drawField(110, "Source:", settings_exist ? "Loaded from NVS (flash)" : "Fresh setup", GREEN);
        drawField(150, "SSID:", saved_ssid.c_str(), WHITE);
        drawField(190, "Password:", maskString(saved_pass).c_str(), 0x7BEF);
        drawField(230, "API Key:", maskString(saved_api_key).c_str(), 0x7BEF);

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

        drawField(370, "NVS:", "Settings persist across reboots!", GREEN);
    } else {
        drawStatus("WiFi Connection FAILED", RED);
        drawField(110, "SSID:", saved_ssid.c_str(), RED);
        drawField(150, "Tip:", "Touch RESET to re-enter creds", YELLOW);
    }
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

    if (settings_exist && saved_ssid.length() > 0) {
        Serial.println("Settings loaded from NVS:");
        Serial.printf("  SSID: %s\n", saved_ssid.c_str());
        Serial.printf("  API Key: %s...\n", saved_api_key.substring(0, 8).c_str());
    } else {
        // No settings - run serial setup
        settings_exist = false;
        runSerialSetup();
    }

    // Connect to WiFi
    connectWifi();
    drawConnectedScreen();
    drawResetButton();

    Serial.println("V4: Ready!");
    Serial.println("Touch RESET SETTINGS to clear saved data and re-enter.");
    Serial.println("Power cycle to verify NVS persistence.");
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
            settings_exist = false;

            // Update display
            gfx->fillRect(0, 55, 480, 350, BLACK);
            drawStatus("Settings CLEARED!", YELLOW);
            drawField(150, "Action:", "NVS wiped", RED);
            drawField(190, "Next:", "Device will reboot in 3s...", YELLOW);

            delay(3000);
            ESP.restart();
        }
    }

    // Detect WiFi reconnect/disconnect
    static bool prev_connected = is_connected;
    bool now_connected = WiFi.isConnected();
    if (now_connected != prev_connected) {
        is_connected = now_connected;
        drawConnectedScreen();
        drawResetButton();
        prev_connected = now_connected;
    }

    delay(50);
}
