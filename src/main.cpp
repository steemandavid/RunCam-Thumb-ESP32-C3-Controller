// =============================================================================
//  RunCam Thumb ESP32-C3 Controller — production boot sequence
//
//  Boot order:
//    1. USB CDC + status LED
//    2. NVS / SettingsStore (defaults on first boot)
//    3. UART1 transport at 115200 (GPIO3 TX, GPIO4 RX)
//    4. RunCamCamera::begin() — GET_DEVICE_INFO, caches features
//    5. OLED init (before auto-start so display is live during boot)
//    6. Auto-start recording if setting enabled
//    7. WiFi AP + WebServer
//    8. Main loop: FlightController::update, OLED render, WS push
// =============================================================================

#ifndef UNIT_TEST

#include <Arduino.h>
#include <HWCDC.h>
#include <Preferences.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"
#include "system_state.h"
#include "transport/uart_transport.h"
#include "camera/runcam_camera.h"
#include "storage/settings_store.h"
#include "flight/flight_controller.h"
#include "display/oled_display.h"
#include "web/ws_notifier.h"
#include "web/web_server.h"

HWCDC             usbSerial;
SemaphoreHandle_t camMutex;

class EspPreferences : public IPreferences {
public:
    bool begin(const char* name, bool readOnly) override { return prefs_.begin(name, readOnly); }
    void end() override                                  { prefs_.end(); }
    bool isKey(const char* key) override                 { return prefs_.isKey(key); }
    int32_t getInt(const char* key, int32_t d = 0) override   { return prefs_.getInt(key, d); }
    size_t  putInt(const char* key, int32_t v) override       { return prefs_.putInt(key, v); }
    bool    remove(const char* key) override                  { return prefs_.remove(key); }
private:
    Preferences prefs_;
};

EspPreferences   espPrefs;
SettingsStore    settingsStore(espPrefs);
UartTransport    uart(CAMERA_UART_NUM, GPIO_UART_TX, GPIO_UART_RX, RUNCAM_BAUD_RATE);
RunCamCamera     camera(uart);
FlightController flight(camera);
OledDisplay      oled;
WsNotifier       wsNotifier;
WebServer        webServer;

SystemState      systemState;

static uint32_t lastLedToggleMs = 0;
static bool     ledOn = false;

static void updateStatusLed(uint32_t now, FlightState st) {
    if (st == FlightState::RECORDING) {
        digitalWrite(GPIO_STATUS_LED, HIGH);
        ledOn = true;
        return;
    }
    uint32_t period = 2000;
    if (now - lastLedToggleMs >= period / 2) {
        ledOn = !ledOn;
        digitalWrite(GPIO_STATUS_LED, ledOn ? HIGH : LOW);
        lastLedToggleMs = now;
    }
}

void setup() {
    usbSerial.begin(115200);
    delay(1500);
    usbSerial.println();
    usbSerial.println("=== RunCam Thumb Pro W Controller boot ===");

    camMutex = xSemaphoreCreateMutex();

    pinMode(GPIO_STATUS_LED, OUTPUT);
    digitalWrite(GPIO_STATUS_LED, LOW);

    // ---- NVS / Settings ----
    settingsStore.begin();
    CameraSettings settings = settingsStore.load();
    usbSerial.println("Settings: loaded from NVS");

    // ---- UART + Camera ----
    uart.begin();
    delay(100);
    CameraResult r = camera.begin();
    if (r == CameraResult::OK) {
        DeviceInfo info = camera.getDeviceInfo();
        usbSerial.printf("Camera: proto=v%u features=0x%04X\n",
                         info.protocolVersion, info.featureBitmask);
    } else {
        usbSerial.printf("Camera: begin() failed, result=%d\n", static_cast<int>(r));
    }
    systemState.cameraCommsOk = (r == CameraResult::OK);
    systemState.autoStartRec = (settings.autoStartRec == 1);

    // ---- OLED ----
    Wire.begin(GPIO_OLED_SDA, GPIO_OLED_SCL);
    if (oled.begin()) {
        usbSerial.println("OLED: ready");
    } else {
        usbSerial.println("OLED: init failed");
    }

    // ---- Auto-start recording ----
    if (systemState.autoStartRec && r == CameraResult::OK) {
        if (xSemaphoreTake(camMutex, portMAX_DELAY) == pdTRUE) {
            CameraResult sr = flight.forceStartRecording(millis());
            xSemaphoreGive(camMutex);
            if (sr == CameraResult::OK) {
                usbSerial.println("Auto-start: recording started");
            } else {
                usbSerial.printf("Auto-start: failed, result=%d\n", static_cast<int>(sr));
            }
        }
    }

    // ---- WiFi AP + Web ----
    webServer.begin(camera, flight, settingsStore, wsNotifier, camMutex);
    usbSerial.printf("WiFi AP: SSID=\"%s\" IP=192.168.4.1\n", WIFI_SSID);

    usbSerial.println("=== boot complete ===");
}

uint32_t lastOledMs = 0;
uint32_t lastWsMs   = 0;

void loop() {
    uint32_t now = millis();

    if (xSemaphoreTake(camMutex, portMAX_DELAY) == pdTRUE) {
        flight.update(now);
        systemState.flightState      = flight.getState();
        systemState.recordingSeconds = flight.getRecordingSeconds(now);
        systemState.cameraCommsOk    = camera.isInitialised();
        xSemaphoreGive(camMutex);
    }

    updateStatusLed(now, systemState.flightState);

    if (now - lastOledMs >= OLED_REFRESH_INTERVAL_MS) {
        oled.render(systemState);
        lastOledMs = now;
    }

    if (now - lastWsMs >= WS_STATUS_INTERVAL_MS) {
        wsNotifier.broadcastStatus(systemState);
        lastWsMs = now;
    }

    webServer.update();
    delay(5);
}

#endif // UNIT_TEST
