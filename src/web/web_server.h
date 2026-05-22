#pragma once

#ifndef UNIT_TEST

#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "camera/runcam_camera.h"
#include "flight/flight_controller.h"
#include "flight/preflight_check.h"
#include "storage/settings_store.h"
#include "web/ws_notifier.h"
#include "web/web_ui.h"

class WebServer {
public:
    // The mutex guards every call into camera_ and fc_ from web callbacks.
    // The main loop must take the same mutex before calling flight.update()
    // or any camera method. See Code_Review_Phase4 MAJOR-2.
    void begin(RunCamCamera& camera, FlightController& fc, SettingsStore& store,
               WsNotifier& ws, const PreflightResult& pfResult,
               SemaphoreHandle_t mutex);
    void update();

private:
    AsyncWebServer    server_{80};
    RunCamCamera*     camera_   = nullptr;
    FlightController* fc_       = nullptr;
    SettingsStore*    store_    = nullptr;
    WsNotifier*       ws_       = nullptr;
    const PreflightResult* pfResult_ = nullptr;
    SemaphoreHandle_t mutex_    = nullptr;
};

#endif // UNIT_TEST
