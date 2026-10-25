/**
 * @file baskerville.h
 * @brief Baskerville Mode - Rogue AP Hunting
 * 
 * "Mr. Holmes, they were the footprints of a gigantic hound!"
 * 
 * Baskerville hunts down rogue APs physically using RSSI tracking.
 * Follow the signal to find the device.
 */

#pragma once

#include <cstdlib>  // for abs()
#include "modes/mode.h"
#include "core/hypothesis.h"
#include "utils/mac_utils.h"
#include "utils/time_utils.h"

namespace dupin {

// RSSI tracking settings
constexpr size_t RSSI_HISTORY_SIZE = 16;
constexpr uint32_t RSSI_SAMPLE_INTERVAL_MS = 500;
constexpr int8_t MIN_RSSI_CHANGE_FOR_TREND = 3;  // dB

/**
 * @brief Movement trend relative to target
 */
enum class HuntTrend : uint8_t {
    Unknown = 0,
    Approaching,   // Signal getting stronger
    Receding,      // Signal getting weaker
    Static,        // Signal stable
    Lost           // Target not seen recently
};

/**
 * @brief Signal quality assessment
 */
enum class SignalQuality : uint8_t {
    Excellent = 0,  // > -50 dBm
    Good = 1,       // -50 to -60 dBm
    Fair = 2,       // -60 to -70 dBm
    Weak = 3,       // -70 to -80 dBm
    VeryWeak = 4    // < -80 dBm
};

/**
 * @brief Current hunt status
 */
struct HuntStatus {
    // Target
    uint8_t targetBSSID[MAC_LEN] = {0};
    char targetSSID[DUPIN_SSID_MAX_LEN + 1] = {0};
    uint32_t hypothesisId = 0;
    
    // Current state
    HuntTrend trend = HuntTrend::Unknown;
    SignalQuality quality = SignalQuality::VeryWeak;
    float trendConfidence = 0.0f;
    
    // RSSI tracking
    int8_t currentRSSI = -128;
    int8_t peakRSSI = -128;
    int8_t rssiGradient = 0;  // dB/sample, positive = approaching
    
    // History
    int8_t rssiHistory[RSSI_HISTORY_SIZE] = {0};
    uint8_t rssiCount = 0;
    
    // Timing
    uint32_t lastSeen = 0;
    uint32_t huntStartTime = 0;
    uint32_t sampleCount = 0;
    
    // State
    bool isActive = false;
    
    /**
     * @brief Set target SSID
     */
    void setSSID(const char* s) {
        if (s == nullptr) { targetSSID[0] = '\0'; return; }
        size_t i = 0;
        while (i < DUPIN_SSID_MAX_LEN && s[i] != '\0') { targetSSID[i] = s[i]; i++; }
        targetSSID[i] = '\0';
    }
    
    /**
     * @brief Record new RSSI sample
     */
    void recordRSSI(int8_t rssi, uint32_t timestamp) {
        currentRSSI = rssi;
        lastSeen = timestamp;
        sampleCount++;
        
        if (rssi > peakRSSI) peakRSSI = rssi;
        
        // Add to history
        if (rssiCount < RSSI_HISTORY_SIZE) {
            rssiHistory[rssiCount++] = rssi;
        } else {
            // Shift history
            for (size_t i = 0; i < RSSI_HISTORY_SIZE - 1; i++) {
                rssiHistory[i] = rssiHistory[i + 1];
            }
            rssiHistory[RSSI_HISTORY_SIZE - 1] = rssi;
        }
        
        // Calculate gradient
        if (rssiCount >= 4) {
            // Compare first and last quarters
            int8_t firstAvg = (rssiHistory[0] + rssiHistory[1]) / 2;
            int8_t lastAvg = (rssiHistory[rssiCount-2] + rssiHistory[rssiCount-1]) / 2;
            rssiGradient = lastAvg - firstAvg;
        }
        
        // Update quality
        if (rssi > -50) quality = SignalQuality::Excellent;
        else if (rssi > -60) quality = SignalQuality::Good;
        else if (rssi > -70) quality = SignalQuality::Fair;
        else if (rssi > -80) quality = SignalQuality::Weak;
        else quality = SignalQuality::VeryWeak;
    }
    
    /**
     * @brief Calculate trend from gradient
     */
    void updateTrend() {
        if (rssiCount < 4) {
            trend = HuntTrend::Unknown;
            trendConfidence = 0.0f;
            return;
        }
        
        if (rssiGradient > MIN_RSSI_CHANGE_FOR_TREND) {
            trend = HuntTrend::Approaching;
            trendConfidence = static_cast<float>(rssiGradient) / 20.0f;
        } else if (rssiGradient < -MIN_RSSI_CHANGE_FOR_TREND) {
            trend = HuntTrend::Receding;
            trendConfidence = static_cast<float>(-rssiGradient) / 20.0f;
        } else {
            trend = HuntTrend::Static;
            trendConfidence = 1.0f - (static_cast<float>(abs(rssiGradient)) / 3.0f);
        }
        
        if (trendConfidence > 1.0f) trendConfidence = 1.0f;
        if (trendConfidence < 0.0f) trendConfidence = 0.0f;
    }
    
    /**
     * @brief Clear hunt status
     */
    void clear() {
        *this = HuntStatus{};
    }
};

/**
 * @brief Baskerville mode configuration
 */
struct BaskervilleConfig {
    uint32_t lostTimeoutMs = 10000;     // Mark as lost after 10s without signal
    uint32_t sampleIntervalMs = RSSI_SAMPLE_INTERVAL_MS;
};

/**
 * @brief Baskerville Mode - The Hound Hunter
 * 
 * Physical tracking of rogue APs via RSSI gradient analysis.
 * Provides hot/cold feedback to guide the user to the device.
 */
class BaskervilleMode : public IMode {
public:
    BaskervilleMode();
    
    // IMode interface
    const char* name() const override { return "Baskerville"; }
    void onObservation(const Observation& obs) override;
    void update() override;
    void begin() override;
    void end() override;
    
    /**
     * @brief Start hunting a target from a hypothesis
     */
    bool startHunt(const Hypothesis& target);
    
    /**
     * @brief Start hunting a specific BSSID
     */
    bool startHunt(const uint8_t* bssid, const char* ssid = nullptr);
    
    /**
     * @brief Stop current hunt
     */
    void stopHunt();
    
    /**
     * @brief Check if actively hunting
     */
    bool isHunting() const { return m_status.isActive; }
    
    /**
     * @brief Get current hunt status
     */
    const HuntStatus& getStatus() const { return m_status; }
    
    /**
     * @brief Get configuration
     */
    const BaskervilleConfig& getConfig() const { return m_config; }
    void setConfig(const BaskervilleConfig& config) { m_config = config; }
    
    /**
     * @brief Get hot/cold indicator (-100 to +100)
     * Negative = receding (cold), Positive = approaching (hot)
     */
    int8_t getHotColdIndicator() const;
    
    /**
     * @brief Get text description of current status
     */
    const char* getStatusText() const;

private:
    void checkForLostTarget();
    
    BaskervilleConfig m_config;
    HuntStatus m_status;
    uint32_t m_lastStatusUpdate = 0;
};

/**
 * @brief Get display name for hunt trend
 */
inline const char* huntTrendName(HuntTrend trend) {
    switch (trend) {
        case HuntTrend::Approaching: return "APPROACHING";
        case HuntTrend::Receding: return "RECEDING";
        case HuntTrend::Static: return "STATIC";
        case HuntTrend::Lost: return "LOST";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Get display name for signal quality
 */
inline const char* signalQualityName(SignalQuality q) {
    switch (q) {
        case SignalQuality::Excellent: return "EXCELLENT";
        case SignalQuality::Good: return "GOOD";
        case SignalQuality::Fair: return "FAIR";
        case SignalQuality::Weak: return "WEAK";
        case SignalQuality::VeryWeak: return "VERY WEAK";
        default: return "UNKNOWN";
    }
}

} // namespace dupin
