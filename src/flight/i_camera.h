#pragma once

#include "camera/camera_result.h"
#include "camera/camera_settings.h"
#include <cstdint>

class ICamera {
public:
    virtual ~ICamera() = default;
    virtual CameraResult startRecording() = 0;
    virtual CameraResult stopRecording() = 0;
    virtual CameraResult readSetting(SettingId id, uint8_t& outValue) = 0;
    virtual bool         isRecording() const = 0;
    virtual CameraResult pollRecordingState() = 0;
};
