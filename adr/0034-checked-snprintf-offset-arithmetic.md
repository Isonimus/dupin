---
id: '0034'
title: "An accumulating snprintf offset is checked before it advances"
type: architecture
status: accepted
date: 2026-10-25
supersedes: []
superseded_by: []
---

## Context

CodeQL's `cpp/overflowing-snprintf` (CWE-190, flagged high) landed on the first public push, at
`reportClockChip` in `main.cpp`. The function builds the I2C scan line by appending `" 0xNN"`
for each answered address into a fixed buffer, advancing a running offset by each `snprintf`
return:

```cpp
written += static_cast<size_t>(snprintf(addresses + written, sizeof(addresses) - written,
                                        " 0x%02X", static_cast<unsigned>(address)));
```

`snprintf` returns the number of characters it *would have* written, not the number it wrote.
The buffer was hand-sized to hold every possible address (`(0x78 - 0x08) * 5 + 1`), so in this
firmware it cannot truncate and cannot overflow. But the safety rests entirely on that sizing:
if any write ever truncated, its return would exceed the space left, `written` would pass
`sizeof(addresses)`, and the next iteration's `sizeof(addresses) - written` would underflow
`size_t` to a huge value, which `snprintf` then treats as a nearly unbounded buffer, writing past
the object. The behaviour past that point is undefined, so no single overrun figure is
reproducible; the reproducible fact is the overrun itself, which `test/test_i2c_scan` catches with
a canary region placed after a deliberately undersized buffer: the canary is overwritten with the
guard removed and intact with it, so the test fails before the fix and passes after. A later,
wider format string, or a narrower buffer, turns a provably-safe call into a stack overflow with
nothing between the two.

The formatting loop was also unreachable by the host suite: `reportClockChip` reads `M5.In_I2C`
and lives under `#ifndef NATIVE_BUILD`, so the one piece of it that is pure arithmetic over a
`bool[]` had no test, against invariant 1.

## Decision

An `snprintf` whose return value advances a running buffer offset checks that return against the
space remaining *before* folding it into the offset: a negative result (encoding error) or a
result at or beyond the remaining size (truncation) stops the accumulation and re-terminates at
the last whole write, rather than advancing the offset past the buffer.

The loop is extracted into `dupin::formatI2cScanAddresses` (`src/utils/i2c_scan.{h,cpp}`), a pure
function compiled by both envs, with the buffer-run width named as `kI2cAddressRunChars` and the
caller's buffer sized from it (invariant 20). `test/test_i2c_scan` asserts the no-truncation path
fills exactly, the truncation path stops at a whole-address boundary, and a canary region after a
deliberately undersized buffer survives untouched: that canary test fails before the guard and
passes after.

## Consequences

- A new standing invariant (§4 #31) states the checked-offset rule for all future accumulating
  `snprintf` sites, `verified_by` `make test` for this site and CodeQL `cpp/overflowing-snprintf`
  for the class, since CodeQL is now live on this repo.
- The caller is shorter and the arithmetic is host-tested, so the CodeQL alert clears on the next
  push rather than being dismissed. Dismissing it would have recorded nothing and left the latent
  underflow in place for the next format change to re-arm.
- The guard adds a branch per answered address on a once-per-boot diagnostic path: no measurable
  cost, and it buys the invariant.
