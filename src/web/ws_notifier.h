#pragma once

#ifndef UNIT_TEST

#include <ESPAsyncWebServer.h>
#include "system_state.h"
#include "flight/preflight_check.h"

class WsNotifier {
public:
    void begin(AsyncWebServer& server);
    void broadcastStatus(const SystemState& state);
    void broadcastPreflight(const PreflightResult& result);
    void broadcastError(const char* code, const char* message);
    void cleanupClients();

private:
    AsyncWebSocket ws_{"/ws"};
};

#endif // UNIT_TEST
