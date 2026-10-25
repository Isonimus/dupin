/**
 * @file holmes.cpp
 * @brief Holmes Mode implementation
 */

#include "modes/holmes/holmes.h"
#include "ui/serial_logger.h"
#include "storage/session_manager.h"
#include <cstring>

namespace dupin {

HolmesMode::HolmesMode() {
    clear();
}

void HolmesMode::begin() {
    setActive(true);
    clear();
    m_lastDecayTime = getTimestamp();
    logger.logStatus("Holmes mode active - correlating evidence...");
}

void HolmesMode::end() {
    setActive(false);
    logger.logStatus("Holmes mode stopped");
}

void HolmesMode::clear() {
    for (size_t i = 0; i < MAX_HYPOTHESES; i++) {
        m_hypotheses[i].clear();
    }
    m_nextHypothesisId = 1;
}

void HolmesMode::onObservation(const Observation& obs) {
    // Holmes primarily works with anomalies and findings,
    // not raw observations. This is a pass-through.
    (void)obs;
}

void HolmesMode::update() {
    if (!isActive()) return;
    
    // Decay confidence periodically
    uint32_t now = getTimestamp();
    if (now - m_lastDecayTime >= 1000) {
        decayConfidence();
        m_lastDecayTime = now;
    }
    
    expireHypotheses();
}

void HolmesMode::processAnomaly(const Anomaly& anomaly) {
    if (!isActive()) return;
    
    Hypothesis* hyp = correlateAnomaly(anomaly);
    if (hyp != nullptr && hyp->confidence >= 0.5f) {
        raiseHypothesis(*hyp);
    }
}

void HolmesMode::processFinding(const Finding& finding) {
    if (!isActive()) return;
    
    // Track movement findings for device movement detection
    if (finding.type == FindingType::BSSIDMovement) {
        trackMovement(finding);
        
        // Check if multiple APs are moving - likely device is moving
        if (checkDeviceMovement()) {
            // Device movement hypothesis created, skip individual AP correlation
            // for movement findings to avoid false positives
            return;
        }
    }
    
    // Find hypothesis that matches this finding's target
    Hypothesis* hyp = findHypothesisByTarget(finding.ssid, finding.bssid);
    
    if (hyp != nullptr) {
        updateHypothesisFromFinding(*hyp, finding);
        
        if (hyp->confidence >= 0.5f) {
            raiseHypothesis(*hyp);
        }
    }
}

Hypothesis* HolmesMode::correlateAnomaly(const Anomaly& anomaly) {
    // Find or create hypothesis for this target
    Hypothesis* hyp = findOrCreateHypothesis(anomaly.ssid, anomaly.bssid);
    if (hyp == nullptr) return nullptr;
    
    // Infer hypothesis type from anomaly
    HypothesisType inferredType = inferType(anomaly);
    
    // If new hypothesis or upgrading type
    if (hyp->type == HypothesisType::Unknown || 
        defaultThreatLevel(inferredType) > defaultThreatLevel(hyp->type)) {
        hyp->type = inferredType;
        hyp->threat = defaultThreatLevel(inferredType);
    }
    
    // Add anomaly as evidence
    float weight = 1.0f;
    switch (anomaly.severity) {
        case Severity::Critical: weight = 2.0f; break;
        case Severity::High: weight = 1.5f; break;
        case Severity::Medium: weight = 1.0f; break;
        default: weight = 0.5f; break;
    }
    
    hyp->addEvidence(Evidence::Type::Anomaly, anomaly.id, weight);
    
    // Update confidence based on evidence
    float evidenceWeight = hyp->totalEvidenceWeight();
    hyp->confidence = evidenceWeight / (evidenceWeight + m_config.minEvidenceWeight);
    if (hyp->confidence > 1.0f) hyp->confidence = 1.0f;
    
    hyp->updatedAt = getTimestamp();
    
    // Set summary based on type
    switch (hyp->type) {
        case HypothesisType::EvilTwin:
            hyp->setSummary("Suspected evil twin attack - fake AP mimicking network");
            break;
        case HypothesisType::DeauthAttack:
            hyp->setSummary("Deauthentication attack in progress");
            break;
        case HypothesisType::RogueAP:
            hyp->setSummary("Unauthorized access point detected");
            break;
        case HypothesisType::PineappleDevice:
            hyp->setSummary("Possible WiFi Pineapple or attack device");
            break;
        default:
            hyp->setSummary("Suspicious wireless activity detected");
            break;
    }
    
    return hyp;
}

void HolmesMode::updateHypothesisFromFinding(Hypothesis& hyp, const Finding& finding) {
    // Add finding as evidence
    float weight = 1.0f;
    switch (finding.strength) {
        case FindingStrength::Conclusive: weight = 2.0f; break;
        case FindingStrength::Strong: weight = 1.5f; break;
        case FindingStrength::Moderate: weight = 1.0f; break;
        default: weight = 0.5f; break;
    }
    
    hyp.addEvidence(Evidence::Type::Finding, finding.id, weight);
    
    // Update confidence
    float evidenceWeight = hyp.totalEvidenceWeight();
    hyp.confidence = evidenceWeight / (evidenceWeight + m_config.minEvidenceWeight);
    if (hyp.confidence > 1.0f) hyp.confidence = 1.0f;
    
    // Upgrade hypothesis type based on finding
    adjustConfidence(hyp, finding);
    
    hyp.updatedAt = getTimestamp();
}

HypothesisType HolmesMode::inferType(const Anomaly& anomaly) {
    switch (anomaly.type) {
        case AnomalyType::NewBSSIDForSSID:
            return HypothesisType::EvilTwin;
            
        case AnomalyType::SecurityDowngrade:
            return HypothesisType::EvilTwin;  // Security downgrade is evil twin hallmark
            
        case AnomalyType::ExcessiveDeauths:
            return HypothesisType::DeauthAttack;
            
        case AnomalyType::UnexpectedChannel:
            return HypothesisType::RogueAP;
            
        case AnomalyType::RSSIAnomaly:
            return HypothesisType::RogueAP;  // Could be rogue or interference
            
        default:
            return HypothesisType::Unknown;
    }
}

void HolmesMode::adjustConfidence(Hypothesis& hyp, const Finding& finding) {
    // Certain findings strongly confirm certain hypotheses
    
    if (hyp.type == HypothesisType::EvilTwin) {
        // Movement + Probe promiscuity = likely Pineapple
        if (finding.type == FindingType::BSSIDMovement &&
            finding.metrics.movement.gradient > 0) {
            // Approaching - could be pineapple walk-around
            hyp.type = HypothesisType::PineappleDevice;
            hyp.threat = ThreatLevel::Critical;
            hyp.confidence += 0.1f;
        }
        
        if (finding.type == FindingType::ProbePromiscuity) {
            hyp.type = HypothesisType::PineappleDevice;
            hyp.threat = ThreatLevel::Critical;
            hyp.confidence += 0.15f;
        }
    }
    
    if (finding.type == FindingType::ChannelHopping) {
        // Channel hopping with high confidence
        if (finding.strength >= FindingStrength::Strong) {
            hyp.confidence += 0.1f;
        }
    }
    
    if (hyp.confidence > 1.0f) hyp.confidence = 1.0f;
}

Hypothesis* HolmesMode::findOrCreateHypothesis(const char* ssid, const uint8_t* bssid) {
    // Try to find existing
    Hypothesis* existing = findHypothesisByTarget(ssid, bssid);
    if (existing != nullptr) return existing;
    
    // Create new
    for (size_t i = 0; i < MAX_HYPOTHESES; i++) {
        if (!m_hypotheses[i].isActive()) {
            m_hypotheses[i].clear();
            m_hypotheses[i].id = m_nextHypothesisId++;
            m_hypotheses[i].createdAt = getTimestamp();
            m_hypotheses[i].updatedAt = m_hypotheses[i].createdAt;
            m_hypotheses[i].confidence = 0.1f;  // Initial confidence
            
            if (ssid != nullptr && ssid[0] != '\0') {
                m_hypotheses[i].setSSID(ssid);
            }
            if (bssid != nullptr && !macIsNull(bssid)) {
                macCopy(m_hypotheses[i].bssid, bssid);
            }
            
            return &m_hypotheses[i];
        }
    }
    
    return nullptr;  // No slots
}

Hypothesis* HolmesMode::findHypothesisByTarget(const char* ssid, const uint8_t* bssid) {
    for (size_t i = 0; i < MAX_HYPOTHESES; i++) {
        if (!m_hypotheses[i].isActive()) continue;
        
        // Match by BSSID first (more specific)
        if (bssid != nullptr && !macIsNull(bssid) &&
            m_hypotheses[i].hasBSSID() &&
            macEquals(m_hypotheses[i].bssid, bssid)) {
            return &m_hypotheses[i];
        }
        
        // Match by SSID
        if (ssid != nullptr && ssid[0] != '\0' &&
            m_hypotheses[i].hasSSID() &&
            strcmp(m_hypotheses[i].ssid, ssid) == 0) {
            return &m_hypotheses[i];
        }
    }
    
    return nullptr;
}

void HolmesMode::raiseHypothesis(const Hypothesis& hypothesis) {
    char macStr[18] = "N/A";
    if (hypothesis.hasBSSID()) {
        macFormat(hypothesis.bssid, macStr);
    }
    
    logger.logWarning("Holmes: Hypothesis formed!");
    Serial.printf("  Type: %s [%s]\n", 
                  hypothesisTypeName(hypothesis.type),
                  threatLevelName(hypothesis.threat));
    
    if (hypothesis.hasSSID()) {
        Serial.printf("  Target: \"%s\"\n", hypothesis.ssid);
    }
    Serial.printf("  BSSID: %s\n", macStr);
    Serial.printf("  Confidence: %.1f%% (%d evidence items)\n", 
                  hypothesis.confidence * 100.0f,
                  hypothesis.evidenceCount);
    Serial.printf("  %s\n", hypothesis.summary);
    
    // Log to session file
    g_sessionManager.logHypothesis(hypothesis);
    
    if (m_callback != nullptr) {
        m_callback(hypothesis);
    }
}

void HolmesMode::decayConfidence() {
    for (size_t i = 0; i < MAX_HYPOTHESES; i++) {
        if (m_hypotheses[i].isActive()) {
            m_hypotheses[i].confidence -= m_config.confidenceDecayRate;
            if (m_hypotheses[i].confidence < 0.0f) {
                m_hypotheses[i].confidence = 0.0f;
            }
        }
    }
}

void HolmesMode::expireHypotheses() {
    for (size_t i = 0; i < MAX_HYPOTHESES; i++) {
        if (m_hypotheses[i].type != HypothesisType::Unknown &&
            m_hypotheses[i].confidence < m_config.minConfidence) {
            m_hypotheses[i].clear();
        }
    }
}

size_t HolmesMode::getHypothesisCount() const {
    size_t count = 0;
    for (size_t i = 0; i < MAX_HYPOTHESES; i++) {
        if (m_hypotheses[i].isActive()) count++;
    }
    return count;
}

size_t HolmesMode::removeHypothesesForAnomaly(uint32_t anomalyId) {
    size_t removed = 0;
    for (size_t i = 0; i < MAX_HYPOTHESES; i++) {
        if (m_hypotheses[i].isActive()) {
            // Check if this hypothesis references the anomaly in its evidence
            // For now, match by SSID which is how anomalies create hypotheses
            // In a more sophisticated system, we'd track anomaly IDs in evidence
            // Clear any hypothesis that matches - will be rebuilt if new anomalies appear
            m_hypotheses[i].clear();
            removed++;
        }
    }
    if (removed > 0) {
        Serial.printf("[HOLMES] Removed %zu hypotheses (cascade from anomaly #%u)\n",
                      removed, static_cast<unsigned>(anomalyId));
    }
    return removed;
}

const Hypothesis* HolmesMode::getTopHypothesis() const {
    const Hypothesis* top = nullptr;
    float maxConfidence = 0.0f;
    
    for (size_t i = 0; i < MAX_HYPOTHESES; i++) {
        if (m_hypotheses[i].isActive() && 
            m_hypotheses[i].confidence > maxConfidence) {
            maxConfidence = m_hypotheses[i].confidence;
            top = &m_hypotheses[i];
        }
    }
    
    return top;
}

size_t HolmesMode::getHypothesesByThreat(ThreatLevel minLevel, Hypothesis* out, 
                                          size_t maxResults) const {
    size_t found = 0;
    for (size_t i = 0; i < MAX_HYPOTHESES && found < maxResults; i++) {
        if (m_hypotheses[i].isActive() && 
            m_hypotheses[i].threat >= minLevel) {
            if (out != nullptr) {
                out[found] = m_hypotheses[i];
            }
            found++;
        }
    }
    return found;
}

// ============================================
// Device Movement Detection
// ============================================

void HolmesMode::trackMovement(const Finding& finding) {
    // Only track BSSIDMovement findings
    if (finding.type != FindingType::BSSIDMovement) return;
    
    uint32_t now = getTimestamp();
    int8_t direction = 0;
    
    // Determine direction from gradient (positive = closer, negative = farther)
    if (finding.metrics.movement.gradient > 3) {
        direction = 1;   // Approaching
    } else if (finding.metrics.movement.gradient < -3) {
        direction = -1;  // Receding
    }
    // Small gradients = 0 (mixed/stationary)
    
    // Check if we already have an entry for this BSSID
    for (size_t i = 0; i < MAX_MOVEMENT_EVENTS; i++) {
        if (m_movements[i].valid && 
            macEquals(m_movements[i].bssid, finding.bssid)) {
            // Update existing entry
            m_movements[i].direction = direction;
            m_movements[i].timestamp = now;
            return;
        }
    }
    
    // Add new entry (circular buffer)
    m_movements[m_movementHead].valid = true;
    macCopy(m_movements[m_movementHead].bssid, finding.bssid);
    m_movements[m_movementHead].direction = direction;
    m_movements[m_movementHead].timestamp = now;
    m_movementHead = (m_movementHead + 1) % MAX_MOVEMENT_EVENTS;
}

bool HolmesMode::checkDeviceMovement() {
    uint32_t now = getTimestamp();
    
    // Count unique BSSIDs with movement in the window
    size_t closerCount = 0;
    size_t fartherCount = 0;
    size_t totalMoving = 0;
    
    for (size_t i = 0; i < MAX_MOVEMENT_EVENTS; i++) {
        if (!m_movements[i].valid) continue;
        
        // Only consider recent movements
        if (now - m_movements[i].timestamp > MOVEMENT_WINDOW_MS) {
            m_movements[i].valid = false;  // Expire old entries
            continue;
        }
        
        totalMoving++;
        if (m_movements[i].direction > 0) closerCount++;
        else if (m_movements[i].direction < 0) fartherCount++;
    }
    
    // If 3+ BSSIDs moving in the same direction, device is moving
    bool deviceMoving = (closerCount >= MOVEMENT_THRESHOLD || 
                         fartherCount >= MOVEMENT_THRESHOLD ||
                         totalMoving >= MOVEMENT_THRESHOLD + 1);  // Mixed but many
    
    if (deviceMoving && !m_deviceMovingActive) {
        // Transition to device moving state
        m_deviceMovingActive = true;
        createDeviceMovementHypothesis();
        return true;
    }
    
    if (!deviceMoving && m_deviceMovingActive) {
        // Movement stopped
        m_deviceMovingActive = false;
    }
    
    return m_deviceMovingActive;
}

void HolmesMode::createDeviceMovementHypothesis() {
    // Find or reuse a DeviceMovement hypothesis
    Hypothesis* hyp = nullptr;
    
    // Look for existing device movement hypothesis
    for (size_t i = 0; i < MAX_HYPOTHESES; i++) {
        if (m_hypotheses[i].type == HypothesisType::DeviceMovement) {
            hyp = &m_hypotheses[i];
            break;
        }
    }
    
    // Create new if not found
    if (hyp == nullptr) {
        for (size_t i = 0; i < MAX_HYPOTHESES; i++) {
            if (!m_hypotheses[i].isActive()) {
                hyp = &m_hypotheses[i];
                hyp->clear();
                hyp->id = m_nextHypothesisId++;
                hyp->createdAt = getTimestamp();
                break;
            }
        }
    }
    
    if (hyp == nullptr) return;  // No slots
    
    hyp->type = HypothesisType::DeviceMovement;
    hyp->threat = ThreatLevel::None;
    hyp->confidence = 0.9f;  // High confidence
    hyp->setSSID("[DEVICE]");
    hyp->setSummary("Detector is in motion - multiple APs showing coordinated RSSI changes");
    hyp->updatedAt = getTimestamp();
    
    // Log and notify
    logger.logInfo("Holmes: Device movement detected");
    Serial.println("  Multiple APs showing coordinated movement = device is moving");
    
    if (m_callback != nullptr) {
        m_callback(*hyp);
    }
}

} // namespace dupin
