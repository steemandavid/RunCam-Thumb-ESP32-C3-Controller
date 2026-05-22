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

        case FlightState::ARMED:
            break;

        case FlightState::RECORDING: {
            uint32_t elapsed = nowMs - recordingStartMs_;
            if (elapsed < AUTO_STOP_DURATION_MS) break;

            // Back-off: don't hammer the camera with stop attempts faster
            // than once per second. Code_Review_Phase4 MAJOR-3.
            if (lastStopAttemptMs_ != 0 &&
                (nowMs - lastStopAttemptMs_) < AUTO_STOP_RETRY_INTERVAL_MS) {
                break;
            }
            lastStopAttemptMs_ = nowMs;
            stopAttemptCount_++;

            CameraResult r = camera_.stopRecording();
            if (r == CameraResult::OK) {
                // Reset back-off state.
                lastStopAttemptMs_ = 0;
                stopAttemptCount_  = 0;

                if (autoRestart_) {
                    CameraResult sr = camera_.startRecording();
                    if (sr == CameraResult::OK) {
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
            } else if (stopAttemptCount_ >= AUTO_STOP_MAX_RETRIES) {
                // Give up — force back to IDLE and let the operator decide.
                // The camera state may have drifted; an out-of-band signal
                // (LED / current) is the canonical confirmation.
                state_ = FlightState::IDLE;
                armedLatched_ = false;
                armDebounceStart_ = 0;
                lastStopAttemptMs_ = 0;
                stopAttemptCount_  = 0;
            }
            break;
        }

        case FlightState::STOPPING:
            // Reached only via external trigger (forceStopRecording).
            state_ = FlightState::IDLE;
            armedLatched_ = false;
            armDebounceStart_ = 0;
            break;
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
bool FlightController::getAutoRestart() const       { return autoRestart_; }

CameraResult FlightController::forceStartRecording() {
    if (state_ != FlightState::IDLE) return CameraResult::OK;  // already recording
    CameraResult result = camera_.startRecording();
    if (result == CameraResult::OK) {
        state_ = FlightState::RECORDING;
        recordingStartMs_ = 0;
    }
    return result;
}

CameraResult FlightController::forceStopRecording() {
    if (state_ != FlightState::RECORDING) return CameraResult::OK;  // not recording
    CameraResult result = camera_.stopRecording();
    if (result == CameraResult::OK) {
        state_ = FlightState::IDLE;
        armedLatched_ = false;
        armDebounceStart_ = 0;
    }
    return result;
}
