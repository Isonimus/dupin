/**
 * @file text_wrap.h
 * @brief Word-wrapping for the popup/detail canvases (ADR-0005).
 *
 * Extracted from main.cpp so the wrap algorithm is host-testable. wrapText() is
 * pure — no display dependency — and printWrapped() renders wrapped lines to a
 * Canvas. Splitting the two is what lets pio test -e native reach the logic.
 */
#pragma once

#include <cstddef>

#include "hal/display/canvas_types.h"

namespace dupin {

// Line-buffer bound. Popup callers wrap well under this (a ~145 px popup at the
// approximate size-1 glyph width is ~24 chars); the renderer resets a line at the
// bound rather than overrunning the buffer.
constexpr int kMaxLineChars = 48;

/**
 * @brief Greedy, space-delimited word wrap. Pure; no display, no allocation.
 *
 * Splits @p text into lines of at most @p maxCharsPerLine characters and calls
 * @p emitLine(const char*) once per line, in order. Runs of spaces collapse; a
 * word longer than the limit is emitted on its own line rather than split. The
 * template keeps the firmware path zero-overhead (no std::function).
 *
 * @p maxCharsPerLine is clamped to kMaxLineChars: the line buffer is the hard
 * ceiling, so a wider request simply wraps at kMaxLineChars. Words that overflow
 * a line move to the next one — never dropped — and the buffer can never overflow.
 */
template <typename EmitLine>
void wrapText(const char* text, int maxCharsPerLine, EmitLine&& emitLine) {
    if (text == nullptr || maxCharsPerLine <= 0) return;

    // The line buffer is the hard ceiling: a wider request would let lineLen reach
    // kMaxLineChars before the break test (against maxCharsPerLine) ever fires, at
    // which point the copy guards silently drop every remaining word. Clamp so the
    // wrap width and the buffer coincide — extra text moves to the next line, never
    // vanishes.
    if (maxCharsPerLine > kMaxLineChars) maxCharsPerLine = kMaxLineChars;

    char line[kMaxLineChars + 1] = {0};
    int lineLen = 0;

    const char* p = text;
    while (*p != '\0') {
        // Extract the next word, bounded by the line buffer.
        char word[kMaxLineChars + 1];
        int wordLen = 0;
        while (*p != '\0' && *p != ' ' && wordLen < kMaxLineChars) {
            word[wordLen++] = *p++;
        }
        word[wordLen] = '\0';
        while (*p == ' ') ++p;  // collapse runs of spaces

        // Does the word still fit on the current line? (+1 for the joining space.)
        if (lineLen > 0 && lineLen + 1 + wordLen > maxCharsPerLine) {
            emitLine(line);
            line[0] = '\0';
            lineLen = 0;
        }

        if (lineLen > 0 && lineLen < kMaxLineChars) {
            line[lineLen++] = ' ';
        }
        for (int i = 0; i < wordLen && lineLen < kMaxLineChars; ++i) {
            line[lineLen++] = word[i];
        }
        line[lineLen] = '\0';
    }

    if (lineLen > 0) emitLine(line);
}

/**
 * @brief Wrap @p text to @p maxWidth px and render each line to @p canvas from
 *        (@p x, @p y), advancing by @p lineHeight. Returns the y below the last
 *        line.
 */
int printWrapped(Canvas& canvas, int x, int y, const char* text,
                 int maxWidth, int lineHeight);

// Appended by fitText() when it shortens a string. Three ASCII dots rather than a
// single U+2026: the built-in font is ASCII, so an ellipsis code point would render
// as whatever occupies that slot, and a shortening marker that is itself unreadable
// defeats the point of showing one (ADR-0013).
constexpr char kFitMarker[] = "...";
constexpr int kFitMarkerChars = 3;

/**
 * @brief Copy @p text into @p dst, shortened to @p maxChars with a visible marker.
 *
 * For text that fits, an exact copy. For text that does not, the first
 * (@p maxChars - kFitMarkerChars) characters followed by kFitMarker, so the reader
 * can tell a shortened string from a complete one -- which is the defect this exists
 * to close: a silently truncated name looks like a real name (ADR-0013).
 *
 * Pure; no display, no allocation. @p dstSize is honoured as a second, independent
 * ceiling, so the buffer cannot overflow however wrong @p maxChars is, and the source
 * is read under a bound rather than to its terminator -- an unterminated @p text
 * cannot walk off the end of its object. Always NUL-terminates when @p dstSize > 0.
 *
 * A @p maxChars too small to hold the marker hard-truncates instead: there is no room
 * to signal with, and emitting only a marker would say less than the characters it
 * replaced.
 */
void fitText(char* dst, size_t dstSize, const char* text, int maxChars);

} // namespace dupin
