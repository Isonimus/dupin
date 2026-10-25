---
id: '0020'
title: "Cases can be deleted, and the baseline gate works only in cases it made"
type: architecture
status: accepted
date: 2026-09-15
supersedes: []
superseded_by: []
---

## Context

`make verify-baseline` (ADR-0016) opens the first case on the card, captures until Watson has
announced three `[NEW SSID]` lines, stops — which writes the baseline — resets, and asserts the
same count comes back. On the operator's card the first case is `home`, a case in real use.

**The gate starves itself.** Watson prints `[NEW SSID]` only when an SSID is absent from the
baseline it is scoring against (`watson.cpp`, `wasNewSSID`), and that baseline is the one the
case restored. Every passing run saved its discoveries back into `home`, so each run had fewer
left to find: the store read 28, 32, 35 and then 40 SSIDs over successive runs, and on
2026-09-15 the gate failed with two new SSIDs in 30 s while frames kept arriving (14 to 361).
The committed firmware at `8393d3e` fails identically, so the gate, not the firmware, broke.

**Relaxing the novelty requirement would make the gate pass on broken firmware.** If the
discovery step accepted any baseline of three or more, pass 1 on `home` would load 40, capture
nothing new, report `Saved baseline (40 SSIDs)`, and pass 2 would read back 40 — the 40 the
file already held. A save that wrote nothing at all passes that round trip. The count proves a
write only when the case started out holding something different from what was saved, and the
one state that guarantees it is an empty case.

**An empty case per run needs a way to remove it, and there is none.** `deleteSession` exists
but nothing outside a host test calls it. The keyboard cannot delete a case, so neither can the
serial channel: it may expose nothing the operator could not already do (invariant 14). A gate
that makes a uniquely named case each run and leaves it behind fills the card, and
`refreshCaseList()` lists at most 16 cases, after which the gate's own case is not listed and
the gate breaks again. A fixed name reused each run is refused on the second run:
`createSession` returns `AlreadyExists`.

**`deleteSession` itself is not fit to be called.** It returns a bare `bool` where it can fail
in four ways (invariant 21). It ignores each failed `SD.remove`, so the `SD.rmdir` that follows
fails on a non-empty directory with nothing said. It removes entries from the directory it is
still iterating, which is not a traversal FAT promises to complete. It builds each path with an
unchecked `snprintf` into 96 bytes, where a truncated path removes the wrong file or none. A
case folder holds only files today — `baseline.json`, `case.json`, `anomalies.json`,
`findings.json`, `hypotheses.json` — so a subdirectory there is content Dupin did not write.

**A running capture writes into the open case.** `c` opens the case menu without stopping
capture, and a running capture saves into whichever case is current twice over: `loop()`
autosaves the baseline on an interval while `g_running` holds, and stopping saves it again.
The menu blocks `loop()`, so the autosave pauses while it is open and resumes the moment it
closes. Deleting that case under a running capture would leave the next autosave or stop
writing into a folder that no longer exists.

**The gate cannot find a case by position.** `listSessions` returns entries in FAT directory
order, and a deleted entry's slot is reused by the next folder created, so where a case lands
in the list is not predictable. `state` does not report the selection, and `driveDevice` runs
a fixed step list after a reset, so a gate cannot "press down until the case is selected".

## Decision

1. **`x` on a selected case in the case list asks to delete it.** A printable key, because the
   serial `key` command carries one character and no modifier (`serial_command.cpp`): `x`
   needs no change to the vocabulary, so an injected delete is the operator's delete by
   construction. The list already uses `o`, `l`, `;`, `.`, Tab, Enter and Del. `x` on the
   `[+] New Case` row does nothing.

2. **Deleting takes a second key.** `x` arms it, and the list's notice line reads
   `Delete? ENTER=yes, other key=no` in red with the case still highlighted. Enter deletes. Any
   other key disarms and is consumed — including navigation, because a delete that stayed armed
   while the selection moved would be re-aimed at a case the operator did not choose. The armed
   state is reported through the existing `notice=` field.

3. **Deleting is refused while capture is running, and otherwise closes the case.** The hazard
   is the running capture, not the case being open: a stopped firmware writes nothing until
   the next stop. Refusing every open case would also leave the gate unable to remove the last
   case it opened without a third. When the deleted case is the open one, `main.cpp` clears
   Watson's baseline along with the session, because no baseline outlives its case
   (invariant 16). The refusal reads `Stop capture to delete a case`.

4. **`deleteSession` returns a `SessionResult`**, and removes nothing it cannot account for.
   It lists the folder before removing anything; a subdirectory, or an entry whose path does not
   fit the path buffer, is refused as `UnexpectedContent` with the folder untouched. It then
   removes the listed files and then the folder, stopping at the first failure. A failure
   before anything is removed is `WriteFailed`; one after is `DeleteIncomplete`, because the
   case is still listed but no longer holds what it did, and "Card write failed" on a case that
   still appears would read as nothing having happened. The list is refreshed after every
   attempt, and an open case keeps its in-memory baseline on a failure, since its folder still
   exists and the next save rewrites what was lost. `NoCard`, `InvalidName` and `NotFound` mean
   what they mean for rename. Each new
   refusal has its own message that fits the panel, as ADR-0019 requires.

5. **`state` reports the selection as `sel=`**, placed before `notice=`, which has to stay last.
   It is the selected case's name, `+` for the `[+] New Case` row, and `-` when the menu is
   closed. Neither `+` nor `-` is a valid name character, so no case can be mistaken for either.
   It is read-only and reports what is already highlighted on the panel.

6. **The harness gains an interactive session**: open a tap once, then `send` a command and
   `waitFor` a predicate as often as the caller needs. `driveDevice` becomes a fixed step list
   run through it. Every wait stays a timer (invariant 15). A navigation loop is bounded by the
   listed-case ceiling plus one, so a case that never appears fails rather than spins.

7. **`make verify-baseline` works only in cases it creates, and deletes them.** The names are
   `verify_gate_a` and `verify_gate_b`. It first deletes either one if an interrupted run left
   it behind; creates `verify_gate_a`, which must report `No stored baseline`; captures three
   `[NEW SSID]` lines, which an empty case now guarantees wherever a few networks are in range;
   and stops, which saves. The saved count is read from the first `Saved baseline` line *after*
   `[KEY] Stop detected`, never from the first one at all: the interval autosave prints the same
   line, and although its 60 s default is longer than the gate takes to reach the stop, a card
   whose config sets `baseline_autosave_interval` lower would hand the gate an autosave's count
   and fail pass 2 on correct firmware. After the reset it navigates to `verify_gate_a` by `sel=` and asserts
   the exact count comes back. It then creates `verify_gate_b`, which must report no stored
   baseline and `ssids=0` — a fixed expected value, where the switch used to be checked
   against whatever the card's second case held — and deletes both, asserting neither can
   still be selected. It never opens, writes or deletes a case it did not create.

**Rejected:**
- *A `select <name>` serial command.* The operator reaches a case by navigating, not by name;
  a command that jumps there is the parallel input path invariant 14 forbids.
- *Fn+Del as the delete key.* The key vocabulary carries no modifier, so the gate could not
  send it without widening the channel for this one key.
- *A reserved case the gate empties each run.* Emptying a baseline is not an operator action
  either, so it has the same cost as deleting, and it leaves a folder on every card forever.

## Consequences

- `LEDGER.md` loses the verify-baseline starvation line.
- A new standing invariant: a verify gate writes only into cases it created and deletes them
  before it exits.
- `README.md`'s Session Management table gains the `x` delete and its confirmation.
  `CLAUDE.md` §3 describes the gate as working in its own cases.
- `make test` asserts every `deleteSession` refusal's message, and that a refusal is distinct
  and fits the panel. What it cannot assert is the SD half: removing files and folders is
  device-only, and `make verify-baseline` is its proof, because the gate's cleanup is a delete.
- `make verify-input` asserts arming and disarming — `x` shows the question and a second key
  withdraws it — and still never creates or deletes a case. It needs one case on the card to
  arm a delete against, and fails saying so on a card with none.
- The gate needs one free slot below the 16-case ceiling for each of its two cases. On a
  fuller card it fails because its case never becomes selectable, not by writing elsewhere.
