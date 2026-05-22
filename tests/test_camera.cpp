#include "unity.h"
#include "camera/runcam_camera.h"
#include "mocks/mock_transport.h"
#include "protocol/crc8.h"
#include <cstdint>
#include <vector>

void setUp() {}
void tearDown() {}

// --- Response builders matching the wire format observed on a Thumb Pro W ---

static std::vector<uint8_t> deviceInfo(uint8_t proto, uint16_t features) {
    std::vector<uint8_t> f = {0xCC, proto,
                              (uint8_t)(features & 0xFF),
                              (uint8_t)(features >> 8)};
    f.push_back(crc8_dvb_s2(f.data(), f.size()));
    return f;
}

static std::vector<uint8_t> ack(uint8_t actionEcho) {
    std::vector<uint8_t> f = {0x55, 0x06, 0x01, 0x00, actionEcho};
    f.push_back(crc8_dvb_s2(f.data(), f.size()));
    return f;
}

static std::vector<uint8_t> nak(uint8_t errCode) {
    std::vector<uint8_t> f = {0x55, 0x05, 0xFF, errCode};
    f.push_back(crc8_dvb_s2(f.data(), f.size()));
    return f;
}

// Features value used for "fully capable" tests — Thumb Pro W reports 0x0077.
static constexpr uint16_t ALL_FEATURES = 0x00FF;
static constexpr uint16_t THUMB_PRO_W_FEATURES = 0x0077;

// --- begin() ---------------------------------------------------------------

void test_begin_success() {
    MockTransport t;
    t.enqueueResponse(deviceInfo(1, THUMB_PRO_W_FEATURES));
    RunCamCamera c(t);
    TEST_ASSERT_EQUAL(CameraResult::OK, c.begin());
    TEST_ASSERT_TRUE(c.isInitialised());
    TEST_ASSERT_EQUAL_UINT16(0x0077, c.getFeatureBitmask());
    TEST_ASSERT_EQUAL_UINT8(1, c.getDeviceInfo().protocolVersion);
}

void test_begin_timeout() {
    MockTransport t;
    RunCamCamera c(t);
    TEST_ASSERT_EQUAL(CameraResult::ERROR_TIMEOUT, c.begin());
    TEST_ASSERT_FALSE(c.isInitialised());
}

void test_begin_crc_error_then_success() {
    MockTransport t;
    t.enqueueResponse({0xCC, 0x01, 0x77, 0x00, 0xFF});  // bad CRC
    t.enqueueResponse(deviceInfo(1, THUMB_PRO_W_FEATURES));
    RunCamCamera c(t);
    TEST_ASSERT_EQUAL(CameraResult::OK, c.begin());
}

// --- Recording via Power-toggle --------------------------------------------

void test_recording_toggle_via_power() {
    MockTransport t;
    t.enqueueResponse(deviceInfo(1, ALL_FEATURES));
    t.enqueueResponse(ack(0x01));  // first Power → start
    t.enqueueResponse(ack(0x01));  // second Power → stop
    RunCamCamera c(t);
    c.begin();

    TEST_ASSERT_FALSE(c.isRecording());
    TEST_ASSERT_EQUAL(CameraResult::OK, c.startRecording());
    TEST_ASSERT_TRUE(c.isRecording());
    TEST_ASSERT_EQUAL(CameraResult::OK, c.stopRecording());
    TEST_ASSERT_FALSE(c.isRecording());
}

void test_start_recording_idempotent() {
    MockTransport t;
    t.enqueueResponse(deviceInfo(1, ALL_FEATURES));
    t.enqueueResponse(ack(0x01));
    RunCamCamera c(t);
    c.begin();
    c.startRecording();
    // Second start should not send anything — already recording.
    TEST_ASSERT_EQUAL(CameraResult::OK, c.startRecording());
}

void test_start_recording_no_power_feature() {
    MockTransport t;
    t.enqueueResponse(deviceInfo(1, 0x0000));  // no features
    RunCamCamera c(t);
    c.begin();
    TEST_ASSERT_EQUAL(CameraResult::ERROR_NOT_SUPPORTED, c.startRecording());
}

void test_start_recording_camera_rejects_state() {
    MockTransport t;
    t.enqueueResponse(deviceInfo(1, ALL_FEATURES));
    t.enqueueResponse(nak(NAK_WRONG_STATE));  // camera rejects
    RunCamCamera c(t);
    c.begin();
    TEST_ASSERT_EQUAL(CameraResult::REJECTED_STATE, c.startRecording());
    TEST_ASSERT_FALSE(c.isRecording());
}

// --- capturePhoto -----------------------------------------------------------

void test_capture_photo_success() {
    MockTransport t;
    t.enqueueResponse(deviceInfo(1, ALL_FEATURES));
    t.enqueueResponse(ack(ACTION_PHOTO));
    RunCamCamera c(t);
    c.begin();
    TEST_ASSERT_EQUAL(CameraResult::OK, c.capturePhoto());
}

// --- not-initialised --------------------------------------------------------

void test_not_initialised() {
    MockTransport t;
    RunCamCamera c(t);
    TEST_ASSERT_EQUAL(CameraResult::ERROR_NOT_INITIALISED, c.startRecording());
    TEST_ASSERT_EQUAL(CameraResult::ERROR_NOT_INITIALISED, c.stopRecording());
    TEST_ASSERT_EQUAL(CameraResult::ERROR_NOT_INITIALISED, c.capturePhoto());
}

// --- Settings (deferred but still callable) --------------------------------

void test_write_setting_no_feature() {
    MockTransport t;
    t.enqueueResponse(deviceInfo(1, 0x0000));
    RunCamCamera c(t);
    c.begin();
    TEST_ASSERT_EQUAL(CameraResult::ERROR_NOT_SUPPORTED,
                      c.writeSetting(SettingId::RESOLUTION, 0));
}

void test_write_setting_invalid_value() {
    MockTransport t;
    t.enqueueResponse(deviceInfo(1, ALL_FEATURES));
    RunCamCamera c(t);
    c.begin();
    TEST_ASSERT_EQUAL(CameraResult::ERROR_NOT_SUPPORTED,
                      c.writeSetting(SettingId::RESOLUTION, 99));
}

void test_write_setting_nak_invalid_arg() {
    MockTransport t;
    t.enqueueResponse(deviceInfo(1, ALL_FEATURES));
    t.enqueueResponse(nak(NAK_INVALID_ARG));
    RunCamCamera c(t);
    c.begin();
    TEST_ASSERT_EQUAL(CameraResult::REJECTED_ARG,
                      c.writeSetting(SettingId::RESOLUTION, 0));
}

// --- Button simulation ------------------------------------------------------

void test_simulate_power_button() {
    MockTransport t;
    t.enqueueResponse(deviceInfo(1, ALL_FEATURES));
    t.enqueueResponse(ack(ACTION_POWER_BUTTON));
    RunCamCamera c(t);
    c.begin();
    TEST_ASSERT_EQUAL(CameraResult::OK, c.simulatePowerButton());
}

void test_simulate_mode_button() {
    MockTransport t;
    t.enqueueResponse(deviceInfo(1, ALL_FEATURES));
    t.enqueueResponse(ack(ACTION_MODE_BUTTON));
    RunCamCamera c(t);
    c.begin();
    TEST_ASSERT_EQUAL(CameraResult::OK, c.simulateModeButton());
}

void test_simulate_5key_not_supported() {
    MockTransport t;
    // Thumb Pro W: FEAT_5KEY off
    t.enqueueResponse(deviceInfo(1, THUMB_PRO_W_FEATURES));
    RunCamCamera c(t);
    c.begin();
    TEST_ASSERT_EQUAL(CameraResult::ERROR_NOT_SUPPORTED,
                      c.simulateKeyPress(KEY_CONFIRM));
}

// --- Retry behaviour --------------------------------------------------------

void test_nak_short_circuits_retry() {
    MockTransport t;
    t.enqueueResponse(deviceInfo(1, ALL_FEATURES));
    t.enqueueResponse(nak(NAK_WRONG_STATE));
    // No further responses queued — if retry happened, would be timeout.
    RunCamCamera c(t);
    c.begin();
    TEST_ASSERT_EQUAL(CameraResult::REJECTED_STATE, c.startRecording());
}

void test_timeout_exhausts_retries() {
    MockTransport t;
    t.enqueueResponse(deviceInfo(1, ALL_FEATURES));
    // No further responses — all retries time out.
    RunCamCamera c(t);
    c.begin();
    TEST_ASSERT_EQUAL(CameraResult::ERROR_TIMEOUT, c.startRecording());
}

// --- Feature bitmask --------------------------------------------------------

void test_has_feature() {
    MockTransport t;
    t.enqueueResponse(deviceInfo(1, THUMB_PRO_W_FEATURES));
    RunCamCamera c(t);
    c.begin();
    TEST_ASSERT_TRUE (c.hasFeature(FEAT_SIMULATE_POWER_BUTTON));
    TEST_ASSERT_TRUE (c.hasFeature(FEAT_SIMULATE_WIFI_BUTTON));
    TEST_ASSERT_TRUE (c.hasFeature(FEAT_CHANGE_MODE));
    TEST_ASSERT_FALSE(c.hasFeature(FEAT_SIMULATE_5KEY));
    TEST_ASSERT_TRUE (c.hasFeature(FEAT_DEVICE_SETTINGS_ACCESS));
    TEST_ASSERT_TRUE (c.hasFeature(FEAT_DISPLAY_PORT));
    TEST_ASSERT_TRUE (c.hasFeature(FEAT_START_RECORDING));
    TEST_ASSERT_FALSE(c.hasFeature(FEAT_STOP_RECORDING));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_begin_success);
    RUN_TEST(test_begin_timeout);
    RUN_TEST(test_begin_crc_error_then_success);
    RUN_TEST(test_recording_toggle_via_power);
    RUN_TEST(test_start_recording_idempotent);
    RUN_TEST(test_start_recording_no_power_feature);
    RUN_TEST(test_start_recording_camera_rejects_state);
    RUN_TEST(test_capture_photo_success);
    RUN_TEST(test_not_initialised);
    RUN_TEST(test_write_setting_no_feature);
    RUN_TEST(test_write_setting_invalid_value);
    RUN_TEST(test_write_setting_nak_invalid_arg);
    RUN_TEST(test_simulate_power_button);
    RUN_TEST(test_simulate_mode_button);
    RUN_TEST(test_simulate_5key_not_supported);
    RUN_TEST(test_nak_short_circuits_retry);
    RUN_TEST(test_timeout_exhausts_retries);
    RUN_TEST(test_has_feature);
    return UNITY_END();
}
