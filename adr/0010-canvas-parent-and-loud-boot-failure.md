---
id: '0010'
title: "The canvas takes its parent from M5.Display, and boot failures halt loudly"
type: architecture
status: accepted
date: 2026-09-12
supersedes: []
superseded_by: []
---

## Context

The device verify harness built in ADR-0009 failed on its first run against hardware:
`make build` reported SUCCESS and the firmware boot-looped, panicking in `setup()` with
`Guru Meditation ... Core 1 panic'ed (LoadProhibited)`, `EXCVADDR: 0x00000074`.

**The first diagnosis was wrong, and the code disproved it.** The panic decoded to
`pushSprite` at `src/main.cpp:1853` inside `renderCaseMenu()`, one line below a
`g_canvas.createSprite(240, 135)` whose result was never checked, so the obvious reading was
that a ~63 KB contiguous allocation had failed. Reading LovyanGFX settled it otherwise.
`LGFX_Sprite.hpp:421` is:

```cpp
pixelcopy_t p(_img, dst->getColorDepth(), getColorDepth(), ...)
```

`_img` — the sprite buffer — is only passed along, never dereferenced there. The pointer
being dereferenced is `dst`, which `pushSprite(x, y)` supplies as `_parent`. The allocation
was fine; the canvas had no parent.

**The cause is static initialisation order.** `src/main.cpp` declared

```cpp
static dupin::Canvas g_canvas(&M5Cardputer.Display);
```

and `M5Cardputer.h:19` declares that member as

```cpp
M5GFX &Display = M5.Display;
```

It is a *reference* member, bound when `M5Cardputer_Class`'s constructor runs. Taking its
address from a static initialiser in another translation unit reads it before anything
guarantees that constructor has run, and yields null. `M5Unified.hpp:215` by contrast
declares `M5GFX Display;` — a real member object, whose address is a fixed offset from a
global and therefore a link-time constant, safe to take arbitrarily early.

The failure was invisible for three reasons worth recording. `createSprite` succeeded, so
the buffer was real. `updateProgress(30, "Canvas ready")` printed unconditionally, so the
log actively asserted health. And `setup()` draws its own progress screen through
`M5Cardputer.Display` directly — which works, because by then the constructor has run — so
everything up to the first `pushSprite` looked correct.

**A second defect in the same two lines.** The original order was

```cpp
g_canvas.createSprite(240, 135);
g_canvas.setColorDepth(16);
```

`LGFX_Sprite::setColorDepth` on a sprite that already holds a buffer calls `deleteSprite()`
and then `createSprite()` again. The canvas was therefore allocated, freed and allocated a
second time, and the second allocation's return value — the only one that could report
failure — was discarded.

**The harness's own assertion was also wrong.** ADR-0009 §5 chose `[DUPIN] Ready!` as the
marker. With an SD card present, `setup()` enters the case-selection menu and blocks on a
keypress, so that line is only printed after a human chooses a case: unreachable by an
unattended gate. Worse, the useful marker cannot simply be moved earlier — the panic
happened *after* `[MENU] Found 2 sessions`, so any assertion that stops at a boot line would
have reported PASS on the broken firmware.

## Decision

**1. A display pointer captured at static-initialisation time comes from `M5.Display`.**
`g_canvas` is constructed with `&M5.Display`, the real member object, never with
`&M5Cardputer.Display`, which is a reference alias to the same display but readable only
after its owner is constructed. The two denote the same panel at run time; only one of them
is safe to read this early.

**2. Colour depth is set before the sprite is created,** so the buffer is allocated once at
the depth it will keep, and the allocation that matters is the one whose result is checked.

**3. A boot that cannot produce a usable canvas halts loudly instead of continuing.**
`createSprite` returning null and `getParent()` returning null are both checked, and either
calls `haltBootFailure()`, which logs `[ERROR] boot aborted: <reason>`, prints the reason on
the panel, and stops. This is `docs/quality-bar.md`'s "fail loud" rule applied at the exact
boundary that failed: the previous behaviour printed `Canvas ready`, carried on, and
surfaced the mistake four call frames away inside a third-party library.

**4. The boot gate asserts initialisation plus a settle window.**
`scripts/boot-verify.mjs` waits for `[BOOT] Initialization complete` — the last line
`setup()` prints on every path, with or without an SD card — and then requires 8 s during
which no failure pattern and no reset banner appears. The settle window is what carries the
assertion: a boot loop reboots roughly every 2 s, so it is the part that would have caught
the original defect, and the marker alone would not have. `ADR-0009` §5 is amended to point
here.

**5. `SCREEN_WIDTH`/`SCREEN_HEIGHT` from `src/ui/display.h` are used instead of repeating
240 and 135.** They already existed; `main.cpp` simply did not include the header.

## Consequences

`make verify-boot` is green, measured on this commit: PASS, marker reached and 8 s of settle
with no panic and no reset. The `LEDGER.md` line recording the boot loop is closed by
deletion.

The regression was proved in the direction that counts. With only the parent line reverted
to `&M5Cardputer.Display`, the gate fails; with it restored, the gate passes. The failure
message is now `[ERROR] boot aborted: canvas has no parent display` rather than a
`Guru Meditation` decoded against an ELF — which is the practical value of decision 3, and
is itself evidence that the check sits at the right boundary.

Standing invariant row 9 is added to `CLAUDE.md` §4 in the same commit. It is `review-only`:
the repo has no C++ linter, so nothing mechanically prevents the next static initialiser
from reading a reference member. This is stated rather than glossed, because §4 exists to
keep an aspiration from being read as a guarantee.

The `-Wformat-truncation` sites move with this edit, from `main.cpp:427/1421/1839` to
`471/1465/1883`. The `LEDGER.md` line carrying them is updated, and the underlying defect is
unchanged — the buffers still need sizing to their real inputs.

ADR-0005's `dupin::Canvas` seam is untouched. The bug was in how the canvas was given its
parent, not in the typedef, and nothing here changes what a second board would plug into.

The `cardputer_adv` environment still does not boot on this hardware; it panics earlier, on
Core 0, before `setup()` prints. That is a separate `LEDGER.md` line and is not addressed
here.
