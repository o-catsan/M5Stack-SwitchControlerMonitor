#include <M5Unified.h>
// Do not include <Usb.h> directly: on Windows it can resolve case-insensitively
// to ESP32 core's USB.h. usbhub.h includes USB Host Shield's local "Usb.h".
#include <usbhub.h>
#include <hiduniversal.h>

#ifndef USB_MODULE_SS_CH
#define USB_MODULE_SS_CH 1
#endif

#ifndef USB_MODULE_INT_CH
#define USB_MODULE_INT_CH 1
#endif

#ifndef USB_HOST_SHIELD_SS_GPIO
#define USB_HOST_SHIELD_SS_GPIO 19
#endif

#ifndef USB_HOST_SHIELD_INT_GPIO
#define USB_HOST_SHIELD_INT_GPIO 35
#endif

#ifndef SERIAL2_RX_PIN
#if defined(BUILD_TARGET_CORES3SE)
// M5Unified CoreS3 SE Port C definition: pin 1=RX(GPIO18), pin 2=TX(GPIO17).
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

// USB Host global objects
USB Usb;
USBHub Hub(&Usb);
HIDUniversal Hid(&Usb);
char lastTxData[21] = "00,00,00,80,80,80,80";

struct BatteryStatus {
    int level = -1;
    uint32_t lastUpdateMs = 0;
} batteryStatus;

const char* boardName() {
#if defined(BUILD_TARGET_CORES3SE)
    return "M5 CoreS3 SE";
#elif defined(ARDUINO_M5STACK_CORE2) || defined(ARDUINO_M5STACK_Core2)
    return "M5Stack Core2";
#else
    return "M5Stack Core";
#endif
}

// Data structure to hold controller state
struct ControllerState {
    uint8_t raw[64];
    uint8_t rawLen;

    bool btnA, btnB, btnX, btnY;
    bool btnL, btnR, btnZL, btnZR;
    bool btnMinus, btnPlus, btnHome, btnCapture;
    bool btnLStick, btnRStick;
    uint8_t dpad;  // Hat switch value (0-7, 8=center)
    uint8_t lX, lY;
    uint8_t rX, rY;
} padState;

// HID report parser implementation
class ControllerParser : public HIDReportParser {
public:
    void Parse(USBHID* hid, bool is_rpt_id, uint8_t len, uint8_t* buf) override {
        (void)hid;
        (void)is_rpt_id;

        if (len > 64) len = 64;
        memcpy(padState.raw, buf, len);
        padState.rawLen = len;

        if (len < 7) return;

        padState.btnY = (buf[0] & 0x01);
        padState.btnB = (buf[0] & 0x02);
        padState.btnA = (buf[0] & 0x04);
        padState.btnX = (buf[0] & 0x08);
        padState.btnL = (buf[0] & 0x10);
        padState.btnR = (buf[0] & 0x20);
        padState.btnZL = (buf[0] & 0x40);
        padState.btnZR = (buf[0] & 0x80);

        padState.btnMinus = (buf[1] & 0x01);
        padState.btnPlus = (buf[1] & 0x02);
        padState.btnLStick = (buf[1] & 0x04);
        padState.btnRStick = (buf[1] & 0x08);
        padState.btnHome = (buf[1] & 0x10);
        padState.btnCapture = (buf[1] & 0x20);

        padState.dpad = buf[2] & 0x0F;
        padState.lX = buf[3];
        padState.lY = buf[4];
        padState.rX = buf[5];
        padState.rY = buf[6];
    }
} parser;

// Communication and UI update intervals
unsigned long lastSendTime = 0;
unsigned long lastDraw = 0;
unsigned long lastDebugPoll = 0;
const unsigned long SEND_INTERVAL_MS = 200;
const unsigned long DRAW_INTERVAL_MS = 33;
const unsigned long DEBUG_POLL_INTERVAL_MS = 200;
const uint32_t BATTERY_UPDATE_INTERVAL_MS = 10000;
const int16_t BATTERY_TEXT_X = 190;
const int16_t BATTERY_TEXT_Y = 215;

uint8_t usbTaskState = USB_STATE_DETACHED;
int usbIntLevel = -1;
uint8_t max3421Revision = 0;

void resetPadState() {
    memset(&padState, 0, sizeof(padState));
    padState.dpad = 8;
    padState.lX = 0x80;
    padState.lY = 0x80;
    padState.rX = 0x80;
    padState.rY = 0x80;
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    resetPadState();

    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);
    M5.Display.println("M5 Switch2CoRE Sender");
    M5.Display.setTextSize(1);
    M5.Display.printf("Board: %s\n", boardName());
    M5.Display.println("Init USB Host...");
    M5.Display.printf("DIP: SS CH%d(GPIO%d) / INT CH%d(GPIO%d)\n",
                      USB_MODULE_SS_CH, USB_HOST_SHIELD_SS_GPIO,
                      USB_MODULE_INT_CH, USB_HOST_SHIELD_INT_GPIO);

    if (Usb.Init() == -1) {
        M5.Display.setTextColor(RED);
        M5.Display.println("OSC did not start.");
        while (true) {
            delay(1000);
        }
    }
    M5.Display.setTextColor(GREEN);
    M5.Display.println("USB Host Init OK");
    M5.Display.setTextColor(WHITE);

    if (!Hid.SetReportParser(0, &parser)) {
        M5.Display.println("SetReportParser Error");
    }

    Serial2.begin(115200, SERIAL_8N1, SERIAL2_RX_PIN, SERIAL2_TX_PIN);
    M5.Display.printf("Serial2 Started (115200) RX:%d TX:%d\n", SERIAL2_RX_PIN,
                      SERIAL2_TX_PIN);
    M5.Display.println("Waiting for 5 seconds...");
    delay(5000);
}

void drawControllerInfo() {
    M5.Display.setCursor(0, 15);
    M5.Display.fillRect(0, 15, M5.Display.width(), M5.Display.height() - 15, BLACK);

    M5.Display.setCursor(0, 15);
    M5.Display.printf("ST:%02X INT:%d REV:%02X\n", usbTaskState, usbIntLevel,
                      max3421Revision);

    M5.Display.setCursor(0, 30);
    M5.Display.setTextColor(YELLOW);
    M5.Display.print("RAW: ");
    for (int i = 0; i < 8 && i < padState.rawLen; i++) {
        M5.Display.printf("%02X ", padState.raw[i]);
    }
    M5.Display.println();
    M5.Display.setTextColor(WHITE);

    M5.Display.setCursor(0, 50);
    M5.Display.printf("A:%d B:%d X:%d Y:%d\n", padState.btnA, padState.btnB,
                      padState.btnX, padState.btnY);
    M5.Display.printf("L:%d R:%d ZL:%d ZR:%d\n", padState.btnL, padState.btnR,
                      padState.btnZL, padState.btnZR);
    M5.Display.printf("-:%d +:%d H:%d C:%d\n", padState.btnMinus,
                      padState.btnPlus, padState.btnHome, padState.btnCapture);

    const char* dpadStr = "CENTER";
    int dx = 0, dy = 0;
    switch (padState.dpad) {
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
    M5.Display.printf("LS:%d RS:%d DP:%s\n", padState.btnLStick,
                      padState.btnRStick, dpadStr);

    M5.Display.setCursor(0, 100);
    M5.Display.printf("L Stick: X=%3d Y=%3d\n", padState.lX, padState.lY);
    M5.Display.printf("R Stick: X=%3d Y=%3d\n", padState.rX, padState.rY);

    int cx = 60, cy = 160, r = 25;
    M5.Display.drawRect(cx - r, cy - r, r * 2, r * 2, DARKGREY);
    int lx = map(padState.lX, 0, 255, -r, r);
    int ly = map(padState.lY, 0, 255, -r, r);
    M5.Display.fillCircle(cx + lx, cy + ly, 4, GREEN);
    M5.Display.setCursor(cx - 10, cy + r + 5);
    M5.Display.print("LS");

    cx = 160;
    M5.Display.drawRect(cx - r, cy - r, r * 2, r * 2, DARKGREY);
    int rx = map(padState.rX, 0, 255, -r, r);
    int ry = map(padState.rY, 0, 255, -r, r);
    M5.Display.fillCircle(cx + rx, cy + ry, 4, GREEN);
    M5.Display.setCursor(cx - 10, cy + r + 5);
    M5.Display.print("RS");

    cx = 260;
    M5.Display.drawRect(cx - r, cy - r, r * 2, r * 2, DARKGREY);
    M5.Display.drawLine(cx - r, cy, cx + r, cy, DARKGREY);
    M5.Display.drawLine(cx, cy - r, cx, cy + r, DARKGREY);

    if (padState.dpad != 8) {
        M5.Display.fillCircle(cx + (dx * 15), cy + (dy * 15), 6, YELLOW);
    } else {
        M5.Display.fillCircle(cx, cy, 4, DARKGREY);
    }
    M5.Display.setCursor(cx - 15, cy + r + 5);
    M5.Display.print("DPAD");

    M5.Display.setCursor(0, 215);
    M5.Display.setTextColor(CYAN);
    M5.Display.printf("TX: %s", lastTxData);
    M5.Display.setTextColor(WHITE);
    drawBatteryStatus();
}

void sendControllerState() {
    while (Serial2.available()) {
        Serial2.read();
    }

    uint8_t byte0 = 0;
    if (padState.btnA) byte0 |= 0x01;
    if (padState.btnB) byte0 |= 0x02;
    if (padState.btnX) byte0 |= 0x04;
    if (padState.btnY) byte0 |= 0x08;
    if (padState.btnL) byte0 |= 0x10;
    if (padState.btnR) byte0 |= 0x20;
    if (padState.btnZL) byte0 |= 0x40;
    if (padState.btnZR) byte0 |= 0x80;

    uint8_t byte1 = 0;
    if (padState.btnMinus) byte1 |= 0x01;
    if (padState.btnPlus) byte1 |= 0x02;
    if (padState.btnHome) byte1 |= 0x04;
    if (padState.btnCapture) byte1 |= 0x08;
    if (padState.btnLStick) byte1 |= 0x10;
    if (padState.btnRStick) byte1 |= 0x20;

    uint8_t byte2 = 0;
    if (padState.dpad == 0x0F || padState.dpad == 8) {
        byte2 = 0;
    } else {
        byte2 = (padState.dpad & 0x0F) + 1;
    }

    snprintf(lastTxData, sizeof(lastTxData), "%02X,%02X,%02X,%02X,%02X,%02X,%02X", byte0,
             byte1, byte2, padState.lX, padState.lY, padState.rX, padState.rY);
    Serial2.print(lastTxData);
    Serial2.print("\r\n");
}

void updateBatteryStatus() {
    const uint32_t now = millis();
    if (batteryStatus.lastUpdateMs != 0 &&
        now - batteryStatus.lastUpdateMs < BATTERY_UPDATE_INTERVAL_MS) {
        return;
    }

    batteryStatus.lastUpdateMs = now;
    const int level = M5.Power.getBatteryLevel();
    batteryStatus.level = (level >= 0 && level <= 100) ? level : -1;
}

uint16_t getBatteryTextColor(int level) {
    if (level >= 51) return WHITE;
    if (level >= 26) return YELLOW;
    if (level >= 0) return RED;
    return WHITE;
}

void drawBatteryStatus() {
    char batteryText[11];
    if (batteryStatus.level < 0) {
        snprintf(batteryText, sizeof(batteryText), "BAT:  --%%");
    } else {
        snprintf(batteryText, sizeof(batteryText), "BAT: %3d%%", batteryStatus.level);
    }

    M5.Display.setTextSize(1);
    M5.Display.setCursor(BATTERY_TEXT_X, BATTERY_TEXT_Y);
    M5.Display.setTextColor(getBatteryTextColor(batteryStatus.level), BLACK);
    M5.Display.print(batteryText);
    M5.Display.setTextColor(WHITE, BLACK);
}

void loop() {
    Usb.Task();
    M5.update();
    updateBatteryStatus();

    if (millis() - lastDebugPoll >= DEBUG_POLL_INTERVAL_MS) {
        lastDebugPoll = millis();
        usbTaskState = Usb.getUsbTaskState();
        usbIntLevel = digitalRead(USB_HOST_SHIELD_INT_GPIO);
        max3421Revision = Usb.regRd(rREVISION);
    }

    if (millis() - lastDraw > DRAW_INTERVAL_MS) {
        drawControllerInfo();
        lastDraw = millis();
    }

    if (millis() - lastSendTime >= SEND_INTERVAL_MS) {
        lastSendTime = millis();
        sendControllerState();
    }
}
