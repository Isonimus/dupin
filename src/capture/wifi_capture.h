/**
 * @file wifi_capture.h
 * @brief ESP32 WiFi promiscuous mode capture
 * 
 * Handles WiFi initialization, promiscuous mode setup,
 * and channel hopping for passive frame capture.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>

#ifndef NATIVE_BUILD
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#endif

namespace dupin {

/**
 * @brief Callback type for received frames
 * 
 * @param data Raw frame data
 * @param len Frame length
 * @param rssi Signal strength
 * @param channel Channel frame was received on
 */
using FrameCallback = std::function<void(const uint8_t* data, size_t len, int8_t rssi, uint8_t channel)>;

/**
 * @brief WiFi capture configuration
 */
struct CaptureConfig {
    uint8_t startChannel = 1;           // First channel to scan
    uint8_t endChannel = 13;            // Last channel to scan (13 for EU, 11 for US)
    uint32_t channelHopIntervalMs = 100; // Time on each channel
    bool enableChannelHopping = true;    // If false, stay on startChannel
};

/**
 * @brief WiFi frame capture in promiscuous mode
 * 
 * Manages the ESP32 WiFi radio for passive monitoring.
 * Captures management frames (beacons, probes, etc.) and
 * passes them to a callback for processing.
 */
class WiFiCapture {
public:
    WiFiCapture();
    ~WiFiCapture();
    
    // Prevent copying
    WiFiCapture(const WiFiCapture&) = delete;
    WiFiCapture& operator=(const WiFiCapture&) = delete;
    
    /**
     * @brief Start WiFi capture
     * 
     * @param config Capture configuration
     * @return true on success
     */
    bool start(const CaptureConfig& config = CaptureConfig{});
    
    /**
     * @brief Stop WiFi capture
     */
    void stop();
    
    /**
     * @brief Check if capture is running
     */
    bool isRunning() const { return m_running; }
    
    /**
     * @brief Set the frame callback
     * 
     * @param callback Function to call for each captured frame
     */
    void setCallback(FrameCallback callback);
    
    /**
     * @brief Get current channel
     */
    uint8_t getCurrentChannel() const { return m_currentChannel; }
    
    /**
     * @brief Set channel manually
     * 
     * @param channel WiFi channel (1-14)
     * @return true if channel was set successfully
     */
    bool setChannel(uint8_t channel);
    
    /**
     * @brief Update function - call periodically for channel hopping
     * 
     * Should be called from the main loop if channel hopping is enabled.
     */
    void update();
    
    /**
     * @brief Get capture statistics
     */
    struct Stats {
        uint32_t framesReceived = 0;
        uint32_t beacons = 0;
        uint32_t probeRequests = 0;
        uint32_t probeResponses = 0;
        uint32_t deauths = 0;
        uint32_t otherManagement = 0;
        uint32_t dataFrames = 0;
        uint32_t channelHops = 0;
    };
    
    const Stats& getStats() const { return m_stats; }
    void resetStats();

private:
#ifndef NATIVE_BUILD
    /**
     * @brief Promiscuous mode callback (static, called by ESP-IDF)
     */
    static void promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type);
#endif

    /**
     * @brief Process a received frame
     */
    void processFrame(const uint8_t* data, size_t len, int8_t rssi);
    
    /**
     * @brief Hop to next channel
     */
    void hopChannel();

    // Configuration
    CaptureConfig m_config;
    
    // State
    bool m_running = false;
    uint8_t m_currentChannel = 1;
    uint32_t m_lastChannelHop = 0;
    
    // Callback
    FrameCallback m_callback;
    
    // Statistics
    Stats m_stats;
    
    // Singleton instance for static callback
    static WiFiCapture* s_instance;
};

} // namespace dupin
