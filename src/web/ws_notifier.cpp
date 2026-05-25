#ifndef UNIT_TEST

#include "web/ws_notifier.h"
#include <ArduinoJson.h>

void WsNotifier::begin(AsyncWebServer& server) {
    server.addHandler(&ws_);
}

void WsNotifier::broadcastStatus(const SystemState& state) {
    if (ws_.count() == 0) return;
    JsonDocument doc;
    doc["type"]             = "status";
    const char* states[]    = {"IDLE", "RECORDING"};
    doc["state"]            = states[static_cast<int>(state.flightState)];
    doc["recordingSeconds"] = state.recordingSeconds;
    doc["cameraCommsOk"]    = state.cameraCommsOk;
    String json;
    serializeJson(doc, json);
    ws_.textAll(json);
}

void WsNotifier::broadcastError(const char* code, const char* message) {
    if (ws_.count() == 0) return;
    JsonDocument doc;
    doc["type"]    = "error";
    doc["code"]    = code;
    doc["message"] = message;
    String json;
    serializeJson(doc, json);
    ws_.textAll(json);
}

void WsNotifier::cleanupClients() {
    ws_.cleanupClients();
}

#endif // UNIT_TEST
