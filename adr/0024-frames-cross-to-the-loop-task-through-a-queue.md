---
id: '0024'
title: "Frames cross to the loop task through a queue, and what will not fit is counted"
type: architecture
status: accepted
date: 2026-09-16
supersedes: []
superseded_by: []
---

## Context

`LEDGER.md` carried a hand-off with nothing between its two ends: mode state is written
from the Wi-Fi task and read from the loop task, and `src/` holds no mutex, critical
section or atomic anywhere. ADR-0022 removed the worst single instance — a case switch
clearing Watson's baseline under a running capture — and left the rest as read from the
code rather than reproduced.

Reading it before deciding found a second reach the ledger line had missed, and it is the
one that matters. `esp_wifi_set_promiscuous_rx_cb` is set in `WiFiCapture::start`, and the
handler calls the user callback inline, so everything below runs on the Wi-Fi task:

| Site | What runs on the Wi-Fi task |
|---|---|
| `onFrame` | `g_modeManager.broadcastObservation` mutates every active mode's tables |
| `MarpleMode::raiseAnomaly` | calls its callback synchronously |
| `onAnomalyDetected` | `M5Cardputer.Speaker.tone()`, then `showToast()` |
| `showToast` | `updateDisplay()` — `fillScreen`, the whole panel, then `pushSprite(0, 0)` |

So the Wi-Fi task can render a full frame and push the sprite while the loop task is
drawing into the same `M5Canvas` on its own 1 Hz timer. Separately, the autosave in
`loop()` serialises Watson's baseline into an SD write while `onFrame` keeps inserting
into it.

What reading could not settle is the size of either fix, so `scripts/race-probe.mjs` was
written to measure it. Two runs, 2026-09-15, 125 s of capture each, a Cardputer in a room
with a dozen 2.4 GHz networks in earshot:

| Figure | Run 1 | Run 2 |
|---|---|---|
| Task `onFrame` runs on | `wifi` | `wifi` |
| Task `loop()` runs on | `loopTask` | `loopTask` |
| Longest gap between two `loop()` passes | 129 ms | 131 ms |
| One `updateDisplay()` | 16.8 ms | 16.8 ms |
| One SD baseline save | 19.8 ms | 21.4 ms |
| Wi-Fi task stack free, lowest | 4104 bytes | 4104 bytes |
| Mean frame rate | 15.6/s | 14.5/s |
| **Peak frames between two passes** | **23** | **5** |

Three of those change the decision.

**A lock across the save is cheap, and the argument against one is not throughput.** The
save it would be held across measured 19.8 and 21.4 ms — about a third of a frame at the
mean rate, three or four at the burst rate the 23-per-pass peak implies. The intuition
that a mutex would starve capture is wrong, and is recorded here as wrong so it is not
raised again as if it had been measured.

**The peak a queue would be sized from does not reproduce.** Twenty-three frames between
two passes in one run and five in the next, same firmware, same room, same duration. A
depth chosen from either figure is a guess wearing a number.

**The gap between passes is 13× its nominal.** `loop()` ends in `delay(10)`, and the
measured worst case is 131 ms — a render, an autosave and the modal case menu's 50 ms
repaint all sit inside a pass. Whatever carries frames across has to survive a consumer
that disappears for an eighth of a second.

The Wi-Fi task had 4104 bytes of stack free in both runs, but neither run entered the
renderer from it: `rendersOffLoopTask` was 0, because no anomaly fired. The headroom
figure describes the cheap path, not the one through LovyanGFX, and no measurement here
says what that path costs.

## Decision

1. **`onFrame` posts and returns; the modes are reached only from `loop()`.** It parses
   the frame into an `Observation` and hands it to a queue. Everything downstream — the
   modes, the anomaly callbacks, the toast, the tone, the status line, the renderer, the
   SD writer — then runs on one task. The absence of a lock stops being luck and becomes
   correct by construction.

2. **No lock is introduced, and that is the point.** A mutex would be affordable, as the
   figures above show, but it fixes only the half that reads mode state. The Wi-Fi task
   would still enter the renderer through `showToast`, and a lock around mode state does
   not make two tasks pushing one sprite safe. A rule that every future callback must
   remember to take a lock is also a rule that will eventually be forgotten; single-writer
   has nothing to forget.

3. **The queue depth is named once, with its cost stated where the name is** — the
   ADR-0013 pattern, as invariant 25 restates it. It is not derived from the measured peak,
   because that peak moved 4.6× between two runs; it is chosen with headroom over the worse
   of them and the choice is written down as a choice.

4. **A full queue drops, counts, and says so.** The drop count is reported by `state` and
   by `probe`, and reaches the panel. A silenced drop is the failure mode this whole record
   exists to remove: capture that quietly stops observing looks exactly like an environment
   that went quiet.

5. **Frames are conserved, and a gate asserts it.** Everything `onFrame` accepts is either
   drained by `loop()` or counted as dropped, and `drained + dropped == accepted` is an
   assertion that fails if ingestion is lost — rather than a check for a number that stayed
   0, which passes just as well on firmware that captured nothing (ADR-0016 decision 3,
   invariant 17).

6. **The queue itself is device-only and is proven on the device.** It is a platform
   primitive, like the SD mount: there is no host-testable logic hiding inside it, and
   dressing one up would be the cutout ADR-0003 warns about. The probe becomes the gate that
   asserts decisions 4 and 5 and takes the name the convention gives a gate —
   `scripts/race-probe.mjs` is now `scripts/race-verify.mjs`, reachable as `make verify-race`
   (invariant 2). It keeps its measured figures in the run's artifacts: a probe answers once,
   but these are the figures that catch the defect coming back, and `maxFramesPerPass` is
   what will say when the depth chosen in decision 3 has stopped being enough.

## Alternatives rejected

**A mutex around mode state.** Measured affordable and still insufficient: it leaves the
renderer reachable from two tasks, and it makes correctness a habit every future callback
has to keep. Recorded here with its measured cost so the next reader knows it was priced
rather than assumed.

**Moving only the effects — toast, tone, status — to the loop task behind flags.** Cheaper,
and it does fix the renderer reentry. It leaves mode state unsynchronised, which is the
original ledger item, and makes the remainder harder to see by removing its most visible
symptom.

**A hand-rolled single-producer ring buffer, so the drain logic could be host-tested.**
Attractive against invariant 1 until the memory ordering is written out: the producer and
the consumer sit on different cores of a dual-core ESP32, and a pair of volatile indices is
not a fence. Buying a host test with a lock-free structure nobody on this project will
verify is a worse trade than a FreeRTOS queue and a device gate.

## Consequences

- The ledger's race line closes when the fix lands; the two new lines it spawned — the
  measured figures, and the stop path that unmounts the card and forgets the case — stay
  open on their own terms.
- Invariant 26 follows from decisions 1, 4 and 5.
- `onFrame` becomes a bounded copy-and-post, which is also what makes the Wi-Fi task's
  stack headroom stop mattering: the deepest thing it will call is the queue.
- An anomaly is raised one `loop()` pass later than it is observed — up to 131 ms by the
  measurement above. Nothing here is a real-time path; the toast, the tone and the status
  line are all things a person reads.
- `probe`'s counters keep their place after the fix rather than being deleted with the
  defect they diagnosed: `rendersOffLoopTask` and `renderCollisions` must read 0 forever,
  and a future change that reintroduces a cross-task render is caught by the figure that
  found this one. `probe` gains the conservation figures and `state` gains `dropped=`, so
  the operator and the gate read one number rather than two.
- The cap on what the queue holds is a memory decision, stated where it is named. Raising
  it costs `sizeof(Observation)` per slot, of which 33 bytes are the SSID field.
