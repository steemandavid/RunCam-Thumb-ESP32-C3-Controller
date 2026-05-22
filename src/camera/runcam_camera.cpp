#include "camera/runcam_camera.h"
#include "protocol/crc8.h"
#include "config.h"

RunCamCamera::RunCamCamera(IRunCamTransport& transport) : transport_(transport) {}

CameraResult RunCamCamera::begin() {
    CameraResult result = sendRequest(CMD_GET_DEVICE_INFO, nullptr, 0);
    if (result != CameraResult::OK) return result;
    initialised_ = true;
    return CameraResult::OK;
}

bool       RunCamCamera::isInitialised() const     { return initialised_; }
DeviceInfo RunCamCamera::getDeviceInfo() const     { return deviceInfo_; }
uint16_t   RunCamCamera::getFeatureBitmask() const { return featureBitmask_; }
bool       RunCamCamera::hasFeature(uint16_t flag) const { return (featureBitmask_ & flag) != 0; }

// --- Recording: toggled via Power button (action 0x01) per FSD §4.7 ----------

CameraResult RunCamCamera::startRecording() {
    if (!initialised_) return CameraResult::ERROR_NOT_INITIALISED;
    if (!hasFeature(FEAT_SIMULATE_POWER_BUTTON)) return CameraResult::ERROR_NOT_SUPPORTED;
    if (isRecording_) return CameraResult::OK;  // already recording
    CameraResult r = sendAction(ACTION_POWER_BUTTON);
    if (r == CameraResult::OK) isRecording_ = true;
    return r;
}

CameraResult RunCamCamera::stopRecording() {
    if (!initialised_) return CameraResult::ERROR_NOT_INITIALISED;
    if (!hasFeature(FEAT_SIMULATE_POWER_BUTTON)) return CameraResult::ERROR_NOT_SUPPORTED;
    if (!isRecording_) return CameraResult::OK;  // already stopped
    CameraResult r = sendAction(ACTION_POWER_BUTTON);
    if (r == CameraResult::OK) isRecording_ = false;
    return r;
}

CameraResult RunCamCamera::capturePhoto() {
    if (!initialised_) return CameraResult::ERROR_NOT_INITIALISED;
    return sendAction(ACTION_PHOTO);
}

// --- Settings (DEFERRED per FSD §4.5 — kept for future enablement) ----------

CameraResult RunCamCamera::writeSetting(SettingId id, int32_t value) {
    if (!initialised_) return CameraResult::ERROR_NOT_INITIALISED;
    if (!hasFeature(FEAT_DEVICE_SETTINGS_ACCESS)) return CameraResult::ERROR_NOT_SUPPORTED;
    if (!isValidSettingValue(id, value)) return CameraResult::ERROR_NOT_SUPPORTED;

    uint8_t settingByte = settingIdToByte(id);
    uint8_t payload[2] = {settingByte, static_cast<uint8_t>(value)};
    return sendRequest(CMD_WRITE_SETTING, payload, 2);
}

CameraResult RunCamCamera::readSetting(SettingId id, uint8_t& outValue) {
    if (!initialised_) return CameraResult::ERROR_NOT_INITIALISED;
    if (!hasFeature(FEAT_DEVICE_SETTINGS_ACCESS)) return CameraResult::ERROR_NOT_SUPPORTED;

    uint8_t settingByte = settingIdToByte(id);
    // Build, send, parse response. The settings read response format on the
    // Thumb Pro W is currently unknown — see FSD §4.5. Returning the first
    // payload byte of an ACK is a best-effort fallback.
    uint8_t txBuf[MAX_FRAME_SIZE];
    size_t txLen = buildFrame(txBuf, sizeof(txBuf), CMD_GET_SETTINGS, &settingByte, 1);
    if (txLen == 0) return CameraResult::ERROR_TRANSPORT;

    CameraResult result = CameraResult::ERROR_TIMEOUT;
    for (int attempt = 0; attempt < RUNCAM_MAX_RETRIES; attempt++) {
        transport_.flush();
        if (!transport_.send(txBuf, txLen)) { result = CameraResult::ERROR_TRANSPORT; continue; }

        uint8_t rxBuf[MAX_FRAME_SIZE];
        int rxLen = transport_.receive(rxBuf, sizeof(rxBuf), RUNCAM_RESPONSE_TIMEOUT_MS);
        if (rxLen <= 0) { result = CameraResult::ERROR_TIMEOUT; continue; }

        ParsedResponse resp{};
        ParseResult pr = parseResponse(rxBuf, static_cast<size_t>(rxLen), resp);
        if (pr != ParseResult::OK) { result = parseResultToCameraResult(pr); continue; }
        if (resp.kind == ResponseKind::Nak) return nakToResult(resp.errCode);
        if (resp.kind == ResponseKind::Ack) {
            outValue = resp.actionEcho;  // best-effort, see comment above
            return CameraResult::OK;
        }
        return CameraResult::ERROR_HEADER;
    }
    return result;
}

// --- Button-simulation helpers ----------------------------------------------

CameraResult RunCamCamera::simulatePowerButton() {
    if (!initialised_) return CameraResult::ERROR_NOT_INITIALISED;
    if (!hasFeature(FEAT_SIMULATE_POWER_BUTTON)) return CameraResult::ERROR_NOT_SUPPORTED;
    return sendAction(ACTION_POWER_BUTTON);
}

CameraResult RunCamCamera::simulateModeButton() {
    if (!initialised_) return CameraResult::ERROR_NOT_INITIALISED;
    if (!hasFeature(FEAT_CHANGE_MODE)) return CameraResult::ERROR_NOT_SUPPORTED;
    return sendAction(ACTION_MODE_BUTTON);
}

CameraResult RunCamCamera::simulateWifiButton() {
    if (!initialised_) return CameraResult::ERROR_NOT_INITIALISED;
    if (!hasFeature(FEAT_SIMULATE_WIFI_BUTTON)) return CameraResult::ERROR_NOT_SUPPORTED;
    return sendAction(ACTION_WIFI_BUTTON);
}

CameraResult RunCamCamera::simulateKeyPress(uint8_t key) {
    if (!initialised_) return CameraResult::ERROR_NOT_INITIALISED;
    if (!hasFeature(FEAT_SIMULATE_5KEY)) return CameraResult::ERROR_NOT_SUPPORTED;
    return sendRequest(CMD_5KEY_SIMULATION_PRESS, &key, 1);
}

CameraResult RunCamCamera::keyRelease() {
    if (!initialised_) return CameraResult::ERROR_NOT_INITIALISED;
    if (!hasFeature(FEAT_SIMULATE_5KEY)) return CameraResult::ERROR_NOT_SUPPORTED;
    return sendRequest(CMD_5KEY_SIMULATION_RELEASE, nullptr, 0);
}

CameraResult RunCamCamera::connectionEvent(uint8_t event) {
    if (!initialised_) return CameraResult::ERROR_NOT_INITIALISED;
    if (!hasFeature(FEAT_SIMULATE_5KEY)) return CameraResult::ERROR_NOT_SUPPORTED;
    return sendRequest(CMD_5KEY_CONNECTION_EVENT, &event, 1);
}

// --- Internals --------------------------------------------------------------

CameraResult RunCamCamera::sendAction(uint8_t action) {
    return sendRequest(CMD_CAMERA_CONTROL, &action, 1);
}

CameraResult RunCamCamera::sendRequest(uint8_t cmd, const uint8_t* data, size_t dataLen) {
    uint8_t txBuf[MAX_FRAME_SIZE];
    size_t txLen = buildFrame(txBuf, sizeof(txBuf), cmd, data, dataLen);
    if (txLen == 0) return CameraResult::ERROR_TRANSPORT;

    CameraResult result = CameraResult::ERROR_TIMEOUT;
    for (int attempt = 0; attempt < RUNCAM_MAX_RETRIES; attempt++) {
        transport_.flush();
        if (!transport_.send(txBuf, txLen)) {
            result = CameraResult::ERROR_TRANSPORT;
            continue;
        }

        uint8_t rxBuf[MAX_FRAME_SIZE];
        int rxLen = transport_.receive(rxBuf, sizeof(rxBuf), RUNCAM_RESPONSE_TIMEOUT_MS);
        if (rxLen <= 0) { result = CameraResult::ERROR_TIMEOUT; continue; }

        ParsedResponse resp{};
        ParseResult pr = parseResponse(rxBuf, static_cast<size_t>(rxLen), resp);
        if (pr != ParseResult::OK) { result = parseResultToCameraResult(pr); continue; }

        // GET_DEVICE_INFO: cache the result.
        if (cmd == CMD_GET_DEVICE_INFO && resp.kind == ResponseKind::DeviceInfo) {
            deviceInfo_.protocolVersion = resp.protoVer;
            deviceInfo_.featureBitmask  = resp.features;
            featureBitmask_             = resp.features;
            return CameraResult::OK;
        }

        // Anything else: ACK is success, NAK is no-retry rejection.
        if (resp.kind == ResponseKind::Ack) return CameraResult::OK;
        if (resp.kind == ResponseKind::Nak) return nakToResult(resp.errCode);
        return CameraResult::ERROR_HEADER;
    }
    return result;
}

CameraResult RunCamCamera::nakToResult(uint8_t errCode) {
    switch (errCode) {
        case NAK_UNKNOWN_CMD: return CameraResult::REJECTED_UNKNOWN;
        case NAK_WRONG_STATE: return CameraResult::REJECTED_STATE;
        case NAK_INVALID_ARG: return CameraResult::REJECTED_ARG;
        default:              return CameraResult::REJECTED_UNKNOWN;
    }
}

CameraResult RunCamCamera::parseResultToCameraResult(ParseResult pr) {
    switch (pr) {
        case ParseResult::OK:               return CameraResult::OK;
        case ParseResult::ERROR_CRC:        return CameraResult::ERROR_CRC;
        case ParseResult::ERROR_HEADER:     return CameraResult::ERROR_HEADER;
        case ParseResult::ERROR_TIMEOUT:    return CameraResult::ERROR_TIMEOUT;
        case ParseResult::ERROR_INCOMPLETE: return CameraResult::ERROR_INCOMPLETE;
        default:                            return CameraResult::ERROR_TRANSPORT;
    }
}
