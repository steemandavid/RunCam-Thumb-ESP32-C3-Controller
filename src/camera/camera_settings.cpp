#include "camera/camera_settings.h"

bool isValidSettingValue(SettingId id, int32_t value) {
    switch (id) {
        case SettingId::RESOLUTION:     return value >= 0 && value <= 4;
        case SettingId::FPS:            return value >= 0 && value <= 3;
        case SettingId::FOV:            return value >= 0 && value <= 2;
        case SettingId::VIDEO_FORMAT:   return value >= 0 && value <= 1;
        case SettingId::EIS:            return value == 0 || value == 1;
        case SettingId::LOOP_RECORDING: return value == 0 || value == 1;
        case SettingId::AUTO_START_REC: return value == 0 || value == 1;
        case SettingId::SHARPNESS:      return value >= 0 && value <= 2;
        case SettingId::EXPOSURE:       return value >= -2 && value <= 2;
        case SettingId::WHITE_BALANCE:  return value >= 0 && value <= 7;
        case SettingId::CONTRAST:       return value >= 0 && value <= 2;
        case SettingId::SATURATION:     return value >= 0 && value <= 2;
        case SettingId::HUE:            return value >= -180 && value <= 180;
        default:                        return false;
    }
}

uint8_t settingIdToByte(SettingId id) {
    return static_cast<uint8_t>(id);
}

const char* settingIdToNvsKey(SettingId id) {
    switch (id) {
        case SettingId::RESOLUTION:     return "res";
        case SettingId::FPS:            return "fps";
        case SettingId::FOV:            return "fov";
        case SettingId::VIDEO_FORMAT:   return "fmt";
        case SettingId::EIS:            return "eis";
        case SettingId::LOOP_RECORDING: return "loop";
        case SettingId::AUTO_START_REC: return "autostart";
        case SettingId::SHARPNESS:      return "sharp";
        case SettingId::EXPOSURE:       return "exposure";
        case SettingId::WHITE_BALANCE:  return "wb";
        case SettingId::CONTRAST:       return "contrast";
        case SettingId::SATURATION:     return "saturation";
        case SettingId::HUE:            return "hue";
        default:                        return "";
    }
}
