#pragma once

#include "transport/i_transport.h"
#include <vector>
#include <queue>
#include <cstdint>
#include <cstddef>

class MockTransport : public IRunCamTransport {
public:
    void enqueueResponse(const std::vector<uint8_t>& response);
    std::vector<uint8_t> getSentBytes() const;
    void reset();

    bool send(const uint8_t* data, size_t len) override;
    int receive(uint8_t* buffer, size_t maxLen, uint32_t timeoutMs) override;
    void flush() override;

private:
    std::vector<uint8_t> sentBytes_;
    std::queue<std::vector<uint8_t>> responseQueue_;
};
