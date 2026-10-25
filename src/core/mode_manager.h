/**
 * @file mode_manager.h
 * @brief Mode Manager - Orchestration of Detective Modes
 * 
 * Coordinates all detective modes, manages mode transitions,
 * and broadcasts observations to active modes.
 */

#pragma once

#include "modes/mode.h"
#include "core/observation.h"
#include "utils/time_utils.h"

namespace dupin {

// Maximum registered modes
constexpr size_t MAX_MODES = 8;

/**
 * @brief Mode Manager Configuration
 */
struct ModeManagerConfig {
    bool allowMultipleActive = true;  // Allow multiple modes to receive observations
    uint32_t updateIntervalMs = 100;  // How often to call update() on modes
};

/**
 * @brief Mode Manager - The Conductor
 * 
 * Orchestrates all detective modes, routing observations and
 * managing the lifecycle of each mode.
 */
class ModeManager {
public:
    ModeManager();
    
    /**
     * @brief Register a mode with the manager
     */
    bool registerMode(IMode* mode);
    
    /**
     * @brief Get a registered mode by name
     */
    IMode* getMode(const char* name);
    const IMode* getMode(const char* name) const;
    
    /**
     * @brief Set a mode as the primary active mode
     */
    bool setActiveMode(const char* name);
    
    /**
     * @brief Get the primary active mode
     */
    IMode* getActiveMode() { return m_primaryMode; }
    const IMode* getActiveMode() const { return m_primaryMode; }
    
    /**
     * @brief Start a specific mode
     */
    bool startMode(const char* name);
    
    /**
     * @brief Stop a specific mode
     */
    bool stopMode(const char* name);
    
    /**
     * @brief Start all registered modes
     */
    void startAll();
    
    /**
     * @brief Stop all registered modes
     */
    void stopAll();
    
    /**
     * @brief Broadcast an observation to all active modes
     */
    void broadcastObservation(const Observation& obs);
    
    /**
     * @brief Call update() on all active modes
     */
    void update();
    
    /**
     * @brief Get count of registered modes
     */
    size_t getModeCount() const { return m_modeCount; }
    
    /**
     * @brief Get count of active modes
     */
    size_t getActiveModeCount() const;
    
    /**
     * @brief Get configuration
     */
    const ModeManagerConfig& getConfig() const { return m_config; }
    void setConfig(const ModeManagerConfig& config) { m_config = config; }
    
    /**
     * @brief Get mode by index (for iteration)
     */
    IMode* getModeAt(size_t index);
    const IMode* getModeAt(size_t index) const;

private:
    ModeManagerConfig m_config;
    IMode* m_modes[MAX_MODES] = {nullptr};
    size_t m_modeCount = 0;
    IMode* m_primaryMode = nullptr;
    uint32_t m_lastUpdateTime = 0;
};

} // namespace dupin
