/**
 * @file frame_parser.cpp
 * @brief 802.11 frame parser implementation
 */

#include "capture/frame_parser.h"
#include "utils/mac_utils.h"
#include <cstring>

namespace dupin {

// WPA OUI for vendor-specific WPA IE
static const uint8_t WPA_OUI[] = {0x00, 0x50, 0xF2, 0x01};

bool FrameParser::parse(const uint8_t* data, size_t len, int8_t rssi,
                        uint8_t channel, Observation& out) {
    if (data == nullptr || len < MIN_MGMT_HEADER_SIZE) {
        return false;
    }
    
    // Clear output
    out.clear();
    
    // Set basic fields
    out.rssi = rssi;
    out.channel = channel;
    
    // Get frame type
    out.frameType = getFrameType(data[0], data[1]);
    
    if (out.frameType == FrameType::Unknown) {
        return false;  // We only parse known types
    }
    
    // Extract MAC addresses
    extractAddresses(data, out);
    
    // Parse frame-specific data
    switch (out.frameType) {
        case FrameType::Beacon:
            return parseBeacon(data, len, out);
            
        case FrameType::ProbeRequest:
            return parseProbeRequest(data, len, out);
            
        case FrameType::ProbeResponse:
            return parseProbeResponse(data, len, out);
            
        case FrameType::Association:
        case FrameType::Reassociation:
        case FrameType::Disassociation:
        case FrameType::Deauthentication:
        case FrameType::Action:
        case FrameType::Data:
            // Minimal parsing - just addresses
            return true;
            
        default:
            return false;
    }
}

void FrameParser::extractAddresses(const uint8_t* header, Observation& out) {
    // 802.11 management frame header:
    // Bytes 0-1: Frame Control
    // Bytes 2-3: Duration
    // Bytes 4-9: Address 1 (Receiver/Destination)
    // Bytes 10-15: Address 2 (Transmitter/Source)
    // Bytes 16-21: Address 3 (BSSID)
    // Bytes 22-23: Sequence Control
    
    macCopy(out.receiverMac, header + 4);
    macCopy(out.transmitterMac, header + 10);
    macCopy(out.bssid, header + 16);
}

bool FrameParser::parseBeacon(const uint8_t* data, size_t len, Observation& out) {
    // Beacon frame:
    // 24 bytes header + 12 bytes fixed parameters + tagged parameters
    
    size_t offset = MIN_MGMT_HEADER_SIZE;
    
    if (len < offset + BEACON_FIXED_PARAMS_SIZE) {
        return false;
    }
    
    // Fixed parameters at offset 24:
    // Timestamp: 8 bytes (skip)
    // Beacon Interval: 2 bytes
    // Capability Info: 2 bytes
    
    // Extract beacon interval (in TUs)
    out.beaconInterval = data[offset + 8] | (data[offset + 9] << 8);
    
    // Parse capability info for basic security detection
    uint16_t capabilityInfo = data[offset + 10] | (data[offset + 11] << 8);
    bool privacy = (capabilityInfo & 0x0010) != 0;  // Privacy bit
    
    // Default security based on privacy bit
    if (privacy) {
        out.security.type = SecurityType::WEP;  // Will be upgraded if RSN/WPA found
    } else {
        out.security.type = SecurityType::Open;
    }
    
    offset += BEACON_FIXED_PARAMS_SIZE;
    
    // Parse information elements
    parseInformationElements(data + offset, len - offset, out);
    
    return true;
}

bool FrameParser::parseProbeRequest(const uint8_t* data, size_t len, Observation& out) {
    // Probe request: header + tagged parameters (no fixed params)
    
    if (len <= MIN_MGMT_HEADER_SIZE) {
        return true;  // Empty probe request is valid
    }
    
    // Parse information elements
    parseInformationElements(data + MIN_MGMT_HEADER_SIZE, 
                             len - MIN_MGMT_HEADER_SIZE, out);
    
    // Probe requests don't have security info
    out.security.type = SecurityType::Unknown;
    
    return true;
}

bool FrameParser::parseProbeResponse(const uint8_t* data, size_t len, Observation& out) {
    // Probe response has same format as beacon
    return parseBeacon(data, len, out);
}

void FrameParser::parseInformationElements(const uint8_t* data, size_t len, Observation& out) {
    size_t offset = 0;
    
    while (offset + 2 <= len) {
        uint8_t tag = data[offset];
        uint8_t tagLen = data[offset + 1];
        
        if (offset + 2 + tagLen > len) {
            break;  // Invalid IE, stop parsing
        }
        
        const uint8_t* tagData = data + offset + 2;
        
        switch (tag) {
            case IE_SSID:
                // SSID
                if (tagLen > 0 && tagLen <= DUPIN_SSID_MAX_LEN) {
                    memcpy(out.ssid, tagData, tagLen);
                    out.ssid[tagLen] = '\0';
                }
                break;
                
            case IE_DS_PARAMETER:
                // DS Parameter Set - contains channel
                if (tagLen >= 1) {
                    // Note: We already have channel from capture, but this
                    // tells us the AP's configured channel
                }
                break;
                
            case IE_RSN:
                // RSN (WPA2/WPA3) Information Element
                parseRSN(tagData, tagLen, out.security);
                break;
                
            case IE_HT_CAPABILITIES:
                // 802.11n High Throughput
                out.capabilities.ht = true;
                break;
                
            case IE_VHT_CAPABILITIES:
                // 802.11ac Very High Throughput
                out.capabilities.vht = true;
                break;
                
            case IE_VENDOR:
                // Vendor specific - check for WPA1
                parseVendorIE(tagData, tagLen, out.security);
                break;
                
            default:
                // Ignore other IEs
                break;
        }
        
        offset += 2 + tagLen;
    }
}

void FrameParser::parseRSN(const uint8_t* data, size_t len, SecurityInfo& out) {
    // RSN Information Element format:
    // Version (2) + Group Cipher Suite (4) + Pairwise Cipher Count (2) +
    // Pairwise Cipher Suites (4*n) + AKM Count (2) + AKM Suites (4*n) + ...
    
    if (len < 8) {
        return;  // Too short
    }
    
    // At minimum, seeing RSN means WPA2
    out.type = SecurityType::WPA2;
    
    // Skip version (2 bytes)
    size_t offset = 2;
    
    // Group cipher suite (4 bytes: OUI + type)
    if (offset + 4 <= len) {
        uint8_t groupCipherType = data[offset + 3];
        switch (groupCipherType) {
            case 1: out.groupCipher = CipherType::WEP40; break;
            case 2: out.groupCipher = CipherType::TKIP; break;
            case 4: out.groupCipher = CipherType::CCMP; break;
            case 5: out.groupCipher = CipherType::WEP104; break;
            default: break;
        }
        offset += 4;
    }
    
    // Pairwise cipher count
    if (offset + 2 > len) return;
    uint16_t pairwiseCount = data[offset] | (data[offset + 1] << 8);
    offset += 2;
    
    // Pairwise cipher suites
    if (pairwiseCount > 0 && offset + 4 <= len) {
        uint8_t pairwiseCipherType = data[offset + 3];
        switch (pairwiseCipherType) {
            case 2: out.pairwiseCipher = CipherType::TKIP; break;
            case 4: out.pairwiseCipher = CipherType::CCMP; break;
            case 8: out.pairwiseCipher = CipherType::GCMP; break;
            default: break;
        }
        offset += 4 * pairwiseCount;
    }
    
    // AKM count
    if (offset + 2 > len) return;
    uint16_t akmCount = data[offset] | (data[offset + 1] << 8);
    offset += 2;
    
    // AKM suites
    if (akmCount > 0 && offset + 4 <= len) {
        uint8_t akmType = data[offset + 3];
        switch (akmType) {
            case 1: out.authType = AuthType::Enterprise; break;  // 802.1X
            case 2: out.authType = AuthType::PSK; break;         // PSK
            case 8: 
                out.type = SecurityType::WPA3;  // SAE
                out.authType = AuthType::SAE;
                break;
            default: break;
        }
    }
}

void FrameParser::parseVendorIE(const uint8_t* data, size_t len, SecurityInfo& out) {
    // Check for WPA OUI: 00:50:F2:01
    if (len < 4) {
        return;
    }
    
    if (memcmp(data, WPA_OUI, 4) == 0) {
        // This is WPA1 (if RSN wasn't found, or in addition to RSN)
        if (out.type == SecurityType::WEP || out.type == SecurityType::Unknown) {
            out.type = SecurityType::WPA;
        }
        // WPA parsing is similar to RSN but starts after OUI+type
        // For simplicity, we just mark it as WPA
    }
}

} // namespace dupin
