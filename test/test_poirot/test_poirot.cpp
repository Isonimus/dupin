/**
 * @file test_poirot.cpp
 * @brief Unit tests for Poirot mode
 */

#include <unity.h>
#include "modes/poirot/poirot.h"
#include "utils/time_utils.h"

using namespace dupin;

static PoirotMode g_poirot;

// Test BSSIDs
static const uint8_t TEST_BSSID[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};

void setUp(void) {
    setMockMillis(1000);
    g_poirot = PoirotMode();
    g_poirot.begin();
}

void tearDown(void) {
    g_poirot.end();
}

// Helper to create anomaly for investigation
Anomaly makeTestAnomaly(const char* ssid, const uint8_t* bssid) {
    Anomaly anomaly;
    anomaly.id = 1;
    anomaly.type = AnomalyType::NewBSSIDForSSID;
    anomaly.setSSID(ssid);
    if (bssid) macCopy(anomaly.bssid, bssid);
    anomaly.confidence = 0.8f;
    anomaly.severity = Severity::High;
    anomaly.firstSeen = getTimestamp();
    return anomaly;
}

// Helper to create beacon observation
Observation makeBeacon(const char* ssid, const uint8_t* bssid,
                       int8_t rssi = -50, uint8_t channel = 6) {
    Observation obs;
    obs.frameType = FrameType::Beacon;
    obs.setSSID(ssid);
    if (bssid) macCopy(obs.bssid, bssid);
    obs.rssi = rssi;
    obs.channel = channel;
    obs.timestamp = getTimestamp();
    obs.beaconInterval = 100;
    return obs;
}

// Helper to create probe response
Observation makeProbeResponse(const char* ssid, const uint8_t* bssid) {
    Observation obs;
    obs.frameType = FrameType::ProbeResponse;
    obs.setSSID(ssid);
    if (bssid) macCopy(obs.bssid, bssid);
    obs.rssi = -55;
    obs.channel = 6;
    obs.timestamp = getTimestamp();
    return obs;
}

// ============================================
// Basic Functionality
// ============================================

void test_poirot_name(void) {
    TEST_ASSERT_EQUAL_STRING("Poirot", g_poirot.name());
}

void test_poirot_starts_with_no_investigations(void) {
    TEST_ASSERT_EQUAL(0, g_poirot.getInvestigationCount());
    TEST_ASSERT_EQUAL(0, g_poirot.getFindingCount());
}

void test_poirot_starts_investigation(void) {
    Anomaly anomaly = makeTestAnomaly("TestNetwork", TEST_BSSID);
    
    bool result = g_poirot.investigate(anomaly);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1, g_poirot.getInvestigationCount());
}

void test_poirot_doesnt_duplicate_investigation(void) {
    Anomaly anomaly = makeTestAnomaly("TestNetwork", TEST_BSSID);
    
    g_poirot.investigate(anomaly);
    g_poirot.investigate(anomaly);  // Same anomaly again
    
    TEST_ASSERT_EQUAL(1, g_poirot.getInvestigationCount());
}

// ============================================
// Movement Detection
// ============================================

void test_poirot_detects_movement_approaching(void) {
    Anomaly anomaly = makeTestAnomaly("TestNetwork", TEST_BSSID);
    g_poirot.investigate(anomaly);
    
    PoirotConfig config = g_poirot.getConfig();
    config.movementRSSIThreshold = 5;
    g_poirot.setConfig(config);
    
    // Simulate RSSI increasing (approaching)
    g_poirot.onObservation(makeBeacon("TestNetwork", TEST_BSSID, -60, 6));
    advanceMockMillis(100);
    g_poirot.onObservation(makeBeacon("TestNetwork", TEST_BSSID, -55, 6));
    advanceMockMillis(100);
    g_poirot.onObservation(makeBeacon("TestNetwork", TEST_BSSID, -50, 6));
    advanceMockMillis(100);
    g_poirot.onObservation(makeBeacon("TestNetwork", TEST_BSSID, -45, 6));
    
    TEST_ASSERT_EQUAL(1, g_poirot.getFindingCount());
    
    // Verify finding details
    const Finding* findings = g_poirot.getFindings();
    bool found = false;
    for (size_t i = 0; i < MAX_FINDINGS; i++) {
        if (findings[i].type == FindingType::BSSIDMovement) {
            found = true;
            TEST_ASSERT_EQUAL(FindingStrength::Moderate, findings[i].strength);
            TEST_ASSERT_TRUE(findings[i].metrics.movement.gradient > 0);
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

void test_poirot_detects_movement_receding(void) {
    Anomaly anomaly = makeTestAnomaly("TestNetwork", TEST_BSSID);
    g_poirot.investigate(anomaly);
    
    PoirotConfig config = g_poirot.getConfig();
    config.movementRSSIThreshold = 5;
    g_poirot.setConfig(config);
    
    // Simulate RSSI decreasing (moving away)
    g_poirot.onObservation(makeBeacon("TestNetwork", TEST_BSSID, -45, 6));
    advanceMockMillis(100);
    g_poirot.onObservation(makeBeacon("TestNetwork", TEST_BSSID, -50, 6));
    advanceMockMillis(100);
    g_poirot.onObservation(makeBeacon("TestNetwork", TEST_BSSID, -55, 6));
    advanceMockMillis(100);
    g_poirot.onObservation(makeBeacon("TestNetwork", TEST_BSSID, -60, 6));
    
    TEST_ASSERT_EQUAL(1, g_poirot.getFindingCount());
    
    const Finding* findings = g_poirot.getFindings();
    bool found = false;
    for (size_t i = 0; i < MAX_FINDINGS; i++) {
        if (findings[i].type == FindingType::BSSIDMovement) {
            found = true;
            TEST_ASSERT_EQUAL(FindingStrength::Weak, findings[i].strength);
            TEST_ASSERT_TRUE(findings[i].metrics.movement.gradient < 0);
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

// ============================================
// Channel Hopping Detection
// ============================================

void test_poirot_detects_channel_hopping(void) {
    Anomaly anomaly = makeTestAnomaly("TestNetwork", TEST_BSSID);
    anomaly.type = AnomalyType::UnexpectedChannel;
    g_poirot.investigate(anomaly);
    
    PoirotConfig config = g_poirot.getConfig();
    config.channelHopThreshold = 3;
    g_poirot.setConfig(config);
    
    // Same AP on multiple channels
    g_poirot.onObservation(makeBeacon("TestNetwork", TEST_BSSID, -50, 1));
    advanceMockMillis(100);
    g_poirot.onObservation(makeBeacon("TestNetwork", TEST_BSSID, -50, 6));
    advanceMockMillis(100);
    g_poirot.onObservation(makeBeacon("TestNetwork", TEST_BSSID, -50, 11));
    
    TEST_ASSERT_EQUAL(1, g_poirot.getFindingCount());
    
    const Finding* findings = g_poirot.getFindings();
    bool found = false;
    for (size_t i = 0; i < MAX_FINDINGS; i++) {
        if (findings[i].type == FindingType::ChannelHopping) {
            found = true;
            TEST_ASSERT_EQUAL(FindingStrength::Strong, findings[i].strength);
            TEST_ASSERT_EQUAL(3, findings[i].metrics.channelHop.channelChanges);
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

// ============================================
// Beacon Personality Detection
// ============================================

void test_poirot_detects_unusual_beacon_interval(void) {
    Anomaly anomaly = makeTestAnomaly("TestNetwork", TEST_BSSID);
    anomaly.type = AnomalyType::SecurityDowngrade;
    g_poirot.investigate(anomaly);
    
    // Non-standard beacon interval
    Observation obs = makeBeacon("TestNetwork", TEST_BSSID, -50, 6);
    obs.beaconInterval = 50;  // Non-standard (should be 100)
    
    g_poirot.onObservation(obs);
    
    TEST_ASSERT_EQUAL(1, g_poirot.getFindingCount());
    
    const Finding* findings = g_poirot.getFindings();
    bool found = false;
    for (size_t i = 0; i < MAX_FINDINGS; i++) {
        if (findings[i].type == FindingType::TimingAnomaly) {
            found = true;
            TEST_ASSERT_EQUAL(100, findings[i].metrics.timing.expectedInterval);
            TEST_ASSERT_EQUAL(50, findings[i].metrics.timing.actualInterval);
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

// ============================================
// Probe Promiscuity Detection
// ============================================

void test_poirot_detects_probe_promiscuity(void) {
    Anomaly anomaly = makeTestAnomaly("TestNetwork", TEST_BSSID);
    g_poirot.investigate(anomaly);
    
    PoirotConfig config = g_poirot.getConfig();
    config.probePromiscuityThreshold = 3;
    g_poirot.setConfig(config);
    
    // Multiple probe responses
    for (int i = 0; i < 4; i++) {
        g_poirot.onObservation(makeProbeResponse("TestNetwork", TEST_BSSID));
        advanceMockMillis(100);
    }
    
    TEST_ASSERT_EQUAL(1, g_poirot.getFindingCount());
    
    const Finding* findings = g_poirot.getFindings();
    bool found = false;
    for (size_t i = 0; i < MAX_FINDINGS; i++) {
        if (findings[i].type == FindingType::ProbePromiscuity) {
            found = true;
            TEST_ASSERT_EQUAL(FindingStrength::Moderate, findings[i].strength);
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

// ============================================
// Investigation Expiration
// ============================================

void test_poirot_investigation_expires(void) {
    Anomaly anomaly = makeTestAnomaly("TestNetwork", TEST_BSSID);
    g_poirot.investigate(anomaly);
    
    TEST_ASSERT_EQUAL(1, g_poirot.getInvestigationCount());
    
    // Advance time past timeout (5 minutes default)
    advanceMockMillis(400000);  // ~6.7 minutes
    g_poirot.update();
    
    TEST_ASSERT_EQUAL(0, g_poirot.getInvestigationCount());
}

// ============================================
// Findings Per Anomaly
// ============================================

void test_poirot_get_findings_for_anomaly(void) {
    Anomaly anomaly1 = makeTestAnomaly("Network1", TEST_BSSID);
    anomaly1.id = 1;
    g_poirot.investigate(anomaly1);
    
    static const uint8_t BSSID2[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    Anomaly anomaly2 = makeTestAnomaly("Network2", BSSID2);
    anomaly2.id = 2;
    g_poirot.investigate(anomaly2);
    
    PoirotConfig config = g_poirot.getConfig();
    config.movementRSSIThreshold = 5;
    g_poirot.setConfig(config);
    
    // Generate findings for anomaly 1
    g_poirot.onObservation(makeBeacon("Network1", TEST_BSSID, -60, 6));
    advanceMockMillis(100);
    g_poirot.onObservation(makeBeacon("Network1", TEST_BSSID, -50, 6));
    advanceMockMillis(100);
    g_poirot.onObservation(makeBeacon("Network1", TEST_BSSID, -40, 6));
    advanceMockMillis(100);
    g_poirot.onObservation(makeBeacon("Network1", TEST_BSSID, -30, 6));
    
    // Get findings for anomaly 1
    Finding results[5];
    size_t count = g_poirot.getFindingsForAnomaly(1, results, 5);
    
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(1, results[0].anomalyId);
}

// ============================================
// Inactive Mode
// ============================================

void test_poirot_ignores_when_inactive(void) {
    g_poirot.end();  // Deactivate
    
    Anomaly anomaly = makeTestAnomaly("TestNetwork", TEST_BSSID);
    bool result = g_poirot.investigate(anomaly);
    
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL(0, g_poirot.getInvestigationCount());
}

// ============================================
// Test Runner
// ============================================

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Basic functionality
    RUN_TEST(test_poirot_name);
    RUN_TEST(test_poirot_starts_with_no_investigations);
    RUN_TEST(test_poirot_starts_investigation);
    RUN_TEST(test_poirot_doesnt_duplicate_investigation);
    
    // Movement detection
    RUN_TEST(test_poirot_detects_movement_approaching);
    RUN_TEST(test_poirot_detects_movement_receding);
    
    // Channel hopping
    RUN_TEST(test_poirot_detects_channel_hopping);
    
    // Beacon personality
    RUN_TEST(test_poirot_detects_unusual_beacon_interval);
    
    // Probe promiscuity
    RUN_TEST(test_poirot_detects_probe_promiscuity);
    
    // Expiration
    RUN_TEST(test_poirot_investigation_expires);
    
    // Findings per anomaly
    RUN_TEST(test_poirot_get_findings_for_anomaly);
    
    // Inactive mode
    RUN_TEST(test_poirot_ignores_when_inactive);
    
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_poirot_name);
    RUN_TEST(test_poirot_starts_with_no_investigations);
    RUN_TEST(test_poirot_starts_investigation);
    RUN_TEST(test_poirot_doesnt_duplicate_investigation);
    RUN_TEST(test_poirot_detects_movement_approaching);
    RUN_TEST(test_poirot_detects_movement_receding);
    RUN_TEST(test_poirot_detects_channel_hopping);
    RUN_TEST(test_poirot_detects_unusual_beacon_interval);
    RUN_TEST(test_poirot_detects_probe_promiscuity);
    RUN_TEST(test_poirot_investigation_expires);
    RUN_TEST(test_poirot_get_findings_for_anomaly);
    RUN_TEST(test_poirot_ignores_when_inactive);
    UNITY_END();
}
void loop() {}
#endif
