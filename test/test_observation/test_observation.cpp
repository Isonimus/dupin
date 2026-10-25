/**
 * @file test_observation.cpp
 * @brief Unit tests for Observation structure
 */

#include <unity.h>
#include "core/observation.h"

using namespace dupin;

void setUp(void) {}
void tearDown(void) {}

// ============================================
// Basic Structure Tests
// ============================================

void test_observation_default_initialized(void) {
    Observation obs;
    
    TEST_ASSERT_EQUAL(0, obs.timestamp);
    TEST_ASSERT_EQUAL(FrameType::Unknown, obs.frameType);
    TEST_ASSERT_EQUAL(0, obs.channel);
    TEST_ASSERT_EQUAL(0, obs.rssi);
    TEST_ASSERT_EQUAL_STRING("", obs.ssid);
    TEST_ASSERT_TRUE(macIsNull(obs.bssid));
}

void test_observation_clear(void) {
    Observation obs;
    obs.timestamp = 12345;
    obs.frameType = FrameType::Beacon;
    obs.channel = 6;
    obs.rssi = -50;
    obs.setSSID("TestNetwork");
    
    obs.clear();
    
    TEST_ASSERT_EQUAL(0, obs.timestamp);
    TEST_ASSERT_EQUAL(FrameType::Unknown, obs.frameType);
    TEST_ASSERT_EQUAL(0, obs.channel);
    TEST_ASSERT_EQUAL_STRING("", obs.ssid);
}

// ============================================
// SSID Tests
// ============================================

void test_observation_set_ssid(void) {
    Observation obs;
    
    obs.setSSID("MyNetwork");
    
    TEST_ASSERT_EQUAL_STRING("MyNetwork", obs.ssid);
    TEST_ASSERT_TRUE(obs.hasSSID());
}

void test_observation_set_ssid_null(void) {
    Observation obs;
    obs.setSSID("Something");
    
    obs.setSSID(nullptr);
    
    TEST_ASSERT_EQUAL_STRING("", obs.ssid);
    TEST_ASSERT_FALSE(obs.hasSSID());
}

void test_observation_set_ssid_truncates_long_names(void) {
    Observation obs;
    
    // SSID longer than 32 chars
    obs.setSSID("ThisIsAVeryLongSSIDNameThatExceedsTheMaximumLength");
    
    TEST_ASSERT_EQUAL(32, strlen(obs.ssid));
    TEST_ASSERT_TRUE(obs.hasSSID());
}

void test_observation_has_ssid_empty(void) {
    Observation obs;
    
    TEST_ASSERT_FALSE(obs.hasSSID());
}

// ============================================
// BSSID Tests
// ============================================

void test_observation_has_bssid(void) {
    Observation obs;
    
    TEST_ASSERT_FALSE(obs.hasBSSID());
    
    uint8_t bssid[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    macCopy(obs.bssid, bssid);
    
    TEST_ASSERT_TRUE(obs.hasBSSID());
}

// ============================================
// Frame Type Tests
// ============================================

void test_observation_is_management_frame(void) {
    Observation obs;
    
    obs.frameType = FrameType::Beacon;
    TEST_ASSERT_TRUE(obs.isManagementFrame());
    
    obs.frameType = FrameType::ProbeRequest;
    TEST_ASSERT_TRUE(obs.isManagementFrame());
    
    obs.frameType = FrameType::Deauthentication;
    TEST_ASSERT_TRUE(obs.isManagementFrame());
    
    obs.frameType = FrameType::Data;
    TEST_ASSERT_FALSE(obs.isManagementFrame());
    
    obs.frameType = FrameType::Unknown;
    TEST_ASSERT_FALSE(obs.isManagementFrame());
}

void test_frame_type_name(void) {
    TEST_ASSERT_EQUAL_STRING("Beacon", frameTypeName(FrameType::Beacon));
    TEST_ASSERT_EQUAL_STRING("ProbeReq", frameTypeName(FrameType::ProbeRequest));
    TEST_ASSERT_EQUAL_STRING("Deauth", frameTypeName(FrameType::Deauthentication));
    TEST_ASSERT_EQUAL_STRING("Unknown", frameTypeName(FrameType::Unknown));
}

// ============================================
// Security Tests  
// ============================================

void test_security_info_equality(void) {
    SecurityInfo sec1;
    sec1.type = SecurityType::WPA2;
    sec1.pairwiseCipher = CipherType::CCMP;
    sec1.authType = AuthType::PSK;
    
    SecurityInfo sec2;
    sec2.type = SecurityType::WPA2;
    sec2.pairwiseCipher = CipherType::CCMP;
    sec2.authType = AuthType::PSK;
    
    SecurityInfo sec3;
    sec3.type = SecurityType::Open;
    
    TEST_ASSERT_TRUE(sec1 == sec2);
    TEST_ASSERT_FALSE(sec1 == sec3);
    TEST_ASSERT_TRUE(sec1 != sec3);
}

void test_security_downgrade_open_from_wpa2(void) {
    SecurityInfo open;
    open.type = SecurityType::Open;
    
    SecurityInfo wpa2;
    wpa2.type = SecurityType::WPA2;
    
    TEST_ASSERT_TRUE(open.isDowngradeFrom(wpa2));
    TEST_ASSERT_FALSE(wpa2.isDowngradeFrom(open));
}

void test_security_downgrade_wep_from_wpa(void) {
    SecurityInfo wep;
    wep.type = SecurityType::WEP;
    
    SecurityInfo wpa;
    wpa.type = SecurityType::WPA;
    
    SecurityInfo wpa2;
    wpa2.type = SecurityType::WPA2;
    
    TEST_ASSERT_TRUE(wep.isDowngradeFrom(wpa));
    TEST_ASSERT_TRUE(wep.isDowngradeFrom(wpa2));
}

void test_security_downgrade_wpa2_from_wpa3(void) {
    SecurityInfo wpa2;
    wpa2.type = SecurityType::WPA2;
    
    SecurityInfo wpa3;
    wpa3.type = SecurityType::WPA3;
    
    TEST_ASSERT_TRUE(wpa2.isDowngradeFrom(wpa3));
    TEST_ASSERT_FALSE(wpa3.isDowngradeFrom(wpa2));
}

void test_security_no_downgrade_same_type(void) {
    SecurityInfo wpa2_1;
    wpa2_1.type = SecurityType::WPA2;
    
    SecurityInfo wpa2_2;
    wpa2_2.type = SecurityType::WPA2;
    
    TEST_ASSERT_FALSE(wpa2_1.isDowngradeFrom(wpa2_2));
}

void test_security_type_name(void) {
    TEST_ASSERT_EQUAL_STRING("Open", securityTypeName(SecurityType::Open));
    TEST_ASSERT_EQUAL_STRING("WPA2", securityTypeName(SecurityType::WPA2));
    TEST_ASSERT_EQUAL_STRING("WPA3", securityTypeName(SecurityType::WPA3));
}

// ============================================
// Capabilities Tests
// ============================================

void test_capabilities_default(void) {
    Capabilities caps;
    
    TEST_ASSERT_FALSE(caps.ht);
    TEST_ASSERT_FALSE(caps.vht);
    TEST_ASSERT_FALSE(caps.he);
}

void test_capabilities_equality(void) {
    Capabilities caps1;
    caps1.ht = true;
    caps1.vht = true;
    
    Capabilities caps2;
    caps2.ht = true;
    caps2.vht = true;
    
    Capabilities caps3;
    caps3.ht = true;
    
    TEST_ASSERT_TRUE(caps1 == caps2);
    TEST_ASSERT_FALSE(caps1 == caps3);
}

// ============================================
// Test Runner
// ============================================

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Structure tests
    RUN_TEST(test_observation_default_initialized);
    RUN_TEST(test_observation_clear);
    
    // SSID tests
    RUN_TEST(test_observation_set_ssid);
    RUN_TEST(test_observation_set_ssid_null);
    RUN_TEST(test_observation_set_ssid_truncates_long_names);
    RUN_TEST(test_observation_has_ssid_empty);
    
    // BSSID tests
    RUN_TEST(test_observation_has_bssid);
    
    // Frame type tests
    RUN_TEST(test_observation_is_management_frame);
    RUN_TEST(test_frame_type_name);
    
    // Security tests
    RUN_TEST(test_security_info_equality);
    RUN_TEST(test_security_downgrade_open_from_wpa2);
    RUN_TEST(test_security_downgrade_wep_from_wpa);
    RUN_TEST(test_security_downgrade_wpa2_from_wpa3);
    RUN_TEST(test_security_no_downgrade_same_type);
    RUN_TEST(test_security_type_name);
    
    // Capabilities tests
    RUN_TEST(test_capabilities_default);
    RUN_TEST(test_capabilities_equality);
    
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_observation_default_initialized);
    RUN_TEST(test_observation_clear);
    RUN_TEST(test_observation_set_ssid);
    RUN_TEST(test_observation_set_ssid_null);
    RUN_TEST(test_observation_set_ssid_truncates_long_names);
    RUN_TEST(test_observation_has_ssid_empty);
    RUN_TEST(test_observation_has_bssid);
    RUN_TEST(test_observation_is_management_frame);
    RUN_TEST(test_frame_type_name);
    RUN_TEST(test_security_info_equality);
    RUN_TEST(test_security_downgrade_open_from_wpa2);
    RUN_TEST(test_security_downgrade_wep_from_wpa);
    RUN_TEST(test_security_downgrade_wpa2_from_wpa3);
    RUN_TEST(test_security_no_downgrade_same_type);
    RUN_TEST(test_security_type_name);
    RUN_TEST(test_capabilities_default);
    RUN_TEST(test_capabilities_equality);
    UNITY_END();
}
void loop() {}
#endif
