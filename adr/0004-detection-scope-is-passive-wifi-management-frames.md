---
id: '0004'
title: "Detection scope: passive 2.4 GHz Wi-Fi management frames"
type: architecture
status: accepted
date: 2026-09-10
supersedes: []
superseded_by: []
---

## Context

`CLAUDE.md` opens by stating what Dupin is — a passive monitor-mode tool that
observes and never transmits — but nowhere is it written down what Dupin will and
will not *try to detect*. That boundary lived only implicitly in the five modes,
which is exactly the kind of load-bearing decision that gets re-litigated from
nobody's memory the first time someone asks "should Dupin catch X too?".

The question was forced by a measured cross-check against the sibling red-team
suite, Adversary (an ESP32 offensive tool in the same `local_repo`), which has
grown a large arsenal since it was last reviewed. Each attack it ships was mapped
to whether it manifests as a frame a passive 2.4 GHz 802.11 monitor can observe.

**The Wi-Fi attack plane is already inside Dupin's model.** Adversary's entire
`attack/` and `ap/` suite produces 802.11 management or data frames, and every one
maps onto a detector Dupin already defines in `src/modes/marple/marple.h`:

| Adversary module | Observable as | Dupin detector (exists) |
| --- | --- | --- |
| `attack/deauth` | deauth flood to a victim | `TARGETED_DEAUTH_THRESHOLD` |
| `attack/karma_ap` | many SSIDs from one BSSID | `KARMA_SSID_THRESHOLD` |
| `attack/beacon_spam` | burst of new SSIDs | `BEACON_FLOOD_THRESHOLD` |
| `attack/evil_twin`, `ap/*captive/portal` | one SSID from many BSSIDs | `CLONE_SPAM_THRESHOLD` + Baskerville rogue-AP hunt |
| `attack/probe_flood` | probe-request storm | `DEFAULT_PROBE_THRESHOLD` |
| `sniffer/traffic_proxy` (rogue-AP MITM) | an unexpected AP for a known SSID | Baskerville / clone detection |

So on the Wi-Fi plane there is nothing new to reach for: growth in that plane is a
new Marple detector or threshold, not a new sensor.

**One adjacent plane is in physical reach; the rest are not.** The genuinely new
attack families Adversary added — BLE spam (`ble_spanner`), a BLE HID injector,
Classic-BT tooling, sub-GHz CC1101 (OOK, spectrum sweep, jamming), NRF24 jamming,
IR, RFID/NFC, and BadUSB — sort cleanly by one physical fact: what radio or bus the
signal lives on.

- **BLE** is 2.4 GHz and the ESP32-S3 Dupin already runs has a BLE radio. It is not
  802.11, so no current mode sees it, but it is the one attack surface Dupin could
  observe without new hardware.
- **Sub-GHz CC1101, NRF24 jamming, IR, RFID, BadUSB** are out of reach. Jamming is
  RF energy, not a frame — seeing it needs a spectrum sensor Dupin does not have;
  sub-GHz and NRF24 are other radios; IR and RFID are other buses; BadUSB is not
  wireless at all. A passive 802.11 monitor cannot detect any of them, and pretending
  a roadmap item could is worse than saying so.

## Decision

**1. Dupin's detection scope is passive observation of 2.4 GHz 802.11 management and
data frames in monitor mode.** It transmits nothing. New detection capability within
this scope is added as Marple detectors and thresholds over the frames already
captured, not as new radios.

**2. The Wi-Fi attack plane is in scope and substantially covered.** The mapping
above is the evidence; a newly observed Wi-Fi attack type earns a detector, and the
absence of one is a gap to log, not a scope question.

**3. BLE is the one adjacent plane in scope-by-reach, and is deferred as a feature.**
It rides the same S3 radio, so it is buildable, but a detector's thresholds must cite
measured on-air behaviour, not guesses (`~/.claude` design rule; ADR-0002 carries the
same discipline for firmware). A ledger line carries it, gated on a
`scripts/ble-probe.mjs` that measures what BLE advertisement spam actually looks like
before any detector is designed.

**4. Sub-GHz, NRF24 jamming, IR, RFID and BadUSB are explicitly out of scope.** They
are not 802.11 frames and Dupin has no sensor for them. **This ADR is their record.**
No ledger line is created per out-of-scope attack: a decision *not* to pursue
something is recorded once, as a decision, not as a standing worklist of things we
have chosen not to do. If the question is raised again, the answer is here.

## Consequences

A single `[feature]` line is added to `LEDGER.md` for BLE observation, citing this
ADR and naming the probe-before-detector sequence.

No ledger lines are added for the out-of-scope planes; their record is decision 4.
This is the deliberate opposite of enumerating them, so the ledger stays a worklist of
things we intend to do rather than a catalogue of things we do not.

Adversary is named here only as the measurement source for the cross-check; it is a
separate corpus, and nothing in it is a Dupin decision.

This ADR adds no `CLAUDE.md` §4 invariant. Scope is a boundary on what work is in
bounds, not a per-slice definition-of-done rule, so encoding it as a checklist row
would misfile it; the record belongs here and the one actionable consequence is the
BLE ledger line.
