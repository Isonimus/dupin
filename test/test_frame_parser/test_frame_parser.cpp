/**
 * @file test_frame_parser.cpp
 * @brief Unit tests for 802.11 frame parser
 * 
 * Uses captured frame samples to test parsing logic.
 */

#include <unity.h>
#include "capture/frame_parser.h"
#include "utils/mac_utils.h"

using namespace dupin;

void setUp(void) {}
void tearDown(void) {}

// ============================================
// Sample Frames
// ============================================

// Minimal valid beacon frame (header + fixed params, no IEs)
// Frame Control: 0x80 0x00 (Beacon)
// Duration: 0x00 0x00
// DA: FF:FF:FF:FF:FF:FF (broadcast)
// SA: AA:BB:CC:DD:EE:01
// BSSID: AA:BB:CC:DD:EE:01
// Seq: 0x00 0x00
// Fixed params: timestamp(8) + interval(2) + capability(2)
static const uint8_t BEACON_MINIMAL[] = {
    // Frame Control + Duration (4 bytes)
    0x80, 0x00, 0x00, 0x00,
    // Address 1 (DA) - broadcast
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // Address 2 (SA)
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01,
    // Address 3 (BSSID)
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01,
    // Sequence Control
    0x00, 0x00,
    // Fixed Parameters (12 bytes)
    // Timestamp (8 bytes)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Beacon Interval (2 bytes) = 100 TUs
    0x64, 0x00,
    // Capability Info (2 bytes) - no privacy
    0x01, 0x00
};

// Beacon with SSID IE
static const uint8_t BEACON_WITH_SSID[] = {
    // Frame Control + Duration
    0x80, 0x00, 0x00, 0x00,
    // Address 1 (DA) - broadcast
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // Address 2 (SA)
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
    // Address 3 (BSSID)
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
    // Sequence Control
    0x10, 0x00,
    // Fixed Parameters
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp
    0xC8, 0x00,  // Beacon Interval = 200 TUs
    0x11, 0x00,  // Capability - privacy bit set
    // Information Elements
    // SSID: "TestNet"
    0x00, 0x07, 'T', 'e', 's', 't', 'N', 'e', 't',
    // DS Parameter Set: Channel 6
    0x03, 0x01, 0x06,
    // HT Capabilities (simplified - just tag)
    0x2D, 0x00
};

// Probe Request frame
static const uint8_t PROBE_REQUEST[] = {
    // Frame Control: Probe Request (0x40)
    0x40, 0x00, 0x00, 0x00,
    // Address 1 (DA) - broadcast
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // Address 2 (SA) - client
    0xAA, 0x11, 0x22, 0x33, 0x44, 0x55,
    // Address 3 (BSSID) - broadcast
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // Sequence Control
    0x00, 0x00,
    // Information Elements
    // SSID: "MyWiFi"
    0x00, 0x06, 'M', 'y', 'W', 'i', 'F', 'i'
};

// Deauthentication frame
static const uint8_t DEAUTH_FRAME[] = {
    // Frame Control: Deauth (0xC0)
    0xC0, 0x00, 0x00, 0x00,
    // Address 1 (DA)
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01,
    // Address 2 (SA)
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
    // Address 3 (BSSID)
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
    // Sequence Control
    0x00, 0x00,
    // Reason code
    0x03, 0x00
};

// ============================================
// Frame Type Detection Tests
// ============================================

void test_parse_beacon_type(void) {
    Observation obs;
    bool result = FrameParser::parse(BEACON_MINIMAL, sizeof(BEACON_MINIMAL), 
                                      -50, 6, obs);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(FrameType::Beacon, obs.frameType);
}

void test_parse_probe_request_type(void) {
    Observation obs;
    bool result = FrameParser::parse(PROBE_REQUEST, sizeof(PROBE_REQUEST),
                                      -60, 1, obs);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(FrameType::ProbeRequest, obs.frameType);
}

void test_parse_deauth_type(void) {
    Observation obs;
    bool result = FrameParser::parse(DEAUTH_FRAME, sizeof(DEAUTH_FRAME),
                                      -45, 11, obs);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(FrameType::Deauthentication, obs.frameType);
}

// ============================================
// Address Extraction Tests
// ============================================

void test_parse_extracts_bssid(void) {
    Observation obs;
    FrameParser::parse(BEACON_MINIMAL, sizeof(BEACON_MINIMAL), -50, 6, obs);
    
    uint8_t expected[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    TEST_ASSERT_TRUE(macEquals(obs.bssid, expected));
}

void test_parse_extracts_transmitter(void) {
    Observation obs;
    FrameParser::parse(PROBE_REQUEST, sizeof(PROBE_REQUEST), -50, 6, obs);
    
    uint8_t expected[] = {0xAA, 0x11, 0x22, 0x33, 0x44, 0x55};
    TEST_ASSERT_TRUE(macEquals(obs.transmitterMac, expected));
}

// ============================================
// SSID Parsing Tests
// ============================================

void test_parse_beacon_ssid(void) {
    Observation obs;
    FrameParser::parse(BEACON_WITH_SSID, sizeof(BEACON_WITH_SSID), -50, 6, obs);
    
    TEST_ASSERT_EQUAL_STRING("TestNet", obs.ssid);
    TEST_ASSERT_TRUE(obs.hasSSID());
}

void test_parse_probe_request_ssid(void) {
    Observation obs;
    FrameParser::parse(PROBE_REQUEST, sizeof(PROBE_REQUEST), -50, 6, obs);
    
    TEST_ASSERT_EQUAL_STRING("MyWiFi", obs.ssid);
}

void test_parse_beacon_no_ssid(void) {
    Observation obs;
    FrameParser::parse(BEACON_MINIMAL, sizeof(BEACON_MINIMAL), -50, 6, obs);
    
    TEST_ASSERT_FALSE(obs.hasSSID());
}

// ============================================
// Basic Fields Tests
// ============================================

void test_parse_sets_rssi(void) {
    Observation obs;
    FrameParser::parse(BEACON_MINIMAL, sizeof(BEACON_MINIMAL), -75, 6, obs);
    
    TEST_ASSERT_EQUAL(-75, obs.rssi);
}

void test_parse_sets_channel(void) {
    Observation obs;
    FrameParser::parse(BEACON_MINIMAL, sizeof(BEACON_MINIMAL), -50, 11, obs);
    
    TEST_ASSERT_EQUAL(11, obs.channel);
}

void test_parse_beacon_interval(void) {
    Observation obs;
    FrameParser::parse(BEACON_MINIMAL, sizeof(BEACON_MINIMAL), -50, 6, obs);
    
    TEST_ASSERT_EQUAL(100, obs.beaconInterval);  // 0x64 = 100
}

void test_parse_beacon_interval_200(void) {
    Observation obs;
    FrameParser::parse(BEACON_WITH_SSID, sizeof(BEACON_WITH_SSID), -50, 6, obs);
    
    TEST_ASSERT_EQUAL(200, obs.beaconInterval);  // 0xC8 = 200
}

// ============================================
// Security Detection Tests
// ============================================

void test_parse_open_network(void) {
    // BEACON_MINIMAL has capability info 0x0001 - no privacy bit
    Observation obs;
    FrameParser::parse(BEACON_MINIMAL, sizeof(BEACON_MINIMAL), -50, 6, obs);
    
    TEST_ASSERT_EQUAL(SecurityType::Open, obs.security.type);
}

void test_parse_privacy_bit_sets_wep(void) {
    // BEACON_WITH_SSID has capability info 0x0011 - privacy bit set
    Observation obs;
    FrameParser::parse(BEACON_WITH_SSID, sizeof(BEACON_WITH_SSID), -50, 6, obs);
    
    // Without RSN IE, privacy bit defaults to WEP
    TEST_ASSERT_EQUAL(SecurityType::WEP, obs.security.type);
}

// ============================================
// Capabilities Tests
// ============================================

void test_parse_ht_capabilities(void) {
    // BEACON_WITH_SSID includes HT Capabilities IE (0x2D)
    Observation obs;
    FrameParser::parse(BEACON_WITH_SSID, sizeof(BEACON_WITH_SSID), -50, 6, obs);
    
    TEST_ASSERT_TRUE(obs.capabilities.ht);
}

// ============================================
// Edge Cases
// ============================================

void test_parse_null_data_fails(void) {
    Observation obs;
    bool result = FrameParser::parse(nullptr, 100, -50, 6, obs);
    
    TEST_ASSERT_FALSE(result);
}

void test_parse_too_short_fails(void) {
    uint8_t short_frame[] = {0x80, 0x00, 0x00};
    Observation obs;
    bool result = FrameParser::parse(short_frame, sizeof(short_frame), -50, 6, obs);
    
    TEST_ASSERT_FALSE(result);
}

void test_parse_clears_output_first(void) {
    Observation obs;
    obs.setSSID("OldSSID");
    obs.rssi = -99;
    
    FrameParser::parse(BEACON_MINIMAL, sizeof(BEACON_MINIMAL), -50, 6, obs);
    
    // Old values should be cleared
    TEST_ASSERT_EQUAL(-50, obs.rssi);  // New value
    TEST_ASSERT_FALSE(obs.hasSSID());   // Cleared (beacon has no SSID IE)
}

// ============================================
// Test Runner
// ============================================

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Frame type detection
    RUN_TEST(test_parse_beacon_type);
    RUN_TEST(test_parse_probe_request_type);
    RUN_TEST(test_parse_deauth_type);
    
    // Address extraction
    RUN_TEST(test_parse_extracts_bssid);
    RUN_TEST(test_parse_extracts_transmitter);
    
    // SSID parsing
    RUN_TEST(test_parse_beacon_ssid);
    RUN_TEST(test_parse_probe_request_ssid);
    RUN_TEST(test_parse_beacon_no_ssid);
    
    // Basic fields
    RUN_TEST(test_parse_sets_rssi);
    RUN_TEST(test_parse_sets_channel);
    RUN_TEST(test_parse_beacon_interval);
    RUN_TEST(test_parse_beacon_interval_200);
    
    // Security detection
    RUN_TEST(test_parse_open_network);
    RUN_TEST(test_parse_privacy_bit_sets_wep);
    
    // Capabilities
    RUN_TEST(test_parse_ht_capabilities);
    
    // Edge cases
    RUN_TEST(test_parse_null_data_fails);
    RUN_TEST(test_parse_too_short_fails);
    RUN_TEST(test_parse_clears_output_first);
    
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_beacon_type);
    RUN_TEST(test_parse_probe_request_type);
    RUN_TEST(test_parse_deauth_type);
    RUN_TEST(test_parse_extracts_bssid);
    RUN_TEST(test_parse_extracts_transmitter);
    RUN_TEST(test_parse_beacon_ssid);
    RUN_TEST(test_parse_probe_request_ssid);
    RUN_TEST(test_parse_beacon_no_ssid);
    RUN_TEST(test_parse_sets_rssi);
    RUN_TEST(test_parse_sets_channel);
    RUN_TEST(test_parse_beacon_interval);
    RUN_TEST(test_parse_beacon_interval_200);
    RUN_TEST(test_parse_open_network);
    RUN_TEST(test_parse_privacy_bit_sets_wep);
    RUN_TEST(test_parse_ht_capabilities);
    RUN_TEST(test_parse_null_data_fails);
    RUN_TEST(test_parse_too_short_fails);
    RUN_TEST(test_parse_clears_output_first);
    UNITY_END();
}
void loop() {}
#endif
