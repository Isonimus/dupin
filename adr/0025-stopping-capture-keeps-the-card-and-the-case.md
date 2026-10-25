---
id: '0025'
title: "Stopping capture keeps the card and the case"
type: architecture
status: accepted
date: 2026-09-17
supersedes: []
superseded_by: []
---

## Context

ADR-0016 made the case the owner of a baseline, and invariant 16 states it: a baseline belongs
to exactly one case, and no baseline outlives a case. The stop path in `main.cpp` breaks both.

`SessionManager::end()` carries two facts in one call:

```cpp
void SessionManager::end() {
    if (m_sdAvailable) { SD.end(); }
    m_sdAvailable = false;
    m_currentSession[0] = '\0';
}
```

The first line is about the SPI bus. The last is about ownership — it closes the open case. The
stop handler calls it after saving, under a comment reading "Properly unmount SD card to ensure
clean state for next boot".

What that produces was measured on 2026-09-15 by the probe that is now `scripts/race-verify.mjs`.
After a start-stop-start-stop in one boot, the card held the 13 SSIDs of the first window,
`state` reported `case=- ssids=18`, and the second stop printed nothing at all. Five SSIDs and
45 seconds of observation were dropped, and no surface said so.

The silence is structural rather than accidental. `saveBaseline` and `saveCaseStats` both guard
on `hasSession()`, and the stop path's own block guards on `isAvailable()`; after the first stop
all three are false, so the second stop does not fail to save — it never reaches the save. The
interval autosave in `loop()` is guarded the same way and is equally off for the whole second
window. A capture that writes nothing looks exactly like a capture that found nothing.

The comment's reason does not survive reading `begin()`. That function opens with `SD.end()`,
`SPI.end()`, a CS toggle, 160 dummy clocks and a CMD0 reset, written for precisely the case of
a card left active by another firmware. The recovery a "clean state" wants already exists, at
the mount, where a card in an unknown state is actually encountered. An unmount at stop cannot
reach a *next boot* through a power cycle, and the warm-reset case it could reach is the one
`begin()` already recovers from. It buys nothing and costs every capture window after the first.

> **Correction (2026-10-25, ADR-0032):** `begin()` no longer runs the CS toggle, dummy clocks and
> CMD0 by hand. `SD.begin()` opens with the same steps, so the recovery this paragraph relies on
> still happens at the mount, and the argument stands.

The objection worth taking seriously is the bus: SD and the display share SPI on this board, so
a card left mounted for the whole boot is a card mounted underneath every render. That is
already the steady state during the first capture window, and `make verify-race` measured
125 seconds of it on 2026-09-16 with `renderCollisions=0` and `maxRenderUs=16682`. Leaving the
card mounted after a stop is the state the device already runs in, not a new one.

## Decision

1. **Stopping capture is about the radio.** The stop path stops the capture, drains the queue,
   writes the baseline and the case stats, and leaves the mount and the open case exactly as it
   found them. `g_sessionManager.end()` is removed from it.

2. **Mounting and case ownership belong to `setup()` and the case menu.** Those are the two
   places that may change which case is open or whether the card is mounted, and they are the
   two places an operator is deciding that. Any later path that wants a case goes through the
   menu rather than growing a lifetime call of its own.

3. **`end()` keeps both meanings**, because its one remaining caller — `~SessionManager()` —
   wants both, and splitting an API to serve a call site that no longer exists is speculative
   generality. The stop path carries a comment naming this ADR, because putting `end()` back is
   the plausible "fix" a later reader reaches for.

4. **A stop whose write reported failure says so.** Three outcomes, three distinct lines:
   - the write returned success — `[SD] Saved to case <name>`;
   - the card is there and the write returned failure — `[ERROR]` on the wire and a toast on
     the panel, because at a stop there is no retry left and `[ERROR]` is what fails a verify
     run (ADR-0009);
   - no case is open — `[SD] No case open, nothing saved`, a notice rather than an error,
     because a card-less board is a declared configuration and a separate `LEDGER.md` item.

   The old `[SD] Session saved and unmounted` printed unconditionally after two ignored return
   values, so it claimed a save on every run that reached it, including the ones that wrote
   nothing. The same treatment goes to the interval autosave, which discarded its result too.

   **The claim is exactly as strong as the return value it rests on, and no stronger.**
   `saveBaseline` checks that `SD.open` succeeded and then discards the return of every
   `file.print` and `file.printf`; `writeFile` in `utils/file_utils.h`, which `saveCaseStats`
   goes through, does the same. A card that fills or is jostled loose partway through the entry
   loop therefore returns success and leaves a truncated document, and this decision reports
   that as `[SD] Saved to case <name>`. That is a real gap, it predates this decision, and it
   is carried as a `LEDGER.md` item rather than quietly folded in here — but it is named at the
   claim it weakens, because a decision that says "a write that could not happen is announced"
   without this paragraph would be read as a guarantee it does not make.

5. **`[ERROR]` is for a failure with no retry behind it; `[SD]` is for one there is.** The
   interval autosave leaves `g_lastBaselineCount` where it was on failure, so the next interval
   sees the count still differ and writes again — the firmware recovers on its own. Marking that
   `[ERROR]` would fail a verify run over a transient the device fixed unaided, and
   `make verify-race` sizes its first window to contain exactly one autosave, so it is a path a
   gate drives on purpose rather than an unlikely one. The stop has no next interval, so its
   failure keeps `[ERROR]`; a card that is genuinely gone reaches it there. This follows
   ADR-0024, which put drops under `[QUEUE]` for the same reason.

6. **`make verify-baseline` captures twice.** The gate asserts that a baseline written under a
   case comes back under that case, and it is green today on firmware that loses the second
   window — because it only ever captures once. A single-window round trip cannot see this
   defect, so the second window becomes part of the gate rather than a gate of its own: it
   reads `state` after the first stop and requires the case to still be open, then captures and
   stops again and round-trips the *second* window's count across the reset.

## Alternatives rejected

**Stop lets go, loudly** — keep `end()` in the stop path, and refuse a second capture until a
case is opened. It is a smaller change and it does fix the silence. It was rejected because it
fixes the reporting and keeps the defect: the operator still loses the second window, and now
has to know that "stop" means "close the case" and walk through the menu between windows. The
word on the screen is `ESC=Stop`, and a stop that ends the case is a heavier action than the
word claims. Nothing about the radio stopping requires the card to be let go.

**Split `end()` into `unmount()` and `closeCase()`** and call neither from stop. Correct in the
abstract, but it adds two public methods to serve zero call sites — the stop path wants neither
half. Decision 3 records the reasoning instead, which is what a later split would need anyway.

## Consequences

- A second, third and fourth capture window in one boot write to the open case, and the
  interval autosave runs in all of them. This is the behaviour the ledger item was opened for.
- The in-RAM baseline no longer outlives its case, so invariant 16 holds across a stop by
  construction rather than by the operator not stopping.
- The card stays mounted from `setup()` (or the menu's reinit) to power-off. The menu's
  "Reinit SD" path is unchanged and still recovers a card that was removed and reinserted.
- `make verify-baseline` gets longer by one capture window and its save wait. Its pass budget
  grows to match; the budget is a tap lifetime, never an assertion (ADR-0015).
- A truncated write still reads as a success at the stop, until the writers check what they
  wrote. `make verify-baseline` does not pass silently on it — `restore()` compares the restored
  count against the saved one across a reset, so a truncated document fails there — but it fails
  one reset later and reads as a restore defect rather than the save-time truncation it is.
- What a passing run does **not** prove is the failed-write announcement. Making a card refuse
  a write on demand would need a capability the operator does not have, which invariant 14
  forbids the serial channel from growing. That branch is review-only, and says so in the
  invariant row rather than being claimed as verified.
