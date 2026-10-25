/**
 * @file test_dupin_core.cpp
 * @brief Unit tests for Dupin Core
 */

#include <unity.h>
#include "core/dupin_core.h"
#include "utils/time_utils.h"

using namespace dupin;

static DupinCore g_dupin;

// Test BSSID
static const uint8_t TEST_BSSID[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};

void setUp(void) {
    setMockMillis(1000);
    g_dupin = DupinCore();
    g_dupin.begin();
}

void tearDown(void) {
    g_dupin.end();
}

// Helper to create test hypothesis
Hypothesis makeHypothesis(HypothesisType type, const char* ssid = "TestNetwork") {
    static uint32_t nextId = 1;
    Hypothesis hyp;
    hyp.id = nextId++;
    hyp.type = type;
    hyp.setSSID(ssid);
    macCopy(hyp.bssid, TEST_BSSID);
    hyp.threat = defaultThreatLevel(type);
    hyp.confidence = 0.8f;
    hyp.createdAt = getTimestamp();
    return hyp;
}

// ============================================
// Basic Functionality
// ============================================

void test_dupin_core_name(void) {
    TEST_ASSERT_EQUAL_STRING("Dupin", g_dupin.name());
}

void test_dupin_core_starts_with_no_incidents(void) {
    TEST_ASSERT_EQUAL(0, g_dupin.getActiveIncidentCount());
    TEST_ASSERT_EQUAL(0, g_dupin.getTotalIncidentCount());
}

// ============================================
// Incident Creation
// ============================================

void test_dupin_creates_incident_from_hypothesis(void) {
    Hypothesis hyp = makeHypothesis(HypothesisType::EvilTwin);
    
    Incident* incident = g_dupin.createIncident(hyp);
    
    TEST_ASSERT_NOT_NULL(incident);
    TEST_ASSERT_EQUAL(1, g_dupin.getActiveIncidentCount());
    TEST_ASSERT_EQUAL(IncidentStatus::Active, incident->status);
    TEST_ASSERT_EQUAL_STRING("TestNetwork", incident->ssid);
}

void test_dupin_tracks_multiple_incidents(void) {
    g_dupin.createIncident(makeHypothesis(HypothesisType::EvilTwin, "Network1"));
    g_dupin.createIncident(makeHypothesis(HypothesisType::DeauthAttack, "Network2"));
    g_dupin.createIncident(makeHypothesis(HypothesisType::RogueAP, "Network3"));
    
    TEST_ASSERT_EQUAL(3, g_dupin.getActiveIncidentCount());
}

void test_dupin_finds_incident_by_id(void) {
    Incident* inc = g_dupin.createIncident(makeHypothesis(HypothesisType::EvilTwin));
    uint32_t id = inc->id;
    
    const Incident* found = g_dupin.findIncident(id);
    
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL(id, found->id);
}

// ============================================
// Incident Resolution
// ============================================

void test_dupin_resolves_incident(void) {
    Incident* inc = g_dupin.createIncident(makeHypothesis(HypothesisType::EvilTwin));
    uint32_t id = inc->id;
    
    bool result = g_dupin.resolveIncident(id, "Confirmed evil twin");
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(IncidentStatus::Resolved, inc->status);
    TEST_ASSERT_EQUAL(0, g_dupin.getActiveIncidentCount());
    TEST_ASSERT_EQUAL(1, g_dupin.getTotalIncidentCount());
}

void test_dupin_dismisses_incident(void) {
    Incident* inc = g_dupin.createIncident(makeHypothesis(HypothesisType::RogueAP));
    uint32_t id = inc->id;
    
    bool result = g_dupin.dismissIncident(id, "False positive");
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(IncidentStatus::Dismissed, inc->status);
}

// ============================================
// Session Statistics
// ============================================

void test_dupin_tracks_session_stats(void) {
    g_dupin.recordObservation();
    g_dupin.recordObservation();
    g_dupin.recordAnomaly();
    g_dupin.recordFinding();
    g_dupin.recordHypothesis();
    
    const SessionSummary& summary = g_dupin.getSummary();
    
    TEST_ASSERT_EQUAL(2, summary.observationsProcessed);
    TEST_ASSERT_EQUAL(1, summary.anomaliesDetected);
    TEST_ASSERT_EQUAL(1, summary.findingsGenerated);
    TEST_ASSERT_EQUAL(1, summary.hypothesesFormed);
}

void test_dupin_tracks_incident_stats(void) {
    Incident* inc1 = g_dupin.createIncident(makeHypothesis(HypothesisType::EvilTwin));
    Incident* inc2 = g_dupin.createIncident(makeHypothesis(HypothesisType::RogueAP));
    
    g_dupin.resolveIncident(inc1->id);
    g_dupin.dismissIncident(inc2->id);
    
    const SessionSummary& summary = g_dupin.getSummary();
    
    TEST_ASSERT_EQUAL(2, summary.incidentsCreated);
    TEST_ASSERT_EQUAL(1, summary.incidentsResolved);
    TEST_ASSERT_EQUAL(1, summary.incidentsDismissed);
}

// ============================================
// False Positive Learning
// ============================================

void test_dupin_matches_dismissed_by_ssid(void) {
    Hypothesis hyp1 = makeHypothesis(HypothesisType::RogueAP, "FalsePositiveAP");
    Incident* inc = g_dupin.createIncident(hyp1);
    g_dupin.dismissIncident(inc->id);
    
    Hypothesis hyp2 = makeHypothesis(HypothesisType::RogueAP, "FalsePositiveAP");
    
    bool matches = g_dupin.matchesDismissed(hyp2);
    
    TEST_ASSERT_TRUE(matches);
}

void test_dupin_no_match_different_ssid(void) {
    Hypothesis hyp1 = makeHypothesis(HypothesisType::RogueAP, "Network1");
    Incident* inc = g_dupin.createIncident(hyp1);
    g_dupin.dismissIncident(inc->id);
    
    // Use different SSID AND different BSSID
    Hypothesis hyp2;
    hyp2.id = 100;
    hyp2.type = HypothesisType::RogueAP;
    hyp2.setSSID("Network2");
    static const uint8_t OTHER_BSSID[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    macCopy(hyp2.bssid, OTHER_BSSID);
    
    bool matches = g_dupin.matchesDismissed(hyp2);
    
    TEST_ASSERT_FALSE(matches);
}

// ============================================
// Auto-Resolution
// ============================================

void test_dupin_auto_resolves_stale_incidents(void) {
    DupinCoreConfig config = g_dupin.getConfig();
    config.autoResolveTimeoutMs = 5000;  // 5 seconds
    g_dupin.setConfig(config);
    
    Incident* inc = g_dupin.createIncident(makeHypothesis(HypothesisType::EvilTwin));
    TEST_ASSERT_EQUAL(IncidentStatus::Active, inc->status);
    
    // Advance time past timeout
    advanceMockMillis(10000);
    g_dupin.update();
    
    TEST_ASSERT_EQUAL(IncidentStatus::Resolved, inc->status);
}

// ============================================
// Timeline Export
// ============================================

void test_dupin_exports_incident_timeline(void) {
    Hypothesis hyp = makeHypothesis(HypothesisType::EvilTwin);
    hyp.addEvidence(Evidence::Type::Anomaly, 101, 1.0f);
    hyp.addEvidence(Evidence::Type::Finding, 201, 1.0f);
    
    Incident* inc = g_dupin.createIncident(hyp);
    g_dupin.resolveIncident(inc->id);
    
    DupinCore::TimelineEntry entries[10];
    size_t count = g_dupin.getIncidentTimeline(inc->id, entries, 10);
    
    TEST_ASSERT_TRUE(count >= 3);  // Start, evidence, resolve
    TEST_ASSERT_EQUAL(DupinCore::TimelineEntry::Type::Start, entries[0].type);
}

// ============================================
// Clear Incidents
// ============================================

void test_dupin_clears_all_incidents(void) {
    g_dupin.createIncident(makeHypothesis(HypothesisType::EvilTwin));
    g_dupin.createIncident(makeHypothesis(HypothesisType::RogueAP));
    
    TEST_ASSERT_EQUAL(2, g_dupin.getActiveIncidentCount());
    
    g_dupin.clearIncidents();
    
    TEST_ASSERT_EQUAL(0, g_dupin.getTotalIncidentCount());
}

// ============================================
// Test Runner
// ============================================

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Basic
    RUN_TEST(test_dupin_core_name);
    RUN_TEST(test_dupin_core_starts_with_no_incidents);
    
    // Incident creation
    RUN_TEST(test_dupin_creates_incident_from_hypothesis);
    RUN_TEST(test_dupin_tracks_multiple_incidents);
    RUN_TEST(test_dupin_finds_incident_by_id);
    
    // Resolution
    RUN_TEST(test_dupin_resolves_incident);
    RUN_TEST(test_dupin_dismisses_incident);
    
    // Session stats
    RUN_TEST(test_dupin_tracks_session_stats);
    RUN_TEST(test_dupin_tracks_incident_stats);
    
    // False positive learning
    RUN_TEST(test_dupin_matches_dismissed_by_ssid);
    RUN_TEST(test_dupin_no_match_different_ssid);
    
    // Auto-resolution
    RUN_TEST(test_dupin_auto_resolves_stale_incidents);
    
    // Timeline
    RUN_TEST(test_dupin_exports_incident_timeline);
    
    // Clear
    RUN_TEST(test_dupin_clears_all_incidents);
    
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_dupin_core_name);
    RUN_TEST(test_dupin_core_starts_with_no_incidents);
    RUN_TEST(test_dupin_creates_incident_from_hypothesis);
    RUN_TEST(test_dupin_tracks_multiple_incidents);
    RUN_TEST(test_dupin_finds_incident_by_id);
    RUN_TEST(test_dupin_resolves_incident);
    RUN_TEST(test_dupin_dismisses_incident);
    RUN_TEST(test_dupin_tracks_session_stats);
    RUN_TEST(test_dupin_tracks_incident_stats);
    RUN_TEST(test_dupin_matches_dismissed_by_ssid);
    RUN_TEST(test_dupin_no_match_different_ssid);
    RUN_TEST(test_dupin_auto_resolves_stale_incidents);
    RUN_TEST(test_dupin_exports_incident_timeline);
    RUN_TEST(test_dupin_clears_all_incidents);
    UNITY_END();
}
void loop() {}
#endif
