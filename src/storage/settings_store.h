#pragma once

#include "camera/camera_settings.h"
#include <cstdint>
#include <cstddef>

// Abstract interface for NVS-like key/value storage.
// Implemented by real Preferences on ESP32 and MockPreferences in tests.
class IPreferences {
public:
    virtual ~IPreferences() = default;
    virtual bool begin(const char* name, bool readOnly) = 0;
    virtual void end() = 0;
    virtual bool isKey(const char* key) = 0;
    virtual int32_t getInt(const char* key, int32_t defaultValue = 0) = 0;
    virtual size_t putInt(const char* key, int32_t value) = 0;
    virtual bool remove(const char* key) = 0;
};

class SettingsStore {
public:
    explicit SettingsStore(IPreferences& prefs);
    void begin();
    CameraSettings load();
    bool save(const CameraSettings& settings);
    bool saveSetting(SettingId id, int32_t value);
    void resetToDefaults();

private:
    IPreferences& prefs_;
    bool initialised_ = false;
    void writeDefaults();
};
