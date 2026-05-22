#include "protocol/runcam_protocol.h"
#include "protocol/crc8.h"

size_t buildFrame(uint8_t* buffer, size_t maxLen, uint8_t cmd, const uint8_t* data, size_t dataLen) {
    size_t frameLen = 2 + dataLen + 1;  // header + cmd + data + crc
    if (frameLen > maxLen || frameLen > MAX_FRAME_SIZE) return 0;
    if (dataLen > 255) return 0;

    buffer[0] = RCSP_HEADER;
    buffer[1] = cmd;
    for (size_t i = 0; i < dataLen; i++) buffer[2 + i] = data[i];
    buffer[2 + dataLen] = crc8_dvb_s2(buffer, 2 + dataLen);
    return frameLen;
}

ParseResult parseResponse(const uint8_t* data, size_t len, ParsedResponse& out) {
    if (len == 0) return ParseResult::ERROR_TIMEOUT;

    // ----- 0xCC-framed device info response (5 bytes) -----
    if (data[0] == RCSP_HEADER) {
        if (len < 5) return ParseResult::ERROR_INCOMPLETE;
        uint8_t computed = crc8_dvb_s2(data, 4);
        if (computed != data[4]) return ParseResult::ERROR_CRC;
        out.kind        = ResponseKind::DeviceInfo;
        out.protoVer    = data[1];
        out.features    = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
        out.frameLength = 5;
        return ParseResult::OK;
    }

    // ----- 0x55-framed ACK / NAK response (length-prefixed) -----
    if (data[0] == RCSP_RESPONSE_HEADER) {
        if (len < 2) return ParseResult::ERROR_INCOMPLETE;
        uint8_t totalLen = data[1];
        if (totalLen < 4 || totalLen > 16) return ParseResult::ERROR_INVALID_LENGTH;
        if (len < totalLen) return ParseResult::ERROR_INCOMPLETE;

        uint8_t computed = crc8_dvb_s2(data, totalLen - 1);
        if (computed != data[totalLen - 1]) return ParseResult::ERROR_CRC;

        uint8_t status = data[2];
        if (status == 0x01 && totalLen == 6) {
            out.kind        = ResponseKind::Ack;
            out.actionEcho  = data[4];
            out.frameLength = totalLen;
            return ParseResult::OK;
        }
        if (status == 0xFF && totalLen == 5) {
            out.kind        = ResponseKind::Nak;
            out.errCode     = data[3];
            out.frameLength = totalLen;
            return ParseResult::OK;
        }
        return ParseResult::ERROR_INVALID_LENGTH;
    }

    return ParseResult::ERROR_HEADER;
}

ParseResult parseDeviceInfo(const ParsedResponse& resp, DeviceInfo& out) {
    if (resp.kind != ResponseKind::DeviceInfo) return ParseResult::ERROR_HEADER;
    out.protocolVersion = resp.protoVer;
    out.featureBitmask  = resp.features;
    return ParseResult::OK;
}
