#include "mock_transport.h"
#include <cstring>

void MockTransport::enqueueResponse(const std::vector<uint8_t>& response) {
    responseQueue_.push(response);
}

std::vector<uint8_t> MockTransport::getSentBytes() const {
    return sentBytes_;
}

void MockTransport::reset() {
    sentBytes_.clear();
    while (!responseQueue_.empty()) {
        responseQueue_.pop();
    }
}

bool MockTransport::send(const uint8_t* data, size_t len) {
    sentBytes_.insert(sentBytes_.end(), data, data + len);
    return true;
}

int MockTransport::receive(uint8_t* buffer, size_t maxLen, uint32_t) {
    if (responseQueue_.empty()) {
        return 0;
    }
    const auto& response = responseQueue_.front();
    size_t copyLen = (response.size() < maxLen) ? response.size() : maxLen;
    memcpy(buffer, response.data(), copyLen);
    responseQueue_.pop();
    return static_cast<int>(copyLen);
}

void MockTransport::flush() {}
