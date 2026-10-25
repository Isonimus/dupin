# Dupin Architecture

> **Defensive Wi-Fi Intelligence Toolkit**

This is the architecture and detective-mode reference for Dupin. For project philosophy see
[MANIFESTO.md](../MANIFESTO.md); for how to build and contribute see
[CONTRIBUTING.md](../CONTRIBUTING.md); for the roadmap see [ROADMAP.md](../ROADMAP.md). Durable
design decisions are recorded as ADRs under [adr/](../adr/).

---

## Target Hardware

| Component | Specification |
|-----------|---------------|
| **Platform** | ESP32-S3 |
| **Test Device** | M5Stack Cardputer / Cardputer ADV |
| **Mode** | Promiscuous (monitor mode) |

---

## 1. High-Level Architecture

Dupin is **layered, not modal-exclusive**.  
Modes are lenses over the same data stream.

```
┌──────────────────────────┐
│  Radio Capture Layer     │  (promiscuous mode)
└─────────────┬────────────┘
              │
┌─────────────▼────────────┐
│  Frame Normalization     │
│  (802.11 parsing)        │
└─────────────┬────────────┘
              │
┌─────────────▼────────────┐
│  Observation Store       │  ← Dupin Core Memory
│  (ring buffers + stats)  │
└──────┬─────────┬─────────┘
       │         │
 ┌─────▼───┐ ┌───▼────────┐
 │ Baseline│ │ Event Stream│
 └─────┬───┘ └────┬────────┘
       │          │
┌──────▼───┐ ┌────▼────────┐
│ Inference │ │ Mode Engines │
│ Engine    │ │ (Watson...) │
└──────┬───┘ └────┬────────┘
       │          │
┌──────▼──────────▼────────┐
│ UI / Logs / Alerts        │
└───────────────────────────┘
```

---

## 2. Core Data Model (Shared by All Modes)

This is critical. Everything else builds on this.

### 2.1 Frame Types Ingested

- Beacon
- Probe Request
- Probe Response
- Association / Reassociation
- Disassociation
- Deauthentication
- Data (optional / header-only)

### 2.2 Observation Schema

```typescript
Observation {
  timestamp
  frame_type
  ssid?               // nullable
  bssid?
  transmitter_mac
  receiver_mac?
  channel
  rssi
  security {
    open | wep | wpa | wpa2 | wpa3
    cipher?
  }
  capabilities {
    ht?
    vht?
    he?
  }
}
```

### 2.3 Storage Tiers

| Tier | Duration | Purpose |
|------|----------|--------|
| Short-term ring buffer | seconds–minutes | Live analysis |
| Aggregated counters | minutes–hours | Trend detection |
| Baseline snapshots | persistent | Reference state |

---

## 3. Mode-by-Mode Specification

Now the important part.

### 🕵️ Watson Mode — Passive Logging & Baselining

> **Role:** Ground truth. No interpretation.

**Inputs:**
- Beacon frames
- Probe requests
- Probe responses
- Assoc / disassoc frames

**Data Collected:**
- SSID → BSSID mapping
- Channel usage per BSSID
- RSSI min / max / avg
- Security capabilities per SSID
- Probe request frequency per device

**Stored State:**

```typescript
Baseline {
  ssid: {
    known_bssids[]
    typical_channels[]
    rssi_range
    security_profile
  }
}
```

**Outputs:**
- Logs only
- Baseline snapshots
- No alerts

> *"Watson never cries wolf."*

---

### 🕵️ Marple Mode — Anomaly Detection

> **Role:** Spot deviations from local normal.

**Inputs:**
- Watson Baseline
- Live observation stream

**Detection Rules (Examples):**
- New BSSID advertising known SSID
- Known SSID on unexpected channel
- RSSI suddenly much stronger than baseline
- Security downgrade (WPA2 → Open)
- Beacon interval anomalies

**Output Events:**

```typescript
Anomaly {
  type
  ssid
  bssid?
  confidence
  deviation_metric
  first_seen
}
```

**Behavior:**
- Soft alerts
- No conclusions
- Time-decaying confidence

> *"Marple notices the crooked picture frame."*

---

### 🕵️ Poirot Mode — Follow-Up Heuristics

> **Role:** Focused investigation after suspicion.

**Inputs:**
- Active anomalies from Marple
- Short-term ring buffer

**Heuristics:**
- Probe response promiscuity (responds to many SSIDs)
- Probe attraction rate (clients suddenly clustering)
- BSSID movement (RSSI gradient over time)
- Channel hopping behavior
- Beacon personality mismatch (capabilities inconsistency)

**Output:**

```typescript
FollowUpFinding {
  anomaly_id
  signal
  strength
  explanation
}
```

**Behavior:**
- Time-boxed
- Triggered only when anomaly exists
- Designed to reduce ambiguity

> *"Just one more thing…"*

---

### 🕵️ Holmes Mode — Correlation & Hypothesis Engine

> **Role:** Turn signals into narratives.

**Inputs:**
- Marple anomalies
- Poirot findings
- Historical baseline

**Correlations:**
- Multiple anomalies → single actor
- Temporal clustering
- Cross-channel behavior
- Repeated patterns across days

**Hypotheses:**

```typescript
Hypothesis {
  type: EvilTwin | RogueAP | Misconfig | Interference
  confidence
  supporting_evidence[]
}
```

**Output:**
- Ranked hypotheses
- Confidence evolution
- No enforcement

> *"Holmes explains why."*

---

### 🕵️ Baskerville Mode — Rogue AP Hunting

> **Role:** Physical localization.

**Inputs:**
- Target BSSID from Holmes
- Live RSSI samples

**Techniques:**
- RSSI gradient tracking
- Directional movement inference
- Signal stability vs mobility

**Output:**

```typescript
HuntStatus {
  target_bssid
  trend: approaching | receding | static
  confidence
}
```

**UI:**
- Hot/cold indicator
- No maps (ESP constraint)

> *"Something is stalking the network."*

---

### 🕵️ Dupin Core — Memory & Reconstruction

> **Role:** Trust & explainability.

**Inputs:**
- All stored events
- Past incidents

**Functions:**
- Case reconstruction timeline
- Confidence adjustment based on history
- Baseline aging
- False-positive suppression

**Output:**
- "This resembles Incident #3"
- Step-by-step reasoning
- Forensic replay

> *"Dupin remembers."*
