---
id: '0026'
title: "A partial save fails loudly at both ends"
type: architecture
status: accepted
date: 2026-09-17
supersedes: []
superseded_by: []
---

## Context

ADR-0025 made the stop path say out loud when a write did not land, and recorded in the same
breath that the claim is exactly as strong as the return value it rests on, and no stronger.
This is that return value.

It did not rest on anything. `saveBaseline` opened the file, checked that `SD.open` had
succeeded, and then discarded the return of all four of its writes — the envelope `printf`,
the separator, every entry, the closing brace. `writeFile` in `src/utils/file_utils.h`, which
`saveCaseStats` goes through, discarded its one. A card that filled or came loose during the
entry loop returned `true`, left `baseline.json` without its ending, and the stop printed
`[SD] Saved to case <name>` over the damage.

Four measurements, all taken from the pinned framework (ADR-0008) rather than assumed:

1. **A short write is visible.** `File::print` and `File::printf` reach
   `VFSFileImpl::write`, which is `fwrite`, which returns fewer items than asked when the
   card is full or failing. Comparing each write's return against its length is a real check.
2. **A short write is not the only failure.** `fwrite` buffers. `File::flush()` and
   `File::close()` both return `void`, and the `fflush` and `fclose` under them have their
   results discarded. A card that fills while the buffer drains fails *after* the last write
   has already returned its full count. Nothing in the write path sees it.
3. **`getWriteError()` is dead here.** It reports a flag that only `Print::setWriteError`
   raises, and nothing in the FS or SD path ever calls it. The mechanism the `LEDGER.md` item
   proposed as the fix does not exist on this platform; it would have returned 0 forever and
   read like a check.
4. **The reader was the last line of defence and was not holding it.**
   `parseBaselineJson` has three exits that return a count indistinguishable from success: an
   SSID whose closing quote never arrives, a baseline that fills before the document runs out,
   and a document that simply stops after a complete entry. The third is the one a truncated
   save produces, and it comes back as "this case has fewer SSIDs than you remember".

The last of those is the one that made this look harder than it is. An entry ends
`...,"bssids":[]}`. The document ends `...]}`. A document cut off after an entry has exactly
the final two characters a finished one has, so the obvious check — does it end in `]}` —
cannot work. What separates them is the character before: the writer puts a newline there and
an entry cannot contain one.

## Decision

**1. Every write is checked against what it was asked to write.** A write that returns fewer
bytes than its length ends the document there and is reported. This is decision 1 because it
is the cheap half and it catches the common case: a card that is full or gone reports it on
the write that discovers it.

**2. The card's own figure is the last word.** After the file is closed, its size on the card
must equal the number of bytes the writes accounted for. This exists because of measurement 2:
the buffered failure lands after the last write returned its full count, and re-opening the
file and asking the card how long it is, is the only check that sees it. One extra open and
stat per save is the whole cost.

**3. The reader requires the ending the writer emits, and takes it from the writer's own
constant.** The document ends `\n]}\n`. The newline is what makes that unambiguous: an entry
is one `snprintf` with no newline in it, and every octet that could carry one arrives escaped
(ADR-0017), while the separator between entries is `\n,\n`. So a document cut off anywhere
ends in entry text, in `]}`, or in `\n,` — and never in `\n]}`. The writer's ending and the
reader's expectation are the same named constant, so a change to one is a change to both.

**4. A document with no envelope is not checked, and that is said where it is decided.**
Two kinds have none: the bare arrays written before the envelope existed (ADR-0017), and the
`{"ssids":[]}` that `createSession` writes for a case with nothing in it yet. Neither ends the
way the writer ends a document, and neither can be told apart from a truncation by its text.
Reporting them damaged would raise an `[ERROR]` on every boot of a card that is perfectly
intact, which is how a real alarm gets trained away. They are reported complete. The check
covers every document this firmware has written since the envelope landed, which is every
document a save has produced.

**5. Both halves report which of three things happened, not a count and not a bool.** Reading
returns `Complete`, `Truncated` or `Overflowed` beside the number of entries; writing returns
`Complete`, `ShortWrite` or `EntryTooLong` beside the number of bytes. `Overflowed` and
`EntryTooLong` are faults in *this* firmware — a baseline too small for the card's document,
a format ceiling that has drifted from the format — and a caller that cannot tell them from a
card failure reports the wrong cause to the operator.

**6. The document is written into a sink, so the checking can be tested on the host.**
`writeBaselineDocument` takes anything with `write(const char*, size_t) -> size_t`. The
device passes one holding the `File`; `make test` passes one that goes short at byte N and
asserts the result says `ShortWrite`. Without this the write-checking would sit behind
`#ifndef NATIVE_BUILD` with no test over it at all, and no gate can reach it either — making
a card refuse a write mid-document needs a capability invariant 14 keeps off the serial
channel. This is ADR-0003 decision 4 applied: the cutout hides the SD call and nothing else.

**7. Both failures are `[ERROR]`, by ADR-0025 decision 5.** A write that did not land and a
document that did not end have no retry behind them. The autosave's `[SD] ... retrying` line
is unaffected: it retries because the count it compares against is left where it was, and
that is still true when the save fails for this reason rather than for a mount failure.

**8. A truncated document still yields the entries that survived.** They are real
observations and discarding them buys nothing — the defect was the silence, not the partial
data. The load reports `[ERROR]`, names the case, and says how many entries it got out before
the document ran out.

## Alternatives rejected

**`getWriteError()` before `close()`**, which is what `LEDGER.md` proposed. Measurement 3:
nothing raises that flag on this platform, so the check would have passed forever while
reading like a guard. Recorded here rather than left as a nicer-sounding option, because it
is the first thing the next reader will reach for.

**Applying invariant 21 to `writeFile` and its neighbours** — returning *which* way the write
failed from every file helper rather than a `bool`. Invariant 21 exists so that a caller can
report a reason the API threw away; no caller here branches on it. `saveCaseStats` does the
same thing whether the open failed, the write went short or the size came back wrong, and so
does the stop path above it. The reason the operator needs is a sentence on the wire, and it
is printed at the site that knows it. An enum returned so that every caller can collapse it
to `if (!ok)` is the speculative generality the bar forbids. `saveBaseline`'s own result is
the exception, and it is exceptional for a stated reason: two of its three outcomes are bugs
in this firmware rather than facts about the card.

**Writing to a temporary file and renaming over the original**, so a failed save leaves the
previous good baseline on the card. This is a real improvement and it is not this one: it
buys durability, where this ADR buys truthfulness, and it introduces a window between
`SD.remove` and `SD.rename` in which a power loss loses the baseline outright — FAT rename
cannot replace an existing file, so the window cannot be closed, only measured. It goes to
`LEDGER.md` to be decided on its own evidence.

**Adding a `"complete":true` field** to the envelope, written last, and reading the document
as truncated when it is absent. More explicit than decision 3, and rejected on cost: it
changes the format, it needs the hand-written empty document in `createSession` changed to
match, and — the deciding one — every document already on the card was written without it, so
a reader that trusted only the marker would call every intact case damaged. The fallback for
those would have to be the ending check anyway, which means shipping two mechanisms where one
answers. The objection to decision 3, that it leans on whitespace nobody knows is
load-bearing, is answered by deriving both ends from one constant rather than by a second
format.

## Consequences

The document format gains one field. Readers are unaffected: every field is looked up by key
(ADR-0017), so a document with the marker parses under the old reader and one without it
parses under the new.

`createSession` keeps writing `{"ssids":[]}` for a new case, and that is deliberate rather
than overlooked. It is a second spelling of the format, but not a stale one: a document with
no entries and no `currentSession` parses to exactly the state the writer's own empty document
parses to, since an absent counter reads as zero. It is left alone because the alternatives
are worse than the duplication — assembling it from the writer's constants means rendering the
envelope and appending the ending by hand at a call site that has no baseline to write, and
writing it through the writer means standing up a 64-entry `Baseline` on the stack of a task
that has no use for one. A comment at the site says so, so the next reader does not "fix" it.

`make verify-baseline` exercises decision 2 on every run: the size comparison runs on every
successful save, so arithmetic that does not match what the writer counted turns every save
into a failure and the gate goes red. What the gate cannot do is drive the failure path — no
serial command can make a card refuse a write, and inventing one would breach invariant 14.
That half is review-only, which is why decision 6 puts the logic somewhere `make test` can
reach it instead.

A card carrying a document truncated by an earlier firmware will now say so on every boot of
that case, until the next successful save rewrites it. That is the intended behaviour and not
a regression: the document really is damaged, and the alternative is the silence this ADR
exists to end.
