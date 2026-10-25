/**
 * @file test_watson.cpp
 * @brief Unit tests for Watson mode
 */

#include <unity.h>
#include "modes/watson/watson.h"
#include "utils/time_utils.h"

using namespace dupin;

void setUp(void) {
    setMockMillis(0);
}

void tearDown(void) {}

// Helper to create a test observation
Observation makeBeacon(const char* ssid, const uint8_t* bssid, 
                       int8_t rssi = -50, uint8_t channel = 6) {
    Observation obs;
    obs.frameType = FrameType::Beacon;
    obs.setSSID(ssid);
    if (bssid) {
        macCopy(obs.bssid, bssid);
    }
    obs.rssi = rssi;
    obs.channel = channel;
    obs.timestamp = getTimestamp();
    return obs;
}

Observation makeProbeRequest(const char* ssid) {
    Observation obs;
    obs.frameType = FrameType::ProbeRequest;
    if (ssid) {
        obs.setSSID(ssid);
    }
    obs.timestamp = getTimestamp();
    return obs;
}

// Default test BSSID
static const uint8_t BSSID1[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
static const uint8_t BSSID2[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
static const uint8_t BSSID3[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

// ============================================
// Basic Functionality
// ============================================

void test_watson_name(void) {
    WatsonMode watson;
    
    TEST_ASSERT_EQUAL_STRING("Watson", watson.name());
}

void test_watson_starts_inactive(void) {
    WatsonMode watson;
    
    TEST_ASSERT_FALSE(watson.isActive());
}

void test_watson_begin_activates(void) {
    WatsonMode watson;
    
    watson.begin();
    
    TEST_ASSERT_TRUE(watson.isActive());
    
    watson.end();
}

void test_watson_end_deactivates(void) {
    WatsonMode watson;
    watson.begin();
    
    watson.end();
    
    TEST_ASSERT_FALSE(watson.isActive());
}

// ============================================
// Observation Processing
// ============================================

void test_watson_counts_observations(void) {
    WatsonMode watson;
    watson.begin();
    
    watson.onObservation(makeBeacon("Test1", BSSID1));
    watson.onObservation(makeBeacon("Test2", BSSID2));
    watson.onObservation(makeProbeRequest("Test3"));
    
    TEST_ASSERT_EQUAL(3, watson.getObservationCount());
    
    watson.end();
}

void test_watson_tracks_session_stats(void) {
    WatsonMode watson;
    watson.begin();
    
    watson.onObservation(makeBeacon("AP1", BSSID1));
    watson.onObservation(makeBeacon("AP2", BSSID2));
    watson.onObservation(makeProbeRequest(""));
    
    const auto& stats = watson.getSessionStats();
    
    TEST_ASSERT_EQUAL(3, stats.observations);
    TEST_ASSERT_EQUAL(2, stats.beacons);
    TEST_ASSERT_EQUAL(1, stats.probeRequests);
    
    watson.end();
}

void test_watson_stores_observations(void) {
    WatsonMode watson;
    watson.begin();
    
    watson.onObservation(makeBeacon("Test", BSSID1));
    
    TEST_ASSERT_EQUAL(1, watson.getStore().size());
    
    watson.end();
}

// ============================================
// Baseline Building
// ============================================

void test_watson_builds_baseline_from_beacons(void) {
    WatsonMode watson;
    watson.begin();
    
    watson.onObservation(makeBeacon("MyNetwork", BSSID1));
    
    const Baseline& baseline = watson.getBaseline();
    TEST_ASSERT_EQUAL(1, baseline.count());
    
    const SSIDBaseline* entry = baseline.find("MyNetwork");
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_STRING("MyNetwork", entry->ssid);
    
    watson.end();
}

void test_watson_tracks_multiple_bssids(void) {
    WatsonMode watson;
    watson.begin();
    
    watson.onObservation(makeBeacon("SharedSSID", BSSID1, -50, 1));
    watson.onObservation(makeBeacon("SharedSSID", BSSID2, -60, 6));
    watson.onObservation(makeBeacon("SharedSSID", BSSID3, -55, 11));
    
    const SSIDBaseline* entry = watson.getBaseline().find("SharedSSID");
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL(3, entry->bssidCount);
    TEST_ASSERT_EQUAL(3, entry->channelCount);
    
    watson.end();
}

void test_watson_tracks_rssi_range(void) {
    WatsonMode watson;
    watson.begin();
    
    watson.onObservation(makeBeacon("TestAP", BSSID1, -40, 6));
    watson.onObservation(makeBeacon("TestAP", BSSID1, -70, 6));
    watson.onObservation(makeBeacon("TestAP", BSSID1, -55, 6));
    
    const SSIDBaseline* entry = watson.getBaseline().find("TestAP");
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL(-40, entry->rssiMax);
    TEST_ASSERT_EQUAL(-70, entry->rssiMin);
    
    watson.end();
}

void test_watson_detects_new_ssids(void) {
    WatsonMode watson;
    watson.begin();
    
    watson.onObservation(makeBeacon("SSID1", BSSID1));
    watson.onObservation(makeBeacon("SSID2", BSSID2));
    
    TEST_ASSERT_EQUAL(2, watson.getSessionStats().newSSIDs);
    
    watson.end();
}

void test_watson_detects_new_bssid_for_known_ssid(void) {
    WatsonMode watson;
    watson.begin();
    
    // First BSSID
    watson.onObservation(makeBeacon("TestSSID", BSSID1));
    TEST_ASSERT_EQUAL(1, watson.getSessionStats().newSSIDs);
    TEST_ASSERT_EQUAL(0, watson.getSessionStats().newBSSIDs);
    
    // Second BSSID for same SSID
    watson.onObservation(makeBeacon("TestSSID", BSSID2));
    TEST_ASSERT_EQUAL(1, watson.getSessionStats().newSSIDs);  // Still 1
    TEST_ASSERT_EQUAL(1, watson.getSessionStats().newBSSIDs); // New BSSID detected
    
    watson.end();
}

void test_watson_ignores_observations_without_ssid(void) {
    WatsonMode watson;
    watson.begin();
    
    Observation obs;
    obs.frameType = FrameType::Data;
    // No SSID
    
    watson.onObservation(obs);
    
    TEST_ASSERT_EQUAL(0, watson.getBaseline().count());
    TEST_ASSERT_EQUAL(0, watson.getSessionStats().newSSIDs);
    
    watson.end();
}

// ============================================
// Configuration
// ============================================

void test_watson_config_defaults(void) {
    WatsonMode watson;
    
    const auto& config = watson.getConfig();
    
    TEST_ASSERT_EQUAL(30000, config.statsLogIntervalMs);
    TEST_ASSERT_TRUE(config.logNewSSIDs);
    TEST_ASSERT_TRUE(config.logNewBSSIDs);
}

void test_watson_config_can_be_changed(void) {
    WatsonMode watson;
    
    WatsonConfig config;
    config.statsLogIntervalMs = 120000;
    config.logNewSSIDs = false;
    
    watson.setConfig(config);
    
    TEST_ASSERT_EQUAL(120000, watson.getConfig().statsLogIntervalMs);
    TEST_ASSERT_FALSE(watson.getConfig().logNewSSIDs);
}

// ============================================
// Baseline Management
// ============================================

void test_watson_clear_baseline(void) {
    WatsonMode watson;
    watson.begin();
    
    watson.onObservation(makeBeacon("Test", BSSID1));
    TEST_ASSERT_EQUAL(1, watson.getBaseline().count());
    
    watson.clearBaseline();
    
    TEST_ASSERT_EQUAL(0, watson.getBaseline().count());
    
    watson.end();
}

void test_watson_reset_session_stats(void) {
    WatsonMode watson;
    watson.begin();
    
    watson.onObservation(makeBeacon("Test", BSSID1));
    TEST_ASSERT_EQUAL(1, watson.getSessionStats().observations);
    
    watson.resetSessionStats();
    
    TEST_ASSERT_EQUAL(0, watson.getSessionStats().observations);
    TEST_ASSERT_EQUAL(0, watson.getSessionStats().newSSIDs);
    
    watson.end();
}

// ============================================
// Inactive Mode
// ============================================

void test_watson_ignores_observations_when_inactive(void) {
    WatsonMode watson;
    // Note: NOT calling begin()
    
    watson.onObservation(makeBeacon("Test", BSSID1));
    
    TEST_ASSERT_EQUAL(0, watson.getObservationCount());
    TEST_ASSERT_EQUAL(0, watson.getBaseline().count());
}

// ============================================
// Test Runner
// ============================================

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Basic functionality
    RUN_TEST(test_watson_name);
    RUN_TEST(test_watson_starts_inactive);
    RUN_TEST(test_watson_begin_activates);
    RUN_TEST(test_watson_end_deactivates);
    
    // Observation processing
    RUN_TEST(test_watson_counts_observations);
    RUN_TEST(test_watson_tracks_session_stats);
    RUN_TEST(test_watson_stores_observations);
    
    // Baseline building
    RUN_TEST(test_watson_builds_baseline_from_beacons);
    RUN_TEST(test_watson_tracks_multiple_bssids);
    RUN_TEST(test_watson_tracks_rssi_range);
    RUN_TEST(test_watson_detects_new_ssids);
    RUN_TEST(test_watson_detects_new_bssid_for_known_ssid);
    RUN_TEST(test_watson_ignores_observations_without_ssid);
    
    // Configuration
    RUN_TEST(test_watson_config_defaults);
    RUN_TEST(test_watson_config_can_be_changed);
    
    // Baseline management
    RUN_TEST(test_watson_clear_baseline);
    RUN_TEST(test_watson_reset_session_stats);
    
    // Inactive mode
    RUN_TEST(test_watson_ignores_observations_when_inactive);
    
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_watson_name);
    RUN_TEST(test_watson_starts_inactive);
    RUN_TEST(test_watson_begin_activates);
    RUN_TEST(test_watson_end_deactivates);
    RUN_TEST(test_watson_counts_observations);
    RUN_TEST(test_watson_tracks_session_stats);
    RUN_TEST(test_watson_stores_observations);
    RUN_TEST(test_watson_builds_baseline_from_beacons);
    RUN_TEST(test_watson_tracks_multiple_bssids);
    RUN_TEST(test_watson_tracks_rssi_range);
    RUN_TEST(test_watson_detects_new_ssids);
    RUN_TEST(test_watson_detects_new_bssid_for_known_ssid);
    RUN_TEST(test_watson_ignores_observations_without_ssid);
    RUN_TEST(test_watson_config_defaults);
    RUN_TEST(test_watson_config_can_be_changed);
    RUN_TEST(test_watson_clear_baseline);
    RUN_TEST(test_watson_reset_session_stats);
    RUN_TEST(test_watson_ignores_observations_when_inactive);
    UNITY_END();
}
void loop() {}
#endif
