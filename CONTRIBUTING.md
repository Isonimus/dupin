# Contributing to Dupin

Thanks for your interest in Dupin, a passive Wi-Fi intelligence toolkit for the ESP32-S3.
This guide covers how to build, test, and submit changes.

## Authorized and lawful use

Dupin is **passive**: it runs the radio in monitor mode and transmits nothing. It does not
attack, impersonate, or interfere with networks. Even so, passively capturing wireless traffic
is regulated differently across jurisdictions, and operating the radio is **your legal
responsibility**. Only monitor environments you are authorized to observe, and read the
[DISCLAIMER](DISCLAIMER.md) before you build or run the firmware. Contributions that add
transmitting, attacking, or impersonation capabilities are out of scope and will not be merged.

## Getting started

You need [PlatformIO](https://platformio.org/install) (the CLI or the IDE extension). No other
global toolchain is required: PlatformIO fetches the pinned ESP32 platform and the host test
runner on first build.

```bash
git clone https://github.com/Isonimus/dupin.git
cd dupin

# Build the default Cardputer firmware
pio run -e cardputer

# Upload to a connected board, then watch the serial console
pio run -e cardputer -t upload
pio device monitor
```

Build environments: `cardputer` (default), `cardputer_adv`, `esp32s3_generic`, and `native`
(the host unit-test environment, which excludes `main.cpp` and the capture layer).

## Engineering standards

The engineering bar, the testing standard, commit hygiene, and the language rules live in
[`docs/quality-bar.md`](docs/quality-bar.md) and in [`CLAUDE.md`](CLAUDE.md), which also records
the conventions specific to this repository. Please read them before a non-trivial change. In
short:

- **C++17** (`-std=gnu++17`) on the Arduino framework. Keep code lean and purposeful.
- **Every piece of logic gets a regression test that fails before the fix and passes after.**
  Prefer a host test; reach for a device script only when the host cannot assert the behaviour.
- **Fail loud.** No swallowed errors, no fallback that masks a real failure.
- Architecture, data formats, and boundaries are recorded as Architecture Decision Records under
  [`adr/`](adr/). As an outside contributor you do **not** need to write or touch ADRs: that is
  the maintainer's decision-record workflow. Read the relevant one when a change touches a
  decision it records.

## Verifying a change

- **Host logic:** `pio test -e native` is the default and must pass.
- **Device behaviour:** behaviour the host cannot assert (rendering, radio capture, SD mounting,
  input handling) is covered by verify scripts under `scripts/`, each reachable from a `make`
  target (for example `make verify-boot`, `make verify-input`). These need a board on a USB port.
- **A green compile is not verification.** `pio run` confirms the code parses, nothing more.

State what you ran and what it reported in your pull request. If you cannot run the device
scripts (no hardware), say so: the maintainer runs them.

## Commits and pull requests

- **Conventional commits**: `type(scope): subject`, for example `fix(storage): ...`. Types:
  `feat`, `fix`, `docs`, `refactor`, `test`, `chore`.
- **One logical change per commit.** Explain *why* in the body, not just *what*.
- Open a pull request against `main` using the pull-request template. Describe the change, how you
  tested it, and which board(s) you ran it on.

## Reporting bugs and requesting features

Use the GitHub issue forms (Bug report / Feature request). For a suspected **security** defect in
the firmware itself, follow [SECURITY.md](SECURITY.md) rather than opening a public issue.

By participating you agree to the [Code of Conduct](CODE_OF_CONDUCT.md).
