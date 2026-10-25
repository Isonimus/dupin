/**
 * @file test_observation_store.cpp
 * @brief Unit tests for ObservationStore
 */

#include <unity.h>
#include "storage/observation_store.h"
#include "utils/mac_utils.h"

using namespace dupin;

void setUp(void) {}
void tearDown(void) {}

// Helper to create a test observation
Observation makeTestObs(FrameType type, const char* ssid, int8_t rssi, uint32_t timestamp = 0) {
    Observation obs;
    obs.frameType = type;
    obs.setSSID(ssid);
    obs.rssi = rssi;
    obs.timestamp = timestamp;
    obs.channel = 6;
    return obs;
}

// ============================================
// Basic Operations
// ============================================

void test_store_starts_empty(void) {
    ObservationStore<10> store;
    
    TEST_ASSERT_TRUE(store.isEmpty());
    TEST_ASSERT_EQUAL(0, store.size());
    TEST_ASSERT_EQUAL(10, store.capacity());
}

void test_store_push_increases_size(void) {
    ObservationStore<10> store;
    
    store.push(makeTestObs(FrameType::Beacon, "Test", -50));
    
    TEST_ASSERT_FALSE(store.isEmpty());
    TEST_ASSERT_EQUAL(1, store.size());
}

void test_store_peek_returns_oldest(void) {
    ObservationStore<10> store;
    
    store.push(makeTestObs(FrameType::Beacon, "First", -50));
    store.push(makeTestObs(FrameType::Beacon, "Second", -60));
    
    Observation obs;
    store.peek(&obs);
    
    TEST_ASSERT_EQUAL_STRING("First", obs.ssid);
}

void test_store_peek_newest(void) {
    ObservationStore<10> store;
    
    store.push(makeTestObs(FrameType::Beacon, "First", -50));
    store.push(makeTestObs(FrameType::Beacon, "Second", -60));
    store.push(makeTestObs(FrameType::Beacon, "Third", -70));
    
    Observation obs;
    store.peekNewest(&obs);
    
    TEST_ASSERT_EQUAL_STRING("Third", obs.ssid);
}

void test_store_get_recent(void) {
    ObservationStore<10> store;
    
    store.push(makeTestObs(FrameType::Beacon, "First", -50));
    store.push(makeTestObs(FrameType::Beacon, "Second", -60));
    store.push(makeTestObs(FrameType::Beacon, "Third", -70));
    
    Observation obs;
    
    // getRecent(0) = newest
    store.getRecent(0, &obs);
    TEST_ASSERT_EQUAL_STRING("Third", obs.ssid);
    
    // getRecent(1) = second newest
    store.getRecent(1, &obs);
    TEST_ASSERT_EQUAL_STRING("Second", obs.ssid);
    
    // getRecent(2) = oldest
    store.getRecent(2, &obs);
    TEST_ASSERT_EQUAL_STRING("First", obs.ssid);
}

void test_store_overflow(void) {
    ObservationStore<3> store;
    
    store.push(makeTestObs(FrameType::Beacon, "One", -50));
    store.push(makeTestObs(FrameType::Beacon, "Two", -50));
    store.push(makeTestObs(FrameType::Beacon, "Three", -50));
    
    // This should overwrite "One"
    bool overwritten = store.push(makeTestObs(FrameType::Beacon, "Four", -50));
    
    TEST_ASSERT_TRUE(overwritten);
    TEST_ASSERT_EQUAL(3, store.size());
    
    Observation obs;
    store.peek(&obs);
    TEST_ASSERT_EQUAL_STRING("Two", obs.ssid);  // "One" was overwritten
}

// ============================================
// Statistics Tests
// ============================================

void test_stats_frame_type_counts(void) {
    ObservationStore<20> store;
    
    store.push(makeTestObs(FrameType::Beacon, "AP1", -50));
    store.push(makeTestObs(FrameType::Beacon, "AP2", -55));
    store.push(makeTestObs(FrameType::ProbeRequest, "", -60));
    store.push(makeTestObs(FrameType::ProbeResponse, "AP1", -45));
    store.push(makeTestObs(FrameType::Deauthentication, "", -40));
    
    const auto& stats = store.getStats();
    
    TEST_ASSERT_EQUAL(2, stats.beacons);
    TEST_ASSERT_EQUAL(1, stats.probeRequests);
    TEST_ASSERT_EQUAL(1, stats.probeResponses);
    TEST_ASSERT_EQUAL(1, stats.deauths);
    TEST_ASSERT_EQUAL(5, stats.totalFrames);
}

void test_stats_rssi(void) {
    ObservationStore<10> store;
    
    store.push(makeTestObs(FrameType::Beacon, "AP1", -50));
    store.push(makeTestObs(FrameType::Beacon, "AP2", -70));
    store.push(makeTestObs(FrameType::Beacon, "AP3", -40));
    
    const auto& stats = store.getStats();
    
    TEST_ASSERT_EQUAL(-40, stats.rssiMax);
    TEST_ASSERT_EQUAL(-70, stats.rssiMin);
    // Average: (-50 + -70 + -40) / 3 = -160 / 3 = -53
    TEST_ASSERT_EQUAL(-53, stats.rssiAverage());
}

// Every frame adds to this sum, not only beacons, so it leaves 32 bits one frame past
// 2^31 / 128 at the -128 floor -- about 43 days at the 8.2 frames/s a quiet room measured,
// and far sooner in a busy one (ADR-0029).
void test_stats_rssi_average_survives_a_sum_past_32_bits(void) {
    const uint32_t kFramesPastInt32AtFloor = (UINT32_C(1) << 24) + 1;
    ObservationStore<1> store;
    const Observation frame = makeTestObs(FrameType::Beacon, "AP1", -128);

    for (uint32_t i = 0; i < kFramesPastInt32AtFloor; i++) store.push(frame);

    TEST_ASSERT_EQUAL(-128, store.getStats().rssiAverage());
}

void test_stats_timestamps(void) {
    ObservationStore<10> store;
    
    store.push(makeTestObs(FrameType::Beacon, "AP1", -50, 1000));
    store.push(makeTestObs(FrameType::Beacon, "AP2", -55, 2000));
    store.push(makeTestObs(FrameType::Beacon, "AP3", -60, 3000));
    
    const auto& stats = store.getStats();
    
    TEST_ASSERT_EQUAL(1000, stats.firstFrameTime);
    TEST_ASSERT_EQUAL(3000, stats.lastFrameTime);
}

void test_stats_reset(void) {
    ObservationStore<10> store;
    
    store.push(makeTestObs(FrameType::Beacon, "AP1", -50));
    store.push(makeTestObs(FrameType::Beacon, "AP2", -55));
    
    store.resetStats();
    
    const auto& stats = store.getStats();
    TEST_ASSERT_EQUAL(0, stats.totalFrames);
    TEST_ASSERT_EQUAL(0, stats.beacons);
    
    // But observations are still there
    TEST_ASSERT_EQUAL(2, store.size());
}

// ============================================
// Filter/Search Tests
// ============================================

void test_count_with_filter(void) {
    ObservationStore<10> store;
    
    store.push(makeTestObs(FrameType::Beacon, "AP1", -50));
    store.push(makeTestObs(FrameType::ProbeRequest, "", -60));
    store.push(makeTestObs(FrameType::Beacon, "AP2", -55));
    store.push(makeTestObs(FrameType::Beacon, "AP3", -45));
    
    size_t beaconCount = store.count([](const Observation& obs) {
        return obs.frameType == FrameType::Beacon;
    });
    
    TEST_ASSERT_EQUAL(3, beaconCount);
}

void test_find_all(void) {
    ObservationStore<10> store;
    
    store.push(makeTestObs(FrameType::Beacon, "AP1", -50));
    store.push(makeTestObs(FrameType::ProbeRequest, "", -60));
    store.push(makeTestObs(FrameType::Beacon, "AP2", -55));
    store.push(makeTestObs(FrameType::Beacon, "AP3", -45));
    
    Observation results[3];
    size_t found = store.findAll(
        [](const Observation& obs) { return obs.frameType == FrameType::Beacon; },
        results, 3
    );
    
    TEST_ASSERT_EQUAL(3, found);
    TEST_ASSERT_EQUAL_STRING("AP1", results[0].ssid);
    TEST_ASSERT_EQUAL_STRING("AP2", results[1].ssid);
    TEST_ASSERT_EQUAL_STRING("AP3", results[2].ssid);
}

void test_find_with_ssid_filter(void) {
    ObservationStore<10> store;
    
    store.push(makeTestObs(FrameType::Beacon, "MyNetwork", -50));
    store.push(makeTestObs(FrameType::Beacon, "OtherNet", -60));
    store.push(makeTestObs(FrameType::ProbeResponse, "MyNetwork", -55));
    
    size_t count = store.count([](const Observation& obs) {
        return strcmp(obs.ssid, "MyNetwork") == 0;
    });
    
    TEST_ASSERT_EQUAL(2, count);
}

// ============================================
// Clear Tests
// ============================================

void test_clear_empties_store_and_stats(void) {
    ObservationStore<10> store;
    
    store.push(makeTestObs(FrameType::Beacon, "AP1", -50));
    store.push(makeTestObs(FrameType::Beacon, "AP2", -55));
    
    store.clear();
    
    TEST_ASSERT_TRUE(store.isEmpty());
    TEST_ASSERT_EQUAL(0, store.size());
    TEST_ASSERT_EQUAL(0, store.getStats().totalFrames);
}

// ============================================
// Iterator Tests
// ============================================

void test_iteration(void) {
    ObservationStore<10> store;
    
    store.push(makeTestObs(FrameType::Beacon, "AP1", -50));
    store.push(makeTestObs(FrameType::Beacon, "AP2", -55));
    store.push(makeTestObs(FrameType::Beacon, "AP3", -60));
    
    int count = 0;
    for (const auto& obs : store) {
        TEST_ASSERT_EQUAL(FrameType::Beacon, obs.frameType);
        count++;
    }
    
    TEST_ASSERT_EQUAL(3, count);
}

// ============================================
// Test Runner
// ============================================

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Basic operations
    RUN_TEST(test_store_starts_empty);
    RUN_TEST(test_store_push_increases_size);
    RUN_TEST(test_store_peek_returns_oldest);
    RUN_TEST(test_store_peek_newest);
    RUN_TEST(test_store_get_recent);
    RUN_TEST(test_store_overflow);
    
    // Statistics
    RUN_TEST(test_stats_frame_type_counts);
    RUN_TEST(test_stats_rssi);
    RUN_TEST(test_stats_rssi_average_survives_a_sum_past_32_bits);
    RUN_TEST(test_stats_timestamps);
    RUN_TEST(test_stats_reset);
    
    // Filter/search
    RUN_TEST(test_count_with_filter);
    RUN_TEST(test_find_all);
    RUN_TEST(test_find_with_ssid_filter);
    
    // Clear
    RUN_TEST(test_clear_empties_store_and_stats);
    
    // Iterator
    RUN_TEST(test_iteration);
    
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_store_starts_empty);
    RUN_TEST(test_store_push_increases_size);
    RUN_TEST(test_store_peek_returns_oldest);
    RUN_TEST(test_store_peek_newest);
    RUN_TEST(test_store_get_recent);
    RUN_TEST(test_store_overflow);
    RUN_TEST(test_stats_frame_type_counts);
    RUN_TEST(test_stats_rssi);
    RUN_TEST(test_stats_rssi_average_survives_a_sum_past_32_bits);
    RUN_TEST(test_stats_timestamps);
    RUN_TEST(test_stats_reset);
    RUN_TEST(test_count_with_filter);
    RUN_TEST(test_find_all);
    RUN_TEST(test_find_with_ssid_filter);
    RUN_TEST(test_clear_empties_store_and_stats);
    RUN_TEST(test_iteration);
    UNITY_END();
}
void loop() {}
#endif
