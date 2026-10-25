/**
 * @file holmes.h
 * @brief Holmes Mode - Correlation & Hypothesis Engine
 * 
 * "When you have eliminated the impossible, whatever remains,
 * however improbable, must be the truth."
 * 
 * Holmes correlates multiple anomalies and findings to form
 * hypotheses about what's actually happening.
 */

#pragma once

#include "modes/mode.h"
#include "core/hypothesis.h"
#include "core/anomaly.h"
#include "core/finding.h"
#include "utils/time_utils.h"

namespace dupin {

// Maximum hypotheses to track
constexpr size_t MAX_HYPOTHESES = 16;
constexpr float HYPOTHESIS_CONFIDENCE_DECAY = 0.0f;     // Disabled - cascade from anomaly dismiss (ADR-0003)
constexpr float MIN_HYPOTHESIS_CONFIDENCE = 0.05f;

/**
 * @brief Holmes mode configuration
 */
struct HolmesConfig {
    float confidenceDecayRate = HYPOTHESIS_CONFIDENCE_DECAY;
    float minConfidence = MIN_HYPOTHESIS_CONFIDENCE;
    
    // Correlation thresholds
    uint32_t temporalWindowMs = 60000;   // Anomalies within this window may be related
    float minEvidenceWeight = 2.0f;      // Minimum evidence to form hypothesis
};

/**
 * @brief Holmes Mode - The Correlation Engine
 * 
 * Holmes takes anomalies from Marple and findings from Poirot
 * and correlates them into hypotheses about threats.
 */
class HolmesMode : public IMode {
public:
    HolmesMode();
    
    // IMode interface
    const char* name() const override { return "Holmes"; }
    void onObservation(const Observation& obs) override;
    void update() override;
    void begin() override;
    void end() override;
    
    /**
     * @brief Process an anomaly for correlation
     */
    void processAnomaly(const Anomaly& anomaly);
    
    /**
     * @brief Process a finding for correlation
     */
    void processFinding(const Finding& finding);
    
    /**
     * @brief Get all hypotheses
     */
    const Hypothesis* getHypotheses() const { return m_hypotheses; }
    
    /**
     * @brief Get hypothesis count
     */
    size_t getHypothesisCount() const;
    
    /**
     * @brief Get top hypothesis by confidence
     */
    const Hypothesis* getTopHypothesis() const;
    
    /**
     * @brief Get hypotheses above a threat level
     */
    size_t getHypothesesByThreat(ThreatLevel minLevel, Hypothesis* out, size_t maxResults) const;
    
    /**
     * @brief Get configuration
     */
    const HolmesConfig& getConfig() const { return m_config; }
    void setConfig(const HolmesConfig& config) { m_config = config; }
    
    /**
     * @brief Clear all hypotheses
     */
    void clear();
    
    /**
     * @brief Remove hypotheses that use a specific anomaly as evidence (cascade delete)
     * @return Number of hypotheses removed
     */
    size_t removeHypothesesForAnomaly(uint32_t anomalyId);
    
    /**
     * @brief Callback for new/updated hypotheses
     */
    using HypothesisCallback = void(*)(const Hypothesis& hypothesis);
    void setHypothesisCallback(HypothesisCallback cb) { m_callback = cb; }

private:
    // Correlation logic
    Hypothesis* correlateAnomaly(const Anomaly& anomaly);
    void updateHypothesisFromFinding(Hypothesis& hyp, const Finding& finding);
    
    // Type inference
    HypothesisType inferType(const Anomaly& anomaly);
    void adjustConfidence(Hypothesis& hyp, const Finding& finding);
    
    // Hypothesis management
    Hypothesis* findOrCreateHypothesis(const char* ssid, const uint8_t* bssid);
    Hypothesis* findHypothesisByTarget(const char* ssid, const uint8_t* bssid);
    void raiseHypothesis(const Hypothesis& hypothesis);
    void decayConfidence();
    void expireHypotheses();

    // Configuration
    HolmesConfig m_config;
    
    // State
    Hypothesis m_hypotheses[MAX_HYPOTHESES];
    uint32_t m_nextHypothesisId = 1;
    uint32_t m_lastDecayTime = 0;
    
    // Callback
    HypothesisCallback m_callback = nullptr;
    
    // Movement correlation for device movement detection
    static constexpr size_t MAX_MOVEMENT_EVENTS = 16;
    static constexpr uint32_t MOVEMENT_WINDOW_MS = 10000;  // 10 second window
    static constexpr size_t MOVEMENT_THRESHOLD = 3;        // 3+ BSSIDs = device moving
    
    struct MovementEvent {
        uint8_t bssid[6];
        int8_t direction;   // +1 = closer, -1 = farther, 0 = mixed
        uint32_t timestamp;
        bool valid = false;
    };
    MovementEvent m_movements[MAX_MOVEMENT_EVENTS];
    size_t m_movementHead = 0;
    bool m_deviceMovingActive = false;
    
    // Movement tracking methods
    void trackMovement(const Finding& finding);
    bool checkDeviceMovement();
    void createDeviceMovementHypothesis();
};

} // namespace dupin
