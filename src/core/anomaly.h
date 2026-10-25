/**
 * @file anomaly.h
 * @brief Anomaly types and structures for detection
 * 
 * Defines the types of anomalies that Marple mode can detect
 * when comparing observations against the baseline.
 */

#pragma once

#include <cstdint>
#include "dupin.h"
#include "utils/mac_utils.h"

namespace dupin {

/**
 * @brief Types of detectable anomalies
 */
enum class AnomalyType : uint8_t {
    Unknown = 0,
    
    // SSID/BSSID anomalies
    NewBSSIDForSSID,       // New BSSID advertising a known SSID (possible evil twin)
    UnexpectedChannel,     // Known SSID/BSSID on unexpected channel
    
    // Signal anomalies
    RSSIAnomaly,           // Significant RSSI deviation from baseline
    
    // Security anomalies
    SecurityDowngrade,     // Security weaker than baseline (e.g., WPA2 → Open)
    SecurityUpgrade,       // Security stronger (less suspicious, but notable)
    
    // Timing anomalies
    BeaconIntervalChange,  // Beacon interval differs from baseline
    
    // Behavioral anomalies (basic)
    ExcessiveDeauths,      // Global deauth count exceeded
    ProbeFlood,            // Excessive probe requests (global)
    
    // Attack signatures (ESP32 Marauder / m5porkchop / Bruce)
    TargetedDeauth,        // Focused deauth attack on specific victim
    KarmaAP,               // Same BSSID responding to multiple SSIDs
    BeaconFlood,           // Mass SSID spam in short window
    CloneSpam,             // Multiple BSSIDs for same SSID (AP clone attack)
    TargetedProbeFlood,    // Single device probing many SSIDs (reconnaissance)
    APlessClient,          // Probe response without matching beacon (Karma/rogue)
    EvilTwinCombo,         // Evil twin + deauth attack detected together
    
    // Count
    _Count
};

/**
 * @brief Severity level for anomalies
 */
enum class Severity : uint8_t {
    Info = 0,      // Just informational
    Low = 1,       // Minor deviation
    Medium = 2,    // Notable, worth attention
    High = 3,      // Significant, likely threat
    Critical = 4   // Immediate action recommended
};

/**
 * @brief An detected anomaly
 */
struct Anomaly {
    // Identification
    AnomalyType type = AnomalyType::Unknown;
    uint32_t id = 0;                    // Unique ID for this anomaly
    
    // Context
    char ssid[DUPIN_SSID_MAX_LEN + 1] = {0};
    uint8_t bssid[MAC_LEN] = {0};         // Target/victim
    uint8_t attackerMAC[MAC_LEN] = {0};   // Source of attack (if identifiable)
    uint8_t channel = 0;
    
    // Timing
    uint32_t firstSeen = 0;             // When first detected
    uint32_t lastSeen = 0;              // Most recent observation
    uint32_t occurrences = 0;           // How many times seen
    
    // Confidence and severity
    float confidence = 0.0f;            // 0.0 - 1.0, decays over time
    Severity severity = Severity::Info;
    
    // Additional context (type-specific)
    union {
        struct {
            int8_t expectedRSSI;         // What we expected
            int8_t actualRSSI;           // What we saw
        } rssi;
        
        struct {
            uint8_t expectedSecurity;    // SecurityType enum value
            uint8_t actualSecurity;      // What we saw
        } security;
        
        struct {
            uint16_t expectedInterval;   // Expected beacon interval
            uint16_t actualInterval;     // What we saw
        } beacon;
        
        struct {
            uint32_t count;              // Number of frames
            uint32_t threshold;          // Threshold exceeded
        } flood;
    } details;
    
    /**
     * @brief Set the SSID for this anomaly
     */
    void setSSID(const char* s) {
        if (s == nullptr) {
            ssid[0] = '\0';
            return;
        }
        size_t i = 0;
        while (i < DUPIN_SSID_MAX_LEN && s[i] != '\0') {
            ssid[i] = s[i];
            i++;
        }
        ssid[i] = '\0';
    }
    
    /**
     * @brief Check if this anomaly has an SSID
     */
    bool hasSSID() const {
        return ssid[0] != '\0';
    }
    
    /**
     * @brief Check if this anomaly has a BSSID
     */
    bool hasBSSID() const {
        return !macIsNull(bssid);
    }
    
    /**
     * @brief Clear this anomaly
     */
    void clear() {
        *this = Anomaly{};
    }
    
    /**
     * @brief Check if this is an active (non-empty) anomaly
     */
    bool isActive() const {
        return type != AnomalyType::Unknown && confidence > 0.0f;
    }
    
    /**
     * @brief Update with a new occurrence
     */
    void recordOccurrence(uint32_t timestamp) {
        lastSeen = timestamp;
        occurrences++;
        // Boost confidence on repeated observations
        confidence = (confidence * 0.8f) + 0.2f;
        if (confidence > 1.0f) confidence = 1.0f;
    }
};

/**
 * @brief Get display name for anomaly type
 */
inline const char* anomalyTypeName(AnomalyType type) {
    switch (type) {
        case AnomalyType::NewBSSIDForSSID: return "New BSSID";
        case AnomalyType::UnexpectedChannel: return "Wrong Ch";
        case AnomalyType::RSSIAnomaly: return "RSSI Anomaly";
        case AnomalyType::SecurityDowngrade: return "Sec Downgrade";
        case AnomalyType::SecurityUpgrade: return "Sec Upgrade";
        case AnomalyType::BeaconIntervalChange: return "Beacon Int";
        case AnomalyType::ExcessiveDeauths: return "Deauth Flood";
        case AnomalyType::ProbeFlood: return "Probe Flood";
        case AnomalyType::TargetedDeauth: return "Targeted Deauth";
        case AnomalyType::KarmaAP: return "Karma AP";
        case AnomalyType::BeaconFlood: return "Beacon Spam";
        case AnomalyType::CloneSpam: return "Clone Attack";
        case AnomalyType::TargetedProbeFlood: return "Probe Recon";
        case AnomalyType::APlessClient: return "Rogue AP";
        case AnomalyType::EvilTwinCombo: return "Evil Twin+Deauth";
        default: return "Unknown";
    }
}

/**
 * @brief Get display name for severity
 */
inline const char* severityName(Severity sev) {
    switch (sev) {
        case Severity::Info: return "INFO";
        case Severity::Low: return "LOW";
        case Severity::Medium: return "MEDIUM";
        case Severity::High: return "HIGH";
        case Severity::Critical: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Get severity for an anomaly type (defaults)
 */
inline Severity defaultSeverity(AnomalyType type) {
    switch (type) {
        case AnomalyType::NewBSSIDForSSID: return Severity::High;
        case AnomalyType::SecurityDowngrade: return Severity::Critical;
        case AnomalyType::ExcessiveDeauths: return Severity::High;
        case AnomalyType::TargetedDeauth: return Severity::Critical;
        case AnomalyType::KarmaAP: return Severity::Critical;
        case AnomalyType::BeaconFlood: return Severity::High;
        case AnomalyType::CloneSpam: return Severity::High;
        case AnomalyType::EvilTwinCombo: return Severity::Critical;
        case AnomalyType::APlessClient: return Severity::High;
        case AnomalyType::TargetedProbeFlood: return Severity::Medium;
        case AnomalyType::UnexpectedChannel: return Severity::Medium;
        case AnomalyType::RSSIAnomaly: return Severity::Medium;
        case AnomalyType::ProbeFlood: return Severity::Medium;
        case AnomalyType::BeaconIntervalChange: return Severity::Low;
        case AnomalyType::SecurityUpgrade: return Severity::Info;
        default: return Severity::Info;
    }
}

} // namespace dupin
