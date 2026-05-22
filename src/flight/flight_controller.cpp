#include "flight/flight_controller.h"

FlightController::FlightController(ICamera& camera) : camera_(camera) {}

void FlightController::update(uint32_t nowMs, bool armPinLow) {
    switch (state_) {
        case FlightState::IDLE: {
            bool armAsserted = false;
            if (!armPinLow) {
                armedLatched_ = false;
                armDebounceStart_ = 0;
            } else if (!armedLatched_) {
                if (armDebounceStart_ == 0) {
                    armDebounceStart_ = nowMs;
                }
                if (nowMs - armDebounceStart_ >= ARM_DEBOUNCE_MS) {
                    armedLatched_ = true;
                    armAsserted = true;
                }
            }

            if (armAsserted) {
                CameraResult result = camera_.startRecording();
                if (result == CameraResult::OK) {
                    state_ = FlightState::RECORDING;
                    recordingStartMs_ = nowMs;
                }
            }
            break;
        }

        case FlightState::ARMED: {
            break;
        }

        case FlightState::RECORDING: {
            uint32_t elapsed = nowMs - recordingStartMs_;
            if (elapsed >= AUTO_STOP_DURATION_MS) {
                camera_.stopRecording();
                if (autoRestart_) {
                    CameraResult result = camera_.startRecording();
                    if (result == CameraResult::OK) {
                        state_ = FlightState::RECORDING;
                        recordingStartMs_ = nowMs;
                    } else {
                        state_ = FlightState::IDLE;
                    }
                } else {
                    state_ = FlightState::IDLE;
                    armedLatched_ = false;
                    armDebounceStart_ = 0;
                }
            }
            break;
        }

        case FlightState::STOPPING: {
            // Reached only via external trigger (forceStopRecording)
            state_ = FlightState::IDLE;
            armedLatched_ = false;
            armDebounceStart_ = 0;
            break;
        }
    }
}

FlightState FlightController::getState() const { return state_; }

uint32_t FlightController::getRecordingSeconds(uint32_t nowMs) const {
    if (state_ != FlightState::RECORDING) return 0;
    return (nowMs - recordingStartMs_) / 1000;
}

uint32_t FlightController::getAutoStopSeconds() const {
    return AUTO_STOP_DURATION_MS / 1000;
}

void FlightController::setAutoRestart(bool enabled) { autoRestart_ = enabled; }
bool FlightController::getAutoRestart() const { return autoRestart_; }

void FlightController::forceStartRecording() {
    if (state_ == FlightState::IDLE) {
        CameraResult result = camera_.startRecording();
        if (result == CameraResult::OK) {
            state_ = FlightState::RECORDING;
            recordingStartMs_ = 0;
        }
    }
}

void FlightController::forceStopRecording() {
    if (state_ == FlightState::RECORDING) {
        camera_.stopRecording();
        state_ = FlightState::IDLE;
        armedLatched_ = false;
        armDebounceStart_ = 0;
    }
}
