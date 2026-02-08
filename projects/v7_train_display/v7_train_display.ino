/*
 * ESP32-4848S040 - V7: Train Departure Board Display
 *
 * Split-screen departure board showing real-time train times:
 *   Left half:  LUT (Luton) -> ZFD (Farringdon)
 *   Right half: ZFD (Farringdon) -> LUT (Luton)
 *
 * Parses JSON from Rail Data Marketplace Darwin API.
 * Shows: scheduled time, platform, status (on time/delayed/cancelled)
 * Color coded: green=on time, yellow=delayed, red=cancelled
 *
 * Touch bottom of screen to refresh. Touch "R" to reset settings.
 *
 * Required Libraries:
 *   - Arduino_GFX-master  (from manufacturer)
 *   - Touch_GT911         (from manufacturer)
 *   - ArduinoJson         (from manufacturer libraries)
 */

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <Touch_GT911.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>

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

String saved_ssid;
String saved_pass;
String saved_api_key;
bool settings_exist = false;

// ----- API Configuration -----
const char *API_HOST = "api1.raildata.org.uk";
const char *API_BASE = "/1010-live-departure-board-dep1_2/LDBWS/api/20220120/GetDepBoardWithDetails";

const char *STATION_LUT = "LUT";
const char *STATION_ZFD = "ZFD";

// ----- Display Colors -----
#define COL_BG          BLACK
#define COL_HEADER_BG   0x000A   // Very dark blue
#define COL_HEADER_FG   WHITE
#define COL_COLHDR_BG   0x2104   // Dark gray
#define COL_COLHDR_FG   0xC618   // Light gray
#define COL_ON_TIME     0x07E0   // Green
#define COL_DELAYED     0xFFE0   // Yellow
#define COL_CANCELLED   0xF800   // Red
#define COL_TIME_FG     WHITE
#define COL_PLAT_FG     0xBDF7   // Light blue-gray
#define COL_DIVIDER     0x4208   // Medium gray
#define COL_STATUS_BG   0x0841   // Very dark gray
#define COL_REFRESH_BG  0x04AF   // Blue
#define COL_RESET_BG    0x7800   // Dark red

// ----- Layout Constants -----
#define SCREEN_W       480
#define SCREEN_H       480
#define HALF_W         240
#define HEADER_H       36
#define COLHDR_Y       HEADER_H
#define COLHDR_H       20
#define DATA_Y         (HEADER_H + COLHDR_H)
#define ROW_H          22
#define BOTTOM_BAR_H   50
#define BOTTOM_BAR_Y   (SCREEN_H - BOTTOM_BAR_H)
#define DATA_ROWS      ((BOTTOM_BAR_Y - DATA_Y) / ROW_H)

// Button areas in bottom bar
#define BTN_REFRESH_X  10
#define BTN_REFRESH_Y  BOTTOM_BAR_Y
#define BTN_REFRESH_W  380
#define BTN_REFRESH_H  BOTTOM_BAR_H

#define BTN_RESET_X    400
#define BTN_RESET_Y    BOTTOM_BAR_Y
#define BTN_RESET_W    70
#define BTN_RESET_H    BOTTOM_BAR_H

// ----- Train Data Structures -----
#define MAX_TRAINS 10

struct TrainInfo {
    char std[6];        // "HH:MM"
    char etd[16];       // "On time", "HH:MM", "Cancelled", "Delayed"
    char platform[4];   // "1", "15", "A"
    bool isCancelled;
    char delayReason[80];
    char cancelReason[80];
};

struct BoardData {
    TrainInfo trains[MAX_TRAINS];
    int count;
    char stationName[32];
    char updatedTime[6];  // "HH:MM" from generatedAt
    bool success;
};

BoardData boardLeft;   // LUT -> ZFD
BoardData boardRight;  // ZFD -> LUT

// ----- NVS Functions -----

void saveSettings(const String &ssid, const String &pass, const String &apiKey) {
    prefs.begin("trainboard", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.putString("apikey", apiKey);
    prefs.putBool("configured", true);
    prefs.end();
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

// ----- Serial Setup -----

void runSerialSetup() {
    gfx->fillScreen(COL_BG);
    gfx->setTextSize(2);
    gfx->setTextColor(YELLOW);
    gfx->setCursor(20, 40);
    gfx->print("Setup required");

    gfx->setTextColor(WHITE);
    gfx->setCursor(20, 80);
    gfx->print("Open Serial Monitor");
    gfx->setCursor(20, 105);
    gfx->print("(115200, Newline ending)");

    gfx->setTextColor(CYAN);
    gfx->setCursor(20, 150);
    gfx->print("Waiting for input...");

    Serial.println("\n=== ESP32 Train Board Setup ===\n");
    Serial.println("Step 1/3: Enter your WiFi SSID:");
    saved_ssid = readSerialLine();
    Serial.printf("  SSID: %s\n", saved_ssid.c_str());

    Serial.println("\nStep 2/3: Enter your WiFi password:");
    saved_pass = readSerialLine();
    Serial.println("  Password set.");

    Serial.println("\nStep 3/3: Enter your Darwin API key:");
    saved_api_key = readSerialLine();
    Serial.println("  API key set.");

    saveSettings(saved_ssid, saved_pass, saved_api_key);
    Serial.println("\nSettings saved to flash!");
    settings_exist = true;
}

// ----- WiFi Connection -----

bool connectWifi() {
    gfx->fillScreen(COL_BG);
    gfx->setTextSize(2);
    gfx->setTextColor(YELLOW);
    gfx->setCursor(20, 220);
    gfx->print("Connecting to WiFi...");

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
        Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    } else {
        Serial.println("WiFi connection failed!");
        return false;
    }
}

// ----- API Fetch & Parse -----

String buildApiUrl(const char *fromCrs, const char *toCrs) {
    String url = "https://";
    url += API_HOST;
    url += API_BASE;
    url += "/";
    url += fromCrs;
    url += "?filterCrs=";
    url += toCrs;
    url += "&filterType=to&numRows=10&timeWindow=120";
    return url;
}

bool fetchAndParse(const char *fromCrs, const char *toCrs, BoardData &board) {
    board.success = false;
    board.count = 0;
    memset(board.updatedTime, 0, sizeof(board.updatedTime));
    memset(board.stationName, 0, sizeof(board.stationName));

    String url = buildApiUrl(fromCrs, toCrs);
    Serial.printf("Fetching: %s -> %s\n", fromCrs, toCrs);
    Serial.printf("URL: %s\n", url.c_str());

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    http.addHeader("x-apikey", saved_api_key.c_str());
    http.setTimeout(15000);

    int httpCode = http.GET();
    Serial.printf("HTTP %d\n", httpCode);

    if (httpCode != HTTP_CODE_OK) {
        String err = http.getString();
        Serial.printf("Error: %s\n", err.substring(0, 200).c_str());
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    Serial.printf("Response: %d bytes\n", payload.length());

    // Parse JSON - allocate from heap (PSRAM)
    DynamicJsonDocument doc(32768);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        Serial.printf("JSON parse error: %s\n", error.c_str());
        return false;
    }

    // Extract station name
    strlcpy(board.stationName, doc["locationName"] | fromCrs, sizeof(board.stationName));

    // Extract update time from generatedAt (e.g. "2026-02-08T19:12:56.04...")
    const char *genAt = doc["generatedAt"] | "";
    if (strlen(genAt) >= 16) {
        // Extract "HH:MM" from "YYYY-MM-DDTHH:MM:SS..."
        board.updatedTime[0] = genAt[11];
        board.updatedTime[1] = genAt[12];
        board.updatedTime[2] = ':';
        board.updatedTime[3] = genAt[14];
        board.updatedTime[4] = genAt[15];
        board.updatedTime[5] = '\0';
    }

    // Parse train services
    JsonArray services = doc["trainServices"];
    if (services.isNull()) {
        Serial.println("No train services in response");
        board.success = true;  // Valid response, just no trains
        return true;
    }

    board.count = 0;
    for (JsonObject svc : services) {
        if (board.count >= MAX_TRAINS) break;

        TrainInfo &t = board.trains[board.count];
        strlcpy(t.std, svc["std"] | "--:--", sizeof(t.std));
        strlcpy(t.etd, svc["etd"] | "???", sizeof(t.etd));
        strlcpy(t.platform, svc["platform"] | "-", sizeof(t.platform));
        t.isCancelled = svc["isCancelled"] | false;
        strlcpy(t.cancelReason, svc["cancelReason"] | "", sizeof(t.cancelReason));
        strlcpy(t.delayReason, svc["delayReason"] | "", sizeof(t.delayReason));

        Serial.printf("  %s  Plt %s  %s%s\n",
            t.std, t.platform, t.etd,
            t.isCancelled ? " [CANCELLED]" : "");

        board.count++;
    }

    Serial.printf("Parsed %d services\n", board.count);
    board.success = true;
    return true;
}

// ----- Display Drawing -----

void drawBoardHeader() {
    // Header bar
    gfx->fillRect(0, 0, SCREEN_W, HEADER_H, COL_HEADER_BG);

    gfx->setTextSize(2);
    gfx->setTextColor(COL_HEADER_FG);

    // Left: LUT -> ZFD
    gfx->setCursor(15, 10);
    gfx->print("Luton");
    gfx->setTextColor(COL_DELAYED);
    gfx->print(" > ");
    gfx->setTextColor(COL_HEADER_FG);
    gfx->print("Farringdon");

    // Right: ZFD -> LUT
    gfx->setCursor(HALF_W + 15, 10);
    gfx->print("Farringdon");
    gfx->setTextColor(COL_DELAYED);
    gfx->print(" > ");
    gfx->setTextColor(COL_HEADER_FG);
    gfx->print("Luton");

    // Divider line in header
    gfx->drawFastVLine(HALF_W - 1, 0, HEADER_H, COL_DIVIDER);
    gfx->drawFastVLine(HALF_W, 0, HEADER_H, COL_DIVIDER);
}

void drawColumnHeaders() {
    for (int side = 0; side < 2; side++) {
        int xOff = side * HALF_W;
        gfx->fillRect(xOff, COLHDR_Y, HALF_W, COLHDR_H, COL_COLHDR_BG);

        gfx->setTextSize(1);
        gfx->setTextColor(COL_COLHDR_FG);

        gfx->setCursor(xOff + 8, COLHDR_Y + 6);
        gfx->print("TIME");

        gfx->setCursor(xOff + 62, COLHDR_Y + 6);
        gfx->print("PLT");

        gfx->setCursor(xOff + 100, COLHDR_Y + 6);
        gfx->print("STATUS");
    }

    // Divider
    gfx->drawFastVLine(HALF_W - 1, COLHDR_Y, COLHDR_H, COL_DIVIDER);
    gfx->drawFastVLine(HALF_W, COLHDR_Y, COLHDR_H, COL_DIVIDER);
}

void drawTrainRow(int side, int row, const TrainInfo &train) {
    int xOff = side * HALF_W;
    int y = DATA_Y + (row * ROW_H);

    // Alternate row background for readability
    uint16_t bgColor = (row % 2 == 0) ? COL_BG : COL_STATUS_BG;
    gfx->fillRect(xOff, y, HALF_W, ROW_H, bgColor);

    gfx->setTextSize(2);

    // Scheduled time - always white
    gfx->setTextColor(COL_TIME_FG);
    gfx->setCursor(xOff + 5, y + 3);
    gfx->print(train.std);

    // Platform
    gfx->setTextColor(COL_PLAT_FG);
    gfx->setCursor(xOff + 70, y + 3);
    gfx->print(train.platform);

    // Status (etd) - color coded
    uint16_t statusColor;
    if (train.isCancelled || strcmp(train.etd, "Cancelled") == 0) {
        statusColor = COL_CANCELLED;
    } else if (strcmp(train.etd, "On time") == 0) {
        statusColor = COL_ON_TIME;
    } else {
        statusColor = COL_DELAYED;  // Delayed or has a new time
    }

    gfx->setTextColor(statusColor);
    gfx->setCursor(xOff + 100, y + 3);

    // Truncate long status text to fit in half-width
    char statusBuf[12];
    if (train.isCancelled || strcmp(train.etd, "Cancelled") == 0) {
        strlcpy(statusBuf, "CANCLD", sizeof(statusBuf));
    } else {
        strlcpy(statusBuf, train.etd, sizeof(statusBuf));
    }
    gfx->print(statusBuf);
}

void drawNoTrains(int side) {
    int xOff = side * HALF_W;
    int y = DATA_Y + ROW_H;

    gfx->setTextSize(2);
    gfx->setTextColor(0x7BEF);
    gfx->setCursor(xOff + 15, y + 3);
    gfx->print("No trains");
}

void drawBoardData() {
    // Clear data area
    gfx->fillRect(0, DATA_Y, SCREEN_W, BOTTOM_BAR_Y - DATA_Y, COL_BG);

    // Divider line through data area
    gfx->drawFastVLine(HALF_W - 1, DATA_Y, BOTTOM_BAR_Y - DATA_Y, COL_DIVIDER);
    gfx->drawFastVLine(HALF_W, DATA_Y, BOTTOM_BAR_Y - DATA_Y, COL_DIVIDER);

    // Left side: LUT -> ZFD
    if (boardLeft.success) {
        if (boardLeft.count > 0) {
            int maxRows = min(boardLeft.count, (int)DATA_ROWS);
            for (int i = 0; i < maxRows; i++) {
                drawTrainRow(0, i, boardLeft.trains[i]);
            }
        } else {
            drawNoTrains(0);
        }
    } else {
        int y = DATA_Y + ROW_H;
        gfx->setTextSize(2);
        gfx->setTextColor(COL_CANCELLED);
        gfx->setCursor(15, y + 3);
        gfx->print("Fetch failed");
    }

    // Right side: ZFD -> LUT
    if (boardRight.success) {
        if (boardRight.count > 0) {
            int maxRows = min(boardRight.count, (int)DATA_ROWS);
            for (int i = 0; i < maxRows; i++) {
                drawTrainRow(1, i, boardRight.trains[i]);
            }
        } else {
            drawNoTrains(1);
        }
    } else {
        int y = DATA_Y + ROW_H;
        gfx->setTextSize(2);
        gfx->setTextColor(COL_CANCELLED);
        gfx->setCursor(HALF_W + 15, y + 3);
        gfx->print("Fetch failed");
    }
}

void drawBottomBar() {
    // Refresh button (wide)
    gfx->fillRect(BTN_REFRESH_X, BTN_REFRESH_Y, BTN_REFRESH_W, BTN_REFRESH_H, COL_REFRESH_BG);
    gfx->drawRect(BTN_REFRESH_X, BTN_REFRESH_Y, BTN_REFRESH_W, BTN_REFRESH_H, COL_DIVIDER);

    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);

    // Show update time if available
    if (boardLeft.updatedTime[0] != '\0') {
        char buf[32];
        snprintf(buf, sizeof(buf), "Updated %s", boardLeft.updatedTime);
        gfx->setCursor(BTN_REFRESH_X + 20, BTN_REFRESH_Y + 17);
        gfx->print(buf);

        gfx->setCursor(BTN_REFRESH_X + 230, BTN_REFRESH_Y + 17);
        gfx->print("REFRESH");
    } else {
        gfx->setCursor(BTN_REFRESH_X + 120, BTN_REFRESH_Y + 17);
        gfx->print("REFRESH");
    }

    // Reset button (small, right side)
    gfx->fillRect(BTN_RESET_X, BTN_RESET_Y, BTN_RESET_W, BTN_RESET_H, COL_RESET_BG);
    gfx->drawRect(BTN_RESET_X, BTN_RESET_Y, BTN_RESET_W, BTN_RESET_H, COL_DIVIDER);

    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(BTN_RESET_X + 17, BTN_RESET_Y + 17);
    gfx->print("R");
}

void drawLoadingScreen() {
    gfx->fillRect(0, DATA_Y, SCREEN_W, BOTTOM_BAR_Y - DATA_Y, COL_BG);
    gfx->setTextSize(2);
    gfx->setTextColor(YELLOW);
    gfx->setCursor(140, 230);
    gfx->print("Loading trains...");
}

// ----- Main Fetch & Display -----

void refreshBoard() {
    drawLoadingScreen();

    unsigned long startTime = millis();

    // Fetch both directions
    bool ok1 = fetchAndParse(STATION_LUT, STATION_ZFD, boardLeft);
    delay(300);
    bool ok2 = fetchAndParse(STATION_ZFD, STATION_LUT, boardRight);

    unsigned long elapsed = millis() - startTime;
    Serial.printf("Total fetch: %lu ms\n\n", elapsed);

    // Draw the board
    drawBoardData();
    drawBottomBar();
}

// ----- Main -----

void setup() {
    Serial.begin(115200);
    Serial.println("V7: Train Departure Board - Starting...");

    // Init display
    gfx->begin(16000000);
    gfx->fillScreen(COL_BG);
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    // Init touch
    Wire.begin(19, 45);
    touch.begin();
    touch.setRotation(ROTATION_NORMAL);

    // Load settings from NVS
    settings_exist = loadSettings();

    if (settings_exist && saved_ssid.length() > 0) {
        Serial.println("Settings loaded from NVS.");
        Serial.printf("  SSID: %s\n", saved_ssid.c_str());
    } else {
        settings_exist = false;
        runSerialSetup();
    }

    // Connect to WiFi
    bool wifiOk = connectWifi();

    if (!wifiOk) {
        gfx->fillScreen(COL_BG);
        gfx->setTextSize(2);
        gfx->setTextColor(COL_CANCELLED);
        gfx->setCursor(20, 200);
        gfx->print("WiFi connection failed!");
        gfx->setTextColor(YELLOW);
        gfx->setCursor(20, 240);
        gfx->print("Touch screen to reboot");

        // Wait for touch then reboot
        while (true) {
            touch.read();
            if (touch.isTouched) {
                delay(500);
                ESP.restart();
            }
            delay(50);
        }
    }

    // Draw static elements
    drawBoardHeader();
    drawColumnHeaders();
    drawBottomBar();

    // Initial fetch
    refreshBoard();

    Serial.println("V7: Ready!");
}

void loop() {
    touch.read();

    if (touch.isTouched) {
        int x = map(touch.points[0].x, 480, 0, 0, 479);
        int y = map(touch.points[0].y, 480, 0, 0, 479);

        // Check REFRESH button
        if (x >= BTN_REFRESH_X && x <= BTN_REFRESH_X + BTN_REFRESH_W &&
            y >= BTN_REFRESH_Y && y <= BTN_REFRESH_Y + BTN_REFRESH_H) {

            // Visual feedback
            gfx->fillRect(BTN_REFRESH_X, BTN_REFRESH_Y, BTN_REFRESH_W, BTN_REFRESH_H, 0x0347);
            delay(200);

            if (WiFi.isConnected()) {
                drawBoardHeader();
                drawColumnHeaders();
                refreshBoard();
            } else {
                gfx->setTextSize(2);
                gfx->setTextColor(COL_CANCELLED);
                gfx->setCursor(140, 230);
                gfx->print("WiFi disconnected!");
                drawBottomBar();
            }

            // Wait for release
            while (true) {
                touch.read();
                if (!touch.isTouched) break;
                delay(50);
            }
        }

        // Check RESET button
        if (x >= BTN_RESET_X && x <= BTN_RESET_X + BTN_RESET_W &&
            y >= BTN_RESET_Y && y <= BTN_RESET_Y + BTN_RESET_H) {

            Serial.println("Reset settings pressed");
            clearSettings();
            gfx->fillScreen(COL_BG);
            gfx->setTextSize(2);
            gfx->setTextColor(YELLOW);
            gfx->setCursor(100, 220);
            gfx->print("Settings cleared!");
            gfx->setCursor(100, 250);
            gfx->print("Rebooting...");
            delay(2000);
            ESP.restart();
        }
    }

    delay(50);
}
