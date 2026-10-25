---
id: '0016'
title: "A case owns its baseline, and one store holds it"
type: architecture
status: accepted
date: 2026-09-12
supersedes: []
superseded_by: []
---

## Context

Dupin's investigative unit is the **case**. The operator names one, the firmware makes a
directory for it, and anomalies, findings and hypotheses are logged under it. Detection is
comparative: Marple and Poirot score what they see now against Watson's baseline, which they
hold by pointer (`main.cpp` wires `setBaseline(&g_watson.getBaseline())` at startup). So the
baseline decides what "new" means, and if it is not the case's baseline then "new SSID" is
computed against every environment the device has ever been switched on in.

**It was not the case's baseline.** Two mechanisms had grown up alongside each other:

| | `SessionManager::saveBaseline`/`loadBaseline` | `WatsonMode::saveBaseline`/`loadBaseline` |
|---|---|---|
| Store | SD, `<case>/baseline.json` | NVS via `Preferences`, namespace `dupin_bl` |
| Scope | per case | one, global |
| Capacity | `DUPIN_MAX_SSIDS` = 64, 8 BSSIDs each | **8** SSIDs, **4** BSSIDs each |
| Written by | autosave in `loop()`, and on stop | autosave in `update()`, and in `end()` |
| Read by | **nothing** | `WatsonMode::begin()` |

`SessionManager::loadBaseline` had no production caller anywhere in the tree; the only
reference to it outside its own definition is a host test asserting that it returns false.
`loadSession()` switched the current case without touching the baseline at all. So the
per-case `baseline.json` files were not per-case in content — each was a copy of the one
global NVS store, written into whichever directory happened to be current.

`scripts/baseline-probe.mjs` measured it on the board, driving only the existing `state` and
`key` vocabulary (ADR-0014):

```
  boot                 case=-            mode=none     ssids=0
  first-case-opened    case=home         mode=none     ssids=0
  first-case-watson    case=home         mode=Watson   ssids=8
  second-case-opened   case=Test         mode=Watson   ssids=8

case switched:    home -> Test (yes)
baseline count:   8 -> 8 (unchanged)
```

The case really did change and the baseline did not follow it. The transcript carries exactly
one `[STATUS] Baseline loaded from storage` for the whole run — `WatsonMode::begin()`'s NVS
path — and none on the case switch. That message names no store, which is part of why this
stayed invisible: it is true of either mechanism.

The count is 8 because 8 is the NVS write cap (`saved < 8` in `WatsonMode::saveBaseline`), not
because the environment holds eight networks. A baseline that silently keeps the first 8 of 64
SSIDs and the first 4 of 8 BSSIDs is a summary, not a baseline, and it is the input to every
anomaly score the firmware produces.

**The SD loader had never worked either.** It computes the SSID's length from the closing
quote, clamps it to `DUPIN_SSID_MAX_LEN`, and then calls `getOrCreate(ptr)` with the raw
pointer into the JSON buffer rather than with a terminated copy — the computed length is dead.
`setSSID` does `strncpy(ssid, ptr, 32)`, so every restored entry would have been named by a
32-character slice of `MySSID","security":"WPA2",...`. It compiled, and its host test passed,
because the whole body sits under `#ifndef NATIVE_BUILD` and the test asserts the `#else`
line — the exact failure ADR-0003 recorded and the reason its decision 4 narrowed what a
platform cutout may cover. A cutout that swallows portable parsing makes a test of that
parsing pass while proving nothing.

Two autosave timers were also running the same job to different stores, both at 60 s:
`g_config.baselineAutosaveInterval` in `loop()` writing SD, and `m_saveTimer` in
`WatsonMode::update()` writing NVS.

## Decision

**1. A case owns its baseline.** Opening a case restores that case's baseline; there is no
baseline that outlives a case. This is what makes `case` mean anything to detection rather
than only to the directory logs are written in.

**2. One store and one format: `<case>/baseline.json` on the SD card.** `WatsonMode`'s NVS
pair is deleted along with its autosave timer and its `Preferences` include. Two stores for
one fact have no sync path — the failure stele:ADR-0005 exists to prevent — and here they had
already diverged into one that was written and never read and one that was read and silently
truncating.

**3. Opening a case replaces the baseline wholesale.** `clear()` then load, never merge.
Carrying entries across a case boundary is the defect itself, so the boundary is where the
old contents go.

**4. One function opens a case.** `openCase()` in `main.cpp` does the `loadSession`, the
replace-and-restore, and the badge reset. Both call sites — creating a case and selecting an
existing one — already ran identical code after `loadSession`; this is the second instance
rather than the third, on the same grounds ADR-0015 extracted `openLineReader`: a rule about
what opening a case means cannot live in two places and stay one rule.

**5. Parsing a baseline is portable, and only the file read is not.** `parseBaselineJson()`
takes a mutable buffer and a `Baseline&` and lives outside `#ifndef NATIVE_BUILD`, so the
SSID-termination fix ships with a host test that fails before it and passes after. Applying
ADR-0003's decision 4 here is what makes the fix assertable at all; the SD `File` read stays
under the guard, and the device gate covers it.

**6. With no card there is no case, and no baseline is persisted** — the firmware says so
rather than keeping a lossy global one. Card-less persistence is a feature this removes on
purpose; it is recorded in `LEDGER.md` as a feature line, not left as an implicit promise.

**7. The gate asserts what the format actually preserves.** `<case>/baseline.json` round-trips
the SSID, the security type, the known BSSIDs, `rssiMin`/`rssiMax` and the beacon count. It
does **not** carry `channels[]`, `rssiSum`/`rssiCount`, or the `firstSeen`/`lastSeen`/
`lastSeenSession` stamps. `scripts/baseline-verify.mjs` asserts the surviving fields and
claims nothing about the dropped ones; widening the format is a separate decision about a data
format, and a ledger line carries it.

## Consequences

**The baseline now follows the case, measured.** `make verify-baseline`, on the board:

```
case under test:     home
captured and saved:  22 SSIDs
restored after reset: 22 SSIDs for case home
switched to:         Test, ssids 22 -> 25

reset-to-last-step:  4175 ms (pass 2)
serial lines:        230 across both passes
```

The count written under `home` came back under `home` across a real reset, and changed when
the case changed. The count itself is a property of the room, not of the firmware — runs here
saved 19, 22 and 24 — so the gate asserts that the number agrees with itself across a reset
and never that it is any particular value. What the magnitude does settle is the old store:
the NVS mechanism wrote **8** entries, so on every one of those runs it was discarding two
thirds of the environment, and every anomaly score was computed against what survived.

**`make verify-input` shows the other half of the decision.** Where its transcript read
`ssids=8` after `key w`, it now reads `ssids=0`:

```
key del    -> [CMD] key 0x1B        (skips the case menu, so no case is open)
key w      -> [KEY] Watson selected
state      -> [STATE] mode=Watson running=no menu=no case=- ssids=0
```

That 8 was the global store being restored regardless of context. With no case there is now
no baseline, which is decision 6 visible in a transcript. The gate still passes 7/7 over the
same steps; only the number it was never asserting has changed.

**The host suite went from 291 to 299 cases**, all eight new ones over `parseBaselineJson`.
Each was mutation-checked rather than assumed, in three passes:

```
getOrCreate(ptr) instead of the terminated copy   -> 5 of 8 fail, "Expected Non-NULL"
strstr(ptr, "},") as the field bound              -> 1 fails, "Expected 0 Was 9999"
the original `secPtr < strstr(ptr, "},")` guard   -> 3 fail, "Expected 4 Was 0"
```

The third is worth stating plainly: comparing against a `strstr` that never matched meant the
guard was `secPtr < nullptr`, so **no entry's security type was ever parsed**, not merely the
last one's. The clamping test passes under the first mutant, correctly — an overlong SSID's
first 32 characters are the same either way, and that test is about clamping.

**Proven in both directions on the device too.** Two mutations, `make` exiting 1 on each:

```
opening a case restores nothing   -> FAIL — case home reopened with its 11 SSIDs restored
                                     ... did not appear within 5000 ms
the case boundary does not clear  -> FAIL — the baseline did not follow the case: opening
                                     Test reported 25 SSIDs but state says ssids=26
```

**The second mutation first passed, and that is why it was run.** The gate originally compared
the count `openCase()` logged against the count `state` reported — and both were
`baseline.count()`, so a merged baseline read exactly like a replaced one and the assertion
compared a value with itself. `SessionManager::loadBaseline` now returns the number of entries
it read out of *this case's file* rather than a bool, `openCase()` logs that, and the two
figures are independent. The gate was wrong in the way ADR-0015 describes: a true number under
a label that named something else. Only a forced failure could have shown it.

**What this does not change.** Detection is untouched: the same scoring, the same stale-entry
expiry, the same autosave interval in `loop()`. `make verify-boot` reports 3738 ms and
`make verify-boot-adv` 3740 ms, unchanged. The firmware got smaller — 1041055 to 1036363 bytes
of flash and 104300 to 104236 of RAM on `cardputer` — because a whole persistence mechanism
left.

**What is knowingly left open**, each as a `LEDGER.md` line rather than an implied promise:
card-less baseline persistence, removed here on purpose; the fields the format does not carry;
and that `saveBaseline()` escapes nothing, so an SSID containing a quote or a backslash writes
a document its own parser will misread. That last one is pre-existing and unrelated to
ownership, but it is now on the record instead of in the code only.
