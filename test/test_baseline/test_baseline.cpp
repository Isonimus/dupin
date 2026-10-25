/**
 * @file test_baseline.cpp
 * @brief Unit tests for Baseline structures
 */

#include <unity.h>
#include "core/baseline.h"

using namespace dupin;

void setUp(void) {}
void tearDown(void) {}

// ============================================
// SSIDBaseline Tests
// ============================================

void test_ssid_baseline_starts_inactive(void) {
    SSIDBaseline entry;
    
    TEST_ASSERT_FALSE(entry.isActive());
    TEST_ASSERT_EQUAL(0, entry.bssidCount);
    TEST_ASSERT_EQUAL(0, entry.channelCount);
}

void test_ssid_baseline_set_ssid_activates(void) {
    SSIDBaseline entry;
    
    entry.setSSID("TestNetwork");
    
    TEST_ASSERT_TRUE(entry.isActive());
    TEST_ASSERT_EQUAL_STRING("TestNetwork", entry.ssid);
}

void test_ssid_baseline_add_bssid(void) {
    SSIDBaseline entry;
    entry.setSSID("Test");
    
    uint8_t bssid1[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t bssid2[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    
    TEST_ASSERT_TRUE(entry.addBSSID(bssid1));
    TEST_ASSERT_TRUE(entry.addBSSID(bssid2));
    TEST_ASSERT_EQUAL(2, entry.bssidCount);
    
    TEST_ASSERT_TRUE(entry.hasBSSID(bssid1));
    TEST_ASSERT_TRUE(entry.hasBSSID(bssid2));
}

void test_ssid_baseline_add_bssid_duplicate(void) {
    SSIDBaseline entry;
    entry.setSSID("Test");
    
    uint8_t bssid[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    TEST_ASSERT_TRUE(entry.addBSSID(bssid));
    TEST_ASSERT_FALSE(entry.addBSSID(bssid));  // Duplicate
    TEST_ASSERT_EQUAL(1, entry.bssidCount);
}

void test_ssid_baseline_add_channel(void) {
    SSIDBaseline entry;
    entry.setSSID("Test");
    
    TEST_ASSERT_TRUE(entry.addChannel(1));
    TEST_ASSERT_TRUE(entry.addChannel(6));
    TEST_ASSERT_TRUE(entry.addChannel(11));
    TEST_ASSERT_EQUAL(3, entry.channelCount);
    
    TEST_ASSERT_TRUE(entry.hasChannel(6));
    TEST_ASSERT_FALSE(entry.hasChannel(2));
}

void test_ssid_baseline_rssi_stats(void) {
    SSIDBaseline entry;
    entry.setSSID("Test");
    
    entry.updateRSSI(-50);
    entry.updateRSSI(-60);
    entry.updateRSSI(-40);
    
    TEST_ASSERT_EQUAL(-40, entry.rssiMax);
    TEST_ASSERT_EQUAL(-60, entry.rssiMin);
    TEST_ASSERT_EQUAL(-50, entry.rssiAverage());
}

// A sum of int8 samples first leaves 32 bits one observation past 2^31 / 128, at the -128
// floor. The field is persisted, so the count runs across a case's whole life (ADR-0029).
void test_ssid_baseline_rssi_average_survives_a_sum_past_32_bits(void) {
    const uint32_t kObservationsPastInt32AtFloor = (UINT32_C(1) << 24) + 1;
    SSIDBaseline entry;
    entry.setSSID("Test");

    for (uint32_t i = 0; i < kObservationsPastInt32AtFloor; i++) entry.updateRSSI(-128);

    TEST_ASSERT_EQUAL(-128, entry.rssiAverage());
}

// Past 2^31 observations the count used to be cast to a negative divisor (ADR-0029).
void test_ssid_baseline_rssi_average_divides_by_a_count_past_2_31(void) {
    const uint32_t kCountPastInt32 = UINT32_C(3000000000);
    const int8_t kSample = -70;
    SSIDBaseline entry;
    entry.setSSID("Test");
    entry.rssiCount = kCountPastInt32;
    entry.rssiSum = static_cast<int64_t>(kSample) * kCountPastInt32;

    TEST_ASSERT_EQUAL(kSample, entry.rssiAverage());
}

void test_ssid_baseline_rssi_in_range(void) {
    SSIDBaseline entry;
    entry.setSSID("Test");
    
    entry.updateRSSI(-50);
    entry.updateRSSI(-60);
    // Range is now -60 to -50, tolerance is 20
    // So valid range is -80 to -30
    
    TEST_ASSERT_TRUE(entry.isRSSIInRange(-55));    // Within actual range
    TEST_ASSERT_TRUE(entry.isRSSIInRange(-45));    // Slightly above max, within tolerance
    TEST_ASSERT_TRUE(entry.isRSSIInRange(-75));    // Slightly below min, within tolerance
    TEST_ASSERT_FALSE(entry.isRSSIInRange(-20));   // Too strong - clearly above tolerance
    TEST_ASSERT_FALSE(entry.isRSSIInRange(-90));   // Too weak - clearly below tolerance
}

void test_ssid_baseline_update_from_observation(void) {
    SSIDBaseline entry;
    
    Observation obs;
    obs.timestamp = 12345;
    obs.setSSID("MyNetwork");
    obs.channel = 6;
    obs.rssi = -45;
    obs.frameType = FrameType::Beacon;
    uint8_t bssid[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    macCopy(obs.bssid, bssid);
    
    entry.updateFromObservation(obs);
    
    TEST_ASSERT_TRUE(entry.isActive());
    TEST_ASSERT_EQUAL_STRING("MyNetwork", entry.ssid);
    TEST_ASSERT_EQUAL(1, entry.bssidCount);
    TEST_ASSERT_EQUAL(1, entry.channelCount);
    TEST_ASSERT_EQUAL(1, entry.beaconCount);
    TEST_ASSERT_EQUAL(12345, entry.firstSeen);
    TEST_ASSERT_EQUAL(12345, entry.lastSeen);
}

void test_ssid_baseline_clear(void) {
    SSIDBaseline entry;
    entry.setSSID("Test");
    entry.addChannel(6);
    
    entry.clear();
    
    TEST_ASSERT_FALSE(entry.isActive());
    TEST_ASSERT_EQUAL(0, entry.channelCount);
    TEST_ASSERT_EQUAL(0, entry.beaconCount);
}

// ============================================
// Baseline Container Tests
// ============================================

void test_baseline_starts_empty(void) {
    Baseline baseline;
    
    TEST_ASSERT_EQUAL(0, baseline.count());
    TEST_ASSERT_NULL(baseline.find("Unknown"));
}

void test_baseline_get_or_create(void) {
    Baseline baseline;
    
    SSIDBaseline* entry = baseline.getOrCreate("Network1");
    
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_STRING("Network1", entry->ssid);
    TEST_ASSERT_EQUAL(1, baseline.count());
}

void test_baseline_get_or_create_returns_existing(void) {
    Baseline baseline;
    
    SSIDBaseline* entry1 = baseline.getOrCreate("Network1");
    entry1->addChannel(6);
    
    SSIDBaseline* entry2 = baseline.getOrCreate("Network1");
    
    TEST_ASSERT_EQUAL_PTR(entry1, entry2);
    TEST_ASSERT_TRUE(entry2->hasChannel(6));
    TEST_ASSERT_EQUAL(1, baseline.count());
}

void test_baseline_find(void) {
    Baseline baseline;
    baseline.getOrCreate("Network1");
    baseline.getOrCreate("Network2");
    
    SSIDBaseline* found = baseline.find("Network2");
    
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("Network2", found->ssid);
}

void test_baseline_update_from_observation(void) {
    Baseline baseline;
    
    Observation obs;
    obs.setSSID("TestNet");
    obs.channel = 11;
    obs.rssi = -55;
    uint8_t bssid[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    macCopy(obs.bssid, bssid);
    
    bool result = baseline.update(obs);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1, baseline.count());
    
    SSIDBaseline* entry = baseline.find("TestNet");
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_TRUE(entry->hasChannel(11));
    TEST_ASSERT_TRUE(entry->hasBSSID(bssid));
}

void test_baseline_update_no_ssid_fails(void) {
    Baseline baseline;
    
    Observation obs;
    // No SSID set
    obs.channel = 6;
    
    bool result = baseline.update(obs);
    
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL(0, baseline.count());
}

void test_baseline_clear(void) {
    Baseline baseline;
    baseline.getOrCreate("Network1");
    baseline.getOrCreate("Network2");
    
    baseline.clear();
    
    TEST_ASSERT_EQUAL(0, baseline.count());
    TEST_ASSERT_NULL(baseline.find("Network1"));
}

// ============================================
// Test Runner
// ============================================

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // SSIDBaseline tests
    RUN_TEST(test_ssid_baseline_starts_inactive);
    RUN_TEST(test_ssid_baseline_set_ssid_activates);
    RUN_TEST(test_ssid_baseline_add_bssid);
    RUN_TEST(test_ssid_baseline_add_bssid_duplicate);
    RUN_TEST(test_ssid_baseline_add_channel);
    RUN_TEST(test_ssid_baseline_rssi_stats);
    RUN_TEST(test_ssid_baseline_rssi_average_survives_a_sum_past_32_bits);
    RUN_TEST(test_ssid_baseline_rssi_average_divides_by_a_count_past_2_31);
    RUN_TEST(test_ssid_baseline_rssi_in_range);
    RUN_TEST(test_ssid_baseline_update_from_observation);
    RUN_TEST(test_ssid_baseline_clear);
    
    // Baseline container tests
    RUN_TEST(test_baseline_starts_empty);
    RUN_TEST(test_baseline_get_or_create);
    RUN_TEST(test_baseline_get_or_create_returns_existing);
    RUN_TEST(test_baseline_find);
    RUN_TEST(test_baseline_update_from_observation);
    RUN_TEST(test_baseline_update_no_ssid_fails);
    RUN_TEST(test_baseline_clear);
    
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_ssid_baseline_starts_inactive);
    RUN_TEST(test_ssid_baseline_set_ssid_activates);
    RUN_TEST(test_ssid_baseline_add_bssid);
    RUN_TEST(test_ssid_baseline_add_bssid_duplicate);
    RUN_TEST(test_ssid_baseline_add_channel);
    RUN_TEST(test_ssid_baseline_rssi_stats);
    RUN_TEST(test_ssid_baseline_rssi_average_survives_a_sum_past_32_bits);
    RUN_TEST(test_ssid_baseline_rssi_average_divides_by_a_count_past_2_31);
    RUN_TEST(test_ssid_baseline_rssi_in_range);
    RUN_TEST(test_ssid_baseline_update_from_observation);
    RUN_TEST(test_ssid_baseline_clear);
    RUN_TEST(test_baseline_starts_empty);
    RUN_TEST(test_baseline_get_or_create);
    RUN_TEST(test_baseline_get_or_create_returns_existing);
    RUN_TEST(test_baseline_find);
    RUN_TEST(test_baseline_update_from_observation);
    RUN_TEST(test_baseline_update_no_ssid_fails);
    RUN_TEST(test_baseline_clear);
    UNITY_END();
}
void loop() {}
#endif
