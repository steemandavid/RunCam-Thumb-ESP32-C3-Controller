#pragma once
// Minimal Arduino stub for host unit tests

#include <cstdint>
#include <cstdio>
#include <cstring>

using String = std::string;

#define PROGMEM
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

inline void digitalWrite(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t) { return HIGH; }
inline void pinMode(uint8_t, uint8_t) {}

inline void delay(uint32_t) {}
inline uint32_t millis() { return 0; }

#define Serial MockSerial
struct MockSerialClass {
    void begin(unsigned long) {}
    void print(const char*) {}
    void println(const char*) {}
    void println() {}
};
extern MockSerialClass MockSerial;
