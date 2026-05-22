#pragma once

#include <cstdint>
#include <cstddef>

// RCSP header byte (host -> camera commands, and GET_DEVICE_INFO response)
constexpr uint8_t RCSP_HEADER          = 0xCC;
// Response header used for ACK/NAK frames (camera -> host)
constexpr uint8_t RCSP_RESPONSE_HEADER = 0x55;

// Command bytes (host -> camera)
constexpr uint8_t CMD_GET_DEVICE_INFO         = 0x00;
constexpr uint8_t CMD_CAMERA_CONTROL          = 0x01;
constexpr uint8_t CMD_5KEY_SIMULATION_PRESS   = 0x02;
constexpr uint8_t CMD_5KEY_SIMULATION_RELEASE = 0x03;
constexpr uint8_t CMD_5KEY_CONNECTION_EVENT   = 0x04;
constexpr uint8_t CMD_GET_SETTINGS            = 0x10;
constexpr uint8_t CMD_WRITE_SETTING           = 0x11;
constexpr uint8_t CMD_READ_SETTING_DETAIL     = 0x12;

// Camera control actions
constexpr uint8_t ACTION_WIFI_BUTTON  = 0x00;
constexpr uint8_t ACTION_POWER_BUTTON = 0x01;
constexpr uint8_t ACTION_MODE_BUTTON  = 0x02;
constexpr uint8_t ACTION_START_REC    = 0x03;
constexpr uint8_t ACTION_STOP_REC     = 0x04;
constexpr uint8_t ACTION_PHOTO        = 0x05;

// 5-key simulation keys
constexpr uint8_t KEY_UP      = 0x01;
constexpr uint8_t KEY_LEFT    = 0x02;
constexpr uint8_t KEY_RIGHT   = 0x03;
constexpr uint8_t KEY_DOWN    = 0x04;
constexpr uint8_t KEY_CONFIRM = 0x05;

// 5-key connection events
constexpr uint8_t KEY_EVENT_CONNECTED    = 0x01;
constexpr uint8_t KEY_EVENT_DISCONNECTED = 0x02;

// NAK error codes (from camera, in 0x55 NAK responses)
constexpr uint8_t NAK_UNKNOWN_CMD   = 0x01;  // unknown command / wrong CRC
constexpr uint8_t NAK_WRONG_STATE   = 0x02;  // command not valid in current state
constexpr uint8_t NAK_INVALID_ARG   = 0x04;  // invalid argument / unsupported ID

// Parse result codes
enum class ParseResult : uint8_t {
    OK = 0,
    ERROR_CRC,
    ERROR_HEADER,
    ERROR_TIMEOUT,
    ERROR_INCOMPLETE,
    ERROR_INVALID_LENGTH
};

// Kind of response decoded by parseResponse()
enum class ResponseKind : uint8_t {
    DeviceInfo,  // 0xCC-framed: 5 bytes, proto + features uint16_t LE
    Ack,         // 0x55-framed, status 0x01
    Nak          // 0x55-framed, status 0xFF
};

// Result of parsing one response frame
struct ParsedResponse {
    ResponseKind kind;
    uint8_t      protoVer    = 0;   // valid for DeviceInfo
    uint16_t     features    = 0;   // valid for DeviceInfo
    uint8_t      actionEcho  = 0;   // valid for Ack
    uint8_t      errCode     = 0;   // valid for Nak (see NAK_* constants)
    size_t       frameLength = 0;   // total bytes consumed from buffer
};

// GET_DEVICE_INFO summary, populated from a ParsedResponse of kind DeviceInfo
struct DeviceInfo {
    uint8_t  protocolVersion = 0;
    uint16_t featureBitmask  = 0;
};

// Maximum frame size: header + cmd + 255 data bytes + crc = 258
constexpr size_t MAX_FRAME_SIZE = 258;
constexpr size_t FRAME_OVERHEAD = 3;  // header + cmd + crc

// Build a complete RCSP request frame. Returns the frame length.
// Layout: [0xCC] [cmd] [data...] [CRC8/DVB-S2 over preceding bytes]
size_t buildFrame(uint8_t* buffer, size_t maxLen,
                  uint8_t cmd, const uint8_t* data, size_t dataLen);

// Parse one response frame from the byte buffer. Dispatches on first byte:
//   0xCC -> GET_DEVICE_INFO format (5 bytes total)
//   0x55 -> ACK or NAK format (length is in byte 1, total 5 or 6 bytes)
// On success, populates outResp and sets outResp.frameLength to the number
// of bytes consumed; caller can advance past these to parse a queued frame.
ParseResult parseResponse(const uint8_t* data, size_t len, ParsedResponse& outResp);

// Convenience: parse a DeviceInfo from a ParsedResponse of kind DeviceInfo.
ParseResult parseDeviceInfo(const ParsedResponse& resp, DeviceInfo& out);
