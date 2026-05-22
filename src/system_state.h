#pragma once

#include <cstdint>

enum class FlightState : uint8_t {
    IDLE,
    ARMED,
    RECORDING,
    STOPPING
};

struct SystemState {
    FlightState flightState = FlightState::IDLE;
    uint32_t    recordingSeconds = 0;
    uint32_t    autoStopSeconds = 0;
    bool        preflightPassed = false;
    bool        preflightDone = false;
    bool        preflightDeferred = false;
    bool        cameraCommsOk = false;
    bool        autoRestart = false;
    bool        armPinLow = false;
    char        preflightFailItems[16] = {};
};
