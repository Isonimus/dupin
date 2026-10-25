---
id: '0013'
title: "Text is fitted at the renderer boundary, and the bound it is fitted to is named once"
type: architecture
status: accepted
date: 2026-09-12
supersedes: []
superseded_by: []
---

## Context

ADR-0006 added `-Wformat -Werror=format` over `src/` and, because `-Werror=format` promotes
the whole format family, demoted truncation back to a warning with
`-Wno-error=format-truncation`. Three `-Wformat-truncation` sites in `src/main.cpp` were
carried as a `LEDGER.md` line which prescribed the fix: *"fixing them means sizing the
buffers to their real inputs."*

**That prescription was right for one of the three sites and wrong for the other two.** The
line numbers had also drifted twice — recorded as `471/1465/1883`, actually `479/1476/1894`
on the commit this work began — so the sites were taken from the compiler rather than from
the ledger.

| Site | Buffer | Rendered at | True max content | What it actually is |
|---|---|---|---|---|
| `main.cpp:479` `"Scan #%u"` | `sessBuf[16]` | x=180, size 1 (6 px) | `"Scan #"` + 10 digits = 16 | Buffer one byte short of the format's own worst case |
| `main.cpp:1476` `"Case: %s"` | `toast[32]` | **size 2 (12 px)**, centred | `"Case: "` + 30 = 36 | Buffer is not the constraint |
| `main.cpp:1894` `"%s%s"` | `line[40]` | x=10, size 1 | `"> "` + 30 = 32 | Fits. No real truncation |

**Site 3's reported ceiling of 703 bytes is an artifact of a runtime index.**
`sizeof(SessionMeta)` is 44 and `g_cases` holds 16 of them: 16 × 44 − 1 = 703 exactly.
Because `i` in `g_cases[i].name` is not a compile-time constant, GCC bounds the `%s` by the
whole enclosing array rather than by the 31-byte field. The true maximum is 30 characters.
Sizing a buffer to 703 would have been an arithmetic answer to a question nobody asked.

**Site 2's real constraint lives in a different function and is expressed in pixels.** The
toast renderer computes `boxW = textLen * 12 + 20` and centres it with `(240 - boxW) / 2`:

```
18 chars -> boxW= 236  boxX=   2  on-panel
19 chars -> boxW= 248  boxX=  -4  off-panel
37 chars -> boxW= 464  boxX=-112  off-panel
```

The ceiling is **18 characters**. Enlarging `toast[]` to 38 as the ledger prescribed would
have silenced the compiler by letting a 30-character case name reach a renderer that draws
it at `boxX = -112`. The warning pointed at a buffer; the defect was a display bound, and
following the ledger would have made the observable behaviour worse while turning the
diagnostic off.

**A fourth site was already broken, and no format checker could ever have found it.**
`main.cpp:1456` passed `"SD Failed - Reinsert"` — 20 characters, `boxW` 260, `boxX` −10 —
so on master, today, the SD-failure toast draws off the left edge of the panel. It reaches
the renderer through `strncpy`, not a format call, so `-Wformat-truncation` was never going
to report it. It was found by measuring the renderer rather than by reading the warnings.

**The bound the three sites were sized against was written three ways.** `isValidName()`
rejects `len > 30`; `SessionMeta::name` and `SessionManager::m_currentSession` were each
declared `char[32]`; and the compiler, reading the arrays, reported a 31-character maximum.
So the UI sized a display buffer for 31 characters against a validator that admits 30, and
a reader had no way to tell which number was the intended one.

**Two of the four places that copy a session name omitted the terminator.**
`session_manager.cpp:210` and `:368` wrote `strncpy(dst, src, sizeof - 1)` followed by an
explicit `dst[sizeof - 1] = '\0'`; `:346` (the rename path) and `:374` (the native arm) wrote
only the `strncpy`. This was not a live bug — `strncpy` with `n = 31` never writes byte 31,
and byte 31 was zero from the field's initialiser — but the safety rested on an invariant
nobody had stated and nothing enforced, with a `strcmp(g_cases[i].name, ...)` in the case
menu that would read past the field the moment it stopped holding.

## Decision

**1. The session-name bound is named once and everything derives from it.**
`dupin::kMaxSessionNameChars = 30` in `storage/session_manager.h` is the single statement of
that fact. The two storage fields are declared `char[kMaxSessionNameChars + 1]`,
`isValidName()` tests against it, and the display buffers are sized from it. Three
restatements of one number are how the UI and the validator came to disagree.

**2. Text bound for a fixed-width renderer is fitted at the renderer's boundary, and
shortening is marked.** `dupin::fitText(dst, dstSize, text, maxChars)` in `ui/text_wrap`
copies text that fits verbatim and replaces the tail of text that does not with `...`.
`showToast()` calls it, so the fitting happens once, at the boundary, rather than at each of
the twelve call sites — and no future caller can reintroduce the off-panel box.

The marker is the point, not a decoration. The operator-visible defect was never that a
string was shortened; it was that a shortened string looked like a complete one. `...` is
three ASCII characters rather than U+2026 because the built-in font is ASCII, and a
shortening marker that renders as a wrong glyph communicates nothing.

**3. The toast ceiling is derived from the renderer's own geometry, not written as a
literal.** `kToastGlyphWidthPx`, `kToastBoxPaddingPx` and
`kToastMaxChars = (SCREEN_WIDTH - kToastBoxPaddingPx) / kToastGlyphWidthPx` live in
`ui/display.h`, and the renderer in `main.cpp` uses those same constants in place of the
`12`, `20` and `240` it used to restate. A literal `18` would have been the same defect as
the three-way name bound, one revision later.

**4. There is one way to copy a session name.** `copySessionName(dst, dstSize, src)`
terminates unconditionally. Four sites copying a name into a fixed field is past the rule of
three, and the two that forgot the terminator are the argument for extraction: the idiom was
get-at-able, so it was got wrong.

**5. Display buffers are sized from the format, by the compiler.**
`char sessBuf[sizeof("Scan #") + kUint32MaxDigits]` and
`char toast[sizeof("Case: ") + kMaxSessionNameChars]` state the arithmetic in terms the
compiler evaluates, so the size cannot drift from the format string above it. The hand-
computed `16` was wrong by exactly one byte, which is the failure mode this closes.

**6. Truncation is an error. The `-Wno-error=format-truncation` exemption is removed.**
It existed only to carry the three known sites. With those fixed, its sole remaining effect
would have been to let the next one land as a warning nobody reads — and `CLAUDE.md` §2 is
explicit that a rule enforced by memory holds until the first busy afternoon.

**7. Site 3 states its bound with a precision specifier rather than growing its buffer.**
`"%s%.*s"` with `kMaxSessionNameChars` tells the compiler the bound it cannot infer through
a runtime index, which is what makes `line[]` provably sufficient. It is also a real
hardening and not a way to quiet the diagnostic: the precision bounds the read, so a name
that lost its terminator cannot be read past its own field.

## Consequences

Measured on this commit. `make test` 275 cases pass, up from 262 — thirteen new: nine over
`fitText` and the toast ceiling in `test_text_wrap`, four over `copySessionName` and the name
bound in `test_session_manager`. `make build` SUCCESS with **zero** warnings of any kind,
RAM 104260 B and Flash 1038243 B. `make check` and `make check-version` green.

**Every new check was measured in both directions.** Restoring the missing terminator's
absence fails `test_copy_session_name_terminates_when_source_fills_the_field` with
`Expected '\x00' Was 'X'`. Removing the shortening from `fitText` fails five of the nine new
tests. Moving `kMaxSessionNameChars` to 31 — the value the UI had assumed — fails the
pre-existing `test_invalid_name_too_long`, which is the pairing that matters: the old tests
pin the validator's behaviour and the new one pins the constant to it, so neither can move
alone. And with truncation promoted to an error, a deliberately undersized `snprintf` in
`setup()` fails the build with
`error: 'Board: ' directive output truncated writing 7 bytes into a region of size 4
[-Werror=format-truncation=]`, while the tree without one compiles clean.

**Two changes are visible on the device.** The SD-failure toast now reads `SD Fail -
Reinsert` rather than `SD Failed - Reinsert`: 18 characters instead of 20, authored to fit
rather than left for `fitText` to shorten, because a message that ends in `...` says less
than one that was written to the space available. And a case name long enough to overflow
the toast now renders as `Case: My Long Ca...` inside the panel instead of a box whose left
edge is 112 pixels off it.

**`SessionMeta` lost a byte per entry and nothing depended on it.** The field went from
`char[32]` to `char[31]`; `sizeof(SessionMeta)` is unchanged at 44 because the padding
absorbed it. The struct is built in RAM from directory names and never serialised, so the
on-card format is untouched.

**Invariant 6's enforcement note is now accurate for the first time.** It claimed
`make build` enforced the format contract while the truncation half of that family was
explicitly demoted. Row 6 is unchanged in substance; the new row 12 records the fitting rule.

**What this deliberately does not fix**, both recorded as `LEDGER.md` lines rather than
folded in here. The `"Scan #%u"` sites at x=180 clip at the panel edge past four digits —
that is a layout decision (right-aligning them as the uptime field already does), not a
buffer size, and mixing a layout change into a truncation fix would make the commit atomic
in neither. And `handleCaseMenuInput()` discards a rejected case name silently: the `if`
guarded by `isValidName()` simply does not execute, then the input closes and the buffer is
cleared with no message to the operator. That is a "fail loud" violation in
`docs/quality-bar.md`'s own terms, found while measuring the name bound, and fixing it means
deciding what the failure should say — which is the work, not the edit.
