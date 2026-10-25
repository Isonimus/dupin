/**
 * @file watson.h
 * @brief Watson Mode - Passive logging and baselining
 * 
 * Watson is the foundation mode that:
 * - Logs all observations to serial
 * - Builds behavioral baselines for SSIDs
 * - Tracks BSSID-to-SSID mappings
 * - Records security profiles and channels
 */

#pragma once

#include "modes/mode.h"
#include "core/baseline.h"
#include "storage/observation_store.h"
#include "ui/serial_logger.h"
#include "utils/time_utils.h"

namespace dupin {

/**
 * @brief Watson mode configuration
 */
struct WatsonConfig {
    // Baseline persistence is not Watson's: the open case owns it, and loop()'s
    // g_config.baselineAutosaveInterval is the one timer that writes it (ADR-0016).
    uint32_t statsLogIntervalMs = 30000;      // Log stats every 30 seconds
    bool logNewSSIDs = true;                   // Log when new SSID is discovered
    bool logNewBSSIDs = true;                  // Log when new BSSID for known SSID
};

/**
 * @brief Watson Mode - The Observer
 * 
 * "I see and I observe." - Watson watches everything and builds
 * a comprehensive baseline of the wireless environment.
 */
class WatsonMode : public IMode {
public:
    WatsonMode();
    
    // IMode interface
    const char* name() const override { return "Watson"; }
    void onObservation(const Observation& obs) override;
    void update() override;
    void begin() override;
    void end() override;
    
    /**
     * @brief Get the current baseline
     */
    const Baseline& getBaseline() const { return m_baseline; }
    Baseline& getBaseline() { return m_baseline; }
    
    /**
     * @brief Get the observation store
     */
    const ObservationStore<>& getStore() const { return m_store; }
    
    /**
     * @brief Get configuration
     */
    const WatsonConfig& getConfig() const { return m_config; }
    WatsonConfig& getConfig() { return m_config; }
    
    /**
     * @brief Set configuration
     */
    void setConfig(const WatsonConfig& config) { m_config = config; }
    
    /**
     * @brief Clear the baseline
     */
    void clearBaseline() { 
        m_baseline.clear();
        m_observationCount = 0;
    }
    
    /**
     * @brief Get total observations processed
     */
    uint32_t getObservationCount() const { return m_observationCount; }
    
    /**
     * @brief Get session statistics
     */
    struct SessionStats {
        uint32_t observations = 0;
        uint32_t newSSIDs = 0;
        uint32_t newBSSIDs = 0;
        uint32_t beacons = 0;
        uint32_t probeRequests = 0;
        uint32_t deauths = 0;
        uint32_t sessionStartTime = 0;
    };
    
    const SessionStats& getSessionStats() const { return m_sessionStats; }
    void resetSessionStats();
    
    /**
     * @brief Callback for new SSID discovery notifications
     */
    using SSIDDiscoveryCallback = void(*)(const char* ssid, const uint8_t* bssid);
    void setSSIDCallback(SSIDDiscoveryCallback cb) { m_ssidCallback = cb; }

private:
    /**
     * @brief Update baseline with new observation
     */
    void updateBaseline(const Observation& obs);
    
    /**
     * @brief Check if this is a new SSID
     */
    bool isNewSSID(const char* ssid) const;
    
    /**
     * @brief Check if this is a new BSSID for a known SSID
     */
    bool isNewBSSIDForSSID(const char* ssid, const uint8_t* bssid) const;

    // Configuration
    WatsonConfig m_config;
    
    // State
    Baseline m_baseline;
    ObservationStore<> m_store;
    uint32_t m_observationCount = 0;
    SessionStats m_sessionStats;
    
    // Timers
    Timer m_statsTimer;
    
    // Callback for new SSID notifications
    SSIDDiscoveryCallback m_ssidCallback = nullptr;
};

} // namespace dupin
