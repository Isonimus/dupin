/**
 * @file mac_utils.h
 * @brief MAC address utilities
 * 
 * Functions for parsing, formatting, and comparing MAC addresses.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace dupin {

// MAC address length in bytes
constexpr size_t MAC_LEN = 6;

// Broadcast MAC address
constexpr uint8_t MAC_BROADCAST[MAC_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Null MAC address
constexpr uint8_t MAC_NULL[MAC_LEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/**
 * @brief Compare two MAC addresses for equality
 * 
 * @param a First MAC address (6 bytes)
 * @param b Second MAC address (6 bytes)
 * @return true if addresses are equal
 */
inline bool macEquals(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, MAC_LEN) == 0;
}

/**
 * @brief Check if MAC address is the broadcast address (FF:FF:FF:FF:FF:FF)
 * 
 * @param mac MAC address to check (6 bytes)
 * @return true if broadcast
 */
inline bool macIsBroadcast(const uint8_t* mac) {
    return macEquals(mac, MAC_BROADCAST);
}

/**
 * @brief Check if MAC address is null (00:00:00:00:00:00)
 * 
 * @param mac MAC address to check (6 bytes)
 * @return true if null
 */
inline bool macIsNull(const uint8_t* mac) {
    return macEquals(mac, MAC_NULL);
}

/**
 * @brief Check if MAC address is a multicast address
 * 
 * Multicast addresses have the least significant bit of the first byte set.
 * 
 * @param mac MAC address to check (6 bytes)
 * @return true if multicast
 */
inline bool macIsMulticast(const uint8_t* mac) {
    return (mac[0] & 0x01) != 0;
}

/**
 * @brief Check if MAC address is locally administered
 * 
 * Locally administered addresses have bit 1 of the first byte set.
 * 
 * @param mac MAC address to check (6 bytes)
 * @return true if locally administered
 */
inline bool macIsLocallyAdministered(const uint8_t* mac) {
    return (mac[0] & 0x02) != 0;
}

/**
 * @brief Copy a MAC address
 * 
 * @param dst Destination buffer (6 bytes)
 * @param src Source MAC address (6 bytes)
 */
inline void macCopy(uint8_t* dst, const uint8_t* src) {
    memcpy(dst, src, MAC_LEN);
}

/**
 * @brief Set a MAC address to null (00:00:00:00:00:00)
 * 
 * @param mac MAC address to clear (6 bytes)
 */
inline void macClear(uint8_t* mac) {
    memset(mac, 0, MAC_LEN);
}

/**
 * @brief Format MAC address as string (XX:XX:XX:XX:XX:XX)
 * 
 * @param mac MAC address to format (6 bytes)
 * @param out Output buffer (must be at least 18 bytes)
 * @param uppercase If true, use uppercase hex (default: true)
 */
inline void macFormat(const uint8_t* mac, char* out, bool uppercase = true) {
    const char* fmt = uppercase ? "%02X:%02X:%02X:%02X:%02X:%02X" 
                                : "%02x:%02x:%02x:%02x:%02x:%02x";
    snprintf(out, 18, fmt, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * @brief Parse MAC address from string
 * 
 * Supports formats:
 * - XX:XX:XX:XX:XX:XX (colon separated)
 * - XX-XX-XX-XX-XX-XX (dash separated)
 * 
 * @param str Input string
 * @param out Output buffer (6 bytes)
 * @return true if parsing succeeded
 */
inline bool macParse(const char* str, uint8_t* out) {
    if (str == nullptr || out == nullptr) {
        return false;
    }
    
    unsigned int values[6];
    int matched;
    
    // Try colon format first
    matched = sscanf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
                     &values[0], &values[1], &values[2],
                     &values[3], &values[4], &values[5]);
    
    if (matched != 6) {
        // Try dash format
        matched = sscanf(str, "%02x-%02x-%02x-%02x-%02x-%02x",
                         &values[0], &values[1], &values[2],
                         &values[3], &values[4], &values[5]);
    }
    
    if (matched != 6) {
        return false;
    }
    
    // Validate values are in range
    for (int i = 0; i < 6; i++) {
        if (values[i] > 255) {
            return false;
        }
        out[i] = static_cast<uint8_t>(values[i]);
    }
    
    return true;
}

/**
 * @brief Get the OUI (Organizationally Unique Identifier) from a MAC
 * 
 * The OUI is the first 3 bytes of the MAC address.
 * 
 * @param mac MAC address (6 bytes)
 * @param oui Output buffer (3 bytes)
 */
inline void macGetOUI(const uint8_t* mac, uint8_t* oui) {
    memcpy(oui, mac, 3);
}

} // namespace dupin
