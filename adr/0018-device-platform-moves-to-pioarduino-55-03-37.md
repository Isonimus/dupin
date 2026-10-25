---
id: '0018'
title: "The device platform moves to pioarduino 55.03.37"
type: architecture
status: accepted
date: 2026-09-12
supersedes: []
superseded_by: []
---

## Context

`LEDGER.md` carried this upgrade as a deferral with an explicit expiry condition: *"when a
serial-driven verify script exists to notice a regression"*. ADR-0008 deferred it for one
reason and stated that reason plainly — 55.03.37 was measured green, with identical
diagnostics and the invariant-6 gate still firing, but it would change a binary **no check
in this repo could run**. A platform bump is a change to device behaviour, and at the time
nothing in this repo could observe device behaviour at all.

That gap is closed. Four hardware gates now exist, and between them they cover the layers a
framework and toolchain change is most likely to disturb:

| Gate | What it would notice |
|---|---|
| `make verify-boot` | the firmware coming up at all, and not panicking in the 8 s after (ADR-0010) |
| `make verify-boot-adv` | the same on the second env, which is a separate build configuration (ADR-0011) |
| `make verify-input` | both dispatch chains running and changing observable state (ADR-0014) |
| `make verify-baseline` | the SD mount, and a document surviving a write, a reset and a read (ADR-0016) |

The condition the ledger line named is therefore over-satisfied: it asked for one script and
there are four.

**Invariant 8 governs how the change lands, not whether.** It requires that changing the pin
means re-measuring that the ADR-0006 format gate still fires, *before* the change lands. That
obligation exists because ADR-0008 measured the official `platformio/espressif32` lineage
silently disarming half of invariant 6 — on Arduino 2.0.17 the device and the host agree that
`uint32_t` is `unsigned int`, so the device half enforces nothing while the invariant table
keeps claiming it does. A pin that moved without that measurement could repeat the defect
quietly.

**The gate was measured on both platforms, in both directions, before the pin moved.** The
mutation site is `formatBaselineEntry` in `src/storage/session_manager.cpp`, chosen because
it is portable code compiled by *both* environments — the `logFinding` site ADR-0008 used now
sits inside a `#ifndef NATIVE_BUILD` cutout, so the host build never sees it and it can only
measure one column of the table. `entry.beaconCount` is a `uint32_t`, and the comment above
the call already cites ADR-0006.

| Mistake at `session_manager.cpp:789` | `make test` (host) | `make build` 55.03.34 | `make build` 55.03.37 |
|---|---|---|---|
| `%lu` on an uncast `uint32_t` | **fails, exit 1** | passes, exit 0 | passes, exit 0 |
| `%u` on an uncast `uint32_t` | passes, 310/310 | **fails, exit 1** | **fails, exit 1** |

Both device columns produce the same diagnostic, to the character:

```
error: format '%u' expects argument of type 'unsigned int',
       but argument 9 has type 'long unsigned int' [-Werror=format=]
```

So `uint32_t` is still `long unsigned int` on 55.03.37, and ADR-0006's premise — that the two
targets disagree about what `uint32_t` is — survives the bump. The host column is unchanged by
construction: the `native` env declares `platform = native` and this pin does not touch it.

**The cost is a tenth of what ADR-0008 projected.** ADR-0008 recorded that 55.03.37 "would add
43872 bytes of flash and 1664 bytes of RAM". Measured directly on this commit, in this repo's
own core dir, by building each pin back to back:

| | 55.03.34 | 55.03.37 | delta |
|---|---|---|---|
| Arduino core | 3.3.4 | 3.3.7 | |
| ESP-IDF | 5.5.1 | 5.5.2 | |
| `xtensa-esp-elf-g++` | 14.2.0+20250730 | 14.2.0+20251107 | |
| RAM | 104236 B (31.8%) | 105508 B (32.2%) | **+1272 B** |
| Flash, `cardputer` | 1037979 B (31.1%) | 1041815 B (31.2%) | **+3836 B** |
| Flash, `cardputer_adv` | — | 1041831 B | |
| warnings in `src/` | 0 | 0 | |

The 55.03.34 column reproduces ADR-0008's own figures once this repo's subsequent commits are
allowed for (1037659 → 1037979 flash, 104276 → 104236 RAM), which is what makes the 55.03.37
row's 1081531 the odd one out rather than the code. ADR-0008 took its four measurements each
in a separate `PLATFORMIO_CORE_DIR`; that isolation is why its 55.03.37 row cannot now be
reconstructed, and it is also the honest reason to prefer the number measured here — the
configuration that ships is this one.

**The toolchain travels with the pin, and the two cannot coexist.** 55.03.34 declares
`xtensa-esp-elf-14.2.0_20250730` and 55.03.37 declares `_20251107`. PlatformIO unpacks both
into one unversioned directory, `~/.platformio/packages/toolchain-xtensa-esp-elf`, so moving
the pin either way re-downloads a compiler and overwrites the other. This was observed, not
inferred: reverting the pin mid-measurement printed
`Tool Manager: toolchain-xtensa-esp-elf@14.2.0+20250730 has been installed!` and reproduced
`1037979` exactly, which is what proves the baseline column was built with 55.03.34's own
compiler and that the delta above is a like-for-like comparison of two correctly-configured
builds.

## Decision

**1. The device platform pin moves to pioarduino 55.03.37.** `[esp32s3_common]` names the
55.03.37 release asset by URL. The mechanism is unchanged; only the version moves.

**2. ADR-0008 is not superseded, and this is deliberate.** Superseding it would set
`status: superseded` on a record whose decisions 2, 4 and 5 are still in force — the
URL-versus-registry reasoning, the PlatformIO Core floor, and the declared gap at
`platform = native` — and would mark them dead in the index while they still govern. Decision
1's *mechanism* (pin by URL to an exact pioarduino release) also stands; only its value moves.
Decision 3 was a deferral that named its own expiry condition, and this is that condition being
met, which is a decision executing as designed rather than being overturned. ADR-0008's
Consequences section legislates for exactly this moment — *"changing the pin means re-running
that measurement before the change lands, not after"* — so it governs this change instead of
being reversed by it. A correction marker is appended to its decision 3 for the flash figure
that did not reproduce, per the append-only rule; the figure it published was true of what that
tool printed, and is not edited.

**3. No new standing invariant is created.** Invariant 8 already covered this change, was
followed, and needs no amendment. Adding a row restating what the existing one enforces would
be the second-copy failure the taxonomy exists to prevent.

**4. PlatformIO Core stays at 6.1.18, and CI is unchanged.** 55.03.37 declares
`engines: platformio >=6.1.18`, so it is reachable without touching the floor; the separate
ledger line for 6.1.19 remains open and still gates 55.03.38 and later.
`.github/workflows/firmware.yml` needs no edit: it installs `platformio==6.1.18`, and its cache
key is `hashFiles('platformio.ini')`, which changes with this commit exactly as ADR-0008
intended. Its `restore-keys` prefix will still match the old cache and restore a stale
toolchain, and that is harmless for the reason measured above — PlatformIO compares the
installed package version against the spec and reinstalls on a mismatch.

**5. The upgrade is verified against device behaviour, not against a green compile.** That is
the whole reason it waited, so a compile alone would not discharge it. All four hardware gates
run on the new pin, and the boot gates run on both envs.

## Consequences

**Every gate is green on the new pin, and the device numbers barely move.** Measured on this
commit, board on `/dev/ttyACM0`:

| Gate | 55.03.34 (recorded at `0b908e8`) | 55.03.37 |
|---|---|---|
| `make check` | 17 documents, 0 errors, 0 warnings | 17 documents, 0 errors, 0 warnings |
| `make test` | 310/310 | 310/310 |
| `make build` | SUCCESS | SUCCESS |
| `make verify-boot` | PASS, reset-to-marker 3740 ms | PASS, reset-to-marker **3742 ms** |
| `make verify-boot-adv` | PASS, reset-to-marker 3741 ms | PASS, reset-to-marker **3741 ms** |
| `make verify-input` | PASS 7/7, 3963 ms | PASS 7/7, **3951 ms** |
| `make verify-baseline` | PASS, 28 saved / 28 restored | PASS, **32 saved / 32 restored** |

Boot time is unchanged to within two milliseconds on both envs, across the same 37 serial
lines. The baseline counts differ because the count is a property of the room the board is
sitting in, not of the firmware — that was established when the gate was written (ADR-0016),
and it is why the gate asserts *"the number that came back equals the number that went in"*
rather than any particular number.

**The truncation class is now covered by a newer compiler and still clean.** ADR-0013 removed
the `-Wno-error=format-truncation` exemption, so `-Werror=format` promotes truncation to a hard
error. GCC 14.2.0+20251107 analyses `snprintf` bounds differently from +20250730, and a site it
newly flagged would have failed this build rather than warned. It flagged none: the device
build reports zero warnings in `src/`.

**One `LEDGER.md` line closes and none opens.** The 55.03.37 deferral is deleted; the coupled
PlatformIO Core 6.1.19 line stays open, because 55.03.38 and later still require it.

**What this does not settle.** The `platform = native` env remains unpinned and that gap is
still ADR-0008 decision 5's, unchanged by this commit. Nothing here re-opens the lineage
question either: the choice of pioarduino over `platformio/espressif32` rests on the
invariant-6 measurement in ADR-0008, and this commit re-confirms that measurement holds one
release later rather than revisiting it.
