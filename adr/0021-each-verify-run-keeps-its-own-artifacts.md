---
id: '0021'
title: "Each verify run keeps its own artifacts"
type: architecture
status: accepted
date: 2026-09-15
supersedes: []
superseded_by: []
---

## Context

ADR-0009 made every verify gate write its serial transcript to `verify-artifacts/`, on failure as
well as on success, because the transcript is the only record of what the device said and it is
most needed when the assertion did not hold. It chose the directory and left the names to each
script, and each script chose one fixed name per file: `boot-serial.log`, `boot-summary.txt`,
`boot-build.log`, and the same three for `input-` and `baseline-`. Fixed names break that promise
in three ways, all observed or read from the code on 2026-09-15.

1. **The next run erases the last one.** A `make verify-input` run on b44b48a failed once in six;
   the five reruns that followed rewrote `input-serial.log`, so the only record of the failure was
   gone before anyone read it. The failure is still unexplained (`LEDGER.md`), and it cannot be
   diagnosed until its next occurrence survives a rerun.
2. **Two boards share one name.** `make verify-boot` and `make verify-boot-adv` run the same script
   against different envs and both write `boot-*`. Running both — the pair ADR-0011 exists to keep
   separate — left only the second board's summary; the first had to be copied aside by hand.
3. **A failed run can sit beside a passing summary.** On failure a gate writes its transcript and
   build log but no summary, so the previous run's `result: PASS` stays in the directory next to
   the failing transcript. The converse holds too: a pass never removes a failure's
   `*-build.log`. The directory can therefore read as a pass about a run that failed, which is
   worse than losing the log, because it looks like evidence.

The files are small. The largest artifact measured is 14 016 bytes (`baseline-serial.log`) and a
full run writes at most three files, about 40 KB.

The writing itself is copied: `boot-verify.mjs`, `input-verify.mjs` and `baseline-verify.mjs` each
carry the same try/catch block that picks names and writes them, and `baseline-probe.mjs` a fourth
variant.

## Decision

1. **A run writes into a directory of its own, created once, when its outcome is known:**
   `verify-artifacts/<gate>-<env>/<UTC timestamp>-<OUTCOME>/`, for example
   `verify-artifacts/boot-verify-cardputer_adv/2026-09-15T10-22-03.418Z-FAIL/`. `<gate>` is the
   script's own name (`boot-verify`, `baseline-probe`), so a probe's runs are never filed as a
   gate's, and the timestamp is when the run started. Every gate already writes
   only at the end of a run, so the outcome is known when the name is chosen, and no run ever
   writes into a directory another run made. That removes all three failures at once: nothing is
   overwritten, the env is part of the name, and a directory holds one run's files only.
2. **The outcome is one of three words.** `PASS`: the assertions held. `FAIL`: a `VerifyFailure` —
   the device did not behave as claimed. `ERROR`: anything else thrown — the gate itself broke. A
   harness bug and a device defect need different people looking at them, so they must not share a
   name.
3. **Every run writes `summary.txt`,** a failed one included, carrying `result:` and, when it did not
   pass, the reason. A directory with no summary would send a reader back to the transcript to
   guess which of the three it was.
4. **The directory is created exclusively.** The leaf is made without `recursive`, so if it already
   exists the write fails loudly rather than merging into it. The timestamp carries milliseconds and
   a run takes seconds, so a collision means something is wrong, not unlucky.
5. **One helper in `scripts/verify-harness.mjs` does this,** and all four scripts call it. The gate
   passes its name, env, outcome and the lines of each file; the helper returns the directory, which
   the gate prints. Four copies of the naming rule is how the three defects above got written once
   each.
6. **Nothing is pruned.** At about 40 KB a run, a thousand runs are about 40 MB in a git-ignored
   directory. Retention logic would be code that a measured problem has not asked for, and a pruning
   bug is one more way to lose exactly the failure this decision exists to keep. The operator
   deletes old runs by hand.
7. **The run is reported before it is written, and its names are checked before it starts.** The
   outcome and reason reach the terminal first, so a write that fails — a full disk, a
   `verify-artifacts` that is a file — stops the run with its own error without hiding what the
   run found. The gate and env names must be plain (`[A-Za-z0-9_-]`), because the env arrives
   from `DUPIN_ENV` and a pasted path would otherwise put a run's evidence outside the artifact
   tree without a word; a name that is not plain is refused before the body runs.
8. **The helper is tested on the host with `node --test`,** which ships with Node and needs no
   manifest, so ADR-0002's "no npm manifest" still holds. A `make test-scripts` target runs it and
   joins `make check`, which is already "everything enforceable without hardware" (ADR-0012), so CI
   runs it through the target it already calls (ADR-0007).

### Rejected

- **Keep the fixed names and move the previous run into `history/` at start.** It adds a move that a
  killed run can leave half done, it still needs the env added to fix failure 2, and it fixes failure
  3 only if the move is complete. It is not simpler than a new directory.
- **A new directory per run, plus automatic pruning** of all but the last N passes. See decision 6:
  it costs code and risk to save megabytes nobody has run out of.
- **A `latest` symlink per gate.** The timestamped names already sort, so `ls | tail -1` finds it,
  and a symlink is one more thing to leave dangling when a run is killed.

## Consequences

- The `LEDGER.md` line about fixed artifact names is closed by deleting it. The failure item above it
  stays open: this decision is what lets its next occurrence be kept, not an explanation of it.
- `CLAUDE.md` §3 states where a gate's artifacts land, and invariant 23 makes `recordRun` the only
  way to write them, so the next gate follows the layout rather than inventing a fifth naming
  scheme.
- A run interrupted by a signal still writes nothing, exactly as before: the catch block that writes
  artifacts never runs. That is unchanged, and noted so it is not mistaken for coverage.
- `verify-artifacts/` grows without bound. That is chosen, in decision 6, not overlooked.
- Existing fixed-name files in `verify-artifacts/` are left where they are; they are git-ignored and
  the new layout never reads them.
