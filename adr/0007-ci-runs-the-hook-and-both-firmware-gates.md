---
id: '0007'
title: "CI runs the hook's checks and both firmware gates, as separate jobs"
type: architecture
status: accepted
date: 2026-09-11
supersedes: []
superseded_by: []
---

## Context

`LEDGER.md` carried a line from ADR-0001: no CI workflow exists, though
`.claude/hooks/pre-commit` names `.github/workflows/docs.yml` as "the backstop for clones
without the hook installed". The line asked for a workflow running `make check`,
`make build` and `make test`. There was no `.github/` directory at all.

Building it turned up three facts the line did not contain, each measured on this commit.

**`make check` is not what the hook runs.** The hook runs three checkers: `lint-docs.mjs`,
`build-index.mjs --check`, and `check-immutable.mjs`. `make check` bundles the first two.
The third is the one with no working-tree equivalent — it compares two git revisions, so it
cannot be a directory check. The gap is not theoretical. Rewriting a line of ADR-0006's body
and committing it with `git commit --no-verify` — the exact bypass the hook documents, and
the exact case CI is supposed to backstop — measures as:

| Check | Result on that commit |
|---|---|
| `make check` | exit 0 — passes |
| `check-immutable master HEAD` | exit 2 — `IMMUTABLE adr/0006-…: body line 4 was deleted or rewritten` |

A CI built to the ledger line's literal text would have waved that commit through while
reporting green, and the hook's claim about this file would have been false for the one rule
that most needs a backstop: immutability is the invariant a bypass is most tempting for.

**The device toolchain is not pinned, and resolves differently per machine.**
`platformio.ini` declares a bare `platform = espressif32`. Measured by building the same
commit twice:

| Core dir | Resolved platform | Result | `-Wformat-truncation` warnings |
|---|---|---|---|
| this machine's `~/.platformio` | Espressif 32 **55.3.34** (the pioarduino fork, installed from a URL) | SUCCESS, 43 s | **3** |
| an empty `PLATFORMIO_CORE_DIR` | Espressif 32 **7.1.2** (the official registry) | SUCCESS, 94 s cold | **1** |

PlatformIO satisfies an unpinned spec from whatever already sits in the core dir, so the
platform a build uses is a function of that machine's install history rather than of
anything in the repo. Two developers on the same commit compile against different platform
lineages, and CI would be a third.

Both resolve green and both report **zero** `-Werror=format` errors, so ADR-0006's contract
holds across both — that is the reassuring half. The unreassuring half is that the
diagnostics differ: the `LEDGER.md` line recording "three `snprintf` sites" is true on
55.3.34 and overcounts on 7.1.2, where only the `toast[32]` site is reported. A gate whose
findings depend on the builder's install history is weaker than it appears.

**There is no git remote.** `git remote -v` is empty on this repo. A workflow file is inert
until an origin exists and something is pushed to it. This is recorded rather than left
implicit for the same reason ADR-0002 declared R11 inert out loud: a check believed to run
and not running is worse than a known gap.

The other measurements that shaped the shape of the workflows: `make check` takes **63 ms**,
`make test` takes **8 s** for 262 cases in 18 suites, and a cold device toolchain install
costs **2.4 GB** in the core dir, of which 2.0 GB is `packages/`.

## Decision

**1. Two workflows, not one.** `.github/workflows/docs.yml` and
`.github/workflows/firmware.yml`.

The filename `docs.yml` is not a preference. The vendored hook names that exact path, and
`scripts/` and `.claude/hooks/` have no local adaptation path (stele:ADR-0006) — editing the
hook to name something else would be discarded by the next `--update`. The repo adapts to
the name. Keeping the firmware gates out of it means a 63 ms invariant check is never queued
behind a 2.4 GB toolchain download, so the cheapest signal is also the fastest.

**2. `docs.yml` runs all three of the hook's checkers**, which requires a new
`make check-immutable` target taking `IMMUTABLE_BASE`/`IMMUTABLE_HEAD`. `make check` keeps
its current meaning — the two checks that are a pure function of a directory — because
folding a git-revision comparison into it would break that property and fail on a
single-commit clone. A pull request is checked head-against-base; a push is checked
`github.event.before`-against-`github.sha`; a branch's first push reports an all-zero base
and is announced as skipped with `::notice::` rather than passed silently, matching the
hook's own behaviour on an unborn branch.

**3. `firmware.yml` splits the two gates into independent jobs.** ADR-0006 measured that
neither gate catches what the other does. Sequential steps in one job destroy exactly that
information: the first failure aborts before the second gate reports, so a red run says
"something broke" where the split says which side. The host job needs no cross toolchain and
no cache, so it is also the faster signal of the two.

**4. CI invokes `make` targets, never raw `pio` or `node` commands.** ADR-0002 made
`Makefile` the single named entry point; a CI that spelled the same check differently would
be free to drift from what a developer runs, and "green locally, red in CI" would become a
question about the harness instead of the code.

**5. Gated on `master`/`main` pushes and on every pull request, not on all pushes.**
`wip/m5stickc-plus2-port` is knowingly broken and carried as a ledger item; gating it on push
would mean a permanently red badge, which trains readers to ignore the badge. It is still
checked the moment it opens a pull request. Both branch names are listed because this repo's
branch is `master` while its tooling assumes `main`, and guessing wrong means a workflow that
silently never runs.

**6. PlatformIO is pinned (`platformio==6.1.18`); the ESP32 platform is not, and that is
deferred.** Pinning the CI-side tool costs nothing and removes one source of drift. Pinning
`platform = espressif32` is a different decision: it changes what every developer compiles,
the two candidate versions are from different lineages (a third-party fork versus the
registry), and choosing between them needs its own measurement. It gets a `LEDGER.md` line
citing this ADR rather than being decided as a side effect of adding CI.

**7. No part of the serial-driven device harness runs in CI.** ADR-0002 forbids claiming it
does until a runner with hardware exists. `make build` is a compile; invariant 3 already says
a green compile is not evidence of behaviour, and putting it in CI does not upgrade it.

## Consequences

`CLAUDE.md` §3 gains the CI split alongside the local gates, and standing invariant row 7 is
added in this commit: a check that must run in CI is reachable as a `make` target.

The `LEDGER.md` line asking for the workflow is deleted. A new line asks for the platform pin
with the measured divergence, citing this ADR.

The workflows are inert until this repo has a remote. Nothing about them should be described
as "enforced" until a push to an origin has produced a run.

**Citations inside `.github/` are unchecked.** The linter resolves `ADR-NNNN` references in
`LEDGER.md`, in the corpus, and in the prose read as instruction — `CLAUDE.md`, `README.md`,
`docs/`, `.claude/commands/` (stele:ADR-0020). `.github/workflows/` is in none of those sets,
so an ADR reference that rots in a workflow comment fails nothing. The comments there cite
anyway, because an uncited mechanism is worse than an unchecked citation, but a future reader
should not mistake them for verified links.

The `-Wformat-truncation` ledger line's count of three sites is platform-dependent, as
measured above. Whoever fixes those buffers should size them to their real inputs rather than
to whichever subset their own toolchain happens to report.
