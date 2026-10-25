/**
 * @file baseline.h
 * @brief Baseline data structures for Watson mode
 * 
 * The Baseline captures the "normal" state of the wireless environment.
 * Marple mode uses this to detect anomalies.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include "dupin.h"
#include "utils/mac_utils.h"
#include "core/observation.h"

namespace dupin {

// ============================================
// SSID Baseline Entry
// ============================================

/**
 * @brief Statistics for a single SSID
 * 
 * Captures the normal behavior patterns for an SSID,
 * including known BSSIDs, channels, signal strength, and security.
 */
struct SSIDBaseline {
    // SSID name
    char ssid[DUPIN_SSID_MAX_LEN + 1] = {0};
    
    // Known BSSIDs that advertise this SSID
    uint8_t knownBSSIDs[DUPIN_MAX_BSSIDS_PER_SSID][MAC_LEN] = {{0}};
    uint8_t bssidCount = 0;
    
    // Channels where this SSID has been observed
    uint8_t channels[DUPIN_MAX_CHANNELS] = {0};
    uint8_t channelCount = 0;
    
    // RSSI statistics
    int8_t rssiMin = 0;
    int8_t rssiMax = -128;
    // 64 bits because the sum is persisted and runs across a case's whole life: 32 overflowed,
    // undefined, one observation past 2^31 / 128 at the floor (ADR-0029).
    int64_t rssiSum = 0;
    uint32_t rssiCount = 0;
    // No count this type can hold drives the sum past 64 bits; widening the count re-opens it.
    static constexpr int64_t kLargestSampleMagnitude = 128;
    static_assert(UINT32_MAX <= INT64_MAX / kLargestSampleMagnitude,
                  "rssiSum cannot hold rssiCount samples at the int8_t floor (ADR-0029)");
    
    // Expected security configuration
    SecurityInfo security;
    
    // Timestamps and session tracking
    uint32_t firstSeen = 0;
    uint32_t lastSeen = 0;
    uint32_t beaconCount = 0;
    uint32_t lastSeenSession = 0;  // For stale SSID expiration
    
    // ============================================
    // Methods
    // ============================================
    
    /**
     * @brief Check if this entry is in use
     */
    bool isActive() const {
        return ssid[0] != '\0';
    }
    
    /**
     * @brief Set the SSID (with safe copy)
     */
    void setSSID(const char* newSSID) {
        if (newSSID == nullptr) {
            ssid[0] = '\0';
            return;
        }
        strncpy(ssid, newSSID, DUPIN_SSID_MAX_LEN);
        ssid[DUPIN_SSID_MAX_LEN] = '\0';
    }
    
    /**
     * @brief Add a BSSID to the known list
     * @return true if added, false if already exists or list is full
     */
    bool addBSSID(const uint8_t* bssid) {
        // Check if already exists
        for (uint8_t i = 0; i < bssidCount; i++) {
            if (macEquals(knownBSSIDs[i], bssid)) {
                return false;  // Already known
            }
        }
        
        // Check capacity
        if (bssidCount >= DUPIN_MAX_BSSIDS_PER_SSID) {
            return false;  // Full
        }
        
        macCopy(knownBSSIDs[bssidCount], bssid);
        bssidCount++;
        return true;
    }
    
    /**
     * @brief Result of filtered BSSID addition
     */
    enum class AddResult : uint8_t {
        Added,              // Successfully added
        AlreadyExists,      // BSSID already in baseline  
        SecurityMismatch,   // Security doesn't match baseline (evil twin!)
        Full                // No space for more BSSIDs
    };
    
    /**
     * @brief Add a BSSID with security filtering
     * Rejects OPEN APs for WPA2 networks (catches evil twins)
     */
    AddResult addBSSIDFiltered(const uint8_t* bssid, SecurityType newSecurity) {
        // Check if already exists
        for (uint8_t i = 0; i < bssidCount; i++) {
            if (macEquals(knownBSSIDs[i], bssid)) {
                return AddResult::AlreadyExists;
            }
        }
        
        // Security filtering: reject if baseline has security but new BSSID is OPEN
        if (security.type != SecurityType::Unknown && 
            security.type != SecurityType::Open && 
            newSecurity == SecurityType::Open) {
            // This is likely an evil twin - legitimate AP has security but this one is OPEN
            return AddResult::SecurityMismatch;
        }
        
        // Also reject weaker security (WPA3 baseline but WPA1 new)
        if (security.type == SecurityType::WPA3 && 
            (newSecurity == SecurityType::WPA || newSecurity == SecurityType::WEP)) {
            return AddResult::SecurityMismatch;
        }
        
        // Check capacity
        if (bssidCount >= DUPIN_MAX_BSSIDS_PER_SSID) {
            return AddResult::Full;
        }
        
        macCopy(knownBSSIDs[bssidCount], bssid);
        bssidCount++;
        return AddResult::Added;
    }
    
    /**
     * @brief Check if a BSSID is known for this SSID
     */
    bool hasBSSID(const uint8_t* bssid) const {
        for (uint8_t i = 0; i < bssidCount; i++) {
            if (macEquals(knownBSSIDs[i], bssid)) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Add a channel to the known list
     * @return true if added, false if already exists or list is full
     */
    bool addChannel(uint8_t channel) {
        // Check if already exists
        for (uint8_t i = 0; i < channelCount; i++) {
            if (channels[i] == channel) {
                return false;  // Already known
            }
        }
        
        // Check capacity
        if (channelCount >= DUPIN_MAX_CHANNELS) {
            return false;
        }
        
        channels[channelCount] = channel;
        channelCount++;
        return true;
    }
    
    /**
     * @brief Check if a channel is known for this SSID
     */
    bool hasChannel(uint8_t channel) const {
        for (uint8_t i = 0; i < channelCount; i++) {
            if (channels[i] == channel) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Update RSSI statistics with new observation
     */
    void updateRSSI(int8_t rssi) {
        if (rssi > rssiMax) rssiMax = rssi;
        if (rssi < rssiMin) rssiMin = rssi;
        rssiSum += rssi;
        rssiCount++;
    }
    
    /**
     * @brief Get average RSSI
     */
    int8_t rssiAverage() const {
        if (rssiCount == 0) return 0;
        return static_cast<int8_t>(rssiSum / static_cast<int64_t>(rssiCount));
    }
    
    /**
     * @brief Check if an RSSI value is within the normal range
     * @param rssi Value to check
     * @param toleranceDb Tolerance in dB (default 20)
     */
    bool isRSSIInRange(int8_t rssi, int8_t toleranceDb = 20) const {
        if (rssiCount == 0) return true;  // No baseline yet
        return rssi >= (rssiMin - toleranceDb) && rssi <= (rssiMax + toleranceDb);
    }
    
    /**
     * @brief Update from an observation
     * @return True if observation was accepted, false if security mismatch (evil twin detected)
     */
    bool updateFromObservation(const Observation& obs, uint32_t currentSession = 0) {
        if (!isActive()) {
            // Initialize from first observation
            setSSID(obs.ssid);
            firstSeen = obs.timestamp;
            security = obs.security;
        }
        
        lastSeen = obs.timestamp;
        if (currentSession > 0) {
            lastSeenSession = currentSession;
        }
        
        bool accepted = true;
        if (obs.hasBSSID()) {
            // Use security-filtered addition to catch evil twins
            AddResult result = addBSSIDFiltered(obs.bssid, obs.security.type);
            if (result == AddResult::SecurityMismatch) {
                // Don't add this BSSID - let Marple detect it as anomaly
                accepted = false;
            }
        }
        
        if (obs.channel > 0) {
            addChannel(obs.channel);
        }
        
        updateRSSI(obs.rssi);
        
        // Update security if still unknown and observation has valid security
        if (security.type == SecurityType::Unknown && 
            obs.security.type != SecurityType::Unknown) {
            security = obs.security;
        }
        
        if (obs.frameType == FrameType::Beacon) {
            beaconCount++;
        }
        
        return accepted;
    }
    
    /**
     * @brief Clear/reset the entry
     */
    void clear() {
        ssid[0] = '\0';
        bssidCount = 0;
        channelCount = 0;
        rssiMin = 0;
        rssiMax = -128;
        rssiSum = 0;
        rssiCount = 0;
        security = SecurityInfo{};
        firstSeen = 0;
        lastSeen = 0;
        beaconCount = 0;
    }
};

// ============================================
// Baseline Manager
// ============================================

/**
 * @brief Container for all SSID baselines
 * 
 * Fixed-size storage for baseline data. No dynamic allocation.
 */
class Baseline {
public:
    Baseline() : m_count(0), m_currentSession(0) {}
    
    /**
     * @brief Find or create baseline entry for an SSID
     * @return Pointer to entry, or nullptr if full and SSID not found
     */
    SSIDBaseline* getOrCreate(const char* ssid) {
        // First, try to find existing
        SSIDBaseline* entry = find(ssid);
        if (entry != nullptr) {
            return entry;
        }
        
        // Try to find empty slot
        for (size_t i = 0; i < DUPIN_MAX_SSIDS; i++) {
            if (!m_entries[i].isActive()) {
                m_entries[i].setSSID(ssid);
                m_count++;
                return &m_entries[i];
            }
        }
        
        return nullptr;  // Full
    }
    
    /**
     * @brief Find baseline entry for an SSID
     * @return Pointer to entry, or nullptr if not found
     */
    SSIDBaseline* find(const char* ssid) {
        if (ssid == nullptr) return nullptr;
        
        for (size_t i = 0; i < DUPIN_MAX_SSIDS; i++) {
            if (m_entries[i].isActive() && 
                strcmp(m_entries[i].ssid, ssid) == 0) {
                return &m_entries[i];
            }
        }
        return nullptr;
    }
    
    /**
     * @brief Find baseline entry for an SSID (const version)
     */
    const SSIDBaseline* find(const char* ssid) const {
        if (ssid == nullptr) return nullptr;
        
        for (size_t i = 0; i < DUPIN_MAX_SSIDS; i++) {
            if (m_entries[i].isActive() && 
                strcmp(m_entries[i].ssid, ssid) == 0) {
                return &m_entries[i];
            }
        }
        return nullptr;
    }
    
    /**
     * @brief Update baseline from an observation
     * @param obs The observation
     * @param currentSession Current session number for expiration tracking
     * @return true if accepted, false if security mismatch (let Marple detect)
     */
    bool update(const Observation& obs, uint32_t currentSession = 0) {
        if (!obs.hasSSID()) {
            return false;  // Can't baseline without SSID
        }
        
        SSIDBaseline* entry = getOrCreate(obs.ssid);
        if (entry == nullptr) {
            return false;  // Full
        }
        
        return entry->updateFromObservation(obs, currentSession);
    }
    
    /**
     * @brief Set current session number for all active entries
     */
    void setCurrentSession(uint32_t session) {
        m_currentSession = session;
    }
    
    /**
     * @brief Expire entries not seen in recent sessions
     * @param threshold Number of sessions after which to expire
     * @return Number of entries expired
     */
    size_t expireStaleEntries(uint32_t threshold) {
        if (m_currentSession < threshold) return 0;  // Not enough sessions yet
        
        size_t expired = 0;
        uint32_t expireBeforeSession = m_currentSession - threshold;
        
        for (size_t i = 0; i < DUPIN_MAX_SSIDS; i++) {
            if (m_entries[i].isActive() && 
                m_entries[i].lastSeenSession > 0 &&
                m_entries[i].lastSeenSession < expireBeforeSession) {
                m_entries[i].clear();
                m_count--;
                expired++;
            }
        }
        return expired;
    }
    
    uint32_t getCurrentSession() const { return m_currentSession; }
    
    /**
     * @brief Get entry by index (for iteration)
     */
    SSIDBaseline* at(size_t index) {
        if (index >= DUPIN_MAX_SSIDS) return nullptr;
        return &m_entries[index];
    }
    
    const SSIDBaseline* at(size_t index) const {
        if (index >= DUPIN_MAX_SSIDS) return nullptr;
        return &m_entries[index];
    }
    
    /**
     * @brief Get current number of active entries
     */
    size_t count() const { return m_count; }
    
    /**
     * @brief Get maximum capacity
     */
    constexpr size_t capacity() const { return DUPIN_MAX_SSIDS; }
    
    /**
     * @brief Clear all entries
     */
    void clear() {
        for (size_t i = 0; i < DUPIN_MAX_SSIDS; i++) {
            m_entries[i].clear();
        }
        m_count = 0;
    }
    
private:
    SSIDBaseline m_entries[DUPIN_MAX_SSIDS];
    size_t m_count;
    uint32_t m_currentSession;
};

} // namespace dupin
