/**
 * @file hypothesis.h
 * @brief Hypothesis types for Holmes mode correlation
 * 
 * Holmes correlates anomalies and findings into hypotheses
 * about what's actually happening - evil twin, rogue AP, etc.
 */

#pragma once

#include <cstdint>
#include "dupin.h"
#include "core/anomaly.h"
#include "core/finding.h"
#include "utils/mac_utils.h"

namespace dupin {

// Maximum evidence items per hypothesis
constexpr size_t MAX_EVIDENCE = 8;

/**
 * @brief Hypothesis types - what we think is happening
 */
enum class HypothesisType : uint8_t {
    Unknown = 0,
    
    EvilTwin,           // Fake AP mimicking legitimate network
    RogueAP,            // Unauthorized access point
    PineappleDevice,    // WiFi Pineapple or similar attack device
    DeauthAttack,       // Targeted deauthentication attack
    ProbeHarvesting,    // Device collecting probe requests
    Misconfiguration,   // Likely benign misconfiguration
    Interference,       // RF interference, not malicious
    DeviceMovement,     // Detector device is moving (not the APs)
    
    _Count
};

/**
 * @brief Threat level assessment
 */
enum class ThreatLevel : uint8_t {
    None = 0,       // No threat (misconfiguration, etc.)
    Low = 1,        // Minor concern
    Medium = 2,     // Worth investigating
    High = 3,       // Likely attack
    Critical = 4    // Active attack, immediate action needed
};

/**
 * @brief Evidence item supporting a hypothesis
 */
struct Evidence {
    enum class Type : uint8_t {
        Anomaly,
        Finding,
        Observation
    };
    
    Type type = Type::Anomaly;
    uint32_t id = 0;           // ID of the evidence item
    float weight = 1.0f;       // How strongly this supports the hypothesis
    
    bool isValid() const { return id != 0; }
    void clear() { *this = Evidence{}; }
};

/**
 * @brief A hypothesis about what's happening
 */
struct Hypothesis {
    // Identity
    uint32_t id = 0;
    HypothesisType type = HypothesisType::Unknown;
    
    // Target
    char ssid[DUPIN_SSID_MAX_LEN + 1] = {0};
    uint8_t bssid[MAC_LEN] = {0};
    
    // Assessment
    float confidence = 0.0f;    // 0.0 - 1.0
    ThreatLevel threat = ThreatLevel::None;
    
    // Evidence
    Evidence evidence[MAX_EVIDENCE];
    uint8_t evidenceCount = 0;
    
    // Timing
    uint32_t createdAt = 0;
    uint32_t updatedAt = 0;
    
    // Description
    char summary[128] = {0};
    
    /**
     * @brief Set SSID
     */
    void setSSID(const char* s) {
        if (s == nullptr) { ssid[0] = '\0'; return; }
        size_t i = 0;
        while (i < DUPIN_SSID_MAX_LEN && s[i] != '\0') { ssid[i] = s[i]; i++; }
        ssid[i] = '\0';
    }
    
    /**
     * @brief Set summary text
     */
    void setSummary(const char* text) {
        if (text == nullptr) { summary[0] = '\0'; return; }
        size_t i = 0;
        while (i < 127 && text[i] != '\0') { summary[i] = text[i]; i++; }
        summary[i] = '\0';
    }
    
    /**
     * @brief Add evidence to hypothesis
     */
    bool addEvidence(Evidence::Type type, uint32_t itemId, float weight = 1.0f) {
        if (evidenceCount >= MAX_EVIDENCE) return false;
        
        // Check for duplicate
        for (uint8_t i = 0; i < evidenceCount; i++) {
            if (evidence[i].type == type && evidence[i].id == itemId) {
                // Update weight if higher
                if (weight > evidence[i].weight) {
                    evidence[i].weight = weight;
                }
                return true;
            }
        }
        
        evidence[evidenceCount].type = type;
        evidence[evidenceCount].id = itemId;
        evidence[evidenceCount].weight = weight;
        evidenceCount++;
        return true;
    }
    
    /**
     * @brief Calculate total evidence weight
     */
    float totalEvidenceWeight() const {
        float total = 0.0f;
        for (uint8_t i = 0; i < evidenceCount; i++) {
            total += evidence[i].weight;
        }
        return total;
    }
    
    /**
     * @brief Check if hypothesis is active
     */
    bool isActive() const {
        return type != HypothesisType::Unknown && confidence > 0.0f;
    }
    
    /**
     * @brief Clear hypothesis
     */
    void clear() {
        *this = Hypothesis{};
    }
    
    /**
     * @brief Check if has SSID
     */
    bool hasSSID() const { return ssid[0] != '\0'; }
    
    /**
     * @brief Check if has BSSID
     */
    bool hasBSSID() const { return !macIsNull(bssid); }
};

/**
 * @brief Get display name for hypothesis type
 */
inline const char* hypothesisTypeName(HypothesisType type) {
    switch (type) {
        case HypothesisType::EvilTwin: return "Evil Twin";
        case HypothesisType::RogueAP: return "Rogue AP";
        case HypothesisType::PineappleDevice: return "Pineapple Device";
        case HypothesisType::DeauthAttack: return "Deauth Attack";
        case HypothesisType::ProbeHarvesting: return "Probe Harvesting";
        case HypothesisType::Misconfiguration: return "Misconfiguration";
        case HypothesisType::Interference: return "Interference";
        case HypothesisType::DeviceMovement: return "Device Moving";
        default: return "Unknown";
    }
}

/**
 * @brief Get display name for threat level
 */
inline const char* threatLevelName(ThreatLevel level) {
    switch (level) {
        case ThreatLevel::None: return "NONE";
        case ThreatLevel::Low: return "LOW";
        case ThreatLevel::Medium: return "MEDIUM";
        case ThreatLevel::High: return "HIGH";
        case ThreatLevel::Critical: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Get default threat level for hypothesis type
 */
inline ThreatLevel defaultThreatLevel(HypothesisType type) {
    switch (type) {
        case HypothesisType::EvilTwin: return ThreatLevel::Critical;
        case HypothesisType::RogueAP: return ThreatLevel::High;
        case HypothesisType::PineappleDevice: return ThreatLevel::Critical;
        case HypothesisType::DeauthAttack: return ThreatLevel::High;
        case HypothesisType::ProbeHarvesting: return ThreatLevel::Medium;
        case HypothesisType::Misconfiguration: return ThreatLevel::Low;
        case HypothesisType::Interference: return ThreatLevel::None;
        case HypothesisType::DeviceMovement: return ThreatLevel::None;
        default: return ThreatLevel::None;
    }
}

} // namespace dupin
