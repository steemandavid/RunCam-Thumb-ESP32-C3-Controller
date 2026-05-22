// =============================================================================
//  RunCam Thumb ESP32-C3 Controller — production boot sequence
//  See FSD §12.4 and §3.2.
//
//  Boot order:
//    1. USB CDC + status LED + ARM pin
//    2. NVS / SettingsStore (defaults on first boot)
//    3. UART1 transport at 115200 (GPIO3 TX, GPIO4 RX)
//    4. RunCamCamera::begin() — GET_DEVICE_INFO, caches features
//    5. Apply settings to camera (currently a no-op due to deferred §4.5)
//    6. PreflightCheck::run() — currently returns deferred result
//    7. OLED init
//    8. WiFi AP + WebServer
//    9. Main loop: ARM pin, FlightController::update, OLED render, WS push
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
#include "flight/preflight_check.h"
#include "display/oled_display.h"
#include "web/ws_notifier.h"
#include "web/web_server.h"

HWCDC             usbSerial;
// Mutex guarding all camera + flight controller access. Web routes
// (AsyncTCP task) and the main loop both take this before touching
// the shared modules. See Code_Review_Phase4 MAJOR-2.
SemaphoreHandle_t camMutex;

// ---- IPreferences adapter for the real ESP32 Preferences library -----------
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

PreflightResult  preflight;
SystemState      systemState;

// ---- LED helpers -----------------------------------------------------------
static uint32_t lastLedToggleMs = 0;
static bool     ledOn = false;

static void updateStatusLed(uint32_t now, FlightState st) {
    // IDLE: slow 2s blink. ARMED/RECORDING: solid on. STOPPING: brief off.
    uint32_t period = 0;
    bool     solid  = false;
    switch (st) {
        case FlightState::IDLE:      period = 2000; break;
        case FlightState::ARMED:
        case FlightState::RECORDING: solid  = true; break;
        case FlightState::STOPPING:  solid  = false; break;
    }
    if (solid) {
        digitalWrite(GPIO_STATUS_LED, HIGH);
        ledOn = true;
        return;
    }
    if (period == 0) {
        digitalWrite(GPIO_STATUS_LED, LOW);
        ledOn = false;
        return;
    }
    if (now - lastLedToggleMs >= period / 2) {
        ledOn = !ledOn;
        digitalWrite(GPIO_STATUS_LED, ledOn ? HIGH : LOW);
        lastLedToggleMs = now;
    }
}

// ---- Boot ------------------------------------------------------------------
void setup() {
    usbSerial.begin(115200);
    delay(1500);
    usbSerial.println();
    usbSerial.println("=== RunCam Thumb Pro W Controller boot ===");

    camMutex = xSemaphoreCreateMutex();

    pinMode(GPIO_STATUS_LED, OUTPUT);
    pinMode(GPIO_ARM_PIN, INPUT_PULLUP);
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

    // ---- Preflight (deferred — see §5.2) ----
    preflight = PreflightCheck::run(camera, settings);
    systemState.preflightDone     = true;
    systemState.preflightPassed   = preflight.passed;
    systemState.preflightDeferred = preflight.deferred;
    if (preflight.deferred) {
        usbSerial.println("Preflight: deferred (settings access unavailable)");
    }

    // ---- OLED ----
    Wire.begin(GPIO_OLED_SDA, GPIO_OLED_SCL);
    if (oled.begin()) {
        usbSerial.println("OLED: ready");
    } else {
        usbSerial.println("OLED: init failed");
    }

    // ---- WiFi AP + Web ----
    webServer.begin(camera, flight, settingsStore, wsNotifier, preflight, camMutex);
    usbSerial.printf("WiFi AP: SSID=\"%s\" IP=192.168.4.1\n", WIFI_SSID);

    usbSerial.println("=== boot complete ===");
}

// ---- Main loop -------------------------------------------------------------
uint32_t lastOledMs = 0;
uint32_t lastWsMs   = 0;

void loop() {
    uint32_t now = millis();
    bool armPinLow = digitalRead(GPIO_ARM_PIN) == LOW;

    // All camera + flight access goes under the mutex (MAJOR-2).
    if (xSemaphoreTake(camMutex, portMAX_DELAY) == pdTRUE) {
        flight.update(now, armPinLow);
        systemState.flightState      = flight.getState();
        systemState.recordingSeconds = flight.getRecordingSeconds(now);
        systemState.autoStopSeconds  = flight.getAutoStopSeconds();
        systemState.autoRestart      = flight.getAutoRestart();
        systemState.cameraCommsOk    = camera.isInitialised();
        xSemaphoreGive(camMutex);
    }
    systemState.armPinLow = armPinLow;

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
