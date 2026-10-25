/**
 * @file test_baskerville.cpp
 * @brief Unit tests for Baskerville mode
 */

#include <unity.h>
#include "modes/baskerville/baskerville.h"
#include "core/hypothesis.h"
#include "utils/time_utils.h"

using namespace dupin;

static BaskervilleMode g_baskerville;

// Test BSSID
static const uint8_t TEST_BSSID[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};

void setUp(void) {
    setMockMillis(1000);
    g_baskerville = BaskervilleMode();
    g_baskerville.begin();
}

void tearDown(void) {
    g_baskerville.end();
}

// Helper to create observation from target
Observation makeTargetBeacon(int8_t rssi) {
    Observation obs;
    obs.frameType = FrameType::Beacon;
    obs.setSSID("RogueAP");
    macCopy(obs.bssid, TEST_BSSID);
    obs.rssi = rssi;
    obs.channel = 6;
    obs.timestamp = getTimestamp();
    return obs;
}

// ============================================
// Basic Functionality
// ============================================

void test_baskerville_name(void) {
    TEST_ASSERT_EQUAL_STRING("Baskerville", g_baskerville.name());
}

void test_baskerville_starts_not_hunting(void) {
    TEST_ASSERT_FALSE(g_baskerville.isHunting());
}

void test_baskerville_start_hunt_with_bssid(void) {
    bool result = g_baskerville.startHunt(TEST_BSSID, "TargetSSID");
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_baskerville.isHunting());
    
    const HuntStatus& status = g_baskerville.getStatus();
    TEST_ASSERT_TRUE(macEquals(status.targetBSSID, TEST_BSSID));
}

void test_baskerville_start_hunt_from_hypothesis(void) {
    Hypothesis hyp;
    hyp.id = 1;
    hyp.type = HypothesisType::EvilTwin;
    hyp.setSSID("EvilNetwork");
    macCopy(hyp.bssid, TEST_BSSID);
    
    bool result = g_baskerville.startHunt(hyp);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_baskerville.isHunting());
}

void test_baskerville_stop_hunt(void) {
    g_baskerville.startHunt(TEST_BSSID, "Target");
    TEST_ASSERT_TRUE(g_baskerville.isHunting());
    
    g_baskerville.stopHunt();
    
    TEST_ASSERT_FALSE(g_baskerville.isHunting());
}

// ============================================
// RSSI Tracking
// ============================================

void test_baskerville_tracks_rssi(void) {
    g_baskerville.startHunt(TEST_BSSID, "Target");
    
    g_baskerville.onObservation(makeTargetBeacon(-60));
    
    const HuntStatus& status = g_baskerville.getStatus();
    TEST_ASSERT_EQUAL(-60, status.currentRSSI);
    TEST_ASSERT_EQUAL(1, status.rssiCount);
}

void test_baskerville_tracks_peak_rssi(void) {
    g_baskerville.startHunt(TEST_BSSID, "Target");
    
    g_baskerville.onObservation(makeTargetBeacon(-60));
    advanceMockMillis(500);
    g_baskerville.onObservation(makeTargetBeacon(-45));  // Peak
    advanceMockMillis(500);
    g_baskerville.onObservation(makeTargetBeacon(-55));
    
    const HuntStatus& status = g_baskerville.getStatus();
    TEST_ASSERT_EQUAL(-45, status.peakRSSI);
}

// ============================================
// Trend Detection
// ============================================

void test_baskerville_detects_approaching(void) {
    g_baskerville.startHunt(TEST_BSSID, "Target");
    
    // Simulate increasing RSSI (approaching)
    for (int rssi = -70; rssi <= -50; rssi += 5) {
        g_baskerville.onObservation(makeTargetBeacon(rssi));
        advanceMockMillis(500);
    }
    
    const HuntStatus& status = g_baskerville.getStatus();
    TEST_ASSERT_EQUAL(HuntTrend::Approaching, status.trend);
    TEST_ASSERT_TRUE(status.rssiGradient > 0);
}

void test_baskerville_detects_receding(void) {
    g_baskerville.startHunt(TEST_BSSID, "Target");
    
    // Simulate decreasing RSSI (moving away)
    for (int rssi = -50; rssi >= -70; rssi -= 5) {
        g_baskerville.onObservation(makeTargetBeacon(rssi));
        advanceMockMillis(500);
    }
    
    const HuntStatus& status = g_baskerville.getStatus();
    TEST_ASSERT_EQUAL(HuntTrend::Receding, status.trend);
    TEST_ASSERT_TRUE(status.rssiGradient < 0);
}

void test_baskerville_detects_static(void) {
    g_baskerville.startHunt(TEST_BSSID, "Target");
    
    // Simulate stable RSSI
    for (int i = 0; i < 8; i++) {
        g_baskerville.onObservation(makeTargetBeacon(-55 + (i % 2)));  // Small variation
        advanceMockMillis(500);
    }
    
    const HuntStatus& status = g_baskerville.getStatus();
    TEST_ASSERT_EQUAL(HuntTrend::Static, status.trend);
}

// ============================================
// Signal Quality
// ============================================

void test_baskerville_signal_quality_excellent(void) {
    g_baskerville.startHunt(TEST_BSSID, "Target");
    g_baskerville.onObservation(makeTargetBeacon(-40));
    
    TEST_ASSERT_EQUAL(SignalQuality::Excellent, g_baskerville.getStatus().quality);
}

void test_baskerville_signal_quality_weak(void) {
    g_baskerville.startHunt(TEST_BSSID, "Target");
    g_baskerville.onObservation(makeTargetBeacon(-75));
    
    TEST_ASSERT_EQUAL(SignalQuality::Weak, g_baskerville.getStatus().quality);
}

// ============================================
// Lost Target Detection
// ============================================

void test_baskerville_detects_lost_target(void) {
    g_baskerville.startHunt(TEST_BSSID, "Target");
    g_baskerville.onObservation(makeTargetBeacon(-60));
    
    // Advance time past timeout
    advanceMockMillis(15000);
    g_baskerville.update();
    
    TEST_ASSERT_EQUAL(HuntTrend::Lost, g_baskerville.getStatus().trend);
}

// ============================================
// Hot/Cold Indicator
// ============================================

void test_baskerville_hot_cold_indicator_positive(void) {
    g_baskerville.startHunt(TEST_BSSID, "Target");
    
    // Simulate approaching
    for (int rssi = -70; rssi <= -50; rssi += 5) {
        g_baskerville.onObservation(makeTargetBeacon(rssi));
        advanceMockMillis(500);
    }
    
    int8_t indicator = g_baskerville.getHotColdIndicator();
    TEST_ASSERT_TRUE(indicator > 0);  // Positive = hot/approaching
}

void test_baskerville_hot_cold_indicator_negative(void) {
    g_baskerville.startHunt(TEST_BSSID, "Target");
    
    // Simulate receding
    for (int rssi = -50; rssi >= -70; rssi -= 5) {
        g_baskerville.onObservation(makeTargetBeacon(rssi));
        advanceMockMillis(500);
    }
    
    int8_t indicator = g_baskerville.getHotColdIndicator();
    TEST_ASSERT_TRUE(indicator < 0);  // Negative = cold/receding
}

// ============================================
// Ignores Non-Target
// ============================================

void test_baskerville_ignores_other_bssids(void) {
    g_baskerville.startHunt(TEST_BSSID, "Target");
    
    // Create observation from different BSSID
    Observation obs = makeTargetBeacon(-50);
    static const uint8_t OTHER_BSSID[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    macCopy(obs.bssid, OTHER_BSSID);
    
    g_baskerville.onObservation(obs);
    
    TEST_ASSERT_EQUAL(0, g_baskerville.getStatus().rssiCount);
}

// ============================================
// Test Runner
// ============================================

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Basic
    RUN_TEST(test_baskerville_name);
    RUN_TEST(test_baskerville_starts_not_hunting);
    RUN_TEST(test_baskerville_start_hunt_with_bssid);
    RUN_TEST(test_baskerville_start_hunt_from_hypothesis);
    RUN_TEST(test_baskerville_stop_hunt);
    
    // RSSI tracking
    RUN_TEST(test_baskerville_tracks_rssi);
    RUN_TEST(test_baskerville_tracks_peak_rssi);
    
    // Trend detection
    RUN_TEST(test_baskerville_detects_approaching);
    RUN_TEST(test_baskerville_detects_receding);
    RUN_TEST(test_baskerville_detects_static);
    
    // Signal quality
    RUN_TEST(test_baskerville_signal_quality_excellent);
    RUN_TEST(test_baskerville_signal_quality_weak);
    
    // Lost target
    RUN_TEST(test_baskerville_detects_lost_target);
    
    // Hot/cold indicator
    RUN_TEST(test_baskerville_hot_cold_indicator_positive);
    RUN_TEST(test_baskerville_hot_cold_indicator_negative);
    
    // Filtering
    RUN_TEST(test_baskerville_ignores_other_bssids);
    
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_baskerville_name);
    RUN_TEST(test_baskerville_starts_not_hunting);
    RUN_TEST(test_baskerville_start_hunt_with_bssid);
    RUN_TEST(test_baskerville_start_hunt_from_hypothesis);
    RUN_TEST(test_baskerville_stop_hunt);
    RUN_TEST(test_baskerville_tracks_rssi);
    RUN_TEST(test_baskerville_tracks_peak_rssi);
    RUN_TEST(test_baskerville_detects_approaching);
    RUN_TEST(test_baskerville_detects_receding);
    RUN_TEST(test_baskerville_detects_static);
    RUN_TEST(test_baskerville_signal_quality_excellent);
    RUN_TEST(test_baskerville_signal_quality_weak);
    RUN_TEST(test_baskerville_detects_lost_target);
    RUN_TEST(test_baskerville_hot_cold_indicator_positive);
    RUN_TEST(test_baskerville_hot_cold_indicator_negative);
    RUN_TEST(test_baskerville_ignores_other_bssids);
    UNITY_END();
}
void loop() {}
#endif
