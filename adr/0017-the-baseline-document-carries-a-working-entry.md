---
id: '0017'
title: "The baseline document carries a working entry, and is sized from its own format"
type: architecture
status: accepted
date: 2026-09-12
supersedes: []
superseded_by: []
---

## Context

ADR-0016 made a case own its baseline and `make verify-baseline` proves the count survives a
reset. The count was the only thing that did.

**A restored entry could not do its job.** Three fields were written by no one and read by
detectors that quietly gave up when they were missing:

| Field | Consumer | What a restored entry did instead |
|---|---|---|
| `channels[]` / `channelCount` | `MarpleMode::checkUnexpectedChannel` | `if (entry == nullptr \|\| entry->channelCount == 0) return false;` — the detector returns before comparing anything |
| `rssiCount` | `SSIDBaseline::isRSSIInRange` | `if (rssiCount == 0) return true;  // No baseline yet` — every sample is in range, so the RSSI anomaly never fires |
| `rssiSum` | `rssiAverage()`, reported as an anomaly's `expectedRSSI` | reports 0 dBm — a wrong number in a record rather than a missing one |
| `lastSeenSession` | `Baseline::expireStaleEntries` | `lastSeenSession > 0 && …` — a restored entry is exempt from expiry forever |

So two of Marple's detectors were off for every restored SSID, and stale expiry never reached
one. The gate passed the whole time, because the gate asserted a count and the count was
right. That is ADR-0015's lesson arriving again from a different direction: the number agreed
with itself, and the thing it named did not work.

**The `LEDGER.md` line describing this was wrong in the other direction.** It said a restored
entry "expires at once" once the session counter passes the threshold. The guard is
`lastSeenSession > 0`, so the opposite is true — such entries are never expired. The line is
corrected in place, `LEDGER.md` being the one mutable file.

**`lastSeenSession` cannot work alone.** It is compared against `Baseline::m_currentSession`,
which is not persisted either and restarts at 0 every boot. Persisting one without the other
would trade an entry that never expires for an entry that expires against a counter from a
different power cycle.

**`saveBaseline()` escaped nothing it wrote.** An SSID is up to 32 arbitrary octets off the
air, not text this firmware chose. A network named `My"Net` closed the JSON field early and
the rest of its name was read as document structure — and an SSID is attacker-chosen, so
`x","ssid":"Injected` would have forged a second entry in a file the operator believes
records what was observed.

**The document's size ceiling was an estimate, and it was wrong.** `kMaxBaselineFileBytes`
shipped in the previous commit as 16384, with a comment reading "64 SSIDs with 8 BSSIDs each
serialise to roughly 12KB". Computed from the format rather than guessed, that same worst case
was **19589 bytes**. `saveBaseline()` bounds nothing, so a large baseline was written happily
and then refused on read — and the refusal returned 0, which `openCase()` reports as the case
having no stored baseline. The operator would be told a case was fresh while its baseline sat
on the card.

**`firstSeen` and `lastSeen` are a different matter.** `getTimestamp()` is `millis()`, so both
are uptime since boot; a persisted value would be compared against a counter that restarted.
Greping the tree, neither field on `SSIDBaseline` has a single consumer outside `baseline.h`.

Measured on the board, at the point a baseline would load: free heap **154312 bytes**, largest
contiguous block **102388**.

## Decision

**1. A field is persisted when a restored entry needs it to behave like a live one.** The test
is a named consumer, not completeness. `channels[]`/`channelCount`, `rssiSum`/`rssiCount` and
`lastSeenSession` are carried, each because a specific detector is inert without it.
`firstSeen`/`lastSeen` are not: they measure uptime, they cannot survive the reboot they would
have to survive to matter, and nothing reads them.

**2. The session counter travels with the document, in an envelope.**
`{"currentSession":N,"ssids":[…]}`. It belongs to the baseline rather than to any entry, and
`parseBaselineJson()` sets it even when the document carries none — a case whose file predates
the envelope resets the counter rather than inheriting the previously open case's, which is
the same boundary the entries themselves needed.

**3. Older documents parse.** Every field is looked up by key and an absent key leaves its
default, so a bare array written before this ADR loads with its entries intact and its new
fields at defaults. There is no version number: the document is key-addressed, so a reader can
already tell what it has, and a version field would be a second source of truth about the
same question.

**4. SSIDs are escaped on write and unescaped on read.** `"`, `\`, the short escapes, and
`\u00XX` for the remaining control characters. This is what makes the format safe against its
own input: an injected quote always carries its backslash, so a crafted SSID cannot forge the
`"ssid":"` key the parser scans for.

**5. Both halves of the format are portable, and only the file I/O is not.**
`formatBaselineEntry()` joins `parseBaselineJson()` outside the platform cutout, so the round
trip is asserted on the host — format, parse, compare — rather than inferred from a device
transcript. ADR-0003 decision 4 said a cutout wraps only what is genuinely platform-specific;
the writer had the same defect as the reader and for the same reason.

**6. The ceiling is derived from the format.** `kMaxBaselineEntryBytes` is built from the
literal key text, the widest each value can print, and the per-BSSID and per-channel costs;
`kMaxBaselineFileBytes` multiplies it by `DUPIN_MAX_SSIDS`. A host test formats the widest
entry the structure can hold and asserts it fits, so the constant cannot drift from the format
it describes. A number typed by hand is a number that goes stale the first time a field is
added — which is precisely what had already happened.

**7. A document too large to read is an error, not an absence.** It logs `[ERROR]` with both
sizes and fails the gate. A reader that cannot distinguish "unreadable" from "not there" hands
the operator a fresh baseline and says nothing.

## Consequences

**The ceiling moved from a wrong guess to a computed bound**: 16384 to **38701** bytes, with
`kMaxBaselineEntryBytes` at 604. Against the 102388-byte largest contiguous block measured on
the board, a worst-case load is 38% of it, so the whole-document read stays — streaming would
be complexity bought with a measurement that does not support it.

**The real files are far smaller, and the old ones still load.** From the gate's transcript:

```
[SESSION] Loaded baseline (22 SSIDs, 2941 bytes)   <- written before this ADR, parses
[SESSION] Saved baseline (25 SSIDs)                <- rewritten in the new format
[SESSION] Loaded baseline (25 SSIDs, 4971 bytes)
```

134 bytes per entry before, 199 after: the four new fields cost about 65 bytes each. A full
64-SSID baseline at that rate is roughly 12.7KB, so the 38701-byte ceiling is bounding a worst
case that the air is unlikely to produce — which is what a ceiling is for.

**`make verify-baseline` still passes** — 28 saved, 28 restored for `home`, switching to `Test`
moving it to 37 — as do `make verify-boot` at 3740 ms, `make verify-boot-adv` at 3741 ms and
`make verify-input` at 7/7. The transcript above is the backward-compatibility proof on real
hardware rather than in a fixture: that 2941-byte file was written by the previous commit.

**The host suite went from 299 to 310 cases.** Every new test was mutation-checked:

```
channels not written              -> 2 fail
rssiSum/rssiCount not written     -> 2 fail
lastSeenSession not written       -> 2 fail
escaping off                      -> 3 fail, incl. "Expected 1 Was 2" — the crafted
                                     SSID forged a second entry
Unknown security falls to Open    -> 1 fail, "Expected 0 Was 1"
kEscapedSsidBytes under-sized     -> 1 fail — the ceiling test catches its own constant
```

The escaping mutant's second failure is worth keeping: `Expected 'a\b\x01c\nd' Was
'a\x08\x01c\nd'` — an unescaped backslash-b was read back as a backspace. The round trip
corrupts in both directions, not only on quotes.

**Flash grew 1036363 to 1037979 bytes**, 1616 for the escaping, the envelope and four more
fields. RAM is unchanged at 104236.

**What this does not change.** Ownership is ADR-0016's and is untouched: the case still owns
the baseline, `openCase()` still clears before loading, and the gate still asserts the round
trip per case. This ADR is about what the document has to carry for a restored entry to be
worth restoring.

**Left open**, as a `LEDGER.md` line: `SSIDBaseline::firstSeen` and `lastSeen` are written on
every observation and read by nothing, which is 512 bytes of DRAM across the table and a field
a future reader will assume means something. Deciding whether they earn their place is a
separate change from deciding what the document carries.
