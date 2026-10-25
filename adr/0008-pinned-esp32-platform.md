---
id: '0008'
title: "Pinned ESP32 platform: pioarduino 55.03.34"
type: architecture
status: accepted
date: 2026-09-11
supersedes: []
superseded_by: []
---

## Context

`LEDGER.md` carried a one-line bug: `[esp32s3_common]` declared a bare
`platform = espressif32`, so the platform a build resolved was a function of the builder's
install history rather than of the repo. ADR-0007 recorded the divergence it had already
measured — 55.3.34 on this machine, 7.1.2 in an empty core dir — and deliberately left the
pin to its own evidence, on the grounds that choosing between them changes what every
developer compiles. This is that evidence.

**The choice is between two lineages, not two version numbers.** `platformio/espressif32`
and `pioarduino/platform-espressif32` are separate projects, and the gap between them is a
whole ESP-IDF major plus three years of compiler. Measured by building this commit four
times, each in its own `PLATFORMIO_CORE_DIR`:

| | pioarduino 55.03.34 | pioarduino 55.03.37 | pioarduino 55.03.311 | platformio 7.1.2 |
|---|---|---|---|---|
| Arduino core | 3.3.4 | 3.3.7 | 3.3.11 | **2.0.17** |
| ESP-IDF | 5.5.1 | 5.5.2 | 5.5.5 | 4.4 |
| `xtensa-*-g++` | 14.2.0 | 14.2.0 | — | **8.4.0** (2021r2) |
| in the PlatformIO registry | no | no | no | yes |
| requires PlatformIO Core | >=6.1.18 | >=6.1.18 | **>=6.1.19** | 6.1.18 ok |
| `pio run -e cardputer` | SUCCESS | SUCCESS | **refused to install** | SUCCESS |
| RAM | 104276 B (31.8%) | 105940 B (32.3%) | — | 105644 B (32.2%) |
| Flash | 1037659 B (31.0%) | 1081531 B (32.4%) | — | 962737 B (28.8%) |
| `-Wformat-truncation` warnings | 3 | 3 | — | **1** |
| `-Werror=format` errors | 0 | 0 | — | 0 |

The official platform's Arduino framework package is
`framework-arduinoespressif32 4.20017.260907+sha.dcc1105b`. Its version was republished
under a new epoch on 2026-09-07, but the git sha is `dcc1105b` — the same sha as
`3.20017.241212`, and `esp_arduino_version.h` in the installed package reads major 2, minor
0, patch 17. The 7.x platform's `framework-espidf ~4.60100.0` and `toolchain-xtensa-esp-elf
15.2.0` are for the `espidf` framework; an `arduino` build like this one gets Arduino 2.0.17
on GCC 8.4.0 regardless.

**The decisive measurement: the official platform silently disarms half of invariant 6.**
ADR-0006 established that `%u` on an uncast `uint32_t` passes the host gate and fails the
device gate, and made both gates required because neither catches what the other does.
Re-running that experiment — dropping one `static_cast<unsigned>` at
`session_manager.cpp:517` and building clean:

| Platform | `%u` on an uncast `uint32_t` |
|---|---|
| pioarduino 55.03.34 | **fails, exit 1** — `format '%u' expects argument of type 'unsigned int', but argument 5 has type 'long unsigned int'` |
| pioarduino 55.03.37 | fails, exit 1 — same diagnostic |
| platformio 7.1.2 | **passes, exit 0** |

The cause is not a blind compiler. Asking each build what `uint32_t` actually is — by
writing `Serial.printf("%s", static_cast<uint32_t>(0))`, whose pointer-versus-integer
mismatch every GCC diagnoses with the type spelled out — gives:

| Build | `uint32_t` is |
|---|---|
| host (`pio test -e native`, Ubuntu g++ 13.3.0) | `unsigned int` |
| device, pioarduino 55.03.34 | `long unsigned int` |
| device, platformio 7.1.2 | **`unsigned int`** |

GCC 8.4.0 reported that mismatch correctly, so it checks formats fine. Under Arduino 2.0.17
the device and the host simply agree about `uint32_t`, so `%u` on an uncast one is genuinely
correct there and the compiler is right to stay silent. ADR-0006's premise — "the two
targets disagree about what `uint32_t` is" — is therefore a property of the IDF-5 lineage,
not of the ESP32-S3. Pinning the official platform would leave a row of ADR-0006 §5's table
false and invariant 6's `make build` half enforcing nothing, while the row kept claiming it.

This oracle was run through the project's own build, invoked as `pio run -e cardputer` on a
copy of this commit. ADR-0006 recorded that a bare compile with the toolchain's default
newlib headers gives the opposite answer and is not to be trusted; that rule was followed
here rather than rediscovered.

**PlatformIO Core bounds the reachable versions.** pioarduino declares an `engines` floor,
and it moved: `55.03.34`, `.35`, `.36` and `.37` require `platformio >=6.1.18`, while
`.38`, `.39` and `.311` require `>=6.1.19`. This machine and the CI pin from ADR-0007 both
run 6.1.18, so 55.03.311 did not fail to compile — PlatformIO installed it, then removed it
again and raised `IncompatiblePlatform`. The three newest releases are unreachable without
also bumping the core, which is a separate change to what every developer runs.

**There is no git remote and no device gate.** As ADR-0007 recorded, this repo has no
`origin`, so CI is inert. More importantly here, nothing in this repo can flash and run the
firmware: ADR-0002 split enforcement precisely because a device gate needs hardware on a USB
port, and no verify script exists yet. A platform bump is a change to what runs on the
device, and right now no check in this repo would notice if it regressed.

## Decision

**1. The device platform is pinned to pioarduino 55.03.34, by URL.**
`[esp32s3_common]` declares the release asset explicitly. The lineage is chosen over the
official platform for the reason measured above: it is the one on which invariant 6's device
half actually enforces something. The secondary reasons point the same way — Arduino 3.3.4
on ESP-IDF 5.5.1 against 2.0.17 on 4.4, and GCC 14.2.0 against 8.4.0 — but they are not what
decides it. A monitor-mode tool that calls `esp_wifi_set_promiscuous*` directly should not be
built on an IDF generation frozen in 2024 when the alternative is current.

**2. The pin is a URL rather than a registry spec, because pioarduino is not in the
registry.** `pio pkg show pioarduino/espressif32` reports
`Could not find 'pioarduino/espressif32' package in the PlatormIO Registry`, so
`platform = pioarduino/espressif32@55.03.34` cannot resolve; the project's documented spec
is the GitHub release asset. This is a real cost and is recorded as one: a registry version
is immutable, whereas a GitHub release asset can be re-uploaded under the same tag, so the
pin fixes the URL but not the bytes. It was accepted rather than mitigated because the
alternative is the platform that disarms invariant 6, and because the URL at least makes the
version a fact about the repo instead of a fact about the machine — which is the entire
defect being closed. Verified reachable from an empty core dir: pioarduino 55.03.37
installed from its URL and built green on PlatformIO Core 6.1.18, which is the path CI will
take.

**3. The pin is 55.03.34 — the version already in use — not the newest reachable one.**
55.03.37 was measured green here, with identical diagnostics and the invariant-6 gate still
firing, so it is a viable upgrade. It is nonetheless not what ships in this commit, because
pinning and upgrading are different acts. 55.03.34 is the platform every measurement in this
repo was taken against, including ADR-0006's and the truncation sites in `LEDGER.md`, and it
is what has actually been flashed and run. Pinning it changes no byte of the firmware and no
number in any existing record; it only removes the machine-dependence. 55.03.37 would add
43872 bytes of flash and 1664 bytes of RAM to a binary no check in this repo can run — see
the device-gate gap above — so it is carried as a `LEDGER.md` line to be done when there is
something that could verify it, not folded into a change whose whole purpose is to make the
status quo reproducible.

> **Correction (2026-09-12, ADR-0018):** the flash figure is wrong, and so is the row it
> comes from in the table above. Measured on this repo by building each pin back to back in
> its own core dir, 55.03.37 adds **3836 bytes of flash and 1272 bytes of RAM**, not 43872
> and 1664. The 55.03.34 column reproduces exactly once later commits are allowed for; the
> 55.03.37 row does not, and the four measurements here were each taken in a separate
> `PLATFORMIO_CORE_DIR`, so that row cannot now be reconstructed. **The deferral this
> paragraph argues for is unaffected**: it rested on there being no gate that could run the
> binary, not on the size of the delta.

**4. PlatformIO Core stays at 6.1.18.** ADR-0007 pinned CI to the version this machine runs,
and that match is worth more than three pioarduino releases. Raising the floor to 6.1.19 to
unlock them is its own decision, with its own ledger line, and it must move the local
install and the CI pin together or it reintroduces exactly the host-versus-CI divergence this
ADR closes.

**5. The host environment's `platform = native` is left unpinned, and that is a declared
gap.** `platform-native` supplies no compiler; the host gate compiles with whatever `g++` is
on `PATH` — 13.3.0 here, the runner image's default in CI. So the host half of invariant 6
rests on `uint32_t` being `unsigned int` on x86-64 Linux, which is true but is not something
this repo pins. Pinning it would mean shipping or containerising a toolchain, which is a
large change for a property that has never varied. It is written down rather than fixed
because an unpinned half that nobody has noticed is worse than one that is on the record.

## Consequences

ADR-0006's contract stops being accidentally true. Its §5 table is a claim about this
repo's device build, and until now that build was whatever the machine happened to have;
from this commit the repo names it, so the claim is checkable. The reverse also holds and is
the new rule: the table is only true for this lineage, so changing the pin means re-running
that measurement before the change lands, not after.

Standing invariant row 8 is added to `CLAUDE.md` §4 in the same commit as this ADR. It is
`review-only`: nothing mechanically checks that a platform bump was accompanied by a
re-measurement, and claiming otherwise would be the overclaim §4 exists to prevent.

The `-Wformat-truncation` count is now determinate. `LEDGER.md` counts three sites in
`main.cpp` (lines 427, 1421 and 1839) and that is what the pinned platform reports; the
single site 7.1.2 reported was never a disagreement about the code, only about how hard the
compiler looked. Whoever fixes them still sizes the buffers to their real inputs rather than
to a warning count, as that line already says.

The CI cache key in `.github/workflows/firmware.yml` becomes meaningful. Its comment
recorded that the key could not change when the resolved platform version did, because the
spec was unpinned; the version now lives in `platformio.ini`, so `hashFiles('platformio.ini')`
changes with the pin and a bump gets a cold toolchain rather than a stale cached one. The
comment is corrected in this commit. Citations inside `.github/` are still unchecked by the
linter — stele:ADR-0020 scopes it to `LEDGER.md`, the corpus, `CLAUDE.md`, `README.md`,
`docs/` and `.claude/commands/` — so that correction is a manual obligation, as ADR-0007
already noted.

Two `LEDGER.md` lines open and one closes. The bare-platform bug is closed by deleting its
line. The upgrade to 55.03.37 and the coupled PlatformIO Core bump that would unlock
55.03.311 are each recorded as a line citing this ADR.
