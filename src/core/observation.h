/**
 * @file observation.h
 * @brief Core observation data structure
 * 
 * The Observation struct represents a single captured 802.11 frame
 * with normalized metadata. This is the fundamental data unit in Dupin.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include "dupin.h"
#include "utils/mac_utils.h"

namespace dupin {

// ============================================
// Enumerations
// ============================================

/**
 * @brief 802.11 frame types that Dupin observes
 */
enum class FrameType : uint8_t {
    Unknown = 0,
    Beacon,
    ProbeRequest,
    ProbeResponse,
    Association,
    Reassociation,
    Disassociation,
    Deauthentication,
    Data,           // Data frames (header only)
    Action,         // Action frames
};

/**
 * @brief Security protocol type
 */
enum class SecurityType : uint8_t {
    Unknown = 0,
    Open,
    WEP,
    WPA,
    WPA2,
    WPA3,
};

/**
 * @brief Cipher suite type
 */
enum class CipherType : uint8_t {
    Unknown = 0,
    None,
    WEP40,
    WEP104,
    TKIP,
    CCMP,       // AES-CCMP
    GCMP,       // AES-GCMP (WPA3)
};

/**
 * @brief Authentication type
 */
enum class AuthType : uint8_t {
    Unknown = 0,
    Open,
    PSK,
    SAE,        // WPA3 Simultaneous Authentication of Equals
    Enterprise, // 802.1X
};

// ============================================
// Supporting Structures
// ============================================

/**
 * @brief Security information extracted from beacon/probe response
 */
struct SecurityInfo {
    SecurityType type = SecurityType::Unknown;
    CipherType pairwiseCipher = CipherType::Unknown;
    CipherType groupCipher = CipherType::Unknown;
    AuthType authType = AuthType::Unknown;
    
    bool operator==(const SecurityInfo& other) const {
        return type == other.type &&
               pairwiseCipher == other.pairwiseCipher &&
               groupCipher == other.groupCipher &&
               authType == other.authType;
    }
    
    bool operator!=(const SecurityInfo& other) const {
        return !(*this == other);
    }
    
    /**
     * @brief Check if this represents a downgrade from another security config
     */
    bool isDowngradeFrom(const SecurityInfo& other) const {
        // Open is always a downgrade from anything encrypted
        if (type == SecurityType::Open && other.type != SecurityType::Open) {
            return true;
        }
        // WEP is a downgrade from WPA/WPA2/WPA3
        if (type == SecurityType::WEP && 
            (other.type == SecurityType::WPA || 
             other.type == SecurityType::WPA2 || 
             other.type == SecurityType::WPA3)) {
            return true;
        }
        // WPA is a downgrade from WPA2/WPA3
        if (type == SecurityType::WPA && 
            (other.type == SecurityType::WPA2 || other.type == SecurityType::WPA3)) {
            return true;
        }
        // WPA2 is a downgrade from WPA3
        if (type == SecurityType::WPA2 && other.type == SecurityType::WPA3) {
            return true;
        }
        return false;
    }
};

/**
 * @brief PHY capabilities (802.11n/ac/ax)
 */
struct Capabilities {
    bool ht : 1;    // 802.11n (High Throughput)
    bool vht : 1;   // 802.11ac (Very High Throughput)
    bool he : 1;    // 802.11ax (High Efficiency)
    uint8_t _reserved : 5;
    
    Capabilities() : ht(false), vht(false), he(false), _reserved(0) {}
    
    bool operator==(const Capabilities& other) const {
        return ht == other.ht && vht == other.vht && he == other.he;
    }
};

// ============================================
// Main Observation Structure
// ============================================

/**
 * @brief A single captured 802.11 frame observation
 * 
 * This structure is designed to be:
 * - Fixed size (no dynamic allocation)
 * - Cache-friendly (frequently accessed fields first)
 * - Memory efficient for embedded use
 */
struct Observation {
    // Timestamp (milliseconds since boot)
    uint32_t timestamp = 0;
    
    // Frame classification
    FrameType frameType = FrameType::Unknown;
    
    // Channel this frame was observed on
    uint8_t channel = 0;
    
    // Signal strength (dBm, typically -100 to 0)
    int8_t rssi = 0;
    
    // Padding for alignment
    uint8_t _pad1 = 0;
    
    // SSID (null-terminated, empty string if not present/hidden)
    char ssid[DUPIN_SSID_MAX_LEN + 1] = {0};
    
    // MAC addresses
    uint8_t bssid[MAC_LEN] = {0};
    uint8_t transmitterMac[MAC_LEN] = {0};
    uint8_t receiverMac[MAC_LEN] = {0};
    
    // Security information (from beacons/probe responses)
    SecurityInfo security;
    
    // PHY capabilities
    Capabilities capabilities;
    
    // Beacon interval (in TUs, 1 TU = 1024 microseconds)
    // Only valid for beacon frames
    uint16_t beaconInterval = 0;
    
    // ============================================
    // Helper Methods
    // ============================================
    
    /**
     * @brief Check if this observation has a valid SSID
     */
    bool hasSSID() const {
        return ssid[0] != '\0';
    }
    
    /**
     * @brief Check if this observation has a valid BSSID
     */
    bool hasBSSID() const {
        return !macIsNull(bssid);
    }
    
    /**
     * @brief Check if this is a management frame
     */
    bool isManagementFrame() const {
        switch (frameType) {
            case FrameType::Beacon:
            case FrameType::ProbeRequest:
            case FrameType::ProbeResponse:
            case FrameType::Association:
            case FrameType::Reassociation:
            case FrameType::Disassociation:
            case FrameType::Deauthentication:
            case FrameType::Action:
                return true;
            default:
                return false;
        }
    }
    
    /**
     * @brief Set the SSID (safely, with truncation)
     */
    void setSSID(const char* newSSID) {
        if (newSSID == nullptr) {
            ssid[0] = '\0';
            return;
        }
        strncpy(ssid, newSSID, DUPIN_SSID_MAX_LEN);
        ssid[DUPIN_SSID_MAX_LEN] = '\0';  // Ensure null termination
    }
    
    /**
     * @brief Clear/reset the observation
     */
    void clear() {
        timestamp = 0;
        frameType = FrameType::Unknown;
        channel = 0;
        rssi = 0;
        ssid[0] = '\0';
        macClear(bssid);
        macClear(transmitterMac);
        macClear(receiverMac);
        security = SecurityInfo{};
        capabilities = Capabilities{};
        beaconInterval = 0;
    }
};

// ============================================
// Utility Functions
// ============================================

/**
 * @brief Get string name for frame type
 */
inline const char* frameTypeName(FrameType type) {
    switch (type) {
        case FrameType::Beacon: return "Beacon";
        case FrameType::ProbeRequest: return "ProbeReq";
        case FrameType::ProbeResponse: return "ProbeResp";
        case FrameType::Association: return "Assoc";
        case FrameType::Reassociation: return "Reassoc";
        case FrameType::Disassociation: return "Disassoc";
        case FrameType::Deauthentication: return "Deauth";
        case FrameType::Data: return "Data";
        case FrameType::Action: return "Action";
        default: return "Unknown";
    }
}

/**
 * @brief Get string name for security type
 */
inline const char* securityTypeName(SecurityType type) {
    switch (type) {
        case SecurityType::Open: return "Open";
        case SecurityType::WEP: return "WEP";
        case SecurityType::WPA: return "WPA";
        case SecurityType::WPA2: return "WPA2";
        case SecurityType::WPA3: return "WPA3";
        default: return "Unknown";
    }
}

} // namespace dupin
