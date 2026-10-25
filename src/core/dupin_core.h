/**
 * @file dupin_core.h
 * @brief Dupin Core - Memory & Forensic Reconstruction
 * 
 * "To a great mind, nothing is little."
 * 
 * The Dupin Core ties together all detective modes, manages
 * long-term incident storage, and enables forensic analysis.
 */

#pragma once

#include "modes/mode.h"
#include "core/incident.h"
#include "core/hypothesis.h"
#include "core/baseline.h"
#include "utils/time_utils.h"

namespace dupin {

// Maximum incidents to store
constexpr size_t MAX_INCIDENTS = 16;
constexpr uint32_t INCIDENT_AUTO_RESOLVE_MS = 600000;  // 10 minutes inactive

/**
 * @brief Dupin Core configuration
 */
struct DupinCoreConfig {
    uint32_t autoResolveTimeoutMs = INCIDENT_AUTO_RESOLVE_MS;
    uint32_t baselineAgingDays = 7;  // Baseline entries older than this may age out
    bool learnFromDismissals = true;  // Use dismissed incidents to reduce false positives
};

/**
 * @brief Summary statistics for the session
 */
struct SessionSummary {
    uint32_t sessionStartTime = 0;
    uint32_t observationsProcessed = 0;
    uint32_t anomaliesDetected = 0;
    uint32_t findingsGenerated = 0;
    uint32_t hypothesesFormed = 0;
    uint32_t incidentsCreated = 0;
    uint32_t incidentsResolved = 0;
    uint32_t incidentsDismissed = 0;
};

/**
 * @brief Dupin Core - The Master Mind
 * 
 * Coordinates all detective modes and maintains the long-term
 * memory and incident history.
 */
class DupinCore : public IMode {
public:
    DupinCore();
    
    // IMode interface
    const char* name() const override { return "Dupin"; }
    void onObservation(const Observation& obs) override;
    void update() override;
    void begin() override;
    void end() override;
    
    /**
     * @brief Create incident from hypothesis
     */
    Incident* createIncident(const Hypothesis& hypothesis);
    
    /**
     * @brief Get all incidents
     */
    const Incident* getIncidents() const { return m_incidents; }
    
    /**
     * @brief Get incident by ID
     */
    Incident* findIncident(uint32_t id);
    const Incident* findIncident(uint32_t id) const;
    
    /**
     * @brief Get active incident count
     */
    size_t getActiveIncidentCount() const;
    
    /**
     * @brief Get total incident count (all statuses)
     */
    size_t getTotalIncidentCount() const;
    
    /**
     * @brief Resolve an incident
     */
    bool resolveIncident(uint32_t id, const char* notes = nullptr);
    
    /**
     * @brief Dismiss an incident as false positive
     */
    bool dismissIncident(uint32_t id, const char* notes = nullptr);
    
    /**
     * @brief Get session summary
     */
    const SessionSummary& getSummary() const { return m_summary; }
    
    /**
     * @brief Get configuration
     */
    const DupinCoreConfig& getConfig() const { return m_config; }
    void setConfig(const DupinCoreConfig& config) { m_config = config; }
    
    /**
     * @brief Clear all incidents
     */
    void clearIncidents();
    
    /**
     * @brief Export incident timeline (for forensic analysis)
     */
    struct TimelineEntry {
        uint32_t timestamp;
        enum class Type { Start, Anomaly, Finding, Hypothesis, Resolve } type;
        uint32_t refId;
    };
    
    size_t getIncidentTimeline(uint32_t incidentId, TimelineEntry* out, size_t maxEntries) const;
    
    /**
     * @brief Check if a hypothesis matches past dismissed incidents
     * (false positive suppression)
     */
    bool matchesDismissed(const Hypothesis& hypothesis) const;
    
    /**
     * @brief Record statistics
     */
    void recordObservation() { m_summary.observationsProcessed++; }
    void recordAnomaly() { m_summary.anomaliesDetected++; }
    void recordFinding() { m_summary.findingsGenerated++; }
    void recordHypothesis() { m_summary.hypothesesFormed++; }

private:
    void autoResolveStaleIncidents();
    
    DupinCoreConfig m_config;
    Incident m_incidents[MAX_INCIDENTS];
    uint32_t m_nextIncidentId = 1;
    SessionSummary m_summary;
    uint32_t m_lastUpdateTime = 0;
};

} // namespace dupin
