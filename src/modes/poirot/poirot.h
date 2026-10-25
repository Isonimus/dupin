/**
 * @file poirot.h
 * @brief Poirot Mode - Follow-Up Heuristics
 * 
 * "The little grey cells, they are what we must use."
 * Poirot methodically investigates anomalies with deeper analysis,
 * looking for additional evidence to confirm or dismiss threats.
 */

#pragma once

#include "modes/mode.h"
#include "core/anomaly.h"
#include "core/finding.h"
#include "core/baseline.h"
#include "storage/ring_buffer.h"
#include "utils/time_utils.h"

namespace dupin {

// Maximum investigations and findings
constexpr size_t MAX_INVESTIGATIONS = 16;
constexpr size_t MAX_FINDINGS = 32;
constexpr uint32_t DEFAULT_INVESTIGATION_TIMEOUT_MS = 300000;  // 5 minutes

/**
 * @brief An active investigation into an anomaly
 */
struct Investigation {
    uint32_t anomalyId = 0;
    AnomalyType anomalyType = AnomalyType::Unknown;
    char ssid[DUPIN_SSID_MAX_LEN + 1] = {0};
    uint8_t bssid[MAC_LEN] = {0};
    
    // Timing
    uint32_t startTime = 0;
    uint32_t lastActivity = 0;
    uint32_t timeoutMs = DEFAULT_INVESTIGATION_TIMEOUT_MS;
    
    // Probe tracking for promiscuity detection
    uint8_t probesSent = 0;
    uint8_t probeResponses = 0;
    
    // RSSI tracking for movement detection
    int8_t rssiHistory[8] = {0};
    uint8_t rssiCount = 0;
    
    // Channel tracking
    uint8_t channelHistory[8] = {0};
    uint8_t channelCount = 0;
    
    bool isActive() const { return anomalyId != 0; }
    
    void clear() { *this = Investigation{}; }
    
    void setSSID(const char* s) {
        if (s == nullptr) { ssid[0] = '\0'; return; }
        size_t i = 0;
        while (i < DUPIN_SSID_MAX_LEN && s[i] != '\0') { ssid[i] = s[i]; i++; }
        ssid[i] = '\0';
    }
    
    void recordRSSI(int8_t rssi) {
        if (rssiCount < 8) {
            rssiHistory[rssiCount++] = rssi;
        } else {
            // Shift and add
            for (int i = 0; i < 7; i++) rssiHistory[i] = rssiHistory[i+1];
            rssiHistory[7] = rssi;
        }
    }
    
    int8_t rssiGradient() const {
        if (rssiCount < 2) return 0;
        return rssiHistory[rssiCount-1] - rssiHistory[0];
    }
    
    void recordChannel(uint8_t ch) {
        // Don't record duplicates
        for (uint8_t i = 0; i < channelCount; i++) {
            if (channelHistory[i] == ch) return;
        }
        if (channelCount < 8) {
            channelHistory[channelCount++] = ch;
        }
    }
};

/**
 * @brief Poirot mode configuration
 */
struct PoirotConfig {
    uint32_t investigationTimeoutMs = DEFAULT_INVESTIGATION_TIMEOUT_MS;
    
    // Detection thresholds
    uint8_t probePromiscuityThreshold = 3;  // Unexpected responses
    int8_t movementRSSIThreshold = 10;       // dB change
    uint8_t channelHopThreshold = 3;         // Different channels
};

/**
 * @brief Poirot Mode - The Methodical Investigator
 * 
 * Poirot takes anomalies flagged by Marple and investigates them
 * with additional heuristics to gather more evidence.
 */
class PoirotMode : public IMode {
public:
    PoirotMode();
    
    // IMode interface
    const char* name() const override { return "Poirot"; }
    void onObservation(const Observation& obs) override;
    void update() override;
    void begin() override;
    void end() override;
    
    /**
     * @brief Start investigating an anomaly
     */
    bool investigate(const Anomaly& anomaly);
    
    /**
     * @brief Get all findings
     */
    const Finding* getFindings() const { return m_findings; }
    
    /**
     * @brief Get finding count
     */
    size_t getFindingCount() const;
    
    /**
     * @brief Get findings for a specific anomaly
     */
    size_t getFindingsForAnomaly(uint32_t anomalyId, Finding* out, size_t maxResults) const;
    
    /**
     * @brief Get active investigations
     */
    const Investigation* getInvestigations() const { return m_investigations; }
    
    /**
     * @brief Get investigation count
     */
    size_t getInvestigationCount() const;
    
    /**
     * @brief Get configuration
     */
    const PoirotConfig& getConfig() const { return m_config; }
    void setConfig(const PoirotConfig& config) { m_config = config; }
    
    /**
     * @brief Clear all investigations and findings
     */
    void clear();
    
    /**
     * @brief Remove all findings related to an anomaly (cascade delete)
     * @return Number of findings removed
     */
    size_t removeFindingsForAnomaly(uint32_t anomalyId);
    
    /**
     * @brief Dismiss a specific finding by index (manual dismiss)
     * @return True if dismissed
     */
    bool dismissFinding(size_t index);
    
    /**
     * @brief Callback for new findings
     */
    using FindingCallback = void(*)(const Finding& finding);
    void setFindingCallback(FindingCallback cb) { m_callback = cb; }
    
    /**
     * @brief Set baseline for security comparison
     */
    void setBaseline(const Baseline* baseline) { m_baseline = baseline; }

private:
    // Investigation methods
    void processForInvestigation(const Observation& obs, Investigation& inv);
    void checkProbePromiscuity(const Observation& obs, Investigation& inv);
    void checkMovement(const Observation& obs, Investigation& inv);
    void checkChannelHopping(const Observation& obs, Investigation& inv);
    void checkBeaconPersonality(const Observation& obs, Investigation& inv);
    void checkSecurityMismatch(const Observation& obs, Investigation& inv);
    
    // Finding management
    Finding* createFinding(FindingType type, uint32_t anomalyId);
    void raiseFinding(const Finding& finding);
    
    // Expiration
    void expireInvestigations();

    // Configuration
    PoirotConfig m_config;
    
    // State
    Investigation m_investigations[MAX_INVESTIGATIONS];
    Finding m_findings[MAX_FINDINGS];
    uint32_t m_nextFindingId = 1;
    
    // Baseline for security comparison
    const Baseline* m_baseline = nullptr;
    
    // Callback
    FindingCallback m_callback = nullptr;
};

} // namespace dupin
