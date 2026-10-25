/**
 * @file mode_manager.cpp
 * @brief Mode Manager implementation
 */

#include "core/mode_manager.h"
#include "ui/serial_logger.h"
#include <cstring>

namespace dupin {

ModeManager::ModeManager() {
    for (size_t i = 0; i < MAX_MODES; i++) {
        m_modes[i] = nullptr;
    }
}

bool ModeManager::registerMode(IMode* mode) {
    if (mode == nullptr) return false;
    
    // Check if already registered
    for (size_t i = 0; i < m_modeCount; i++) {
        if (m_modes[i] == mode || 
            strcmp(m_modes[i]->name(), mode->name()) == 0) {
            return false;  // Already registered
        }
    }
    
    // Check capacity
    if (m_modeCount >= MAX_MODES) {
        logger.logError("ModeManager: Max modes reached");
        return false;
    }
    
    m_modes[m_modeCount++] = mode;
    logger.logInfo("ModeManager: Registered mode");
    Serial.printf("  %s\n", mode->name());
    
    return true;
}

IMode* ModeManager::getMode(const char* name) {
    if (name == nullptr) return nullptr;
    
    for (size_t i = 0; i < m_modeCount; i++) {
        if (strcmp(m_modes[i]->name(), name) == 0) {
            return m_modes[i];
        }
    }
    return nullptr;
}

const IMode* ModeManager::getMode(const char* name) const {
    if (name == nullptr) return nullptr;
    
    for (size_t i = 0; i < m_modeCount; i++) {
        if (strcmp(m_modes[i]->name(), name) == 0) {
            return m_modes[i];
        }
    }
    return nullptr;
}

bool ModeManager::setActiveMode(const char* name) {
    IMode* mode = getMode(name);
    if (mode == nullptr) {
        logger.logWarning("ModeManager: Mode not found");
        Serial.printf("  %s\n", name);
        return false;
    }
    
    m_primaryMode = mode;
    
    // Ensure it's started
    if (!mode->isActive()) {
        mode->begin();
    }
    
    logger.logStatus("ModeManager: Active mode set");
    Serial.printf("  %s\n", name);
    
    return true;
}

bool ModeManager::startMode(const char* name) {
    IMode* mode = getMode(name);
    if (mode == nullptr) return false;
    
    if (!mode->isActive()) {
        mode->begin();
    }
    return true;
}

bool ModeManager::stopMode(const char* name) {
    IMode* mode = getMode(name);
    if (mode == nullptr) return false;
    
    if (mode->isActive()) {
        mode->end();
    }
    
    // Clear primary if it was this mode
    if (m_primaryMode == mode) {
        m_primaryMode = nullptr;
    }
    
    return true;
}

void ModeManager::startAll() {
    logger.logStatus("ModeManager: Starting all modes...");
    
    for (size_t i = 0; i < m_modeCount; i++) {
        if (!m_modes[i]->isActive()) {
            m_modes[i]->begin();
        }
    }
    
    Serial.printf("  %zu modes active\n", getActiveModeCount());
}

void ModeManager::stopAll() {
    logger.logStatus("ModeManager: Stopping all modes...");
    
    for (size_t i = 0; i < m_modeCount; i++) {
        if (m_modes[i]->isActive()) {
            m_modes[i]->end();
        }
    }
    
    m_primaryMode = nullptr;
}

void ModeManager::broadcastObservation(const Observation& obs) {
    for (size_t i = 0; i < m_modeCount; i++) {
        if (m_modes[i]->isActive()) {
            m_modes[i]->onObservation(obs);
        }
    }
}

void ModeManager::update() {
    uint32_t now = getTimestamp();
    
    // Rate limit updates
    if (now - m_lastUpdateTime < m_config.updateIntervalMs) {
        return;
    }
    m_lastUpdateTime = now;
    
    // Update all active modes
    for (size_t i = 0; i < m_modeCount; i++) {
        if (m_modes[i]->isActive()) {
            m_modes[i]->update();
        }
    }
}

size_t ModeManager::getActiveModeCount() const {
    size_t count = 0;
    for (size_t i = 0; i < m_modeCount; i++) {
        if (m_modes[i]->isActive()) count++;
    }
    return count;
}

IMode* ModeManager::getModeAt(size_t index) {
    if (index >= m_modeCount) return nullptr;
    return m_modes[index];
}

const IMode* ModeManager::getModeAt(size_t index) const {
    if (index >= m_modeCount) return nullptr;
    return m_modes[index];
}

} // namespace dupin
