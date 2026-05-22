#ifndef UNIT_TEST

#include "web/ws_notifier.h"
#include <ArduinoJson.h>

void WsNotifier::begin(AsyncWebServer& server) {
    server.addHandler(&ws_);
}

void WsNotifier::broadcastStatus(const SystemState& state) {
    if (ws_.count() == 0) return;
    JsonDocument doc;
    doc["type"] = "status";
    const char* states[] = {"IDLE", "ARMED", "RECORDING", "STOPPING"};
    doc["state"] = states[static_cast<int>(state.flightState)];
    doc["recordingSeconds"] = state.recordingSeconds;
    doc["autoStopSeconds"] = state.autoStopSeconds;
    doc["autoRestart"] = false;
    doc["armPin"] = false;
    doc["cameraCommsOk"] = state.cameraCommsOk;
    String json;
    serializeJson(doc, json);
    ws_.textAll(json);
}

void WsNotifier::broadcastPreflight(const PreflightResult& result) {
    if (ws_.count() == 0) return;
    JsonDocument doc;
    doc["type"] = "preflight";
    doc["passed"] = result.passed;
    doc["checks"]["resolution"]["ok"] = result.resolution.ok;
    doc["checks"]["resolution"]["expected"] = result.resolution.expected;
    doc["checks"]["resolution"]["actual"] = result.resolution.actual;
    doc["checks"]["fps"]["ok"] = result.fps.ok;
    doc["checks"]["fps"]["expected"] = result.fps.expected;
    doc["checks"]["fps"]["actual"] = result.fps.actual;
    doc["checks"]["eis"]["ok"] = result.eis.ok;
    doc["checks"]["eis"]["expected"] = result.eis.expected;
    doc["checks"]["eis"]["actual"] = result.eis.actual;
    String json;
    serializeJson(doc, json);
    ws_.textAll(json);
}

void WsNotifier::broadcastError(const char* code, const char* message) {
    if (ws_.count() == 0) return;
    JsonDocument doc;
    doc["type"] = "error";
    doc["code"] = code;
    doc["message"] = message;
    String json;
    serializeJson(doc, json);
    ws_.textAll(json);
}

void WsNotifier::cleanupClients() {
    ws_.cleanupClients();
}

#endif // UNIT_TEST
