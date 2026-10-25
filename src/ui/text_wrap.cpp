/**
 * @file text_wrap.cpp
 * @brief Canvas renderer for wrapped text (ADR-0005).
 */
#include "ui/text_wrap.h"

#include <cstring>

// For kGlyphWidthPx -- geometry constants only, no display driver (ADR-0019).
#include "ui/display.h"

namespace dupin {

// Approximate glyph advance for the built-in font at text size 1, in pixels.
// The renderer wraps by character count rather than measured glyph metrics
// because the popup callers all use size-1 text and an exact per-glyph measure
// would couple this pure logic back to the display. Named per the no-magic-values
// rule; it is a deliberate approximation, documented so it is not "fixed" into a
// display coupling by a later reader (ADR-0005).
// One statement of the size-1 glyph advance, shared with the toast and case-menu
// ceilings that derive from it (ADR-0019).
static constexpr int kApproxCharWidthPx = kGlyphWidthPx;

int printWrapped(Canvas& canvas, int x, int y, const char* text,
                 int maxWidth, int lineHeight) {
    const int maxCharsPerLine = maxWidth / kApproxCharWidthPx;
    wrapText(text, maxCharsPerLine, [&](const char* line) {
        canvas.setCursor(x, y);
        canvas.println(line);
        y += lineHeight;
    });
    return y;
}

void fitText(char* dst, size_t dstSize, const char* text, int maxChars) {
    if (dst == nullptr || dstSize == 0) return;
    if (text == nullptr || maxChars <= 0) {
        dst[0] = '\0';
        return;
    }

    // Two ceilings, and the tighter one wins. maxChars is what the caller can display;
    // dstSize is what it can hold. Honouring both means a caller cannot overflow the
    // buffer by getting the display arithmetic wrong -- the failure this function exists
    // to make impossible should not be reachable through this function either.
    size_t limit = static_cast<size_t>(maxChars);
    if (limit > dstSize - 1) limit = dstSize - 1;

    // Bounded scan rather than strlen: one past the limit is all the answer needed, and
    // stopping there means an unterminated source is read no further than its own field.
    size_t len = 0;
    while (len <= limit && text[len] != '\0') ++len;

    if (len <= limit) {
        memcpy(dst, text, len);
        dst[len] = '\0';
        return;
    }

    const size_t markerChars = static_cast<size_t>(kFitMarkerChars);
    if (limit <= markerChars) {
        // No room to signal the shortening: a marker alone would say less than the
        // characters it replaced, so keep the characters.
        memcpy(dst, text, limit);
        dst[limit] = '\0';
        return;
    }

    const size_t kept = limit - markerChars;
    memcpy(dst, text, kept);
    memcpy(dst + kept, kFitMarker, markerChars);
    dst[limit] = '\0';
}

} // namespace dupin
