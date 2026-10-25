---
id: '0014'
title: "The firmware reads the port, and an injected key runs the real dispatch"
type: architecture
status: accepted
date: 2026-09-12
supersedes: []
superseded_by: []
---

## Context

ADR-0009 built the device verify harness and measured a hard limit on it: `src/` contained
no `Serial.read`, no `Serial.available`, no read from the port of any kind, so the firmware
was write-only over serial and a gate could assert what it volunteered and nothing else.
That ADR declined to change it, on the grounds that adding a command reader is a decision
about production code rather than about the harness. Confirmed still true on the commit this
work began: the only `readBytes` calls in the tree are file I/O.

Four `LEDGER.md` items came to block on that absence — the input path, the
`saveBaseline`/`loadBaseline` round-trip, and the pioarduino platform bump, which is
explicitly due "when a serial-driven verify script exists to notice a regression."

**The firmware was not merely unreachable. It was inert.** `setup()` enters a modal loop on
the case-selection menu whenever an SD card is present and spins there until a physical
keypress clears it:

```cpp
if (sdAvailable) {
    g_inCaseMenu = true;
    refreshCaseList();
    while (g_inCaseMenu) {
        renderCaseMenu();
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange()) { handleCaseMenuInput(); }
        delay(50);
    }
}
```

`loop()` therefore never runs at all until a human touches the keyboard — not the heartbeat,
not the capture update, not anything. ADR-0010 had already met the symptom, moving the boot
marker because `[DUPIN] Ready!` was unreachable unattended, but recorded it as a fact about
the marker rather than about the firmware being unattendable. Any control channel served
only from `loop()` would have been dead on arrival on every device with a card in it, which
is every device that matters.

**The ledger's stated reason for the input gate was wrong.** Its line says the gate is
wanted because "nothing mechanically catches a misspelled board guard." But `handleKeyboard`
is guarded by `#ifndef NATIVE_BUILD`, not by any `DUPIN_HAS_*`, and ADR-0011 established
that all nine of those macros are read by nothing. There is no board guard on the input path
to misspell, so a gate built to catch that would have been guarding a mechanism this repo
does not have. The defect worth catching is the more general one the M5StickC tree actually
exhibited: an input handler that does not run.

**And there is a limit no serial channel can cross.** An injected key enters at the dispatch
and bypasses `M5Cardputer.Keyboard` entirely, so it can prove everything downstream of that
point and nothing upstream of it. The physical keys remain untestable unattended. Stating
this is the difference between a gate and a gate that is trusted for more than it measures.

## Decision

**1. The firmware reads the port, in every build.** Not behind a flag. A flag would mean the
gate proved a binary nobody runs, which is the failure ADR-0008 closed by pinning the
platform: verification has to be evidence about what ships.

**2. The channel exposes nothing the operator could not already do at the keyboard.** This
is the rule that makes decision 1 safe, and it is recorded as a standing invariant rather
than left to judgement. `ping` and `state` are read-only. `key` runs the same dispatch a
keypress runs. Nothing commands the radio, and there is nothing to command: Dupin has no
transmit path, so the channel cannot be turned into one.

The access it requires is physical USB, and that access already allows reflashing the
device outright. A channel reachable only by someone who could replace the firmware grants
strictly less than they already have.

**3. One dispatch per context, reached through a seam rather than copied.**
`handleKeyboard`'s 14-branch chain became `dispatchKey(keyIsActive)`, a template over a
predicate: the physical path passes `M5Cardputer.Keyboard.isKeyPressed`, so its behaviour is
unchanged to the bit, including the priority the `else if` order encodes. The serial path
passes an equality test against one injected key. `handleCaseMenuInput` became
`dispatchCaseMenuKey(const KeyEvent&)`, taking a flattened value because that handler reads
`state.enter/del/fn/tab` and iterates `state.word` rather than testing raw codes.

There is deliberately no second chain. A parallel injection path would be a copy that drifts
from the one the operator uses, which is the concern the `dupin::Keyboard` ledger item
already raises about this file having two input paths.

`KeyEvent` is a *physical* shim — the keys `KeysState` exposes, flattened — and not the
`InputAction` abstraction ADR-0005 defers. That one maps keys and GPIO buttons onto
navigation intents for a second board and stays deferred until such a board is real. This
one only lets a key arrive from somewhere other than the keyboard.

**4. `setup()`'s modal loop pumps the channel too.** It is the only place the channel can be
served before a case is chosen, and without it the firmware is unreachable from boot.

**5. Parsing is pure and host-tested; dispatching is device behaviour and is gated.**
`ui/serial_command` holds the line accumulation and the grammar, asserted by sixteen host
tests, split exactly as `ui/text_wrap` is and for the same reason (invariant 1). The
dispatch functions are not host-tested and that is a decision, not an omission: every branch
drives the display, the mode manager or the session manager, so what they do is behaviour
rather than logic. `scripts/input-verify.mjs` is what covers them — the ADR-0002 split,
applied.

**6. The channel is bounded in both directions.** A line longer than `kMaxCommandChars` is
**refused**, not truncated: truncating `key wxyz` into a valid `key w` would execute
something the sender never asked for. `key` with anything other than exactly one argument is
`Unknown` for the same reason. At most `kMaxCommandBytesPerPoll` bytes are drained per loop
pass, so a flood on the control channel cannot starve a passive monitor's capture path.

**7. Replies are `[CMD]` and `[STATE]`, never `[ERROR]` or `[FATAL]`.** Those two are the
patterns the harness fails on (ADR-0009), and a refused command is not a firmware fault. A
command that cannot be honoured says so rather than being dropped, because a gate that
cannot tell "refused" from "never arrived" reports the wrong thing.

**8. `make verify-input` is the gate, and its steps are ordered so each proves something the
previous one could not.** `state` while the modal menu holds the foreground proves the
channel is served from inside `setup()`. `key del` then `state` proves an injected key
reached the case-menu dispatch, because nothing else clears that loop. `key w` then `key m`
proves the main dispatch runs *and* that the reported mode tracks the key rather than being
a fixed string — the second mode is what closes that hole.

## Consequences

**Measured on this commit.** `make verify-input` PASS, 7/7 steps, 26390 ms end to end
including build and upload, 4195 ms of that from reset to the last step, 73 serial lines.
The transcript:

```
state      -> [STATE] mode=none running=no menu=yes case=- ssids=0
key del    -> [CMD] key 0x1B
state      -> [STATE] mode=none running=no menu=no case=- ssids=0
key w      -> [KEY] Watson selected
state      -> [STATE] mode=Watson running=no menu=no case=- ssids=8
key m      -> [KEY] Marple selected
state      -> [STATE] mode=Marple running=no menu=no case=- ssids=8
```

`make test` 291 cases pass, up from 275 — sixteen new over the parser and the reader.
`make build` SUCCESS with zero warnings, RAM 104300 B (+40) and Flash 1041055 B (+2812).

**The gate was measured in both directions.** With `dispatchCaseMenuKey` made a no-op — the
closest analogue to the M5StickC defect, a handler that does not run — it fails at step three
with `state report showing the case menu was left ... did not appear within 5000 ms`, and
`make` exits 1. A gate that has only ever passed is not evidence.

**What this gate does not prove, stated so it is not assumed.** It does not prove the
physical keys work; an injected key bypasses the keyboard driver by construction. It does not
prove the *spelling* of a board guard, which is what invariant 4 asks for — nothing
mechanically checks that, and the row stays honest about it. What it proves is that both
dispatch chains run and change observable state, which is the consequence a misspelled guard
would have destroyed.

**The `[BOOT] Initialization complete` marker is reached in about three seconds, not the
twenty-five `make verify-boot` reports.** The device's own uptime stamps put the last
initialisation line at 1243 ms, and this gate satisfied seven steps within 4194 ms of the
reset. `captureUntil` re-checks its settle deadline only when a new line arrives, and an
idle device in the case menu sends none, so it always waits out the tap's full lifetime and
reports that as the boot time. The gate still works — no panic appeared in the window — but
the number it prints is the tap's clock, and ADR-0009 and ADR-0010 both quote it as if it
were the boot. Recorded as a `LEDGER.md` line rather than fixed here, because changing the
boot gate's timing loop inside the commit that adds the input gate would make neither
atomic.

**Two `LEDGER.md` items are closed by deletion** — the input-path gap, and the R11
entry-point note's premise that no second verify script existed. **Three are updated**: the
baseline round-trip and the platform bump both named "a serial-driven verify script" as
their blocker and now have a harness that can drive the firmware, so what remains is their
own assertions rather than the missing primitive; and the `dupin::Keyboard` audit line now
records that a third input concept exists.

**`resolvePort` and `resolveBaud` moved into `scripts/verify-harness.mjs`.** Each carries a
rule — no absolute device path, and the baud is read from `platformio.ini` rather than
copied — and a second copy of a rule has no sync path. That is the same reasoning
stele:ADR-0005 applies to documents, applied to code.

**One more restatement of the session-name bound is gone.** `g_inputBuffer` was `char[32]`
with a literal `g_inputPos < 30` beside it; both now derive from `kMaxSessionNameChars`, and
the `strncpy` in the rename path goes through `copySessionName`. ADR-0013 named that bound
once and this is the last place that had not been converted.
