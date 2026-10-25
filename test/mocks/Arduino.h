/**
 * @file Arduino.h
 * @brief Mock Arduino header for native unit testing
 * 
 * This provides minimal Arduino compatibility for running
 * unit tests on the native platform without real hardware.
 */

#pragma once

#ifdef NATIVE_BUILD

#include <cstdint>
#include <cstdio>
#include <cstring>

// Arduino types
typedef uint8_t byte;

// Time functions
inline uint32_t millis() {
    static uint32_t mock_millis = 0;
    return mock_millis++;
}

inline void delay(uint32_t ms) {
    // No-op in tests
    (void)ms;
}

// Serial mock
class MockSerial {
public:
    void begin(unsigned long baud) { (void)baud; }
    void print(const char* s) { printf("%s", s); }
    void println(const char* s) { printf("%s\n", s); }
    void println() { printf("\n"); }
    void print(int n) { printf("%d", n); }
    void println(int n) { printf("%d\n", n); }
    explicit operator bool() const { return true; }
};

extern MockSerial Serial;

// Pin modes (not used in tests but needed for compilation)
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

inline void pinMode(uint8_t pin, uint8_t mode) { (void)pin; (void)mode; }
inline void digitalWrite(uint8_t pin, uint8_t val) { (void)pin; (void)val; }
inline int digitalRead(uint8_t pin) { (void)pin; return 0; }

#endif // NATIVE_BUILD
