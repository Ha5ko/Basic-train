/*
 * ESP32-4848S040 - V9: Final Train Departure Board
 *
 * Split-screen departure board with auto-refresh:
 *   Left:  LUT (Luton) -> ZFD (Farringdon)
 *   Right: ZFD (Farringdon) -> LUT (Luton)
 *
 * Shows trailing 1 hour + next 2 hours of trains.
 * Columns: DEPART, PLT, ARRIVAL
 *   - ARRIVAL shows ETA at destination with delay: "19:45 +0" (green)
 *   - If late: "19:50 +5" (yellow)
 *   - If cancelled: "CANC" (red)
 *
 * Auto-refreshes every 60 seconds. NTP time sync.
 * Past trains shown dimmed. Touch bottom bar to refresh. "R" to reset.
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
#include <time.h>

// ===== Display Setup =====
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

// ===== Touch Setup =====
Touch_GT911 touch(19, 45, -1, -1, 480, 480);

// ===== NVS Storage =====
Preferences prefs;
String saved_ssid;
String saved_pass;
String saved_api_key;
bool settings_exist = false;

// ===== API Configuration =====
const char *API_HOST = "api1.raildata.org.uk";
const char *API_BASE = "/1010-live-departure-board-dep1_2/LDBWS/api/20220120/GetDepBoardWithDetails";
const char *STATION_LUT = "LUT";
const char *STATION_ZFD = "ZFD";

// ===== Display Colors =====
#define COL_BG          BLACK
#define COL_HEADER_BG   0x000A
#define COL_HEADER_FG   WHITE
#define COL_COLHDR_BG   0x2104
#define COL_COLHDR_FG   0xC618
#define COL_ON_TIME     0x07E0   // Green
#define COL_DELAYED     0xFFE0   // Yellow
#define COL_CANCELLED   0xF800   // Red
#define COL_TIME_FG     WHITE
#define COL_PLAT_FG     0xBDF7
#define COL_DIVIDER     0x4208
#define COL_ROW_ALT     0x0841
#define COL_PAST_FG     0x6B4D   // Dimmed for past trains
#define COL_REFRESH_BG  0x04AF
#define COL_RESET_BG    0x7800

// ===== Layout Constants =====
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

// Column X offsets within each half (text size 2 = 12px/char)
#define COL_DEPART_X   3
#define COL_PLT_X      66
#define COL_ARRIVAL_X  95

// Bottom bar buttons
#define BTN_REFRESH_X  0
#define BTN_REFRESH_Y  BOTTOM_BAR_Y
#define BTN_REFRESH_W  410
#define BTN_REFRESH_H  BOTTOM_BAR_H

#define BTN_RESET_X    414
#define BTN_RESET_Y    BOTTOM_BAR_Y
#define BTN_RESET_W    66
#define BTN_RESET_H    BOTTOM_BAR_H

// ===== Data Structures =====
#define MAX_TRAINS 20

struct TrainInfo {
    char serviceID[20];
    char std[6];         // Scheduled departure "HH:MM"
    char etd[16];        // Estimated departure: "On time", "HH:MM", "Cancelled"
    char platform[4];
    bool isCancelled;
    int  delayMins;      // 0=on time, >0=late mins, -1=cancelled, -2=unknown
    char arrTime[6];     // ETA at destination "HH:MM" (from calling points or fallback)
};

struct BoardData {
    TrainInfo trains[MAX_TRAINS];
    int count;
    char updatedTime[6]; // "HH:MM" from API generatedAt
    bool success;
};

BoardData boardLeft;   // LUT -> ZFD
BoardData boardRight;  // ZFD -> LUT

// ===== Auto-refresh =====
#define REFRESH_INTERVAL 60000  // 60 seconds
unsigned long lastRefreshTime = 0;
unsigned long lastCountdownDraw = 0;
bool ntpSynced = false;

// ===== Time Helpers =====

int timeToMinutes(const char *hhmm) {
    if (strlen(hhmm) < 5 || hhmm[2] != ':') return -1;
    int h = (hhmm[0] - '0') * 10 + (hhmm[1] - '0');
    int m = (hhmm[3] - '0') * 10 + (hhmm[4] - '0');
    return h * 60 + m;
}

int calcDelay(const char *scheduled, const char *estimated) {
    if (strcmp(estimated, "On time") == 0) return 0;
    if (strcmp(estimated, "Cancelled") == 0) return -1;
    if (strcmp(estimated, "Delayed") == 0) return -2;

    int sMins = timeToMinutes(scheduled);
    int eMins = timeToMinutes(estimated);
    if (sMins < 0 || eMins < 0) return -2;

    int diff = eMins - sMins;
    if (diff < -720) diff += 1440;
    if (diff > 720) diff -= 1440;
    return (diff > 0) ? diff : 0;
}

void setupNTP() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();

    struct tm ti;
    for (int i = 0; i < 10; i++) {
        if (getLocalTime(&ti, 500)) {
            ntpSynced = true;
            Serial.printf("NTP synced: %02d:%02d:%02d\n", ti.tm_hour, ti.tm_min, ti.tm_sec);
            return;
        }
        delay(500);
    }
    Serial.println("NTP sync failed - will retry");
}

String getCurrentTimeStr() {
    struct tm ti;
    if (!getLocalTime(&ti, 100)) return "??:??";
    ntpSynced = true;
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
    return String(buf);
}

int getCurrentMinutes() {
    struct tm ti;
    if (!getLocalTime(&ti, 100)) return -1;
    return ti.tm_hour * 60 + ti.tm_min;
}

// ===== NVS Functions =====

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

// ===== Serial Input =====

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
    Serial.println("Step 1/3: Enter WiFi SSID:");
    saved_ssid = readSerialLine();
    Serial.printf("  SSID: %s\n", saved_ssid.c_str());

    Serial.println("\nStep 2/3: Enter WiFi password:");
    saved_pass = readSerialLine();
    Serial.println("  Password set.");

    Serial.println("\nStep 3/3: Enter Darwin API key:");
    saved_api_key = readSerialLine();
    Serial.println("  API key set.");

    saveSettings(saved_ssid, saved_pass, saved_api_key);
    Serial.println("\nSettings saved!");
    settings_exist = true;
}

// ===== WiFi =====

bool connectWifi() {
    gfx->fillScreen(COL_BG);
    gfx->setTextSize(2);
    gfx->setTextColor(YELLOW);
    gfx->setCursor(100, 220);
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
    }
    Serial.println("WiFi failed!");
    return false;
}

// ===== API Fetch & Parse =====

String buildApiUrl(const char *fromCrs, const char *toCrs, int timeOffset, int timeWindow) {
    String url = "https://";
    url += API_HOST;
    url += API_BASE;
    url += "/";
    url += fromCrs;
    url += "?filterCrs=";
    url += toCrs;
    url += "&filterType=to&numRows=10";
    url += "&timeOffset=";
    url += String(timeOffset);
    url += "&timeWindow=";
    url += String(timeWindow);
    return url;
}

bool serviceExists(BoardData &board, const char *serviceID) {
    for (int i = 0; i < board.count; i++) {
        if (strcmp(board.trains[i].serviceID, serviceID) == 0) return true;
    }
    return false;
}

bool fetchAndAppend(const char *fromCrs, const char *toCrs, BoardData &board,
                    int timeOffset, int timeWindow) {
    String url = buildApiUrl(fromCrs, toCrs, timeOffset, timeWindow);
    Serial.printf("Fetch %s->%s (offset=%d, window=%d)\n", fromCrs, toCrs, timeOffset, timeWindow);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    http.addHeader("x-apikey", saved_api_key.c_str());
    http.setTimeout(15000);

    int httpCode = http.GET();
    Serial.printf("  HTTP %d\n", httpCode);

    if (httpCode != HTTP_CODE_OK) {
        String err = http.getString();
        Serial.printf("  Error: %s\n", err.substring(0, 150).c_str());
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();
    Serial.printf("  %d bytes\n", payload.length());

    DynamicJsonDocument doc(49152);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        Serial.printf("  JSON error: %s\n", error.c_str());
        return false;
    }

    // Extract update time
    if (board.updatedTime[0] == '\0') {
        const char *genAt = doc["generatedAt"] | "";
        if (strlen(genAt) >= 16) {
            board.updatedTime[0] = genAt[11];
            board.updatedTime[1] = genAt[12];
            board.updatedTime[2] = ':';
            board.updatedTime[3] = genAt[14];
            board.updatedTime[4] = genAt[15];
            board.updatedTime[5] = '\0';
        }
    }

    JsonArray services = doc["trainServices"];
    if (services.isNull()) {
        Serial.println("  No services");
        return true;
    }

    for (JsonObject svc : services) {
        if (board.count >= MAX_TRAINS) break;

        const char *sid = svc["serviceID"] | "";
        if (serviceExists(board, sid)) continue;

        TrainInfo &t = board.trains[board.count];
        strlcpy(t.serviceID, sid, sizeof(t.serviceID));
        strlcpy(t.std, svc["std"] | "--:--", sizeof(t.std));
        strlcpy(t.etd, svc["etd"] | "???", sizeof(t.etd));
        strlcpy(t.platform, svc["platform"] | "-", sizeof(t.platform));
        t.isCancelled = svc["isCancelled"] | false;
        t.arrTime[0] = '\0';  // No arrival time yet

        // Default delay from departure
        t.delayMins = calcDelay(t.std, t.etd);

        // Try to get arrival time at destination from calling points
        JsonArray cpLists = svc["subsequentCallingPoints"];
        if (!cpLists.isNull()) {
            for (JsonVariant cpListVar : cpLists) {
                JsonArray cpList = cpListVar.as<JsonArray>();
                if (cpList.isNull()) {
                    JsonArray inner = cpListVar["callingPoint"];
                    if (!inner.isNull()) cpList = inner;
                }
                if (cpList.isNull()) continue;
                for (JsonObject cp : cpList) {
                    const char *crs = cp["crs"] | "";
                    if (strcmp(crs, toCrs) == 0) {
                        const char *st = cp["st"] | "";
                        const char *et = cp["et"] | "";
                        const char *at = cp["at"] | "";
                        bool cpCancelled = cp["isCancelled"] | false;

                        if (cpCancelled) {
                            t.delayMins = -1;
                        } else if (strlen(at) >= 5) {
                            // Actual arrival - use it as displayed time
                            strlcpy(t.arrTime, at, sizeof(t.arrTime));
                            if (strlen(st) >= 5) t.delayMins = calcDelay(st, at);
                        } else if (strcmp(et, "On time") == 0 && strlen(st) >= 5) {
                            // On time - arrival is scheduled time
                            strlcpy(t.arrTime, st, sizeof(t.arrTime));
                            t.delayMins = 0;
                        } else if (strlen(et) >= 5) {
                            // Estimated arrival time
                            strlcpy(t.arrTime, et, sizeof(t.arrTime));
                            if (strlen(st) >= 5) t.delayMins = calcDelay(st, et);
                        }
                        break;
                    }
                }
            }
        }

        // Fallback: if no arrival data from calling points, use departure etd
        if (t.arrTime[0] == '\0' && !t.isCancelled) {
            if (strcmp(t.etd, "On time") == 0) {
                // No arrival time available, use std as best guess
                strlcpy(t.arrTime, t.std, sizeof(t.arrTime));
            } else if (strlen(t.etd) >= 5) {
                strlcpy(t.arrTime, t.etd, sizeof(t.arrTime));
            }
        }

        Serial.printf("  %s Plt %s -> arr %s delay=%d\n",
            t.std, t.platform, t.arrTime, t.delayMins);
        board.count++;
    }

    return true;
}

void sortBoard(BoardData &board) {
    for (int i = 0; i < board.count - 1; i++) {
        for (int j = 0; j < board.count - i - 1; j++) {
            if (strcmp(board.trains[j].std, board.trains[j + 1].std) > 0) {
                TrainInfo temp = board.trains[j];
                board.trains[j] = board.trains[j + 1];
                board.trains[j + 1] = temp;
            }
        }
    }
}

bool fetchDirection(const char *fromCrs, const char *toCrs, BoardData &board) {
    board.count = 0;
    board.success = false;
    memset(board.updatedTime, 0, sizeof(board.updatedTime));

    Serial.printf("\n=== %s -> %s ===\n", fromCrs, toCrs);

    bool ok1 = fetchAndAppend(fromCrs, toCrs, board, -60, 60);
    delay(300);
    bool ok2 = fetchAndAppend(fromCrs, toCrs, board, 0, 120);

    if (!ok1 && !ok2) return false;

    sortBoard(board);
    board.success = true;
    Serial.printf("Total: %d services\n", board.count);
    return true;
}

// ===== Display Drawing =====

void drawBoardHeader() {
    gfx->fillRect(0, 0, SCREEN_W, HEADER_H, COL_HEADER_BG);
    gfx->setTextSize(2);

    gfx->setTextColor(COL_HEADER_FG);
    gfx->setCursor(15, 10);
    gfx->print("Luton");
    gfx->setTextColor(COL_DELAYED);
    gfx->print(" > ");
    gfx->setTextColor(COL_HEADER_FG);
    gfx->print("Farringdon");

    gfx->setTextColor(COL_HEADER_FG);
    gfx->setCursor(HALF_W + 15, 10);
    gfx->print("Farringdon");
    gfx->setTextColor(COL_DELAYED);
    gfx->print(" > ");
    gfx->setTextColor(COL_HEADER_FG);
    gfx->print("Luton");

    gfx->drawFastVLine(HALF_W - 1, 0, HEADER_H, COL_DIVIDER);
    gfx->drawFastVLine(HALF_W, 0, HEADER_H, COL_DIVIDER);
}

void drawColumnHeaders() {
    for (int side = 0; side < 2; side++) {
        int xOff = side * HALF_W;
        gfx->fillRect(xOff, COLHDR_Y, HALF_W, COLHDR_H, COL_COLHDR_BG);
        gfx->setTextSize(1);
        gfx->setTextColor(COL_COLHDR_FG);
        gfx->setCursor(xOff + COL_DEPART_X, COLHDR_Y + 6);
        gfx->print("DEPART");
        gfx->setCursor(xOff + COL_PLT_X, COLHDR_Y + 6);
        gfx->print("PLT");
        gfx->setCursor(xOff + COL_ARRIVAL_X, COLHDR_Y + 6);
        gfx->print("ARRIVAL");
    }
    gfx->drawFastVLine(HALF_W - 1, COLHDR_Y, COLHDR_H, COL_DIVIDER);
    gfx->drawFastVLine(HALF_W, COLHDR_Y, COLHDR_H, COL_DIVIDER);
}

bool isTrainInPast(const TrainInfo &train) {
    int nowMins = getCurrentMinutes();
    if (nowMins < 0) return false;
    int trainMins = timeToMinutes(train.std);
    if (trainMins < 0) return false;
    int diff = trainMins - nowMins;
    if (diff > 720) diff -= 1440;
    if (diff < -720) diff += 1440;
    return (diff < -2);
}

void drawTrainRow(int side, int row, const TrainInfo &train) {
    int xOff = side * HALF_W;
    int y = DATA_Y + (row * ROW_H);
    bool past = isTrainInPast(train);

    uint16_t bgColor = (row % 2 == 0) ? COL_BG : COL_ROW_ALT;
    gfx->fillRect(xOff, y, HALF_W, ROW_H, bgColor);
    gfx->setTextSize(2);

    // DEPART column - scheduled departure time
    gfx->setTextColor(past ? COL_PAST_FG : COL_TIME_FG);
    gfx->setCursor(xOff + COL_DEPART_X, y + 3);
    gfx->print(train.std);

    // PLT column
    gfx->setTextColor(past ? COL_PAST_FG : COL_PLAT_FG);
    gfx->setCursor(xOff + COL_PLT_X, y + 3);
    gfx->print(train.platform);

    // ARRIVAL column - merged: "HH:MM +N" or "CANC"
    gfx->setCursor(xOff + COL_ARRIVAL_X, y + 3);

    if (train.isCancelled || strcmp(train.etd, "Cancelled") == 0) {
        // Cancelled
        gfx->setTextColor(past ? COL_PAST_FG : COL_CANCELLED);
        gfx->print("CANC");
    } else if (train.delayMins == -2) {
        // Unknown delay
        gfx->setTextColor(past ? COL_PAST_FG : COL_DELAYED);
        if (train.arrTime[0] != '\0') {
            gfx->print(train.arrTime);
            gfx->print(" ?");
        } else {
            gfx->print("Delayed");
        }
    } else if (train.arrTime[0] != '\0') {
        // Have arrival time - show "HH:MM +N"
        uint16_t color = (train.delayMins > 0) ? COL_DELAYED : COL_ON_TIME;
        if (past) color = COL_PAST_FG;
        gfx->setTextColor(color);

        char buf[14];
        snprintf(buf, sizeof(buf), "%s +%d", train.arrTime, train.delayMins);
        gfx->print(buf);
    } else {
        // No arrival data at all, show departure status
        uint16_t color = (strcmp(train.etd, "On time") == 0) ? COL_ON_TIME : COL_DELAYED;
        if (past) color = COL_PAST_FG;
        gfx->setTextColor(color);

        if (strcmp(train.etd, "On time") == 0) {
            gfx->print("On time");
        } else {
            gfx->print(train.etd);
        }
    }
}

void drawBoardSide(int side, BoardData &board) {
    int xOff = side * HALF_W;

    if (board.success) {
        if (board.count > 0) {
            int maxRows = min(board.count, (int)DATA_ROWS);
            for (int i = 0; i < maxRows; i++) {
                drawTrainRow(side, i, board.trains[i]);
            }
        } else {
            gfx->setTextSize(2);
            gfx->setTextColor(0x7BEF);
            gfx->setCursor(xOff + 20, DATA_Y + ROW_H + 3);
            gfx->print("No trains");
        }
    } else {
        gfx->setTextSize(2);
        gfx->setTextColor(COL_CANCELLED);
        gfx->setCursor(xOff + 15, DATA_Y + ROW_H + 3);
        gfx->print("Fetch failed");
    }
}

void drawBoardData() {
    gfx->fillRect(0, DATA_Y, SCREEN_W, BOTTOM_BAR_Y - DATA_Y, COL_BG);
    gfx->drawFastVLine(HALF_W - 1, DATA_Y, BOTTOM_BAR_Y - DATA_Y, COL_DIVIDER);
    gfx->drawFastVLine(HALF_W, DATA_Y, BOTTOM_BAR_Y - DATA_Y, COL_DIVIDER);
    drawBoardSide(0, boardLeft);
    drawBoardSide(1, boardRight);
}

void drawBottomBar() {
    gfx->fillRect(BTN_REFRESH_X, BTN_REFRESH_Y, BTN_REFRESH_W, BTN_REFRESH_H, COL_REFRESH_BG);
    gfx->drawRect(BTN_REFRESH_X, BTN_REFRESH_Y, BTN_REFRESH_W, BTN_REFRESH_H, COL_DIVIDER);

    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);

    String now = getCurrentTimeStr();
    char info[40];
    if (boardLeft.updatedTime[0] != '\0') {
        snprintf(info, sizeof(info), "%s  Upd:%s", now.c_str(), boardLeft.updatedTime);
    } else {
        snprintf(info, sizeof(info), "%s", now.c_str());
    }
    gfx->setCursor(BTN_REFRESH_X + 12, BTN_REFRESH_Y + 17);
    gfx->print(info);

    drawCountdown();

    gfx->fillRect(BTN_RESET_X, BTN_RESET_Y, BTN_RESET_W, BTN_RESET_H, COL_RESET_BG);
    gfx->drawRect(BTN_RESET_X, BTN_RESET_Y, BTN_RESET_W, BTN_RESET_H, COL_DIVIDER);
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(BTN_RESET_X + 20, BTN_RESET_Y + 17);
    gfx->print("R");
}

void drawCountdown() {
    int secsLeft = (int)((REFRESH_INTERVAL - (millis() - lastRefreshTime)) / 1000);
    if (secsLeft < 0) secsLeft = 0;

    gfx->fillRect(BTN_REFRESH_X + 300, BTN_REFRESH_Y + 4, 106, 42, COL_REFRESH_BG);
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(BTN_REFRESH_X + 310, BTN_REFRESH_Y + 17);

    char buf[10];
    snprintf(buf, sizeof(buf), "%ds", secsLeft);
    gfx->print(buf);
}

void drawLoadingOverlay() {
    gfx->fillRect(120, 215, 240, 50, COL_COLHDR_BG);
    gfx->drawRect(120, 215, 240, 50, COL_DIVIDER);
    gfx->setTextSize(2);
    gfx->setTextColor(YELLOW);
    gfx->setCursor(135, 232);
    gfx->print("Fetching trains...");
}

// ===== Main Refresh =====

void refreshBoard() {
    drawLoadingOverlay();

    unsigned long startTime = millis();

    fetchDirection(STATION_LUT, STATION_ZFD, boardLeft);
    delay(300);
    fetchDirection(STATION_ZFD, STATION_LUT, boardRight);

    lastRefreshTime = millis();
    unsigned long elapsed = millis() - startTime;
    Serial.printf("\nTotal fetch: %lu ms\n", elapsed);

    drawBoardHeader();
    drawColumnHeaders();
    drawBoardData();
    drawBottomBar();
}

// ===== Setup & Loop =====

void setup() {
    Serial.begin(115200);
    Serial.println("V9: Final Train Board - Starting...");

    gfx->begin(16000000);
    gfx->fillScreen(COL_BG);
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    Wire.begin(19, 45);
    touch.begin();
    touch.setRotation(ROTATION_NORMAL);

    settings_exist = loadSettings();
    if (settings_exist && saved_ssid.length() > 0) {
        Serial.println("Settings loaded from NVS.");
    } else {
        settings_exist = false;
        runSerialSetup();
    }

    if (!connectWifi()) {
        gfx->fillScreen(COL_BG);
        gfx->setTextSize(2);
        gfx->setTextColor(COL_CANCELLED);
        gfx->setCursor(20, 200);
        gfx->print("WiFi connection failed!");
        gfx->setTextColor(YELLOW);
        gfx->setCursor(20, 240);
        gfx->print("Touch to reboot");
        while (true) {
            touch.read();
            if (touch.isTouched) { delay(500); ESP.restart(); }
            delay(50);
        }
    }

    setupNTP();

    gfx->fillScreen(COL_BG);
    drawBoardHeader();
    drawColumnHeaders();
    drawBottomBar();

    refreshBoard();

    Serial.println("V9: Ready!");
}

void loop() {
    if (millis() - lastRefreshTime >= REFRESH_INTERVAL) {
        if (WiFi.isConnected()) {
            refreshBoard();
        } else {
            Serial.println("WiFi disconnected, skipping refresh");
        }
    }

    if (millis() - lastCountdownDraw >= 1000) {
        lastCountdownDraw = millis();
        drawCountdown();
    }

    touch.read();
    if (touch.isTouched) {
        int x = map(touch.points[0].x, 480, 0, 0, 479);
        int y = map(touch.points[0].y, 480, 0, 0, 479);

        if (x >= BTN_REFRESH_X && x <= BTN_REFRESH_X + BTN_REFRESH_W &&
            y >= BTN_REFRESH_Y && y <= BTN_REFRESH_Y + BTN_REFRESH_H) {

            gfx->fillRect(BTN_REFRESH_X, BTN_REFRESH_Y, BTN_REFRESH_W, BTN_REFRESH_H, 0x0347);
            delay(200);
            if (WiFi.isConnected()) refreshBoard();

            while (true) {
                touch.read();
                if (!touch.isTouched) break;
                delay(50);
            }
        }

        if (x >= BTN_RESET_X && x <= BTN_RESET_X + BTN_RESET_W &&
            y >= BTN_RESET_Y && y <= BTN_RESET_Y + BTN_RESET_H) {

            Serial.println("Reset pressed");
            clearSettings();
            gfx->fillScreen(COL_BG);
            gfx->setTextSize(2);
            gfx->setTextColor(YELLOW);
            gfx->setCursor(100, 220);
            gfx->print("Settings cleared!");
            gfx->setCursor(120, 250);
            gfx->print("Rebooting...");
            delay(2000);
            ESP.restart();
        }
    }

    delay(50);
}
