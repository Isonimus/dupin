# Dupin — Project Conventions

Defensive Wi-Fi Intelligence Toolkit — a passive Wi-Fi security tool that observes,
baselines, and detects anomalies in wireless environments without attacking,
impersonating, or interfering with networks. Monitor mode only; it transmits nothing.
Analysis is layered across named detective modes (Watson, Marple, Poirot, Holmes,
Baskerville, Dupin), from raw logging through correlation to AP localization.

Stack: C++17 (`-std=gnu++17`) on the Arduino framework for ESP32-S3, built with PlatformIO.
Target hardware is the M5Stack Cardputer and Cardputer ADV via M5Unified/M5Cardputer.
`pio run -e cardputer` builds the default firmware; `pio test -e native` runs the unit
tests on the host, where `NATIVE_BUILD` excludes `main.cpp` and the capture layer.

The architecture and the detective-mode specifications live in
[`docs/architecture.md`](docs/architecture.md), a live doc cited here so it is not orphaned
(stele:ADR-0010); the durable decisions behind the code are the ADRs under [`adr/`](adr/).

General working practices — quality bar, testing standard, commit hygiene, delegation,
correction, language — live in [`docs/quality-bar.md`](docs/quality-bar.md) and apply here
without being restated. It ships with the method and is vendored into this repo, so it is
yours to adapt; an update keeps what you changed (stele:ADR-0024).

This file carries only what is specific to **this** repo. Restating a rule from the bar here
would create a second copy with no sync path, which is the failure stele:ADR-0005 exists to
prevent.

## 1. Document taxonomy — four kinds

Every document is exactly one of four things.

| Kind | Files | Rule |
|---|---|---|
| **Immutable** | `adr/*.md`, `slices/*.md` | Written once. Body prose is never edited. Only status/supersession fields may change. |
| **Generated** | `adr/INDEX.md` | Built by script from frontmatter. Never hand-edited. |
| **Ledger** | `LEDGER.md` | Exactly one per repo. The only hand-maintained tracker. |
| **Live doc** | `README.md`, `docs/*.md` | Describes how something behaves *now*. Updated **in the same change** as the thing it describes, and cited from this file so it is never orphaned (stele:ADR-0010). |

- **ADR** — a decision later work must obey (a mechanism, data format, or boundary).
  Asserts *"on date X we chose Y because Z"*: a historical claim, true forever.
- **Slice** — one feature work-unit. Written before implementation, **frozen at merge**
  and rewritten to past tense: *"this is what shipped."* Freezing converts it from a
  current-state claim (always going stale) into a historical one (never stale).
- **README.md** — live document; update it in the same change that alters any
  user/dev-facing feature or API.

**Single writer, one direction.** An ADR records a deferral *once*, as a fact about that
decision. `LEDGER.md` cites the ADR. **Never reach back into an ADR to close a ledger
item.** Closing an item means deleting its line from the ledger.

Changing our minds means writing a **new** ADR that supersedes the old one, never editing
it. The superseding note must say *why the old reasoning was wrong* — that record is the
most valuable thing this workflow produces, and an in-place edit destroys it.

A committed document's body may **gain** lines — an appended `## Amendment — <date>: …`, or a
correction marker placed at the claim it corrects — and may never lose or rewrite one. The
hook enforces this (stele:ADR-0019); frontmatter is exempt, because status and supersession
fields are how a record announces it was superseded.

This is also how a justified rule-violation gets recorded. `~/.claude/CLAUDE.md` §2 says a
justified violation is written down as a decision rather than taken as a silent exception;
in this repo, that decision is a new or superseding ADR.

## 2. Enforcement — invariants are executable

`node scripts/lint-docs.mjs` runs from a pre-commit hook and in CI. `make check` bundles it
with the index comparison and with `make check-version`, a grep asserting the version is
declared once and read rather than restated (ADR-0012), with `make check-formats`, a grep
refusing any `l` or `ll` length modifier in a format string — the one form the two compilers
cannot check jointly in code only one of them is given (ADR-0028) — and with
`make test-scripts`, the host tests for where a verify run's artifacts land (ADR-0021) — so
`check` is now "everything
enforceable without hardware", not "the document rules". The hook checks the
**commit**, not the working tree, so a fix you forgot to stage cannot green a red commit
(stele:ADR-0018); alongside the linter it verifies that `adr/INDEX.md` matches the corpus and
that immutable bodies have only gained lines (stele:ADR-0019).

A citation is bare (`ADR-NNNN`) only when it means *this* repo, and qualified
(`<repo>:ADR-NNNN`) otherwise (stele:ADR-0009). The linter resolves citations in `LEDGER.md`,
in the corpus, and in the prose that is read as instruction — this file, `README.md`,
`docs/`, `.claude/commands/` — so a citation that rots there fails the build rather than
quietly misrouting the next session (stele:ADR-0020).

A rule enforced by memory is a rule that holds until the first busy afternoon. If a
convention matters, it gets a rule; if it genuinely can't be checked, say so out loud
rather than writing it down and trusting it.

Run `/wrap-up` before finishing a task.

## 3. Verification harness — measure, don't assume

Some changes cannot be asserted on the host: rendering, radio capture, SD/SPIFFS mounting,
input handling — anything whose correctness is "does it behave right on the device". The
answer is **not** to skip verification and eyeball it once by hand.

**A green build is not verification.** `pio run -e cardputer` reported SUCCESS on a tree
where every board guard was misspelled `DDUPIN_*`, so the preprocessor deleted the new code
and the Cardputer booted with no working input at all (see `LEDGER.md`). The compiler
confirms the code parses, nothing more.

**Host-testable logic is verified by `pio test -e native`, and that is the default.** The
native env excludes `main.cpp` and `capture/wifi_capture.cpp`, so parsing, baselining,
anomaly scoring, and storage bookkeeping are all assertable on the host; `test/` already
holds the suites. A slice whose behaviour a host test can assert ships Unity tests under
`test/`, not a device script. Reaching for a device script when a host test would do is how
a pile of unrunnable scripts accumulates for logic that never needed hardware.

**Any slice whose behaviour a host test cannot assert ships a verification script.**
The harness those scripts are built on exists: `scripts/verify-harness.mjs` holds the shared
core and `scripts/serial-tap.py` reads the port, split that way because hand-rolled Node tty
reading measured lossy and a dropped `Guru Meditation` is a false green (ADR-0009). Every
deadline in that core is a timer, and every elapsed figure a gate prints is measured from the
device's reset rather than from the harness starting (ADR-0015): a deadline re-checked only
when a line arrives never fires on a device whose correct steady state is silence, which is
every device here. The first script is `scripts/boot-verify.mjs`, reachable as
`make verify-boot`: it resets the board, waits for `[BOOT] Initialization complete`, and then
requires a settle window with no panic and no reset banner. The settle window is the
assertion — a boot loop reboots every couple of seconds, and the marker alone would have
passed on firmware that panicked right after it
(ADR-0010). It caught a boot loop on its first run and is green as of that fix. `make
verify-boot-adv` runs the same gate against the `cardputer_adv` env, which needs its own
target because one env can be broken in `platformio.ini` while the other is green — that is
exactly the defect it caught (ADR-0011).

`make verify-input` is the second script, and it asserts the thing a green build has never
shown: that pressing a key does something. It drives the serial control channel the firmware
now reads (ADR-0014) — `ping`, `state`, `probe`, and `key <arg>`, which runs the same
dispatch a keypress runs — and asserts an injected key reaches both the main chain and the
case-menu chain and changes observable state. `probe` is the newest word: it reports which
task ran what, what a pass and a render and a save cost, and how many frames the queue took,
handed on and had to drop. It is read-only like `state`, it started as the measurement the
choice between a lock and a queue needed (ADR-0024), and it stayed because the figures that
found that defect are the figures that catch it coming back — `make verify-race` asserts on
them now. It cannot prove the physical keys work, because an
injected key bypasses the keyboard driver by construction; say "the dispatch runs", never
"input works". Adding the reader also made the firmware reachable at all: `setup()` sits in a
modal loop on the case menu whenever an SD card is present, so before this `loop()` never ran
until a human pressed a key.

`make verify-baseline` is the third, and it asserts the one thing the case abstraction rests
on: that a baseline written under a case comes back under *that* case. It drives the board
twice, because the reset is the assertion — a round trip checked inside one boot proves only
that RAM kept what RAM was given (ADR-0016). It exists because opening a case used to restore
nothing at all, so every case shared one global store and "new SSID" was computed against every
environment the device had ever been switched on in. Only the SD read and write are
device-only: formatting and parsing a baseline document are portable and are asserted by
`make test` as a round trip, which is where a cutout that swallows portable logic gets caught
rather than hidden (ADR-0003 decision 4). What that document has to carry is its own decision
(ADR-0017): a field is persisted when a restored entry needs it to behave like a live one,
because a detector that gives up on a missing field is a detector silently switched off — and
the count-based gate passed for as long as two of Marple's did exactly that.
The gate works only in cases it creates — `verify_gate_a` and `verify_gate_b` — and deletes
them before it exits. It once saved into the card's first case, so every passing run shrank
what the next could discover, and a round trip into a case already holding the saved figure
passes on a save that wrote nothing (ADR-0020).
It captures **twice** before the reset, and the figure it round-trips is the second window's:
stopping capture used to unmount the card and clear the open case, so everything a later window
learned was dropped with no line anywhere saying so — and this gate stayed green throughout,
because one capture window never reaches the state a second one starts in (ADR-0025). Stopping
is about the radio now; mounting and case ownership belong to `setup()` and the case menu, and
a write that could not happen is said out loud rather than skipped. Every save it makes also
runs the check that the file on the card is as long as the writer counted, so the gate goes red
if that arithmetic ever disagrees with the format — the failure it guards against is one no gate
can stage, since no serial command can make a card refuse a write, so the writer takes a sink
and `make test` is the card that says no (ADR-0026).
During its capture it also presses `c` and asserts the case menu is refused, with capture, case
and baseline left as they were: opening a case clears the baseline the Wi-Fi task is still
inserting into, and the menu's modal loop stalls channel hopping for as long as it is open
(ADR-0022).
`make verify-input` also asserts that a refused case name reaches the operator: the case
menu is a modal loop that repaints itself every 50 ms, so a toast or a status line raised from
it is erased within a frame, and a refusal has to be drawn — and reported by `state` — on the
menu's own surface (ADR-0019). It also asserts that deleting a case waits for a second key: `x`
puts the question in `notice=`, and any other key withdraws it without moving `sel=`
(ADR-0020).

`make verify-race` is the fourth, and it asserts that frames survive the crossing between
tasks. ADR-0024 moved ingestion off the Wi-Fi task and onto a queue the loop task drains, so
what has to hold is conservation: every frame `onFrame` accepted is drained into the modes or
counted as dropped, with nothing left queued once capture has stopped. That identity is the
assertion rather than a check that the drop count is 0, which passes just as well on firmware
that captured nothing (ADR-0016, invariant 17) — so the gate also requires that frames were
accepted, that frames were drained, and that the baseline came out non-empty, which is the
same fact read from Watson rather than from the queue's own bookkeeping. It asserts that the
drop count `state` shows the operator is the one `probe` reports, and that
`rendersOffLoopTask` and `renderCollisions` are 0: those two found the original defect and
must read 0 for good. What a passing run does **not** prove is the drop path itself — whether
a queue this deep ever fills is a property of the air, and a command to fill it would be a
capability the keyboard does not have (invariant 14). It began as `scripts/race-probe.mjs`,
whose measured figures ADR-0024 was decided on, and it still records them: `maxFramesPerPass`
is what will say when the chosen depth has stopped being enough.

`scripts/<slice>-verify.mjs` — drives the real device over the serial port, exercises the
specific behaviour the slice claims, and:

- **fails on any of** a non-zero exit from the `pio` subprocess; a serial line matching
  `/^\[(ERROR|FATAL)\]/`, this codebase's log convention; a serial line containing
  `Guru Meditation`, `abort() was called`, or `Backtrace:`; or the slice's own assertion not
  appearing within its timeout — this half is pass/fail and machine-checkable;
- **writes artifacts** (the captured serial transcript, measured numbers) for human review —
  this half needs eyes, and that is fine, as long as the first half still runs unattended.
  It writes them through `recordRun`, which gives every run a directory of its own,
  `verify-artifacts/<script>-<env>/<UTC start>-<PASS|FAIL|ERROR>/`: fixed file names let the
  reruns after a failure erase its only transcript, and left a stale `result: PASS` beside a
  failing one (ADR-0021);
- **is named in the slice's `## Verification` section**, so the claim and its evidence are
  linked;
- **is named as a `make` target**, because a checker with no entry point runs once.

**R11 is inert in this repo, and that is a declared gap, not an oversight** (ADR-0002). The
rule checks that every `scripts/*-verify.mjs` is reachable from `package.json`; Dupin is a
C++/PlatformIO repo with no npm manifest, so the rule returns early and enforces nothing.
`Makefile` is the entry point the kit's commands call instead, but nothing checks that a
verify script is wired into it — that debt is carried by a `LEDGER.md` line, and the reason
it is written here rather than left implicit is that an unenforced rule mistaken for an
enforced one is worse than a known gap. Probes are exempt either way: a probe answers its
question once and the number lands in an ADR.

**Enforcement is split, because a device gate cannot run unattended.** The serial-driven
half needs hardware on a USB port, so it is a **local** gate named in the slice — never
claim it "runs in CI" until a hardware runner exists (ADR-0002). What runs without hardware
is `make check` (the doc invariants plus `check-version`, ADR-0012, `check-formats`, ADR-0028,
and `test-scripts`, ADR-0021), `make check-immutable`
(immutable bodies, which needs a revision range and so is not part of `make check`),
`make build` (`pio run -e cardputer`), and `make test` (`pio test -e native`).
`make verify-boot`, `make verify-boot-adv`, `make verify-input`, `make verify-baseline` and
`make verify-race` are the local, hardware-attached gates and are deliberately not part of
`make check` (ADR-0009).

Which compiler `make build` and `make test` use is not left to the machine. `platformio.ini`
pins the device platform to an exact pioarduino release (ADR-0008), because a bare `platform =
espressif32` resolved to whatever sat in the builder's core dir — and the alternative lineage
still ships Arduino 2.0.17, on which `uint32_t` is `unsigned int` exactly as on the host, so
the device half of invariant 6 would enforce nothing while still claiming to. The host env is
deliberately *not* pinned and that is a declared gap, not an oversight: it compiles with
whatever `g++` is on `PATH`. The device toolchain is not a separate choice: each
pioarduino release names an exact `xtensa-esp-elf` build, so pinning the platform pins the
compiler too — and because PlatformIO unpacks it into one shared unversioned directory,
moving the pin re-downloads a compiler and overwrites the previous one, which is why two
pins cannot be compared without rebuilding each (ADR-0018).

Those same targets run in CI, as two workflows (ADR-0007). `.github/workflows/docs.yml`
carries the name the vendored pre-commit hook already claims as its backstop and runs all
**three** of the hook's checkers — `make check` alone is not the hook, because it omits the
immutability comparison a `--no-verify` bypass most needs caught.
`.github/workflows/firmware.yml` runs the host suite and the device build as **separate
jobs**, because ADR-0006 measured that neither catches what the other does and a sequential
failure would hide which side broke. CI invokes `make` targets and never raw commands, so it
cannot drift from what a developer runs. Both workflows are inert until this repo has a
remote — say so rather than calling them enforced.

Every slice carries two required sections, both rule-checked: `## Verification` names the
proof (R12), and `## Definition of Done` states the acceptance criteria as Given/When/Then
scenarios written before the code (R13, stele:ADR-0011). Each scenario names its proof in
`## Verification`; the linter checks the sections exist and that the Definition of Done
holds a full triad — it cannot check that a scenario is *right*, which is what `/wrap-up`
is for.

Probes are the same tool used before the fact: when a design question has a measurable
answer (how much DRAM a full-display canvas costs, what a capture cycle takes), write
`scripts/<topic>-probe.mjs` and put the **measured numbers** in the ADR. Design decisions
cite data, not estimates.


## 4. Standing invariants

Repo-specific definition-of-done rules. A slice is not complete until it satisfies every
one that applies. Each cites the ADR that created it; exceptions are listed with the
reason, so nobody "fixes" a deliberate choice.

These live **here, in the repo** — not in assistant memory. A rule that governs the
codebase must be greppable, diffable, reviewable, and survive a change of machine
(stele:ADR-0005).

Each row declares how it is **enforced**, so an aspiration is never mistaken for a
guarantee:

- `verified_by: <script>` — a wired verify script or lint rule checks it;
- `pending (LEDGER)` — enforceable but not yet enforced; a ledger item carries the debt;
- `review-only` — enforceable only by human judgement, so `/wrap-up` is the enforcement.

The declaration is a convention checked at review, not by the linter — the linter does not
read this file (stele:ADR-0004). Nor does anything check that a `verified_by` script is
reachable from an entry point: R11 would, but it reads `package.json` and is inert here
(ADR-0002). So a row claiming a check that runs nowhere is caught at review or not at all,
which is why `pending (LEDGER)` exists and why rows use it rather than overclaiming.

| # | Invariant | Source | Enforced by |
|---|---|---|---|
| 1 | Logic assertable on the host ships Unity tests under `test/`; a device verify script is only for behaviour a host test cannot reach | ADR-0002 | review-only |
| 2 | Behaviour a host test cannot assert ships `scripts/<slice>-verify.mjs`, reachable from a `make` target | ADR-0002 | pending (LEDGER) — R11 reads `package.json` and is inert here |
| 3 | A green `pio run` is never accepted as evidence of behaviour — the compile is necessary, not sufficient | ADR-0002 | review-only |
| 4 | Board feature guards are spelled `DUPIN_*`, never `DDUPIN_*`; the `-D` in `platformio.ini` is the compiler flag prefix | ADR-0002 | partly `verified_by:` `make verify-input`, which catches the *consequence* a misspelling has (a dispatch that does not run); the spelling itself is unchecked, and on master no `#ifdef` guards the input path anyway (ADR-0011), so that half stays pending (LEDGER) |
| 5 | New display or rendering code renders through `dupin::Canvas` (one typedef, host-stubbed), never `M5Cardputer`/`M5Canvas` directly | ADR-0005 | review-only |
| 6 | A `uint32_t` reaches a printf-family call as `%u` with an explicit `static_cast<unsigned>` (or as an `unsigned` variable) — never bare `%lu`/`%u`, whose correctness differs between host and device. No format string carries an `l` or `ll` length modifier at all: `long` is the width at which a fixed-width typedef names a different type on each platform, so a 64-bit value goes through `PRIu64`/`PRId64` instead | ADR-0006, ADR-0028 | `verified_by:` `make check-formats` for the modifier, over every source file whether or not the host compiles it, one line at a time — a specifier split across two literals passes it; `make test` **and** `make build` jointly for pairing a specifier with its argument — neither catches both directions, the `make build` half only enforces on the pinned platform (ADR-0008), and both are blind to code the host never compiles, which is how four sites passed them (ADR-0027, ADR-0028). The grep cannot see an argument's type, so a bare `%u` given a `uint32_t` still rests on `make build` |
| 7 | A check that must run in CI is reachable as a `make` target, and the workflow calls that target rather than the underlying command | ADR-0007 | review-only |
| 8 | The device platform is pinned to an exact release in `platformio.ini`; changing that pin means re-measuring that the ADR-0006 format gate still fires, **before** the change lands | ADR-0008 | review-only |
| 9 | A pointer captured at static-initialisation time is taken from a real member object (`M5.Display`), never from a reference member (`M5Cardputer.Display`), whose binding waits on its owner's constructor | ADR-0010 | review-only |
| 10 | A board `-D` macro in `platformio.ini` names only hardware this firmware has a driver for; `-DBOARD_HAS_PSRAM` in particular is absent from every env, because the ESP32-S3FN8 has no PSRAM die and declaring one panics the framework before `app_main` | ADR-0011 | `verified_by:` `make verify-boot-adv` for the PSRAM claim; review-only for the general rule |
| 11 | The version appears once, as `DUPIN_VERSION_STRING` in `include/dupin.h`; `src/` reads it and never restates it | ADR-0012 | `verified_by:` `make check-version`, part of `make check` |
| 12 | Text bound for a fixed-width renderer is fitted at that renderer's boundary with `dupin::fitText`, which marks what it shortened; the ceiling it fits to is derived from the renderer's own geometry constants, never written as a literal at the call site | ADR-0013 | `verified_by:` `make test` — `fitText` and the toast ceiling are asserted against the box arithmetic `main.cpp` renders with |
| 13 | A bound that both a validator and a display buffer depend on is stated once as a named constant and both derive from it — `dupin::kMaxSessionNameChars` for session names; a display buffer is sized from the format (`sizeof("prefix ") + <bound>`), not by hand arithmetic | ADR-0013 | `verified_by:` `make test` pins the constant to `isValidName`; `make build` sizes the buffers, with `-Werror=format` now covering truncation |
| 14 | The serial control channel exposes no action the operator could not already perform at the keyboard — `ping`/`state`/`probe` are read-only, and `key` runs the same dispatch a keypress runs, through the one shared seam rather than a parallel input path | ADR-0014 | `verified_by:` `make verify-input` proves an injected key reaches both real dispatches; that the vocabulary grants no new capability is review-only |
| 15 | A verify script bounds a wait with a timer, never with the arrival of a line, and reports elapsed time measured from the device's reset — a gate may not print its own lifetime under a device-sounding label | ADR-0015 | review-only |
| 16 | A baseline belongs to exactly one case: opening a case replaces the in-memory baseline with that case's stored one, and no baseline outlives a case. One store holds it, `<case>/baseline.json`; a second mirror of the same fact is a defect, not a fallback | ADR-0016 | `verified_by:` `make verify-baseline` for the round trip and the case boundary; `make test` for the document parsing |
| 17 | A gate compares figures that can disagree. Asserting one reading of a value against another reading of the same value proves nothing — the count a load reports comes from what was loaded, never from the running total it was added to | ADR-0016 | review-only |
| 18 | A field is persisted when a restored entry needs it to behave like a live one — the test is a named consumer, not completeness. A detector that gives up on a missing field is a detector silently switched off, so restoring an entry without it restores an inert one | ADR-0017 | `verified_by:` `make test` asserts the round trip through each detector's own predicate, not through the field values |
| 19 | Text that came off the air is escaped before it is written into a structured document, and unescaped on the way back. An SSID is attacker-chosen input, so an unescaped one can forge the very key the parser scans for | ADR-0017 | `verified_by:` `make test` — a crafted SSID must not produce a second entry |
| 20 | A size ceiling is derived from the format it bounds, never written as a number, and a document that exceeds it fails loudly rather than reading as absent | ADR-0017 | `verified_by:` `make test` formats the widest entry the structure can hold and asserts it fits |
| 21 | A storage or boundary call that can fail in more than one way returns *which* one, never a bare `bool` — a caller cannot report a reason the API threw away, and an operator-facing refusal names the rule it broke on the surface that is actually on screen | ADR-0019 | review-only for the return-type rule; `verified_by:` `make verify-input` that a case-name refusal reaches the case menu, and `make test` that each refusal has its own message that fits the panel |
| 22 | A verify gate writes only into cases it created, and deletes them before it exits. A round trip starts from an empty case, because one into a case already holding the saved figure passes on a save that wrote nothing | ADR-0020 | `verified_by:` `make verify-baseline` asserts each case it creates opens empty and is gone at the end; that no gate writes anywhere else is review-only |
| 23 | A verify script or probe writes its artifacts through `recordRun`, never by naming files itself: each run gets its own directory, named by script, env, start time and outcome, and a failed run records FAIL or ERROR and why in its own `summary.txt` | ADR-0021 | `verified_by:` `make test-scripts` for the layout, the outcome and the exclusive directory; that every script calls `recordRun` is review-only |
| 24 | Nothing changes the open case while capture runs. The case menu is refused at its door while `g_running` holds, so every action inside it — open, create, delete, rename — inherits the refusal, and a new way to reach a case goes through the same check rather than a guard of its own | ADR-0022 | `verified_by:` `make verify-baseline` asserts `c` is refused mid-capture and leaves capture, case and baseline alone; that no other path reaches a case is review-only |
| 25 | A list bound is named once, with its cost stated where the name is; the rows a panel draws are derived from that panel's geometry rather than written as a number; and a listing that could not show everything says so, on every surface the operator has. What is drawn is a window that contains the selection — a row the renderer never reaches is a case the operator opens, renames or deletes blind | ADR-0023 | `verified_by:` `make test` asserts the row count against the footer arithmetic and that the window always holds the selection; that the renderer draws the window it is handed, and that a truncated listing reaches the panel and the log, is review-only |
| 26 | A frame crosses tasks through a queue, never by the producer calling into shared state: the promiscuous callback parses and posts, and the modes, the callbacks they fire, the renderer and the SD writer all run on the loop task. What will not fit is dropped, counted and said out loud — a silenced drop reads exactly like an environment that went quiet. Every frame accepted is drained or counted as dropped, and that is what a gate asserts, because a counter that stayed 0 passes just as well on firmware that captured nothing | ADR-0024 | `verified_by:` `make verify-race` for the conservation of frames and for the drop report reaching `state`; that no later callback reaches mode state from another task is review-only |
| 27 | Stopping capture is about the radio: the card stays mounted and the case stays open, so a later window in the same boot writes where the first one did. Mounting and case ownership belong to `setup()` and the case menu, and nothing else grows a lifetime call of its own. A write whose call reported failure is announced on the wire and on the panel — a capture that saved nothing must not read like a room that was quiet — and the announcement is exactly as strong as the return value under it, which ADR-0026 made see a short write and a document that never reached the card. `[ERROR]` marks a failure with no retry behind it and `[SD]` one there is, because `[ERROR]` fails a verify run | ADR-0025 | `verified_by:` `make verify-baseline` for the case surviving a stop and for the second window's baseline round-tripping the reset; the failed-write announcement is review-only, because making a card refuse a write on demand would need a capability invariant 14 keeps off the serial channel |
| 28 | A write is checked against what it was asked to write, and a document is checked for its own ending. Every write's return is compared against its length; the closed file's size on the card is compared against the bytes written, because `fclose`'s result is discarded and a buffered failure lands after the last write has already returned in full; and a reader that finds a document without the ending its writer emits says so rather than handing back the entries it managed to get. The writer's ending and the reader's expectation are one constant, and both halves report which of three outcomes happened rather than a count or a bool | ADR-0026 | `verified_by:` `make test` for the short-write path, for the reader's three outcomes, and for the two halves read against each other; `make verify-baseline` runs the size comparison on every save it makes, so arithmetic that disagrees with the writer turns every save red. That a card ever goes short is review-only — no gate can make one refuse a write (invariant 14), which is why the writer takes a sink the host suite can be |
| 29 | One document, one formatter, outside the platform cutout. Every writer of a persisted document calls the same function, and that function is compiled by both envs so `-Werror=format` checks its format string — two literals for one document drifted until the saving one dropped a field the creating one wrote, and the host compiled neither. A key whose value cannot mean what the key says is deleted rather than renamed: `created` and `lastSaved` held seconds since boot on a build with no RTC in it, so one recorded how long after power-on a case was made and the other moved backwards across a reset | ADR-0027 | `verified_by:` `make test` for the document's contents, for its ceiling derived from the format, and that a case name cannot forge a key; `make test` and `make build` jointly for the format string, which is invariant 6's gate now that both compile it, with `make check-formats` behind them. That a *new* key can mean what it says is review-only — nothing can check a name against reality |
| 30 | A running sum is wide enough that no value its count's type can hold overflows it, and a `static_assert` beside the fields states that bound — the RSSI sums are `int64_t` over `uint32_t` counts. A persisted integer is parsed with `strtoll`/`strtoull`, never `strtol`/`strtoul`: `long` is 32 bits on the device and 64 on the host, so a wide value reads whole under `make test` and clamps on the board. A figure wider than its field saturates and never wraps, because a count wrapped to 0 switches off the check it was persisted for | ADR-0029 | `verified_by:` `make test` for both averages past 32 bits, a 64-bit sum round-tripping the document, and a count of 2^32 reading back as `UINT32_MAX`; the `static_assert` in `make build` and `make test` for the bound. The parser's width is review-only — no host test can see `long` clamp |
| 31 | An `snprintf` whose return advances a running buffer offset checks that return against the space remaining before adding it — a negative result or one at or beyond the remaining size stops the accumulation and re-terminates, rather than folding a would-have-written length into the offset, which underflows `size_t` and overflows the buffer (CWE-190). Logic like this is pulled out of a device-only path into a pure helper so a host test can reach it | ADR-0034 | `verified_by:` `make test` (`test_i2c_scan` — a canary after a deliberately undersized buffer survives) for this site, and CodeQL `cpp/overflowing-snprintf` for the class, now that CodeQL is live on this repo |

When an ADR's consequences create a rule that all *future* work must follow, add the row
in the same commit as the ADR.
