---
id: '0009'
title: "Device verify harness: a Node orchestrator over a pyserial reader"
type: architecture
status: accepted
date: 2026-09-12
supersedes: []
superseded_by: []
---

## Context

ADR-0002 split enforcement in two: what runs unattended (`make check`, `make build`,
`make test`) and what needs a board on a USB port. It specified the device half in detail —
`scripts/<slice>-verify.mjs`, failing on a non-zero `pio` exit, on `/^\[(ERROR|FATAL)\]/`,
on `Guru Meditation`, `abort() was called` or `Backtrace:`, or on its own assertion not
arriving within a timeout, and writing artifacts for human review — and then nothing built
it. Four `LEDGER.md` items came to block on the same absent thing, which is why it was
worth building before any of them.

**There is hardware, and it was running something else.** `/dev/ttyACM0` presents as
`303a:1001 Espressif USB JTAG/serial debug unit`. Before anything was flashed it was
captured passively and found to be running `THE ADVERSARY / Red Team Wireless Tool /
Version: 0.1.2-alpha` — the sibling red suite ADR-0004 and ADR-0005 already cite, not a
build from this repo. Overwriting it was authorised explicitly rather than assumed.

**The firmware cannot be driven, only observed.** `src/` contains no `Serial.read`,
`Serial.available`, or any other read from the port; the device is write-only over serial.
So a verify script can assert what the firmware volunteers and nothing else. Driving it into
a state — which is what the `saveBaseline`/`loadBaseline` round-trip in ADR-0003 actually
needs — requires a serial command reader in production firmware, and that is a separate
decision about production code, deliberately not taken here.

**Node cannot read the port reliably, and this repo cannot install a package that can.**
ADR-0002 chose `.mjs` for verify scripts, and ADR-0002 also established that this repo has
no npm manifest, so `serialport` is not available. Three dependency-free Node strategies
were measured against the live device:

| Reader | Result |
|---|---|
| `fs.createReadStream` on the tty | 1 line, then spurious EOF |
| `spawn('cat', [port])` into a pipe | 1 line in 7 s — buffered in the pipe |
| `fs.readSync` on a blocking fd, `stty min 0 time 10` | **2 lines in 10 s** |
| pyserial, same 10 s window, back to back | **6 lines** |

Node lost two thirds of the lines. For a gate whose entire purpose is to notice a
`Guru Meditation`, a lossy reader is disqualifying: a dropped panic line is a false green,
which is worse than no gate at all. pyserial arrives with PlatformIO, which `make build` and
`make test` already require, so it costs no new dependency.

`pio device monitor` was tried as a third option and rejected: it fails when its stdout is
not a terminal, and its framing is not a contract this repo should depend on.

## Decision

**1. The harness is a Node orchestrator over a Python reader.** `scripts/boot-verify.mjs`
holds the convention ADR-0002 named and everything that is orchestration — running `pio`,
timeouts, assertions, artifacts. `scripts/serial-tap.py` does nothing but read the port with
pyserial and write whole lines to stdout, flushed. The split is on the measured line above,
not on taste.

**2. The reader emits only complete lines.** The first version used `readline()` with a
timeout and emitted partial lines when the timeout expired mid-line, which was caught in
testing as `[Mai[Main] Loop heartbeat`. That is not cosmetic: a fragmented line can split
`Guru Meditation` across two emitted lines and walk straight past the failure patterns. The
tap accumulates bytes and splits on newlines itself; a trailing fragment is emitted once, at
the end, tagged `<tap-unterminated>` so it cannot be mistaken for a line the device
terminated.

**3. The board is reset by the harness, after the reader is listening.** Uploading resets the
chip, so the boot banner is gone before the host has reopened the re-enumerated port. The tap
pulses DTR/RTS once it is open and then signals `<tap-ready>`, so the capture provably starts
before the boot it is asserting on. Measured working: `rst:0x15 (USB_UART_CHIP_RESET)`
appears in the transcript ahead of the firmware banner.

**4. `scripts/verify-harness.mjs` carries the shared core, and is not itself a verify
script.** It deliberately does not match `*-verify.mjs`, so nothing mistakes it for one. The
failure patterns live there, once, as data rather than as prose repeated per script.

**5. The first script asserts boot, because every other device claim rests on it.**
`make verify-boot` builds, uploads, resets, and asserts `[DUPIN] Ready!` — the line `setup()`
prints once the modes are registered — with no logged error and no panic signature in
between. It is the direct answer to the failure CLAUDE.md §3 is built around: the tree where
every board guard was misspelled `DDUPIN_*` compiled clean and booted useless.

**6. `verify-boot` is never wired into `make check`.** ADR-0002 split enforcement precisely
because this half cannot run unattended, and CI has no hardware runner. It is named as a
`make` target because a checker with no entry point runs once, and nowhere else.

**7. The env is overridable, the port is auto-detected, the baud is read from
`platformio.ini`.** `DUPIN_ENV` and `DUPIN_PORT` exist because the board in hand is not
always the default one and a platform bump must prove itself on the env it targets. Nothing
bakes in an absolute device path, and the baud rate is read from `monitor_speed` rather than
copied, so it cannot drift from the setting a developer's monitor uses.

**8. This script is named in `CLAUDE.md` §3 and the `Makefile`, not in a slice's
`## Verification`.** ADR-0002 ties a verify script to the slice whose claim it proves, and
`slices/` does not exist in this repo yet. Boot is a repo-wide precondition rather than one
slice's behaviour, so it is cited where it is actually reachable. A future slice-specific
script still follows ADR-0002's rule.

## Consequences

**The harness works, and its first run against real hardware failed — correctly.** On
`7f5e9a5` + this change, `make build` reports SUCCESS and the firmware boot-loops:

```
[BOOT] 30% - Canvas ready
...
[MENU] Found 2 sessions
Guru Meditation Error: Core  1 panic'ed (LoadProhibited). Exception was unhandled.
```

Decoded against the ELF, that is `pushSprite` at `src/main.cpp:1853` inside
`renderCaseMenu()`, called from `setup()` at `src/main.cpp:218`, dereferencing null inside
LovyanGFX. `EXCVADDR: 0x00000074` with null in A3/A4. The cause is visible one line up in the
same file: `src/main.cpp:188` calls `g_canvas.createSprite(240, 135)` — roughly 63 KB in one
contiguous allocation — and never checks the result, then prints `[BOOT] 30% - Canvas ready`
regardless. This is the "fail loud" rule in `docs/quality-bar.md` being violated in exactly
the way that rule predicts: a silent failure at a boundary, debugged later as a null
dereference four steps away instead of at the point it happened.

The board is a **Cardputer ADV** — `[M5GFX] [Autodetect] board_M5CardputerADV`, detected from
hardware after the flash was erased, so not a stale NVS cache — while `default_envs` is
`cardputer`. Building `cardputer_adv` instead does not help: it panics earlier still, on
Core 0 at `EXCVADDR: 0x00000004`, before `setup()` prints anything. Both findings are
recorded as `LEDGER.md` lines rather than fixed here, because the real question is what the
canvas should do when 63 KB is not available, and that touches the `dupin::Canvas` decision
in ADR-0005. Mixing it into the commit that introduces the harness would make it atomic in
neither — the same reasoning ADR-0006 applied to the truncation sites.

So `make verify-boot` is red on master as of this commit, and that is the finding, not a
defect in the harness. It is stated here out loud rather than left for someone to discover,
because a gate whose failure is known and unexplained is how a gate gets ignored.

The R11 debt in `LEDGER.md` becomes live. That line said the entry-point-wiring gap was "due
here the day the first verify script is written, and there are none today"; there is one now,
so the line is updated to say so. Invariant 2 stays `pending (LEDGER)`: nothing mechanically
checks that `boot-verify.mjs` is reachable from the `Makefile`, and it is reachable only
because this commit put it there by hand.

`verify-artifacts/` is git-ignored. The transcript is the only record of what the device
actually said, so it is written on failure as well as on success — it is most needed exactly
when the assertion did not hold.

## Amendment — 2026-09-12: the boot marker was unreachable unattended

Decision 5 above chose `[DUPIN] Ready!` as the marker. That was wrong. With an SD card
present, `setup()` enters the case-selection menu and blocks on a keypress, so the line is
printed only after a human chooses a case and an unattended gate can never see it. Moving
the marker earlier is not sufficient either: the panic this harness first caught happened
*after* the last boot line, so a marker-only assertion would have reported PASS on broken
firmware.

`scripts/boot-verify.mjs` now waits for `[BOOT] Initialization complete` and then requires a
settle window in which no failure pattern and no reset banner appears. The reasoning, and the
defect that prompted it, are in ADR-0010.

## Amendment — 2026-09-12: the firmware can now be driven

The Context above records that `src/` contained no read from the serial port, so the device
was write-only and a gate could assert only what it volunteered. That was true on this date
and stays true as a statement about this decision: driving the firmware was called "a
separate decision about production code, deliberately not taken here."

That decision has since been taken. ADR-0014 adds a line-oriented control channel to
production firmware — `ping`, `state`, and `key <arg>`, the last running the same dispatch a
physical keypress runs — together with `scripts/serial-tap.py --send-stdin` and
`driveDevice()` in the harness. The `saveBaseline`/`loadBaseline` round-trip this ADR named
as needing such a reader is no longer blocked on the missing primitive.

The timing this ADR reports is also overstated, and ADR-0014 records the measurement:
`captureUntil` re-checks its settle deadline only when a new line arrives, so an idle device
makes it wait out the tap's full lifetime and report that as the boot. The marker is reached
in about three seconds, not twenty-five.

> **Correction (2026-09-12, ADR-0015):** "the timing this ADR reports" is wrong — this ADR
> reports no timing figure at all. The inflated number was published in ADR-0011, which pastes a
> `33264 ms` run as proof of the PSRAM fix, and a correction marker now sits under it there. The
> defect described just above is real and was measured at 33258 ms against a true boot of
> 3741 ms; ADR-0015 fixes it. Only the attribution was wrong.
