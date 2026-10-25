/**
 * @file test_marple.cpp
 * @brief Unit tests for Marple mode
 */

#include <unity.h>
#include "modes/marple/marple.h"
#include "utils/time_utils.h"

using namespace dupin;

// Test fixtures
static Baseline g_baseline;
static MarpleMode g_marple;

// Test BSSIDs
static const uint8_t KNOWN_BSSID[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
static const uint8_t NEW_BSSID[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

void setUp(void) {
    setMockMillis(1000);
    
    // Set up a baseline with a known network
    g_baseline.clear();
    SSIDBaseline* entry = g_baseline.getOrCreate("TestNetwork");
    entry->addBSSID(KNOWN_BSSID);
    entry->addChannel(6);
    entry->updateRSSI(-50);
    entry->updateRSSI(-60);
    entry->security.type = SecurityType::WPA2;
    
    // Reset Marple
    g_marple = MarpleMode();
    g_marple.setBaseline(&g_baseline);
    g_marple.begin();
}

void tearDown(void) {
    g_marple.end();
}

// Helper to create observations
Observation makeBeacon(const char* ssid, const uint8_t* bssid,
                       int8_t rssi = -55, uint8_t channel = 6) {
    Observation obs;
    obs.frameType = FrameType::Beacon;
    obs.setSSID(ssid);
    if (bssid) macCopy(obs.bssid, bssid);
    obs.rssi = rssi;
    obs.channel = channel;
    obs.timestamp = getTimestamp();
    obs.security.type = SecurityType::WPA2;
    return obs;
}

Observation makeDeauth() {
    Observation obs;
    obs.frameType = FrameType::Deauthentication;
    obs.timestamp = getTimestamp();
    return obs;
}

// ============================================
// Basic Functionality
// ============================================

void test_marple_name(void) {
    TEST_ASSERT_EQUAL_STRING("Marple", g_marple.name());
}

void test_marple_starts_with_no_anomalies(void) {
    TEST_ASSERT_EQUAL(0, g_marple.getAnomalyCount());
}

// ============================================
// New BSSID Detection
// ============================================

void test_marple_detects_new_bssid(void) {
    // Send beacon with new BSSID for known SSID
    Observation obs = makeBeacon("TestNetwork", NEW_BSSID);
    g_marple.onObservation(obs);
    
    TEST_ASSERT_EQUAL(1, g_marple.getAnomalyCount());
    TEST_ASSERT_EQUAL(1, g_marple.getDetectionStats().newBSSIDs);
    
    // Verify anomaly details
    const Anomaly* anomalies = g_marple.getAnomalies();
    bool found = false;
    for (size_t i = 0; i < MAX_ANOMALIES; i++) {
        if (anomalies[i].type == AnomalyType::NewBSSIDForSSID) {
            found = true;
            TEST_ASSERT_EQUAL_STRING("TestNetwork", anomalies[i].ssid);
            TEST_ASSERT_TRUE(macEquals(anomalies[i].bssid, NEW_BSSID));
            TEST_ASSERT_EQUAL(Severity::High, anomalies[i].severity);
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

void test_marple_ignores_known_bssid(void) {
    // Send beacon with known BSSID
    Observation obs = makeBeacon("TestNetwork", KNOWN_BSSID);
    g_marple.onObservation(obs);
    
    TEST_ASSERT_EQUAL(0, g_marple.getAnomalyCount());
}

void test_marple_ignores_unknown_ssid(void) {
    // SSID not in baseline - can't detect "new" BSSID
    Observation obs = makeBeacon("UnknownNetwork", NEW_BSSID);
    g_marple.onObservation(obs);
    
    // No new BSSID anomaly (SSID isn't baselined)
    TEST_ASSERT_EQUAL(0, g_marple.getDetectionStats().newBSSIDs);
}

// ============================================
// Unexpected Channel Detection
// ============================================

void test_marple_detects_unexpected_channel(void) {
    // Known network on unexpected channel
    Observation obs = makeBeacon("TestNetwork", KNOWN_BSSID, -55, 11);  // Channel 11
    g_marple.onObservation(obs);
    
    TEST_ASSERT_EQUAL(1, g_marple.getDetectionStats().unexpectedChannels);
    
    const Anomaly* anomalies = g_marple.getAnomalies();
    bool found = false;
    for (size_t i = 0; i < MAX_ANOMALIES; i++) {
        if (anomalies[i].type == AnomalyType::UnexpectedChannel) {
            found = true;
            TEST_ASSERT_EQUAL(11, anomalies[i].channel);
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

void test_marple_ignores_expected_channel(void) {
    // Known network on expected channel
    Observation obs = makeBeacon("TestNetwork", KNOWN_BSSID, -55, 6);  // Channel 6 is known
    g_marple.onObservation(obs);
    
    TEST_ASSERT_EQUAL(0, g_marple.getDetectionStats().unexpectedChannels);
}

// ============================================
// Security Downgrade Detection
// ============================================

void test_marple_detects_security_downgrade(void) {
    // Known WPA2 network suddenly shows as Open
    Observation obs = makeBeacon("TestNetwork", KNOWN_BSSID);
    obs.security.type = SecurityType::Open;
    
    g_marple.onObservation(obs);
    
    TEST_ASSERT_EQUAL(1, g_marple.getDetectionStats().securityDowngrades);
    
    const Anomaly* anomalies = g_marple.getAnomalies();
    bool found = false;
    for (size_t i = 0; i < MAX_ANOMALIES; i++) {
        if (anomalies[i].type == AnomalyType::SecurityDowngrade) {
            found = true;
            TEST_ASSERT_EQUAL(Severity::Critical, anomalies[i].severity);
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

void test_marple_ignores_same_security(void) {
    // Same security as baseline
    Observation obs = makeBeacon("TestNetwork", KNOWN_BSSID);
    obs.security.type = SecurityType::WPA2;
    
    g_marple.onObservation(obs);
    
    TEST_ASSERT_EQUAL(0, g_marple.getDetectionStats().securityDowngrades);
}

// ============================================
// Excessive Deauth Detection
// ============================================

void test_marple_detects_excessive_deauths(void) {
    MarpleConfig config = g_marple.getConfig();
    config.deauthThreshold = 5;  // Low threshold for testing
    g_marple.setConfig(config);
    
    // Send many deauths
    for (int i = 0; i < 6; i++) {
        g_marple.onObservation(makeDeauth());
    }
    
    TEST_ASSERT_EQUAL(1, g_marple.getDetectionStats().excessiveDeauths);
}

void test_marple_ignores_few_deauths(void) {
    // Default threshold is 10, send fewer
    for (int i = 0; i < 5; i++) {
        g_marple.onObservation(makeDeauth());
    }
    
    TEST_ASSERT_EQUAL(0, g_marple.getDetectionStats().excessiveDeauths);
}

// ============================================
// Confidence Decay
// ============================================

// Confidence of the NewBSSIDForSSID anomaly Marple raised, or 0 if it is gone.
// Four decay tests below read it; the anomaly array is exposed by reference, so the
// value is re-read after every update() rather than cached.
static float newBssidConfidence() {
    const Anomaly* anomalies = g_marple.getAnomalies();
    for (size_t i = 0; i < MAX_ANOMALIES; i++) {
        if (anomalies[i].isActive() && anomalies[i].type == AnomalyType::NewBSSIDForSSID) {
            return anomalies[i].confidence;
        }
    }
    return 0.0f;
}

// A decay rate large enough that expiry is reached in a handful of steps, so the
// expiry tests below stay bounded and their step count is arithmetic, not a guess.
static constexpr float TEST_DECAY_RATE = 0.1f;

// From confidence <= 1.0 at TEST_DECAY_RATE per step, minConfidence (0.1) is crossed in
// at most 10 steps; 20 leaves headroom without hiding a decay that stopped working.
static constexpr int DECAY_STEPS_TO_EXPIRY = 20;

static void enableDecay(float ratePerStep) {
    MarpleConfig config = g_marple.getConfig();
    config.confidenceDecayRate = ratePerStep;
    g_marple.setConfig(config);
}

// Automatic decay is OFF by default: an anomaly stands until the operator dismisses it
// (ADR-0003). These two tests are the guard on that default — re-enabling decay in
// marple.h without deciding to would turn them red.
void test_marple_anomaly_confidence_holds_by_default(void) {
    g_marple.onObservation(makeBeacon("TestNetwork", NEW_BSSID));

    float initialConfidence = newBssidConfidence();
    TEST_ASSERT_TRUE(initialConfidence > 0);

    advanceMockMillis(5000);
    g_marple.update();

    TEST_ASSERT_EQUAL_FLOAT(initialConfidence, newBssidConfidence());
}

void test_marple_anomaly_does_not_expire_by_default(void) {
    g_marple.onObservation(makeBeacon("TestNetwork", NEW_BSSID));
    TEST_ASSERT_EQUAL(1, g_marple.getAnomalyCount());

    for (int i = 0; i < 100; i++) {
        advanceMockMillis(1000);
        g_marple.update();
    }

    TEST_ASSERT_EQUAL(1, g_marple.getAnomalyCount());
}

// The decay mechanism itself still works and is still configurable; only its default
// rate is zero. Without these two, setting the rate back to zero everywhere would be
// indistinguishable from deleting decayConfidence() altogether.
void test_marple_anomaly_confidence_decays_when_configured(void) {
    enableDecay(TEST_DECAY_RATE);
    g_marple.onObservation(makeBeacon("TestNetwork", NEW_BSSID));

    float initialConfidence = newBssidConfidence();
    TEST_ASSERT_TRUE(initialConfidence > TEST_DECAY_RATE);

    // update() decays once per elapsed second, not once per call.
    advanceMockMillis(1000);
    g_marple.update();

    TEST_ASSERT_FLOAT_WITHIN(0.001f, initialConfidence - TEST_DECAY_RATE, newBssidConfidence());
}

void test_marple_anomaly_expires_when_decay_configured(void) {
    enableDecay(TEST_DECAY_RATE);
    g_marple.onObservation(makeBeacon("TestNetwork", NEW_BSSID));
    TEST_ASSERT_EQUAL(1, g_marple.getAnomalyCount());

    for (int i = 0; i < DECAY_STEPS_TO_EXPIRY; i++) {
        advanceMockMillis(1000);
        g_marple.update();
    }

    TEST_ASSERT_EQUAL(0, g_marple.getAnomalyCount());
}

// ============================================
// Repeated Anomalies
// ============================================

void test_marple_repeated_anomaly_boosts_confidence(void) {
    // Create anomaly
    Observation obs1 = makeBeacon("TestNetwork", NEW_BSSID);
    g_marple.onObservation(obs1);
    
    float conf1 = 0;
    const Anomaly* anomalies = g_marple.getAnomalies();
    for (size_t i = 0; i < MAX_ANOMALIES; i++) {
        if (anomalies[i].isActive()) {
            conf1 = anomalies[i].confidence;
            break;
        }
    }
    
    // See it again
    advanceMockMillis(100);
    Observation obs2 = makeBeacon("TestNetwork", NEW_BSSID);
    g_marple.onObservation(obs2);
    
    float conf2 = 0;
    for (size_t i = 0; i < MAX_ANOMALIES; i++) {
        if (anomalies[i].isActive()) {
            conf2 = anomalies[i].confidence;
            break;
        }
    }
    
    TEST_ASSERT_TRUE(conf2 >= conf1);
    
    // Still only 1 anomaly (not duplicated)
    TEST_ASSERT_EQUAL(1, g_marple.getAnomalyCount());
}

// ============================================
// Configuration
// ============================================

void test_marple_detection_can_be_disabled(void) {
    MarpleConfig config = g_marple.getConfig();
    config.detectNewBSSID = false;
    g_marple.setConfig(config);
    
    Observation obs = makeBeacon("TestNetwork", NEW_BSSID);
    g_marple.onObservation(obs);
    
    TEST_ASSERT_EQUAL(0, g_marple.getAnomalyCount());
}

// ============================================
// No Baseline
// ============================================

void test_marple_works_without_baseline(void) {
    g_marple.setBaseline(nullptr);
    
    // Should still detect deauth floods
    MarpleConfig config = g_marple.getConfig();
    config.deauthThreshold = 3;
    g_marple.setConfig(config);
    
    for (int i = 0; i < 4; i++) {
        g_marple.onObservation(makeDeauth());
    }
    
    TEST_ASSERT_EQUAL(1, g_marple.getDetectionStats().excessiveDeauths);
}

// ============================================
// Test Runner
// ============================================

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Basic
    RUN_TEST(test_marple_name);
    RUN_TEST(test_marple_starts_with_no_anomalies);
    
    // New BSSID
    RUN_TEST(test_marple_detects_new_bssid);
    RUN_TEST(test_marple_ignores_known_bssid);
    RUN_TEST(test_marple_ignores_unknown_ssid);
    
    // Unexpected channel
    RUN_TEST(test_marple_detects_unexpected_channel);
    RUN_TEST(test_marple_ignores_expected_channel);
    
    // Security downgrade
    RUN_TEST(test_marple_detects_security_downgrade);
    RUN_TEST(test_marple_ignores_same_security);
    
    // Excessive deauths
    RUN_TEST(test_marple_detects_excessive_deauths);
    RUN_TEST(test_marple_ignores_few_deauths);
    
    // Confidence decay
    RUN_TEST(test_marple_anomaly_confidence_holds_by_default);
    RUN_TEST(test_marple_anomaly_does_not_expire_by_default);
    RUN_TEST(test_marple_anomaly_confidence_decays_when_configured);
    RUN_TEST(test_marple_anomaly_expires_when_decay_configured);
    
    // Repeated anomalies
    RUN_TEST(test_marple_repeated_anomaly_boosts_confidence);
    
    // Configuration
    RUN_TEST(test_marple_detection_can_be_disabled);
    
    // No baseline
    RUN_TEST(test_marple_works_without_baseline);
    
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_marple_name);
    RUN_TEST(test_marple_starts_with_no_anomalies);
    RUN_TEST(test_marple_detects_new_bssid);
    RUN_TEST(test_marple_ignores_known_bssid);
    RUN_TEST(test_marple_ignores_unknown_ssid);
    RUN_TEST(test_marple_detects_unexpected_channel);
    RUN_TEST(test_marple_ignores_expected_channel);
    RUN_TEST(test_marple_detects_security_downgrade);
    RUN_TEST(test_marple_ignores_same_security);
    RUN_TEST(test_marple_detects_excessive_deauths);
    RUN_TEST(test_marple_ignores_few_deauths);
    RUN_TEST(test_marple_anomaly_confidence_holds_by_default);
    RUN_TEST(test_marple_anomaly_does_not_expire_by_default);
    RUN_TEST(test_marple_anomaly_confidence_decays_when_configured);
    RUN_TEST(test_marple_anomaly_expires_when_decay_configured);
    RUN_TEST(test_marple_repeated_anomaly_boosts_confidence);
    RUN_TEST(test_marple_detection_can_be_disabled);
    RUN_TEST(test_marple_works_without_baseline);
    UNITY_END();
}
void loop() {}
#endif
