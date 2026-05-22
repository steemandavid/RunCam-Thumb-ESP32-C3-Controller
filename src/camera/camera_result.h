#pragma once

#include <cstdint>

enum class CameraResult : uint8_t {
    OK = 0,
    ERROR_TIMEOUT,
    ERROR_CRC,
    ERROR_HEADER,
    ERROR_INCOMPLETE,
    ERROR_NOT_SUPPORTED,
    ERROR_TRANSPORT,
    ERROR_NOT_INITIALISED,
    // Camera responded with a NAK rather than an ACK.
    // The cause is in the NAK error code returned by the camera:
    //   REJECTED_STATE   -> command not valid in current camera state (NAK 0x02)
    //   REJECTED_ARG     -> invalid argument / unsupported setting ID (NAK 0x04)
    //   REJECTED_UNKNOWN -> camera didn't recognise the command       (NAK 0x01)
    REJECTED_STATE,
    REJECTED_ARG,
    REJECTED_UNKNOWN
};
