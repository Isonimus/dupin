/**
 * @file finding.h
 * @brief Follow-up findings from Poirot investigations
 * 
 * Findings are the results of deeper analysis on detected anomalies.
 * They add context and evidence to help confirm or dismiss threats.
 */

#pragma once

#include <cstdint>
#include "dupin.h"
#include "core/anomaly.h"
#include "utils/mac_utils.h"

namespace dupin {

/**
 * @brief Types of investigative findings
 */
enum class FindingType : uint8_t {
    Unknown = 0,
    
    // Probe behavior analysis
    ProbePromiscuity,       // AP responds to probes it shouldn't
    ProbeAttraction,        // High rate of devices probing this SSID
    
    // Movement/behavior
    BSSIDMovement,          // RSSI gradient suggests physical movement
    ChannelHopping,         // AP changing channels frequently
    
    // Identity/personality
    BeaconPersonality,      // Beacon differs from typical AP behavior
    VendorMismatch,         // OUI doesn't match expected vendor
    TimingAnomaly,          // Beacon timing irregular
    
    // Security analysis
    SecurityMismatch,       // Security settings differ from baseline (evil twin indicator)
    
    // Network behavior
    ClientMigration,        // Clients switching to suspicious AP
    
    // Count
    _Count
};

/**
 * @brief Strength/confidence qualifier for findings
 */
enum class FindingStrength : uint8_t {
    Weak = 0,       // Slight evidence
    Moderate = 1,   // Notable evidence
    Strong = 2,     // Convincing evidence
    Conclusive = 3  // Very high confidence
};

/**
 * @brief A follow-up finding from investigation
 */
struct Finding {
    // Identity
    uint32_t id = 0;
    FindingType type = FindingType::Unknown;
    
    // Link to parent anomaly
    uint32_t anomalyId = 0;
    
    // Context
    char ssid[DUPIN_SSID_MAX_LEN + 1] = {0};
    uint8_t bssid[MAC_LEN] = {0};
    
    // Evidence
    FindingStrength strength = FindingStrength::Weak;
    char explanation[128] = {0};
    
    // Timing
    uint32_t timestamp = 0;
    
    // Additional metrics (type-specific)
    union {
        struct {
            uint8_t probeResponseCount;
            uint8_t unexpectedResponses;
        } probePromiscuity;
        
        struct {
            int8_t rssiFirst;
            int8_t rssiLast;
            int8_t gradient;  // Positive = approaching
        } movement;
        
        struct {
            uint8_t channelChanges;
            uint32_t windowMs;
        } channelHop;
        
        struct {
            uint16_t expectedInterval;
            uint16_t actualInterval;
        } timing;
        
        struct {
            uint8_t expectedSecurity;  // SecurityType enum value
            uint8_t actualSecurity;    // SecurityType enum value
        } security;
    } metrics;
    
    /**
     * @brief Set SSID
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
     * @brief Set explanation text
     */
    void setExplanation(const char* text) {
        if (text == nullptr) {
            explanation[0] = '\0';
            return;
        }
        size_t i = 0;
        while (i < 127 && text[i] != '\0') {
            explanation[i] = text[i];
            i++;
        }
        explanation[i] = '\0';
    }
    
    /**
     * @brief Check if finding is active
     */
    bool isActive() const {
        return type != FindingType::Unknown;
    }
    
    /**
     * @brief Clear the finding
     */
    void clear() {
        *this = Finding{};
    }
};

/**
 * @brief Get display name for finding type
 */
inline const char* findingTypeName(FindingType type) {
    switch (type) {
        case FindingType::ProbePromiscuity: return "Probe Promiscuity";
        case FindingType::ProbeAttraction: return "Probe Attraction";
        case FindingType::BSSIDMovement: return "BSSID Movement";
        case FindingType::ChannelHopping: return "Channel Hopping";
        case FindingType::BeaconPersonality: return "Beacon Personality";
        case FindingType::VendorMismatch: return "Vendor Mismatch";
        case FindingType::TimingAnomaly: return "Timing Anomaly";
        case FindingType::SecurityMismatch: return "Security Mismatch";
        case FindingType::ClientMigration: return "Client Migration";
        default: return "Unknown";
    }
}

/**
 * @brief Get display name for finding strength
 */
inline const char* findingStrengthName(FindingStrength str) {
    switch (str) {
        case FindingStrength::Weak: return "Weak";
        case FindingStrength::Moderate: return "Moderate";
        case FindingStrength::Strong: return "Strong";
        case FindingStrength::Conclusive: return "Conclusive";
        default: return "Unknown";
    }
}

} // namespace dupin
