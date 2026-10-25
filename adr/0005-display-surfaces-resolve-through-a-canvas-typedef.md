---
id: '0005'
title: "Display surfaces resolve through one Canvas typedef; rendering logic is host-testable"
type: architecture
status: accepted
date: 2026-09-10
supersedes: []
superseded_by: []
---

## Context

Dupin binds directly to the Cardputer everywhere it draws. `src/ui/display.cpp` and
`src/ui/keyboard.cpp` hard-include `<M5Cardputer.h>` under `#ifndef NATIVE_BUILD`, and
`src/main.cpp` declares `static M5Canvas g_canvas(&M5Cardputer.Display)` and drives it
through roughly two hundred call sites, plus a local `printWrapped(M5Canvas&, ...)`
helper. Two costs follow, and both are already recorded as pain in this repo.

**A second board has nowhere to plug in.** Because no display type sits behind an
abstraction, the M5StickC Plus2 port (see `LEDGER.md`, branch `wip/m5stickc-plus2-port`)
had no seam to fill and resorted to runtime board-detection by display width and a
placement-new over the live global canvas — hacks the ledger flags for deletion. The
sibling suite Adversary solved the same problem with a single `Canvas` typedef resolved
per target and a native stub; adopting an established solution rather than hand-rolling
another is the kit's own default (stele:ADR-0025).

**Rendering logic cannot be tested.** All canvas code is `#ifndef NATIVE_BUILD`-excluded
rather than stubbed, so `pio test -e native` cannot reach any of it. `printWrapped` is
pure line-breaking arithmetic — it computes how text splits given a width — yet it is
trapped in `main.cpp`, which the native build excludes entirely, and it carries a bare
`int charWidth = 6` magic value. A green `pio run` is the only thing that ever touches it,
and ADR-0001/ADR-0002 already establish that a green compile is not evidence of behaviour
in this repo.

**The board terrain a second target hits is already measured, and would otherwise be
rediscovered.** From this repo's own `LEDGER.md` and Adversary's proven two-board
`platformio.ini` (it builds an S3 Cardputer and an ESP32-PICO M5StickC Plus2):

- On a no-PSRAM board a full-display 16-bit canvas costs ~65 KB of ~320 KB internal
  DRAM — the buffering strategy is a decision, not a default.
- A PICO-class target needs `huge_app.csv` (3 MB app, no OTA) to fit, and USB-CDC-on-boot
  off because the chip has no USB-OTG.
- The S3 `memory_type` must not be forced to octal PSRAM on a chip-less board; it boots
  loudly (`octal_psram: wrong line mode`). The chip-less default resolves to QIO flash on
  internal SRAM.

These are terrain, not this change's work, and are recorded so the deferred port does not
pay for them a second time.

## Decision

**1. Every off-screen display surface resolves through `dupin::Canvas`**, defined in one
header, `src/hal/display/canvas_types.h`: `M5Canvas` on M5 targets, a reserved arm for a
future LovyanGFX board (Lilygo T-Embed → `LGFX_Sprite`) left as a documented stub, and a
native stub under `NATIVE_BUILD`. New rendering code names `dupin::Canvas`, never
`M5Canvas` directly. This is the single seam a second board plugs into.

**2. The native stub records; it is not a no-op.** It captures the text emitted through
its `println` so a host test can assert what *would* have been drawn. The reason is
ADR-0002's: a stub that only lets code link would buy a green compile, which this repo
does not accept as verification. Only the members host-compiled code actually calls are
defined — the stub grows with real need, not ahead of it (KISS; no speculative surface).

**3. The pure wrap logic is extracted and tested.** `printWrapped` moves out of `main.cpp`
into `src/ui/text_wrap.{h,cpp}`, split into a display-free `wrapText` (a template, so the
firmware path keeps zero allocation and no `std::function`) and a thin `printWrapped` that
renders wrapped lines to a `Canvas`. Two host tests cover it: `wrapText` asserted
canvas-free and deterministic, and `printWrapped` asserted through the recording stub —
the pair proving both the algorithm and the stub path. The `charWidth = 6` becomes the
named `kApproxCharWidthPx` with its approximation documented at the site (stele:ADR-0012),
so it is not "corrected" into a coupling to real glyph metrics by a later reader.

**4. Scope is the typedef and the test seam (pattern A) plus the recorded board data
(pattern C) — nothing more.** This ADR does **not** introduce the full
`IDisplay`/`IInput`/`IStorageBackend` interface HAL, nor the logical-`InputAction`
`InputManager` that abstracts keyboard versus buttons. Both are real patterns in
Adversary worth bringing, but their payoff is a non-M5 or button-only board, and no such
target is committed. Building the abstraction before the second board exists is the
speculative generality the quality bar forbids. They are deferred with ledger lines citing
this ADR, to be done when a second board is a real target.

## Consequences

`src/hal/display/canvas_types.h` is added; `g_canvas` is retyped to `dupin::Canvas`, which
is byte-for-byte `M5Canvas` on device, so the two hundred call sites are untouched.
`printWrapped` now lives in `src/ui/text_wrap.cpp`, which the native build compiles
automatically — the native `build_src_filter` already takes `+<*>` minus `main.cpp` and
`wifi_capture.cpp` — with `canvas_types.h` resolving to the recording stub under
`-DNATIVE_BUILD`. `test/test_text_wrap/` is the first host suite to exercise a display type.

`CLAUDE.md` §4 gains one invariant row in this commit: new display or rendering code
renders through `dupin::Canvas`, never `M5Cardputer`/`M5Canvas` directly. It is enforced
`review-only`: the linter does not read source, and no rule greps for a raw `M5Canvas`, so
the honest enforcement is `/wrap-up`, not a claim of a check that does not run.

Two `[feature]` lines are added to `LEDGER.md`, both citing this ADR: the logical
`InputManager` (pattern B, which is also what the stalled M5StickC port actually needs and
overlaps the standing audit item that `dupin::Keyboard` has no callers), and the interface
HAL for non-M5 boards (pattern D, with the `canvas_types.h` T-Embed arm as its stub).

The wrap buffers are bounded to `kMaxLineChars` and the renderer resets a line on overflow
rather than growing it, so the original's `strcat`-into-`line[50]` overflow risk is closed
in passing; the popup callers wrap well under the bound, so on-screen behaviour is
unchanged.
