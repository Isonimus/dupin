/**
 * @file serial_logger.h
 * @brief Serial output formatting for Dupin
 * 
 * Provides formatted logging for observations, baselines,
 * anomalies, and system status.
 */

#pragma once

#include <cstdint>
#include "core/observation.h"
#include "core/baseline.h"
#include "utils/mac_utils.h"
#include "utils/time_utils.h"

#ifndef NATIVE_BUILD
#include <Arduino.h>
#else
// Mock Serial for native testing
#include <cstdio>
#include <cstdarg>
#include <string>

class MockSerialClass {
public:
    void print(const char* val) { if (val) std::printf("%s", val); }
    void print(char val) { std::printf("%c", val); }
    void print(int val) { std::printf("%d", val); }
    void print(unsigned int val) { std::printf("%u", val); }
    // `long` overloads print through to_string, not %ld/%lu: correct here, but a long length
    // modifier is the form make check-formats bans everywhere, with no exemptions (ADR-0028).
    void print(long val) { std::printf("%s", std::to_string(val).c_str()); }
    void print(unsigned long val) { std::printf("%s", std::to_string(val).c_str()); }
    
    void println(const char* val) { if (val) std::printf("%s\n", val); else std::printf("\n"); }
    void println(char val) { std::printf("%c\n", val); }
    void println(int val) { std::printf("%d\n", val); }
    void println(unsigned int val) { std::printf("%u\n", val); }
    void println(long val) { std::printf("%s\n", std::to_string(val).c_str()); }
    void println(unsigned long val) { std::printf("%s\n", std::to_string(val).c_str()); }
    void println() { std::printf("\n"); }
    
    // The real Print::printf carries this attribute, so the device compiler checks
    // every format string. Without it here the host build — the default gate — is
    // strictly weaker than the device build and silently accepts mismatches it
    // would catch. Keep the two in step, or bugs hide on the side nobody compiles.
    __attribute__((format(printf, 2, 3)))
    void printf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        std::vprintf(fmt, args);
        va_end(args);
    }
};
extern MockSerialClass Serial;
#endif

namespace dupin {

/**
 * @brief Verbosity levels for logging
 */
enum class Verbosity : uint8_t {
    Silent = 0,    // No output
    Minimal = 1,   // Errors and critical alerts only
    Normal = 2,    // Standard operation info
    Verbose = 3,   // Detailed frame-by-frame
    Debug = 4      // Everything, including internals
};

/**
 * @brief Frame type filter for logging
 */
struct LogFilter {
    bool beacons = true;
    bool probeRequests = true;
    bool probeResponses = true;
    bool deauths = true;
    bool associations = true;
    bool data = false;  // Usually too noisy
    
    /**
     * @brief Check if a frame type should be logged
     */
    bool shouldLog(FrameType type) const {
        switch (type) {
            case FrameType::Beacon: return beacons;
            case FrameType::ProbeRequest: return probeRequests;
            case FrameType::ProbeResponse: return probeResponses;
            case FrameType::Deauthentication:
            case FrameType::Disassociation: return deauths;
            case FrameType::Association:
            case FrameType::Reassociation: return associations;
            case FrameType::Data: return data;
            default: return true;
        }
    }
};

/**
 * @brief Serial logging utility
 */
class SerialLogger {
public:
    SerialLogger() = default;
    
    /**
     * @brief Set verbosity level
     */
    void setVerbosity(Verbosity level) { m_verbosity = level; }
    Verbosity getVerbosity() const { return m_verbosity; }
    
    /**
     * @brief Set frame filter
     */
    void setFilter(const LogFilter& filter) { m_filter = filter; }
    const LogFilter& getFilter() const { return m_filter; }
    
    /**
     * @brief Log an observation
     */
    void logObservation(const Observation& obs) {
        if (m_verbosity < Verbosity::Verbose) return;
        if (!m_filter.shouldLog(obs.frameType)) return;
        
        char macStr[18];
        char timeStr[9];
        
        formatTime(obs.timestamp, timeStr);
        macFormat(obs.bssid, macStr);
        
        Serial.printf("[%s] %-9s ch%02d %4ddBm ",
                      timeStr,
                      frameTypeName(obs.frameType),
                      obs.channel,
                      obs.rssi);
        
        Serial.print(macStr);
        
        if (obs.hasSSID()) {
            Serial.printf(" \"%s\"", obs.ssid);
        }
        
        if (m_verbosity >= Verbosity::Debug) {
            Serial.printf(" sec=%s", securityTypeName(obs.security.type));
            if (obs.capabilities.ht) Serial.print(" HT");
            if (obs.capabilities.vht) Serial.print(" VHT");
            if (obs.capabilities.he) Serial.print(" HE");
        }
        
        Serial.println();
    }
    
    /**
     * @brief Log baseline summary
     */
    void logBaseline(const Baseline& baseline) {
        if (m_verbosity < Verbosity::Normal) return;
        
        Serial.println("=== Baseline Summary ===");
        Serial.printf("SSIDs tracked: %zu\n", baseline.count());
        
        for (size_t i = 0; i < baseline.capacity(); i++) {
            const SSIDBaseline* entry = baseline.at(i);
            if (entry == nullptr || !entry->isActive()) continue;
            
            Serial.printf("  \"%s\": %d BSSIDs, %d channels, RSSI [%d,%d]\n",
                          entry->ssid,
                          entry->bssidCount,
                          entry->channelCount,
                          entry->rssiMin,
                          entry->rssiMax);
        }
        Serial.println("========================");
    }
    
    /**
     * @brief Log a status message
     */
    void logStatus(const char* msg) {
        if (m_verbosity < Verbosity::Normal) return;
        Serial.printf("[STATUS] %s\n", msg);
    }
    
    /**
     * @brief Log an info message
     */
    void logInfo(const char* msg) {
        if (m_verbosity < Verbosity::Verbose) return;
        Serial.printf("[INFO] %s\n", msg);
    }
    
    /**
     * @brief Log an error
     */
    void logError(const char* msg) {
        if (m_verbosity < Verbosity::Minimal) return;
        Serial.printf("[ERROR] %s\n", msg);
    }
    
    /**
     * @brief Log a warning
     */
    void logWarning(const char* msg) {
        if (m_verbosity < Verbosity::Normal) return;
        Serial.printf("[WARN] %s\n", msg);
    }
    
    /**
     * @brief Log new SSID discovery
     */
    void logNewSSID(const char* ssid, const uint8_t* bssid) {
        if (m_verbosity < Verbosity::Normal) return;
        
        char macStr[18];
        macFormat(bssid, macStr);
        Serial.printf("[NEW SSID] \"%s\" @ %s\n", ssid, macStr);
    }
    
    /**
     * @brief Log new BSSID for known SSID
     */
    void logNewBSSID(const char* ssid, const uint8_t* bssid) {
        if (m_verbosity < Verbosity::Normal) return;
        
        char macStr[18];
        macFormat(bssid, macStr);
        Serial.printf("[NEW BSSID] \"%s\" + %s\n", ssid, macStr);
    }
    
    /**
     * @brief Log statistics
     */
    void logStats(uint32_t beacons, uint32_t probes, 
                  uint32_t deauths, uint32_t total) {
        if (m_verbosity < Verbosity::Normal) return;
        
        Serial.printf("[STATS] Beacons:%u ProbeReq:%u Deauth:%u Total:%u\n",
                      static_cast<unsigned>(beacons), static_cast<unsigned>(probes),
                      static_cast<unsigned>(deauths), static_cast<unsigned>(total));
    }

private:
    Verbosity m_verbosity = Verbosity::Normal;
    LogFilter m_filter;
};

// Global logger instance
extern SerialLogger logger;

} // namespace dupin
