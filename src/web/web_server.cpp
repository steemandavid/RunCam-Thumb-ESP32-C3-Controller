#ifndef UNIT_TEST

#include "web/web_server.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>

static const char* stateNames[] = {"IDLE", "RECORDING"};

struct CamLock {
    SemaphoreHandle_t m_;
    explicit CamLock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~CamLock() { xSemaphoreGive(m_); }
};

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
                      WsNotifier& ws, SemaphoreHandle_t mutex) {
    camera_   = &camera;
    fc_       = &fc;
    store_    = &store;
    ws_       = &ws;
    mutex_    = mutex;

    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);

    server_.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html", WEB_UI);
    });

    // ---- GET /api/status ---------------------------------------------------
    server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        CamLock lock(mutex_);
        JsonDocument doc;
        doc["state"]            = stateNames[static_cast<int>(fc_->getState())];
        doc["recordingSeconds"] = fc_->getRecordingSeconds(millis());
        doc["cameraCommsOk"]    = camera_->isInitialised();
        auto s = store_->load();
        doc["autoStartRec"]     = s.autoStartRec;
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
        CameraResult r = fc_->forceStartRecording(millis());
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

    // ---- Settings ----------------------------------------------------------
    server_.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest* request) {
        auto s = store_->load();
        JsonDocument doc;
        doc["ok"]           = true;
        doc["autoStartRec"] = s.autoStartRec;
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    server_.on("/api/settings/autoStartRec", HTTP_POST, [](AsyncWebServerRequest*) {
    }, nullptr, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
        JsonDocument doc;
        deserializeJson(doc, data, len);
        int32_t value = doc["value"].as<int32_t>();
        if (value != 0 && value != 1) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"value must be 0 or 1\"}");
            return;
        }
        store_->saveSetting(SettingId::AUTO_START_REC, value);
        String json = String("{\"ok\":true,\"autoStartRec\":") + value + "}";
        request->send(200, "application/json", json);
    });

    server_.on("/api/settings/reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        store_->resetToDefaults();
        auto s = store_->load();
        JsonDocument doc;
        doc["ok"]           = true;
        doc["autoStartRec"] = s.autoStartRec;
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    ws_->begin(server_);
    server_.begin();
}

void WebServer::update() {
    ws_->cleanupClients();
}

#endif // UNIT_TEST
