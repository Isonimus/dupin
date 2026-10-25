/**
 * @file frame_parser.h
 * @brief 802.11 frame parser
 * 
 * Parses raw 802.11 frames into Observation structures.
 * Extracts SSID, BSSID, security info, and capabilities
 * from management frames.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include "core/observation.h"

namespace dupin {

// ============================================
// 802.11 Frame Constants
// ============================================

// Frame Control field bits
constexpr uint8_t FC_TYPE_MASK = 0x0C;
constexpr uint8_t FC_SUBTYPE_MASK = 0xF0;

// Frame types
constexpr uint8_t FC_TYPE_MGMT = 0x00;
constexpr uint8_t FC_TYPE_CTRL = 0x04;
constexpr uint8_t FC_TYPE_DATA = 0x08;

// Management frame subtypes
constexpr uint8_t FC_SUBTYPE_ASSOC_REQ = 0x00;
constexpr uint8_t FC_SUBTYPE_ASSOC_RESP = 0x10;
constexpr uint8_t FC_SUBTYPE_REASSOC_REQ = 0x20;
constexpr uint8_t FC_SUBTYPE_REASSOC_RESP = 0x30;
constexpr uint8_t FC_SUBTYPE_PROBE_REQ = 0x40;
constexpr uint8_t FC_SUBTYPE_PROBE_RESP = 0x50;
constexpr uint8_t FC_SUBTYPE_BEACON = 0x80;
constexpr uint8_t FC_SUBTYPE_DISASSOC = 0xA0;
constexpr uint8_t FC_SUBTYPE_AUTH = 0xB0;
constexpr uint8_t FC_SUBTYPE_DEAUTH = 0xC0;
constexpr uint8_t FC_SUBTYPE_ACTION = 0xD0;

// Information Element (IE) tags
constexpr uint8_t IE_SSID = 0;
constexpr uint8_t IE_SUPPORTED_RATES = 1;
constexpr uint8_t IE_DS_PARAMETER = 3;
constexpr uint8_t IE_TIM = 5;
constexpr uint8_t IE_RSN = 48;
constexpr uint8_t IE_HT_CAPABILITIES = 45;
constexpr uint8_t IE_HT_OPERATION = 61;
constexpr uint8_t IE_VHT_CAPABILITIES = 191;
constexpr uint8_t IE_VHT_OPERATION = 192;
constexpr uint8_t IE_HE_CAPABILITIES = 255;  // Extension element
constexpr uint8_t IE_VENDOR = 221;

// Minimum header sizes
constexpr size_t MIN_MGMT_HEADER_SIZE = 24;
constexpr size_t BEACON_FIXED_PARAMS_SIZE = 12;  // Timestamp(8) + Interval(2) + Capability(2)

// ============================================
// Frame Parser Class
// ============================================

/**
 * @brief Parser for 802.11 frames
 * 
 * Stateless parser that converts raw frame data into Observation structs.
 * Handles beacon, probe request/response, and other management frames.
 */
class FrameParser {
public:
    /**
     * @brief Parse a raw 802.11 frame into an Observation
     * 
     * @param data Raw frame data
     * @param len Frame length
     * @param rssi Signal strength
     * @param channel Channel frame was received on
     * @param out Output Observation structure
     * @return true if parsing succeeded
     */
    static bool parse(const uint8_t* data, size_t len, int8_t rssi, 
                      uint8_t channel, Observation& out);

private:
    /**
     * @brief Parse a beacon frame
     */
    static bool parseBeacon(const uint8_t* data, size_t len, Observation& out);
    
    /**
     * @brief Parse a probe request frame
     */
    static bool parseProbeRequest(const uint8_t* data, size_t len, Observation& out);
    
    /**
     * @brief Parse a probe response frame
     */
    static bool parseProbeResponse(const uint8_t* data, size_t len, Observation& out);
    
    /**
     * @brief Parse information elements (tagged parameters)
     * 
     * @param data Pointer to start of IEs
     * @param len Length of IE data
     * @param out Observation to populate
     */
    static void parseInformationElements(const uint8_t* data, size_t len, Observation& out);
    
    /**
     * @brief Parse RSN (WPA2) information element
     */
    static void parseRSN(const uint8_t* data, size_t len, SecurityInfo& out);
    
    /**
     * @brief Parse vendor-specific IE (for WPA1)
     */
    static void parseVendorIE(const uint8_t* data, size_t len, SecurityInfo& out);
    
    /**
     * @brief Extract MAC addresses from frame header
     */
    static void extractAddresses(const uint8_t* header, Observation& out);
    
    /**
     * @brief Get frame type from frame control field
     */
    static FrameType getFrameType(uint8_t fc0, uint8_t fc1);
};

// ============================================
// Inline Implementations
// ============================================

inline FrameType FrameParser::getFrameType(uint8_t fc0, uint8_t fc1) {
    uint8_t type = fc0 & FC_TYPE_MASK;
    uint8_t subtype = fc0 & FC_SUBTYPE_MASK;
    
    if (type == FC_TYPE_MGMT) {
        switch (subtype) {
            case FC_SUBTYPE_BEACON: return FrameType::Beacon;
            case FC_SUBTYPE_PROBE_REQ: return FrameType::ProbeRequest;
            case FC_SUBTYPE_PROBE_RESP: return FrameType::ProbeResponse;
            case FC_SUBTYPE_ASSOC_REQ:
            case FC_SUBTYPE_ASSOC_RESP: return FrameType::Association;
            case FC_SUBTYPE_REASSOC_REQ:
            case FC_SUBTYPE_REASSOC_RESP: return FrameType::Reassociation;
            case FC_SUBTYPE_DISASSOC: return FrameType::Disassociation;
            case FC_SUBTYPE_DEAUTH: return FrameType::Deauthentication;
            case FC_SUBTYPE_ACTION: return FrameType::Action;
            default: return FrameType::Unknown;
        }
    } else if (type == FC_TYPE_DATA) {
        return FrameType::Data;
    }
    
    return FrameType::Unknown;
}

} // namespace dupin
