---
id: '0011'
title: "Board macros declare only hardware that exists, and the board names itself at boot"
type: architecture
status: accepted
date: 2026-09-12
supersedes: []
superseded_by: []
---

## Context

`LEDGER.md` carried a one-line bug: the `cardputer_adv` environment did not boot. On the
Cardputer ADV in hand it panicked on Core 0 at `EXCVADDR: 0x00000004` before `setup()`
printed anything, while `default_envs = cardputer` booted on the same hardware. The line
suspected `-DBOARD_HAS_PSRAM`. Reproducing it through `make verify-boot` turned that
suspicion into three measurements, and the second and third were not in the line.

**The panic is inside the Arduino core's own PSRAM failure path.** The device names the
cause one line before it dies:

```
E (211) quad_psram: PSRAM chip is not connected, or wrong PSRAM line mode
Guru Meditation Error: Core  0 panic'ed (LoadProhibited). Exception was unhandled.
```

Decoded against `firmware.elf`, the backtrace is entirely framework and IDF:

| Frame | Location |
|---|---|
| `systimer_ll_counter_snapshot` → `systimer_hal_get_counter_value` | `hal/systimer_hal.c:50` |
| `esp_timer_impl_get_time` | `esp_timer/src/esp_timer_impl_systimer.c:72` |
| `psramInit` | `cores/esp32/esp32-hal-psram.c:71` |
| `__esp_system_init_fn_init_psram_new` | `cores/esp32/esp32-hal-misc.c:264` |
| `do_system_init_fn` → `do_core_init` → `start_cpu0_default` → `call_start_cpu0` | `esp_system/startup.c` |

Line 71 is `if (esp_psram_init() != ESP_OK)`. Inside that call the chip-absent path reaches
`esp_timer` before the systimer HAL is initialised and loads through a null context, which
is the `EXCVADDR: 0x00000004`. The hook is registered `ESP_SYSTEM_INIT_FN(init_psram_new,
CORE, BIT(0), 99)`, so it runs during core init — long before `app_main`, and therefore
before any line Dupin prints. The silence was not Dupin failing quietly; Dupin had not
started.

**There is no PSRAM to configure.** The error message admits two readings — absent chip, or
present chip in the wrong line mode — and they imply opposite fixes. The chip's own eFuses
settle it:

```
Chip type: ESP32-S3 (QFN56) (revision v0.2)
Features:  Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded Flash 8MB (GD)
```

`Embedded Flash 8MB` and no PSRAM entry: this is an ESP32-S3FN8, flash-only. Switching to
octal mode would have been a fix for a defect that was not present. The flag was a claim
about hardware, and the hardware disagreed.

Removing that one flag and re-running the gate is the proof in both directions — panic with
it, and with it gone:

```
result:        PASS
environment:   cardputer_adv
marker:        [BOOT] Initialization complete
reset-to-marker+settle: 33264 ms (incl. 8000 ms settle)
```

> **Correction (2026-09-12, ADR-0015):** the `reset-to-marker+settle` label is wrong. That
> figure is the serial tap's own lifetime, not the board's boot — `captureUntil` re-checked its
> settle deadline only when a line arrived, and this device goes quiet once the menu is up. The
> same gate on the same env now reports `reset-to-marker: 3741 ms`. The PASS above stands: the
> verdict never depended on the number, only the number did.

**Every other board macro was already inert.** Auditing the rest of the block, rather than
stopping at the flag that crashed, found that none of them reach any code:

| Macro | Uses in `src/`, `include/`, `test/` |
|---|---|
| `DUPIN_HAS_DISPLAY`, `_KEYBOARD`, `_IR`, `_SDCARD`, `_SPEAKER`, `_MIC` | 0 |
| `DUPIN_HAS_IMU`, `_AUDIO_CODEC`, `_AUDIO_OUT` | 0 |
| `DUPIN_BOARD_CARDPUTER`, `_ADV`, `_GENERIC_S3` | select `DUPIN_BOARD_NAME` only |
| `DUPIN_BOARD_NAME` | defined 5×, read 0× |

So the three board environments differed in no compiled code. Measured on this commit, the
`cardputer` and `cardputer_adv` images have byte-identical `.text`; the whole 16-byte size
difference is in `.flash.rodata`, and it is four M5GFX/M5Unified `__FILE__` strings
embedding `.pio/libdeps/cardputer_adv/`, which is four characters longer than the
`cardputer` path. `-DBOARD_HAS_PSRAM` was the only flag in that block that ever changed the
firmware, and what it changed was whether the board booted.

This also corrects a premise recorded in the `LEDGER.md` entry for the M5StickC Plus 2
port, which attributes eight dead board guards to their `DDUPIN_*` misspelling. The
misspelling was real and did disable that branch's guards. But on master these macros are
dead **spelled correctly** — there are no `#ifdef`s to reach — so a reader could fix every
name and still find nothing guarded.

## Decision

**1. `-DBOARD_HAS_PSRAM` is removed from `cardputer_adv` and must not return.** The module
has no PSRAM die, and on this framework declaring one is not a no-op that wastes a
configuration bit — it is a boot loop in framework code we do not own. The reason is
written at the flag's former site in `platformio.ini`, not only here, because the next
person to add ADV capabilities will read that file and not this one.

**2. A board macro declares only hardware this firmware has a driver for.** The ADV's
BMI270 IMU, ES8311 codec and headphone jack are real, and `DUPIN_HAS_IMU`,
`_AUDIO_CODEC` and `_AUDIO_OUT` are removed anyway: Dupin is a passive Wi-Fi receiver with
no code for any of them, and ADR-0004 scopes it to 802.11 management frames. A macro
naming absent support is indistinguishable, at a glance, from one naming present support —
which is exactly how `-DBOARD_HAS_PSRAM` survived being wrong.

**3. `DUPIN_HAS_DISPLAY`, `_KEYBOARD`, `_IR`, `_SDCARD`, `_SPEAKER` and `_MIC` stay,
unread.** They are kept deliberately against the rule that dead code is deleted, and the
justification is recorded here rather than taken silently. Deleting them makes a future
`#ifdef DUPIN_HAS_KEYBOARD` evaluate false on a Cardputer that has a keyboard — a guard
that compiles, links, and silently removes input. That is precisely the failure the
`DDUPIN_*` typo produced, and it cost a whole port. These six name peripherals this
firmware drives today; the three removed above named peripherals it does not. The
distinction is what this decision rests on, not a headcount of unused symbols.

**4. The board names itself in the boot transcript.** `DUPIN_BOARD_NAME` was defined five
times and read nowhere; `setup()` now prints `[BOOT] Board: <name>` immediately after the
version banner. The reason is the harness, not decoration: `scripts/boot-verify.mjs` keeps
the serial transcript as its artifact, the two envs produce byte-identical `.text`, and
without this line no artifact can be attributed to the environment that produced it. It is
a concatenated string literal rather than a format argument, so it introduces nothing for
invariant 6 to check.

**5. The ADV gate gets its own `make` target.** `make verify-boot-adv` runs the boot gate
against `cardputer_adv`. This decision's regression proof is a device boot, and
`make verify-boot` exercises only the default env — the defect lived in `platformio.ini`,
where one environment can be broken while the other is green. A proof reachable only by
remembering `DUPIN_ENV=cardputer_adv` is the "checker with no entry point runs once"
failure ADR-0009 already names.

## Consequences

Standing invariant row 10 is added to `CLAUDE.md` §4 in the same commit as this ADR. It is
`verified_by: make verify-boot-adv` for the PSRAM half specifically — that target boots the
env whose configuration the claim is about — and `review-only` for the general rule, since
nothing mechanically detects a `-D` naming hardware that is absent.

`cardputer_adv` now boots, so its `LEDGER.md` line is closed by deletion. The open question
that line posed — repair the env or retire it — is answered by repair, but the measurement
that closes it is worth keeping: retiring it was defensible, because the env produces no
distinct firmware today. It is kept because it is where ADV-specific support will attach,
and because `default_envs = cardputer` booting on ADV hardware is a property of M5GFX
autodetecting the panel, not something this repo controls.

The M5StickC Plus 2 ledger line is corrected in the same commit to say that fixing the
macro spelling is necessary and not sufficient, since master defines these guards without
reading them. The port's real requirement is unchanged and is recorded in ADR-0005: a
logical input abstraction, not more `#ifdef`s.

`DUPIN_VERSION_STRING` has the same defect this decision fixes for `DUPIN_BOARD_NAME` — it
is defined in `include/dupin.h` and never read, while `src/main.cpp` carries three
hardcoded `v0.2.1` literals that disagree with its `0.2.0`. It is **not** fixed here. Which
number is true is a decision and not a cleanup: the commit that introduced `0.2.1` into
`main.cpp` is titled `feat(ux): Table navigation and detail popups v0.2.0`, and
`include/dupin.h` has not been touched since the initial project structure. It is carried
as a `LEDGER.md` line so that choosing the version is done deliberately rather than folded
into a boot fix.
