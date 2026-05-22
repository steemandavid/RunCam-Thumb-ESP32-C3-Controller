#include "unity.h"
#include "protocol/crc8.h"
#include <cstdint>

void setUp() {}
void tearDown() {}

// CRC-8/DVB-S2 self-tests using vectors verified against the physical
// RunCam Thumb Pro W on 2026-05-21 (see Serial_Diagnostic_Report.md §7).

void test_crc8_empty_input() {
    uint8_t data[] = {0x00};
    TEST_ASSERT_EQUAL_UINT8(0x00, crc8_dvb_s2(data, 0));
}

void test_crc8_single_zero_byte() {
    uint8_t data[] = {0x00};
    TEST_ASSERT_EQUAL_UINT8(0x00, crc8_dvb_s2(data, 1));
}

// Request vectors — verified by camera responding correctly to these

void test_crc8_request_get_device_info() {
    uint8_t data[] = {0xCC, 0x00};
    TEST_ASSERT_EQUAL_UINT8(0x60, crc8_dvb_s2(data, 2));
}

void test_crc8_request_wifi_button() {
    uint8_t data[] = {0xCC, 0x01, 0x00};
    TEST_ASSERT_EQUAL_UINT8(0x32, crc8_dvb_s2(data, 3));
}

void test_crc8_request_power_button() {
    uint8_t data[] = {0xCC, 0x01, 0x01};
    TEST_ASSERT_EQUAL_UINT8(0xE7, crc8_dvb_s2(data, 3));
}

void test_crc8_request_mode() {
    uint8_t data[] = {0xCC, 0x01, 0x02};
    TEST_ASSERT_EQUAL_UINT8(0x4D, crc8_dvb_s2(data, 3));
}

void test_crc8_request_start_recording() {
    uint8_t data[] = {0xCC, 0x01, 0x03};
    TEST_ASSERT_EQUAL_UINT8(0x98, crc8_dvb_s2(data, 3));
}

void test_crc8_request_stop_recording() {
    uint8_t data[] = {0xCC, 0x01, 0x04};
    TEST_ASSERT_EQUAL_UINT8(0xCC, crc8_dvb_s2(data, 3));
}

// Response vectors — verified by camera transmitting these bytes

void test_crc8_response_device_info() {
    // CC 01 77 00 02  -- camera reports proto v1, features 0x0077
    uint8_t data[] = {0xCC, 0x01, 0x77, 0x00};
    TEST_ASSERT_EQUAL_UINT8(0x02, crc8_dvb_s2(data, 4));
}

void test_crc8_response_ack_mode() {
    // 55 06 01 00 02 63
    uint8_t data[] = {0x55, 0x06, 0x01, 0x00, 0x02};
    TEST_ASSERT_EQUAL_UINT8(0x63, crc8_dvb_s2(data, 5));
}

void test_crc8_response_nak_wrong_state() {
    // 55 05 FF 02 1A
    uint8_t data[] = {0x55, 0x05, 0xFF, 0x02};
    TEST_ASSERT_EQUAL_UINT8(0x1A, crc8_dvb_s2(data, 4));
}

void test_crc8_response_nak_invalid_arg() {
    // 55 05 FF 04 9B
    uint8_t data[] = {0x55, 0x05, 0xFF, 0x04};
    TEST_ASSERT_EQUAL_UINT8(0x9B, crc8_dvb_s2(data, 4));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_crc8_empty_input);
    RUN_TEST(test_crc8_single_zero_byte);
    RUN_TEST(test_crc8_request_get_device_info);
    RUN_TEST(test_crc8_request_wifi_button);
    RUN_TEST(test_crc8_request_power_button);
    RUN_TEST(test_crc8_request_mode);
    RUN_TEST(test_crc8_request_start_recording);
    RUN_TEST(test_crc8_request_stop_recording);
    RUN_TEST(test_crc8_response_device_info);
    RUN_TEST(test_crc8_response_ack_mode);
    RUN_TEST(test_crc8_response_nak_wrong_state);
    RUN_TEST(test_crc8_response_nak_invalid_arg);
    return UNITY_END();
}
