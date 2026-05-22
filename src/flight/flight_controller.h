#pragma once

#include "flight/i_camera.h"
#include "system_state.h"
#include "config.h"
#include <cstdint>

class FlightController {
public:
    explicit FlightController(ICamera& camera);

    void update(uint32_t nowMs, bool armPinLow);
    FlightState getState() const;
    uint32_t getRecordingSeconds(uint32_t nowMs) const;
    uint32_t getAutoStopSeconds() const;
    void setAutoRestart(bool enabled);
    bool getAutoRestart() const;

    // Return the underlying CameraResult so callers (e.g. REST handlers)
    // can surface NAK rejections to the user rather than falsely reporting
    // success.
    CameraResult forceStartRecording();
    CameraResult forceStopRecording();

    // Auto-stop retry / back-off configuration (see update() implementation).
    static constexpr uint32_t AUTO_STOP_RETRY_INTERVAL_MS = 1000;
    static constexpr int      AUTO_STOP_MAX_RETRIES       = 5;

private:
    ICamera& camera_;
    FlightState state_ = FlightState::IDLE;
    bool autoRestart_ = false;

    // ARM-pin debounce
    bool     lastArmPin_ = true;
    uint32_t armDebounceStart_ = 0;
    bool     armedLatched_ = false;

    // Recording timer
    uint32_t recordingStartMs_ = 0;

    // Auto-stop back-off: when the camera NAKs the stop attempt we retry no
    // faster than AUTO_STOP_RETRY_INTERVAL_MS, and give up after
    // AUTO_STOP_MAX_RETRIES — see Code_Review_Phase4 MAJOR-3.
    uint32_t lastStopAttemptMs_ = 0;
    int      stopAttemptCount_  = 0;
};
