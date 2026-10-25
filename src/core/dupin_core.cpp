/**
 * @file dupin_core.cpp
 * @brief Dupin Core implementation
 */

#include "core/dupin_core.h"
#include "ui/serial_logger.h"
#include <cstring>

namespace dupin {

DupinCore::DupinCore() {
    clearIncidents();
}

void DupinCore::begin() {
    setActive(true);
    m_summary = SessionSummary{};
    m_summary.sessionStartTime = getTimestamp();
    m_lastUpdateTime = m_summary.sessionStartTime;
    
    logger.logStatus("Dupin Core active - all systems coordinated");
}

void DupinCore::end() {
    // Log session summary
    logger.logStatus("Dupin Core shutting down");
    Serial.printf("Session Summary:\n");
    Serial.printf("  Observations: %u\n", static_cast<unsigned>(m_summary.observationsProcessed));
    Serial.printf("  Anomalies: %u\n", static_cast<unsigned>(m_summary.anomaliesDetected));
    Serial.printf("  Findings: %u\n", static_cast<unsigned>(m_summary.findingsGenerated));
    Serial.printf("  Hypotheses: %u\n", static_cast<unsigned>(m_summary.hypothesesFormed));
    Serial.printf("  Incidents: %u created, %u resolved, %u dismissed\n",
                  static_cast<unsigned>(m_summary.incidentsCreated),
                  static_cast<unsigned>(m_summary.incidentsResolved),
                  static_cast<unsigned>(m_summary.incidentsDismissed));
    
    setActive(false);
}

void DupinCore::onObservation(const Observation& obs) {
    // Dupin Core itself doesn't process observations directly
    // It coordinates other modes which do
    (void)obs;
}

void DupinCore::update() {
    if (!isActive()) return;
    
    uint32_t now = getTimestamp();
    
    // Periodic maintenance
    if (now - m_lastUpdateTime >= 5000) {  // Every 5 seconds
        autoResolveStaleIncidents();
        m_lastUpdateTime = now;
    }
}

Incident* DupinCore::createIncident(const Hypothesis& hypothesis) {
    if (!isActive()) return nullptr;
    
    // Find empty slot
    for (size_t i = 0; i < MAX_INCIDENTS; i++) {
        if (m_incidents[i].id == 0) {
            Incident& inc = m_incidents[i];
            inc.clear();
            
            inc.id = m_nextIncidentId++;
            inc.status = IncidentStatus::Active;
            inc.createdAt = getTimestamp();
            inc.updatedAt = inc.createdAt;
            
            // Copy from hypothesis
            inc.conclusion = hypothesis;
            if (hypothesis.hasSSID()) {
                inc.setSSID(hypothesis.ssid);
            }
            if (hypothesis.hasBSSID()) {
                macCopy(inc.bssid, hypothesis.bssid);
            }
            
            // Copy evidence references
            for (uint8_t j = 0; j < hypothesis.evidenceCount; j++) {
                if (hypothesis.evidence[j].type == Evidence::Type::Anomaly) {
                    inc.addAnomaly(hypothesis.evidence[j].id);
                } else if (hypothesis.evidence[j].type == Evidence::Type::Finding) {
                    inc.addFinding(hypothesis.evidence[j].id);
                }
            }
            
            m_summary.incidentsCreated++;
            
            logger.logStatus("Dupin: New incident created!");
            Serial.printf("  Incident #%u: %s\n", static_cast<unsigned>(inc.id),
                          hypothesisTypeName(hypothesis.type));
            if (inc.hasSSID()) {
                Serial.printf("  Target: \"%s\"\n", inc.ssid);
            }
            Serial.printf("  Threat: %s, Evidence: %d items\n",
                          threatLevelName(hypothesis.threat),
                          inc.totalEvidence());
            
            return &inc;
        }
    }
    
    logger.logWarning("Dupin: No incident slots available");
    return nullptr;
}

Incident* DupinCore::findIncident(uint32_t id) {
    for (size_t i = 0; i < MAX_INCIDENTS; i++) {
        if (m_incidents[i].id == id) {
            return &m_incidents[i];
        }
    }
    return nullptr;
}

const Incident* DupinCore::findIncident(uint32_t id) const {
    for (size_t i = 0; i < MAX_INCIDENTS; i++) {
        if (m_incidents[i].id == id) {
            return &m_incidents[i];
        }
    }
    return nullptr;
}

size_t DupinCore::getActiveIncidentCount() const {
    size_t count = 0;
    for (size_t i = 0; i < MAX_INCIDENTS; i++) {
        if (m_incidents[i].isActive()) count++;
    }
    return count;
}

size_t DupinCore::getTotalIncidentCount() const {
    size_t count = 0;
    for (size_t i = 0; i < MAX_INCIDENTS; i++) {
        if (m_incidents[i].id != 0) count++;
    }
    return count;
}

bool DupinCore::resolveIncident(uint32_t id, const char* notes) {
    Incident* inc = findIncident(id);
    if (inc == nullptr || !inc->isActive()) return false;
    
    inc->resolve(getTimestamp());
    if (notes != nullptr) {
        inc->setNotes(notes);
    }
    
    m_summary.incidentsResolved++;
    
    logger.logStatus("Dupin: Incident resolved");
    Serial.printf("  Incident #%u: %s\n", static_cast<unsigned>(id),
                  hypothesisTypeName(inc->conclusion.type));
    
    return true;
}

bool DupinCore::dismissIncident(uint32_t id, const char* notes) {
    Incident* inc = findIncident(id);
    if (inc == nullptr || !inc->isActive()) return false;
    
    inc->dismiss(getTimestamp());
    if (notes != nullptr) {
        inc->setNotes(notes);
    }
    
    m_summary.incidentsDismissed++;
    
    logger.logInfo("Dupin: Incident dismissed as false positive");
    Serial.printf("  Incident #%u\n", static_cast<unsigned>(id));
    
    return true;
}

void DupinCore::clearIncidents() {
    for (size_t i = 0; i < MAX_INCIDENTS; i++) {
        m_incidents[i].clear();
    }
    m_nextIncidentId = 1;
}

void DupinCore::autoResolveStaleIncidents() {
    uint32_t now = getTimestamp();
    
    for (size_t i = 0; i < MAX_INCIDENTS; i++) {
        if (m_incidents[i].isActive()) {
            uint32_t inactive = now - m_incidents[i].updatedAt;
            if (inactive > m_config.autoResolveTimeoutMs) {
                m_incidents[i].resolve(now);
                m_incidents[i].setNotes("Auto-resolved due to inactivity");
                m_summary.incidentsResolved++;
                
                logger.logInfo("Dupin: Auto-resolved stale incident");
                Serial.printf("  Incident #%u after %u ms inactive\n",
                              static_cast<unsigned>(m_incidents[i].id),
                              static_cast<unsigned>(inactive));
            }
        }
    }
}

bool DupinCore::matchesDismissed(const Hypothesis& hypothesis) const {
    if (!m_config.learnFromDismissals) return false;
    
    // Check if we've dismissed a similar incident before
    for (size_t i = 0; i < MAX_INCIDENTS; i++) {
        if (m_incidents[i].status == IncidentStatus::Dismissed &&
            m_incidents[i].conclusion.type == hypothesis.type) {
            
            // Match by BSSID
            if (hypothesis.hasBSSID() && m_incidents[i].hasBSSID() &&
                macEquals(m_incidents[i].bssid, hypothesis.bssid)) {
                return true;
            }
            
            // Match by SSID
            if (hypothesis.hasSSID() && m_incidents[i].hasSSID() &&
                strcmp(m_incidents[i].ssid, hypothesis.ssid) == 0) {
                return true;
            }
        }
    }
    
    return false;
}

size_t DupinCore::getIncidentTimeline(uint32_t incidentId, TimelineEntry* out, 
                                       size_t maxEntries) const {
    const Incident* inc = findIncident(incidentId);
    if (inc == nullptr || out == nullptr || maxEntries == 0) return 0;
    
    size_t count = 0;
    
    // Add start event
    if (count < maxEntries) {
        out[count].timestamp = inc->createdAt;
        out[count].type = TimelineEntry::Type::Start;
        out[count].refId = inc->id;
        count++;
    }
    
    // Add anomaly events (we only have IDs, not timestamps)
    for (uint8_t i = 0; i < inc->anomalyCount && count < maxEntries; i++) {
        out[count].timestamp = inc->createdAt;  // Approximate
        out[count].type = TimelineEntry::Type::Anomaly;
        out[count].refId = inc->anomalyIds[i];
        count++;
    }
    
    // Add finding events
    for (uint8_t i = 0; i < inc->findingCount && count < maxEntries; i++) {
        out[count].timestamp = inc->createdAt;  // Approximate
        out[count].type = TimelineEntry::Type::Finding;
        out[count].refId = inc->findingIds[i];
        count++;
    }
    
    // Add resolution event if resolved
    if (inc->resolvedAt > 0 && count < maxEntries) {
        out[count].timestamp = inc->resolvedAt;
        out[count].type = TimelineEntry::Type::Resolve;
        out[count].refId = inc->id;
        count++;
    }
    
    return count;
}

} // namespace dupin
