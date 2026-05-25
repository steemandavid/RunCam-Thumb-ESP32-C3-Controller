#include "storage/settings_store.h"
#include "config.h"
#include <cstring>

SettingsStore::SettingsStore(IPreferences& prefs) : prefs_(prefs) {}

void SettingsStore::begin() {
    prefs_.begin(NVS_NAMESPACE, false);
    if (!prefs_.isKey("init2")) {
        if (prefs_.isKey("init")) prefs_.remove("init");
        writeDefaults();
        prefs_.putInt("init2", 1);
    }
    initialised_ = true;
}

CameraSettings SettingsStore::load() {
    CameraSettings s{};
    s.resolution    = prefs_.getInt("res", DEFAULT_RESOLUTION);
    s.fps           = prefs_.getInt("fps", DEFAULT_FPS);
    s.fov           = prefs_.getInt("fov", DEFAULT_FOV);
    s.videoFormat   = prefs_.getInt("fmt", DEFAULT_VIDEO_FORMAT);
    s.eis           = prefs_.getInt("eis", DEFAULT_EIS);
    s.loopRecording = prefs_.getInt("loop", DEFAULT_LOOP_RECORDING);
    s.autoStartRec  = prefs_.getInt("autostart", DEFAULT_AUTO_START_REC);
    s.sharpness     = prefs_.getInt("sharp", DEFAULT_SHARPNESS);
    s.exposure      = prefs_.getInt("exposure", DEFAULT_EXPOSURE);
    s.whiteBalance  = prefs_.getInt("wb", DEFAULT_WHITE_BALANCE);
    s.contrast      = prefs_.getInt("contrast", DEFAULT_CONTRAST);
    s.saturation    = prefs_.getInt("saturation", DEFAULT_SATURATION);
    s.hue           = prefs_.getInt("hue", DEFAULT_HUE);
    return s;
}

bool SettingsStore::save(const CameraSettings& settings) {
    bool ok = true;
    ok &= prefs_.putInt("res", settings.resolution) > 0;
    ok &= prefs_.putInt("fps", settings.fps) > 0;
    ok &= prefs_.putInt("fov", settings.fov) > 0;
    ok &= prefs_.putInt("fmt", settings.videoFormat) > 0;
    ok &= prefs_.putInt("eis", settings.eis) > 0;
    ok &= prefs_.putInt("loop", settings.loopRecording) > 0;
    ok &= prefs_.putInt("autostart", settings.autoStartRec) > 0;
    ok &= prefs_.putInt("sharp", settings.sharpness) > 0;
    ok &= prefs_.putInt("exposure", settings.exposure) > 0;
    ok &= prefs_.putInt("wb", settings.whiteBalance) > 0;
    ok &= prefs_.putInt("contrast", settings.contrast) > 0;
    ok &= prefs_.putInt("saturation", settings.saturation) > 0;
    ok &= prefs_.putInt("hue", settings.hue) > 0;
    return ok;
}

bool SettingsStore::saveSetting(SettingId id, int32_t value) {
    if (!isValidSettingValue(id, value)) return false;
    const char* key = settingIdToNvsKey(id);
    if (key[0] == '\0') return false;
    return prefs_.putInt(key, value) > 0;
}

void SettingsStore::resetToDefaults() {
    writeDefaults();
}

void SettingsStore::writeDefaults() {
    prefs_.putInt("res", DEFAULT_RESOLUTION);
    prefs_.putInt("fps", DEFAULT_FPS);
    prefs_.putInt("fov", DEFAULT_FOV);
    prefs_.putInt("fmt", DEFAULT_VIDEO_FORMAT);
    prefs_.putInt("eis", DEFAULT_EIS);
    prefs_.putInt("loop", DEFAULT_LOOP_RECORDING);
    prefs_.putInt("autostart", DEFAULT_AUTO_START_REC);
    prefs_.putInt("sharp", DEFAULT_SHARPNESS);
    prefs_.putInt("exposure", DEFAULT_EXPOSURE);
    prefs_.putInt("wb", DEFAULT_WHITE_BALANCE);
    prefs_.putInt("contrast", DEFAULT_CONTRAST);
    prefs_.putInt("saturation", DEFAULT_SATURATION);
    prefs_.putInt("hue", DEFAULT_HUE);
}
