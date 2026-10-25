# Changelog

Notable changes, newest first. This project is in alpha and follows
[Semantic Versioning](https://semver.org/) loosely until 1.0.0.

## [0.1.0] - 2026-10-25

Initial public release.

- **Passive Wi-Fi observation.** Monitor-mode capture that transmits nothing, feeding a layered
  set of detective modes (Watson logging and baselining, Marple anomaly detection).
- **Per-environment baselines on SD.** Named cases stored under `/dupin/<case>/`, each owning its
  own baseline, with loud reporting of partial or failed writes.
- **Serial control channel.** Read-only `ping`/`state`/`probe` plus a `key` injector that drives
  the same dispatch a keypress runs, used by the device verify harness.
- **Verification harness.** Host unit suite (`pio test -e native`) plus device verify scripts
  (`make verify-boot`, `verify-input`, `verify-baseline`, `verify-race`).

[0.1.0]: https://github.com/Isonimus/dupin/releases/tag/v0.1.0
