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
    void forceStartRecording();
    void forceStopRecording();

private:
    ICamera& camera_;
    FlightState state_ = FlightState::IDLE;
    bool autoRestart_ = false;

    // Debounce
    bool lastArmPin_ = true;
    uint32_t armDebounceStart_ = 0;
    bool armedLatched_ = false;

    // Timer
    uint32_t recordingStartMs_ = 0;
};
