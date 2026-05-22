#include "unity.h"
#include "protocol/runcam_protocol.h"
#include "protocol/crc8.h"
#include <cstdint>
#include <vector>

void setUp() {}
void tearDown() {}

// Helpers — build response frames in the wire formats verified on hardware.

static std::vector<uint8_t> makeDeviceInfo(uint8_t proto, uint16_t features) {
    std::vector<uint8_t> f = {RCSP_HEADER, proto,
                              (uint8_t)(features & 0xFF),
                              (uint8_t)(features >> 8)};
    f.push_back(crc8_dvb_s2(f.data(), f.size()));
    return f;
}

static std::vector<uint8_t> makeAck(uint8_t actionEcho) {
    std::vector<uint8_t> f = {RCSP_RESPONSE_HEADER, 0x06, 0x01, 0x00, actionEcho};
    f.push_back(crc8_dvb_s2(f.data(), f.size()));
    return f;
}

static std::vector<uint8_t> makeNak(uint8_t errCode) {
    std::vector<uint8_t> f = {RCSP_RESPONSE_HEADER, 0x05, 0xFF, errCode};
    f.push_back(crc8_dvb_s2(f.data(), f.size()));
    return f;
}

// --- 0xCC-framed device info -----------------------------------------------

void test_parse_device_info_thumb_pro_w() {
    // The exact bytes captured from a Thumb Pro W: CC 01 77 00 02
    uint8_t bytes[] = {0xCC, 0x01, 0x77, 0x00, 0x02};
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::OK, parseResponse(bytes, 5, r));
    TEST_ASSERT_EQUAL(ResponseKind::DeviceInfo, r.kind);
    TEST_ASSERT_EQUAL_UINT8(0x01, r.protoVer);
    TEST_ASSERT_EQUAL_UINT16(0x0077, r.features);
    TEST_ASSERT_EQUAL_size_t(5, r.frameLength);
}

void test_parse_device_info_all_features() {
    auto f = makeDeviceInfo(1, 0xFFFF);
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::OK, parseResponse(f.data(), f.size(), r));
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, r.features);
}

void test_parse_device_info_bad_crc() {
    uint8_t bytes[] = {0xCC, 0x01, 0x77, 0x00, 0xFF};  // wrong CRC
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::ERROR_CRC, parseResponse(bytes, 5, r));
}

void test_parse_device_info_truncated() {
    uint8_t bytes[] = {0xCC, 0x01, 0x77};
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::ERROR_INCOMPLETE, parseResponse(bytes, 3, r));
}

// --- 0x55-framed ACK --------------------------------------------------------

void test_parse_ack_mode() {
    uint8_t bytes[] = {0x55, 0x06, 0x01, 0x00, 0x02, 0x63};
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::OK, parseResponse(bytes, 6, r));
    TEST_ASSERT_EQUAL(ResponseKind::Ack, r.kind);
    TEST_ASSERT_EQUAL_UINT8(0x02, r.actionEcho);
    TEST_ASSERT_EQUAL_size_t(6, r.frameLength);
}

void test_parse_ack_stop_recording() {
    uint8_t bytes[] = {0x55, 0x06, 0x01, 0x00, 0x04, 0xE2};
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::OK, parseResponse(bytes, 6, r));
    TEST_ASSERT_EQUAL(ResponseKind::Ack, r.kind);
    TEST_ASSERT_EQUAL_UINT8(0x04, r.actionEcho);
}

void test_parse_ack_constructed() {
    auto f = makeAck(0x01);  // Power
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::OK, parseResponse(f.data(), f.size(), r));
    TEST_ASSERT_EQUAL(ResponseKind::Ack, r.kind);
    TEST_ASSERT_EQUAL_UINT8(0x01, r.actionEcho);
}

void test_parse_ack_bad_crc() {
    uint8_t bytes[] = {0x55, 0x06, 0x01, 0x00, 0x02, 0xFF};  // wrong CRC
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::ERROR_CRC, parseResponse(bytes, 6, r));
}

void test_parse_ack_truncated() {
    uint8_t bytes[] = {0x55, 0x06, 0x01};  // length says 6 but only 3 bytes
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::ERROR_INCOMPLETE, parseResponse(bytes, 3, r));
}

// --- 0x55-framed NAK --------------------------------------------------------

void test_parse_nak_wrong_state() {
    uint8_t bytes[] = {0x55, 0x05, 0xFF, 0x02, 0x1A};
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::OK, parseResponse(bytes, 5, r));
    TEST_ASSERT_EQUAL(ResponseKind::Nak, r.kind);
    TEST_ASSERT_EQUAL_UINT8(NAK_WRONG_STATE, r.errCode);
}

void test_parse_nak_invalid_arg() {
    uint8_t bytes[] = {0x55, 0x05, 0xFF, 0x04, 0x9B};
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::OK, parseResponse(bytes, 5, r));
    TEST_ASSERT_EQUAL(ResponseKind::Nak, r.kind);
    TEST_ASSERT_EQUAL_UINT8(NAK_INVALID_ARG, r.errCode);
}

void test_parse_nak_unknown() {
    auto f = makeNak(NAK_UNKNOWN_CMD);
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::OK, parseResponse(f.data(), f.size(), r));
    TEST_ASSERT_EQUAL(ResponseKind::Nak, r.kind);
    TEST_ASSERT_EQUAL_UINT8(NAK_UNKNOWN_CMD, r.errCode);
}

void test_parse_nak_bad_crc() {
    uint8_t bytes[] = {0x55, 0x05, 0xFF, 0x02, 0xFF};
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::ERROR_CRC, parseResponse(bytes, 5, r));
}

// --- Error / edge cases -----------------------------------------------------

void test_parse_empty_buffer() {
    uint8_t bytes[1] = {0};
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::ERROR_TIMEOUT, parseResponse(bytes, 0, r));
}

void test_parse_unknown_first_byte() {
    uint8_t bytes[] = {0xAA, 0x01, 0x02};
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::ERROR_HEADER, parseResponse(bytes, 3, r));
}

void test_parse_55_with_bogus_length() {
    uint8_t bytes[] = {0x55, 0xFF, 0x01, 0x00};
    ParsedResponse r{};
    TEST_ASSERT_EQUAL(ParseResult::ERROR_INVALID_LENGTH, parseResponse(bytes, 4, r));
}

// --- Multi-frame buffers ----------------------------------------------------

void test_parse_two_naks_queued() {
    // Two NAK frames back-to-back: 55 05 FF 04 9B 55 05 FF 01 B0
    uint8_t bytes[] = {0x55, 0x05, 0xFF, 0x04, 0x9B,
                       0x55, 0x05, 0xFF, 0x01, 0xB0};
    ParsedResponse r1{};
    TEST_ASSERT_EQUAL(ParseResult::OK, parseResponse(bytes, 10, r1));
    TEST_ASSERT_EQUAL(ResponseKind::Nak, r1.kind);
    TEST_ASSERT_EQUAL_UINT8(NAK_INVALID_ARG, r1.errCode);
    TEST_ASSERT_EQUAL_size_t(5, r1.frameLength);

    ParsedResponse r2{};
    TEST_ASSERT_EQUAL(ParseResult::OK,
                      parseResponse(bytes + r1.frameLength, 10 - r1.frameLength, r2));
    TEST_ASSERT_EQUAL(ResponseKind::Nak, r2.kind);
    TEST_ASSERT_EQUAL_UINT8(NAK_UNKNOWN_CMD, r2.errCode);
}

// --- parseDeviceInfo helper -------------------------------------------------

void test_parseDeviceInfo_from_response() {
    ParsedResponse r{};
    r.kind = ResponseKind::DeviceInfo;
    r.protoVer = 1;
    r.features = 0x0077;
    DeviceInfo info{};
    TEST_ASSERT_EQUAL(ParseResult::OK, parseDeviceInfo(r, info));
    TEST_ASSERT_EQUAL_UINT8(1, info.protocolVersion);
    TEST_ASSERT_EQUAL_UINT16(0x0077, info.featureBitmask);
}

void test_parseDeviceInfo_rejects_ack() {
    ParsedResponse r{};
    r.kind = ResponseKind::Ack;
    DeviceInfo info{};
    TEST_ASSERT_EQUAL(ParseResult::ERROR_HEADER, parseDeviceInfo(r, info));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_device_info_thumb_pro_w);
    RUN_TEST(test_parse_device_info_all_features);
    RUN_TEST(test_parse_device_info_bad_crc);
    RUN_TEST(test_parse_device_info_truncated);
    RUN_TEST(test_parse_ack_mode);
    RUN_TEST(test_parse_ack_stop_recording);
    RUN_TEST(test_parse_ack_constructed);
    RUN_TEST(test_parse_ack_bad_crc);
    RUN_TEST(test_parse_ack_truncated);
    RUN_TEST(test_parse_nak_wrong_state);
    RUN_TEST(test_parse_nak_invalid_arg);
    RUN_TEST(test_parse_nak_unknown);
    RUN_TEST(test_parse_nak_bad_crc);
    RUN_TEST(test_parse_empty_buffer);
    RUN_TEST(test_parse_unknown_first_byte);
    RUN_TEST(test_parse_55_with_bogus_length);
    RUN_TEST(test_parse_two_naks_queued);
    RUN_TEST(test_parseDeviceInfo_from_response);
    RUN_TEST(test_parseDeviceInfo_rejects_ack);
    return UNITY_END();
}
