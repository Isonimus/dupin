/**
 * @file test_time_utils.cpp
 * @brief Unit tests for time utilities
 */

#include <unity.h>
#include "utils/time_utils.h"

using namespace dupin;

void setUp(void) {
    // Reset mock time before each test
    setMockMillis(0);
}

void tearDown(void) {}

// ============================================
// Basic Time Functions
// ============================================

void test_get_timestamp_returns_mock_value(void) {
    setMockMillis(12345);
    
    TEST_ASSERT_EQUAL(12345, getTimestamp());
}

void test_elapsed_ms_simple(void) {
    setMockMillis(1000);
    uint32_t start = getTimestamp();
    
    advanceMockMillis(500);
    
    TEST_ASSERT_EQUAL(500, elapsedMs(start));
}

void test_elapsed_ms_handles_overflow(void) {
    // Start near max uint32
    setMockMillis(0xFFFFFF00);  // ~49.7 days
    uint32_t start = getTimestamp();
    
    // Advance past overflow
    setMockMillis(0x00000100);  // Wrapped around
    
    // Should correctly calculate: 0x100 - 0xFFFFFF00 = 0x200 = 512
    TEST_ASSERT_EQUAL(0x200, elapsedMs(start));
}

void test_has_elapsed_true(void) {
    setMockMillis(1000);
    uint32_t start = getTimestamp();
    
    advanceMockMillis(100);
    
    TEST_ASSERT_TRUE(hasElapsed(start, 100));
    TEST_ASSERT_TRUE(hasElapsed(start, 50));
}

void test_has_elapsed_false(void) {
    setMockMillis(1000);
    uint32_t start = getTimestamp();
    
    advanceMockMillis(50);
    
    TEST_ASSERT_FALSE(hasElapsed(start, 100));
}

// ============================================
// Time Formatting
// ============================================

void test_format_time_zero(void) {
    char buf[16];
    formatTime(0, buf);
    
    TEST_ASSERT_EQUAL_STRING("00:00:00", buf);
}

void test_format_time_seconds(void) {
    char buf[16];
    formatTime(45 * 1000, buf);  // 45 seconds
    
    TEST_ASSERT_EQUAL_STRING("00:00:45", buf);
}

void test_format_time_minutes(void) {
    char buf[16];
    formatTime((5 * 60 + 30) * 1000, buf);  // 5:30
    
    TEST_ASSERT_EQUAL_STRING("00:05:30", buf);
}

void test_format_time_hours(void) {
    char buf[16];
    formatTime((2 * 3600 + 15 * 60 + 45) * 1000, buf);  // 2:15:45
    
    TEST_ASSERT_EQUAL_STRING("02:15:45", buf);
}

// ============================================
// Duration Formatting
// ============================================

void test_format_duration_seconds(void) {
    char buf[16];
    formatDuration(45 * 1000, buf);
    
    TEST_ASSERT_EQUAL_STRING("45s", buf);
}

void test_format_duration_minutes(void) {
    char buf[16];
    formatDuration((2 * 60 + 30) * 1000, buf);
    
    TEST_ASSERT_EQUAL_STRING("2m 30s", buf);
}

void test_format_duration_minutes_exact(void) {
    char buf[16];
    formatDuration(5 * 60 * 1000, buf);
    
    TEST_ASSERT_EQUAL_STRING("5m", buf);
}

void test_format_duration_hours(void) {
    char buf[16];
    formatDuration((1 * 3600 + 15 * 60) * 1000, buf);
    
    TEST_ASSERT_EQUAL_STRING("1h 15m", buf);
}

void test_format_duration_days(void) {
    char buf[16];
    formatDuration((3 * 86400 + 2 * 3600) * 1000UL, buf);
    
    TEST_ASSERT_EQUAL_STRING("3d 2h", buf);
}

// ============================================
// Timer Class
// ============================================

void test_timer_starts_not_elapsed(void) {
    Timer timer(1000);
    timer.start();
    
    TEST_ASSERT_FALSE(timer.isElapsed());
}

void test_timer_elapsed_after_interval(void) {
    setMockMillis(0);
    Timer timer(100);
    timer.start();
    
    advanceMockMillis(100);
    
    TEST_ASSERT_TRUE(timer.isElapsed());
}

void test_timer_check_returns_true_and_resets(void) {
    setMockMillis(0);
    Timer timer(100);
    timer.start();
    
    advanceMockMillis(100);
    
    TEST_ASSERT_TRUE(timer.check());  // Should return true and reset
    TEST_ASSERT_FALSE(timer.isElapsed());  // Should be reset now
}

void test_timer_remaining(void) {
    setMockMillis(0);
    Timer timer(100);
    timer.start();
    
    advanceMockMillis(40);
    
    TEST_ASSERT_EQUAL(60, timer.remaining());
}

void test_timer_remaining_zero_when_elapsed(void) {
    setMockMillis(0);
    Timer timer(100);
    timer.start();
    
    advanceMockMillis(150);
    
    TEST_ASSERT_EQUAL(0, timer.remaining());
}

void test_timer_auto_starts_on_first_check(void) {
    setMockMillis(0);
    Timer timer(100);
    
    // First check should auto-start and return false
    TEST_ASSERT_FALSE(timer.check());
    TEST_ASSERT_TRUE(timer.isStarted());
}

void test_timer_set_interval(void) {
    Timer timer(100);
    timer.setInterval(500);
    
    TEST_ASSERT_EQUAL(500, timer.getInterval());
}

// ============================================
// Test Runner
// ============================================

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Basic time functions
    RUN_TEST(test_get_timestamp_returns_mock_value);
    RUN_TEST(test_elapsed_ms_simple);
    RUN_TEST(test_elapsed_ms_handles_overflow);
    RUN_TEST(test_has_elapsed_true);
    RUN_TEST(test_has_elapsed_false);
    
    // Time formatting
    RUN_TEST(test_format_time_zero);
    RUN_TEST(test_format_time_seconds);
    RUN_TEST(test_format_time_minutes);
    RUN_TEST(test_format_time_hours);
    
    // Duration formatting
    RUN_TEST(test_format_duration_seconds);
    RUN_TEST(test_format_duration_minutes);
    RUN_TEST(test_format_duration_minutes_exact);
    RUN_TEST(test_format_duration_hours);
    RUN_TEST(test_format_duration_days);
    
    // Timer class
    RUN_TEST(test_timer_starts_not_elapsed);
    RUN_TEST(test_timer_elapsed_after_interval);
    RUN_TEST(test_timer_check_returns_true_and_resets);
    RUN_TEST(test_timer_remaining);
    RUN_TEST(test_timer_remaining_zero_when_elapsed);
    RUN_TEST(test_timer_auto_starts_on_first_check);
    RUN_TEST(test_timer_set_interval);
    
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    // Note: On ESP32, these tests would use real millis()
    // Skip mock-dependent tests
    UNITY_END();
}
void loop() {}
#endif
