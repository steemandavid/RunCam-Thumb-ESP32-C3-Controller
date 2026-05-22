#pragma once
// HardwareSerial stub for host unit tests

#include <cstdint>
#include <cstddef>

class HardwareSerial {
public:
    void begin(unsigned long) {}
    size_t write(const uint8_t*, size_t) { return 0; }
    int read() { return -1; }
    int available() { return 0; }
    void flush() {}
};
