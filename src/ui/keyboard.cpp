/**
 * @file keyboard.cpp
 * @brief Keyboard implementation for M5Cardputer
 */

#include "ui/keyboard.h"

#ifndef NATIVE_BUILD
#include <M5Cardputer.h>
#endif

namespace dupin {

bool Keyboard::init() {
#ifndef NATIVE_BUILD
    // M5Cardputer keyboard is initialized via M5.begin()
    return true;
#else
    return true;
#endif
}

void Keyboard::update() {
    m_lastKey = Key::None;
    
#ifndef NATIVE_BUILD
    M5Cardputer.update();
    
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState keys = M5Cardputer.Keyboard.keysState();
        
        // Check for special keys first
        if (M5Cardputer.Keyboard.isKeyPressed(0x0D)) {  // Enter
            m_lastKey = Key::Enter;
        } else if (M5Cardputer.Keyboard.isKeyPressed(0x08)) {  // Backspace
            m_lastKey = Key::Escape;  // Use backspace as escape
        } else if (M5Cardputer.Keyboard.isKeyPressed(';')) {
            m_lastKey = Key::Up;
        } else if (M5Cardputer.Keyboard.isKeyPressed('.')) {
            m_lastKey = Key::Down;
        } else if (M5Cardputer.Keyboard.isKeyPressed('/')) {
            m_lastKey = Key::Right;
        } else if (M5Cardputer.Keyboard.isKeyPressed(',')) {
            m_lastKey = Key::Left;
        } else if (M5Cardputer.Keyboard.isKeyPressed(' ')) {
            m_lastKey = Key::Space;
        } else {
            // Check letter keys for mode switching
            for (char c = 'a'; c <= 'z'; c++) {
                if (M5Cardputer.Keyboard.isKeyPressed(c)) {
                    m_lastKey = static_cast<Key>(c);
                    break;
                }
            }
            if (m_lastKey == Key::None) {
                for (char c = '0'; c <= '9'; c++) {
                    if (M5Cardputer.Keyboard.isKeyPressed(c)) {
                        m_lastKey = static_cast<Key>(c);
                        break;
                    }
                }
            }
        }
    }
#endif
}

bool Keyboard::wasPressed(Key key) const {
    return m_lastKey == key;
}

} // namespace dupin
