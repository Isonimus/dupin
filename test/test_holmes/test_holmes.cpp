/**
 * @file test_holmes.cpp
 * @brief Unit tests for Holmes mode
 */

#include <unity.h>
#include "modes/holmes/holmes.h"
#include "utils/time_utils.h"

using namespace dupin;

static HolmesMode g_holmes;

// Test BSSIDs
static const uint8_t TEST_BSSID[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
static const uint8_t BSSID2[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

void setUp(void) {
    setMockMillis(1000);
    g_holmes = HolmesMode();
    g_holmes.begin();
}

void tearDown(void) {
    g_holmes.end();
}

// Helper to create test anomaly
Anomaly makeAnomaly(AnomalyType type, const char* ssid, const uint8_t* bssid,
                    Severity severity = Severity::High) {
    static uint32_t nextId = 1;
    Anomaly anomaly;
    anomaly.id = nextId++;
    anomaly.type = type;
    anomaly.setSSID(ssid);
    if (bssid) macCopy(anomaly.bssid, bssid);
    anomaly.severity = severity;
    anomaly.confidence = 0.8f;
    anomaly.firstSeen = getTimestamp();
    return anomaly;
}

// Helper to create test finding
Finding makeFinding(FindingType type, uint32_t anomalyId, const char* ssid,
                    FindingStrength strength = FindingStrength::Moderate) {
    static uint32_t nextId = 1;
    Finding finding;
    finding.id = nextId++;
    finding.type = type;
    finding.anomalyId = anomalyId;
    finding.setSSID(ssid);
    finding.strength = strength;
    finding.timestamp = getTimestamp();
    return finding;
}

// ============================================
// Basic Functionality
// ============================================

void test_holmes_name(void) {
    TEST_ASSERT_EQUAL_STRING("Holmes", g_holmes.name());
}

void test_holmes_starts_with_no_hypotheses(void) {
    TEST_ASSERT_EQUAL(0, g_holmes.getHypothesisCount());
}

// ============================================
// Hypothesis Generation from Anomalies
// ============================================

void test_holmes_creates_hypothesis_from_new_bssid(void) {
    Anomaly anomaly = makeAnomaly(AnomalyType::NewBSSIDForSSID, "TestNetwork", TEST_BSSID);
    
    g_holmes.processAnomaly(anomaly);
    
    TEST_ASSERT_EQUAL(1, g_holmes.getHypothesisCount());
    
    const Hypothesis* hyp = g_holmes.getTopHypothesis();
    TEST_ASSERT_NOT_NULL(hyp);
    TEST_ASSERT_EQUAL(HypothesisType::EvilTwin, hyp->type);
    TEST_ASSERT_EQUAL(ThreatLevel::Critical, hyp->threat);
}

void test_holmes_creates_hypothesis_from_security_downgrade(void) {
    Anomaly anomaly = makeAnomaly(AnomalyType::SecurityDowngrade, "SecureNet", TEST_BSSID, 
                                   Severity::Critical);
    
    g_holmes.processAnomaly(anomaly);
    
    const Hypothesis* hyp = g_holmes.getTopHypothesis();
    TEST_ASSERT_NOT_NULL(hyp);
    TEST_ASSERT_EQUAL(HypothesisType::EvilTwin, hyp->type);
}

void test_holmes_creates_hypothesis_from_deauth_flood(void) {
    Anomaly anomaly = makeAnomaly(AnomalyType::ExcessiveDeauths, "", nullptr);
    
    g_holmes.processAnomaly(anomaly);
    
    const Hypothesis* hyp = g_holmes.getTopHypothesis();
    TEST_ASSERT_NOT_NULL(hyp);
    TEST_ASSERT_EQUAL(HypothesisType::DeauthAttack, hyp->type);
    TEST_ASSERT_EQUAL(ThreatLevel::High, hyp->threat);
}

// ============================================
// Evidence Accumulation
// ============================================

void test_holmes_accumulates_evidence(void) {
    Anomaly anomaly1 = makeAnomaly(AnomalyType::NewBSSIDForSSID, "TestNetwork", TEST_BSSID);
    Anomaly anomaly2 = makeAnomaly(AnomalyType::UnexpectedChannel, "TestNetwork", TEST_BSSID);
    
    g_holmes.processAnomaly(anomaly1);
    g_holmes.processAnomaly(anomaly2);
    
    // Should still be 1 hypothesis (same target)
    TEST_ASSERT_EQUAL(1, g_holmes.getHypothesisCount());
    
    const Hypothesis* hyp = g_holmes.getTopHypothesis();
    TEST_ASSERT_NOT_NULL(hyp);
    TEST_ASSERT_EQUAL(2, hyp->evidenceCount);
}

void test_holmes_confidence_increases_with_evidence(void) {
    Anomaly anomaly1 = makeAnomaly(AnomalyType::NewBSSIDForSSID, "TestNetwork", TEST_BSSID);
    g_holmes.processAnomaly(anomaly1);
    
    float conf1 = g_holmes.getTopHypothesis()->confidence;
    
    Anomaly anomaly2 = makeAnomaly(AnomalyType::SecurityDowngrade, "TestNetwork", TEST_BSSID,
                                    Severity::Critical);
    g_holmes.processAnomaly(anomaly2);
    
    float conf2 = g_holmes.getTopHypothesis()->confidence;
    
    TEST_ASSERT_TRUE(conf2 > conf1);
}

// ============================================
// Finding Integration
// ============================================

void test_holmes_incorporates_findings(void) {
    Anomaly anomaly = makeAnomaly(AnomalyType::NewBSSIDForSSID, "TestNetwork", TEST_BSSID);
    g_holmes.processAnomaly(anomaly);
    
    Finding finding = makeFinding(FindingType::BSSIDMovement, anomaly.id, "TestNetwork",
                                   FindingStrength::Strong);
    finding.metrics.movement.gradient = 15;  // Approaching
    
    g_holmes.processFinding(finding);
    
    const Hypothesis* hyp = g_holmes.getTopHypothesis();
    TEST_ASSERT_NOT_NULL(hyp);
    TEST_ASSERT_EQUAL(2, hyp->evidenceCount);  // Anomaly + Finding
}

void test_holmes_upgrades_to_pineapple_on_promiscuity(void) {
    Anomaly anomaly = makeAnomaly(AnomalyType::NewBSSIDForSSID, "TestNetwork", TEST_BSSID);
    g_holmes.processAnomaly(anomaly);
    
    TEST_ASSERT_EQUAL(HypothesisType::EvilTwin, g_holmes.getTopHypothesis()->type);
    
    Finding finding = makeFinding(FindingType::ProbePromiscuity, anomaly.id, "TestNetwork",
                                   FindingStrength::Strong);
    g_holmes.processFinding(finding);
    
    // Should upgrade to Pineapple
    TEST_ASSERT_EQUAL(HypothesisType::PineappleDevice, g_holmes.getTopHypothesis()->type);
    TEST_ASSERT_EQUAL(ThreatLevel::Critical, g_holmes.getTopHypothesis()->threat);
}

// ============================================
// Multiple Hypotheses
// ============================================

void test_holmes_tracks_multiple_hypotheses(void) {
    Anomaly anomaly1 = makeAnomaly(AnomalyType::NewBSSIDForSSID, "Network1", TEST_BSSID);
    Anomaly anomaly2 = makeAnomaly(AnomalyType::ExcessiveDeauths, "Network2", BSSID2);
    
    g_holmes.processAnomaly(anomaly1);
    g_holmes.processAnomaly(anomaly2);
    
    TEST_ASSERT_EQUAL(2, g_holmes.getHypothesisCount());
}

void test_holmes_get_hypotheses_by_threat(void) {
    Anomaly anomaly1 = makeAnomaly(AnomalyType::NewBSSIDForSSID, "Network1", TEST_BSSID);
    Anomaly anomaly2 = makeAnomaly(AnomalyType::RSSIAnomaly, "Network2", BSSID2, Severity::Low);
    
    g_holmes.processAnomaly(anomaly1);
    g_holmes.processAnomaly(anomaly2);
    
    // Filter for CRITICAL only (EvilTwin)
    Hypothesis results[5];
    size_t count = g_holmes.getHypothesesByThreat(ThreatLevel::Critical, results, 5);
    
    TEST_ASSERT_EQUAL(1, count);  // Only EvilTwin is CRITICAL
    TEST_ASSERT_EQUAL(HypothesisType::EvilTwin, results[0].type);
}

// ============================================
// Confidence Decay
// ============================================

// A decay rate large enough to cross minConfidence (0.05) within a bounded loop, so the
// expiry test's step count is arithmetic rather than a guess.
static constexpr float TEST_DECAY_RATE = 0.1f;
static constexpr int DECAY_STEPS_TO_EXPIRY = 20;

static void enableDecay(float ratePerStep) {
    HolmesConfig config = g_holmes.getConfig();
    config.confidenceDecayRate = ratePerStep;
    g_holmes.setConfig(config);
}

// Automatic decay is OFF by default: a hypothesis stands until the anomaly behind it is
// dismissed (ADR-0003). These two tests guard that default.
void test_holmes_hypothesis_confidence_holds_by_default(void) {
    g_holmes.processAnomaly(makeAnomaly(AnomalyType::NewBSSIDForSSID, "TestNetwork", TEST_BSSID));

    float initialConf = g_holmes.getTopHypothesis()->confidence;
    TEST_ASSERT_TRUE(initialConf > 0);

    advanceMockMillis(5000);
    g_holmes.update();

    TEST_ASSERT_EQUAL_FLOAT(initialConf, g_holmes.getTopHypothesis()->confidence);
}

void test_holmes_hypothesis_does_not_expire_by_default(void) {
    g_holmes.processAnomaly(makeAnomaly(AnomalyType::RSSIAnomaly, "WeakNetwork", TEST_BSSID, Severity::Low));
    TEST_ASSERT_EQUAL(1, g_holmes.getHypothesisCount());

    for (int i = 0; i < 300; i++) {
        advanceMockMillis(1000);
        g_holmes.update();
    }

    TEST_ASSERT_EQUAL(1, g_holmes.getHypothesisCount());
}

// The mechanism still works when a rate is configured — without this, zeroing the rate
// would be indistinguishable from deleting decayConfidence().
void test_holmes_hypothesis_confidence_decays_when_configured(void) {
    enableDecay(TEST_DECAY_RATE);
    g_holmes.processAnomaly(makeAnomaly(AnomalyType::NewBSSIDForSSID, "TestNetwork", TEST_BSSID));

    float initialConf = g_holmes.getTopHypothesis()->confidence;
    TEST_ASSERT_TRUE(initialConf > TEST_DECAY_RATE);

    // update() decays once per elapsed second, not once per call.
    advanceMockMillis(1000);
    g_holmes.update();

    TEST_ASSERT_FLOAT_WITHIN(0.001f, initialConf - TEST_DECAY_RATE,
                             g_holmes.getTopHypothesis()->confidence);
}

void test_holmes_hypothesis_expires_when_decay_configured(void) {
    enableDecay(TEST_DECAY_RATE);
    g_holmes.processAnomaly(makeAnomaly(AnomalyType::RSSIAnomaly, "WeakNetwork", TEST_BSSID, Severity::Low));
    TEST_ASSERT_EQUAL(1, g_holmes.getHypothesisCount());

    for (int i = 0; i < DECAY_STEPS_TO_EXPIRY; i++) {
        advanceMockMillis(1000);
        g_holmes.update();
    }

    TEST_ASSERT_EQUAL(0, g_holmes.getHypothesisCount());
}

// ============================================
// Inactive Mode
// ============================================

void test_holmes_ignores_when_inactive(void) {
    g_holmes.end();
    
    Anomaly anomaly = makeAnomaly(AnomalyType::NewBSSIDForSSID, "TestNetwork", TEST_BSSID);
    g_holmes.processAnomaly(anomaly);
    
    TEST_ASSERT_EQUAL(0, g_holmes.getHypothesisCount());
}

// ============================================
// Test Runner
// ============================================

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Basic
    RUN_TEST(test_holmes_name);
    RUN_TEST(test_holmes_starts_with_no_hypotheses);
    
    // Hypothesis generation
    RUN_TEST(test_holmes_creates_hypothesis_from_new_bssid);
    RUN_TEST(test_holmes_creates_hypothesis_from_security_downgrade);
    RUN_TEST(test_holmes_creates_hypothesis_from_deauth_flood);
    
    // Evidence accumulation
    RUN_TEST(test_holmes_accumulates_evidence);
    RUN_TEST(test_holmes_confidence_increases_with_evidence);
    
    // Finding integration
    RUN_TEST(test_holmes_incorporates_findings);
    RUN_TEST(test_holmes_upgrades_to_pineapple_on_promiscuity);
    
    // Multiple hypotheses
    RUN_TEST(test_holmes_tracks_multiple_hypotheses);
    RUN_TEST(test_holmes_get_hypotheses_by_threat);
    
    // Confidence decay
    RUN_TEST(test_holmes_hypothesis_confidence_holds_by_default);
    RUN_TEST(test_holmes_hypothesis_does_not_expire_by_default);
    RUN_TEST(test_holmes_hypothesis_confidence_decays_when_configured);
    RUN_TEST(test_holmes_hypothesis_expires_when_decay_configured);
    
    // Inactive
    RUN_TEST(test_holmes_ignores_when_inactive);
    
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_holmes_name);
    RUN_TEST(test_holmes_starts_with_no_hypotheses);
    RUN_TEST(test_holmes_creates_hypothesis_from_new_bssid);
    RUN_TEST(test_holmes_creates_hypothesis_from_security_downgrade);
    RUN_TEST(test_holmes_creates_hypothesis_from_deauth_flood);
    RUN_TEST(test_holmes_accumulates_evidence);
    RUN_TEST(test_holmes_confidence_increases_with_evidence);
    RUN_TEST(test_holmes_incorporates_findings);
    RUN_TEST(test_holmes_upgrades_to_pineapple_on_promiscuity);
    RUN_TEST(test_holmes_tracks_multiple_hypotheses);
    RUN_TEST(test_holmes_get_hypotheses_by_threat);
    RUN_TEST(test_holmes_hypothesis_confidence_holds_by_default);
    RUN_TEST(test_holmes_hypothesis_does_not_expire_by_default);
    RUN_TEST(test_holmes_hypothesis_confidence_decays_when_configured);
    RUN_TEST(test_holmes_hypothesis_expires_when_decay_configured);
    RUN_TEST(test_holmes_ignores_when_inactive);
    UNITY_END();
}
void loop() {}
#endif
