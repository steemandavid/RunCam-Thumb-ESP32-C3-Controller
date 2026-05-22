#ifndef UNIT_TEST

#include "web/web_server.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>

static const char* stateNames[] = {"IDLE", "ARMED", "RECORDING", "STOPPING"};

void WebServer::begin(RunCamCamera& camera, FlightController& fc, SettingsStore& store,
                       WsNotifier& ws, const PreflightResult& pfResult) {
    camera_ = &camera;
    fc_ = &fc;
    store_ = &store;
    ws_ = &ws;
    pfResult_ = &pfResult;

    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);

    // Serve web UI
    server_.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html", WEB_UI);
    });

    // GET /api/status
    server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["state"] = stateNames[static_cast<int>(fc_->getState())];
        doc["recordingSeconds"] = fc_->getRecordingSeconds(millis());
        doc["autoStopSeconds"] = fc_->getAutoStopSeconds();
        doc["autoRestart"] = fc_->getAutoRestart();
        doc["armPin"] = digitalRead(GPIO_ARM_PIN) == LOW;
        doc["cameraCommsOk"] = camera_->isInitialised();
        auto pf = doc["preflight"].to<JsonObject>();
        pf["passed"]   = pfResult_->passed;
        pf["deferred"] = pfResult_->deferred;
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // GET /api/device
    server_.on("/api/device", HTTP_GET, [this](AsyncWebServerRequest* request) {
        JsonDocument doc;
        auto info = camera_->getDeviceInfo();
        doc["protocolVersion"] = info.protocolVersion;
        // 16-bit features formatted as 0xNNNN hex string for readability.
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

    // POST /api/record/start
    server_.on("/api/record/start", HTTP_POST, [this](AsyncWebServerRequest* request) {
        fc_->forceStartRecording();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // POST /api/record/stop
    server_.on("/api/record/stop", HTTP_POST, [this](AsyncWebServerRequest* request) {
        fc_->forceStopRecording();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // POST /api/photo
    server_.on("/api/photo", HTTP_POST, [this](AsyncWebServerRequest* request) {
        CameraResult r = camera_->capturePhoto();
        request->send(200, "application/json", r == CameraResult::OK ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    // POST /api/button
    server_.on("/api/button", HTTP_POST, [](AsyncWebServerRequest* request) {
    }, nullptr, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
        JsonDocument doc;
        deserializeJson(doc, data, len);
        String btn = doc["button"].as<String>();
        CameraResult r = CameraResult::ERROR_NOT_SUPPORTED;
        if (btn == "power") r = camera_->simulatePowerButton();
        else if (btn == "mode") r = camera_->simulateModeButton();
        else if (btn == "up") r = camera_->simulateKeyPress(0x01);
        else if (btn == "down") r = camera_->simulateKeyPress(0x04);
        else if (btn == "left") r = camera_->simulateKeyPress(0x02);
        else if (btn == "right") r = camera_->simulateKeyPress(0x03);
        else if (btn == "confirm") r = camera_->simulateKeyPress(0x05);
        request->send(200, "application/json", r == CameraResult::OK ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    // POST /api/arm
    server_.on("/api/arm", HTTP_POST, [this](AsyncWebServerRequest* request) {
        fc_->forceStartRecording();
        request->send(200, "application/json", "{\"ok\":true,\"state\":\"ARMED\"}");
    });

    // POST /api/disarm
    server_.on("/api/disarm", HTTP_POST, [this](AsyncWebServerRequest* request) {
        fc_->forceStopRecording();
        request->send(200, "application/json", "{\"ok\":true,\"state\":\"IDLE\"}");
    });

    // POST /api/auto-restart
    server_.on("/api/auto-restart", HTTP_POST, [](AsyncWebServerRequest* request) {
    }, nullptr, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
        JsonDocument doc;
        deserializeJson(doc, data, len);
        fc_->setAutoRestart(doc["enabled"].as<bool>());
        String json = String("{\"ok\":true,\"autoRestart\":") + (doc["enabled"].as<bool>() ? "true" : "false") + "}";
        request->send(200, "application/json", json);
    });

    // Settings endpoints — DEFERRED per FSD §4.5 / §8.5. All return 503 until
    // the Thumb Pro W's setting-ID map is documented.
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
