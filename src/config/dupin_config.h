/**
 * @file dupin_config.h
 * @brief Configuration file reader for dupin.conf on SD card
 * 
 * Reads configuration from /dupin/dupin.conf on SD card.
 * Auto-creates default config if not found.
 */

#pragma once

#include <cstdint>
#include <FS.h>
#include <SD.h>

namespace dupin {

/**
 * @brief Dupin configuration settings
 */
struct DupinConfig {
    // Baseline management
    uint32_t staleSessionThreshold = 7;  // Sessions before SSID expires
    uint32_t baselineAutosaveInterval = 60;  // Seconds between autosaves (0 = disabled)
    
    // Future settings can be added here
    // uint32_t captureChannelDwellMs = 100;
};

/**
 * @brief Create default config file on SD card
 * @return true if created successfully
 */
inline bool createDefaultConfig() {
    const char* configPath = "/dupin/dupin.conf";
    
    // Create directory if needed
    if (!SD.exists("/dupin")) {
        SD.mkdir("/dupin");
    }
    
    File file = SD.open(configPath, FILE_WRITE);
    if (!file) {
        Serial.println("[CONFIG] Failed to create dupin.conf");
        return false;
    }
    
    file.println("# Dupin Configuration File");
    file.println("# ");
    file.println("# Baseline Management");
    file.println("# stale_session_threshold: Sessions without seeing SSID before expiring");
    file.println("stale_session_threshold=7");
    file.println("");
    file.println("# baseline_autosave_interval: Seconds between autosaves (0 = disabled)");
    file.println("baseline_autosave_interval=60");
    
    file.close();
    Serial.println("[CONFIG] Created default dupin.conf");
    return true;
}

/**
 * @brief Load configuration from SD card
 * Auto-creates default config if not found.
 * @param config Configuration struct to populate
 * @return true if file was read, false if failed
 */
inline bool loadConfig(DupinConfig& config) {
    const char* configPath = "/dupin/dupin.conf";
    
    // Auto-create if doesn't exist
    if (!SD.exists(configPath)) {
        Serial.println("[CONFIG] No dupin.conf found, creating default...");
        if (!createDefaultConfig()) {
            return false;
        }
    }
    
    File file = SD.open(configPath, FILE_READ);
    if (!file) {
        Serial.println("[CONFIG] Failed to open dupin.conf");
        return false;
    }
    
    Serial.println("[CONFIG] Loading dupin.conf");
    
    char line[128];
    while (file.available()) {
        size_t len = file.readBytesUntil('\n', line, sizeof(line) - 1);
        line[len] = '\0';
        
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\0' || line[0] == '\n') {
            continue;
        }
        
        // Parse key=value
        char* eq = strchr(line, '=');
        if (eq == nullptr) continue;
        
        *eq = '\0';
        char* key = line;
        char* value = eq + 1;
        
        // Trim whitespace
        while (*key == ' ') key++;
        while (*value == ' ') value++;
        char* end = value + strlen(value) - 1;
        while (end > value && (*end == ' ' || *end == '\r' || *end == '\n')) {
            *end-- = '\0';
        }
        
        // Parse known keys
        if (strcmp(key, "stale_session_threshold") == 0) {
            config.staleSessionThreshold = static_cast<uint32_t>(atoi(value));
            Serial.printf("[CONFIG] stale_session_threshold = %u\n",
                          static_cast<unsigned>(config.staleSessionThreshold));
        } else if (strcmp(key, "baseline_autosave_interval") == 0) {
            config.baselineAutosaveInterval = static_cast<uint32_t>(atoi(value));
            Serial.printf("[CONFIG] baseline_autosave_interval = %u\n",
                          static_cast<unsigned>(config.baselineAutosaveInterval));
        }
    }
    
    file.close();
    return true;
}

} // namespace dupin
