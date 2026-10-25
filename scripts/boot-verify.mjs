#!/usr/bin/env node
/**
 * Device verification: the firmware boots to its ready state without panicking.
 *
 * This is the assertion every other device claim rests on, and the one thing a green
 * `pio run` has never been evidence for (CLAUDE.md §3, ADR-0002): the build that shipped
 * with every board guard misspelled `DDUPIN_*` compiled clean and booted useless.
 *
 * Builds, uploads, resets the board, and asserts the ready banner arrives with no logged
 * error and no panic signature in between. Writes the serial transcript and the measured
 * boot timing to a directory of this run's own under verify-artifacts/ (ADR-0021).
 *
 * Usage: make verify-boot   (DUPIN_PORT overrides the auto-detected serial port)
 */
import {
  captureUntil, recordRun, resolveBaud, resolvePort, resolvePython, runPio,
} from './verify-harness.mjs';

// Overridable because the board a developer has in hand is not always the default one,
// and a platform or env bump needs to prove itself on the env it targets (ADR-0009).
const ENVIRONMENT = process.env.DUPIN_ENV ?? 'cardputer';
// The last line setup() prints on every path, with or without an SD card. `[DUPIN] Ready!`
// was the original choice and was wrong: with a card present setup() enters the case menu
// and blocks on a keypress, so that line is unreachable unattended (ADR-0010).
const READY_MARKER = '[BOOT] Initialization complete';
const BOOT_TIMEOUT_MS = 25_000;
// Reaching the marker is not enough on its own: the crash this harness first caught came
// immediately after it, in the first canvas render. Staying up for this long afterwards,
// with no panic and no reset, is what distinguishes a boot from a boot loop.
const SETTLE_MS = 8_000;

// resolvePort/resolveBaud moved into verify-harness.mjs when the second verify script
// needed them: each carries a rule (no absolute device path; the baud is read from
// platformio.ini rather than copied) and a second copy of a rule has no sync path.

async function main() {
  const python = resolvePython();
  const port = resolvePort();
  const baud = resolveBaud();
  console.log(`boot-verify: env=${ENVIRONMENT} port=${port} baud=${baud} python=${python}`);

  const recording = { gate: 'boot-verify', environment: ENVIRONMENT };
  const { outcome } = await recordRun(recording, async ({ buildLog, transcript, startedAt }) => {
    console.log('boot-verify: building and uploading…');
    await runPio(['run', '-e', ENVIRONMENT, '-t', 'upload'], buildLog);

    console.log(`boot-verify: resetting, waiting for ${JSON.stringify(READY_MARKER)} ` +
                `then ${SETTLE_MS / 1000}s of no panic and no reset…`);
    // The elapsed time comes back from the capture, measured on the device's side of the
    // reset. Timing it from out here is what produced the tap's lifetime instead (ADR-0015).
    const markerMs = await captureUntil({
      python, port, baud, marker: READY_MARKER,
      timeoutMs: BOOT_TIMEOUT_MS, settleMs: SETTLE_MS, transcript,
    });

    const memory = buildLog.filter((line) => /^(RAM|Flash):/.test(line));
    return [
      `environment:     ${ENVIRONMENT}`,
      `port:            ${port} @ ${baud}`,
      `marker:          ${READY_MARKER}`,
      `reset-to-marker: ${markerMs} ms`,
      `settle:          ${SETTLE_MS} ms with no panic and no reset`,
      `serial lines:    ${transcript.length}`,
      ...memory.map((line) => `build:           ${line.trim()}`),
      `total:           ${Date.now() - startedAt} ms`,
    ];
  });
  if (outcome === 'FAIL') process.exitCode = 1;
}

await main();
