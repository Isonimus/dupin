/**
 * @file case_list.h
 * @brief The case menu's list bound and its scroll window (ADR-0023).
 *
 * Pure arithmetic, kept out of main.cpp so the native env can reach it: the window
 * decides which cases a six-row panel shows, and before it existed the renderer drew
 * the first seven and the selection walked off the end of them unseen.
 */
#pragma once

#include <cstddef>

#include "ui/display.h"

namespace dupin {

// How many cases the menu caches from the card.
//
// The menu is a modal loop that repaints every 50 ms (ADR-0019), so it reads the card once
// into an array rather than per frame; this is that array's length, and the only bound on
// how many cases the operator can reach. Its cost is a SessionMeta each -- 44 bytes, so
// 704 bytes of DRAM at sixteen -- which is what makes raising it a decision rather than an
// edit. A card holding more is not silently truncated: listSessions() reports it and the
// menu title marks it (ADR-0023).
constexpr size_t kMaxListedCases = 16;

// Decimal width of @p value, for sizing a counter whose ceiling is known at compile time.
constexpr size_t decimalDigits(size_t value) {
    return value < 10 ? 1 : 1 + decimalDigits(value / 10);
}

// Widest "<first>-<last>/<count>+" the title counter can render. Every figure is bounded by
// kMaxListedCases, so this is the panel-fit claim; the snprintf buffer is a separate and
// wider number, because the compiler bounds a %u by the widest uint32_t (ADR-0013).
constexpr size_t kCaseCounterMaxChars = 3 * decimalDigits(kMaxListedCases) + sizeof("-/+") - 1;

/**
 * @brief The slice of the case list that is drawn.
 */
struct CaseListWindow {
    size_t first = 0;  ///< Index of the first case drawn.
    size_t count = 0;  ///< How many cases are drawn, starting at `first`.
};

/**
 * @brief Which cases a @p visibleRows -row panel shows when @p selectedCase is selected.
 *
 * Paged, not centred: `first` is a multiple of @p visibleRows, so the selection keeps its
 * position on screen and the page turns once per @p visibleRows moves rather than the list
 * sliding under the cursor on every keypress. Stateless for the same reason the list itself
 * is re-read: the modal menu refreshes after every create, rename and delete, and a
 * remembered scroll offset is one more thing to keep in step with a list that changed length.
 *
 * The selection is always inside the returned window -- that is the property the renderer
 * needs, because a highlight bar is only drawn for a row the loop reaches (ADR-0023).
 *
 * A @p selectedCase past the end is clamped to the last case; an empty list or a panel with
 * no rows returns an empty window.
 */
CaseListWindow caseListWindow(size_t selectedCase, size_t caseCount, size_t visibleRows);

} // namespace dupin
