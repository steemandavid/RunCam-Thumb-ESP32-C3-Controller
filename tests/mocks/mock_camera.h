#pragma once

#include "flight/i_camera.h"
#include <vector>
#include <cstdint>

class MockCamera : public ICamera {
public:
    CameraResult startRecordingResult = CameraResult::OK;
    CameraResult stopRecordingResult = CameraResult::OK;
    CameraResult pollRecordingStateResult = CameraResult::OK;
    bool         recordingState = false;

    // Per-setting read results
    uint8_t resolutionValue = 0;
    uint8_t fpsValue = 0;
    uint8_t eisValue = 0;
    bool readSettingFail = false;

    CameraResult startRecording() override {
        startRecordingCalled++;
        if (startRecordingResult == CameraResult::OK) recordingState = true;
        return startRecordingResult;
    }

    CameraResult stopRecording() override {
        stopRecordingCalled++;
        if (stopRecordingResult == CameraResult::OK) recordingState = false;
        return stopRecordingResult;
    }

    bool isRecording() const override {
        return recordingState;
    }

    CameraResult pollRecordingState() override {
        pollRecordingStateCalled++;
        return pollRecordingStateResult;
    }

    CameraResult readSetting(SettingId id, uint8_t& outValue) override {
        readSettingCalls.push_back(id);
        if (readSettingFail) return CameraResult::ERROR_TIMEOUT;
        switch (id) {
            case SettingId::RESOLUTION: outValue = resolutionValue; break;
            case SettingId::FPS:        outValue = fpsValue; break;
            case SettingId::EIS:        outValue = eisValue; break;
            default: outValue = 0; break;
        }
        return CameraResult::OK;
    }

    int startRecordingCalled = 0;
    int stopRecordingCalled = 0;
    int pollRecordingStateCalled = 0;
    std::vector<SettingId> readSettingCalls;
};
