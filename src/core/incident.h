/**
 * @file incident.h
 * @brief Incident storage and forensic reconstruction
 * 
 * "The game is afoot!" - An incident captures a complete security
 * event: anomalies, findings, hypothesis, and resolution.
 */

#pragma once

#include <cstdint>
#include "dupin.h"
#include "core/anomaly.h"
#include "core/finding.h"
#include "core/hypothesis.h"
#include "utils/mac_utils.h"

namespace dupin {

// Maximum items per incident
constexpr size_t MAX_INCIDENT_ANOMALIES = 8;
constexpr size_t MAX_INCIDENT_FINDINGS = 8;

/**
 * @brief Incident resolution status
 */
enum class IncidentStatus : uint8_t {
    Active = 0,       // Ongoing, still gathering evidence
    Resolved = 1,     // Closed with conclusion
    Dismissed = 2,    // False positive / non-threat
    Escalated = 3     // Handed off for further action
};

/**
 * @brief A complete security incident
 * 
 * Captures the full lifecycle of a detection event from
 * initial observation through conclusion.
 */
struct Incident {
    // Identity
    uint32_t id = 0;
    IncidentStatus status = IncidentStatus::Active;
    
    // Target
    char ssid[DUPIN_SSID_MAX_LEN + 1] = {0};
    uint8_t bssid[MAC_LEN] = {0};
    
    // Conclusion
    Hypothesis conclusion;
    
    // Evidence - anomaly IDs
    uint32_t anomalyIds[MAX_INCIDENT_ANOMALIES] = {0};
    uint8_t anomalyCount = 0;
    
    // Evidence - finding IDs
    uint32_t findingIds[MAX_INCIDENT_FINDINGS] = {0};
    uint8_t findingCount = 0;
    
    // Timing
    uint32_t createdAt = 0;
    uint32_t updatedAt = 0;
    uint32_t resolvedAt = 0;
    
    // Metrics
    uint32_t observationCount = 0;
    int8_t peakRSSI = -128;
    
    // Notes
    char notes[128] = {0};
    
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
     * @brief Set notes
     */
    void setNotes(const char* text) {
        if (text == nullptr) { notes[0] = '\0'; return; }
        size_t i = 0;
        while (i < 127 && text[i] != '\0') { notes[i] = text[i]; i++; }
        notes[i] = '\0';
    }
    
    /**
     * @brief Add anomaly ID to incident
     */
    bool addAnomaly(uint32_t anomalyId) {
        if (anomalyCount >= MAX_INCIDENT_ANOMALIES) return false;
        
        // Check for duplicate
        for (uint8_t i = 0; i < anomalyCount; i++) {
            if (anomalyIds[i] == anomalyId) return true;  // Already added
        }
        
        anomalyIds[anomalyCount++] = anomalyId;
        return true;
    }
    
    /**
     * @brief Add finding ID to incident
     */
    bool addFinding(uint32_t findingId) {
        if (findingCount >= MAX_INCIDENT_FINDINGS) return false;
        
        // Check for duplicate
        for (uint8_t i = 0; i < findingCount; i++) {
            if (findingIds[i] == findingId) return true;  // Already added
        }
        
        findingIds[findingCount++] = findingId;
        return true;
    }
    
    /**
     * @brief Get total evidence count
     */
    uint8_t totalEvidence() const {
        return anomalyCount + findingCount;
    }
    
    /**
     * @brief Check if incident is active
     */
    bool isActive() const {
        return id != 0 && status == IncidentStatus::Active;
    }
    
    /**
     * @brief Resolve the incident
     */
    void resolve(uint32_t timestamp) {
        status = IncidentStatus::Resolved;
        resolvedAt = timestamp;
        updatedAt = timestamp;
    }
    
    /**
     * @brief Dismiss as false positive
     */
    void dismiss(uint32_t timestamp) {
        status = IncidentStatus::Dismissed;
        resolvedAt = timestamp;
        updatedAt = timestamp;
    }
    
    /**
     * @brief Clear the incident
     */
    void clear() {
        *this = Incident{};
    }
    
    /**
     * @brief Check if has SSID
     */
    bool hasSSID() const { return ssid[0] != '\0'; }
    
    /**
     * @brief Check if has BSSID
     */
    bool hasBSSID() const { return !macIsNull(bssid); }
    
    /**
     * @brief Get duration in ms
     */
    uint32_t duration(uint32_t currentTime) const {
        if (resolvedAt > 0) {
            return resolvedAt - createdAt;
        }
        return currentTime - createdAt;
    }
};

/**
 * @brief Get display name for incident status
 */
inline const char* incidentStatusName(IncidentStatus status) {
    switch (status) {
        case IncidentStatus::Active: return "ACTIVE";
        case IncidentStatus::Resolved: return "RESOLVED";
        case IncidentStatus::Dismissed: return "DISMISSED";
        case IncidentStatus::Escalated: return "ESCALATED";
        default: return "UNKNOWN";
    }
}

} // namespace dupin
