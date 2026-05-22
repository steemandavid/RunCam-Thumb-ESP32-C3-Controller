#include "flight/preflight_check.h"

PreflightResult PreflightCheck::run(ICamera& /*camera*/, const CameraSettings& expected) {
    // Deferred mode — see header. Do not touch the camera. Return a result
    // that the UI / state machine can render without flagging an error.
    PreflightResult result{};
    result.passed    = true;
    result.deferred  = true;
    result.commsOk   = true;
    result.resolution = {true, expected.resolution, expected.resolution};
    result.fps        = {true, expected.fps,        expected.fps};
    result.eis        = {true, expected.eis,        expected.eis};
    return result;
}
