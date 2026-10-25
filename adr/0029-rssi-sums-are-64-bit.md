---
id: '0029'
title: "RSSI sums are 64-bit"
type: architecture
status: accepted
date: 2026-09-17
supersedes: []
superseded_by: []
---

## Context

1. **Two running sums overflow a signed 32-bit field.** `SSIDBaseline::rssiSum` and
   `ObservationStats::rssiSum` are `int32_t`, and every observation adds an `int8_t` sample to
   them with no guard. At the -128 floor the sum leaves 32 bits on observation 16,777,217
   (2^31 / 128 + 1); at a typical -70 dBm, on about 30.7 million. Signed overflow is undefined
   behaviour. Measured on the host with the tests this change adds: at the floor, both averages
   read 127 dBm where -128 was fed in.

2. **The averages also divide by a count cast to `int32_t`.** Both `rssiAverage()` functions
   cast a `uint32_t` count to `int32_t`, which goes negative past 2^31. Measured: an entry with
   3,000,000,000 samples of -70 averaged 0.

3. **How soon each one is reached.** `make verify-race` on 2026-09-17
   (`race-verify-cardputer/2026-09-17T11-44-09.953Z-PASS`) measured 8.2 frames/s over 125 s in
   a room with 9 SSIDs, so about 0.9 frames/s per SSID.
   - `SSIDBaseline` counts one SSID's frames. It is persisted (ADR-0017), so its count runs
     across a case's whole life. At that rate its sum overflows after about a year.
   - `ObservationStats` counts every frame. It is not persisted, but at the measured rate its
     sum overflows after about 43 days of uptime at -70 dBm, and sooner in a busier room. The
     frame rate is a property of the air, so no measurement bounds it.

4. **The persisted sum is read with `strtol`, which returns `long`.** `long` is 32 bits on the
   ESP32 and 64 on the x86-64 host. So a wider figure in `baseline.json` would read whole on
   the host and clamp to `LONG_MIN` on the device. That is ADR-0028's defect in a parser rather
   than a format string, and a host test cannot see it.

5. **The options, as the ledger item stated them.** Saturating the sum keeps the field and
   the document as they are, but the stored average freezes at whatever it was when the sum
   stopped moving. A running mean in place of the sum changes what the persisted field means
   and loses precision on every update. Widening the sum to 64 bits changes the field's type
   and the document's ceiling, and nothing else.

## Decision

1. **Both RSSI sums are `int64_t`; both counts stay `uint32_t`.** The largest sum a 32-bit
   count can reach is 128 × (2^32 − 1), about 5.5 × 10^11, far inside 2^63. So no count this
   type can hold overflows the sum. That is a bound on the types, not an estimate of the air.
   A `static_assert` beside `SSIDBaseline`'s fields states it, so widening the count later has
   to re-establish it.

2. **The average divides in 64 bits.** The count is cast to `int64_t`, never to `int32_t`.

3. **The document carries the sum through `PRId64`** (ADR-0028), and **the parser reads
   integers with `strtoll` and `strtoull`**, which return `long long` on both platforms, never
   `strtol` or `strtoul`. The unsigned reader moves too: once it is the only reader still
   returning a platform-width type, the next wide field to reach it inherits the defect.
   **A figure wider than 32 bits saturates at `UINT32_MAX` and does not wrap.** On the device,
   `strtoul` already saturated. On the host it wrapped, so a document holding 2^32 read as a
   count of 0 there. A plain `strtoull` and a cast would have carried that wrap to the device,
   and `rssiCount` or `lastSeenSession` at 0 switches off exactly the check ADR-0017 persisted
   them for. The adversarial review of this change measured it, and `make test` now asserts
   the saturation on both.

4. **The ceiling follows the format** (ADR-0017 invariant 20). One field widens from 11 to 20
   printed characters. Measured on the host, `kMaxBaselineEntryBytes` goes from 604 to 613 and
   `kMaxBaselineFileBytes` from 38,704 to 39,280. The reader allocates the file's own size
   rather than the ceiling, so the ceiling costs no RAM. The one ceiling-sized buffer, the
   entry text in `writeBaselineDocument`, grows by 9 bytes on the stack.

5. **Documents already on a card parse unchanged.** Every sum they hold fits 32 bits, and
   the key and its position are the same. No migration.

## Consequences

- The overflow `[bug]` in `LEDGER.md` is closed. `make test` asserts both averages past the
  32-bit overflow point, the division by a count past 2^31, and a sum past 32 bits round-tripping
  the document. The ceiling test formats `INT64_MIN`.
- **A count still wraps at 2^32**, which is defined behaviour, and after it the average is
  wrong. For `SSIDBaseline` that is more than a century per SSID at the measured rate.
  `ObservationStats` wraps after about 16 years at the measured rate, or 50 days at 1,000
  frames/s. Its `rssiAverage()` has no reader in the firmware, only the host suite. This ADR
  does not widen the counts: a wrap is not undefined behaviour, and widening `beaconCount`,
  `totalFrames` and their neighbours is a different change with a document cost of its own.
- **The parser's width is enforced by review, not by a gate.** `make test` cannot catch
  `strtol` on a host where `long` is 64 bits, and `make build` compiles it without complaint.
  The rule is stated at the reader.
