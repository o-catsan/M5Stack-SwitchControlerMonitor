#include <M5Unified.h>
#include <WiFi.h>

#ifndef SERIAL2_RX_PIN
#if defined(BUILD_TARGET_CORES3SE)
#define SERIAL2_RX_PIN 18
#define SERIAL2_TX_PIN 17
#elif defined(ARDUINO_M5STACK_CORE2) || defined(ARDUINO_M5STACK_Core2)
#define SERIAL2_RX_PIN 13
#define SERIAL2_TX_PIN 14
#else
#define SERIAL2_RX_PIN 16
#define SERIAL2_TX_PIN 17
#endif
#endif

constexpr char WIFI_SSID[] = "M5Stack-Wireless-Sender";
constexpr char WIFI_PASSWORD[] = "m5stackwifi";
constexpr char SENDER_IP[] = "192.168.4.1";
constexpr uint16_t WIFI_TCP_PORT = 12345;

constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 3000;
constexpr uint32_t TCP_RETRY_INTERVAL_MS = 1000;

WiFiClient tcpClient;
char lastRxData[48] = "--";
uint32_t rxCount = 0;
uint32_t lastWiFiRetryMs = 0;
uint32_t lastTcpRetryMs = 0;
char tcpLineBuffer[64] = {0};
size_t tcpLineLen = 0;
M5Canvas uiCanvas(&M5.Display);
bool uiCanvasReady = false;

constexpr uint32_t DRAW_INTERVAL_MS = 33;
constexpr int16_t UI_TOP_Y = 15;

struct ReceivedPadState {
    bool btnA = false;
    bool btnB = false;
    bool btnX = false;
    bool btnY = false;
    bool btnL = false;
    bool btnR = false;
    bool btnZL = false;
    bool btnZR = false;
    bool btnMinus = false;
    bool btnPlus = false;
    bool btnHome = false;
    bool btnCapture = false;
    bool btnLStick = false;
    bool btnRStick = false;
    uint8_t dpad = 8;
    uint8_t lX = 0x80;
    uint8_t lY = 0x80;
    uint8_t rX = 0x80;
    uint8_t rY = 0x80;
} rxPadState;

const char* boardName() {
#if defined(BUILD_TARGET_CORES3SE)
    return "M5 CoreS3 SE";
#elif defined(ARDUINO_M5STACK_CORE2) || defined(ARDUINO_M5STACK_Core2)
    return "M5Stack Core2";
#else
    return "M5Stack Core";
#endif
}

bool isWiFiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void connectWiFiIfNeeded() {
    if (isWiFiConnected()) {
        return;
    }

    uint32_t now = millis();
    if (lastWiFiRetryMs != 0 && now - lastWiFiRetryMs < WIFI_RETRY_INTERVAL_MS) {
        return;
    }

    lastWiFiRetryMs = now;
    Serial.printf("Connecting WiFi SSID=%s...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectTcpIfNeeded() {
    if (!isWiFiConnected() || tcpClient.connected()) {
        return;
    }

    uint32_t now = millis();
    if (lastTcpRetryMs != 0 && now - lastTcpRetryMs < TCP_RETRY_INTERVAL_MS) {
        return;
    }

    lastTcpRetryMs = now;
    Serial.printf("Connecting TCP %s:%u...\n", SENDER_IP, WIFI_TCP_PORT);
    if (tcpClient.connect(SENDER_IP, WIFI_TCP_PORT)) {
        tcpClient.setNoDelay(true);
        Serial.println("TCP connected");
    } else {
        Serial.println("TCP connect failed");
    }
}

void processTcpInput() {
    while (tcpClient.connected() && tcpClient.available()) {
        const char c = static_cast<char>(tcpClient.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            if (tcpLineLen > 0) {
                tcpLineBuffer[tcpLineLen] = '\0';
                strncpy(lastRxData, tcpLineBuffer, sizeof(lastRxData) - 1);
                lastRxData[sizeof(lastRxData) - 1] = '\0';
                parseRxLine(lastRxData);
                rxCount++;
                Serial2.print(lastRxData);
                Serial2.print("\r\n");
            }
            tcpLineLen = 0;
            continue;
        }

        if (tcpLineLen < sizeof(tcpLineBuffer) - 1) {
            tcpLineBuffer[tcpLineLen++] = c;
        } else {
            tcpLineLen = 0;
        }
    }
}

uint8_t clampToByte(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return static_cast<uint8_t>(value);
}

bool parseRxLine(const char* line) {
    int p[7] = {0};
    const int parsed = sscanf(line, "%x,%x,%x,%x,%x,%x,%x", &p[0], &p[1], &p[2], &p[3],
                              &p[4], &p[5], &p[6]);
    if (parsed != 7) {
        return false;
    }

    const uint8_t byte0 = clampToByte(p[0]);
    const uint8_t byte1 = clampToByte(p[1]);
    const uint8_t byte2 = clampToByte(p[2]);

    rxPadState.btnA = (byte0 & 0x01) != 0;
    rxPadState.btnB = (byte0 & 0x02) != 0;
    rxPadState.btnX = (byte0 & 0x04) != 0;
    rxPadState.btnY = (byte0 & 0x08) != 0;
    rxPadState.btnL = (byte0 & 0x10) != 0;
    rxPadState.btnR = (byte0 & 0x20) != 0;
    rxPadState.btnZL = (byte0 & 0x40) != 0;
    rxPadState.btnZR = (byte0 & 0x80) != 0;

    rxPadState.btnMinus = (byte1 & 0x01) != 0;
    rxPadState.btnPlus = (byte1 & 0x02) != 0;
    rxPadState.btnHome = (byte1 & 0x04) != 0;
    rxPadState.btnCapture = (byte1 & 0x08) != 0;
    rxPadState.btnLStick = (byte1 & 0x10) != 0;
    rxPadState.btnRStick = (byte1 & 0x20) != 0;

    rxPadState.dpad = (byte2 == 0) ? 8 : static_cast<uint8_t>((byte2 - 1) & 0x0F);
    rxPadState.lX = clampToByte(p[3]);
    rxPadState.lY = clampToByte(p[4]);
    rxPadState.rX = clampToByte(p[5]);
    rxPadState.rY = clampToByte(p[6]);

    return true;
}

template <typename DisplayType>
void drawReceiverInfoTo(DisplayType& target, int16_t yOffset) {
    const int16_t textY = UI_TOP_Y - yOffset;

    target.setCursor(0, textY);
    target.setTextColor(WHITE);
    target.printf("WiFi:%s TCP:%s\n", isWiFiConnected() ? "connected" : "connecting",
                  tcpClient.connected() ? "connected" : "waiting");
    target.printf("RX:%lu RSSI:%d IP:%s\n", static_cast<unsigned long>(rxCount),
                  isWiFiConnected() ? WiFi.RSSI() : 0,
                  isWiFiConnected() ? WiFi.localIP().toString().c_str() : "--");

    target.setCursor(0, 50 - yOffset);
    target.printf("A:%d B:%d X:%d Y:%d\n", rxPadState.btnA, rxPadState.btnB,
                  rxPadState.btnX, rxPadState.btnY);
    target.printf("L:%d R:%d ZL:%d ZR:%d\n", rxPadState.btnL, rxPadState.btnR,
                  rxPadState.btnZL, rxPadState.btnZR);
    target.printf("-:%d +:%d H:%d C:%d\n", rxPadState.btnMinus,
                  rxPadState.btnPlus, rxPadState.btnHome, rxPadState.btnCapture);

    const char* dpadStr = "CENTER";
    int dx = 0;
    int dy = 0;
    switch (rxPadState.dpad) {
        case 0:
            dpadStr = "UP";
            dy = -1;
            break;
        case 1:
            dpadStr = "UP-R";
            dx = 1;
            dy = -1;
            break;
        case 2:
            dpadStr = "RIGHT";
            dx = 1;
            break;
        case 3:
            dpadStr = "DW-R";
            dx = 1;
            dy = 1;
            break;
        case 4:
            dpadStr = "DOWN";
            dy = 1;
            break;
        case 5:
            dpadStr = "DW-L";
            dx = -1;
            dy = 1;
            break;
        case 6:
            dpadStr = "LEFT";
            dx = -1;
            break;
        case 7:
            dpadStr = "UP-L";
            dx = -1;
            dy = -1;
            break;
        default:
            break;
    }

    target.printf("LS:%d RS:%d DP:%s\n", rxPadState.btnLStick,
                  rxPadState.btnRStick, dpadStr);

    target.setCursor(0, 100 - yOffset);
    target.printf("L Stick: X=%3d Y=%3d\n", rxPadState.lX, rxPadState.lY);
    target.printf("R Stick: X=%3d Y=%3d\n", rxPadState.rX, rxPadState.rY);

    int cx = 60;
    int cy = 160 - yOffset;
    int r = 25;
    target.drawRect(cx - r, cy - r, r * 2, r * 2, DARKGREY);
    int lx = map(rxPadState.lX, 0, 255, -r, r);
    int ly = map(rxPadState.lY, 0, 255, -r, r);
    target.fillCircle(cx + lx, cy + ly, 4, GREEN);
    target.setCursor(cx - 10, cy + r + 5);
    target.print("LS");

    cx = 160;
    target.drawRect(cx - r, cy - r, r * 2, r * 2, DARKGREY);
    int rx = map(rxPadState.rX, 0, 255, -r, r);
    int ry = map(rxPadState.rY, 0, 255, -r, r);
    target.fillCircle(cx + rx, cy + ry, 4, GREEN);
    target.setCursor(cx - 10, cy + r + 5);
    target.print("RS");

    cx = 260;
    target.drawRect(cx - r, cy - r, r * 2, r * 2, DARKGREY);
    target.drawLine(cx - r, cy, cx + r, cy, DARKGREY);
    target.drawLine(cx, cy - r, cx, cy + r, DARKGREY);
    if (rxPadState.dpad != 8) {
        target.fillCircle(cx + (dx * 15), cy + (dy * 15), 6, YELLOW);
    } else {
        target.fillCircle(cx, cy, 4, DARKGREY);
    }
    target.setCursor(cx - 15, cy + r + 5);
    target.print("DPAD");

    target.setCursor(0, 215 - yOffset);
    target.setTextColor(CYAN);
    target.printf("RX: %s", lastRxData);
    target.setTextColor(WHITE);
}

void drawStatus() {
    if (uiCanvasReady) {
        uiCanvas.fillSprite(BLACK);
        drawReceiverInfoTo(uiCanvas, UI_TOP_Y);
        uiCanvas.pushSprite(0, UI_TOP_Y);
    } else {
        M5.Display.fillRect(0, UI_TOP_Y, M5.Display.width(),
                            M5.Display.height() - UI_TOP_Y, BLACK);
        drawReceiverInfoTo(M5.Display, 0);
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, SERIAL2_RX_PIN, SERIAL2_TX_PIN);

    M5.Display.setRotation(1);
    uiCanvas.setColorDepth(8);
    uiCanvasReady = uiCanvas.createSprite(
        M5.Display.width(), M5.Display.height() - UI_TOP_Y) != nullptr;
    if (uiCanvasReady) {
        uiCanvas.setTextSize(1);
    }
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(WHITE);
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextSize(2);
    M5.Display.println("WiFi Receiver");
    M5.Display.setTextSize(1);
    M5.Display.printf("Board: %s\n", boardName());

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(100);

    connectWiFiIfNeeded();
    drawStatus();
}

void loop() {
    M5.update();

    connectWiFiIfNeeded();
    connectTcpIfNeeded();

    processTcpInput();

    static uint32_t lastDrawMs = 0;
    uint32_t now = millis();
    if (now - lastDrawMs >= DRAW_INTERVAL_MS) {
        lastDrawMs = now;
        drawStatus();
    }

    if (!tcpClient.connected() && isWiFiConnected()) {
        tcpClient.stop();
    }
}
