/**
 * @file test_i2c_scan.cpp
 * @brief Unit tests for the I2C scan line formatter (ADR-0034).
 *
 * The load-bearing case is test_truncation_does_not_overflow: it is the regression for the
 * CWE-190 CodeQL flagged. Without the snprintf return-value guard, a write into a buffer too
 * small to hold it advances `written` past the buffer, the next `outSize - written`
 * underflows size_t, and the following call overruns the buffer. The canary region after the
 * buffer catches exactly that overrun.
 */
#include <unity.h>

#include <cstring>

#include "utils/i2c_scan.h"

using namespace dupin;

// The bounds reportClockChip scans, mirrored here so the test exercises the real range.
static constexpr size_t kFirst = 0x08;
static constexpr size_t kEnd = 0x78;

void setUp(void) {}
void tearDown(void) {}

void test_no_addresses_answered_yields_empty(void) {
    bool answered[kEnd] = {};
    char out[64];
    memset(out, 'Z', sizeof(out));
    const size_t written = formatI2cScanAddresses(out, sizeof(out), answered, kFirst, kEnd);
    TEST_ASSERT_EQUAL(0, written);
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_single_address(void) {
    bool answered[kEnd] = {};
    answered[0x51] = true;  // the PCF8563/BM8563 slot M5Unified probes
    char out[64];
    const size_t written = formatI2cScanAddresses(out, sizeof(out), answered, kFirst, kEnd);
    TEST_ASSERT_EQUAL_STRING(" 0x51", out);
    TEST_ASSERT_EQUAL(5, written);
}

void test_multiple_addresses_in_order(void) {
    bool answered[kEnd] = {};
    answered[0x10] = true;
    answered[0x3C] = true;
    answered[0x68] = true;
    char out[64];
    const size_t written = formatI2cScanAddresses(out, sizeof(out), answered, kFirst, kEnd);
    TEST_ASSERT_EQUAL_STRING(" 0x10 0x3C 0x68", out);
    TEST_ASSERT_EQUAL(15, written);
}

// The whole scannable range, into a buffer sized exactly as reportClockChip sizes it. This is
// the no-truncation path: every address must land and the buffer must end cleanly.
void test_full_range_exact_fit(void) {
    bool answered[kEnd] = {};
    for (size_t a = kFirst; a < kEnd; ++a) answered[a] = true;

    char out[(kEnd - kFirst) * kI2cAddressRunChars + 1];
    const size_t written = formatI2cScanAddresses(out, sizeof(out), answered, kFirst, kEnd);

    TEST_ASSERT_EQUAL((kEnd - kFirst) * kI2cAddressRunChars, written);
    TEST_ASSERT_EQUAL(written, strlen(out));
    TEST_ASSERT_EQUAL(0, out[written]);
    // First and last runs are the real endpoints, proving the whole list was emitted in order.
    TEST_ASSERT_EQUAL(0, strncmp(out, " 0x08", 5));
    TEST_ASSERT_EQUAL(0, strcmp(out + written - 5, " 0x77"));
}

// Regression for CWE-190: a buffer too small for the answered set must truncate at a whole
// address boundary without ever writing past outSize. The canary bytes after the advertised
// buffer must survive untouched.
void test_truncation_does_not_overflow(void) {
    bool answered[kEnd] = {};
    for (size_t a = kFirst; a < kEnd; ++a) answered[a] = true;  // far more than will fit

    constexpr unsigned char kCanary = 0x7F;
    char storage[64];
    memset(storage, kCanary, sizeof(storage));
    constexpr size_t kOutSize = 16;  // holds three whole " 0xNN" runs (15 chars + NUL); a 4th is refused

    const size_t written = formatI2cScanAddresses(storage, kOutSize, answered, kFirst, kEnd);

    // Nothing was written at or beyond the advertised size: the canary is intact.
    for (size_t i = kOutSize; i < sizeof(storage); ++i) {
        TEST_ASSERT_EQUAL_HEX8(kCanary, static_cast<unsigned char>(storage[i]));
    }
    // NUL-terminated within bounds, reported length matches, and only whole addresses appear.
    TEST_ASSERT_TRUE(written < kOutSize);
    TEST_ASSERT_EQUAL(0, storage[written]);
    TEST_ASSERT_EQUAL(written, strlen(storage));
    TEST_ASSERT_EQUAL_STRING(" 0x08 0x09 0x0A", storage);
}

// A zero-size buffer writes nothing at all, not even a terminator.
void test_zero_size_buffer_writes_nothing(void) {
    bool answered[kEnd] = {};
    answered[0x20] = true;
    constexpr unsigned char kCanary = 0x5A;
    char storage[8];
    memset(storage, kCanary, sizeof(storage));
    const size_t written = formatI2cScanAddresses(storage, 0, answered, kFirst, kEnd);
    TEST_ASSERT_EQUAL(0, written);
    for (size_t i = 0; i < sizeof(storage); ++i) {
        TEST_ASSERT_EQUAL_HEX8(kCanary, static_cast<unsigned char>(storage[i]));
    }
}

#ifdef NATIVE_BUILD
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_no_addresses_answered_yields_empty);
    RUN_TEST(test_single_address);
    RUN_TEST(test_multiple_addresses_in_order);
    RUN_TEST(test_full_range_exact_fit);
    RUN_TEST(test_truncation_does_not_overflow);
    RUN_TEST(test_zero_size_buffer_writes_nothing);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_no_addresses_answered_yields_empty);
    RUN_TEST(test_single_address);
    RUN_TEST(test_multiple_addresses_in_order);
    RUN_TEST(test_full_range_exact_fit);
    RUN_TEST(test_truncation_does_not_overflow);
    RUN_TEST(test_zero_size_buffer_writes_nothing);
    UNITY_END();
}
void loop() {}
#endif
