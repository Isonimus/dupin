/**
 * @file canvas_types.h
 * @brief Platform-agnostic off-screen canvas type (ADR-0005).
 *
 * Every display surface resolves through dupin::Canvas so rendering code names
 * one type instead of M5Canvas directly. It is the single seam a second board
 * plugs into, and the reason rendering logic can be compiled and asserted on the
 * host. New rendering code uses dupin::Canvas, never M5Canvas directly
 * (CLAUDE.md §4).
 */
#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <vector>

namespace dupin {

/**
 * @brief Host stub for the off-screen canvas.
 *
 * Deliberately not a no-op: it records each line emitted through println() so a
 * host test can assert what would have been rendered. A green compile is not
 * verification in this repo (ADR-0002), so the stub exists to make rendering
 * logic assertable, not merely linkable. Only the members that host-compiled
 * code calls are defined; it grows with real need, not ahead of it.
 */
class Canvas {
public:
    void setCursor(int x, int y) { cursorX_ = x; cursorY_ = y; }

    void println(const char* text) {
        recorded_.emplace_back(text ? text : "");
    }

    // Test accessors — the recorded render.
    const std::vector<std::string>& lines() const { return recorded_; }
    int cursorX() const { return cursorX_; }
    int cursorY() const { return cursorY_; }

private:
    std::vector<std::string> recorded_;
    int cursorX_ = 0;
    int cursorY_ = 0;
};

} // namespace dupin

#else // device build

#include <M5Cardputer.h>

namespace dupin {

// M5 targets (Cardputer, Cardputer ADV) draw through M5GFX's M5Canvas.
//
// A future LovyanGFX board (Lilygo T-Embed) would add an arm here resolving
// Canvas to LGFX_Sprite; that port is deferred (LEDGER, ADR-0005). The full
// IDisplay interface HAL a non-M5 board needs is deferred with it — this typedef
// is the display-type seam, not the whole abstraction.
using Canvas = M5Canvas;

} // namespace dupin

#endif
