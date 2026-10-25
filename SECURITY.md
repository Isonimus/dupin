# Security Policy

## What counts as a security issue here

Dupin is a **passive** monitoring tool: it transmits nothing and has no attack capability. Its
monitor-mode observation of the air is its intended function, **not** a vulnerability, and all use
is governed by the [DISCLAIMER](DISCLAIMER.md): authorized observation, education, and research
only.

A reportable security issue is a defect **in the firmware itself** that puts its *operator* or
their data at risk. For example:

- A parsing bug in how on-air data is handled: an SSID or other frame field is attacker-chosen
  input, so a crafted value that crashes the device, corrupts a stored baseline, or escalates into
  code execution is in scope.
- Loss or silent corruption of data stored on the SD card (baselines, case metadata).
- Any remote-code-execution or memory-safety bug reachable from received frames.

Please do **not** report the existence of monitor mode, or ask for help monitoring an environment
you are not authorized to observe.

## Reporting

Report privately through **GitHub's private vulnerability reporting** (the repository's Security
tab, "Report a vulnerability"). If that is unavailable to you, contact the maintainer,
[@Isonimus](https://github.com/Isonimus). Please do not open a public issue for a suspected
firmware vulnerability.

## Supported versions

The project is in **alpha**. Only the latest `main` is supported; fixes land there.

| Version            | Supported |
| ------------------ | --------- |
| latest `main`      | yes       |
| older commits/tags | no        |
