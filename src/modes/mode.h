/**
 * @file mode.h
 * @brief Base interface for all Dupin detective modes
 * 
 * All modes (Watson, Marple, Poirot, Holmes, Baskerville)
 * implement this interface for uniform observation handling.
 */

#pragma once

#include "core/observation.h"

namespace dupin {

/**
 * @brief Abstract interface for detector modes
 * 
 * Each mode processes observations and maintains its own state.
 * Modes are registered with the ModeManager and receive
 * observations as they are captured.
 */
class IMode {
public:
    virtual ~IMode() = default;
    
    /**
     * @brief Get the mode's display name
     */
    virtual const char* name() const = 0;
    
    /**
     * @brief Called when a new observation is captured
     * 
     * @param obs The observation to process
     */
    virtual void onObservation(const Observation& obs) = 0;
    
    /**
     * @brief Called periodically for time-based operations
     * 
     * Called from the main loop at regular intervals.
     * Use for confidence decay, periodic saves, etc.
     */
    virtual void update() = 0;
    
    /**
     * @brief Initialize the mode
     * 
     * Called once when the mode is activated.
     * Load any saved state here.
     */
    virtual void begin() {}
    
    /**
     * @brief Clean up the mode
     * 
     * Called when the mode is deactivated.
     * Save any state that needs to persist.
     */
    virtual void end() {}
    
    /**
     * @brief Check if the mode is currently active
     */
    virtual bool isActive() const { return m_active; }
    
    /**
     * @brief Set the mode's active state
     */
    virtual void setActive(bool active) { m_active = active; }
    
protected:
    bool m_active = false;
};

} // namespace dupin
