# Dupin

> **Defensive Wi-Fi Intelligence Toolkit**

[![Firmware CI](https://github.com/Isonimus/dupin/actions/workflows/firmware.yml/badge.svg)](https://github.com/Isonimus/dupin/actions/workflows/firmware.yml)
[![CodeQL](https://github.com/Isonimus/dupin/actions/workflows/codeql.yml/badge.svg)](https://github.com/Isonimus/dupin/actions/workflows/codeql.yml)
[![PlatformIO](https://img.shields.io/badge/build-PlatformIO-orange)](https://platformio.org/)
[![ESP32-S3](https://img.shields.io/badge/platform-ESP32--S3-blue)](https://www.espressif.com/)
[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)

---

## What is Dupin?

Dupin is a **passive Wi-Fi security tool** designed to observe, analyze, and detect anomalies in wireless environments, without attacking, impersonating, or interfering with networks.

> *"What changed, and why does it matter?"*

### Key Features

- **Passive observation**: Monitor mode only, no packets transmitted
- **Behavioral baselining**: Learn what's "normal" for your environment
- **Anomaly detection**: Spot evil twins, rogue APs, and security downgrades
- **Detective modes**: Layered analysis from logging to correlation
- **AP hunting**: Physical localization via signal tracking

---

## Detective Modes

| Mode | Purpose |
|------|---------|
| **Watson** | Passive logging & baseline building |
| **Marple** | Anomaly detection from baseline deviations |
| **Poirot** | Follow-up heuristics for suspicious activity |
| **Holmes** | Correlation engine for hypothesis generation |
| **Baskerville** | Physical hunting of rogue APs |

---

## Hardware Requirements

- **ESP32-S3** based board (developed and tested on the M5Stack Cardputer and Cardputer ADV)
- USB-C cable for programming
- (Optional) FAT32-formatted microSD card for per-environment baselines

---

## Quick Start

### Prerequisites

- [PlatformIO](https://platformio.org/install) (CLI or IDE extension)

### Build & Upload

```bash
# Clone the repository
git clone https://github.com/Isonimus/dupin.git
cd dupin

# Build the default Cardputer firmware
pio run -e cardputer

# Upload to the connected board
pio run -e cardputer -t upload

# Monitor serial output
pio device monitor
```

Build targets: `cardputer` (default), `cardputer_adv`, `esp32s3_generic`, and `native` (the
host test environment).

### Run Tests

```bash
# Run the host unit suite (no hardware needed)
pio test -e native
```

The host suite is the default verification; logic that cannot be asserted on the host is covered
by the device verify scripts under `scripts/` (see [CONTRIBUTING.md](CONTRIBUTING.md)).

---

## Project Structure

```
dupin/
├── src/
│   ├── main.cpp          # Entry point
│   ├── core/             # Data structures
│   ├── capture/          # Wi-Fi frame capture
│   ├── modes/            # Detective modes
│   ├── storage/          # Persistence & buffers
│   ├── ui/               # Display & serial output
│   └── utils/            # Common utilities
├── include/              # Public headers
├── test/                 # Unit & integration tests
├── adr/                  # Architecture Decision Records (the "why")
├── docs/                 # Documentation (architecture, development guide)
├── scripts/              # Device verify scripts and the serial harness
├── MANIFESTO.md          # Project philosophy
├── CONTRIBUTING.md       # Contribution guide & engineering standards
└── ROADMAP.md            # Implementation plan
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [MANIFESTO.md](MANIFESTO.md) | Project philosophy & principles |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Contribution guide & engineering standards |
| [ROADMAP.md](ROADMAP.md) | Implementation roadmap & progress |
| [docs/architecture.md](docs/architecture.md) | Architecture & detective-mode reference |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | Developer setup guide |
| [adr/INDEX.md](adr/INDEX.md) | Index of Architecture Decision Records |

---

## Philosophy

Dupin follows three core principles:

1. **Dupin observes**: Never attacks, impersonates, or interferes
2. **Normality is local**: Baselines are environment-specific
3. **Context matters**: Patterns over signatures

See [MANIFESTO.md](MANIFESTO.md) for the full philosophy.

---

## Session Management

Dupin supports multiple named sessions stored on SD card, allowing you to maintain separate baselines for different environments (e.g., Home, Office, Airport).

### Usage

| Action | Key |
|--------|-----|
| Select session at boot | Boot menu appears automatically |
| Switch session at runtime | Press **c**, with capture stopped |
| Create new session | Select **[+] New Case** and type a name (`a-z`, `A-Z`, `0-9`, `_`, up to 30) |
| Rename session | Select session, press **Tab** |
| Delete session | Select session, press **x**, then **Enter** to confirm (any other key cancels) |
| Navigate menu | **O** (up) / **L** (down) |
| Confirm | **Enter** |
| Delete a character while typing | **Del** (on an empty name it cancels instead) |
| Cancel typing | **Fn + `** |
| Skip session selection | **Del** on the list |

If a name is refused (empty, already taken, no card, or the card will not write), the
reason is shown in red on the menu and the name you typed stays in the editor so you can fix
it.

The menu itself is refused while capture is running: pressing **c** says `Stop capture first`
and nothing else happens. Capture keeps saving into whichever session is open, and opening
another one would move the running capture into it and clear the baseline mid-write, so every
action in the menu (open, create, rename, delete) waits for a stop (ADR-0022).

Stopping capture writes the baseline and the case statistics to the open session and leaves
that session open, so you can start and stop as many times as you like in one boot and every
window is saved to the same place. The panel says which of three things happened: `STOPPED`
when the write landed, `SAVE FAILED` in red when the card was there and it did not, and
`No case: not saved` in yellow when there was no session to write to (ADR-0025).

A save that the card does not take is a failure, not a success with a shorter file: every write
is checked against what it was asked to write, and the file's length on the card is checked once
it is closed, because the card can still refuse while the last write is being flushed. If a
session's stored baseline was damaged by an earlier failure, opening it restores the networks
that survived and the serial log says the document stops before its end, rather than the
session quietly coming back holding fewer networks than it had (ADR-0026).

The list shows six sessions at a time and turns a page when the selection passes the last one,
so the highlighted session is always on screen. When there are more than six, the title says
which of them you are looking at, as `7-12/14`. At most 16 sessions are listed; if the card
holds more, the title ends in `+` and the boot log says the card holds more than the list can
cache. Delete or rename sessions to reach the rest (ADR-0023).

Deleting removes the session's folder and everything in it from the card, and cannot be undone.
A folder holding a subfolder (which Dupin never creates) is refused and left untouched. If the
card fails partway through, the menu says the delete is incomplete rather than that nothing
happened, because some of the session's files may already be gone.

### SD Card Requirements

- **Format**: FAT32
- **Pins**: M5Cardputer ADV (MOSI=14, MISO=39, CLK=40, CS=12)
- **Directory**: Sessions stored in `/dupin/<session_name>/`

---

## Troubleshooting

### A microSD card is not detected

Dupin mounts the card through the SD library's own reset (ADR-0032), which recovers a card left
active by another firmware such as M5Launcher. If a card still does not mount:

1. Confirm it is formatted **FAT32**.
2. Reseat the card, or power-cycle the board (power off, then on).

Dupin is **alpha** software: expect rough edges, and please report a reproducible mount failure
with what the board and card were doing just before it (see [CONTRIBUTING.md](CONTRIBUTING.md)).

---

## Contributing

Please read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting code.

---

## License

MIT License. See [LICENSE](LICENSE) for details.

---

## Acknowledgments

Named after [C. Auguste Dupin](https://en.wikipedia.org/wiki/C._Auguste_Dupin), Edgar Allan Poe's fictional detective who pioneered analytical crime-solving through observation and deduction.

> *"Dupin remembers."*
