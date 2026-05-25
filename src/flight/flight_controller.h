#pragma once

#include "flight/i_camera.h"
#include "system_state.h"
#include <cstdint>

class FlightController {
public:
    explicit FlightController(ICamera& camera);

    void update(uint32_t nowMs);
    FlightState getState() const;
    uint32_t getRecordingSeconds(uint32_t nowMs) const;

    CameraResult forceStartRecording(uint32_t nowMs = 0);    CameraResult forceStopRecording();

private:
    ICamera& camera_;
    FlightState state_ = FlightState::IDLE;
    uint32_t recordingStartMs_ = 0;
};
