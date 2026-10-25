/**
 * Host tests for how a verify run records its artifacts (ADR-0021), and for the session options
 * a device run is refused on before any port is opened (ADR-0032).
 *
 * The device half of the harness cannot run here, but where a run's evidence lands can, and
 * that is what lost the only transcript of a failure: fixed file names, rewritten by the
 * next run. Run with `make test-scripts`.
 */
import assert from 'node:assert/strict';
import {
  existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterEach, beforeEach, test } from 'node:test';
import { VerifyFailure, openDeviceSession, recordRun } from './verify-harness.mjs';

const FIRST_START = Date.parse('2026-09-15T10:22:03.418Z');
const SECOND_START = FIRST_START + 30_000;
const QUIET = { log() {}, error() {} };

let root;
beforeEach(() => { root = mkdtempSync(join(tmpdir(), 'verify-artifacts-')); });
afterEach(() => { rmSync(root, { recursive: true, force: true }); });

const runOf = (overrides) => ({
  root, gate: 'input-verify', environment: 'cardputer', startedAt: FIRST_START, report: QUIET,
  ...overrides,
});
const read = (...parts) => readFileSync(join(...parts), 'utf8');
const runsOf = (gateDirectory) => readdirSync(join(root, gateDirectory)).sort();

const passing = async ({ transcript }) => {
  transcript.push('[BOOT] Initialization complete');
  return ['steps: 1/1 satisfied'];
};

test('a second run of the same gate leaves the first run whole', async () => {
  const failed = await recordRun(runOf({}), async ({ transcript }) => {
    transcript.push('[CMD] key a');
    throw new VerifyFailure('no state reply within 5000 ms');
  });
  await recordRun(runOf({ startedAt: SECOND_START }), passing);

  assert.deepEqual(runsOf('input-verify-cardputer'),
                   ['2026-09-15T10-22-03.418Z-FAIL', '2026-09-15T10-22-33.418Z-PASS']);
  assert.equal(read(failed.directory, 'serial.log'), '[CMD] key a\n');
});

test('the same gate on two boards writes to two places', async () => {
  await recordRun(runOf({ gate: 'boot-verify', environment: 'cardputer' }), passing);
  await recordRun(runOf({ gate: 'boot-verify', environment: 'cardputer_adv' }), passing);

  assert.deepEqual(runsOf('boot-verify-cardputer'), ['2026-09-15T10-22-03.418Z-PASS']);
  assert.deepEqual(runsOf('boot-verify-cardputer_adv'), ['2026-09-15T10-22-03.418Z-PASS']);
});

test('a failed run says it failed, and why, in its own summary', async () => {
  const run = await recordRun(runOf({}), async () => {
    throw new VerifyFailure('no state reply within 5000 ms');
  });

  assert.equal(run.outcome, 'FAIL');
  const summary = read(run.directory, 'summary.txt');
  assert.match(summary, /^result: +FAIL$/m);
  assert.match(summary, /^reason: +no state reply within 5000 ms$/m);
  assert.doesNotMatch(summary, /PASS/);
});

test('a passing run records PASS ahead of what the gate measured', async () => {
  const run = await recordRun(runOf({}), passing);

  assert.equal(run.outcome, 'PASS');
  const [resultLine, firstMeasured] = read(run.directory, 'summary.txt').split('\n');
  assert.match(resultLine, /^result: +PASS$/);
  assert.equal(firstMeasured, 'steps: 1/1 satisfied');
  assert.equal(read(run.directory, 'serial.log'), '[BOOT] Initialization complete\n');
});

test('a gate that breaks is recorded as ERROR, not as a device failure, and still throws',
     async () => {
  const broken = new TypeError('cannot read properties of undefined');
  await assert.rejects(recordRun(runOf({}), async () => { throw broken; }),
                       (error) => error === broken);

  const [directory] = runsOf('input-verify-cardputer');
  assert.equal(directory, '2026-09-15T10-22-03.418Z-ERROR');
  const summary = read(root, 'input-verify-cardputer', directory, 'summary.txt');
  assert.match(summary, /^result: +ERROR$/m);
});

test('a run that never reached the device writes no transcript to mistake for a silent one',
     async () => {
  const run = await recordRun(runOf({}), async ({ buildLog }) => {
    buildLog.push('*** [upload] Error 2');
    throw new VerifyFailure('pio run exited 1');
  });

  assert.equal(existsSync(join(run.directory, 'serial.log')), false);
  assert.equal(read(run.directory, 'build.log'), '*** [upload] Error 2\n');
});

test('a run that cannot write its artifacts has still said why it failed', async () => {
  const notADirectory = join(root, 'verify-artifacts');
  writeFileSync(notADirectory, '');
  const errors = [];
  const report = { log() {}, error: (line) => errors.push(line) };

  await assert.rejects(
    recordRun(runOf({ root: notADirectory, report }), async () => {
      throw new VerifyFailure('no ready marker within 25000 ms');
    }),
    { code: 'ENOTDIR' });
  assert.ok(errors.some((line) => line.includes('no ready marker within 25000 ms')),
            `the reason never reached the operator: ${JSON.stringify(errors)}`);
});

test('an environment that is not a plain name is refused before the run starts', async () => {
  let ran = false;
  await assert.rejects(
    recordRun(runOf({ environment: 'cardputer/../../elsewhere' }), async () => {
      ran = true;
      return [];
    }),
    VerifyFailure);

  assert.equal(ran, false);
  assert.deepEqual(readdirSync(root), []);
});

test('a run never writes into a directory another run made', async () => {
  const first = await recordRun(runOf({}), passing);

  await assert.rejects(recordRun(runOf({}), passing), { code: 'EEXIST' });
  assert.deepEqual(readdirSync(first.directory).sort(), ['serial.log', 'summary.txt']);
});

// Never spawned when the options are refused; if they were not, the spawn fails with a message
// neither test below accepts, so a missing refusal cannot pass for one.
const sessionOf = (overrides) => ({
  python: 'no-such-python', port: 'no-such-port', baud: 115200,
  readyTimeoutMs: 1_000, budgetMs: 1_000, transcript: [], ...overrides,
});

test('a session that resets the board is refused without a boot marker to wait for', async () => {
  await assert.rejects(openDeviceSession(sessionOf({ reset: true, readyMarker: null })),
                       /needs a readyMarker/);
});

test('a session that attaches without a reset is refused a boot marker it would never see',
     async () => {
  await assert.rejects(
    openDeviceSession(sessionOf({ reset: false, readyMarker: '[BOOT] Initialization complete' })),
    /no boot to wait for/);
});
