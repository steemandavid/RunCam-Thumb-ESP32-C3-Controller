#ifndef UNIT_TEST

#include "transport/uart_transport.h"
#include "config.h"

UartTransport::UartTransport(uint8_t uartNum, int txPin, int rxPin, uint32_t baud)
    : serial_(uartNum), txPin_(txPin), rxPin_(rxPin), baud_(baud) {}

bool UartTransport::begin() {
    serial_.begin(baud_, SERIAL_8N1, rxPin_, txPin_);
    return true;
}

bool UartTransport::send(const uint8_t* data, size_t len) {
    size_t written = serial_.write(data, len);
    serial_.flush();
    return written == len;
}

int UartTransport::receive(uint8_t* buffer, size_t maxLen, uint32_t timeoutMs) {
    // Wait up to timeoutMs for the first byte; once any byte arrives, extend
    // the deadline by RUNCAM_INTER_BYTE_TIMEOUT_MS after each subsequent byte
    // so bursty responses (e.g. the camera retransmitting NAK frames) are
    // captured intact. Per FSD §4.7.
    uint32_t deadline = millis() + timeoutMs;
    size_t idx = 0;
    while (idx < maxLen && (int32_t)(deadline - millis()) > 0) {
        int b = serial_.read();
        if (b >= 0) {
            buffer[idx++] = static_cast<uint8_t>(b);
            deadline = millis() + RUNCAM_INTER_BYTE_TIMEOUT_MS;
        }
    }
    return static_cast<int>(idx);
}

void UartTransport::flush() {
    while (serial_.available()) {
        serial_.read();
    }
}

#endif // UNIT_TEST
