/**
 * @file baskerville.cpp
 * @brief Baskerville Mode implementation
 */

#include "modes/baskerville/baskerville.h"
#include "ui/serial_logger.h"

namespace dupin {

BaskervilleMode::BaskervilleMode() {
    m_status.clear();
}

void BaskervilleMode::begin() {
    setActive(true);
    m_status.clear();
    logger.logStatus("Baskerville mode active - ready to hunt");
}

void BaskervilleMode::end() {
    stopHunt();
    setActive(false);
    logger.logStatus("Baskerville mode stopped");
}

bool BaskervilleMode::startHunt(const Hypothesis& target) {
    if (!isActive()) return false;
    
    if (!target.hasBSSID()) {
        logger.logError("Cannot hunt - no target BSSID");
        return false;
    }
    
    m_status.clear();
    m_status.isActive = true;
    m_status.hypothesisId = target.id;
    macCopy(m_status.targetBSSID, target.bssid);
    
    if (target.hasSSID()) {
        m_status.setSSID(target.ssid);
    }
    
    m_status.huntStartTime = getTimestamp();
    
    char macStr[18];
    macFormat(m_status.targetBSSID, macStr);
    
    logger.logStatus("Hunt started!");
    Serial.printf("  Target: %s\n", macStr);
    if (m_status.targetSSID[0] != '\0') {
        Serial.printf("  SSID: \"%s\"\n", m_status.targetSSID);
    }
    Serial.println("  Move around to track the signal...");
    
    return true;
}

bool BaskervilleMode::startHunt(const uint8_t* bssid, const char* ssid) {
    if (!isActive()) return false;
    
    if (bssid == nullptr || macIsNull(bssid)) {
        logger.logError("Cannot hunt - invalid BSSID");
        return false;
    }
    
    m_status.clear();
    m_status.isActive = true;
    macCopy(m_status.targetBSSID, bssid);
    
    if (ssid != nullptr) {
        m_status.setSSID(ssid);
    }
    
    m_status.huntStartTime = getTimestamp();
    
    char macStr[18];
    macFormat(m_status.targetBSSID, macStr);
    
    logger.logStatus("Hunt started!");
    Serial.printf("  Target: %s\n", macStr);
    if (m_status.targetSSID[0] != '\0') {
        Serial.printf("  SSID: \"%s\"\n", m_status.targetSSID);
    }
    
    return true;
}

void BaskervilleMode::stopHunt() {
    if (m_status.isActive) {
        logger.logStatus("Hunt stopped");
        
        if (m_status.sampleCount > 0) {
            Serial.printf("  Samples: %u\n", static_cast<unsigned>(m_status.sampleCount));
            Serial.printf("  Peak RSSI: %d dBm\n", m_status.peakRSSI);
        }
    }
    
    m_status.clear();
}

void BaskervilleMode::onObservation(const Observation& obs) {
    if (!isActive() || !m_status.isActive) return;
    
    // Check if this observation is from our target
    if (!obs.hasBSSID()) return;
    if (!macEquals(obs.bssid, m_status.targetBSSID)) return;
    
    // Record the RSSI
    m_status.recordRSSI(obs.rssi, obs.timestamp ? obs.timestamp : getTimestamp());
    m_status.updateTrend();
    
    // Periodic status output
    uint32_t now = getTimestamp();
    if (now - m_lastStatusUpdate >= 2000) {  // Every 2 seconds
        m_lastStatusUpdate = now;
        
        Serial.printf("  [%s] %d dBm (%s) %s\n",
                      huntTrendName(m_status.trend),
                      m_status.currentRSSI,
                      signalQualityName(m_status.quality),
                      getStatusText());
    }
}

void BaskervilleMode::update() {
    if (!isActive() || !m_status.isActive) return;
    
    checkForLostTarget();
}

void BaskervilleMode::checkForLostTarget() {
    if (m_status.lastSeen == 0) return;
    
    uint32_t elapsed = getTimestamp() - m_status.lastSeen;
    
    if (elapsed > m_config.lostTimeoutMs) {
        if (m_status.trend != HuntTrend::Lost) {
            m_status.trend = HuntTrend::Lost;
            m_status.trendConfidence = 0.0f;
            logger.logWarning("Target signal lost!");
            Serial.println("  Move closer or return to last known location");
        }
    }
}

int8_t BaskervilleMode::getHotColdIndicator() const {
    if (!m_status.isActive) return 0;
    if (m_status.trend == HuntTrend::Lost) return -100;
    
    // Map gradient to -100..+100 scale
    int16_t indicator = m_status.rssiGradient * 10;
    
    if (indicator > 100) indicator = 100;
    if (indicator < -100) indicator = -100;
    
    return static_cast<int8_t>(indicator);
}

const char* BaskervilleMode::getStatusText() const {
    if (!m_status.isActive) return "Not hunting";
    
    switch (m_status.trend) {
        case HuntTrend::Approaching:
            if (m_status.quality == SignalQuality::Excellent) {
                return ">>> VERY HOT! You're close! <<<";
            } else if (m_status.quality == SignalQuality::Good) {
                return ">> HOT - Keep going! <<";
            }
            return "> Getting warmer >";
            
        case HuntTrend::Receding:
            if (m_status.trendConfidence > 0.7f) {
                return "<<< COLD - Turn back! <<<";
            }
            return "< Getting colder <";
            
        case HuntTrend::Static:
            if (m_status.quality <= SignalQuality::Good) {
                return "=== Very close! Look around ===";
            }
            return "--- Stable signal ---";
            
        case HuntTrend::Lost:
            return "??? Signal lost ???";
            
        default:
            return "Acquiring signal...";
    }
}

} // namespace dupin
