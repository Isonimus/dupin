---
id: '0023'
title: "The case list is bounded once, scrolls, and reports what it cannot show"
type: architecture
status: accepted
date: 2026-09-15
supersedes: []
superseded_by: []
---

## Context

`LEDGER.md` recorded one defect: `refreshCaseList()` lists at most a bare `16` cases and drops
the 17th without a word. Reading the code on 2026-09-15 found the list is bounded in three
places that do not agree, and the dropped 17th is the least of them.

| Bound | Where | Value |
|---|---|---|
| The cached array | `g_cases[16]` and `listSessions(g_cases, 16)` in `src/main.cpp` | a bare `16`, written twice |
| Rows the renderer draws | `for (size_t i = 0; i < g_caseCount && i < 7; i++)` | a bare `7` |
| Rows that fit above the footer | rows start at y=25 and step 14; the footer line is at y=120 and the size-1 glyph is 8 px tall | 6 |

Three consequences, in the order an operator meets them.

1. **The selection leaves the drawn list at the eighth case.** Navigation runs to
   `1 + g_caseCount`, while the render loop stops at seven, and the highlight bar is drawn
   only for rows the loop reaches. With eight or more cases the operator moves the selection
   onto rows that are not on screen, and `ENTER`, `TAB` and `X` then open, rename or delete a
   case nobody can see. The delete confirmation names no case either — it reads
   `Delete? ENTER=yes, other key=no`.
2. **The seventh row is drawn over the footer.** It lands at y=123 and the help line sits at
   y=120. This was already noticed: the comment above the footer in `renderCaseMenu()` reasons
   about a full list of seven running to y=123, and works around the collision by choosing what
   to draw in that slot rather than by drawing one row fewer.
3. **The 17th case is dropped in silence**, which is the original ledger line. `listSessions()`
   returns a count, and a full array and a truncated one return the same number.

The cache itself is worth keeping: the case menu is a modal loop that repaints every 50 ms
(ADR-0019), so re-reading the card per frame is not affordable. Its cost is bounded and small —
`SessionMeta` is 44 bytes, so sixteen of them are the 704 bytes the renderer's format comment
already cites.

## Decision

1. **One name for the cache bound.** `dupin::kMaxListedCases` in `src/ui/case_list.h` replaces
   both bare `16`s, and `SessionMeta`'s size and the resulting 704 bytes are stated there, so
   moving the bound is a decision made against its cost rather than by editing a literal.
2. **The rows are derived from the panel geometry, never written.** `kCaseMenuRows` in
   `src/ui/display.h` is
   `(kCaseMenuFooterTopPx - kCaseMenuListTopPx - kGlyphHeightPx) / kCaseMenuRowHeightPx + 1`,
   and `kCaseMenuCaseRows` is one less, because row 0 is the pinned `[+] New Case` action. That
   arithmetic yields 6 case rows where the literal said 7, which is the collision in Context 2
   resolved at its cause.
3. **The list scrolls, by pages, statelessly.** `caseListWindow(selectedCase, caseCount,
   visibleRows)` returns the first index drawn and how many, as
   `first = (selectedCase / visibleRows) * visibleRows`. Pages rather than a centred cursor:
   the selection keeps its place on screen and the page turns once every six moves, instead of
   the whole list sliding under the cursor on every keypress. Stateless rather than a remembered
   scroll offset: the modal loop has no place to keep one in step with a list that is refreshed
   after every create, rename and delete.
4. **A truncated listing says so.** `listSessions()` returns
   `SessionListing { size_t count; bool truncated; }` instead of a bare count — the same rule
   invariant 21 states for failures, applied to an incomplete answer. It is a flag and not a
   count of the surplus, because counting the surplus means walking the rest of an unbounded
   directory for a number the operator cannot act on; knowing the card holds more is what tells
   them to delete or rename.
5. **Both facts reach the operator on the panel and the serial port.** The title carries
   `<first>-<last>/<count>` while scrolling, with a trailing `+` when the card holds more than
   the cache, and `refreshCaseList()` logs `[MENU] Found N case(s)` with `, card holds more than
   the list can cache` appended when truncated.
6. **Proven on the host.** The window arithmetic and the row derivation are pure, so they live
   outside `main.cpp` — which the native env excludes — and are asserted by
   `test/test_case_list/`, per invariant 1. The tests were run first against the behaviour this
   ADR replaces (a window fixed at `first = 0`, seven rows): the page assertions and the
   footer-collision assertion failed, and pass on the derivation above.

### Rejected

- **Name the bound and report the overflow, without scrolling.** Closes the ledger line as
  written and leaves Context 1 — the operator acting on rows they cannot see — which is the
  defect reachable with nine cases rather than seventeen. It is also the half a host test cannot
  reach, since without a window there is no arithmetic to assert.
- **Drop the cache and read the card as the list scrolls.** Removes the cap entirely, but the
  menu repaints every 50 ms and a directory read per frame is not affordable, so it needs a
  paging cursor over `openNextFile()` and its own decision. A card holding more than sixteen
  investigations is not the case to design for first.
- **A centred window that keeps the selection in the middle.** Always shows the selection, like
  the paged window, but slides every row on every keypress on a six-row panel. More repaint
  churn for no reachability the paged window lacks.

## Consequences

- The `LEDGER.md` line about the 17th case is closed by deleting it.
- Invariant 25 in `CLAUDE.md` records the rule: a list bound is named once, the rows are derived
  from the geometry that holds them, and a listing that could not show everything says so.
- Six cases are visible at a time rather than seven, one of which was unreadable anyway.
- `listSessions()`'s return type changes, so any future caller is forced to look at `truncated`
  rather than inherit today's silence.
- `listSessions()` also stopped leaking a `File` handle per hidden directory: its `continue` for
  a name beginning with `.` skipped the `entry.close()` at the foot of the loop. Found in the
  function this decision changes and fixed with it.
- The cap is still sixteen, and a card with more cases still cannot reach them all from the
  menu. What changes is that the device now says so. Raising it is a memory decision with a
  measured cost, which is why decision 1 puts the cost next to the number.
