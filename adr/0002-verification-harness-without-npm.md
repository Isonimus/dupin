---
id: '0002'
title: "Verification harness without npm; R11 declared inert"
type: architecture
status: accepted
date: 2026-09-10
supersedes: ['0001']
superseded_by: []
---

## Context

ADR-0001, written hours earlier in this same session, adapted the vendored method kit's
verification section from its shipped browser framing to firmware. Its mechanism decisions
were right. Its first decision — **add a `package.json` so R11 becomes live** — was wrong,
and one of its Context claims was factually wrong. Both are corrected here rather than
edited into that record.

**Why the `package.json` reasoning was wrong.** ADR-0001 observed correctly that four of
the kit's own commands invoke npm (`/adr` step 7, `/slice` step 5, `/audit` steps 1–3,
`/wrap-up` step 1) and that `adr/INDEX.md`'s generated header says "Regenerate with
`npm run index`" — none of which could run. It then treated that as evidence that the repo
needed npm. It was evidence of the opposite: the kit ships written for JavaScript repos,
and the thing to adapt was the kit's prose, not this repo's toolchain. Adding a manifest to
satisfy prose inverts which artifact serves which. Dupin builds with PlatformIO and has no
JavaScript runtime dependency; a `package.json` here exists only to be a place for R11 to
look, which is a rule shaping a codebase instead of describing it.

Two further costs were not weighed. The manifest's `name` field is what
`scripts/lint-docs.mjs` (`repoName`) reads to recognise a self-qualified citation, so
naming it `dupin-docs` silently made `dupin:ADR-NNNN` resolve as *another* repo's decision
— unchecked, per stele:ADR-0009 — while `CLAUDE.md` §2 requires this repo's own citations
to be bare anyway. And an npm manifest invites `npm install` in a repo that must never
acquire a node_modules tree.

**Why the mock-clock claim was wrong.** ADR-0001's Context attributed four of the six red
host tests to "a mock clock that cannot be moved", citing `test/mocks/Arduino.h` returning
`mock_millis++`. Measured since: `src/utils/time_utils.h` already provides a settable clock
(`setMockMillis`, `advanceMockMillis`) under `NATIVE_BUILD`, and ten test suites already
drive it — including the four that fail, which call `advanceMockMillis` themselves. The
auto-incrementing definition in `test/mocks/Arduino.h` is never reached by those suites:
`time_utils.h` includes `<Arduino.h>` only when `NATIVE_BUILD` is *not* defined. The real
cause is elsewhere — commit `a8b524b` disabled confidence decay by setting its rate
constants to `0.0f` and the tests were never re-pointed — and gets its own record, cited
from `LEDGER.md`. The claim was asserted from a file read rather than measured,
which is the failure `~/.claude/CLAUDE.md` §4 exists to prevent, and it is repeated here
because a wrong root cause in a decision record is worse than none: the next session would
have rewritten a working clock.

**What is actually missing.** R11's purpose is sound — stele:ADR-0004 found eleven of
twelve verify scripts wired nowhere. But its mechanism is npm-shaped, and
this repo has zero `scripts/*-verify.mjs` today, so nothing is being enforced either way.
What the repo does need is a single named entry point for the checkers, because the kit's
commands must name *something* and `node scripts/lint-docs.mjs .` repeated across five
documents is the duplication stele:ADR-0005 exists to prevent.

## Decision

**1. No `package.json`.** It is deleted. Dupin acquires no JavaScript manifest and no
package manager; the three vendored `scripts/*.mjs` checkers run under plain `node`, which
they were written to do (zero dependencies).

**2. `Makefile` is the single entry point.** `make lint`, `make index`, `make index-check`,
`make check` (lint + index-check), `make build` (`pio run -e cardputer`), and `make test`
(`pio test -e native`). The kit's four commands and the generated `adr/INDEX.md` header are
rewritten to call these targets. `make` is the idiomatic entry point for a C++ repo and adds
no toolchain that a PlatformIO developer does not already have.

**3. R11 is declared inert here, out loud.** `harnessWiring` returns early when
`package.json` is absent, so it enforces nothing in this repo and never will unless
re-pointed. It is left in the vendored linter unmodified — forking a checker to chase a rule
this repo has no verify script for is cost with no present benefit. `CLAUDE.md` §3 states the
gap, §4 invariant 2 declares its enforcement `pending (LEDGER)`, and a ledger line carries
the debt with the concrete fix: teach R11 to read a `Makefile` target list, and extend the
pre-commit hook's extracted-path list to include `Makefile`, on the day the first verify
script is written. `CLAUDE.md` §2's standing demand — say so out loud rather than writing a
rule down and trusting it — is satisfied by that, not by the rule's presence.

**4. ADR-0001's mechanism decisions are carried forward unchanged**, and remain the law of
this repo:

- Host-testable logic is verified by `pio test -e native`, and that is the default; a slice
  whose behaviour a host test can assert ships Unity tests under `test/`, not a device script.
- Behaviour a host test cannot assert ships `scripts/<slice>-verify.mjs`, driven over the
  serial port, failing on a non-zero `pio` exit, a serial line matching
  `/^\[(ERROR|FATAL)\]/`, a line containing `Guru Meditation`, `abort() was called` or
  `Backtrace:`, or its own assertion not appearing within its timeout — and writing the
  serial transcript as an artifact for human review.
- Probes are `scripts/<topic>-probe.mjs`, answer a design question once, and put the measured
  number in an ADR.
- Enforcement is split: the device half is a local gate named in the slice, never claimed as
  CI until a hardware runner exists.
- A green `pio run` is never evidence of behaviour. The `DDUPIN_*` incident in `LEDGER.md` is
  the reason.
- Board feature guards are spelled `DUPIN_*`; the `-D` in `platformio.ini` is the compiler
  flag prefix, not part of the macro name.

## Consequences

`CLAUDE.md` §3 and §4 are rewritten in this commit: the wiring bullet names a `make` target,
the R11 paragraph states the gap instead of claiming enforcement, the split-enforcement
paragraph names `make check`/`make build`/`make test`, and §4's rows cite this ADR as the
live record. The invariant-table preamble no longer claims R11 checks that a `verified_by`
script is reachable, because it does not.

Citations to this repo's own decisions must be **bare** (`ADR-0002`). With no
`package.json`, `repoName` returns null and every qualified citation — `dupin:ADR-0002`
included — is skipped as another repo's, so a typo in one fails nothing. This was already
true before, for a different reason (the manifest was named `dupin-docs`), and `CLAUDE.md`
§2 already requires bare self-citations; it is stated here so the next reader does not
discover it by writing an unchecked citation.

`/wrap-up` step 2 (the mutation check) stays inapplicable: it is gated on
`scripts/check-mutants.mjs`, which this repo does not have, and no `make mutants` target is
invented for a harness that does not exist.

The vendored `scripts/build-index.mjs` is edited in two strings (the generated header and
the staleness message) to name `make index`. That is a local divergence from the kit and
`/init-method --update` may overwrite it; the alternative — a generated file instructing the
reader to run a command that cannot run — is the exact failure ADR-0001 was written to fix.
