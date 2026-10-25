/**
 * Shared core for the device verify scripts (ADR-0009).
 *
 * Implements the contract ADR-0002 wrote down: a verify script fails on a non-zero exit
 * from the `pio` subprocess, on a serial line matching this codebase's error convention,
 * on a panic signature, or on its own assertion not arriving within a timeout — and it
 * writes the transcript for human review either way.
 *
 * Every deadline here is a timer (ADR-0015). The device under test is a passive monitor
 * that says nothing once it has booted, so a deadline re-checked only when a line arrives
 * is a deadline that never fires on correct firmware.
 *
 * This module is not itself a verify script and deliberately does not match
 * `*-verify.mjs`, so nothing mistakes it for one.
 */
import { spawn, spawnSync } from 'node:child_process';
import { mkdirSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { createInterface } from 'node:readline';

/**
 * The failure conditions ADR-0002 names. `[ERROR]`/`[FATAL]` is this codebase's log
 * convention (src/ui/serial_logger.h); the rest are how the ESP32 reports a crash.
 */
export const FAILURE_PATTERNS = [
  { name: 'logged error', test: (line) => /^\[(ERROR|FATAL)\]/.test(line) },
  { name: 'panic', test: (line) => line.includes('Guru Meditation') },
  { name: 'abort', test: (line) => line.includes('abort() was called') },
  { name: 'backtrace', test: (line) => line.includes('Backtrace:') },
];

const TAP_READY_MARKER = '<tap-ready>';

/** The ROM banner the chip prints on every reset — see the settle window below. */
const RESET_PATTERN = /^(ESP-ROM:|rst:0x)/;

/** Raised for an expected verification failure, so callers can report it without a stack. */
export class VerifyFailure extends Error {}

const SERIAL_DEVICE_PREFIXES = ['ttyACM', 'ttyUSB'];

/** Sentinel distinguishing "the deadline passed" from a line the device actually sent. */
const TIMED_OUT = Symbol('timed-out');

/** Slack on the tap's own lifetime, so it outlives the last deadline rather than racing it. */
const TAP_GRACE_MS = 5_000;

/**
 * The board's serial port. Auto-detected so no absolute device path is baked into a script
 * or a Makefile, per the no-hardcoded-paths rule.
 */
export function resolvePort() {
  if (process.env.DUPIN_PORT) return process.env.DUPIN_PORT;
  const candidates = readdirSync('/dev')
    .filter((entry) => SERIAL_DEVICE_PREFIXES.some((prefix) => entry.startsWith(prefix)))
    .sort();
  if (candidates.length === 0) {
    throw new VerifyFailure(
      'no serial device found under /dev (looked for ' +
      `${SERIAL_DEVICE_PREFIXES.join('*, ')}*). Is the board plugged in? ` +
      'Set DUPIN_PORT to name it explicitly.');
  }
  return join('/dev', candidates[0]);
}

/** Read the baud rate from platformio.ini so it is not a second copy of that setting. */
export function resolveBaud() {
  const match = readFileSync('platformio.ini', 'utf8').match(/^monitor_speed\s*=\s*(\d+)/m);
  if (!match) throw new VerifyFailure('monitor_speed not found in platformio.ini');
  return Number(match[1]);
}

/**
 * Find an interpreter that can import pyserial. Fails loud rather than falling back to
 * an interpreter that would die later with a less obvious message.
 */
export function resolvePython() {
  const candidates = [
    process.env.DUPIN_PYTHON,
    'python3',
    join(process.env.HOME ?? '', '.platformio', 'penv', 'bin', 'python'),
  ].filter(Boolean);
  for (const candidate of candidates) {
    const probe = spawnSync(candidate, ['-c', 'import serial'], { stdio: 'ignore' });
    if (probe.status === 0) return candidate;
  }
  throw new VerifyFailure(
    `no Python interpreter with pyserial found (tried: ${candidates.join(', ')}). ` +
    'pyserial ships with PlatformIO; set DUPIN_PYTHON to override.');
}

/**
 * Run a `pio` subcommand, streaming its output into `logLines`. A non-zero exit is a
 * verification failure, not an exception to swallow.
 */
export async function runPio(args, logLines) {
  const child = spawn('pio', args, { stdio: ['ignore', 'pipe', 'pipe'] });
  const consume = async (stream) => {
    for await (const line of createInterface({ input: stream, crlfDelay: Infinity })) {
      logLines.push(line);
    }
  };
  await Promise.all([consume(child.stdout), consume(child.stderr)]);
  const status = await new Promise((resolve) => child.on('close', resolve));
  if (status !== 0) {
    throw new VerifyFailure(`pio ${args.join(' ')} exited ${status}`);
  }
}

/**
 * Spawn the serial tap. `seconds` is derived from the caller's own budget plus
 * `TAP_GRACE_MS`, so the reader outlives the last deadline it is being asked to serve —
 * the reader's clock is the backstop, never the assertion (ADR-0015).
 */
function spawnTap({ python, port, baud, budgetMs, sendStdin = false, reset = true }) {
  const seconds = Math.ceil((budgetMs + TAP_GRACE_MS) / 1000);
  const args = ['scripts/serial-tap.py',
    '--port', port, '--baud', String(baud), '--seconds', String(seconds)];
  if (reset) args.push('--reset');
  if (sendStdin) args.push('--send-stdin');
  return spawn(python, args,
    { stdio: [sendStdin ? 'pipe' : 'ignore', 'pipe', 'pipe'] });
}

/**
 * A line reader over the tap's stdout whose reads are bounded by a wall-clock deadline.
 *
 * `next(deadline)` resolves with the next device line, or `null` once the deadline passes
 * or the tap's stdout closes — `ended` tells those two apart, because a reader that died is
 * a broken harness while a device that went quiet is usually correct (ADR-0015).
 *
 * A read still outstanding when a deadline passes is carried to the next call rather than
 * abandoned: the loser of a `Promise.race` stays pending, and the line that eventually
 * resolves it would otherwise be swallowed with no transcript entry.
 */
function openLineReader(stdout, transcript) {
  const reader = createInterface({ input: stdout, crlfDelay: Infinity });
  const lines = reader[Symbol.asyncIterator]();
  let pending = null;
  let firstFailure = null;
  let ended = false;
  // Replaced when the tap announces itself, which is the first instant after the reset
  // pulse — so an elapsed time measured from here is the board's, not the process's.
  let resetAt = Date.now();

  const next = async (deadline) => {
    for (;;) {
      const remaining = deadline - Date.now();
      if (remaining <= 0) return null;
      if (!pending) pending = lines.next();
      let timer;
      const expiry = new Promise((resolve) => {
        timer = setTimeout(() => resolve(TIMED_OUT), remaining);
      });
      const result = await Promise.race([pending, expiry]);
      clearTimeout(timer);
      if (result === TIMED_OUT) return null;
      pending = null;
      if (result.done) {
        ended = true;
        return null;
      }
      if (result.value === TAP_READY_MARKER) {
        resetAt = Date.now();
        continue;
      }
      transcript.push(result.value);
      const hit = FAILURE_PATTERNS.find((pattern) => pattern.test(result.value));
      // Remember the first hit: it is the cause, and what follows is usually its wreckage.
      if (hit && !firstFailure) firstFailure = { reason: hit.name, line: result.value };
      return result.value;
    }
  };

  return {
    next,
    get failure() { return firstFailure; },
    get ended() { return ended; },
    get resetAt() { return resetAt; },
    close: () => reader.close(),
  };
}

/** Drain the tap's stderr into a string, for use as evidence after it has closed. */
function collectStderr(child) {
  const chunks = [];
  child.stderr.on('data', (chunk) => chunks.push(chunk));
  return () => Buffer.concat(chunks).toString('utf8').trim();
}

/**
 * Reset the board, wait for `marker`, then require `settleMs` in which nothing panicked
 * and nothing reset.
 *
 * Returns the measured reset-to-marker time in milliseconds. That is the board's boot, and
 * it is the number a caller should report: before ADR-0015 this function ran until the tap
 * expired and its caller printed the tap's lifetime under a device-sounding label.
 */
export async function captureUntil({
  python, port, baud, marker, timeoutMs, settleMs = 0, transcript,
}) {
  const child = spawnTap({ python, port, baud, budgetMs: timeoutMs + settleMs });
  const readStderr = collectStderr(child);
  const reader = openLineReader(child.stdout, transcript);

  let markerMs = null;
  let failure = null;
  let deadline = Date.now() + timeoutMs;
  let status = null;
  try {
    for (;;) {
      const line = await reader.next(deadline);
      if (line === null) break;
      // Read `reader.failure` per line so the first failure of *either* kind wins: a panic
      // and a reset are both causes, and whichever came first is the one worth reporting.
      if (!failure && reader.failure) failure = reader.failure;
      // A reset is normal before the marker — the harness causes one — but after it, it
      // means the device came up, fell over and started again. That is the shape a boot
      // loop has, and it is the failure this settle window exists to catch.
      if (markerMs !== null && !failure && RESET_PATTERN.test(line)) {
        failure = { reason: 'reset after boot', line };
      }
      if (markerMs === null && line.includes(marker)) {
        markerMs = Date.now() - reader.resetAt;
        // Keep reading to the end of the settle window: the crash this gate first caught
        // came *after* the marker, so the marker is the start of the assertion, not the end.
        deadline = Date.now() + settleMs;
      }
    }
  } finally {
    child.kill('SIGTERM');
    status = await new Promise((resolve) => child.on('close', resolve));
    reader.close();
  }

  const stderr = readStderr();
  // SIGTERM leaves a null status, so only an exit the tap reached on its own says the
  // reader itself broke rather than having been stopped here.
  if (reader.ended && status !== 0 && status !== null && stderr) {
    throw new VerifyFailure(`serial tap failed: ${stderr}`);
  }
  if (failure) {
    throw new VerifyFailure(`${failure.reason} on serial: ${failure.line}`);
  }
  if (markerMs === null) {
    throw new VerifyFailure(
      `marker ${JSON.stringify(marker)} did not appear within ${timeoutMs} ms ` +
      `(${transcript.length} line(s) captured)`);
  }
  if (reader.ended) {
    throw new VerifyFailure(
      `the serial tap closed before the ${settleMs} ms settle window ended — the reader ` +
      'stopped, which is a broken harness and not a quiet device');
  }
  return markerMs;
}

/**
 * Reset the board, wait for `readyMarker`, and return a session over the serial control
 * channel: `send` a command line, `waitFor` a line a predicate accepts, as often as the caller
 * needs. It exists for gates whose next command depends on the last reply — finding a case
 * whose place in the list the card decides means pressing down until `state` names it
 * (ADR-0020). A fixed script is `driveDevice`, below, which is built on this.
 *
 * `budgetMs` is the most the caller will spend after boot; it sizes the tap's lifetime and is
 * never itself an assertion. Every wait is its own timer (ADR-0015). Failure patterns are
 * checked on every line throughout, so a panic between two waits fails the run rather than
 * being read as a missing reply. The caller must `close()` the session, in a `finally`.
 *
 * Requires firmware that reads the port (ADR-0014). Before that existed the device was
 * write-only over serial and a gate could only assert what it volunteered (ADR-0009).
 *
 * `reset: false` attaches to a board that is already running, for a boot the harness must not
 * cause: one after a power cycle, or one M5Launcher started. There is then no boot to wait
 * for, so `readyMarker` must be null, and a reset banner on the wire fails the session: it
 * means the attach itself reset the board, and what follows is a boot the caller did not ask
 * to observe. `resetAt` is then the moment of attaching, so it must not be reported as the
 * board's time (ADR-0015).
 *
 * The tap opens such a port exactly as it opens any other, with DTR and RTS asserted. Releasing
 * them was tried and is what reset the board: Linux raises both on open and pyserial lowers DTR
 * before RTS, which passes through the USB-Serial-JTAG's reset state. Measured 2026-10-25: the
 * released open gave `rst:0x15 (USB_UART_CHIP_RESET)`, the default open answered `probe` twice
 * with no banner.
 */
export async function openDeviceSession({
  python, port, baud, readyMarker, readyTimeoutMs, budgetMs, transcript, reset = true,
}) {
  if (reset === (readyMarker === null)) {
    throw new Error(reset
      ? 'a session that resets the board needs a readyMarker to wait for'
      : 'a session that attaches without a reset has no boot to wait for; pass readyMarker null');
  }
  const child = spawnTap(
    { python, port, baud, budgetMs: readyTimeoutMs + budgetMs, sendStdin: true, reset });
  const readStderr = collectStderr(child);
  const reader = openLineReader(child.stdout, transcript);

  const waitFor = async (predicate, timeoutMs, what) => {
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      const line = await reader.next(deadline);
      if (reader.failure) {
        throw new VerifyFailure(`${reader.failure.reason} on serial: ${reader.failure.line}`);
      }
      if (line === null && reader.ended) {
        // The tap outlived its budget or died: a broken harness, not a quiet device.
        throw new VerifyFailure(
          `the serial tap closed while waiting for ${what} ` +
          `(${transcript.length} line(s) captured)`);
      }
      if (line === null) {
        throw new VerifyFailure(
          `${what} did not appear within ${timeoutMs} ms ` +
          `(${transcript.length} line(s) captured)`);
      }
      if (!reset && RESET_PATTERN.test(line)) {
        throw new VerifyFailure(`the board reset after an attach that must not reset it: ${line}`);
      }
      if (predicate(line)) return line;
    }
  };

  let closed = false;
  const close = async () => {
    if (closed) return;
    closed = true;
    child.stdin.end();
    child.kill('SIGTERM');
    await new Promise((resolve) => child.on('close', resolve));
    reader.close();
    const stderr = readStderr();
    // SIGTERM is how a healthy tap is stopped here, so stderr is recorded as evidence
    // rather than treated as a failure on its own.
    if (stderr) transcript.push(`<tap-stderr> ${stderr}`);
  };

  if (!reset) {
    // A board that was already running may hold half a line in its command reader -- one
    // attach got `[CMD] err unknown` for a `probe` that arrived appended to bytes no newline had
    // ended. A blank line closes whatever is there: the reader takes it as no command at all,
    // and a fragment becomes one `[CMD] err` line the caller's wait skips. A reset needs none of
    // this, because the reader it boots is empty (ADR-0014).
    child.stdin.write('\n');
  }
  if (reset) {
    try {
      await waitFor((line) => line.includes(readyMarker), readyTimeoutMs,
                    `boot marker ${JSON.stringify(readyMarker)}`);
    } catch (error) {
      await close();
      throw error;
    }
  }

  return {
    send: (command) => { child.stdin.write(`${command}\n`); },
    waitFor,
    close,
    // Measured from the reset, not from this process starting, so an elapsed figure is
    // comparable with the boot gate's rather than carrying the tap's startup (ADR-0015).
    get resetAt() { return reader.resetAt; },
  };
}

/**
 * Reset the board, wait for `readyMarker`, then run a fixed list of steps.
 *
 * Returns the satisfied steps and the reset-to-last-step time in milliseconds.
 *
 * Each step sends one command line and waits for a reply its `expect` predicate accepts.
 */
export async function driveDevice({
  python, port, baud, readyMarker, readyTimeoutMs, steps, transcript,
}) {
  const budgetMs = steps.reduce((total, step) => total + step.timeoutMs, 0);
  const session = await openDeviceSession(
    { python, port, baud, readyMarker, readyTimeoutMs, budgetMs, transcript });

  const observed = [];
  let elapsedMs = null;
  try {
    for (const step of steps) {
      session.send(step.send);
      const line = await session.waitFor(
        step.expect, step.timeoutMs,
        `${step.label} (after sending ${JSON.stringify(step.send)})`);
      observed.push({ send: step.send, label: step.label, line });
    }
    elapsedMs = Date.now() - session.resetAt;
  } finally {
    await session.close();
  }

  return { observed, elapsedMs };
}

// The case menu's delete confirmation, as `state` reports it through `notice=` (ADR-0020). It
// names the case it is about to remove, and the name is fitted to what the fixed words leave on
// the notice line, so a long one arrives shortened and marked (ADR-0013). Two gates read this
// question, and both matched a fixed string until the question grew a name — which turned an
// input-gate change into a baseline-gate failure. It lives here so there is one reader.
const DELETE_PROMPT_PREFIX = 'Del ';
const DELETE_PROMPT_SUFFIX = '? ENTER=yes/any=no';
const FIT_MARKER = '...';

/**
 * @brief Does @p notice ask to delete @p caseName?
 *
 * The name's ceiling is deliberately not restated here: a second copy of it would drift from
 * the one the firmware derives. What is checked is the relation — the fixed words around either
 * the name whole, or an opening of it carrying the shortening marker.
 */
export function asksToDelete(notice, caseName) {
  if (!notice.startsWith(DELETE_PROMPT_PREFIX) || !notice.endsWith(DELETE_PROMPT_SUFFIX)) {
    return false;
  }
  const shown = notice.slice(DELETE_PROMPT_PREFIX.length,
                             notice.length - DELETE_PROMPT_SUFFIX.length);
  if (shown === caseName) return true;
  if (!shown.endsWith(FIT_MARKER) || shown.length <= FIT_MARKER.length) return false;
  return caseName.startsWith(shown.slice(0, -FIT_MARKER.length));
}

/** Where every run's directory is made; git-ignored (ADR-0009). */
const ARTIFACT_ROOT = 'verify-artifacts';

/**
 * What a gate or env name may hold to become a path segment. The env comes from DUPIN_ENV, so a
 * pasted path would otherwise place a run's evidence outside the artifact tree, silently.
 */
const PLAIN_NAME = /^[A-Za-z0-9_-]+$/;

function writeLines(path, lines) {
  writeFileSync(path, lines.join('\n') + '\n', 'utf8');
}

/**
 * Run @p body and keep its evidence in a directory of its own (ADR-0021):
 * `<root>/<gate>-<environment>/<UTC start>-<PASS|FAIL|ERROR>/`, holding `summary.txt` always,
 * and `serial.log` and `build.log` when the run produced them.
 *
 * The directory is named only once the outcome is known, and made exclusively, so no run can
 * rewrite another's transcript or leave its files beside another's summary — the three ways
 * fixed names lost or misrepresented a failure. @p body receives the logs to fill and returns
 * the summary lines of a pass. A `VerifyFailure` is a FAIL and resolves; anything else thrown
 * is the gate breaking, recorded as ERROR and rethrown, because a harness bug and a device
 * defect need different people looking at them.
 */
export async function recordRun(
  { root = ARTIFACT_ROOT, gate, environment, startedAt = Date.now(), report = console }, body,
) {
  for (const [label, name] of [['gate', gate], ['environment', environment]]) {
    if (!PLAIN_NAME.test(name ?? '')) {
      throw new VerifyFailure(`${label} ${JSON.stringify(name)} is not a plain name ` +
                              `(${PLAIN_NAME.source}), so it cannot name an artifact directory`);
    }
  }
  const buildLog = [];
  const transcript = [];
  let outcome = 'PASS';
  let details;
  let thrown;
  try {
    details = await body({ buildLog, transcript, startedAt });
  } catch (error) {
    thrown = error;
    outcome = error instanceof VerifyFailure ? 'FAIL' : 'ERROR';
    details = [
      `reason:             ${error instanceof Error ? error.message : String(error)}`,
      `environment:        ${environment}`,
      `serial lines:       ${transcript.length}`,
      `total:              ${Date.now() - startedAt} ms`,
    ];
  }

  // Reported before anything is written: if the write fails too, its error still stops the run,
  // but the operator has already been told what the run itself found.
  const summary = [`result:             ${outcome}`, ...details];
  if (outcome === 'PASS') {
    report.log(summary.join('\n'));
  } else {
    report.error(`${gate}: ${outcome} — ${details[0].replace(/^reason: +/, '')}`);
  }

  // Colons are not portable in a file name, and the stamp must still sort as time does.
  const stamp = new Date(startedAt).toISOString().replaceAll(':', '-');
  const directory = join(root, `${gate}-${environment}`, `${stamp}-${outcome}`);
  mkdirSync(dirname(directory), { recursive: true });
  // Not recursive: an existing directory means two runs claimed one name, and merging into it
  // is exactly the mixing this layout exists to prevent.
  mkdirSync(directory);
  writeLines(join(directory, 'summary.txt'), summary);
  // An empty transcript from a run that never reached the device would read as a device that
  // said nothing, so a log is written only when there is something in it.
  if (transcript.length > 0) writeLines(join(directory, 'serial.log'), transcript);
  if (buildLog.length > 0) writeLines(join(directory, 'build.log'), buildLog);
  (outcome === 'PASS' ? report.log : report.error)(`${gate}: artifacts in ${directory}/`);

  if (outcome === 'ERROR') throw thrown;
  return { outcome, directory };
}
