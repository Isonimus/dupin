---
id: '0006'
title: "printf format contract for uint32_t across host and device"
type: architecture
status: accepted
date: 2026-09-11
supersedes: []
superseded_by: []
---

## Context

`LEDGER.md` carried a one-line bug: `session_manager.cpp` formatted `uint32_t` ids with
`%lu` in the `logFinding` and `logHypothesis` writers. Fixing it turned up three facts that
the line did not contain, each of them measured on this commit.

**The two targets disagree about what `uint32_t` is.** The compilers say so in their own
diagnostics:

- native (`pio test -e native`, x86-64 Linux g++): `uint32_t {aka 'unsigned int'}`
- device (`pio run -e cardputer`, ESP32-S3 Arduino): `uint32_t {aka 'long unsigned int'}`

So `%lu` is correct on the device and wrong on the host, and `%u` is correct on the host
and wrong on the device. **No plain conversion specifier is correct on both.** The original
`%lu` was therefore not simply a bug — it was right for the target that ships and wrong for
the target that tests.

**A measurement that looked authoritative and was not.** Compiling a bare `.c` file with
`xtensa-esp32s3-elf-g++` and its default newlib headers reports `%u` as correct for
`uint32_t`, with no warning. That is not the header set or the flags the Arduino build
uses, and it gives the opposite answer to the real build. The only trustworthy oracle is
the project's own build, invoked through `make`. This is recorded because the false reading
is cheap to obtain and convincing, and it sent one session down the wrong path.

**Neither build was checking.** The device `Print::printf` is declared
`__attribute__((format(printf, 2, 3)))`, but the `cardputer` env passed no `-Wall` or
`-Wformat`, so nothing was promoted or even reported. The host mock `MockSerialClass::printf`
in `src/ui/serial_logger.h` carried no format attribute at all, so `Serial.printf` was
entirely unchecked there. The host suite — the default gate under ADR-0002 — was thus
strictly weaker than the device compiler on this class, which is backwards for a harness
whose job is to catch things before hardware.

With the attribute added and `-Wformat` on, the tree held mismatches in both directions:
`%lu` on a `uint32_t` (host-wrong) across seven files, and `%u` on a `uint32_t`
(device-wrong) at `dupin_core.cpp:94,157,175` and `poirot.cpp:64,300` — the latter group
predating this work entirely, and invisible to every check the repo ran.

## Decision

**1. A `uint32_t` is printed with `%u` and an explicit `static_cast<unsigned>`.** `unsigned
int` is 32 bits on both targets, so the cast is lossless and the specifier is then exact
everywhere. `PRIu32` from `<cinttypes>` is the textbook alternative and was rejected on
readability: the JSON templates in `session_manager.cpp` carry four to six specifiers each,
and splicing them apart into `"...\"beacons\":%" PRIu32 ",..."` makes the format — the part
a reader must check against the data schema — materially harder to read. The cost is a cast
per argument, paid at the call site where the type is visible.

A counter that exists only to index may instead be declared `unsigned` outright, which
removes the cast rather than repeating it; `watson.cpp`'s baseline-load loop does this.

**2. The host mock mirrors the device declaration.** `MockSerialClass::printf` carries
`__attribute__((format(printf, 2, 3)))`, the same attribute the real `Print::printf` has.
Without it the host build silently accepts what the device build would reject.

**3. Both environments check formats as errors, on our sources only.** `platformio.ini`
sets `build_src_flags = -Wformat -Werror=format -Wno-error=format-truncation` for the
device common section and the native env. `build_src_flags` rather than `build_flags` is
deliberate: it applies to project sources and not to M5Unified, M5Cardputer or Unity, whose
diagnostics we cannot fix and must not be able to break our build.

**4. Truncation is demoted to a warning, not silenced.** `-Werror=format` promotes the
whole format family, which swept in `-Wformat-truncation` — a real but separate class
(bounded `snprintf` cutting displayed text, not a type mismatch). It stays visible as a
warning and is carried by a `LEDGER.md` line rather than fixed here, because mixing the two
classes in one change would make it atomic in neither.

**5. Both gates are required, because each catches what the other cannot.** Measured by
removing a single cast and a single specifier and running each env:

| Mistake | `make test` (host) | `make build` (device) |
|---|---|---|
| `%lu` on a `uint32_t` | fails, exit 1 | passes |
| `%u` on an uncast `uint32_t` | passes, exit 0 | fails, exit 1 |

A change verified on only one of them is not verified. This is the first invariant in this
repo that no single command can enforce.

## Consequences

Standing invariant row 6 is added to `CLAUDE.md` §4 in the same commit as this ADR, and is
`verified_by` the two build gates jointly — it is the reason the row names both.

The absence of CI matters more than it did. The existing `LEDGER.md` line for the missing
workflow already asks for `make check`, `make build` and `make test`; this decision is the
concrete case where running fewer than all three lets a defect through, in either direction.
The device half still needs no hardware — it is a compile, not a device gate under
ADR-0002 — so nothing here waits on a hardware runner.

The three `-Wformat-truncation` sites in `main.cpp` are deferred with a ledger line citing
this ADR. They are warnings today and a future change may promote them once the buffers are
sized to their real inputs.

Any future target whose `uint32_t` is neither of the two spellings seen here does not
invalidate the contract: the cast is to `unsigned`, which the specifier names exactly, so
the call sites stay correct without revisiting them.
