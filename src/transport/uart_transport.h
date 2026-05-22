#pragma once

#ifndef UNIT_TEST

#include "transport/i_transport.h"
#include <HardwareSerial.h>

class UartTransport : public IRunCamTransport {
public:
    UartTransport(uint8_t uartNum, int txPin, int rxPin, uint32_t baud);
    bool begin();
    bool send(const uint8_t* data, size_t len) override;
    int receive(uint8_t* buffer, size_t maxLen, uint32_t timeoutMs) override;
    void flush() override;

private:
    HardwareSerial serial_;
    int txPin_;
    int rxPin_;
    uint32_t baud_;
};

#endif // UNIT_TEST
