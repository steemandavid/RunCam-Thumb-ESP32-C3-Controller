#pragma once

#ifndef UNIT_TEST

#include <ESPAsyncWebServer.h>
#include "camera/runcam_camera.h"
#include "flight/flight_controller.h"
#include "flight/preflight_check.h"
#include "storage/settings_store.h"
#include "web/ws_notifier.h"
#include "web/web_ui.h"

class WebServer {
public:
    void begin(RunCamCamera& camera, FlightController& fc, SettingsStore& store,
               WsNotifier& ws, const PreflightResult& pfResult);
    void update();

private:
    AsyncWebServer server_{80};
    RunCamCamera* camera_ = nullptr;
    FlightController* fc_ = nullptr;
    SettingsStore* store_ = nullptr;
    WsNotifier* ws_ = nullptr;
    const PreflightResult* pfResult_ = nullptr;
};

#endif // UNIT_TEST
