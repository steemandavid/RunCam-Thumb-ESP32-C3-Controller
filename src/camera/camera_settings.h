#pragma once

#include <cstdint>

// Setting IDs matching RCSP protocol (§4.5)
enum class SettingId : uint8_t {
    RESOLUTION       = 0x01,
    FPS              = 0x02,
    FOV              = 0x03,
    VIDEO_FORMAT     = 0x04,
    EIS              = 0x05,
    LOOP_RECORDING   = 0x06,
    AUTO_START_REC   = 0x07,
    SHARPNESS        = 0x10,
    EXPOSURE         = 0x11,
    WHITE_BALANCE    = 0x12,
    CONTRAST         = 0x13,
    SATURATION       = 0x14,
    HUE              = 0x15,
};

struct CameraSettings {
    int32_t resolution;
    int32_t fps;
    int32_t fov;
    int32_t videoFormat;
    int32_t eis;
    int32_t loopRecording;
    int32_t autoStartRec;
    int32_t sharpness;
    int32_t exposure;
    int32_t whiteBalance;
    int32_t contrast;
    int32_t saturation;
    int32_t hue;
};

// Validate a setting value against its allowed range.
bool isValidSettingValue(SettingId id, int32_t value);

// Convert SettingId to the RCSP protocol byte.
uint8_t settingIdToByte(SettingId id);

// Get the NVS key name for a setting.
const char* settingIdToNvsKey(SettingId id);
