#include "flight/flight_controller.h"

FlightController::FlightController(ICamera& camera) : camera_(camera) {}

void FlightController::update(uint32_t /*nowMs*/) {
    // No arm pin or auto-stop logic. State transitions are driven
    // exclusively by forceStartRecording() / forceStopRecording().
}

FlightState FlightController::getState() const { return state_; }

uint32_t FlightController::getRecordingSeconds(uint32_t nowMs) const {
    if (state_ != FlightState::RECORDING) return 0;
    return (nowMs - recordingStartMs_) / 1000;
}

CameraResult FlightController::forceStartRecording(uint32_t nowMs) {
    if (state_ != FlightState::IDLE) return CameraResult::OK;
    CameraResult result = camera_.startRecording();
    if (result == CameraResult::OK) {
        state_ = FlightState::RECORDING;
        recordingStartMs_ = nowMs;
    }
    return result;
}

CameraResult FlightController::forceStopRecording() {
    if (state_ != FlightState::RECORDING) return CameraResult::OK;
    CameraResult result = camera_.stopRecording();
    if (result == CameraResult::OK) {
        state_ = FlightState::IDLE;
    }
    return result;
}
