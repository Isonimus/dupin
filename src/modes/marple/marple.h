/**
 * @file marple.h
 * @brief Marple Mode - Anomaly Detection
 * 
 * Miss Marple notices when things are "not quite right."
 * Compares observations against the baseline and detects
 * deviations that could indicate an attack.
 */

#pragma once

#include "modes/mode.h"
#include "core/anomaly.h"
#include "core/baseline.h"
#include "storage/ring_buffer.h"
#include "utils/time_utils.h"

namespace dupin {

// Maximum number of active anomalies to track
constexpr size_t MAX_ANOMALIES = 32;

// Detection thresholds
constexpr int8_t DEFAULT_RSSI_DEVIATION_THRESHOLD = 25;  // dBm
constexpr uint32_t DEFAULT_DEAUTH_THRESHOLD = 10;        // per window
constexpr uint32_t DEFAULT_PROBE_THRESHOLD = 50;         // per window
constexpr uint32_t ANOMALY_WINDOW_MS = 60000;            // 1 minute
constexpr float CONFIDENCE_DECAY_PER_SECOND = 0.0f;      // Disabled - manual dismiss only (ADR-0003)
constexpr float MIN_CONFIDENCE_THRESHOLD = 0.1f;         // Below this = expired

// Attack detection thresholds
constexpr uint32_t TARGETED_DEAUTH_THRESHOLD = 10;       // deauths to same victim in window
constexpr uint32_t TARGETED_DEAUTH_WINDOW_MS = 5000;     // 5 second window
constexpr uint32_t KARMA_SSID_THRESHOLD = 5;             // SSIDs from same BSSID
constexpr uint32_t KARMA_WINDOW_MS = 30000;              // 30 second window
constexpr uint32_t BEACON_FLOOD_THRESHOLD = 20;          // new SSIDs in window
constexpr uint32_t BEACON_FLOOD_WINDOW_MS = 10000;       // 10 second window
constexpr uint32_t CLONE_SPAM_THRESHOLD = 3;             // BSSIDs for same SSID

/**
 * @brief Marple mode configuration
 */
struct MarpleConfig {
    int8_t rssiDeviationThreshold = DEFAULT_RSSI_DEVIATION_THRESHOLD;
    uint32_t deauthThreshold = DEFAULT_DEAUTH_THRESHOLD;
    uint32_t probeThreshold = DEFAULT_PROBE_THRESHOLD;
    uint32_t windowMs = ANOMALY_WINDOW_MS;
    float confidenceDecayRate = CONFIDENCE_DECAY_PER_SECOND;
    float minConfidence = MIN_CONFIDENCE_THRESHOLD;
    
    // Detection toggles
    bool detectNewBSSID = true;
    bool detectUnexpectedChannel = true;
    bool detectRSSIAnomaly = true;
    bool detectSecurityDowngrade = true;
    bool detectExcessiveDeauths = true;
};

/**
 * @brief Marple Mode - The Observer Who Notices
 * 
 * "I always find that everyone has their little part to play."
 * - Miss Marple watches for anything out of the ordinary.
 */
class MarpleMode : public IMode {
public:
    MarpleMode();
    
    // IMode interface
    const char* name() const override { return "Marple"; }
    void onObservation(const Observation& obs) override;
    void update() override;
    void begin() override;
    void end() override;
    
    /**
     * @brief Set the baseline to compare against
     */
    void setBaseline(const Baseline* baseline) { m_baseline = baseline; }
    const Baseline* getBaseline() const { return m_baseline; }
    
    /**
     * @brief Get all active anomalies
     */
    const Anomaly* getAnomalies() const { return m_anomalies; }
    
    /**
     * @brief Get count of active anomalies
     */
    size_t getAnomalyCount() const;
    
    /**
     * @brief Get a specific anomaly by ID
     */
    const Anomaly* findAnomaly(uint32_t id) const;
    
    /**
     * @brief Get configuration
     */
    const MarpleConfig& getConfig() const { return m_config; }
    MarpleConfig& getConfig() { return m_config; }
    void setConfig(const MarpleConfig& config) { m_config = config; }
    
    /**
     * @brief Clear all detected anomalies
     */
    void clearAnomalies();
    
    /**
     * @brief Dismiss a specific anomaly by index (manual dismiss)
     * @return The anomaly ID if dismissed, 0 if invalid index
     */
    uint32_t dismissAnomaly(size_t index);
    
    /**
     * @brief Callback type for new anomaly alerts
     */
    using AnomalyCallback = void(*)(const Anomaly& anomaly);
    
    /**
     * @brief Set callback for new anomalies
     */
    void setAnomalyCallback(AnomalyCallback cb) { m_callback = cb; }
    
    /**
     * @brief Get detection statistics
     */
    struct DetectionStats {
        uint32_t newBSSIDs = 0;
        uint32_t unexpectedChannels = 0;
        uint32_t rssiAnomalies = 0;
        uint32_t securityDowngrades = 0;
        uint32_t excessiveDeauths = 0;
        uint32_t totalAnomalies = 0;
    };
    
    const DetectionStats& getDetectionStats() const { return m_stats; }
    void resetStats() { m_stats = DetectionStats{}; }

private:
    // Detection methods (baseline comparison)
    bool checkNewBSSID(const Observation& obs);
    bool checkUnexpectedChannel(const Observation& obs);
    bool checkRSSIAnomaly(const Observation& obs);
    bool checkSecurityDowngrade(const Observation& obs);
    bool checkExcessiveDeauths(const Observation& obs);
    
    // Attack detection methods (pattern matching)
    bool checkTargetedDeauth(const Observation& obs);
    bool checkKarmaAP(const Observation& obs);
    bool checkBeaconFlood(const Observation& obs);
    bool checkCloneSpam(const Observation& obs);
    bool checkTargetedProbeFlood(const Observation& obs);
    bool checkAPlessClient(const Observation& obs);
    bool checkEvilTwinCombo();  // Correlation check, no obs needed
    
    // Anomaly management
    Anomaly* findOrCreateAnomaly(AnomalyType type, const char* ssid, 
                                  const uint8_t* bssid);
    void raiseAnomaly(const Anomaly& anomaly);
    void decayConfidence();
    void expireOldAnomalies();
    
    // Deauth/probe flood tracking (global)
    struct FloodTracker {
        uint32_t windowStart = 0;
        uint32_t deauthCount = 0;
        uint32_t probeCount = 0;
        
        void reset(uint32_t timestamp) {
            windowStart = timestamp;
            deauthCount = 0;
            probeCount = 0;
        }
    };
    
    // Targeted deauth tracking (per MAC pair)
    struct DeauthPair {
        uint8_t attacker[MAC_LEN] = {0};
        uint8_t victim[MAC_LEN] = {0};
        uint32_t count = 0;
        uint32_t windowStart = 0;
        bool active = false;
    };
    static constexpr size_t MAX_DEAUTH_PAIRS = 16;
    
    // Karma AP tracking (BSSID -> SSIDs)
    struct KarmaTracker {
        uint8_t bssid[MAC_LEN] = {0};
        char ssids[8][DUPIN_SSID_MAX_LEN + 1] = {{0}};
        uint8_t ssidCount = 0;
        uint32_t firstSeen = 0;
        bool active = false;
        
        bool hasSSID(const char* ssid) const {
            for (uint8_t i = 0; i < ssidCount; i++) {
                if (strcmp(ssids[i], ssid) == 0) return true;
            }
            return false;
        }
    };
    static constexpr size_t MAX_KARMA_TRACKERS = 8;
    
    // Beacon flood tracking (new SSID rate)
    struct SSIDFloodTracker {
        uint32_t windowStart = 0;
        uint32_t newSSIDCount = 0;
        
        void reset(uint32_t timestamp) {
            windowStart = timestamp;
            newSSIDCount = 0;
        }
    };
    
    // Per-MAC probe flood tracking (reconnaissance detection)
    struct ProbeTracker {
        uint8_t mac[MAC_LEN] = {0};
        char ssids[16][DUPIN_SSID_MAX_LEN + 1] = {{0}};
        uint8_t ssidCount = 0;
        uint32_t firstSeen = 0;
        bool active = false;
        
        bool hasSSID(const char* ssid) const {
            for (uint8_t i = 0; i < ssidCount; i++) {
                if (strcmp(ssids[i], ssid) == 0) return true;
            }
            return false;
        }
    };
    static constexpr size_t MAX_PROBE_TRACKERS = 8;
    
    // Beacon tracker for AP-less detection
    struct BeaconEntry {
        char ssid[DUPIN_SSID_MAX_LEN + 1] = {0};
        uint8_t bssid[MAC_LEN] = {0};
        uint32_t lastSeen = 0;
        bool active = false;
    };
    static constexpr size_t MAX_BEACON_ENTRIES = 32;
    
    // Configuration
    MarpleConfig m_config;
    
    // State
    const Baseline* m_baseline = nullptr;
    Anomaly m_anomalies[MAX_ANOMALIES];
    uint32_t m_nextAnomalyId = 1;
    FloodTracker m_floodTracker;
    uint32_t m_lastDecayTime = 0;
    uint32_t m_startTime = 0;  // For startup grace period
    
    // Attack trackers
    DeauthPair m_deauthPairs[MAX_DEAUTH_PAIRS];
    KarmaTracker m_karmaTrackers[MAX_KARMA_TRACKERS];
    SSIDFloodTracker m_ssidFloodTracker;
    ProbeTracker m_probeTrackers[MAX_PROBE_TRACKERS];
    BeaconEntry m_beaconCache[MAX_BEACON_ENTRIES];
    
    // Callback
    AnomalyCallback m_callback = nullptr;
    
    // Statistics
    DetectionStats m_stats;
};

} // namespace dupin
