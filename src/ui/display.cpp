/**
 * @file display.cpp
 * @brief Display implementation for M5Cardputer
 */

#include "ui/display.h"

#ifndef NATIVE_BUILD
#include <M5Cardputer.h>
#endif

namespace dupin {

#ifndef NATIVE_BUILD
bool M5Display::init() {
    // M5Cardputer handles display init in begin()
    M5Cardputer.Display.setRotation(1);  // Landscape mode
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(Colors::White, Colors::Black);
    clear();
    return true;
}

void M5Display::clear() {
    M5Cardputer.Display.fillScreen(Colors::Black);
}

void M5Display::refresh() {
    // M5GFX auto-updates
}

void M5Display::setCursor(int x, int y) {
    M5Cardputer.Display.setCursor(x, y);
}

void M5Display::print(const char* text) {
    M5Cardputer.Display.print(text);
}

void M5Display::println(const char* text) {
    M5Cardputer.Display.println(text);
}

void M5Display::setTextColor(uint16_t fg, uint16_t bg) {
    M5Cardputer.Display.setTextColor(fg, bg);
}

void M5Display::setTextSize(uint8_t size) {
    M5Cardputer.Display.setTextSize(size);
}

void M5Display::fillRect(int x, int y, int w, int h, uint16_t color) {
    M5Cardputer.Display.fillRect(x, y, w, h, color);
}

void M5Display::drawProgressBar(int x, int y, int w, int h, 
                                  int8_t value, int8_t min, int8_t max,
                                  uint16_t fgColor, uint16_t bgColor) {
    // Background
    M5Cardputer.Display.fillRect(x, y, w, h, bgColor);
    
    // Calculate fill width
    int range = max - min;
    if (range == 0) range = 1;
    int fillW = ((value - min) * w) / range;
    if (fillW < 0) fillW = 0;
    if (fillW > w) fillW = w;
    
    // Foreground
    if (fillW > 0) {
        M5Cardputer.Display.fillRect(x, y, fillW, h, fgColor);
    }
    
    // Border
    M5Cardputer.Display.drawRect(x, y, w, h, Colors::White);
}
#endif

} // namespace dupin
