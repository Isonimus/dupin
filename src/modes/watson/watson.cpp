/**
 * @file watson.cpp
 * @brief Watson Mode implementation
 */

#include "modes/watson/watson.h"

namespace dupin {

WatsonMode::WatsonMode()
    : m_statsTimer(m_config.statsLogIntervalMs)
{
}

void WatsonMode::begin() {
    setActive(true);

    // Nothing is loaded here. The baseline belongs to the open case and was restored when
    // the case was opened; Watson used to read its own copy out of NVS at this point, which
    // is what gave every case the same baseline regardless of which one was open (ADR-0016).
    // The count is logged because it is what the detection modes will score against.
    Serial.printf("[BASELINE] Watson active over %zu SSIDs\n", m_baseline.count());

    // Reset session stats
    resetSessionStats();
    m_sessionStats.sessionStartTime = getTimestamp();

    m_statsTimer.setInterval(m_config.statsLogIntervalMs);
    m_statsTimer.start();

    logger.logStatus("Watson mode active - observing...");
}

void WatsonMode::end() {
    // No save here either: the baseline is written to the open case by loop()'s autosave
    // and again when capture stops, which is the one persistence path (ADR-0016).
    setActive(false);
    logger.logStatus("Watson mode stopped");
}

void WatsonMode::onObservation(const Observation& obs) {
    if (!isActive()) return;
    
    m_observationCount++;
    m_sessionStats.observations++;
    
    // Update frame type counters
    switch (obs.frameType) {
        case FrameType::Beacon:
            m_sessionStats.beacons++;
            break;
        case FrameType::ProbeRequest:
            m_sessionStats.probeRequests++;
            break;
        case FrameType::Deauthentication:
        case FrameType::Disassociation:
            m_sessionStats.deauths++;
            break;
        default:
            break;
    }
    
    // Store observation
    m_store.push(obs);
    
    // Log observation
    logger.logObservation(obs);
    
    // Update baseline
    updateBaseline(obs);
}

void WatsonMode::update() {
    if (!isActive()) return;
    
    // Periodic stats logging
    if (m_statsTimer.check()) {
        logger.logStats(
            m_sessionStats.beacons,
            m_sessionStats.probeRequests,
            m_sessionStats.deauths,
            m_sessionStats.observations
        );
    }
}

void WatsonMode::updateBaseline(const Observation& obs) {
    // Only baseline frames with SSIDs
    if (!obs.hasSSID()) {
        return;
    }
    
    // Check if this is a new SSID
    bool wasNewSSID = isNewSSID(obs.ssid);
    bool wasNewBSSID = false;
    
    if (!wasNewSSID && obs.hasBSSID()) {
        wasNewBSSID = isNewBSSIDForSSID(obs.ssid, obs.bssid);
    }
    
    // Update baseline with current session for tracking
    m_baseline.update(obs, m_baseline.getCurrentSession());
    
    // Log discoveries
    if (wasNewSSID) {
        m_sessionStats.newSSIDs++;
        if (m_config.logNewSSIDs) {
            logger.logNewSSID(obs.ssid, obs.bssid);
        }
        // Notify callback for UI status ticker
        if (m_ssidCallback != nullptr) {
            m_ssidCallback(obs.ssid, obs.bssid);
        }
    } else if (wasNewBSSID) {
        m_sessionStats.newBSSIDs++;
        if (m_config.logNewBSSIDs) {
            logger.logNewBSSID(obs.ssid, obs.bssid);
        }
    }
}

bool WatsonMode::isNewSSID(const char* ssid) const {
    return m_baseline.find(ssid) == nullptr;
}

bool WatsonMode::isNewBSSIDForSSID(const char* ssid, const uint8_t* bssid) const {
    const SSIDBaseline* entry = m_baseline.find(ssid);
    if (entry == nullptr) {
        return false;  // SSID not known, not a "new BSSID for known SSID"
    }
    return !entry->hasBSSID(bssid);
}

void WatsonMode::resetSessionStats() {
    m_sessionStats = SessionStats{};
}

} // namespace dupin
