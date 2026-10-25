/**
 * @file poirot.cpp
 * @brief Poirot Mode implementation
 */

#include "modes/poirot/poirot.h"
#include "ui/serial_logger.h"
#include "storage/session_manager.h"
#include <cstring>

namespace dupin {

PoirotMode::PoirotMode() {
    clear();
}

void PoirotMode::begin() {
    setActive(true);
    clear();
    logger.logStatus("Poirot mode active - investigating...");
}

void PoirotMode::end() {
    setActive(false);
    logger.logStatus("Poirot mode stopped");
}

void PoirotMode::clear() {
    for (size_t i = 0; i < MAX_INVESTIGATIONS; i++) {
        m_investigations[i].clear();
    }
    for (size_t i = 0; i < MAX_FINDINGS; i++) {
        m_findings[i].clear();
    }
    m_nextFindingId = 1;
}

bool PoirotMode::investigate(const Anomaly& anomaly) {
    if (!isActive()) return false;
    
    // Check if already investigating this anomaly
    for (size_t i = 0; i < MAX_INVESTIGATIONS; i++) {
        if (m_investigations[i].anomalyId == anomaly.id) {
            return true;  // Already investigating
        }
    }
    
    // Find empty slot
    for (size_t i = 0; i < MAX_INVESTIGATIONS; i++) {
        if (!m_investigations[i].isActive()) {
            Investigation& inv = m_investigations[i];
            inv.clear();
            inv.anomalyId = anomaly.id;
            inv.anomalyType = anomaly.type;
            inv.setSSID(anomaly.ssid);
            if (anomaly.hasBSSID()) {
                macCopy(inv.bssid, anomaly.bssid);
            }
            inv.startTime = getTimestamp();
            inv.lastActivity = inv.startTime;
            inv.timeoutMs = m_config.investigationTimeoutMs;
            
            logger.logInfo("Poirot: Starting investigation");
            Serial.printf("  Anomaly #%u: %s\n", static_cast<unsigned>(anomaly.id),
                          anomalyTypeName(anomaly.type));
            if (anomaly.hasSSID()) {
                Serial.printf("  Target: \"%s\"\n", anomaly.ssid);
            }
            
            return true;
        }
    }
    
    return false;  // No slots available
}

void PoirotMode::onObservation(const Observation& obs) {
    if (!isActive()) return;
    
    // Process observation against all active investigations
    for (size_t i = 0; i < MAX_INVESTIGATIONS; i++) {
        if (m_investigations[i].isActive()) {
            // Check if this observation is relevant to the investigation
            bool relevant = false;
            
            // Match by SSID
            if (obs.hasSSID() && m_investigations[i].ssid[0] != '\0' &&
                strcmp(obs.ssid, m_investigations[i].ssid) == 0) {
                relevant = true;
            }
            
            // Match by BSSID
            if (obs.hasBSSID() && !macIsNull(m_investigations[i].bssid) &&
                macEquals(obs.bssid, m_investigations[i].bssid)) {
                relevant = true;
            }
            
            if (relevant) {
                processForInvestigation(obs, m_investigations[i]);
            }
        }
    }
}

void PoirotMode::update() {
    if (!isActive()) return;
    
    expireInvestigations();
}

void PoirotMode::processForInvestigation(const Observation& obs, Investigation& inv) {
    inv.lastActivity = getTimestamp();
    
    // Track RSSI for movement detection
    inv.recordRSSI(obs.rssi);
    
    // Track channels
    if (obs.channel > 0) {
        inv.recordChannel(obs.channel);
    }
    
    // Run heuristics based on anomaly type
    switch (inv.anomalyType) {
        case AnomalyType::NewBSSIDForSSID:
            // For new BSSID, check movement and probe behavior
            checkMovement(obs, inv);
            checkProbePromiscuity(obs, inv);
            checkBeaconPersonality(obs, inv);
            break;
            
        case AnomalyType::UnexpectedChannel:
            checkChannelHopping(obs, inv);
            break;
            
        case AnomalyType::SecurityDowngrade:
            // For security downgrade, check security mismatch and beacon personality
            checkSecurityMismatch(obs, inv);
            checkBeaconPersonality(obs, inv);
            break;
            
        default:
            // General checks
            checkMovement(obs, inv);
            checkChannelHopping(obs, inv);
            break;
    }
}

void PoirotMode::checkProbePromiscuity(const Observation& obs, Investigation& inv) {
    // Track probe responses
    if (obs.frameType == FrameType::ProbeResponse) {
        inv.probeResponses++;
        
        // If we saw responses without sending probes, that's suspicious
        // (In a real implementation, we'd track if WE sent probes)
        // For now, track unexpected probe responses in general
        
        if (inv.probeResponses >= m_config.probePromiscuityThreshold) {
            // Check if we already have this finding
            for (size_t i = 0; i < MAX_FINDINGS; i++) {
                if (m_findings[i].type == FindingType::ProbePromiscuity &&
                    m_findings[i].anomalyId == inv.anomalyId) {
                    return;  // Already reported
                }
            }
            
            Finding* finding = createFinding(FindingType::ProbePromiscuity, inv.anomalyId);
            if (finding != nullptr) {
                finding->setSSID(inv.ssid);
                macCopy(finding->bssid, inv.bssid);
                finding->strength = FindingStrength::Moderate;
                finding->setExplanation("AP responding to many probes - may be karma attack");
                finding->metrics.probePromiscuity.probeResponseCount = inv.probeResponses;
                
                raiseFinding(*finding);
            }
        }
    }
}

void PoirotMode::checkMovement(const Observation& obs, Investigation& inv) {
    // Need at least 4 RSSI samples
    if (inv.rssiCount < 4) return;
    
    int8_t gradient = inv.rssiGradient();
    
    // Check if we already have a movement finding
    for (size_t i = 0; i < MAX_FINDINGS; i++) {
        if (m_findings[i].type == FindingType::BSSIDMovement &&
            m_findings[i].anomalyId == inv.anomalyId) {
            return;  // Already reported
        }
    }
    
    // Significant RSSI change indicates movement
    if (gradient > m_config.movementRSSIThreshold) {
        // Getting stronger - approaching
        Finding* finding = createFinding(FindingType::BSSIDMovement, inv.anomalyId);
        if (finding != nullptr) {
            finding->setSSID(inv.ssid);
            macCopy(finding->bssid, inv.bssid);
            finding->strength = FindingStrength::Moderate;
            finding->setExplanation("Signal strengthening - device may be approaching");
            finding->metrics.movement.rssiFirst = inv.rssiHistory[0];
            finding->metrics.movement.rssiLast = inv.rssiHistory[inv.rssiCount-1];
            finding->metrics.movement.gradient = gradient;
            
            raiseFinding(*finding);
        }
    } else if (gradient < -m_config.movementRSSIThreshold) {
        // Getting weaker - receding
        Finding* finding = createFinding(FindingType::BSSIDMovement, inv.anomalyId);
        if (finding != nullptr) {
            finding->setSSID(inv.ssid);
            macCopy(finding->bssid, inv.bssid);
            finding->strength = FindingStrength::Weak;
            finding->setExplanation("Signal weakening - device may be moving away");
            finding->metrics.movement.rssiFirst = inv.rssiHistory[0];
            finding->metrics.movement.rssiLast = inv.rssiHistory[inv.rssiCount-1];
            finding->metrics.movement.gradient = gradient;
            
            raiseFinding(*finding);
        }
    }
}

void PoirotMode::checkChannelHopping(const Observation& obs, Investigation& inv) {
    // Check for channel hopping behavior
    if (inv.channelCount >= m_config.channelHopThreshold) {
        // Check if we already have this finding
        for (size_t i = 0; i < MAX_FINDINGS; i++) {
            if (m_findings[i].type == FindingType::ChannelHopping &&
                m_findings[i].anomalyId == inv.anomalyId) {
                return;  // Already reported
            }
        }
        
        Finding* finding = createFinding(FindingType::ChannelHopping, inv.anomalyId);
        if (finding != nullptr) {
            finding->setSSID(inv.ssid);
            macCopy(finding->bssid, inv.bssid);
            finding->strength = FindingStrength::Strong;
            finding->setExplanation("AP observed on multiple channels - may be scanning/hopping");
            finding->metrics.channelHop.channelChanges = inv.channelCount;
            finding->metrics.channelHop.windowMs = getTimestamp() - inv.startTime;
            
            raiseFinding(*finding);
        }
    }
}

void PoirotMode::checkBeaconPersonality(const Observation& obs, Investigation& inv) {
    // Check beacon interval consistency
    if (obs.frameType != FrameType::Beacon) return;
    
    // Standard beacon interval is 100 TUs (102.4ms)
    // Non-standard intervals may indicate rogue AP
    if (obs.beaconInterval != 0 && obs.beaconInterval != 100) {
        // Check if we already have this finding
        for (size_t i = 0; i < MAX_FINDINGS; i++) {
            if (m_findings[i].type == FindingType::TimingAnomaly &&
                m_findings[i].anomalyId == inv.anomalyId) {
                return;  // Already reported
            }
        }
        
        Finding* finding = createFinding(FindingType::TimingAnomaly, inv.anomalyId);
        if (finding != nullptr) {
            finding->setSSID(inv.ssid);
            macCopy(finding->bssid, inv.bssid);
            finding->strength = (obs.beaconInterval < 50 || obs.beaconInterval > 200) 
                               ? FindingStrength::Strong : FindingStrength::Weak;
            finding->setExplanation("Non-standard beacon interval");
            finding->metrics.timing.expectedInterval = 100;
            finding->metrics.timing.actualInterval = obs.beaconInterval;
            
            raiseFinding(*finding);
        }
    }
}

Finding* PoirotMode::createFinding(FindingType type, uint32_t anomalyId) {
    for (size_t i = 0; i < MAX_FINDINGS; i++) {
        if (!m_findings[i].isActive()) {
            m_findings[i].clear();
            m_findings[i].id = m_nextFindingId++;
            m_findings[i].type = type;
            m_findings[i].anomalyId = anomalyId;
            m_findings[i].timestamp = getTimestamp();
            return &m_findings[i];
        }
    }
    return nullptr;  // No slots
}

void PoirotMode::raiseFinding(const Finding& finding) {
    logger.logInfo("Poirot: New finding!");
    Serial.printf("  Type: %s [%s]\n", 
                  findingTypeName(finding.type),
                  findingStrengthName(finding.strength));
    Serial.printf("  Anomaly #%u\n", static_cast<unsigned>(finding.anomalyId));
    if (finding.ssid[0] != '\0') {
        Serial.printf("  SSID: \"%s\"\n", finding.ssid);
    }
    Serial.printf("  %s\n", finding.explanation);
    
    // Log to session file
    g_sessionManager.logFinding(finding);
    
    if (m_callback != nullptr) {
        m_callback(finding);
    }
}

void PoirotMode::expireInvestigations() {
    uint32_t now = getTimestamp();
    
    for (size_t i = 0; i < MAX_INVESTIGATIONS; i++) {
        if (m_investigations[i].isActive()) {
            uint32_t elapsed = now - m_investigations[i].lastActivity;
            if (elapsed > m_investigations[i].timeoutMs) {
                logger.logInfo("Poirot: Investigation expired");
                Serial.printf("  Anomaly #%u timed out after %u ms\n",
                              static_cast<unsigned>(m_investigations[i].anomalyId),
                              static_cast<unsigned>(elapsed));
                m_investigations[i].clear();
            }
        }
    }
}

size_t PoirotMode::getFindingCount() const {
    size_t count = 0;
    for (size_t i = 0; i < MAX_FINDINGS; i++) {
        if (m_findings[i].isActive()) count++;
    }
    return count;
}

size_t PoirotMode::removeFindingsForAnomaly(uint32_t anomalyId) {
    size_t removed = 0;
    for (size_t i = 0; i < MAX_FINDINGS; i++) {
        if (m_findings[i].isActive() && m_findings[i].anomalyId == anomalyId) {
            m_findings[i].clear();
            removed++;
        }
    }
    // Also remove related investigations
    for (size_t i = 0; i < MAX_INVESTIGATIONS; i++) {
        if (m_investigations[i].isActive() && m_investigations[i].anomalyId == anomalyId) {
            m_investigations[i].clear();
        }
    }
    if (removed > 0) {
        Serial.printf("[POIROT] Removed %zu findings for anomaly #%u\n", removed,
                      static_cast<unsigned>(anomalyId));
    }
    return removed;
}

bool PoirotMode::dismissFinding(size_t index) {
    // Find the Nth active finding
    size_t activeCount = 0;
    for (size_t i = 0; i < MAX_FINDINGS; i++) {
        if (m_findings[i].isActive()) {
            if (activeCount == index) {
                Serial.printf("[POIROT] Dismissed finding #%u\n", static_cast<unsigned>(m_findings[i].id));
                m_findings[i].clear();
                return true;
            }
            activeCount++;
        }
    }
    return false;
}

size_t PoirotMode::getInvestigationCount() const {
    size_t count = 0;
    for (size_t i = 0; i < MAX_INVESTIGATIONS; i++) {
        if (m_investigations[i].isActive()) count++;
    }
    return count;
}

size_t PoirotMode::getFindingsForAnomaly(uint32_t anomalyId, Finding* out, size_t maxResults) const {
    size_t found = 0;
    for (size_t i = 0; i < MAX_FINDINGS && found < maxResults; i++) {
        if (m_findings[i].isActive() && m_findings[i].anomalyId == anomalyId) {
            if (out != nullptr) {
                out[found] = m_findings[i];
            }
            found++;
        }
    }
    return found;
}

void PoirotMode::checkSecurityMismatch(const Observation& obs, Investigation& inv) {
    // Only check beacons (they contain security info)
    if (obs.frameType != FrameType::Beacon) return;
    
    // Need baseline to compare against
    if (m_baseline == nullptr) return;
    
    // Find baseline entry for this SSID
    const SSIDBaseline* baseline = m_baseline->find(inv.ssid);
    if (baseline == nullptr) return;
    
    // Check if we already have this finding
    for (size_t i = 0; i < MAX_FINDINGS; i++) {
        if (m_findings[i].type == FindingType::SecurityMismatch &&
            m_findings[i].anomalyId == inv.anomalyId) {
            return;  // Already reported
        }
    }
    
    // Compare security type
    SecurityType baselineSec = baseline->security.type;
    SecurityType obsSec = obs.security.type;
    
    // Security downgrade = evil twin indicator
    if (baselineSec != obsSec && baselineSec > obsSec) {
        Finding* finding = createFinding(FindingType::SecurityMismatch, inv.anomalyId);
        if (finding != nullptr) {
            finding->setSSID(inv.ssid);
            macCopy(finding->bssid, inv.bssid);
            
            // Strong finding if downgrade to Open, Moderate otherwise
            if (obsSec == SecurityType::Open) {
                finding->strength = FindingStrength::Conclusive;
                finding->setExplanation("Security downgrade to OPEN - likely evil twin attack");
            } else {
                finding->strength = FindingStrength::Strong;
                finding->setExplanation("Security weaker than baseline - possible evil twin");
            }
            
            finding->metrics.security.expectedSecurity = static_cast<uint8_t>(baselineSec);
            finding->metrics.security.actualSecurity = static_cast<uint8_t>(obsSec);
            
            raiseFinding(*finding);
        }
    }
}

} // namespace dupin
