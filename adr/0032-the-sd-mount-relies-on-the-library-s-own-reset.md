---
id: '0032'
title: "The SD mount relies on the library's own reset"
type: architecture
status: accepted
date: 2026-10-25
supersedes: []
superseded_by: []
---

## Context

On 2026-09-14 Dupin failed to mount a card labelled 8 GB on the Cardputer ADV: every attempt drew
three `crc error` replies to CMD0, then `GO_IDLE_STATE failed` and `f_mount failed: (3)`, while a
16 GB card mounted first time and Adversary mounted the 8 GB card on the same board. Clock speed
and SPI bus were already ruled out: the library sends CMD0 at a fixed 400 kHz, and the global
`SPI` on the S3 is FSPI, the bus Adversary uses.

What remained was a sequence `SessionManager::begin()` ran by hand before `SD.begin()`: CS
toggled high, MOSI driven high, 160 clocks at 400 kHz, then a CMD0 whose reply was read and
discarded. It was copied from a community fix to recover a card M5Launcher left mid-command, and
Adversary has no such thing. It could not be deleted on a guess, because a card that will not
mount after the launcher is the failure it was written for.

`scripts/sd-mount-probe.mjs` was written to measure it. One invocation flashed one variant, with
the sequence or without it, and observed one card across up to three arms: `reset` (the harness
resets the chip and the card stays powered), `power` (the operator removes all power), and
`launcher` (the operator boots through M5Launcher into Dupin, which M5Launcher installed from a
`.bin` on the card under test). Power and launcher boots print their mount to a port nobody has
open, so the firmware keeps a mount report, which the read-only `probe` word now carries.

Measured 2026-10-14 and 2026-10-25, every boot in every passing run:

| Card | Sequence | Chip reset | Power cycle | M5Launcher | Mounted |
|---|---|---|---|---|---|
| labelled 8 GB | on | 6/6 | 6/6 | 3/3 | 15/15 |
| labelled 8 GB | off | 6/6 | 6/6 | 3/3 | 15/15 |
| 16 GB | on | 6/6 | 3/3 | | 9/9 |
| 16 GB | off | 3/3 | 3/3 | | 6/6 |

Every one of the 45 mounted on its first attempt at 10 MHz. Every time the sequence ran, the
card answered its CMD0 with R1 `0x01`, idle. None of the September errors appeared. Four runs
failed on the harness rather than the card, as described below, and every boot they completed
also mounted.

Reading the library settles why the sequence could not matter. `ff_sd_initialize` in
`libraries/SD/src/sd_diskio.cpp` opens with the same steps: CS high, 20 bytes of `0xFF` (160
clocks) at 400 kHz, then CMD0. The hand-rolled sequence was a first copy of what `SD.begin()` does
next, so a card it could recover is a card the library's own preamble recovers.

The card labelled 8 GB reports 15240 MiB. A card reader on Linux sees a 14.9 GiB disk holding a
7.4 GiB partition, so the two agree; both read the same capacity register, so neither rules out a
register that lies, and the label is not explained. CMD0 fails or succeeds before any capacity is
read, so it does not bear on this decision.

The harness learned three things on the way:

- Attaching to a running board must open the port exactly as every gate does. Releasing DTR and
  RTS before the open was tried, to leave the reset circuit alone, and it reset the board:
  `rst:0x15 (USB_UART_CHIP_RESET)`. Linux raises both lines on open and pyserial lowers DTR before
  RTS, which passes through the USB-Serial-JTAG reset state. The default open answered `probe`
  twice with no reset banner.
- A board that was already running can hold half a line in its command reader. One attach drew
  `[CMD] err unknown` for a `probe` appended to bytes no newline had ended, and most later attaches
  showed one to three such lines. ModemManager is not the writer: the port carries
  `ID_MM_DEVICE_IGNORE=1`. What writes them is unknown.
- An operator presses Enter when the screen looks ready, which can be mid-boot. One attach landed
  there and its `probe` drew no reply within 5 s.

## Decision

1. **`begin()` runs no reset of its own.** The sequence is deleted and `SD.begin()`'s preamble is
   the reset. What ships is exactly the variant measured as "off", including the `SD.end()` and
   `SPI.end()` it kept ahead of the speed ladder.
2. **The mount is reported by `probe`**, as `sdAttempts` and `sdMountedHz` (0 when no attempt
   mounted). It is read-only like the rest of `probe`, so invariant 14 holds. A boot nobody
   watched is otherwise a boot whose mount nobody can read.
3. **A session can attach without a reset.** `openDeviceSession({ reset: false })` opens the port
   like any other, waits for no boot marker, fails if a reset banner appears, and sends a blank
   line first. The reader treats that blank line as no command, so whatever was half-written ends
   as one `[CMD] err` line instead of corrupting the caller's first command.
4. **The probe stays, without the sequence axis.** It flashes the tree for every arm but the
   launcher, and pings an attached board until it answers before asking for the report. It is the
   tool for the day the failure recurs: run it in the state the card was in.
5. **No "already mounted by the launcher" check.** Adversary has one; Dupin mounted 3 out of 3
   straight after M5Launcher without it.

## Consequences

- The September failure is unexplained. Neither a chip reset, a cold start nor a launcher boot
  reproduces it with either variant, and what preceded it was not recorded. `LEDGER.md` carries it
  as a watch item pointing at the probe.
- One state the sequence's comment named was not reproduced: a chip reset while the card is in the
  middle of a transfer. The reset arm resets a card that sits idle in the case menu. The library's
  preamble covers that state as the deleted sequence did, since the steps are the same, but this
  is read from the code and was not measured.
- ADR-0025 argued that unmounting at stop buys nothing, because `begin()` already recovers a card
  another firmware left active. That still holds, through the library's preamble rather than a
  copy of it; a correction marker at that paragraph says so.
- The stray input on attach is absorbed, not understood. `LEDGER.md` carries it.
- The decision rests on two cards on one board. A card that fails to mount in a state these arms
  do not reach would reopen it, and the probe is how that would be shown.
