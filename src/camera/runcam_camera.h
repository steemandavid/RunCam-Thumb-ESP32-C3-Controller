#pragma once

#include "transport/i_transport.h"
#include "protocol/runcam_protocol.h"
#include "camera/camera_settings.h"
#include "camera/camera_result.h"
#include "flight/i_camera.h"
#include <cstdint>

// Feature bitmask bits — uint16_t per FSD §4.4
constexpr uint16_t FEAT_SIMULATE_POWER_BUTTON  = (1u << 0);
constexpr uint16_t FEAT_SIMULATE_WIFI_BUTTON   = (1u << 1);
constexpr uint16_t FEAT_CHANGE_MODE            = (1u << 2);
constexpr uint16_t FEAT_SIMULATE_5KEY          = (1u << 3);
constexpr uint16_t FEAT_DEVICE_SETTINGS_ACCESS = (1u << 4);
constexpr uint16_t FEAT_DISPLAY_PORT           = (1u << 5);
constexpr uint16_t FEAT_START_RECORDING        = (1u << 6);
constexpr uint16_t FEAT_STOP_RECORDING         = (1u << 7);

class RunCamCamera : public ICamera {
public:
    explicit RunCamCamera(IRunCamTransport& transport);

    CameraResult begin();
    bool         isInitialised() const;
    DeviceInfo   getDeviceInfo() const;
    uint16_t     getFeatureBitmask() const;
    bool         hasFeature(uint16_t flag) const;

    // Recording is toggled via action 0x01 (Power button) per FSD §4.7.
    // The explicit Start (0x03) / Stop (0x04) actions are unreliable on
    // the Thumb Pro W firmware.
    CameraResult startRecording() override;
    CameraResult stopRecording() override;
    bool         isRecording() const { return isRecording_; }

    CameraResult capturePhoto();

    CameraResult writeSetting(SettingId id, int32_t value);
    CameraResult readSetting(SettingId id, uint8_t& outValue) override;

    CameraResult simulatePowerButton();
    CameraResult simulateModeButton();
    CameraResult simulateWifiButton();
    CameraResult simulateKeyPress(uint8_t key);
    CameraResult keyRelease();
    CameraResult connectionEvent(uint8_t event);

private:
    IRunCamTransport& transport_;
    DeviceInfo deviceInfo_{};
    uint16_t   featureBitmask_ = 0;
    bool       initialised_   = false;
    bool       isRecording_   = false;  // host-tracked, toggled by Power button

    // Send a request and wait for a single response frame.
    // Returns CameraResult::OK only for explicit acceptance:
    //   - GET_DEVICE_INFO returning a DeviceInfo response
    //   - Anything else returning an ACK
    // Returns one of the REJECTED_* codes for NAK responses (no retry).
    // Returns ERROR_TIMEOUT/CRC/etc. on transport failures (retried up to MAX_RETRIES).
    CameraResult sendRequest(uint8_t cmd, const uint8_t* data, size_t dataLen);

    // Send a CAMERA_CONTROL action with the given action byte.
    CameraResult sendAction(uint8_t action);

    static CameraResult nakToResult(uint8_t errCode);
    static CameraResult parseResultToCameraResult(ParseResult pr);
};
