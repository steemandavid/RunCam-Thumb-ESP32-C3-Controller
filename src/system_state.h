#pragma once

#include <cstdint>

enum class FlightState : uint8_t {
    IDLE,
    RECORDING
};

struct SystemState {
    FlightState flightState = FlightState::IDLE;
    uint32_t    recordingSeconds = 0;
    bool        cameraCommsOk = false;
    bool        autoStartRec = false;
};
