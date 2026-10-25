---
id: '0019'
title: "A refused case name says what rule it broke"
type: architecture
status: accepted
date: 2026-09-13
supersedes: []
superseded_by: []
---

## Context

`LEDGER.md` carried this as a one-line defect: `handleCaseMenuInput()` discards a rejected
case name silently. The line is right that something is silent and wrong about what, and
both errors matter enough to record, because each one would have sent the fix to the wrong
place.

**The failure modes the ledger names cannot happen.** It says the silent rejections are "a
name over 30 characters, or one with a character outside `[A-Za-z0-9_]`". Neither is
reachable from the keyboard. `dispatchCaseMenuKey()` filters every character as it is typed
with a rule that is a byte-for-byte copy of `SessionManager::isValidName()`'s, and bounds
the buffer at `kMaxSessionNameChars`. By the time `Enter` is pressed the name in
`g_inputBuffer` has already satisfied both halves of the validator, so the
`isValidName(g_inputBuffer)` guard at the confirm branch can only fail on the one input the
filter cannot stop: an empty buffer.

That duplicate is also why nobody noticed. The UI *prevents* the invalid input rather than
*reporting* it, so the rejection path reads as dead code while the failures that do occur
fall through it untouched.

**What is actually silent, then:**

| Reachable failure | Today |
|---|---|
| `Enter` on an empty buffer | input closes, buffer clears, nothing shown |
| `createSession()` returns false | swallowed by a bare `if`; menu stays, nothing shown |
| `renameSession()` returns false | return value is not even read |

The third is not in the ledger at all. `renameSession()` is called for its side effect and
`refreshCaseList()` runs unconditionally afterwards, so a rename that failed repaints the
list showing the old name and looks like a no-op.

**A toast cannot carry the message, which is the ledger's second wrong premise.** The line
proposes "a toast naming the rule that was broken" on the grounds that `fitText` already
bounds one. `fitText` does, but the surface does not exist: `showToast()` stores the text
and calls `updateDisplay()`, which draws the *main* screen — mode list, stats, footer. The
case menu is a modal `while (g_inCaseMenu)` loop in `setup()` and in the reopen path, and
that loop calls `renderCaseMenu()` every 50 ms. A toast raised from inside the case menu
would repaint the main screen over the menu for one frame and be overwritten on the next
tick. The status ticker fails the same way, for the same reason: `setStatus()` writes
`g_statusMessage`, which only `updateDisplay()` reads.

**The root cause is the return type, not the call site.** Both storage calls know exactly
which rule was broken and throw it away on the way out. `createSession()` distinguishes no
card, invalid name, a session that already exists, and a `mkdir` that failed —
`Serial.printf`-ing two of them — and returns `false` for all four. `renameSession()`
distinguishes five and returns `false` for all five. A caller cannot report a reason the
API will not return, so fixing only the UI would mean the UI guessing: "could not create
case" is a different flavour of the same silence, and it is the flavour that survives review
because it looks like it fails loud.

This repo has already applied the opposite principle once, one layer over. ADR-0014's
serial reader states it directly — "a command that cannot be honoured says so rather than
being dropped; a gate that cannot tell *refused* from *never arrived* reports the wrong
thing." The control channel got that treatment. The operator standing at the keyboard did
not.

## Decision

**1. `createSession()` and `renameSession()` return a reason, not a bool.** A new
`enum class SessionResult` names every way each can fail — `Ok`, `NoCard`, `InvalidName`,
`AlreadyExists`, `NotFound`, `WriteFailed` — and each early return is replaced by the
matching value. The enum is declared in `session_manager.h` and is portable, so every
mapping from an input to a reason is assertable by `make test` rather than only observable
on a device. `SessionResult::Ok` is the only success, and the type is an `enum class` so it
cannot be tested as a truthy int by accident.

This is the change the defect actually needs. The two call sites in `main.cpp` are the only
production callers, so the blast radius is the block being rewritten anyway.

**2. The rejection renders on the case-menu surface, because that is the only surface the
modal loop draws.** A module-level notice buffer is painted by `renderCaseMenu()`, on both
of its screens — under the input box on the text-input screen, under the list on the list
screen. Not a toast and not the status ticker: as measured above, both draw through
`updateDisplay()` and are erased within one 50 ms tick of the menu loop.

**3. A rejected name keeps the text input open, with the buffer intact.** Today the confirm
branch clears `g_inputBuffer` and sets `g_inTextInput = false` on every path, success or
not. Failing loud is not only saying what broke; it is leaving the operator able to act on
it. A name refused for already existing is one character away from a name that works, and
throwing it away makes the operator retype it to find out. Cancel (`ESC`) still clears,
because that is a withdrawal rather than a refusal.

Keeping the name is worth nothing on its own, because there was no way to edit it. Found
while wiring the device gate: the text editor's backspace branch was unreachable. `ev.del`
was matched by the cancel test above it, so every press of the key discarded the whole name
rather than one character, and the branch that deletes a character had never run. Backspace
now deletes, and cancels only when there is nothing left to delete — which also leaves the
existing gate's escape route intact, since `key del` on an empty input still exits. Without
this the decision above would have been nominal: the operator could read the name that was
refused and still not fix it.

**4. The character rule is stated once.** `SessionManager::isValidNameChar(char)` becomes
the single predicate; `isValidName()` loops over it and the UI filter calls it. This is
invariant 13's shape applied to a class rather than a bound — the length half already
derives from `kMaxSessionNameChars`, and the character half is the copy that was left. It
is what let the UI and the validator agree by coincidence rather than by construction.

**5. The notice is reported by `state`, so the device gate can assert it.** `reportState()`
gains a `notice=` field carrying the current message or `-`. This grants no capability
invariant 14 protects: `state` is read-only and the field reports what is already on the
panel. Without it `make verify-input` could only assert that *something* did not happen,
which is the assertion that passes on a firmware that does nothing at all.

**6. The notice buffer is sized from the renderer's geometry.** The case menu draws at text
size 1, whose built-in-font glyph is 6 px wide, so the ceiling derives from `SCREEN_WIDTH`
and that glyph width the way `kToastMaxChars` already derives from the size-2 one — never
written as a literal at the call site (ADR-0013).

## Consequences

The defect's ledger line is deleted in this commit, which is how an item closes. Its two
wrong premises are not annotated there or anywhere else; they are recorded above, in the
decision that corrects them, because the ledger is a worklist and a deleted line takes its
errors with it.

Standing invariant row 21 is added to `CLAUDE.md` §4 in the same commit: a storage or
boundary call that can fail in more than one way returns *which* one, never a bare `bool`.
It is a genuine addition rather than a second copy of "fail loud" in
`docs/quality-bar.md`. The bar forbids swallowing an error at the point it is caught; this
row is about the API shape upstream that leaves a caller nothing to report — an honest
caller of a bare `bool` still cannot fail loud, so the two rules catch different mistakes
at different sites. It is `review-only`: nothing mechanically checks a return type against
how many ways a function can fail.

`make test` gains the assertions that each input maps to its own `SessionResult`, which is
the half of this change a host can see. The device half — that a refused name reaches the
panel and stays there with the text preserved — is `make verify-input`'s, driven through
the `key` vocabulary that already exists and asserted through the new `notice=` field.

An adversarial review of the change, briefed without this record, found three places the
code claimed more than it did. Each was reproduced before it was fixed, and each is fixed in
this commit rather than carried:

- **`createSession()` ignored both file writes.** It returned `Ok` once `mkdir` succeeded,
  whether or not `baseline.json` and `case.json` were written, so a failed write opened a
  case the card never received. Both results are now checked and a failure returns
  `WriteFailed`. The folder is left in place: `loadSession()` needs only the folder, and a
  case with no `baseline.json` reads as empty rather than corrupt. No host test reaches
  this — the writes sit inside `#ifndef NATIVE_BUILD` — and no unattended gate can make a
  card refuse a write, so it rests on review.
- **A case that failed to open was reported by a toast.** `openCase()` raised
  `showToast("Case load failed")`, which is the surface decision 2 measured as invisible
  from the menu, and the create path then cleared the editor straight after calling it.
  `openCase()` now writes the notice, and the confirm branch closes the editor *before*
  opening, so that report survives. This also covers opening an existing case from the
  list, which had the same invisible toast.
- **"The switch has no `default`, so a missing case fails the build" was not true.** Adding
  an unhandled `SessionResult` value compiled clean on the host *and* the device, because
  nothing enabled `-Wswitch`. `-Werror=switch` is added to `build_src_flags` in both envs,
  and the same probe is re-run to show it now fails.

Two `-Wformat` obligations follow from decision 6 and are met in this commit rather than
carried: the notice is written with a bounded `snprintf` whose buffer is sized from the
format, and any case name interpolated into it is bounded by an explicit `%.*s` precision
for the reason `renderCaseMenu()` already documents at its own list line — a runtime index
into `g_cases` makes the compiler bound a bare `%s` by the whole array.
