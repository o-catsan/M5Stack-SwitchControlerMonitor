#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
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
M5Canvas uiCanvas(&M5.Display);
bool uiCanvasReady = false;

constexpr uint8_t BOOT_MODE_PIN = 4;
constexpr uint8_t BOOT_MODE_USB = 0;
constexpr uint8_t BOOT_MODE_BLUETOOTH = 1;

bool bootModeBluetooth = false;
bool dualSenseConnected = false;
String dualSenseDeviceName = "";
BLEScan* bleScan = nullptr;
bool bleScanActive = false;
unsigned long lastBluetoothScanMs = 0;

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
    bool isPs5;
    bool rawHasReportId;
    uint8_t reportId;
    int ps5ButtonOffset;
    uint8_t dpad;  // Hat switch value (0-7, 8=center)
    uint8_t lX, lY;
    uint8_t rX, rY;
} padState;

bool isPs5ControllerReport(uint8_t* buf, uint8_t len, bool isRptId) {
    if (len < 10) return false;
    return (buf[0] == 0x01 || buf[0] == 0x11);
}

void parsePs5ControllerReport(uint8_t* buf, bool isRptId) {
    padState.isPs5 = true;
    padState.rawHasReportId = isRptId || buf[0] == 0x01 || buf[0] == 0x11;
    padState.reportId = padState.rawHasReportId ? buf[0] : 0;
    padState.ps5ButtonOffset = -1;

    padState.btnA = padState.btnB = padState.btnX = padState.btnY = false;
    padState.btnL = padState.btnR = padState.btnZL = padState.btnZR = false;
    padState.btnMinus = padState.btnPlus = padState.btnHome = padState.btnCapture = false;
    padState.btnLStick = padState.btnRStick = false;
    padState.dpad = 8;

    int base = 0;
    if (padState.rawLen < base + 10) {
        padState.isPs5 = false;
        return;
    }

    padState.lX = buf[base + 1];
    padState.lY = buf[base + 2];
    padState.rX = buf[base + 3];
    padState.rY = buf[base + 4];

    padState.btnZL = buf[base + 5] != 0;
    padState.btnZR = buf[base + 6] != 0;

    int buttonOffset = base + 8;
    padState.ps5ButtonOffset = buttonOffset;
    uint8_t buttons0 = buf[buttonOffset];
    uint8_t buttons1 = buf[buttonOffset + 1];
    uint8_t buttons2 = (buttonOffset + 2 < padState.rawLen) ? buf[buttonOffset + 2] : 0;

    padState.dpad = buttons0 & 0x0F;
    padState.btnX = (buttons0 & 0x10); // Square
    padState.btnA = (buttons0 & 0x20); // Cross
    padState.btnB = (buttons0 & 0x40); // Circle
    padState.btnY = (buttons0 & 0x80); // Triangle

    padState.btnL = (buttons1 & 0x01);   // L1
    padState.btnR = (buttons1 & 0x02);   // R1
    padState.btnLStick = (buttons1 & 0x40);  // L3
    padState.btnRStick = (buttons1 & 0x80);  // R3
    padState.btnMinus = (buttons1 & 0x10);   // Share
    padState.btnPlus = (buttons1 & 0x20);    // Options
    padState.btnHome = (buttons2 & 0x01);    // PS
    padState.btnCapture = (buttons2 & 0x02); // Touchpad click
}

// HID report parser implementation
class ControllerParser : public HIDReportParser {
public:
    void Parse(USBHID* hid, bool is_rpt_id, uint8_t len, uint8_t* buf) override {
        (void)hid;
        (void)is_rpt_id;

        if (len > 64) len = 64;
        memcpy(padState.raw, buf, len);
        padState.rawLen = len;

        Serial.print("HID RAW:");
        Serial.print(is_rpt_id ? " ID=" : "");
        if (is_rpt_id) {
            Serial.print(buf[0], HEX);
            Serial.print(" ");
        }
        for (uint8_t i = (is_rpt_id ? 1 : 0); i < len; i++) {
            if (buf[i] < 0x10) Serial.print('0');
            Serial.print(buf[i], HEX);
            Serial.print(' ');
        }
        Serial.print(" len=");
        Serial.println(len);

        if (len < 7) return;

        if (isPs5ControllerReport(buf, len, is_rpt_id)) {
            parsePs5ControllerReport(buf, is_rpt_id);
            return;
        }

        padState.isPs5 = false;
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

class DualSenseAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        if (dualSenseConnected) return;

        String name = advertisedDevice.getName();
        bool looksLikeDualSense =
            name.indexOf("DualSense") >= 0 ||
            name.indexOf("Wireless Controller") >= 0 ||
            name.indexOf("PS5") >= 0;

        if (!looksLikeDualSense) return;

        dualSenseDeviceName = advertisedDevice.getName().c_str();
        if (bleScan != nullptr) {
            bleScan->stop();
        }

        BLEClient* client = BLEDevice::createClient();
        if (client->connect(&advertisedDevice)) {
            dualSenseConnected = true;
            Serial.printf("Connected to BLE device: %s\n", dualSenseDeviceName.c_str());
        } else {
            delete client;
        }

        bleScanActive = false;
    }
};

// Communication and UI update intervals
unsigned long lastSendTime = 0;
unsigned long lastDraw = 0;
unsigned long lastDebugPoll = 0;
const unsigned long SEND_INTERVAL_MS = 200;
const unsigned long DRAW_INTERVAL_MS = 33;
const unsigned long DEBUG_POLL_INTERVAL_MS = 200;
const int16_t UI_TOP_Y = 15;
const uint32_t BATTERY_UPDATE_INTERVAL_MS = 10000;
const int16_t BATTERY_TEXT_X = 190;
const int16_t BATTERY_TEXT_Y = 215;

uint8_t usbTaskState = USB_STATE_DETACHED;
int usbIntLevel = -1;
uint8_t max3421Revision = 0;

void resetPadState() {
    memset(&padState, 0, sizeof(padState));
    padState.isPs5 = false;
    padState.dpad = 8;
    padState.lX = 0x80;
    padState.lY = 0x80;
    padState.rX = 0x80;
    padState.rY = 0x80;
}

void startBluetoothScan() {
    if (!bootModeBluetooth || dualSenseConnected || bleScan == nullptr || bleScanActive) {
        return;
    }

    bleScanActive = true;
    bleScan->setAdvertisedDeviceCallbacks(new DualSenseAdvertisedDeviceCallbacks());
    bleScan->setActiveScan(true);
    bleScan->setInterval(100);
    bleScan->setWindow(99);
    bleScan->start(3, false);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    pinMode(BOOT_MODE_PIN, INPUT_PULLUP);
    bootModeBluetooth = (digitalRead(BOOT_MODE_PIN) == HIGH);

    resetPadState();

    M5.Display.setRotation(1);
    uiCanvas.setColorDepth(8);
    uiCanvasReady = uiCanvas.createSprite(
        M5.Display.width(), M5.Display.height() - UI_TOP_Y) != nullptr;
    if (uiCanvasReady) {
        uiCanvas.setTextSize(1);
    }

    Serial.begin(115200);
    M5.Display.setTextSize(2);
    M5.Display.println("M5 PS5 CoRE Sender");
    M5.Display.setTextSize(1);
    M5.Display.printf("Board: %s\n", boardName());

    if (bootModeBluetooth) {
        M5.Display.println("Boot mode: Bluetooth");
        M5.Display.println("Scanning for DualSense...");
        BLEDevice::init("M5Stack DualSense Sender");
        bleScan = BLEDevice::getScan();
        startBluetoothScan();
    } else {
        M5.Display.println("Boot mode: USB Host");
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
            Serial.println("SetReportParser Error");
        }
    }

    Serial2.begin(115200, SERIAL_8N1, SERIAL2_RX_PIN, SERIAL2_TX_PIN);
    M5.Display.printf("Serial2 Started (115200) RX:%d TX:%d\n", SERIAL2_RX_PIN,
                      SERIAL2_TX_PIN);
    Serial.println("Serial debug ready.");
    Serial.println(bootModeBluetooth ? "Bluetooth mode ready" : "USB Host Init OK");
    M5.Display.println("Waiting for 5 seconds...");
    delay(5000);
}

uint16_t getBatteryTextColor(int level);

template <typename DisplayType>
void drawBatteryStatus(DisplayType& target, int16_t yOffset) {
    char batteryText[11];
    if (batteryStatus.level < 0) {
        snprintf(batteryText, sizeof(batteryText), "BAT:  --%%");
    } else {
        snprintf(batteryText, sizeof(batteryText), "BAT: %3d%%", batteryStatus.level);
    }

    target.setTextSize(1);
    target.setCursor(BATTERY_TEXT_X, BATTERY_TEXT_Y - yOffset);
    target.setTextColor(getBatteryTextColor(batteryStatus.level), BLACK);
    target.print(batteryText);
    target.setTextColor(WHITE, BLACK);
}

template <typename DisplayType>
void drawControllerInfoTo(DisplayType& target, int16_t yOffset) {
    const int16_t textY = UI_TOP_Y - yOffset;

    target.setCursor(0, textY);
    target.printf("ST:%02X INT:%d REV:%02X\n", usbTaskState, usbIntLevel,
                  max3421Revision);

    target.setTextColor(WHITE);
    target.setCursor(0, 50 - yOffset);
    if (padState.isPs5) {
        target.printf("SQ:%d CR:%d CI:%d TR:%d\n", padState.btnX, padState.btnA,
                      padState.btnB, padState.btnY);
        target.printf("L1:%d R1:%d L2:%d R2:%d\n", padState.btnL, padState.btnR,
                      padState.btnZL, padState.btnZR);
        target.printf("SH:%d OP:%d PS:%d TP:%d\n", padState.btnMinus,
                      padState.btnPlus, padState.btnHome, padState.btnCapture);
    } else {
        target.printf("A:%d B:%d X:%d Y:%d\n", padState.btnA, padState.btnB,
                      padState.btnX, padState.btnY);
        target.printf("L:%d R:%d ZL:%d ZR:%d\n", padState.btnL, padState.btnR,
                      padState.btnZL, padState.btnZR);
        target.printf("-:%d +:%d H:%d C:%d\n", padState.btnMinus,
                      padState.btnPlus, padState.btnHome, padState.btnCapture);
    }

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
    target.printf("LS:%d RS:%d DP:%s\n", padState.btnLStick,
                  padState.btnRStick, dpadStr);

    target.setCursor(0, 100 - yOffset);
    target.printf("L Stick: X=%3d Y=%3d\n", padState.lX, padState.lY);
    target.printf("R Stick: X=%3d Y=%3d\n", padState.rX, padState.rY);

    int cx = 60, cy = 160 - yOffset, r = 25;
    target.drawRect(cx - r, cy - r, r * 2, r * 2, DARKGREY);
    int lx = map(padState.lX, 0, 255, -r, r);
    int ly = map(padState.lY, 0, 255, -r, r);
    target.fillCircle(cx + lx, cy + ly, 4, GREEN);
    target.setCursor(cx - 10, cy + r + 5);
    target.print("LS");

    cx = 160;
    target.drawRect(cx - r, cy - r, r * 2, r * 2, DARKGREY);
    int rx = map(padState.rX, 0, 255, -r, r);
    int ry = map(padState.rY, 0, 255, -r, r);
    target.fillCircle(cx + rx, cy + ry, 4, GREEN);
    target.setCursor(cx - 10, cy + r + 5);
    target.print("RS");

    cx = 260;
    target.drawRect(cx - r, cy - r, r * 2, r * 2, DARKGREY);
    target.drawLine(cx - r, cy, cx + r, cy, DARKGREY);
    target.drawLine(cx, cy - r, cx, cy + r, DARKGREY);

    if (padState.dpad != 8) {
        target.fillCircle(cx + (dx * 15), cy + (dy * 15), 6, YELLOW);
    } else {
        target.fillCircle(cx, cy, 4, DARKGREY);
    }
    target.setCursor(cx - 15, cy + r + 5);
    target.print("DPAD");

    target.setCursor(0, 215 - yOffset);
    target.setTextColor(CYAN);
    target.printf("TX: %s", lastTxData);
    target.setTextColor(WHITE);
    drawBatteryStatus(target, yOffset);
}

void drawControllerInfo() {
    if (uiCanvasReady) {
        uiCanvas.fillSprite(BLACK);
        drawControllerInfoTo(uiCanvas, UI_TOP_Y);
        uiCanvas.pushSprite(0, UI_TOP_Y);
    } else {
        M5.Display.fillRect(0, UI_TOP_Y, M5.Display.width(),
                            M5.Display.height() - UI_TOP_Y, BLACK);
        drawControllerInfoTo(M5.Display, 0);
    }
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
    Serial.print("BTN:");
    Serial.print(padState.isPs5 ? "PS5 " : "SW ");
    Serial.print("SQ:"); Serial.print(padState.btnX);
    Serial.print(" CR:"); Serial.print(padState.btnA);
    Serial.print(" CI:"); Serial.print(padState.btnB);
    Serial.print(" TR:"); Serial.print(padState.btnY);
    Serial.print(" L1:"); Serial.print(padState.btnL);
    Serial.print(" R1:"); Serial.print(padState.btnR);
    Serial.print(" L2:"); Serial.print(padState.btnZL);
    Serial.print(" R2:"); Serial.print(padState.btnZR);
    Serial.print(" SH:"); Serial.print(padState.btnMinus);
    Serial.print(" OP:"); Serial.print(padState.btnPlus);
    Serial.print(" PS:"); Serial.print(padState.btnHome);
    Serial.print(" TP:"); Serial.print(padState.btnCapture);
    Serial.print(" DP:"); Serial.print(padState.dpad);
    Serial.print(" LX:"); Serial.print(padState.lX);
    Serial.print(" LY:"); Serial.print(padState.lY);
    Serial.print(" RX:"); Serial.print(padState.rX);
    Serial.print(" RY:"); Serial.print(padState.rY);
    Serial.print(" RAW:");
    for (int i = 0; i < padState.rawLen && i < 12; i++) {
        Serial.print(" ");
        Serial.print(padState.raw[i], HEX);
    }
    Serial.println();
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

void loop() {
    if (!bootModeBluetooth) {
        Usb.Task();
    }
    M5.update();
    updateBatteryStatus();

    if (bootModeBluetooth && !dualSenseConnected) {
        if (millis() - lastBluetoothScanMs >= 5000) {
            lastBluetoothScanMs = millis();
            startBluetoothScan();
        }
    }

    if (millis() - lastDebugPoll >= DEBUG_POLL_INTERVAL_MS) {
        lastDebugPoll = millis();
        if (!bootModeBluetooth) {
            usbTaskState = Usb.getUsbTaskState();
            usbIntLevel = digitalRead(USB_HOST_SHIELD_INT_GPIO);
            max3421Revision = Usb.regRd(rREVISION);
            Serial.printf("USB state=%02X INT=%d REV=%02X\n", usbTaskState, usbIntLevel, max3421Revision);
        } else {
            Serial.printf("Bluetooth mode: %s\n", dualSenseConnected ? dualSenseDeviceName.c_str() : "scanning");
        }
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
