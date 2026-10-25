/**
 * @file marple.cpp
 * @brief Marple Mode implementation
 */

#include "modes/marple/marple.h"
#include "ui/serial_logger.h"
#include "storage/session_manager.h"

namespace dupin {

MarpleMode::MarpleMode() {
    clearAnomalies();
}

void MarpleMode::begin() {
    setActive(true);
    clearAnomalies();
    m_floodTracker.reset(getTimestamp());
    m_lastDecayTime = getTimestamp();
    m_startTime = getTimestamp();  // Track startup for grace period
    resetStats();
    
    // Clear beacon cache for fresh start
    for (size_t i = 0; i < MAX_BEACON_ENTRIES; i++) {
        m_beaconCache[i].active = false;
    }
    
    if (m_baseline == nullptr) {
        logger.logWarning("Marple: No baseline set - detection limited");
    } else {
        logger.logStatus("Marple mode active - watching for anomalies...");
    }
}

void MarpleMode::end() {
    setActive(false);
    logger.logStatus("Marple mode stopped");
}

void MarpleMode::onObservation(const Observation& obs) {
    if (!isActive()) return;
    
    // Update flood tracking
    uint32_t now = obs.timestamp ? obs.timestamp : getTimestamp();
    
    if (now - m_floodTracker.windowStart > m_config.windowMs) {
        m_floodTracker.reset(now);
    }
    
    // Track frame types for flood detection
    if (obs.frameType == FrameType::Deauthentication ||
        obs.frameType == FrameType::Disassociation) {
        m_floodTracker.deauthCount++;
    } else if (obs.frameType == FrameType::ProbeRequest) {
        m_floodTracker.probeCount++;
    }
    
    // Run detection checks (only if we have a baseline)
    if (m_baseline != nullptr && obs.hasSSID()) {
        if (m_config.detectNewBSSID) {
            checkNewBSSID(obs);
        }
        
        if (m_config.detectUnexpectedChannel) {
            checkUnexpectedChannel(obs);
        }
        
        if (m_config.detectRSSIAnomaly) {
            checkRSSIAnomaly(obs);
        }
        
        if (m_config.detectSecurityDowngrade) {
            checkSecurityDowngrade(obs);
        }
    }
    
    // Flood detection (works without baseline)
    if (m_config.detectExcessiveDeauths) {
        checkExcessiveDeauths(obs);
    }
    
    // ========== ATTACK PATTERN DETECTION ==========
    // Targeted deauth (per MAC pair)
    if (obs.frameType == FrameType::Deauthentication ||
        obs.frameType == FrameType::Disassociation) {
        checkTargetedDeauth(obs);
    }
    
    // Karma AP (same BSSID, multiple SSIDs)
    if (obs.frameType == FrameType::ProbeResponse) {
        checkKarmaAP(obs);
        // Improved detection: skips baseline SSIDs, requires 3+ occurrences
        checkAPlessClient(obs);
    }
    
    // Beacon flood (many new SSIDs rapidly)
    if (obs.frameType == FrameType::Beacon) {
        checkBeaconFlood(obs);
        checkCloneSpam(obs);
    }
    
    // Probe flood (single device probing many SSIDs)
    if (obs.frameType == FrameType::ProbeRequest) {
        checkTargetedProbeFlood(obs);
    }
    
    // Correlate evil twin + deauth combo periodically
    checkEvilTwinCombo();
}

void MarpleMode::update() {
    if (!isActive()) return;
    
    // Decay confidence periodically
    uint32_t now = getTimestamp();
    if (now - m_lastDecayTime >= 1000) {  // Every second
        decayConfidence();
        m_lastDecayTime = now;
    }
    
    // Expire old anomalies
    expireOldAnomalies();
}

// ============================================
// Detection Methods
// ============================================

bool MarpleMode::checkNewBSSID(const Observation& obs) {
    if (!obs.hasBSSID()) return false;
    
    const SSIDBaseline* entry = m_baseline->find(obs.ssid);
    if (entry == nullptr) {
        // SSID not in baseline - can't detect new BSSID
        return false;
    }
    
    // Check if this BSSID is known for this SSID
    if (!entry->hasBSSID(obs.bssid)) {
        // New BSSID for known SSID - potential evil twin!
        Anomaly* anomaly = findOrCreateAnomaly(
            AnomalyType::NewBSSIDForSSID,
            obs.ssid,
            obs.bssid
        );
        
        if (anomaly != nullptr) {
            anomaly->channel = obs.channel;
            anomaly->severity = Severity::High;
            anomaly->recordOccurrence(obs.timestamp);
            
            if (anomaly->occurrences == 1) {
                m_stats.newBSSIDs++;
                m_stats.totalAnomalies++;
                raiseAnomaly(*anomaly);
            }
        }
        return true;
    }
    
    return false;
}

bool MarpleMode::checkUnexpectedChannel(const Observation& obs) {
    const SSIDBaseline* entry = m_baseline->find(obs.ssid);
    if (entry == nullptr || entry->channelCount == 0) {
        return false;
    }
    
    // Check if channel is expected
    if (!entry->hasChannel(obs.channel)) {
        Anomaly* anomaly = findOrCreateAnomaly(
            AnomalyType::UnexpectedChannel,
            obs.ssid,
            obs.bssid
        );
        
        if (anomaly != nullptr) {
            anomaly->channel = obs.channel;
            anomaly->severity = Severity::Medium;
            anomaly->recordOccurrence(obs.timestamp);
            
            if (anomaly->occurrences == 1) {
                m_stats.unexpectedChannels++;
                m_stats.totalAnomalies++;
                raiseAnomaly(*anomaly);
            }
        }
        return true;
    }
    
    return false;
}

bool MarpleMode::checkRSSIAnomaly(const Observation& obs) {
    const SSIDBaseline* entry = m_baseline->find(obs.ssid);
    if (entry == nullptr || entry->rssiMax == entry->rssiMin) {
        return false;  // No baseline or no range
    }
    
    // Check if RSSI is significantly outside expected range
    if (!entry->isRSSIInRange(obs.rssi)) {
        int8_t deviation = 0;
        if (obs.rssi > entry->rssiMax) {
            deviation = obs.rssi - entry->rssiMax;
        } else if (obs.rssi < entry->rssiMin) {
            deviation = entry->rssiMin - obs.rssi;
        }
        
        // Only alert if deviation exceeds threshold
        if (deviation > m_config.rssiDeviationThreshold) {
            Anomaly* anomaly = findOrCreateAnomaly(
                AnomalyType::RSSIAnomaly,
                obs.ssid,
                obs.bssid
            );
            
            if (anomaly != nullptr) {
                anomaly->details.rssi.expectedRSSI = entry->rssiAverage();
                anomaly->details.rssi.actualRSSI = obs.rssi;
                anomaly->severity = Severity::Medium;
                anomaly->recordOccurrence(obs.timestamp);
                
                if (anomaly->occurrences == 1) {
                    m_stats.rssiAnomalies++;
                    m_stats.totalAnomalies++;
                    raiseAnomaly(*anomaly);
                }
            }
            return true;
        }
    }
    
    return false;
}

bool MarpleMode::checkSecurityDowngrade(const Observation& obs) {
    // Only check beacons and probe responses
    if (obs.frameType != FrameType::Beacon && 
        obs.frameType != FrameType::ProbeResponse) {
        return false;
    }
    
    const SSIDBaseline* entry = m_baseline->find(obs.ssid);
    if (entry == nullptr) {
        return false;
    }
    
    // Check for downgrade
    if (obs.security.isDowngradeFrom(entry->security)) {
        Anomaly* anomaly = findOrCreateAnomaly(
            AnomalyType::SecurityDowngrade,
            obs.ssid,
            obs.bssid
        );
        
        if (anomaly != nullptr) {
            anomaly->details.security.expectedSecurity = 
                static_cast<uint8_t>(entry->security.type);
            anomaly->details.security.actualSecurity = 
                static_cast<uint8_t>(obs.security.type);
            anomaly->severity = Severity::Critical;
            anomaly->recordOccurrence(obs.timestamp);
            
            if (anomaly->occurrences == 1) {
                m_stats.securityDowngrades++;
                m_stats.totalAnomalies++;
                raiseAnomaly(*anomaly);
            }
        }
        return true;
    }
    
    return false;
}

bool MarpleMode::checkExcessiveDeauths(const Observation& obs) {
    // Check if deauth count exceeds threshold
    if (m_floodTracker.deauthCount >= m_config.deauthThreshold) {
        // Find or create flood anomaly (no specific SSID/BSSID)
        Anomaly* anomaly = nullptr;
        
        // Find existing excessive deauth anomaly
        for (size_t i = 0; i < MAX_ANOMALIES; i++) {
            if (m_anomalies[i].type == AnomalyType::ExcessiveDeauths &&
                m_anomalies[i].isActive()) {
                anomaly = &m_anomalies[i];
                break;
            }
        }
        
        if (anomaly == nullptr) {
            // Create new
            for (size_t i = 0; i < MAX_ANOMALIES; i++) {
                if (!m_anomalies[i].isActive()) {
                    anomaly = &m_anomalies[i];
                    anomaly->clear();
                    anomaly->type = AnomalyType::ExcessiveDeauths;
                    anomaly->id = m_nextAnomalyId++;
                    anomaly->firstSeen = getTimestamp();
                    anomaly->confidence = 0.5f;
                    break;
                }
            }
        }
        
        if (anomaly != nullptr) {
            anomaly->details.flood.count = m_floodTracker.deauthCount;
            anomaly->details.flood.threshold = m_config.deauthThreshold;
            anomaly->severity = Severity::High;
            anomaly->recordOccurrence(getTimestamp());
            
            if (anomaly->occurrences == 1) {
                m_stats.excessiveDeauths++;
                m_stats.totalAnomalies++;
                raiseAnomaly(*anomaly);
            }
        }
        
        // Reset counter to avoid repeated alerts
        m_floodTracker.deauthCount = 0;
        return true;
    }
    
    return false;
}

// ============================================
// Anomaly Management
// ============================================

Anomaly* MarpleMode::findOrCreateAnomaly(AnomalyType type, const char* ssid,
                                          const uint8_t* bssid) {
    // First, try to find existing anomaly with same type and identifiers
    for (size_t i = 0; i < MAX_ANOMALIES; i++) {
        if (m_anomalies[i].type == type && m_anomalies[i].isActive()) {
            // Check SSID match
            bool ssidMatch = (ssid == nullptr && !m_anomalies[i].hasSSID()) ||
                             (ssid != nullptr && strcmp(m_anomalies[i].ssid, ssid) == 0);
            
            // Check BSSID match
            bool bssidMatch = (bssid == nullptr && !m_anomalies[i].hasBSSID()) ||
                              (bssid != nullptr && macEquals(m_anomalies[i].bssid, bssid));
            
            if (ssidMatch && bssidMatch) {
                return &m_anomalies[i];
            }
        }
    }
    
    // Not found - create new
    for (size_t i = 0; i < MAX_ANOMALIES; i++) {
        if (!m_anomalies[i].isActive()) {
            m_anomalies[i].clear();
            m_anomalies[i].type = type;
            m_anomalies[i].id = m_nextAnomalyId++;
            m_anomalies[i].firstSeen = getTimestamp();
            m_anomalies[i].confidence = 0.5f;  // Initial confidence
            m_anomalies[i].severity = defaultSeverity(type);
            
            if (ssid != nullptr) {
                m_anomalies[i].setSSID(ssid);
            }
            if (bssid != nullptr) {
                macCopy(m_anomalies[i].bssid, bssid);
            }
            
            return &m_anomalies[i];
        }
    }
    
    // No space available
    return nullptr;
}

void MarpleMode::raiseAnomaly(const Anomaly& anomaly) {
    // Log the anomaly
    char macStr[18] = "N/A";
    if (anomaly.hasBSSID()) {
        macFormat(anomaly.bssid, macStr);
    }
    
    logger.logWarning("Marple detected anomaly!");
    Serial.printf("  Type: %s [%s]\n", 
                  anomalyTypeName(anomaly.type),
                  severityName(anomaly.severity));
    
    if (anomaly.hasSSID()) {
        Serial.printf("  SSID: \"%s\"\n", anomaly.ssid);
    }
    Serial.printf("  BSSID: %s\n", macStr);
    Serial.printf("  Confidence: %.1f%%\n", anomaly.confidence * 100.0f);
    
    // Log to session file
    g_sessionManager.logAnomaly(anomaly);
    
    // Call user callback
    if (m_callback != nullptr) {
        m_callback(anomaly);
    }
}

void MarpleMode::decayConfidence() {
    for (size_t i = 0; i < MAX_ANOMALIES; i++) {
        if (m_anomalies[i].isActive()) {
            m_anomalies[i].confidence -= m_config.confidenceDecayRate;
            if (m_anomalies[i].confidence < 0.0f) {
                m_anomalies[i].confidence = 0.0f;
            }
        }
    }
}

void MarpleMode::expireOldAnomalies() {
    for (size_t i = 0; i < MAX_ANOMALIES; i++) {
        if (m_anomalies[i].type != AnomalyType::Unknown &&
            m_anomalies[i].confidence < m_config.minConfidence) {
            m_anomalies[i].clear();
        }
    }
}

size_t MarpleMode::getAnomalyCount() const {
    size_t count = 0;
    for (size_t i = 0; i < MAX_ANOMALIES; i++) {
        if (m_anomalies[i].isActive()) {
            count++;
        }
    }
    return count;
}

uint32_t MarpleMode::dismissAnomaly(size_t index) {
    // Find the Nth active anomaly
    size_t activeCount = 0;
    for (size_t i = 0; i < MAX_ANOMALIES; i++) {
        if (m_anomalies[i].isActive()) {
            if (activeCount == index) {
                uint32_t id = m_anomalies[i].id;
                m_anomalies[i].clear();
                Serial.printf("[MARPLE] Dismissed anomaly #%u\n", static_cast<unsigned>(id));
                return id;
            }
            activeCount++;
        }
    }
    return 0;  // Invalid index
}

const Anomaly* MarpleMode::findAnomaly(uint32_t id) const {
    for (size_t i = 0; i < MAX_ANOMALIES; i++) {
        if (m_anomalies[i].id == id && m_anomalies[i].isActive()) {
            return &m_anomalies[i];
        }
    }
    return nullptr;
}

void MarpleMode::clearAnomalies() {
    for (size_t i = 0; i < MAX_ANOMALIES; i++) {
        m_anomalies[i].clear();
    }
    m_nextAnomalyId = 1;
}

// ============================================
// Attack Pattern Detection
// ============================================

bool MarpleMode::checkTargetedDeauth(const Observation& obs) {
    uint32_t now = obs.timestamp ? obs.timestamp : getTimestamp();
    
    // Need source and destination MACs
    if (!obs.hasBSSID()) return false;
    
    // Extract source MAC from frame (obs.bssid is typically source for deauth)
    // For deauth: source is attacker, destination is victim
    const uint8_t* attacker = obs.bssid;
    const uint8_t* victim = obs.receiverMac;  // Receiver in deauth frame is the victim
    
    // Find existing pair or create new
    DeauthPair* pair = nullptr;
    size_t freeSlot = MAX_DEAUTH_PAIRS;
    
    for (size_t i = 0; i < MAX_DEAUTH_PAIRS; i++) {
        if (!m_deauthPairs[i].active) {
            if (freeSlot == MAX_DEAUTH_PAIRS) freeSlot = i;
            continue;
        }
        
        // Check if window expired
        if (now - m_deauthPairs[i].windowStart > TARGETED_DEAUTH_WINDOW_MS) {
            m_deauthPairs[i].active = false;
            if (freeSlot == MAX_DEAUTH_PAIRS) freeSlot = i;
            continue;
        }
        
        // Match existing pair
        if (macEquals(m_deauthPairs[i].attacker, attacker) &&
            macEquals(m_deauthPairs[i].victim, victim)) {
            pair = &m_deauthPairs[i];
            break;
        }
    }
    
    // Create new pair
    if (pair == nullptr && freeSlot < MAX_DEAUTH_PAIRS) {
        pair = &m_deauthPairs[freeSlot];
        pair->active = true;
        macCopy(pair->attacker, attacker);
        macCopy(pair->victim, victim);
        pair->count = 0;
        pair->windowStart = now;
    }
    
    if (pair == nullptr) return false;
    
    pair->count++;
    
    // Check threshold
    if (pair->count >= TARGETED_DEAUTH_THRESHOLD) {
        Anomaly* anomaly = findOrCreateAnomaly(
            AnomalyType::TargetedDeauth,
            obs.ssid[0] ? obs.ssid : nullptr,
            victim
        );
        
        if (anomaly != nullptr) {
            macCopy(anomaly->attackerMAC, attacker);
            anomaly->severity = Severity::Critical;
            anomaly->details.flood.count = pair->count;
            anomaly->details.flood.threshold = TARGETED_DEAUTH_THRESHOLD;
            anomaly->recordOccurrence(now);
            
            if (anomaly->occurrences == 1) {
                m_stats.totalAnomalies++;
                raiseAnomaly(*anomaly);
            }
        }
        
        pair->count = 0;  // Reset to avoid repeated alerts
        return true;
    }
    
    return false;
}

bool MarpleMode::checkKarmaAP(const Observation& obs) {
    if (!obs.hasBSSID() || !obs.hasSSID()) return false;
    
    uint32_t now = obs.timestamp ? obs.timestamp : getTimestamp();
    
    // Find tracker for this BSSID
    KarmaTracker* tracker = nullptr;
    size_t freeSlot = MAX_KARMA_TRACKERS;
    
    for (size_t i = 0; i < MAX_KARMA_TRACKERS; i++) {
        if (!m_karmaTrackers[i].active) {
            if (freeSlot == MAX_KARMA_TRACKERS) freeSlot = i;
            continue;
        }
        
        // Check if expired
        if (now - m_karmaTrackers[i].firstSeen > KARMA_WINDOW_MS) {
            m_karmaTrackers[i].active = false;
            m_karmaTrackers[i].ssidCount = 0;
            if (freeSlot == MAX_KARMA_TRACKERS) freeSlot = i;
            continue;
        }
        
        if (macEquals(m_karmaTrackers[i].bssid, obs.bssid)) {
            tracker = &m_karmaTrackers[i];
            break;
        }
    }
    
    // Create new tracker
    if (tracker == nullptr && freeSlot < MAX_KARMA_TRACKERS) {
        tracker = &m_karmaTrackers[freeSlot];
        tracker->active = true;
        macCopy(tracker->bssid, obs.bssid);
        tracker->ssidCount = 0;
        tracker->firstSeen = now;
    }
    
    if (tracker == nullptr) return false;
    
    // Add SSID if new
    if (!tracker->hasSSID(obs.ssid) && tracker->ssidCount < 8) {
        strncpy(tracker->ssids[tracker->ssidCount], obs.ssid, DUPIN_SSID_MAX_LEN);
        tracker->ssids[tracker->ssidCount][DUPIN_SSID_MAX_LEN] = '\0';
        tracker->ssidCount++;
    }
    
    // Check threshold - Karma AP!
    if (tracker->ssidCount >= KARMA_SSID_THRESHOLD) {
        Anomaly* anomaly = findOrCreateAnomaly(
            AnomalyType::KarmaAP,
            "KARMA",
            obs.bssid
        );
        
        if (anomaly != nullptr) {
            macCopy(anomaly->attackerMAC, obs.bssid);
            anomaly->severity = Severity::Critical;
            anomaly->details.flood.count = tracker->ssidCount;
            anomaly->details.flood.threshold = KARMA_SSID_THRESHOLD;
            anomaly->recordOccurrence(now);
            
            if (anomaly->occurrences == 1) {
                m_stats.totalAnomalies++;
                raiseAnomaly(*anomaly);
            }
        }
        
        tracker->ssidCount = 0;  // Reset
        return true;
    }
    
    return false;
}

bool MarpleMode::checkBeaconFlood(const Observation& obs) {
    if (!obs.hasSSID()) return false;
    
    uint32_t now = obs.timestamp ? obs.timestamp : getTimestamp();
    
    // Reset window if expired
    if (now - m_ssidFloodTracker.windowStart > BEACON_FLOOD_WINDOW_MS) {
        m_ssidFloodTracker.reset(now);
    }
    
    // Check if this is a new SSID (not in baseline)
    bool isNew = (m_baseline == nullptr || m_baseline->find(obs.ssid) == nullptr);
    
    if (isNew) {
        m_ssidFloodTracker.newSSIDCount++;
    }
    
    // Check threshold
    if (m_ssidFloodTracker.newSSIDCount >= BEACON_FLOOD_THRESHOLD) {
        Anomaly* anomaly = findOrCreateAnomaly(
            AnomalyType::BeaconFlood,
            "BEACON_SPAM",
            obs.bssid
        );
        
        if (anomaly != nullptr) {
            if (obs.hasBSSID()) {
                macCopy(anomaly->attackerMAC, obs.bssid);
            }
            anomaly->severity = Severity::High;
            anomaly->details.flood.count = m_ssidFloodTracker.newSSIDCount;
            anomaly->details.flood.threshold = BEACON_FLOOD_THRESHOLD;
            anomaly->recordOccurrence(now);
            
            if (anomaly->occurrences == 1) {
                m_stats.totalAnomalies++;
                raiseAnomaly(*anomaly);
            }
        }
        
        m_ssidFloodTracker.reset(now);
        return true;
    }
    
    return false;
}

bool MarpleMode::checkCloneSpam(const Observation& obs) {
    if (!obs.hasSSID() || !obs.hasBSSID()) return false;
    
    // This uses the existing NewBSSIDForSSID detection
    // We just aggregate - if we see the same SSID from many different BSSIDs
    // within a short window, it's a clone spam attack
    
    if (m_baseline == nullptr) return false;
    
    const SSIDBaseline* entry = m_baseline->find(obs.ssid);
    if (entry == nullptr) return false;
    
    // Check if we have too many BSSIDs for this SSID
    if (entry->bssidCount >= CLONE_SPAM_THRESHOLD) {
        // Count NewBSSIDForSSID anomalies for this SSID
        size_t evilTwinCount = 0;
        for (size_t i = 0; i < MAX_ANOMALIES; i++) {
            if (m_anomalies[i].type == AnomalyType::NewBSSIDForSSID &&
                strcmp(m_anomalies[i].ssid, obs.ssid) == 0) {
                evilTwinCount++;
            }
        }
        
        if (evilTwinCount >= CLONE_SPAM_THRESHOLD) {
            Anomaly* anomaly = findOrCreateAnomaly(
                AnomalyType::CloneSpam,
                obs.ssid,
                nullptr
            );
            
            if (anomaly != nullptr) {
                anomaly->severity = Severity::High;
                anomaly->details.flood.count = evilTwinCount;
                anomaly->details.flood.threshold = CLONE_SPAM_THRESHOLD;
                anomaly->recordOccurrence(getTimestamp());
                
                if (anomaly->occurrences == 1) {
                    m_stats.totalAnomalies++;
                    raiseAnomaly(*anomaly);
                }
            }
            return true;
        }
    }
    
    return false;
}

bool MarpleMode::checkTargetedProbeFlood(const Observation& obs) {
    if (!obs.hasSSID()) return false;
    
    uint32_t now = obs.timestamp ? obs.timestamp : getTimestamp();
    const uint8_t* mac = obs.transmitterMac;
    
    // Find tracker for this MAC
    ProbeTracker* tracker = nullptr;
    size_t freeSlot = MAX_PROBE_TRACKERS;
    
    for (size_t i = 0; i < MAX_PROBE_TRACKERS; i++) {
        if (!m_probeTrackers[i].active) {
            if (freeSlot == MAX_PROBE_TRACKERS) freeSlot = i;
            continue;
        }
        
        // Check if expired (30 second window)
        if (now - m_probeTrackers[i].firstSeen > 30000) {
            m_probeTrackers[i].active = false;
            m_probeTrackers[i].ssidCount = 0;
            if (freeSlot == MAX_PROBE_TRACKERS) freeSlot = i;
            continue;
        }
        
        if (macEquals(m_probeTrackers[i].mac, mac)) {
            tracker = &m_probeTrackers[i];
            break;
        }
    }
    
    // Create new tracker
    if (tracker == nullptr && freeSlot < MAX_PROBE_TRACKERS) {
        tracker = &m_probeTrackers[freeSlot];
        tracker->active = true;
        macCopy(tracker->mac, mac);
        tracker->ssidCount = 0;
        tracker->firstSeen = now;
    }
    
    if (tracker == nullptr) return false;
    
    // Add SSID if new and we have room
    if (!tracker->hasSSID(obs.ssid) && tracker->ssidCount < 16) {
        strncpy(tracker->ssids[tracker->ssidCount], obs.ssid, DUPIN_SSID_MAX_LEN);
        tracker->ssids[tracker->ssidCount][DUPIN_SSID_MAX_LEN] = '\0';
        tracker->ssidCount++;
    }
    
    // Check threshold - reconnaissance/probe flood
    if (tracker->ssidCount >= 15) {
        Anomaly* anomaly = findOrCreateAnomaly(
            AnomalyType::TargetedProbeFlood,
            nullptr,
            nullptr
        );
        
        if (anomaly != nullptr) {
            macCopy(anomaly->attackerMAC, mac);
            anomaly->severity = Severity::Medium;
            anomaly->details.flood.count = tracker->ssidCount;
            anomaly->details.flood.threshold = 15;
            anomaly->recordOccurrence(now);
            
            if (anomaly->occurrences == 1) {
                m_stats.totalAnomalies++;
                raiseAnomaly(*anomaly);
            }
        }
        
        tracker->ssidCount = 0;  // Reset
        return true;
    }
    
    return false;
}

bool MarpleMode::checkAPlessClient(const Observation& obs) {
    // A probe response with no matching beacon indicates rogue AP
    
    // Skip during startup grace period (10 seconds) to let beacon cache populate
    uint32_t now = obs.timestamp ? obs.timestamp : getTimestamp();
    if (now - m_startTime < 10000) {
        // Still update beacon cache even during grace period
        if (obs.frameType == FrameType::Beacon && obs.hasSSID() && obs.hasBSSID()) {
            goto updateBeaconCache;  // Jump to cache update code
        }
        return false;
    }
    
    // First, update beacon cache if this is a beacon
updateBeaconCache:
    if (obs.frameType == FrameType::Beacon && obs.hasSSID() && obs.hasBSSID()) {
        // Find or create entry
        for (size_t i = 0; i < MAX_BEACON_ENTRIES; i++) {
            if (m_beaconCache[i].active && 
                strcmp(m_beaconCache[i].ssid, obs.ssid) == 0 &&
                macEquals(m_beaconCache[i].bssid, obs.bssid)) {
                m_beaconCache[i].lastSeen = now;
                return false;
            }
        }
        
        // Find empty slot - extended timeout to 5 minutes for channel hopping
        for (size_t i = 0; i < MAX_BEACON_ENTRIES; i++) {
            if (!m_beaconCache[i].active || 
                (now - m_beaconCache[i].lastSeen > 300000)) {  // Expire after 5 min
                strncpy(m_beaconCache[i].ssid, obs.ssid, DUPIN_SSID_MAX_LEN);
                m_beaconCache[i].ssid[DUPIN_SSID_MAX_LEN] = '\0';
                macCopy(m_beaconCache[i].bssid, obs.bssid);
                m_beaconCache[i].lastSeen = now;
                m_beaconCache[i].active = true;
                break;
            }
        }
        return false;
    }
    
    // For probe responses, check if we've seen a matching beacon
    if (obs.frameType == FrameType::ProbeResponse && obs.hasSSID() && obs.hasBSSID()) {
        // Skip if SSID is in our baseline (known network)
        if (m_baseline != nullptr && m_baseline->find(obs.ssid) != nullptr) {
            return false;  // Known SSID from baseline, not suspicious
        }
        
        // Look for matching beacon (extended 5 min window)
        bool foundBeacon = false;
        for (size_t i = 0; i < MAX_BEACON_ENTRIES; i++) {
            if (m_beaconCache[i].active &&
                strcmp(m_beaconCache[i].ssid, obs.ssid) == 0 &&
                macEquals(m_beaconCache[i].bssid, obs.bssid) &&
                (now - m_beaconCache[i].lastSeen < 300000)) {  // 5 min window
                foundBeacon = true;
                break;
            }
        }
        
        if (!foundBeacon) {
            // Probe response without beacon - possible rogue AP
            Anomaly* anomaly = findOrCreateAnomaly(
                AnomalyType::APlessClient,
                obs.ssid,
                obs.bssid
            );
            
            if (anomaly != nullptr) {
                macCopy(anomaly->attackerMAC, obs.bssid);
                anomaly->severity = Severity::High;
                anomaly->recordOccurrence(now);
                
                // Only alert after 3+ occurrences to reduce false positives
                if (anomaly->occurrences == 3) {
                    m_stats.totalAnomalies++;
                    raiseAnomaly(*anomaly);
                }
            }
            return true;
        }
    }
    
    return false;
}

bool MarpleMode::checkEvilTwinCombo() {
    // Look for correlation: NewBSSIDForSSID + ExcessiveDeauths/TargetedDeauth
    // for the same SSID within recent anomalies
    
    for (size_t i = 0; i < MAX_ANOMALIES; i++) {
        if (m_anomalies[i].type != AnomalyType::NewBSSIDForSSID ||
            !m_anomalies[i].isActive()) {
            continue;
        }
        
        const char* targetSSID = m_anomalies[i].ssid;
        if (targetSSID[0] == '\0') continue;
        
        // Check if there's a deauth anomaly for the same SSID
        for (size_t j = 0; j < MAX_ANOMALIES; j++) {
            if (i == j) continue;
            
            if ((m_anomalies[j].type == AnomalyType::ExcessiveDeauths ||
                 m_anomalies[j].type == AnomalyType::TargetedDeauth) &&
                m_anomalies[j].isActive()) {
                
                // Same SSID or close in time
                if ((m_anomalies[j].hasSSID() && strcmp(m_anomalies[j].ssid, targetSSID) == 0) ||
                    (m_anomalies[i].lastSeen > 0 && m_anomalies[j].lastSeen > 0 &&
                     (m_anomalies[i].lastSeen > m_anomalies[j].lastSeen ? 
                      m_anomalies[i].lastSeen - m_anomalies[j].lastSeen : 
                      m_anomalies[j].lastSeen - m_anomalies[i].lastSeen) < 60000)) {
                    
                    // Found combo - check if we already raised this
                    Anomaly* combo = findOrCreateAnomaly(
                        AnomalyType::EvilTwinCombo,
                        targetSSID,
                        m_anomalies[i].bssid
                    );
                    
                    if (combo != nullptr && combo->occurrences == 0) {
                        combo->severity = Severity::Critical;
                        macCopy(combo->attackerMAC, m_anomalies[i].bssid);
                        combo->recordOccurrence(getTimestamp());
                        m_stats.totalAnomalies++;
                        raiseAnomaly(*combo);
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

} // namespace dupin
