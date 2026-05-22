#include "unity.h"
#include "protocol/runcam_protocol.h"
#include <cstdint>
#include <cstring>
#include <vector>

void setUp() {}
void tearDown() {}

static std::vector<uint8_t> buildVec(uint8_t cmd, const uint8_t* data, size_t dataLen) {
    uint8_t buf[MAX_FRAME_SIZE];
    size_t len = buildFrame(buf, sizeof(buf), cmd, data, dataLen);
    return std::vector<uint8_t>(buf, buf + len);
}

void test_encode_get_device_info() {
    auto frame = buildVec(CMD_GET_DEVICE_INFO, nullptr, 0);
    uint8_t expected[] = {0xCC, 0x00, 0x60};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame.data(), 3);
    TEST_ASSERT_EQUAL_size_t(3, frame.size());
}

void test_encode_start_recording() {
    uint8_t action = ACTION_START_REC;
    auto frame = buildVec(CMD_CAMERA_CONTROL, &action, 1);
    uint8_t expected[] = {0xCC, 0x01, 0x03, 0x98};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame.data(), 4);
}

void test_encode_stop_recording() {
    uint8_t action = ACTION_STOP_REC;
    auto frame = buildVec(CMD_CAMERA_CONTROL, &action, 1);
    uint8_t expected[] = {0xCC, 0x01, 0x04, 0xCC};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame.data(), 4);
}

void test_encode_capture_photo() {
    uint8_t action = ACTION_PHOTO;
    auto frame = buildVec(CMD_CAMERA_CONTROL, &action, 1);
    uint8_t expected[] = {0xCC, 0x01, 0x05, 0x19};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame.data(), 4);
}

void test_encode_power_button() {
    uint8_t action = ACTION_POWER_BUTTON;
    auto frame = buildVec(CMD_CAMERA_CONTROL, &action, 1);
    TEST_ASSERT_EQUAL_size_t(4, frame.size());
    TEST_ASSERT_EQUAL_UINT8(0xCC, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(0x01, frame[1]);
    TEST_ASSERT_EQUAL_UINT8(0x01, frame[2]);
}

void test_encode_mode_button() {
    uint8_t action = ACTION_MODE_BUTTON;
    auto frame = buildVec(CMD_CAMERA_CONTROL, &action, 1);
    TEST_ASSERT_EQUAL_size_t(4, frame.size());
    TEST_ASSERT_EQUAL_UINT8(0xCC, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(0x01, frame[1]);
    TEST_ASSERT_EQUAL_UINT8(0x02, frame[2]);
}

void test_encode_key_press_confirm() {
    uint8_t key = KEY_CONFIRM;
    auto frame = buildVec(CMD_5KEY_SIMULATION_PRESS, &key, 1);
    uint8_t expected[] = {0xCC, 0x02, 0x05, 0x04};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame.data(), 4);
}

void test_encode_key_release() {
    auto frame = buildVec(CMD_5KEY_SIMULATION_RELEASE, nullptr, 0);
    TEST_ASSERT_EQUAL_size_t(3, frame.size());
    TEST_ASSERT_EQUAL_UINT8(0xCC, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(0x03, frame[1]);
}

void test_encode_key_connection_event() {
    uint8_t event = KEY_EVENT_CONNECTED;
    auto frame = buildVec(CMD_5KEY_CONNECTION_EVENT, &event, 1);
    TEST_ASSERT_EQUAL_size_t(4, frame.size());
    TEST_ASSERT_EQUAL_UINT8(0x04, frame[1]);
    TEST_ASSERT_EQUAL_UINT8(0x01, frame[2]);
}

void test_encode_get_settings() {
    uint8_t settingId = 0x01;
    auto frame = buildVec(CMD_GET_SETTINGS, &settingId, 1);
    uint8_t expected[] = {0xCC, 0x10, 0x01, 0x5C};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame.data(), 4);
}

void test_encode_write_setting() {
    uint8_t payload[] = {0x01, 0x00}; // resolution = 4K
    auto frame = buildVec(CMD_WRITE_SETTING, payload, 2);
    uint8_t expected[] = {0xCC, 0x11, 0x01, 0x00, 0x9B};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame.data(), 5);
}

void test_encode_read_setting_detail() {
    uint8_t settingId = 0x05; // EIS
    auto frame = buildVec(CMD_READ_SETTING_DETAIL, &settingId, 1);
    TEST_ASSERT_EQUAL_size_t(4, frame.size());
    TEST_ASSERT_EQUAL_UINT8(0x12, frame[1]);
    TEST_ASSERT_EQUAL_UINT8(0x05, frame[2]);
}

void test_encode_buffer_too_small() {
    uint8_t buf[2];
    size_t len = buildFrame(buf, 2, CMD_GET_DEVICE_INFO, nullptr, 0);
    TEST_ASSERT_EQUAL_size_t(0, len);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_encode_get_device_info);
    RUN_TEST(test_encode_start_recording);
    RUN_TEST(test_encode_stop_recording);
    RUN_TEST(test_encode_capture_photo);
    RUN_TEST(test_encode_power_button);
    RUN_TEST(test_encode_mode_button);
    RUN_TEST(test_encode_key_press_confirm);
    RUN_TEST(test_encode_key_release);
    RUN_TEST(test_encode_key_connection_event);
    RUN_TEST(test_encode_get_settings);
    RUN_TEST(test_encode_write_setting);
    RUN_TEST(test_encode_read_setting_detail);
    RUN_TEST(test_encode_buffer_too_small);
    return UNITY_END();
}
