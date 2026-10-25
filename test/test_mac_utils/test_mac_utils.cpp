/**
 * @file test_mac_utils.cpp
 * @brief Unit tests for MAC address utilities
 */

#include <unity.h>
#include "utils/mac_utils.h"

using namespace dupin;

void setUp(void) {}
void tearDown(void) {}

// ============================================
// Comparison Tests
// ============================================

void test_mac_equals_identical(void) {
    uint8_t mac1[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t mac2[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    TEST_ASSERT_TRUE(macEquals(mac1, mac2));
}

void test_mac_equals_different(void) {
    uint8_t mac1[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t mac2[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x00};
    
    TEST_ASSERT_FALSE(macEquals(mac1, mac2));
}

// ============================================
// Special Address Tests
// ============================================

void test_mac_is_broadcast(void) {
    uint8_t broadcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t regular[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    TEST_ASSERT_TRUE(macIsBroadcast(broadcast));
    TEST_ASSERT_FALSE(macIsBroadcast(regular));
}

void test_mac_is_null(void) {
    uint8_t null_mac[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t regular[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    TEST_ASSERT_TRUE(macIsNull(null_mac));
    TEST_ASSERT_FALSE(macIsNull(regular));
}

void test_mac_is_multicast(void) {
    uint8_t multicast[] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};  // LSB of first byte set
    uint8_t unicast[] = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    
    TEST_ASSERT_TRUE(macIsMulticast(multicast));
    TEST_ASSERT_FALSE(macIsMulticast(unicast));
}

void test_mac_is_locally_administered(void) {
    uint8_t local[] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};  // Bit 1 of first byte set
    uint8_t global[] = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    
    TEST_ASSERT_TRUE(macIsLocallyAdministered(local));
    TEST_ASSERT_FALSE(macIsLocallyAdministered(global));
}

// ============================================
// Copy and Clear Tests
// ============================================

void test_mac_copy(void) {
    uint8_t src[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t dst[6] = {0};
    
    macCopy(dst, src);
    
    TEST_ASSERT_TRUE(macEquals(src, dst));
}

void test_mac_clear(void) {
    uint8_t mac[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    macClear(mac);
    
    TEST_ASSERT_TRUE(macIsNull(mac));
}

// ============================================
// Format Tests
// ============================================

void test_mac_format_uppercase(void) {
    uint8_t mac[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    char buffer[18];
    
    macFormat(mac, buffer, true);
    
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", buffer);
}

void test_mac_format_lowercase(void) {
    uint8_t mac[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    char buffer[18];
    
    macFormat(mac, buffer, false);
    
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", buffer);
}

void test_mac_format_with_zeros(void) {
    uint8_t mac[] = {0x00, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E};
    char buffer[18];
    
    macFormat(mac, buffer, true);
    
    TEST_ASSERT_EQUAL_STRING("00:0A:0B:0C:0D:0E", buffer);
}

// ============================================
// Parse Tests
// ============================================

void test_mac_parse_colon_format(void) {
    uint8_t mac[6];
    bool result = macParse("AA:BB:CC:DD:EE:FF", mac);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_HEX8(0xAA, mac[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, mac[1]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, mac[2]);
    TEST_ASSERT_EQUAL_HEX8(0xDD, mac[3]);
    TEST_ASSERT_EQUAL_HEX8(0xEE, mac[4]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, mac[5]);
}

void test_mac_parse_dash_format(void) {
    uint8_t mac[6];
    bool result = macParse("AA-BB-CC-DD-EE-FF", mac);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_HEX8(0xAA, mac[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, mac[5]);
}

void test_mac_parse_lowercase(void) {
    uint8_t mac[6];
    bool result = macParse("aa:bb:cc:dd:ee:ff", mac);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_HEX8(0xAA, mac[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, mac[5]);
}

void test_mac_parse_invalid_returns_false(void) {
    uint8_t mac[6];
    
    TEST_ASSERT_FALSE(macParse("invalid", mac));
    TEST_ASSERT_FALSE(macParse("AA:BB:CC:DD:EE", mac));  // Too short
    TEST_ASSERT_FALSE(macParse(nullptr, mac));
}

void test_mac_parse_null_output_returns_false(void) {
    TEST_ASSERT_FALSE(macParse("AA:BB:CC:DD:EE:FF", nullptr));
}

// ============================================
// OUI Tests
// ============================================

void test_mac_get_oui(void) {
    uint8_t mac[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t oui[3];
    
    macGetOUI(mac, oui);
    
    TEST_ASSERT_EQUAL_HEX8(0xAA, oui[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, oui[1]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, oui[2]);
}

// ============================================
// Test Runner
// ============================================

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Comparison
    RUN_TEST(test_mac_equals_identical);
    RUN_TEST(test_mac_equals_different);
    
    // Special addresses
    RUN_TEST(test_mac_is_broadcast);
    RUN_TEST(test_mac_is_null);
    RUN_TEST(test_mac_is_multicast);
    RUN_TEST(test_mac_is_locally_administered);
    
    // Copy and clear
    RUN_TEST(test_mac_copy);
    RUN_TEST(test_mac_clear);
    
    // Format
    RUN_TEST(test_mac_format_uppercase);
    RUN_TEST(test_mac_format_lowercase);
    RUN_TEST(test_mac_format_with_zeros);
    
    // Parse
    RUN_TEST(test_mac_parse_colon_format);
    RUN_TEST(test_mac_parse_dash_format);
    RUN_TEST(test_mac_parse_lowercase);
    RUN_TEST(test_mac_parse_invalid_returns_false);
    RUN_TEST(test_mac_parse_null_output_returns_false);
    
    // OUI
    RUN_TEST(test_mac_get_oui);
    
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_mac_equals_identical);
    RUN_TEST(test_mac_equals_different);
    RUN_TEST(test_mac_is_broadcast);
    RUN_TEST(test_mac_is_null);
    RUN_TEST(test_mac_is_multicast);
    RUN_TEST(test_mac_is_locally_administered);
    RUN_TEST(test_mac_copy);
    RUN_TEST(test_mac_clear);
    RUN_TEST(test_mac_format_uppercase);
    RUN_TEST(test_mac_format_lowercase);
    RUN_TEST(test_mac_format_with_zeros);
    RUN_TEST(test_mac_parse_colon_format);
    RUN_TEST(test_mac_parse_dash_format);
    RUN_TEST(test_mac_parse_lowercase);
    RUN_TEST(test_mac_parse_invalid_returns_false);
    RUN_TEST(test_mac_parse_null_output_returns_false);
    RUN_TEST(test_mac_get_oui);
    UNITY_END();
}
void loop() {}
#endif
