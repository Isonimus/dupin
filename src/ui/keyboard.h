/**
 * @file keyboard.h
 * @brief Keyboard input handler for M5Cardputer
 * 
 * Handles keyboard input and maps keys to Dupin actions.
 */

#pragma once

#include <cstdint>

namespace dupin {

/**
 * @brief Input key codes
 */
enum class Key : uint8_t {
    None = 0,
    
    // Mode initials (direct switch)
    W = 'w',  // Watson
    M = 'm',  // Marple
    P = 'p',  // Poirot
    H = 'h',  // Holmes
    B = 'b',  // Baskerville
    D = 'd',  // Dupin
    
    // Navigation
    Up = 0x80,
    Down = 0x81,
    Left = 0x82,
    Right = 0x83,
    
    // Actions
    Enter = 0x0D,  // Start/select
    Escape = 0x1B, // Stop/back
    Space = 0x20,  // Toggle
    
    // Numeric
    Key0 = '0', Key1 = '1', Key2 = '2', Key3 = '3', Key4 = '4',
    Key5 = '5', Key6 = '6', Key7 = '7', Key8 = '8', Key9 = '9',
};

/**
 * @brief Keyboard input handler
 */
class Keyboard {
public:
    /**
     * @brief Initialize keyboard
     */
    bool init();
    
    /**
     * @brief Update keyboard state (call in loop)
     */
    void update();
    
    /**
     * @brief Check if a key was just pressed
     */
    bool wasPressed(Key key) const;
    
    /**
     * @brief Check if any key was pressed
     */
    bool anyKeyPressed() const { return m_lastKey != Key::None; }
    
    /**
     * @brief Get the last key pressed
     */
    Key getLastKey() const { return m_lastKey; }
    
    /**
     * @brief Clear key state
     */
    void clearKey() { m_lastKey = Key::None; }

private:
    Key m_lastKey = Key::None;
    bool m_keyPressed = false;
};

} // namespace dupin
