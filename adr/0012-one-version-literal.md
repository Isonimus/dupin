---
id: '0012'
title: "One version literal, read rather than restated"
type: architecture
status: accepted
date: 2026-09-12
supersedes: []
superseded_by: []
---

## Context

ADR-0011 fixed `DUPIN_BOARD_NAME`, which `include/dupin.h` defined five times and nothing
read, and recorded that `DUPIN_VERSION_STRING` had the identical defect but was deliberately
left alone. This is that decision.

The repo carried two versions and displayed the one that was not in the header:

| Site | Value |
|---|---|
| `include/dupin.h` — `DUPIN_VERSION_STRING` | `0.2.0`, read by nothing |
| `src/main.cpp:172` — serial boot banner | `v0.2.1` |
| `src/main.cpp:204` — splash screen | `v0.2.1` |
| `src/main.cpp:1317` — ASCII-art header | `v0.2.1` |

The reason this was not folded into ADR-0011 is that choosing between the two numbers is a
decision rather than a cleanup, and the git history does not settle it. The commit that
introduced `0.2.1` into `main.cpp` is titled `feat(ux): Table navigation and detail popups
v0.2.0` — its own subject disagrees with the literal it added — and `include/dupin.h` has
not been touched since `d794165 Project structure`. Neither number is evidently the later
intent, so the operator chose, and `0.2.0` is now the version.

This matters more than a display string, because the choice is visible on the device: the
banner a user sees goes from `v0.2.1` to `v0.2.0`. Recorded here so that a future reader
finds a decision rather than what looks like an accidental version regression.

## Decision

**1. `DUPIN_VERSION_STRING` in `include/dupin.h` is the only version literal, and it is
`0.2.0`.** All three sites in `src/main.cpp` concatenate it instead of restating it. They
are string-literal concatenations, not format arguments, so nothing here is subject to
invariant 6.

**2. `DUPIN_VERSION_MAJOR`, `_MINOR` and `_PATCH` are deleted.** They were a second
representation of the same fact, also unread, and keeping them means keeping them in step
with the string by hand — which is the defect this decision closes, not a mitigation.

This is the opposite call to ADR-0011 §3, which kept six unread `DUPIN_HAS_*` macros on
purpose, so the line between them is stated rather than left to look inconsistent. Those
guards are kept because deleting them fails *silently*: a later `#ifdef DUPIN_HAS_KEYBOARD`
would compile, link, and drop input on a board that has a keyboard. Deleting a version
number cannot do that — a `#if DUPIN_VERSION_MAJOR >= 1` that needs it is a compile error
the moment it is written, and re-adding the macro is trivial. **Unread declarations are kept
when their absence would fail quietly, and deleted when it would fail loudly.**

**3. The rule is enforced by `make check-version`, not by review.** It greps `src/` for a
quoted dotted triple and fails if one is found, naming `DUPIN_VERSION_STRING` in the error.
A grep rather than a script because it needs no parsing to be exact: on the commit that
added it, the only four matches in the whole repo were the one definition and the three
copies it replaced, so the pattern has no false positives to tune away. It was measured in
both directions before being trusted — a literal injected into `main.cpp` fails it exit 1,
and removing that literal passes exit 0. A checker that has only ever passed is not evidence.

**4. It joins `make check`.** It needs no hardware, so unlike the device gates of ADR-0002
there is no reason to split it out, and CI already runs `check` (ADR-0007). This widens
`check` from purely doc invariants to one source invariant, which is a deliberate change of
that target's scope: the criterion for `check` is "runs without hardware", not "reads
Markdown".

## Consequences

Standing invariant row 11 is added to `CLAUDE.md` §4 in the same commit, `verified_by:`
`make check-version`. It is one of the few rows in that table that is genuinely mechanical
rather than `review-only` or `pending (LEDGER)`.

The `LEDGER.md` line ADR-0011 opened for this is closed by deletion in the same commit.

`make check` now fails on a source pattern, not only on documents. A contributor adding a
version string to a new file in `src/` will see it there rather than at review, which is the
point; a contributor who genuinely needs a dotted triple in a string — a protocol version, a
dotted-quad address — will see it too, and the answer is to name it as a constant, which the
quality bar asks for anyway.

The ASCII-art banner in `printBanner()` stays square only while the version is five
characters wide, because its padding is literal. That is noted at the site. `check-version`
sees duplication, not alignment, and is not claimed to catch it. The same block had a
pre-existing off-by-one — the `Defensive Wi-Fi Intelligence` row was 40 characters against
the box's 41 — corrected here under the boyscout rule since the edit touched those lines.
