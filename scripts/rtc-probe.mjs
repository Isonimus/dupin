#!/usr/bin/env node
/**
 * Probe: does this board carry a clock chip?
 *
 * ADR-0027 records the Cardputer ADV as carrying one, and a case-creation timestamp waits on
 * whether it does (LEDGER.md). The v1.1 has none. The answer decides whether a timestamp can
 * read a clock on the ADV or needs a time source that does not exist yet, so it is measured on
 * the board rather than taken from a product page.
 *
 * The firmware reports it once at boot, in `[BOOT] RTC:`: whether M5Unified found a clock chip,
 * which board it detected at runtime, and every address answering on the internal I2C bus. The
 * scan is the stronger half. M5Unified only tries a PCF8563/BM8563 at 0x51, so its "none" rules
 * out one part, not a clock.
 *
 * It answers once and the result lands in an ADR, so it is deliberately not a `make` target
 * (CLAUDE.md §3), and it asserts nothing about the answer.
 *
 * Usage: node scripts/rtc-probe.mjs   (DUPIN_ENV defaults to cardputer_adv; DUPIN_PORT
 *        overrides the auto-detected port)
 */
import {
  VerifyFailure, captureUntil, recordRun, resolveBaud, resolvePort, resolvePython, runPio,
} from './verify-harness.mjs';

// The ADV is the board the question is about; the v1.1 is the control.
const ENVIRONMENT = process.env.DUPIN_ENV ?? 'cardputer_adv';
const REPORT_PREFIX = '[BOOT] RTC: ';
// Matches verify-input: the 25 s boot budget has no headroom on these boards (ADR-0014).
const BOOT_TIMEOUT_MS = 40_000;
// The one clock part M5Unified probes on an ESP32-S3 board, marked in the scan so a reader
// need not look it up. The scan itself is the evidence, whatever answers.
const KNOWN_CLOCK_ADDRESSES = new Map([[0x51, 'PCF8563/BM8563']]);
const REPORT_PATTERN =
  /^\[BOOT\] RTC: (found|none); board=(\S+); (?:no internal I2C bus|internal I2C answers at((?: 0x[0-9A-F]{2})+| nothing))$/;

/** Split the boot report into its fields, failing loudly on a line of any other shape. */
function parseReport(line) {
  const match = REPORT_PATTERN.exec(line);
  if (match === null) {
    throw new VerifyFailure(`clock report has an unexpected shape: ${line}`);
  }
  const [, clock, board, scanned] = match;
  const addresses = scanned === undefined || scanned === ' nothing'
    ? [] : scanned.trim().split(' ').map((hex) => Number.parseInt(hex, 16));
  return { clock, board, hasInternalBus: scanned !== undefined, addresses };
}

function describeAddress(address) {
  const hex = `0x${address.toString(16).toUpperCase().padStart(2, '0')}`;
  const part = KNOWN_CLOCK_ADDRESSES.get(address);
  return part === undefined ? hex : `${hex} (${part} address)`;
}

async function main() {
  const python = resolvePython();
  const port = resolvePort();
  const baud = resolveBaud();
  console.log(`rtc-probe: env=${ENVIRONMENT} port=${port} baud=${baud}`);

  const recording = { gate: 'rtc-probe', environment: ENVIRONMENT };
  const { outcome } = await recordRun(recording, async ({ buildLog, transcript }) => {
    console.log('rtc-probe: building and uploading…');
    await runPio(['run', '-e', ENVIRONMENT, '-t', 'upload'], buildLog);

    console.log(`rtc-probe: resetting, waiting for ${JSON.stringify(REPORT_PREFIX)}…`);
    await captureUntil({
      python, port, baud, marker: REPORT_PREFIX, timeoutMs: BOOT_TIMEOUT_MS, transcript,
    });
    const line = transcript.find((captured) => captured.includes(REPORT_PREFIX));
    const report = parseReport(line.slice(line.indexOf(REPORT_PREFIX)));

    const bus = report.hasInternalBus
      ? (report.addresses.length === 0
        ? 'enabled, nothing answered'
        : report.addresses.map(describeAddress).join(', '))
      : 'not enabled on this board';
    return [
      `environment:      ${ENVIRONMENT}`,
      `port:             ${port} @ ${baud}`,
      `report:           ${line.trim()}`,
      '',
      `detected board:   ${report.board}`,
      `M5Unified clock:  ${report.clock}`,
      `internal I2C:     ${bus}`,
    ];
  });
  if (outcome === 'FAIL') process.exitCode = 1;
}

await main();
