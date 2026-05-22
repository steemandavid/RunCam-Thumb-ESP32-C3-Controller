#ifndef UNIT_TEST

#include "web/web_server.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>

static const char* stateNames[] = {"IDLE", "ARMED", "RECORDING", "STOPPING"};

// RAII helper — acquires the camera/flight mutex on construction.
struct CamLock {
    SemaphoreHandle_t m_;
    explicit CamLock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~CamLock() { xSemaphoreGive(m_); }
};

// Map a CameraResult to a short JSON-friendly error string. Returns nullptr
// on OK so the caller can detect success.
static const char* cameraResultErrorText(CameraResult r) {
    switch (r) {
        case CameraResult::OK:                    return nullptr;
        case CameraResult::ERROR_TIMEOUT:         return "camera timeout";
        case CameraResult::ERROR_CRC:             return "crc mismatch";
        case CameraResult::ERROR_HEADER:          return "bad response header";
        case CameraResult::ERROR_INCOMPLETE:      return "incomplete response";
        case CameraResult::ERROR_NOT_SUPPORTED:   return "feature not supported";
        case CameraResult::ERROR_TRANSPORT:       return "transport error";
        case CameraResult::ERROR_NOT_INITIALISED: return "camera not initialised";
        case CameraResult::REJECTED_STATE:        return "camera rejected (wrong state)";
        case CameraResult::REJECTED_ARG:          return "camera rejected (invalid arg)";
        case CameraResult::REJECTED_UNKNOWN:      return "camera rejected (unknown)";
    }
    return "unknown error";
}

static String okOrError(CameraResult r) {
    const char* err = cameraResultErrorText(r);
    if (err == nullptr) return String("{\"ok\":true}");
    String s = "{\"ok\":false,\"error\":\"";
    s += err;
    s += "\"}";
    return s;
}

void WebServer::begin(RunCamCamera& camera, FlightController& fc, SettingsStore& store,
                      WsNotifier& ws, const PreflightResult& pfResult,
                      SemaphoreHandle_t mutex) {
    camera_   = &camera;
    fc_       = &fc;
    store_    = &store;
    ws_       = &ws;
    pfResult_ = &pfResult;
    mutex_    = mutex;

    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);

    // ---- Static web UI -----------------------------------------------------
    server_.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html", WEB_UI);
    });

    // ---- GET /api/status ---------------------------------------------------
    server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        CamLock lock(mutex_);
        JsonDocument doc;
        doc["state"]            = stateNames[static_cast<int>(fc_->getState())];
        doc["recordingSeconds"] = fc_->getRecordingSeconds(millis());
        doc["autoStopSeconds"]  = fc_->getAutoStopSeconds();
        doc["autoRestart"]      = fc_->getAutoRestart();
        doc["armPin"]           = digitalRead(GPIO_ARM_PIN) == LOW;
        doc["cameraCommsOk"]    = camera_->isInitialised();
        auto pf = doc["preflight"].to<JsonObject>();
        pf["passed"]   = pfResult_->passed;
        pf["deferred"] = pfResult_->deferred;
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // ---- GET /api/device ---------------------------------------------------
    server_.on("/api/device", HTTP_GET, [this](AsyncWebServerRequest* request) {
        CamLock lock(mutex_);
        JsonDocument doc;
        auto info = camera_->getDeviceInfo();
        doc["protocolVersion"] = info.protocolVersion;
        char hex[8];
        snprintf(hex, sizeof(hex), "0x%04X", info.featureBitmask);
        doc["featureBitmask"] = hex;
        auto feat = doc["features"].to<JsonObject>();
        feat["simulatePowerButton"]  = camera_->hasFeature(FEAT_SIMULATE_POWER_BUTTON);
        feat["simulateWifiButton"]   = camera_->hasFeature(FEAT_SIMULATE_WIFI_BUTTON);
        feat["changeMode"]           = camera_->hasFeature(FEAT_CHANGE_MODE);
        feat["simulate5Key"]         = camera_->hasFeature(FEAT_SIMULATE_5KEY);
        feat["deviceSettingsAccess"] = camera_->hasFeature(FEAT_DEVICE_SETTINGS_ACCESS);
        feat["displayPort"]          = camera_->hasFeature(FEAT_DISPLAY_PORT);
        feat["startRecording"]       = camera_->hasFeature(FEAT_START_RECORDING);
        feat["stopRecording"]        = camera_->hasFeature(FEAT_STOP_RECORDING);
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // ---- Recording control -------------------------------------------------
    server_.on("/api/record/start", HTTP_POST, [this](AsyncWebServerRequest* request) {
        CamLock lock(mutex_);
        CameraResult r = fc_->forceStartRecording();
        request->send(200, "application/json", okOrError(r));
    });
    server_.on("/api/record/stop", HTTP_POST, [this](AsyncWebServerRequest* request) {
        CamLock lock(mutex_);
        CameraResult r = fc_->forceStopRecording();
        request->send(200, "application/json", okOrError(r));
    });

    // ---- Photo + button simulation ----------------------------------------
    server_.on("/api/photo", HTTP_POST, [this](AsyncWebServerRequest* request) {
        CamLock lock(mutex_);
        CameraResult r = camera_->capturePhoto();
        request->send(200, "application/json", okOrError(r));
    });

    server_.on("/api/button", HTTP_POST, [](AsyncWebServerRequest*) {
    }, nullptr, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
        JsonDocument doc;
        deserializeJson(doc, data, len);
        String btn = doc["button"].as<String>();
        CamLock lock(mutex_);
        CameraResult r = CameraResult::ERROR_NOT_SUPPORTED;
        if      (btn == "power")   r = camera_->simulatePowerButton();
        else if (btn == "mode")    r = camera_->simulateModeButton();
        else if (btn == "wifi")    r = camera_->simulateWifiButton();
        else if (btn == "up")      r = camera_->simulateKeyPress(0x01);
        else if (btn == "down")    r = camera_->simulateKeyPress(0x04);
        else if (btn == "left")    r = camera_->simulateKeyPress(0x02);
        else if (btn == "right")   r = camera_->simulateKeyPress(0x03);
        else if (btn == "confirm") r = camera_->simulateKeyPress(0x05);
        request->send(200, "application/json", okOrError(r));
    });

    // ---- Arm override -----------------------------------------------------
    // /api/arm and /api/disarm just forward to the flight controller; the
    // response carries the real resulting state, not a hardcoded label.
    server_.on("/api/arm", HTTP_POST, [this](AsyncWebServerRequest* request) {
        CamLock lock(mutex_);
        CameraResult r = fc_->forceStartRecording();
        if (r != CameraResult::OK) {
            request->send(200, "application/json", okOrError(r));
            return;
        }
        String body = String("{\"ok\":true,\"state\":\"") +
                      stateNames[static_cast<int>(fc_->getState())] + "\"}";
        request->send(200, "application/json", body);
    });
    server_.on("/api/disarm", HTTP_POST, [this](AsyncWebServerRequest* request) {
        CamLock lock(mutex_);
        CameraResult r = fc_->forceStopRecording();
        if (r != CameraResult::OK) {
            request->send(200, "application/json", okOrError(r));
            return;
        }
        String body = String("{\"ok\":true,\"state\":\"") +
                      stateNames[static_cast<int>(fc_->getState())] + "\"}";
        request->send(200, "application/json", body);
    });

    // ---- Auto-restart toggle ----------------------------------------------
    server_.on("/api/auto-restart", HTTP_POST, [](AsyncWebServerRequest*) {
    }, nullptr, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
        JsonDocument doc;
        deserializeJson(doc, data, len);
        bool enabled = doc["enabled"].as<bool>();
        {
            CamLock lock(mutex_);
            fc_->setAutoRestart(enabled);
        }
        String body = String("{\"ok\":true,\"autoRestart\":") +
                      (enabled ? "true" : "false") + "}";
        request->send(200, "application/json", body);
    });

    // ---- Settings endpoints — deferred per FSD §4.5 / §8.5 ----------------
    static const char* SETTINGS_DEFERRED_BODY =
        "{\"ok\":false,\"error\":\"settings access deferred\"}";
    server_.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(503, "application/json", SETTINGS_DEFERRED_BODY);
    });
    server_.on("^/api/settings/(.+)$", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(503, "application/json", SETTINGS_DEFERRED_BODY);
    });
    server_.on("/api/settings/reset", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(503, "application/json", SETTINGS_DEFERRED_BODY);
    });

    ws_->begin(server_);
    server_.begin();
}

void WebServer::update() {
    ws_->cleanupClients();
}

#endif // UNIT_TEST
