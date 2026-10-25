/**
 * @file wifi_capture.cpp
 * @brief ESP32 WiFi promiscuous mode capture implementation
 */

#include "capture/wifi_capture.h"

#ifndef NATIVE_BUILD
#include <Arduino.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#endif

namespace dupin {

// Static instance pointer for callback
WiFiCapture* WiFiCapture::s_instance = nullptr;

WiFiCapture::WiFiCapture() {
    s_instance = this;
}

WiFiCapture::~WiFiCapture() {
    stop();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool WiFiCapture::start(const CaptureConfig& config) {
    if (m_running) {
        return true;  // Already running
    }
    
    m_config = config;
    
#ifndef NATIVE_BUILD
    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    
    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        return false;
    }
    
    // Set WiFi mode to station (required for promiscuous)
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        return false;
    }
    
    // Start WiFi
    if (esp_wifi_start() != ESP_OK) {
        return false;
    }
    
    // Configure promiscuous mode filter
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT |  // Management frames
                       WIFI_PROMIS_FILTER_MASK_DATA     // Data frames (header only)
    };
    esp_wifi_set_promiscuous_filter(&filter);
    
    // Set the callback
    esp_wifi_set_promiscuous_rx_cb(promiscuousCallback);
    
    // Enable promiscuous mode
    if (esp_wifi_set_promiscuous(true) != ESP_OK) {
        return false;
    }
    
    // Set initial channel
    setChannel(m_config.startChannel);
#endif
    
    m_running = true;
    m_lastChannelHop = millis();
    resetStats();
    
    return true;
}

void WiFiCapture::stop() {
    if (!m_running) {
        return;
    }
    
#ifndef NATIVE_BUILD
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_stop();
    esp_wifi_deinit();
#endif
    
    m_running = false;
}

void WiFiCapture::setCallback(FrameCallback callback) {
    m_callback = callback;
}

bool WiFiCapture::setChannel(uint8_t channel) {
    if (channel < 1 || channel > 14) {
        return false;
    }
    
#ifndef NATIVE_BUILD
    if (esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        return false;
    }
#endif
    
    m_currentChannel = channel;
    return true;
}

void WiFiCapture::update() {
    if (!m_running || !m_config.enableChannelHopping) {
        return;
    }
    
#ifndef NATIVE_BUILD
    uint32_t now = millis();
    if (now - m_lastChannelHop >= m_config.channelHopIntervalMs) {
        hopChannel();
        m_lastChannelHop = now;
    }
#endif
}

void WiFiCapture::hopChannel() {
    uint8_t nextChannel = m_currentChannel + 1;
    
    if (nextChannel > m_config.endChannel) {
        nextChannel = m_config.startChannel;
    }
    
    setChannel(nextChannel);
    m_stats.channelHops++;
}

void WiFiCapture::resetStats() {
    m_stats = Stats{};
}

#ifndef NATIVE_BUILD
void WiFiCapture::promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (s_instance == nullptr) {
        return;
    }
    
    const wifi_promiscuous_pkt_t* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
    const wifi_pkt_rx_ctrl_t& rx_ctrl = pkt->rx_ctrl;
    
    // Extract frame data
    const uint8_t* data = pkt->payload;
    size_t len = rx_ctrl.sig_len;
    int8_t rssi = rx_ctrl.rssi;
    
    s_instance->processFrame(data, len, rssi);
}
#endif

void WiFiCapture::processFrame(const uint8_t* data, size_t len, int8_t rssi) {
    if (len < 24) {  // Minimum 802.11 header size
        return;
    }
    
    m_stats.framesReceived++;
    
    // Get frame control field
    uint8_t frameType = (data[0] >> 2) & 0x03;
    uint8_t frameSubtype = (data[0] >> 4) & 0x0F;
    
    // Track by type
    if (frameType == 0) {  // Management frame
        switch (frameSubtype) {
            case 0x08:  // Beacon
                m_stats.beacons++;
                break;
            case 0x04:  // Probe Request
                m_stats.probeRequests++;
                break;
            case 0x05:  // Probe Response
                m_stats.probeResponses++;
                break;
            case 0x0C:  // Deauthentication
            case 0x0A:  // Disassociation
                m_stats.deauths++;
                break;
            default:
                m_stats.otherManagement++;
                break;
        }
    } else if (frameType == 2) {  // Data frame
        m_stats.dataFrames++;
    }
    
    // Call user callback
    if (m_callback) {
        m_callback(data, len, rssi, m_currentChannel);
    }
}

} // namespace dupin
