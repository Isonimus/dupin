/**
 * @file time_utils.h
 * @brief Time utilities for Dupin
 * 
 * Platform-agnostic time functions with millisecond precision.
 * Uses millis() on ESP32, or mock for native testing.
 */

#pragma once

#include <cstdint>
#include <cstdio>

#ifndef NATIVE_BUILD
#include <Arduino.h>
#else
// Mock millis() for native testing
extern uint32_t g_mockMillis;
inline uint32_t millis() { return g_mockMillis; }
inline void setMockMillis(uint32_t ms) { g_mockMillis = ms; }
inline void advanceMockMillis(uint32_t ms) { g_mockMillis += ms; }
#endif

namespace dupin {

/**
 * @brief Get current timestamp in milliseconds
 * 
 * @return Milliseconds since boot (or mock value in tests)
 */
inline uint32_t getTimestamp() {
    return millis();
}

/**
 * @brief Calculate elapsed time since a timestamp
 * 
 * Handles 32-bit overflow correctly (wraps every ~49.7 days).
 * 
 * @param since Previous timestamp
 * @return Milliseconds elapsed
 */
inline uint32_t elapsedMs(uint32_t since) {
    uint32_t now = millis();
    // This handles overflow correctly due to unsigned arithmetic
    return now - since;
}

/**
 * @brief Check if a duration has elapsed since a timestamp
 * 
 * @param since Previous timestamp
 * @param durationMs Duration to check
 * @return true if durationMs has passed since 'since'
 */
inline bool hasElapsed(uint32_t since, uint32_t durationMs) {
    return elapsedMs(since) >= durationMs;
}

/**
 * @brief Format a timestamp as HH:MM:SS
 * 
 * Converts milliseconds to hours:minutes:seconds format.
 * Useful for logging and display.
 * 
 * @param timestampMs Timestamp in milliseconds
 * @param out Output buffer (must be at least 9 bytes: "HH:MM:SS\0")
 */
inline void formatTime(uint32_t timestampMs, char* out) {
    uint32_t totalSeconds = timestampMs / 1000;
    uint32_t hours = totalSeconds / 3600;
    uint32_t minutes = (totalSeconds % 3600) / 60;
    uint32_t seconds = totalSeconds % 60;
    
    snprintf(out, 9, "%02u:%02u:%02u", 
             static_cast<unsigned>(hours % 100),  // Wrap at 99 hours
             static_cast<unsigned>(minutes),
             static_cast<unsigned>(seconds));
}

/**
 * @brief Format a duration in human-readable form
 * 
 * Examples: "5s", "2m 30s", "1h 15m", "3d 2h"
 * 
 * @param durationMs Duration in milliseconds
 * @param out Output buffer (must be at least 12 bytes)
 */
inline void formatDuration(uint32_t durationMs, char* out) {
    uint32_t seconds = durationMs / 1000;
    
    if (seconds < 60) {
        snprintf(out, 12, "%us", static_cast<unsigned>(seconds));
    } else if (seconds < 3600) {
        uint32_t mins = seconds / 60;
        uint32_t secs = seconds % 60;
        if (secs > 0) {
            snprintf(out, 12, "%um %us", static_cast<unsigned>(mins), static_cast<unsigned>(secs));
        } else {
            snprintf(out, 12, "%um", static_cast<unsigned>(mins));
        }
    } else if (seconds < 86400) {
        uint32_t hours = seconds / 3600;
        uint32_t mins = (seconds % 3600) / 60;
        if (mins > 0) {
            snprintf(out, 12, "%uh %um", static_cast<unsigned>(hours), static_cast<unsigned>(mins));
        } else {
            snprintf(out, 12, "%uh", static_cast<unsigned>(hours));
        }
    } else {
        uint32_t days = seconds / 86400;
        uint32_t hours = (seconds % 86400) / 3600;
        if (hours > 0) {
            snprintf(out, 12, "%ud %uh", static_cast<unsigned>(days), static_cast<unsigned>(hours));
        } else {
            snprintf(out, 12, "%ud", static_cast<unsigned>(days));
        }
    }
}

/**
 * @brief Simple timer for periodic operations
 */
class Timer {
public:
    /**
     * @brief Create a timer with specified interval
     * @param intervalMs Interval in milliseconds
     */
    explicit Timer(uint32_t intervalMs) 
        : m_interval(intervalMs), m_lastTrigger(0), m_started(false) {}
    
    /**
     * @brief Start/reset the timer
     */
    void start() {
        m_lastTrigger = millis();
        m_started = true;
    }
    
    /**
     * @brief Check if timer has elapsed and auto-reset
     * 
     * @return true if interval has passed (resets timer)
     */
    bool check() {
        if (!m_started) {
            start();
            return false;
        }
        
        if (hasElapsed(m_lastTrigger, m_interval)) {
            m_lastTrigger = millis();
            return true;
        }
        return false;
    }
    
    /**
     * @brief Check if timer has elapsed (without reset)
     */
    bool isElapsed() const {
        if (!m_started) return false;
        return dupin::hasElapsed(m_lastTrigger, m_interval);
    }
    
    /**
     * @brief Get remaining time until next trigger
     */
    uint32_t remaining() const {
        if (!m_started) return m_interval;
        uint32_t elapsed = elapsedMs(m_lastTrigger);
        if (elapsed >= m_interval) return 0;
        return m_interval - elapsed;
    }
    
    /**
     * @brief Set new interval
     */
    void setInterval(uint32_t intervalMs) {
        m_interval = intervalMs;
    }
    
    uint32_t getInterval() const { return m_interval; }
    bool isStarted() const { return m_started; }

private:
    uint32_t m_interval;
    uint32_t m_lastTrigger;
    bool m_started;
};

} // namespace dupin
