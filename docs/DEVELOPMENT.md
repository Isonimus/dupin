# Development Guide

> Setting up your environment to contribute to Dupin.

---

## Prerequisites

### Required

- **PlatformIO Core** — [Installation Guide](https://platformio.org/install/cli)
- **Git** — For version control
- **C++17 compatible toolchain** — Provided by PlatformIO

### Optional

- **VS Code** with PlatformIO extension
- **Carpduter ADV** or ESP32-S3 development board

---

## Getting Started

### 1. Clone the Repository

```bash
git clone https://github.com/your-repo/dupin.git
cd dupin
```

### 2. Install PlatformIO

```bash
# Via pip
pip install platformio

# Or via installer script
curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
python3 get-platformio.py
```

### 3. Build the Project

```bash
# Default target (Carpduter ADV)
pio run

# Specific environment
pio run -e esp32s3_dev
```

### 4. Run Tests

```bash
# Native tests (no hardware needed)
pio test -e native

# On-device tests
pio test -e carpduter_adv
```

---

## Project Structure

```
dupin/
├── src/                  # Source code
│   ├── main.cpp          # Entry point
│   ├── core/             # Core data structures
│   │   ├── observation.h # Observation struct
│   │   └── baseline.h    # Baseline management
│   ├── capture/          # Wi-Fi capture layer
│   │   ├── wifi_capture.h
│   │   └── frame_parser.h
│   ├── modes/            # Detective modes
│   │   ├── mode.h        # Base interface
│   │   ├── watson/       # Passive logging
│   │   ├── marple/       # Anomaly detection
│   │   ├── poirot/      # Follow-up
│   │   ├── holmes/       # Correlation
│   │   └── baskerville/  # AP hunting
│   ├── storage/          # Data persistence
│   ├── ui/               # User interface
│   └── utils/            # Shared utilities
├── include/              # Public headers
│   └── dupin.h           # Main include
├── lib/                  # External libraries
├── test/                 # Tests
│   ├── unit/             # Unit tests
│   ├── integration/      # Integration tests
│   └── mocks/            # Mock implementations
└── docs/                 # Documentation
```

---

## Coding Standards

See [CONTRIBUTING.md](../CONTRIBUTING.md) for:

- Naming conventions
- Code style guidelines
- Testing requirements (≥70% coverage, target 99%)
- Documentation standards
- Git workflow

---

## Build Environments

| Environment | Platform | Use Case |
|-------------|----------|----------|
| `carpduter_adv` | ESP32-S3 | Primary target hardware |
| `esp32s3_dev` | ESP32-S3 | Generic S3 dev boards |
| `native` | Desktop | Unit testing without hardware |

---

## Useful Commands

```bash
# Build
pio run                      # Build default environment
pio run -e native            # Build for native testing

# Upload
pio run -t upload            # Upload to connected board
pio run -t upload -e carpduter_adv  # Specific environment

# Test
pio test                     # Run all tests
pio test -e native           # Native tests only
pio test -e native -f unit   # Only unit tests

# Monitor
pio device monitor           # Serial monitor
pio device monitor -b 115200 # Specific baud rate

# Clean
pio run -t clean             # Clean build artifacts

# Dependencies
pio lib install <library>    # Install library
pio lib update               # Update all libraries
```

---

## Debugging

### Serial Output

All Dupin output goes to serial at 115200 baud:

```bash
pio device monitor
```

### ESP32 Exception Decoder

The build is configured to include the exception decoder. Stack traces will be automatically decoded in the monitor.

### Native Debugging

For unit tests, use your preferred C++ debugger:

```bash
# Build with debug symbols
pio test -e native --verbose

# Debug with gdb
gdb .pio/build/native/program
```

---

## Common Issues

### Upload Fails

1. Check USB connection
2. Hold BOOT button while uploading
3. Verify correct port: `pio device list`

### Tests Fail to Compile

1. Ensure Unity framework is available
2. Check for missing mock implementations
3. Verify `NATIVE_BUILD` flag is set

### Serial Monitor Shows Garbage

1. Verify baud rate matches (115200)
2. Check USB CDC is enabled in build flags

---

## Architecture Overview

See [Claude.md](../Claude.md) for the full architecture specification.

```
Radio Capture → Frame Parser → Observation Store
                                      ↓
                              Watson (Baseline)
                                      ↓
                              Marple (Anomaly)
                                      ↓
                              Poirot (Heuristics)
                                      ↓
                              Holmes (Correlation)
                                      ↓
                              Baskerville (Hunt)
```

---

## Next Steps

1. Read [ROADMAP.md](../ROADMAP.md) for current implementation status
2. Pick a task from the roadmap
3. Follow TDD: write tests first, then implement
4. Submit a PR following the contribution guidelines

---

> *"What changed — and why does it matter?"*