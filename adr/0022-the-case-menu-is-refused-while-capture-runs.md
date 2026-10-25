---
id: '0022'
title: "The case menu is refused while capture runs"
type: architecture
status: accepted
date: 2026-09-15
supersedes: []
superseded_by: []
---

## Context

ADR-0016 made the case the owner of a baseline: opening a case clears Watson's baseline and
restores that case's. ADR-0020 found that a running capture saves into whichever case is open,
through the interval autosave in `loop()` and again through the stop, and refused *deleting* a
case while capture runs. It left the rest of the menu open to a running capture, and `LEDGER.md`
recorded what that allows: opening or creating another case mid-capture moves the capture into it.

Reading the code on 2026-09-15 showed the hazard is wider than the case the capture lands in.

1. **The baseline is written from two tasks with nothing between them.** `onFrame` is the
   promiscuous-mode callback and runs on the Wi-Fi task; it hands each observation straight to the
   modes, so Watson inserts into its baseline from there. `openCase()` runs on the loop task and
   calls `baseline.clear()` and then `loadBaseline()` on the same object. `src/` and `include/`
   hold no mutex, critical section or atomic. The capture keeps running while the menu is open, so
   a switch mid-capture is a clear-and-reload racing an insert, not only a misfiled one.
2. **The menu stalls capture anyway.** The case menu is a modal loop that blocks `loop()`, so while
   it is open `g_wifiCapture.update()` never runs and channel hopping stops on whichever channel it
   was on, and the autosave and the heartbeat stop with it. A capture that looks running is
   listening to one channel for as long as the operator browses.
3. **Renaming the open case is not a hazard.** `renameSession` moves `m_currentSession` with the
   folder, so the autosave follows the new name. It is refused below only as a consequence of
   refusing the menu, not on its own merit.

The gate `make verify-baseline` already starts a capture in a case it created, which makes the
hazard reachable from it: with a step that sends `key c` while capture runs and expects a refusal,
the firmware before this decision opened the menu, and the gate failed on that step. Its transcript
shows three `[NEW SSID]` lines and then `[KEY] Case Menu`, with no refusal within 5 000 ms
(`verify-artifacts/baseline-verify-cardputer/2026-09-15T12-51-25.908Z-FAIL/`).

## Decision

1. **`c` is refused while capture runs.** The main dispatch checks `g_running` before it touches
   the card or opens the menu, prints `[CASE] Case menu refused: capture is running`, and raises
   the toast `Stop capture first`. The toast is seen: it is raised from the main screen, which is
   not the 50 ms modal repaint ADR-0019 found erasing toasts inside the menu.
2. **The refusal is at the menu's door, not at each action inside it.** One check covers opening,
   creating, deleting and renaming, and removes the stall in Context 2, which a per-action refusal
   would keep for as long as the menu stayed open. ADR-0020's delete refusal becomes unreachable
   and is removed rather than left as dead code; the property it protected — no case changes under
   a running capture — now holds for every action in the menu instead of one.
3. **Proven on the device by `make verify-baseline`.** During its capture, after Watson has added
   entries to the gate's case, it sends `key c` and asserts the refusal line, then that `state`
   still reports `running=yes menu=no`, the gate's case, and at least the entries it had.

### Rejected

- **Refuse only switching, inside the menu.** Keeps browsing and renaming, but needs a guard on
  open, create and delete separately, and leaves channel hopping stalled while the menu is open.
- **Stop capture, then switch.** Stopping unmounts the card (`g_sessionManager.end()`), so the load
  that follows fails unless the stop path is restructured, and it ends the operator's capture
  without asking. The most code for the least safety.

## Consequences

- The `LEDGER.md` line about switching case during capture is closed by deleting it.
- Browsing or renaming cases mid-capture now costs a stop. That is chosen: stopping is one key and
  saves the capture into the case it belongs to.
- Invariant 24 in `CLAUDE.md` records the rule, so a later action added to the menu, or a second
  way into it, does not reopen the race by reaching the case outside the check.
- The race in Context 1 is removed only for case changes. Every mode is still written from the
  Wi-Fi task while `loop()` reads and saves the same state — the autosave serialises the baseline
  while inserts continue. That is not decided here; `LEDGER.md` carries it.
