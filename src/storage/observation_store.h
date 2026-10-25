/**
 * @file observation_store.h
 * @brief Observation storage with ring buffer and statistics
 * 
 * Stores recent observations and maintains statistics
 * about captured frames.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include "dupin.h"
#include "core/observation.h"
#include "storage/ring_buffer.h"

namespace dupin {

// ============================================
// Observation Statistics
// ============================================

/**
 * @brief Statistics about captured observations
 */
struct ObservationStats {
    // Frame type counts
    uint32_t beacons = 0;
    uint32_t probeRequests = 0;
    uint32_t probeResponses = 0;
    uint32_t deauths = 0;
    uint32_t otherManagement = 0;
    uint32_t dataFrames = 0;
    
    // Totals
    uint32_t totalFrames = 0;
    uint32_t uniqueSSIDs = 0;
    uint32_t uniqueBSSIDs = 0;
    
    // Timestamps
    uint32_t firstFrameTime = 0;
    uint32_t lastFrameTime = 0;
    
    // RSSI stats
    int8_t rssiMin = 0;
    int8_t rssiMax = -128;
    int64_t rssiSum = 0;  // every frame adds to it, so 32 bits overflowed in weeks (ADR-0029)
    
    /**
     * @brief Get average RSSI
     */
    int8_t rssiAverage() const {
        if (totalFrames == 0) return 0;
        return static_cast<int8_t>(rssiSum / static_cast<int64_t>(totalFrames));
    }
    
    /**
     * @brief Reset all statistics
     */
    void reset() {
        *this = ObservationStats{};
    }
};

// ============================================
// Observation Store
// ============================================

/**
 * @brief Ring buffer storage for observations with statistics tracking
 * 
 * Stores the most recent observations in a fixed-size ring buffer.
 * Automatically tracks frame statistics as observations are added.
 */
template <size_t Capacity = DUPIN_MAX_OBSERVATIONS>
class ObservationStore {
public:
    ObservationStore() = default;
    
    /**
     * @brief Add an observation to the store
     * 
     * @param obs Observation to store
     * @return true if an old observation was overwritten
     */
    bool push(const Observation& obs) {
        updateStats(obs);
        return m_buffer.push(obs);
    }
    
    /**
     * @brief Get the oldest observation
     * 
     * @param out Pointer to store the observation
     * @return true if store is not empty
     */
    bool peek(Observation* out) const {
        return m_buffer.peek(out);
    }
    
    /**
     * @brief Get the most recent observation
     * 
     * @param out Pointer to store the observation
     * @return true if store is not empty
     */
    bool peekNewest(Observation* out) const {
        return m_buffer.peekNewest(out);
    }
    
    /**
     * @brief Get observation at index (0 = oldest)
     * 
     * @param index Index from oldest
     * @param out Pointer to store the observation
     * @return true if index is valid
     */
    bool at(size_t index, Observation* out) const {
        return m_buffer.at(index, out);
    }
    
    /**
     * @brief Get observation by index from newest (0 = newest)
     * 
     * @param index Index from newest
     * @param out Pointer to store the observation
     * @return true if index is valid
     */
    bool getRecent(size_t index, Observation* out) const {
        if (index >= m_buffer.size()) {
            return false;
        }
        // Convert from newest-first to oldest-first index
        size_t oldestFirstIndex = m_buffer.size() - 1 - index;
        return m_buffer.at(oldestFirstIndex, out);
    }
    
    /**
     * @brief Find observations matching a filter
     * 
     * @param filter Function that returns true for matching observations
     * @param out Array to store matching observations
     * @param maxResults Maximum number of results
     * @return Number of matches found
     */
    template <typename FilterFunc>
    size_t findAll(FilterFunc filter, Observation* out, size_t maxResults) const {
        size_t found = 0;
        
        for (const auto& obs : m_buffer) {
            if (filter(obs)) {
                if (out != nullptr && found < maxResults) {
                    out[found] = obs;
                }
                found++;
                if (found >= maxResults) break;
            }
        }
        
        return found;
    }
    
    /**
     * @brief Count observations matching a filter
     */
    template <typename FilterFunc>
    size_t count(FilterFunc filter) const {
        size_t count = 0;
        for (const auto& obs : m_buffer) {
            if (filter(obs)) count++;
        }
        return count;
    }
    
    /**
     * @brief Get current number of stored observations
     */
    size_t size() const { return m_buffer.size(); }
    
    /**
     * @brief Get maximum capacity
     */
    constexpr size_t capacity() const { return Capacity; }
    
    /**
     * @brief Check if store is empty
     */
    bool isEmpty() const { return m_buffer.isEmpty(); }
    
    /**
     * @brief Check if store is full
     */
    bool isFull() const { return m_buffer.isFull(); }
    
    /**
     * @brief Clear all observations and reset statistics
     */
    void clear() {
        m_buffer.clear();
        m_stats.reset();
    }
    
    /**
     * @brief Get statistics
     */
    const ObservationStats& getStats() const { return m_stats; }
    
    /**
     * @brief Reset statistics (without clearing observations)
     */
    void resetStats() { m_stats.reset(); }
    
    // Iterator support
    auto begin() const { return m_buffer.begin(); }
    auto end() const { return m_buffer.end(); }

private:
    /**
     * @brief Update statistics from an observation
     */
    void updateStats(const Observation& obs) {
        m_stats.totalFrames++;
        
        // Frame type counts
        switch (obs.frameType) {
            case FrameType::Beacon:
                m_stats.beacons++;
                break;
            case FrameType::ProbeRequest:
                m_stats.probeRequests++;
                break;
            case FrameType::ProbeResponse:
                m_stats.probeResponses++;
                break;
            case FrameType::Deauthentication:
            case FrameType::Disassociation:
                m_stats.deauths++;
                break;
            case FrameType::Data:
                m_stats.dataFrames++;
                break;
            default:
                m_stats.otherManagement++;
                break;
        }
        
        // Timestamps
        if (m_stats.totalFrames == 1) {
            m_stats.firstFrameTime = obs.timestamp;
        }
        m_stats.lastFrameTime = obs.timestamp;
        
        // RSSI
        if (obs.rssi > m_stats.rssiMax) m_stats.rssiMax = obs.rssi;
        if (obs.rssi < m_stats.rssiMin) m_stats.rssiMin = obs.rssi;
        m_stats.rssiSum += obs.rssi;
        
        // Note: uniqueSSIDs/uniqueBSSIDs would require additional tracking
        // structures which are expensive. For now, leave at 0 and track
        // in Baseline instead.
    }

    RingBuffer<Observation, Capacity> m_buffer;
    ObservationStats m_stats;
};

} // namespace dupin
