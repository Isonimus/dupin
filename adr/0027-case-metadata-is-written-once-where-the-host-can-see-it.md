---
id: '0027'
title: "Case metadata is written once, where the host can see it"
type: architecture
status: accepted
date: 2026-09-17
supersedes: []
superseded_by: []
---

## Context

1. **Three bare `%lu` sites format a `uint32_t` in `src/storage/session_manager.cpp`**, the form
   invariant 6 forbids: `"created"` and `"lastSaved"` in `createSession()`, and `"lastSaved"` in
   `saveCaseStats()`. All three are correct on the platform that compiles them, which is why no
   gate has ever failed on them. `-Werror=format` is on both envs and the host mock carries the
   same format attribute the real `Print::printf` has, so the joint enforcement invariant 6
   claims does exist — but the first two sit inside `#ifndef NATIVE_BUILD` and the host never
   compiles them, and on the device `uint32_t` *is* `unsigned long`, so `%lu` matches. The third
   passes the host only because its `#else` arm substitutes `0UL`: an `#ifdef` inside the
   argument list changes the argument's type per arm, so each arm type-checks against a
   specifier neither arm would survive with the other's argument. A compiler cannot see a
   violation in code it is not given, and neither cutout hands it over.

2. **The document has two writers and no readers.** `createSession()` writes `name`, `created`,
   `sessions`, `totalUptime`, `stats`, `baseline`, `detections` and `lastSaved` as a 512-byte
   literal; `saveCaseStats()` rewrites the whole file from a second literal carrying the same
   keys **minus `created`**. `writeFile` truncates, so the first save deletes the field the
   creation wrote. Nothing in the tree parses `case.json` — `SessionMeta::created` and
   `lastUsed` are declared and filled by nobody, and `listSessions` reports every case's counts
   as 0 rather than reading the file, which `LEDGER.md` already tracks.

3. **There is no clock.** `dupin::getTimestamp()` returns `millis()`, and no RTC is referenced
   anywhere in `src/` or `include/`. Both timestamp fields are `millis() / 1000`, so `created`
   records how many seconds after *that* boot the case was made — a case created ten seconds
   into a boot carries `"created":10` forever — and `lastSaved` moves backwards every time the
   board is reset. Neither key can mean what it says on this hardware.

4. The baseline half of this same file was moved out of the cutout for exactly this reason
   (ADR-0003 decision 4, ADR-0026): while formatting and parsing lived inside it, the host tests
   asserted the `#else` line and nothing more.

## Decision

1. **One function formats the document.** `formatCaseMetadata(name, stats, out, outSize)`
   returns the bytes written and 0 when it would not fit — the contract `formatBaselineEntry`
   already uses. Both writers call it; neither holds a format literal of its own.

2. **It lives outside `#ifndef NATIVE_BUILD`**, so the host compiles it and `-Werror=format`
   checks every specifier in it on both envs (ADR-0003 decision 4). This is what makes the
   `%lu` question answerable by a gate instead of by review.

3. **A new case's document is the same document.** `createSession()` passes a
   default-constructed `CaseStats`, whose `sessionCount` is already 1 — so the initial file and
   every later one are one format with different figures, and a key cannot be added to one and
   forgotten in the other.

4. **The document carries no `created` and no `lastSaved`.** A key whose value is uptime does
   not mean what it says, and neither field has a consumer (invariant 18). They are deleted
   rather than renamed: an honest name would describe two readings of a counter that resets, in
   a file nothing reads.

5. **The ceiling is derived from the format**, as `kMaxCaseMetadataBytes` — the widest each
   value can print plus the keys taken from the format text itself, never a hand-written 512
   (invariant 20).

6. **The case name is escaped on the way in** with the same `escapeJsonString` the SSID half
   uses. Escaping is a property of the document, not a claim about which callers exist today:
   a name reaching `saveCaseStats` came from a folder on the card, not from `isValidName`, and
   "FAT32 forbids a quote in a filename" is a fact about the filesystem rather than about this
   code (invariant 19).

## Alternatives rejected

- **Fix the three sites in place** — `%u` plus `static_cast<unsigned>`, which is what the ledger
  line proposed. It closes the line and keeps the blindness: the host still never compiles the
  creation document, so the next bare specifier written behind that cutout lands exactly the
  same way, and `case.json` still has no test of any kind. It also cannot carry a regression
  test, because both platforms behave identically before and after the change.

- **Keep the fields with honest names** (`createdAtUptimeSeconds`). True, and still two readings
  of a counter that resets, in a file with no reader.

- **Wire the ADV's hardware RTC and write a real timestamp.** That is a feature with its own
  questions — who sets the clock, what a board without one does (the Cardputer v1.1 has no RTC),
  and whether a confidently wrong clock is worse than no clock at all. It belongs in its own
  decision on its own evidence, not folded into the change that found the fields empty.
  `LEDGER.md` carries it.

  > **Correction (2026-09-17, ADR-0030):** "the ADV's hardware RTC" does not exist. Measured on
  > the ADV, M5Unified finds no clock chip, and the only addresses answering on the internal I2C
  > bus are the audio codec, the keyboard controller and the IMU. Neither board has a clock, so
  > this option needs a time source added first. Rejecting it here stands, for the reasons above.

- **Read `case.json` back before rewriting it, to preserve `created`.** That needs the first
  `case.json` parser this repo has ever had — the same one the deferred `listSessions` item
  needs — and it would spend it on preserving a field that measures uptime.

- **A `#ifdef`-free timestamp passed in as a parameter, keeping both fields.** It would fix the
  format hole without deleting anything, and it would leave the firmware writing a key that
  cannot mean what it says. The format violation is the symptom here; the field is the defect.

## Consequences

- `case.json` loses two keys. Nothing in this repo reads them, and an operator reading the file
  on a PC loses two numbers that were never times. A card written by an older build keeps its
  copies; no reader exists to be confused by either shape.

- The case document is host-testable for the first time: `make test` asserts what it contains,
  that it fits its derived ceiling, and that a crafted case name cannot inject a second key.

- Invariant 6's joint gate now covers this document on both envs. The general hole it exposes —
  that a cutout hides the host half wherever one is written — is not closed by this decision;
  it is closed by a gate that reads the form rather than the type, which is its own decision.

- `saveCaseStats` and `createSession` can no longer drift, which is what produced a document
  that silently dropped a field on first save.
