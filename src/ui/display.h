/**
 * @file display.h
 * @brief Display abstraction for Dupin UI
 * 
 * Provides a simple interface for displaying status, lists, and
 * hunt visualization on the M5Cardputer screen.
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace dupin {

// Screen dimensions for Cardputer (240x135)
constexpr int SCREEN_WIDTH = 240;
constexpr int SCREEN_HEIGHT = 135;

// Toast overlay geometry. The toast draws its text at size 2, whose built-in-font glyph
// advance is twice the size-1 kApproxCharWidthPx in ui/text_wrap.cpp, and the box adds a
// fixed padding either side.
//
// kToastMaxChars is derived from those two rather than written as a literal, because the
// renderer centres the box with (SCREEN_WIDTH - boxW) / 2: one character past this ceiling
// makes that origin negative and the box is drawn off the left edge of the panel. Nothing
// warns about it -- the text reaches the renderer through strncpy, not a format call -- so
// the ceiling has to be stated where both the renderer and ui/text_wrap's fitText() read
// it, or the two drift and the drift is invisible (ADR-0013).
//
// The size-1 advance is the shared root of all three: ui/text_wrap.cpp wraps by it, the
// toast is twice it, and the case-menu notice is at size 1 like the wrapper. Three readers
// of one number is where it stops being a local detail and becomes a constant.
constexpr int kGlyphWidthPx = 6;
constexpr int kToastGlyphWidthPx = kGlyphWidthPx * 2;
constexpr int kToastBoxPaddingPx = 20;
constexpr int kToastMaxChars = (SCREEN_WIDTH - kToastBoxPaddingPx) / kToastGlyphWidthPx;

// Case-menu notice geometry. The notice says why a case name was refused and is drawn at
// size 1 with a 10 px margin either side, flush left rather than centred -- so overrunning
// it does not mirror the toast's negative-origin failure, it simply runs off the right of
// the panel and the operator loses the end of the reason. Derived here for the same reason
// the toast ceiling is: the renderer and the code that fills the buffer must read one
// number (ADR-0019).
constexpr int kCaseNoticeMarginPx = 20;
constexpr int kCaseNoticeMaxChars = (SCREEN_WIDTH - kCaseNoticeMarginPx) / kGlyphWidthPx;

// The delete confirmation, and how much of the case's name it can carry. The question used to
// name nothing -- which case was about to go was carried only by the highlight bar, so the
// question and its subject were never in one sentence (ADR-0020). The notice is one line, so
// the name's ceiling is what the fixed words leave; derived here rather than written, because
// a reworded prompt must shorten the name rather than push it off the panel (ADR-0013). The
// words are short for the same reason: every character spent on them is a character of the
// operator's own case name they will not see.
constexpr char kDeletePromptPrefix[] = "Del ";
constexpr char kDeletePromptSuffix[] = "? ENTER=yes/any=no";
constexpr int kDeletePromptFixedChars =
    static_cast<int>(sizeof(kDeletePromptPrefix) - 1 + sizeof(kDeletePromptSuffix) - 1);
constexpr int kDeletePromptNameChars = kCaseNoticeMaxChars - kDeletePromptFixedChars;

// Footer geometry. The uptime sits at the right edge and the dropped-frame count, when there
// is one, sits immediately left of it -- both right-aligned, so the gap between them and the
// gap to the panel edge are one number rather than two literals that can drift apart.
constexpr int kFooterMarginPx = 2;

// Header row geometry. The row carries the open case's name from the left margin and the scan
// number against the right one. The scan number used to be drawn at a fixed x=180, where ten
// characters fit at size 1, so the field clipped at the panel edge as soon as the session number
// passed four digits (ADR-0013). Right-aligning it is the same treatment the uptime readout in
// the footer already gets.
constexpr int kPanelMarginPx = 4;
constexpr int kHeaderRowTopPx = 18;

/** @brief x at which a size-1 field of @p chars glyphs ends flush with the right margin. */
constexpr int rightAlignedX(int chars) {
    return SCREEN_WIDTH - chars * kGlyphWidthPx - kPanelMarginPx;
}

// The name and the scan number share one row, so the name's ceiling is whatever the number
// leaves: the label, the name and one glyph of clear space must all fit left of the number's
// origin. A 30-character case name reaches x=220 on its own, well past that origin, so without
// this the right-aligned number would be overdrawn by the name it was moved clear of.
constexpr int kHeaderLabelChars = 6;  // "Case: "
constexpr int kHeaderFieldGapChars = 1;

/** @brief How many characters of case name fit left of a @p scanChars-wide scan field. */
constexpr int headerNameMaxChars(int scanChars) {
    return (rightAlignedX(scanChars) - kPanelMarginPx) / kGlyphWidthPx
           - kHeaderLabelChars - kHeaderFieldGapChars;
}

// Case-menu list geometry. The rows run from under the title rule down to the footer, which
// carries the help line or the notice (ADR-0019).
constexpr int kGlyphHeightPx = 8;
constexpr int kCaseMenuListTopPx = 25;
constexpr int kCaseMenuRowHeightPx = 14;
constexpr int kCaseMenuFooterTopPx = 120;

// How many rows fit, derived rather than written: a row's text runs from its own top to one
// glyph height below it, so the last row that clears the footer is the (n-1)th at
// top + (n-1) * rowHeight + glyphHeight <= footer. The renderer used to draw seven case rows,
// which put the seventh at y=123 under a footer at y=120 -- the collision was worked around in
// a comment instead of being drawn one row shorter (ADR-0023).
constexpr int kCaseMenuRows =
    (kCaseMenuFooterTopPx - kCaseMenuListTopPx - kGlyphHeightPx) / kCaseMenuRowHeightPx + 1;

// Row 0 is the pinned "[+] New Case" action, so the cases get one row fewer.
constexpr int kCaseMenuCaseRows = kCaseMenuRows - 1;

// UI Colors
namespace Colors {
    constexpr uint16_t Black = 0x0000;
    constexpr uint16_t White = 0xFFFF;
    constexpr uint16_t Red = 0xF800;
    constexpr uint16_t Green = 0x07E0;
    constexpr uint16_t Blue = 0x001F;
    constexpr uint16_t Yellow = 0xFFE0;
    constexpr uint16_t Orange = 0xFD20;
    constexpr uint16_t Cyan = 0x07FF;
    constexpr uint16_t Gray = 0x7BEF;
    constexpr uint16_t DarkGray = 0x39E7;
    
    // Threat level colors
    constexpr uint16_t Critical = Red;
    constexpr uint16_t High = Orange;
    constexpr uint16_t Medium = Yellow;
    constexpr uint16_t Low = Green;
    constexpr uint16_t None = Gray;
}

/**
 * @brief Screen type for different views
 */
enum class ScreenType : uint8_t {
    Status = 0,      // Main status screen
    AnomalyList,     // List of detected anomalies
    IncidentList,    // List of incidents
    HypothesisList,  // List of hypotheses
    HuntMode,        // Baskerville hunt visualization
    Settings,        // Configuration screen
    About            // About/version screen
};

/**
 * @brief Display driver interface
 */
class IDisplay {
public:
    virtual ~IDisplay() = default;
    
    /**
     * @brief Initialize the display
     */
    virtual bool init() = 0;
    
    /**
     * @brief Clear the screen
     */
    virtual void clear() = 0;
    
    /**
     * @brief Refresh/push buffer to screen
     */
    virtual void refresh() = 0;
    
    /**
     * @brief Set text cursor position
     */
    virtual void setCursor(int x, int y) = 0;
    
    /**
     * @brief Print text at current cursor
     */
    virtual void print(const char* text) = 0;
    virtual void println(const char* text) = 0;
    
    /**
     * @brief Set text color
     */
    virtual void setTextColor(uint16_t fg, uint16_t bg = Colors::Black) = 0;
    
    /**
     * @brief Set text size (1-3)
     */
    virtual void setTextSize(uint8_t size) = 0;
    
    /**
     * @brief Draw a filled rectangle
     */
    virtual void fillRect(int x, int y, int w, int h, uint16_t color) = 0;
    
    /**
     * @brief Draw a horizontal progress bar
     */
    virtual void drawProgressBar(int x, int y, int w, int h, 
                                  int8_t value, int8_t min, int8_t max,
                                  uint16_t fgColor, uint16_t bgColor) = 0;
    
    /**
     * @brief Get screen width
     */
    virtual int width() const = 0;
    
    /**
     * @brief Get screen height
     */
    virtual int height() const = 0;
};

#ifndef NATIVE_BUILD
/**
 * @brief M5Cardputer display implementation using M5GFX
 */
class M5Display : public IDisplay {
public:
    bool init() override;
    void clear() override;
    void refresh() override;
    void setCursor(int x, int y) override;
    void print(const char* text) override;
    void println(const char* text) override;
    void setTextColor(uint16_t fg, uint16_t bg = Colors::Black) override;
    void setTextSize(uint8_t size) override;
    void fillRect(int x, int y, int w, int h, uint16_t color) override;
    void drawProgressBar(int x, int y, int w, int h, 
                          int8_t value, int8_t min, int8_t max,
                          uint16_t fgColor, uint16_t bgColor) override;
    int width() const override { return SCREEN_WIDTH; }
    int height() const override { return SCREEN_HEIGHT; }
};
#endif

/**
 * @brief Null display for native builds (no-op)
 */
class NullDisplay : public IDisplay {
public:
    bool init() override { return true; }
    void clear() override {}
    void refresh() override {}
    void setCursor(int, int) override {}
    void print(const char*) override {}
    void println(const char*) override {}
    void setTextColor(uint16_t, uint16_t = 0) override {}
    void setTextSize(uint8_t) override {}
    void fillRect(int, int, int, int, uint16_t) override {}
    void drawProgressBar(int, int, int, int, int8_t, int8_t, int8_t, uint16_t, uint16_t) override {}
    int width() const override { return SCREEN_WIDTH; }
    int height() const override { return SCREEN_HEIGHT; }
};

} // namespace dupin
