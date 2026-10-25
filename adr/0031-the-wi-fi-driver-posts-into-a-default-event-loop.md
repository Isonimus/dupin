---
id: '0031'
title: "The Wi-Fi driver posts into a default event loop"
type: architecture
status: accepted
date: 2026-09-17
supersedes: []
superseded_by: []
---

## Context

1. **Every capture run flooded the serial log.** `LEDGER.md` recorded
   `wifi:failed to post WiFi event=43 ret=259` about every 100 ms, and asked whether the default
   event loop was missing or the hop path needed the event suppressed. In the pinned IDF 5.5.2
   headers for the S3, event 43 is `WIFI_EVENT_HOME_CHANNEL_CHANGE`, and 259 is 0x103,
   `ESP_ERR_INVALID_STATE`.

2. **The loop was missing.** `WiFiCapture::start()` calls `esp_wifi_init` directly. Nothing in
   `src/` calls `esp_event_loop_create_default()`. In the Arduino core only the `Network` library
   creates the loop (`NetworkEvents.cpp`), and Dupin does not use it. The driver posts each event
   it raises into that loop, and a post into a loop that does not exist fails.

3. **Measured on 2026-09-17 with `make verify-race`**, on the same board in the same room:

   | | before (`14-21-30.521Z-PASS`) | after (`14-25-04.071Z-PASS`) |
   |---|---|---|
   | `failed to post` lines | 1,235 | 0 |
   | of which event 43 (channel hop) | 1,231 | 0 |
   | of which events 2 and 3 (`STA_START`, `STA_STOP`) | 2 each | 0 |
   | serial transcript | 1,512 lines | 276 lines |
   | mean frame rate | 8.6 frames/s | 7.9 frames/s |

   Events 2 and 3 failing shows that every event failed, not only the hop. The frame rates are
   two single runs. ADR-0029 measured 8.2 frames/s in this room, so they show no effect on
   capture in either direction, and they cannot rule out a small one.

4. **The other option hides the defect.** Lowering the `wifi` log tag would silence the lines
   while every post kept failing. Any later handler for a Wi-Fi event would then never run, and
   nothing would say why.

## Decision

1. **`setup()` creates the default event loop once**, after the frame queue and before anything
   can start Wi-Fi. A failure halts boot through `haltBootFailure`, with the IDF error name on the
   wire. Capture must not start on a driver whose events go nowhere.

2. **It is created in `setup()`, not in `WiFiCapture::start()`.** The loop outlives capture
   windows, and a second create returns `ESP_ERR_INVALID_STATE`. Lifetime calls belong to
   `setup()`, as they do for the card and the case (ADR-0025).

3. **The driver's error lines are not filtered.** With the loop in place the transcript holds
   no `E (` or `W (` lines at all, so a new one is signal.

## Consequences

- The ledger item is closed. It also asked whether the 55.03.37 bump caused the flood. It did not:
  the loop was missing whatever the platform version.
- The loop costs one FreeRTOS task. The pinned sdkconfig gives it a 2048-byte stack
  (`CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE`) and a 32-entry queue
  (`CONFIG_ESP_SYSTEM_EVENT_QUEUE_SIZE`). The firmware reports no free-heap figure, so this was not
  measured on the device. No handler is registered, so each of the ten posts a second is
  dequeued and discarded.
- A future handler on the default loop runs on the event task, not the loop task. If it touches
  mode state, invariant 26 (ADR-0024) applies to it like any other producer.
- `make verify-boot-adv` and `make verify-input` pass on this firmware. Boot measured 3758 ms from
  reset to marker, identical to ADR-0030's figure.
