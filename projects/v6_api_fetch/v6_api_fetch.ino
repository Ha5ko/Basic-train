/*
 * ESP32-4848S040 - V6: Fetch Train Data
 *
 * Builds on V4 (NVS settings) to fetch real-time train departure data
 * from the Rail Data Marketplace (Darwin) REST API.
 *
 * Fetches departures for both directions:
 *   - LUT (Luton) -> ZFD (Farringdon)
 *   - ZFD (Farringdon) -> LUT (Luton)
 *
 * Displays fetch status on screen, prints raw JSON to Serial Monitor
 * for verification that data is flowing correctly.
 *
 * API: Rail Data Marketplace - Live Departure Board
 *   URL: https://api1.raildata.org.uk/1010-live-departure-board-dep/LDBWS/api/20220120/GetDepBoardWithDetails/{CRS}
 *   Auth: x-apikey header
 *
 * On first boot: prompts for WiFi SSID, password, API key via Serial
 * On subsequent boots: loads from NVS flash automatically
 *
 * Required Libraries:
 *   - Arduino_GFX-master  (from manufacturer)
 *   - Touch_GT911         (from manufacturer)
 *
 * Board Settings: same as V1/V2/V4
 */

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <Touch_GT911.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>

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

// ----- API Configuration -----
const char *API_HOST = "api1.raildata.org.uk";
const char *API_BASE = "/1010-live-departure-board-dep1_2/LDBWS/api/20220120/GetDepBoardWithDetails";

// Station CRS codes
const char *STATION_LUT = "LUT";  // Luton
const char *STATION_ZFD = "ZFD";  // Farringdon

// Button bounds
#define BTN_FETCH_X 20
#define BTN_FETCH_Y 410
#define BTN_FETCH_W 200
#define BTN_FETCH_H 50

#define BTN_RESET_X 260
#define BTN_RESET_Y 410
#define BTN_RESET_W 200
#define BTN_RESET_H 50

// ----- NVS Functions (from V4) -----

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

// ----- Serial Input -----

String readSerialLine() {
    String input = "";
    while (true) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (input.length() > 0) return input;
                continue;
            }
            input += c;
        }
        delay(10);
    }
}

// ----- Display Functions -----

void drawHeader() {
    gfx->fillRect(0, 0, 480, 40, 0x1082);
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, 12);
    gfx->print("V6: Train API Fetch Test");
}

void drawStatus(int y, const char *msg, uint16_t color) {
    gfx->fillRect(0, y, 480, 24, BLACK);
    gfx->setTextSize(2);
    gfx->setTextColor(color);
    gfx->setCursor(20, y + 4);
    gfx->print(msg);
}

void drawFetchButton() {
    gfx->fillRoundRect(BTN_FETCH_X, BTN_FETCH_Y, BTN_FETCH_W, BTN_FETCH_H, 8, 0x04AF);
    gfx->drawRoundRect(BTN_FETCH_X, BTN_FETCH_Y, BTN_FETCH_W, BTN_FETCH_H, 8, WHITE);
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(BTN_FETCH_X + 30, BTN_FETCH_Y + 17);
    gfx->print("FETCH DATA");
}

void drawResetButton() {
    gfx->fillRoundRect(BTN_RESET_X, BTN_RESET_Y, BTN_RESET_W, BTN_RESET_H, 8, RED);
    gfx->drawRoundRect(BTN_RESET_X, BTN_RESET_Y, BTN_RESET_W, BTN_RESET_H, 8, WHITE);
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(BTN_RESET_X + 15, BTN_RESET_Y + 17);
    gfx->print("RESET SETTINGS");
}

String maskString(const String &s) {
    if (s.length() <= 3) return s;
    String masked = s.substring(0, 3);
    for (unsigned int i = 3; i < s.length() && i < 20; i++) masked += '*';
    return masked;
}

// ----- Serial Setup (from V4) -----

void runSerialSetup() {
    drawStatus(55, "Setup required - open Serial Monitor", YELLOW);

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

    Serial.println();
    Serial.println("=== ESP32 Train Board Setup ===");
    Serial.println();
    Serial.println("Step 1/3: Enter your WiFi SSID:");
    saved_ssid = readSerialLine();
    Serial.printf("  SSID set to: %s\n", saved_ssid.c_str());

    Serial.println();
    Serial.println("Step 2/3: Enter your WiFi password:");
    saved_pass = readSerialLine();
    Serial.println("  Password set.");

    Serial.println();
    Serial.println("Step 3/3: Enter your Darwin API key:");
    saved_api_key = readSerialLine();
    Serial.println("  API key set.");

    saveSettings(saved_ssid, saved_pass, saved_api_key);

    Serial.println();
    Serial.println("All settings saved to flash!");
    settings_exist = true;
}

// ----- WiFi Connection -----

bool connectWifi() {
    drawStatus(55, "Connecting to WiFi...", YELLOW);
    Serial.printf("Connecting to '%s'...\n", saved_ssid.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
    WiFi.setAutoReconnect(true);

    int attempts = 0;
    while (!WiFi.isConnected() && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.isConnected()) {
        char buf[80];
        snprintf(buf, sizeof(buf), "WiFi OK - IP: %s", WiFi.localIP().toString().c_str());
        drawStatus(55, buf, GREEN);
        Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    } else {
        drawStatus(55, "WiFi FAILED - touch RESET to re-enter", RED);
        Serial.println("WiFi connection failed!");
        return false;
    }
}

// ----- API Fetch -----

String buildApiUrl(const char *fromCrs, const char *toCrs) {
    String url = "https://";
    url += API_HOST;
    url += API_BASE;
    url += "/";
    url += fromCrs;
    url += "?filterCrs=";
    url += toCrs;
    url += "&filterType=to&numRows=10";
    return url;
}

bool fetchTrainData(const char *fromCrs, const char *toCrs, const char *label) {
    String url = buildApiUrl(fromCrs, toCrs);

    char statusBuf[80];
    snprintf(statusBuf, sizeof(statusBuf), "Fetching %s -> %s ...", fromCrs, toCrs);
    drawStatus(85, statusBuf, YELLOW);
    Serial.println();
    Serial.println("========================================");
    Serial.printf("Fetching: %s -> %s\n", fromCrs, toCrs);
    Serial.printf("URL: %s\n", url.c_str());
    Serial.println("========================================");

    WiFiClientSecure client;
    client.setInsecure();  // Skip cert verification for now

    HTTPClient http;
    http.begin(client, url);
    http.addHeader("x-apikey", saved_api_key.c_str());
    http.setTimeout(15000);

    int httpCode = http.GET();

    Serial.printf("HTTP Response Code: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();

        snprintf(statusBuf, sizeof(statusBuf), "%s -> %s : OK (%d bytes)", fromCrs, toCrs, payload.length());
        drawStatus(85, statusBuf, GREEN);

        // Print first 2000 chars to Serial (may be truncated for very long responses)
        Serial.println("--- Response Body ---");
        if (payload.length() > 2000) {
            Serial.println(payload.substring(0, 2000));
            Serial.printf("... [truncated, total %d bytes]\n", payload.length());
        } else {
            Serial.println(payload);
        }
        Serial.println("--- End Response ---");

        http.end();
        return true;
    } else {
        String errorBody = http.getString();
        snprintf(statusBuf, sizeof(statusBuf), "%s -> %s : FAILED (HTTP %d)", fromCrs, toCrs, httpCode);
        drawStatus(85, statusBuf, RED);

        Serial.printf("HTTP Error: %d\n", httpCode);
        Serial.println("Error body:");
        Serial.println(errorBody.substring(0, 500));

        http.end();
        return false;
    }
}

void doFetchBothDirections() {
    drawStatus(85, "Starting API fetch...", CYAN);

    // Clear results area
    gfx->fillRect(0, 110, 480, 290, BLACK);

    unsigned long startTime = millis();

    // Fetch LUT -> ZFD
    bool ok1 = fetchTrainData(STATION_LUT, STATION_ZFD, "Luton -> Farringdon");

    // Show result on screen
    int y = 120;
    gfx->setTextSize(2);
    if (ok1) {
        gfx->setTextColor(GREEN);
        gfx->setCursor(20, y);
        gfx->print("LUT -> ZFD: OK");
    } else {
        gfx->setTextColor(RED);
        gfx->setCursor(20, y);
        gfx->print("LUT -> ZFD: FAILED");
    }

    delay(500);  // Brief pause between requests

    // Fetch ZFD -> LUT
    bool ok2 = fetchTrainData(STATION_ZFD, STATION_LUT, "Farringdon -> Luton");

    y = 150;
    if (ok2) {
        gfx->setTextColor(GREEN);
        gfx->setCursor(20, y);
        gfx->print("ZFD -> LUT: OK");
    } else {
        gfx->setTextColor(RED);
        gfx->setCursor(20, y);
        gfx->print("ZFD -> LUT: FAILED");
    }

    unsigned long elapsed = millis() - startTime;

    // Summary
    y = 200;
    gfx->setTextColor(WHITE);
    gfx->setCursor(20, y);
    char buf[80];
    snprintf(buf, sizeof(buf), "Fetch took: %lu ms", elapsed);
    gfx->print(buf);

    if (ok1 && ok2) {
        drawStatus(85, "Both fetches successful!", GREEN);
        gfx->setTextColor(GREEN);
        gfx->setCursor(20, 240);
        gfx->print("Check Serial Monitor for");
        gfx->setCursor(20, 265);
        gfx->print("raw JSON responses.");
    } else if (!ok1 && !ok2) {
        drawStatus(85, "Both fetches FAILED", RED);
        gfx->setTextColor(YELLOW);
        gfx->setCursor(20, 240);
        gfx->print("Check API key & Serial");
        gfx->setCursor(20, 265);
        gfx->print("Monitor for error details.");
    } else {
        drawStatus(85, "Partial success - check Serial", YELLOW);
    }

    gfx->setTextColor(0x7BEF);
    gfx->setCursor(20, 310);
    gfx->print("Touch FETCH to retry");

    Serial.println();
    Serial.printf("Total fetch time: %lu ms\n", elapsed);
    Serial.println("Touch FETCH DATA on screen to retry.");
}

// ----- Main -----

void setup() {
    Serial.begin(115200);
    Serial.println("V6: API Fetch Test - Starting...");

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

    // Load settings from NVS
    settings_exist = loadSettings();

    if (settings_exist && saved_ssid.length() > 0) {
        Serial.println("Settings loaded from NVS.");
        Serial.printf("  SSID: %s\n", saved_ssid.c_str());
        Serial.printf("  API Key: %s...\n", saved_api_key.substring(0, 8).c_str());
    } else {
        settings_exist = false;
        runSerialSetup();
    }

    // Connect to WiFi
    bool wifiOk = connectWifi();

    if (wifiOk) {
        // Auto-fetch on boot
        drawFetchButton();
        drawResetButton();
        doFetchBothDirections();
        drawFetchButton();
        drawResetButton();
    } else {
        drawResetButton();
    }

    Serial.println("V6: Ready!");
}

void loop() {
    touch.read();

    if (touch.isTouched) {
        int x = map(touch.points[0].x, 480, 0, 0, 479);
        int y = map(touch.points[0].y, 480, 0, 0, 479);

        // Check FETCH button
        if (x >= BTN_FETCH_X && x <= BTN_FETCH_X + BTN_FETCH_W &&
            y >= BTN_FETCH_Y && y <= BTN_FETCH_Y + BTN_FETCH_H) {

            // Visual feedback
            gfx->fillRoundRect(BTN_FETCH_X, BTN_FETCH_Y, BTN_FETCH_W, BTN_FETCH_H, 8, 0x0347);
            delay(200);

            if (WiFi.isConnected()) {
                doFetchBothDirections();
            } else {
                drawStatus(85, "WiFi not connected!", RED);
            }

            drawFetchButton();
            drawResetButton();

            // Wait for touch release
            while (true) {
                touch.read();
                if (!touch.isTouched) break;
                delay(50);
            }
        }

        // Check RESET button
        if (x >= BTN_RESET_X && x <= BTN_RESET_X + BTN_RESET_W &&
            y >= BTN_RESET_Y && y <= BTN_RESET_Y + BTN_RESET_H) {

            Serial.println("Reset button pressed!");
            gfx->fillRoundRect(BTN_RESET_X, BTN_RESET_Y, BTN_RESET_W, BTN_RESET_H, 8, 0x7800);
            delay(200);

            clearSettings();
            settings_exist = false;

            gfx->fillRect(0, 55, 480, 350, BLACK);
            drawStatus(55, "Settings CLEARED!", YELLOW);
            drawStatus(150, "Rebooting in 3s...", YELLOW);

            delay(3000);
            ESP.restart();
        }
    }

    delay(50);
}
