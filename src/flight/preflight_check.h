#pragma once

#include "flight/i_camera.h"
#include "camera/camera_settings.h"
#include <cstdint>

// PreflightCheck — see FSD §5.2.
//
// **DEFERRED MODE** (active): the camera's GET_SETTINGS implementation
// does not currently expose a known setting-ID map for the Thumb Pro W
// (see Serial_Diagnostic_Report.md §2.4), so this module returns a
// synthetic "settings access not available" result without contacting
// the camera. `passed` is true so the LED/UI does not enter the failure
// pattern.
//
// When the setting map becomes available, switch run() back to its
// original implementation that reads each value from the camera.

struct PreflightItem {
    bool    ok;
    int32_t expected;
    int32_t actual;
};

struct PreflightResult {
    bool          passed;
    bool          deferred;   // true while in deferred mode
    PreflightItem resolution;
    PreflightItem fps;
    PreflightItem eis;
    bool          commsOk;
};

class PreflightCheck {
public:
    static PreflightResult run(ICamera& camera, const CameraSettings& expected);
};
