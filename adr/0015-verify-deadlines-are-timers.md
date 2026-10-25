---
id: '0015'
title: "Verify deadlines are timers, and a gate reports device time"
type: architecture
status: accepted
date: 2026-09-12
supersedes: []
superseded_by: []
---

## Context

`captureUntil()` in `scripts/verify-harness.mjs` is the core of `make verify-boot` and
`make verify-boot-adv`. It waits for a boot marker and then requires a settle window in
which no panic and no reset banner appears — the assertion ADR-0010 added after a marker-only
check passed on firmware that crashed immediately afterwards.

Its settle deadline was checked in the body of a `for await` loop over the tap's lines:

```js
if (sawMarker && Date.now() >= settleDeadline) break;
```

So the deadline advanced only when a line arrived. **The device this harness exists to verify
is a passive monitor that, once booted, says nothing.** Dupin transmits nothing and logs
nothing while idling in the case menu; silence is its healthy steady state. The one event the
deadline was waiting on is the one a correct device never produces.

Measured on `e96d44b`, `cardputer` env, with the board on `/dev/ttyACM0`:

```
reset-to-marker+settle: 33258 ms (incl. 8000 ms settle)
serial lines:  37
total:         51489 ms
```

The tap is spawned with `--seconds ceil((25000 + 8000) / 1000)` = 33. The reported figure is
the tap's own lifetime to within process teardown, and it is reported under a label naming a
device measurement. The transcript says what really happened: the last framework-stamped line
is at 1243 ms, the last device line is `[MENU] Found 2 sessions`, and the remaining ~25 s are
empty. The gate's *verdict* was sound — nothing panicked in the window — but every number it
printed described the harness, not the board.

The function's own docstring claimed the opposite: *"Resolves as soon as the marker is seen —
the tap is killed rather than left to run out its clock, so a passing gate costs only as long
as the boot it is proving."* That is the property the loop condition destroyed, written
directly above it. A comment is not a check, and this one had been read as one.

**Two further defects sat in the same arithmetic.** The tap's lifetime equals the harness's
entire budget, so firmware that reached the marker near `BOOT_TIMEOUT_MS` would have the
reader die part-way through its own settle window and fail a boot that was merely slow. And
`captureUntil` could not tell "the deadline passed" from "the tap ended", because both left
the loop the same way — a distinction that only starts to matter once the deadline is real.

**The newer of the two readers was already right.** `driveDevice()` (ADR-0014) races each
read against a timer, because a step that expects a reply has to give up on not receiving
one. The two functions disagreed about how a deadline works, and the older one was wrong; a
third script copying the nearest example had even odds of inheriting the defect.

## Decision

**1. A deadline in this harness is a timer, never a line arrival.** Waiting for the device to
speak before noticing that time has passed is only correct for a device that is guaranteed to
keep speaking, and no device here is.

**2. One bounded line reader, shared by both entry points.** `openLineReader(stdout,
transcript)` owns the race, the transcript append, and the first-failure record;
`captureUntil` and `driveDevice` both drive it. This is the second instance rather than the
third, so it is not a reuse-on-speculation: the extraction exists because one copy had a bug
the other did not, and two copies of a deadline rule have no sync path.

**3. A read outstanding at a deadline is carried to the next call, not abandoned.** Racing a
promise and walking away leaves the loser pending; the line that eventually resolves it would
be consumed with no transcript entry. Every deadline in the harness is currently terminal, so
this is latent rather than live — which is exactly when it is cheap to remove.

**4. The tap outlives the harness's budget by `TAP_GRACE_MS`.** The reader's clock is the
backstop, not the assertion, so it must never be the thing that ends a run.

**5. A gate reports what the device did.** `captureUntil` returns the measured reset-to-marker
time, taken from the tap's `<tap-ready>` line — printed immediately after the reset pulse, so
the span is the board's, not the process's. `boot-verify.mjs` prints that and the settle
window as separate, separately-named figures.

**6. "The tap ended" and "the deadline passed" are distinct outcomes** and get distinct
failure messages. A reader that died is a broken harness; a device that went quiet is, here,
usually correct.

## Consequences

**The gate got faster and the number got true.** Same board, same env, immediately after:

```
reset-to-marker:  3741 ms
settle:           8000 ms with no panic and no reset
serial lines:     37
total:            34645 ms
```

The verdict is the same and the transcript is the same 37 lines. What changed is that the
elapsed figure is now the board's: 3741 ms against the 33258 ms the same gate reported before,
and the run costs 34645 ms instead of 51489. The ~19 s saved is exactly the silence the old
loop sat through waiting for a line that a correct device was never going to send.
`make verify-boot-adv` reports 3741 ms on the same board, against the 33264 ms ADR-0011 pastes
for that env — the two envs boot identically, as they always did.

`make verify-input` is unchanged at 7/7 steps over the same 73 lines, and its own figure moved
from 4195 ms to 3963 ms because it was timed from process spawn under a `reset-to-last-step`
label. That is the same mistake at a smaller scale, and it is fixed the same way. The two
gates now sit on one clock: the marker at 3741 ms, seven driven steps satisfied 222 ms later.

**Proven in both directions.** Three failure paths, each forced and observed, with `make`
exiting 1 on all three:

```
marker never arrives          READY_MARKER set to a line the firmware never prints
  -> FAIL — marker "[BOOT] This line is never printed" did not appear within
     25000 ms (37 line(s) captured)

reader dies mid-settle        TAP_GRACE_MS temporarily -25_000
  -> FAIL — the serial tap closed before the 8000 ms settle window ended — the
     reader stopped, which is a broken harness and not a quiet device

boot loop                     delay(2000); ESP.restart(); after the boot marker
  -> FAIL — reset after boot on serial: ESP-ROM:esp32s3-20210327
```

The first case now fails at 25000 ms — the timeout the script declares — rather than whenever
the tap happened to expire. The second exercises a branch that did not previously exist. The
third is ADR-0010's assertion, re-proved rather than assumed because the reset check moved
during the extraction.

**What this does not change.** The verdict logic is untouched: the same failure patterns, the
same reset-after-boot rule, the same settle requirement ADR-0010 argued for. This ADR is about
when the harness looks at its clock and what it prints, not about what makes a boot acceptable.
`make verify-boot` stays a local, hardware-attached gate outside `make check` (ADR-0009).

**The inflated figure stays on the record where it was published.** ADR-0011 pastes a
`33264 ms` run as proof that removing `-DBOARD_HAS_PSRAM` stopped the panic. That transcript is
a true record of what the tool printed and is not edited; a correction marker under it names
the label as wrong and points here. The same marker is appended to the ADR-0009 amendment,
which asserted that ADR-0009 "reports" a timing — it reports none, and ADR-0011 is the only
place the figure was ever published.
