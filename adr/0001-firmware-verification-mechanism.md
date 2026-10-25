---
id: '0001'
title: "Verification mechanism for firmware behaviour"
type: architecture
status: superseded
date: 2026-09-10
supersedes: []
superseded_by: ['0002']
---

## Context

The method kit was vendored into Dupin in `f47e4b5` with its verification section written
for a browser target. `CLAUDE.md` §3 as shipped told the next session to write
`scripts/<slice>-verify.mjs` that "drives the real system headlessly" and "fails on any
console error or page error", and to wire it into `package.json`.

Dupin is ESP32-S3 firmware built with PlatformIO. There is no page and no browser console.
Prose that names a mechanism the repo does not have is worse than no prose: it reads as
authoritative and cannot be followed, so the next session either invents its own answer or
skips verification and calls it compliance.

Three measured facts shaped this decision.

**R11 currently enforces nothing here.** The rule returns early unless *both* `scripts/`
and `package.json` exist (`scripts/lint-docs.mjs` `harnessWiring`). Dupin has no
`package.json`, so `node scripts/lint-docs.mjs .` reports `0 error(s), 1 warning(s)` — and
would keep reporting zero errors with an unwired verify script sitting in `scripts/`. The
load-bearing half of the harness is inert, silently.

**The kit's own commands assume npm and are broken.** `.claude/commands/adr.md` step 7
instructs `npm run index` and `npm run lint`; the generated header in `adr/INDEX.md` says
"Regenerate with `npm run index`". Neither command can run. This is not a hypothetical
future need for `package.json` — two shipped instructions already depend on it.

**The host/hardware split is already established in this repo and is clean.** `test/`
holds 17 suites (`test_frame_parser`, `test_baseline`, `test_marple`, `test_holmes`, …)
run by `pio test -e native`, whose `build_src_filter` excludes `main.cpp` and
`capture/wifi_capture.cpp`. Pure logic — parsing, baselining, anomaly scoring, storage
bookkeeping — is therefore already host-assertable today. What is left over is exactly the
set the shipped §3 was reaching for: rendering, radio capture, SD/SPIFFS mounting, and
input handling.

That leftover set is not academic. The lost session recorded in `LEDGER.md` broke input on
the Cardputer and a `pio run -e cardputer` build still reported SUCCESS, because the dead
`DDUPIN_*` guards deleted the code before the compiler saw it. A green compile is not
evidence that the firmware behaves; nothing in this repo would have caught that.

There is also no CI: `.github/workflows/` does not exist, though `.claude/hooks/pre-commit`
names `.github/workflows/docs.yml` as its backstop for clones without the hook installed.

**And the host suite is currently red, which is how the absence of CI compounds.** Measured
on this commit, with source byte-identical to `1c81428`: `pio test -e native` exits 1 —
`250 test cases: 6 failed, 241 succeeded`, with `test_holmes`, `test_marple`, and
`test_session_manager` ERRORED. Four of the six are decay/expiry assertions defeated by a
mock clock that cannot be moved (`test/mocks/Arduino.h` returns `mock_millis++`, advancing
1 ms per call with no setter), so behaviour that depends on elapsed time is currently
unfalsifiable on the host. A suite nothing runs unattended is a suite that goes red and
stays red; these failures are older than this ADR and were found only because the checks
were run by hand while writing it.

## Decision

**1. Add a minimal `package.json`** whose sole purpose is dev-tooling wiring — `lint`,
`index`, and an aggregate `check`. It declares no runtime dependencies and ships no
JavaScript into the firmware. This repairs the two broken kit instructions above and
reactivates R11, rather than leaving a harness rule that looks enforced and is not.

**2. Host-testable logic is verified by `pio test -e native`, and that is the default.** A
slice whose behaviour can be asserted on the host ships Unity tests under `test/`, not a
verify script. Reaching for a verify script when a host test would do is how a suite of
device scripts accumulates for logic that never needed hardware.

**3. Hardware-dependent behaviour ships `scripts/<slice>-verify.mjs`** — a Node script
that drives the real device over the serial port. It replaces "fails on any console error
or page error" with the firmware equivalent, and fails when any of these hold:

- the `pio` subprocess it invokes exits non-zero;
- a serial line matches `/^\[(ERROR|FATAL)\]/`, this codebase's existing log convention;
- a serial line contains an ESP32 crash marker — `Guru Meditation`, `abort() was called`,
  or `Backtrace:`;
- the behaviour-specific assertion the slice claims does not appear within its timeout.

It writes artifacts — the captured serial transcript, plus any measured numbers — for
human review, is named in the slice's `## Verification` section, and is wired into
`package.json`, which R11 enforces.

**4. Probes are `scripts/<topic>-probe.mjs`**, exempt from wiring by R11's name rule. A
probe answers a design question once and its measured number lands in an ADR.

**5. Enforcement is split honestly, because a device gate cannot run unattended.** The
serial-driven half requires hardware on a USB port, so it is a **local** gate named in the
slice, not a CI check. What runs without hardware is the compile (`pio run`), the host
suite (`pio test -e native`), and the doc checks (`npm run check`). Any claim that a device
verify script "runs in CI" would be false here until a hardware runner exists.

## Consequences

`CLAUDE.md` §3 is rewritten in the same commit as this ADR to state the mechanism above,
and standing-invariant rows 1–3 are added for it. The shipped §3's browser framing is
replaced rather than annotated: it described a harness this repo never had, so there is no
earlier reasoning to preserve — the reasoning being corrected is recorded here, in the
Context section.

R11 becomes live the moment `package.json` lands. Any `scripts/*-verify.mjs` added from now
on and left unwired fails the commit, which is the intent.

A green `pio run` is explicitly **not** accepted as verification of behaviour anywhere in
this repo. The `DDUPIN_*` incident is the reason, and it is the reason invariant 3 names
the compile as necessary but insufficient.

Two gaps are deferred rather than solved, and carry ledger lines citing this ADR: there is
no CI workflow at all yet, and no verify script exists for the input path that the lost
session broke.
