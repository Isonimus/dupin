---
id: '0003'
title: "The host suite asserts only behaviour the host can reach"
type: architecture
status: accepted
date: 2026-09-10
supersedes: []
superseded_by: []
---

## Context

`pio test -e native` was red — `250 test cases: 6 failed` — and had been for some time. No
CI runs it, so nothing announced the regression. ADR-0002 corrected the wrong root cause
recorded in ADR-0001; this record states the right one and the rule that falls out of it.

**The four decay tests asserted behaviour a product decision had removed.** `git log -S`
places both changes in one commit, `a8b524b` "feat(ux): Manual dismiss for anomalies and
findings":

    src/modes/marple/marple.h:  CONFIDENCE_DECAY_PER_SECOND = 0.0f   // manual dismiss only
    src/modes/holmes/holmes.h:  HYPOTHESIS_CONFIDENCE_DECAY = 0.0f   // cascade from dismiss

That commit gave the operator an explicit dismiss action and turned automatic decay off, so
a raised anomaly now stands until dismissed rather than fading on its own. It is a coherent
decision — a passive monitor that quietly forgets what it saw is worse than one that waits
to be acknowledged — but it was never written down, and the four tests asserting the old
fading behaviour (`test_marple_anomaly_confidence_decays`, `test_marple_anomaly_expires`,
and the Holmes pair, last touched in `755cb61`/`b258629`) were left in place. Nothing was
broken: `decayConfidence()` and `expireOldAnomalies()` still work, and both rates are still
configurable. The tests were stale.

**The two storage tests asserted a mock that does not exist.** Both did
`mgr.loadSession("test")` and then expected the write to succeed, with the comment "In
native build, this should succeed (mocked)". There is no such mock. `src/utils/file_utils.h`
compiles every SD operation out under `NATIVE_BUILD` and returns false, which is the correct
behaviour — a host stub that returned true would be a fabricated success, exactly the
masked failure `~/.claude/CLAUDE.md` §3 forbids. Session *selection* does succeed on the
host, because it is in-memory bookkeeping; a *write* cannot, because there is no card.

Measured before the change, three of the six were not just stale but vacuous: `saveBaseline`,
`logAnomaly`, `logFinding` and `logHypothesis` wrapped their whole bodies in
`#ifndef NATIVE_BUILD ... #else return false; #endif`, so on the host they never reached
either their own precondition or the filesystem. A test over a function whose body is
compiled out asserts the `#else` line and nothing else.

## Decision

**1. Automatic confidence decay stays off by default.** `CONFIDENCE_DECAY_PER_SECOND` and
`HYPOTHESIS_CONFIDENCE_DECAY` remain `0.0f`; an anomaly or hypothesis stands until the
operator dismisses it, or until a caller configures a non-zero rate. Both constants now
cite this ADR at the site, per stele:ADR-0012, so the next reader sees the zero is deliberate.

**2. The decay tests assert the default and the mechanism separately.** Four tests per mode
replace the two stale ones: confidence holds and the record survives under the default
config, and — with a rate configured through `setConfig` — confidence falls by exactly that
rate per elapsed second and the record expires below `minConfidence`. The pair matters: the
default tests alone would pass if `decayConfidence()` were deleted, and the mechanism tests
alone would pass if the default were changed.

**3. A storage write never reports success on the host.** The native arm of every SD
operation refuses. Host tests may assert name validation, session selection, preconditions,
and that a write refuses; they may never assert that a write succeeded. Anything that has to
prove bytes reached the card is device-verified (ADR-0002 invariant 2).

**4. A platform cutout wraps only what is genuinely platform-specific.** `logAnomaly`,
`logFinding` and `logHypothesis` lose their `#ifndef NATIVE_BUILD` wrappers entirely: the
precondition and the JSON formatting are portable, and `appendJsonObject()` is the one
boundary that needs the guard — it already has it. `saveBaseline` and `loadBaseline` keep
theirs, because they drive the SD `File` API directly and streaming to a `File` cannot be
expressed through the portable helpers without buffering a whole baseline in RAM, which an
ESP32 with 320KB of DRAM cannot spend. Their preconditions are hoisted out of the guard all
the same, so the check exists on both platforms.

## Consequences

`pio test -e native` is green: 251 test cases, 0 failed. The ledger's red-suite bug line is
deleted, which is how an item closes.

The four new decay tests were mutation-checked rather than assumed: setting
`CONFIDENCE_DECAY_PER_SECOND` to `0.01f` fails both default tests, and removing the
subtraction in `decayConfidence()` fails both configured tests. The storage pair was checked
the same way — making `appendJsonObject` return true on the host fails
`test_log_anomaly_refuses_without_sd_storage`. That mutant does **not** fail
`test_save_baseline_refuses_without_sd_storage`, because `saveBaseline`'s body is compiled
out on the host; that test guards its `#else` arm and no more, which its comment says and a
ledger line tracks.

Decision 4 narrows what a future `#ifndef NATIVE_BUILD` may cover. A cutout that swallows a
precondition does not merely reduce coverage — it makes a test of that precondition pass
while proving nothing, which is worse than having no test, and it is how three of these six
failures stayed invisible.
