#pragma once

#include <cstdint>
#include <cstddef>

class IRunCamTransport {
public:
    virtual ~IRunCamTransport() = default;
    virtual bool send(const uint8_t* data, size_t len) = 0;
    virtual int receive(uint8_t* buffer, size_t maxLen, uint32_t timeoutMs) = 0;
    virtual void flush() = 0;
};
