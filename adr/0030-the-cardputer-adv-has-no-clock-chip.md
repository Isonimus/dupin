---
id: '0030'
title: "The Cardputer ADV has no clock chip"
type: architecture
status: accepted
date: 2026-09-17
supersedes: []
superseded_by: []
---

## Context

1. **A case has no creation time, and ADR-0027 assumed a board it could come from.** ADR-0027
   deleted `created` and `lastSaved` from `case.json`, because both held seconds since boot.
   Among the rejected options it named "Wire the ADV's hardware RTC", so a real timestamp looked
   like wiring on one of the two boards. The operator reported that the Cardputer v1.1 has no
   clock chip, and doubted the ADV claim. `LEDGER.md` deferred the timestamp until a board check
   settled it.

2. **M5Unified's own answer is weaker than it looks.** Dupin calls `M5Cardputer.begin()` with the
   default config, so `internal_rtc` is on and `external_rtc` is off. On an ESP32-S3 board,
   M5Unified 0.2.11 probes the internal I2C bus for one part only: a PCF8563/BM8563 at 0x51.
   `M5.Rtc.isEnabled()` returning false rules out that part, not a clock. On the v1.1,
   M5Unified's pin table gives the internal bus no pins (255, 255), so nothing is probed there at
   all.

3. **Measured on the ADV, 2026-09-17**, with `scripts/rtc-probe.mjs`
   (`rtc-probe-cardputer_adv/2026-09-17T13-55-49.962Z-PASS`):

   ```
   [BOOT] RTC: none; board=CardputerADV; internal I2C answers at 0x18 0x34 0x69
   ```

   M5Unified detected the board as an ADV at runtime, so the measurement is of the right
   hardware. Three addresses answer: 0x18 (the ES8311 audio codec), 0x34 (the TCA8418 keyboard
   controller) and 0x69 (the BMI270 IMU). Nothing answers at 0x51, and nothing else answers. A
   clock chip reachable by the firmware would have to sit on this bus, so the ADV has none.

## Decision

1. **Neither board has a clock chip.** The ADV measured none (Context 3), and the v1.1 has no
   internal I2C bus to carry one (Context 2). A case timestamp cannot read an on-board clock on
   either board. It needs a time source added first: an RTC unit on Port A, or a date the
   operator enters at the keyboard. Network time is not an option, because Dupin transmits
   nothing. Which source, and whether a wrong date is worse than none, stays deferred in
   `LEDGER.md`.

2. **The firmware reports the answer on every boot.** Right after `M5Cardputer.begin()`,
   `setup()` prints one `[BOOT] RTC:` line: whether M5Unified found a clock, which board it
   detected, and every address answering on the internal bus. The line stays rather than being
   reverted after one reading, for two reasons. A later board revision is answered by rerunning
   the probe rather than by rebuilding it. And the timestamp feature has to branch on exactly
   this runtime fact. The detected board is printed because `DUPIN_BOARD_NAME` names the env
   that built the image, and the two board envs build byte-identical code (ADR-0011).

3. **`scripts/rtc-probe.mjs` is a probe, not a gate.** It flashes the env, resets the board,
   parses that line, fails loudly on a line of any other shape, and asserts nothing about the
   answer. So it has no `make` target (CLAUDE.md §3). It defaults to `cardputer_adv`, and
   `DUPIN_ENV=cardputer` runs it on the v1.1.

## Consequences

- ADR-0027's option "Wire the ADV's hardware RTC" rested on hardware that does not exist, and a
  correction marker under it cites this record.
- The scan probes 112 addresses at boot. `make verify-boot-adv` measured `reset-to-marker:
  3758 ms` with it, against 3741 ms in ADR-0015 without it. Two single runs cannot separate the
  scan's cost from run-to-run variation. What they do bound is that the scan adds no visible
  delay to the boot.
- `external_rtc` stays off, so an RTC unit plugged into Port A is not probed and would read as
  `none`. Turning it on belongs to the change that adds such a unit.
- The v1.1's answer rests on M5Unified's pin table and the operator's report, not on a run of
  the probe. `DUPIN_ENV=cardputer node scripts/rtc-probe.mjs` is the run that would measure it.
