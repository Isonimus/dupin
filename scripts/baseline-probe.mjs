#!/usr/bin/env node
/**
 * Probe: does a case own its baseline?
 *
 * ADR-0003 records that `SessionManager::saveBaseline`/`loadBaseline` are device-verified
 * only, and `LEDGER.md` proposes a round-trip gate over them. Reading the tree first turned
 * up a prior question that gate cannot answer for itself. `SessionManager::loadBaseline` has
 * no production caller, and `loadSession()` switches the current case without touching the
 * baseline at all. What `[STATE] ... ssids=` reports is `WatsonMode`'s in-memory baseline,
 * restored by `WatsonMode::begin()` from NVS — one global store, capped at 8 SSIDs, with no
 * knowledge of cases.
 *
 * So a round-trip gate written against `ssids=` today would pass while proving the NVS path
 * under a label naming the SD one — the failure ADR-0015 has just finished removing from the
 * boot gate. This probe measures the fact that decides which store should own persistence:
 * whether opening a different case changes the baseline the modes actually score against.
 *
 * It answers once and the numbers land in an ADR, so it is deliberately not a `make` target
 * (CLAUDE.md §3). It drives only the existing `state`/`key` vocabulary (ADR-0014) and
 * asserts nothing about the answer — a probe that presumed its result would not be a
 * measurement.
 *
 * Usage: node scripts/baseline-probe.mjs   (DUPIN_PORT overrides the auto-detected port)
 */
import {
  VerifyFailure, driveDevice, recordRun, resolveBaud, resolvePort, resolvePython, runPio,
} from './verify-harness.mjs';

const ENVIRONMENT = process.env.DUPIN_ENV ?? 'cardputer';
const READY_MARKER = '[BOOT] Initialization complete';
// Matches verify-input rather than verify-boot: the same 25 s budget has no headroom on this
// board, and a probe that fails on a slow boot measures the wrong thing (ADR-0014).
const BOOT_TIMEOUT_MS = 40_000;
const STEP_TIMEOUT_MS = 5_000;

// `l` is the case menu's down key and `\r` its select; both are echoed by the command
// dispatch before they are acted on, which is what each intermediate step waits for.
const ENTER_ECHO = '[CMD] key 0xD';

const STEPS = [
  {
    send: 'state', probe: 'boot',
    label: 'boot state, with the modal case menu in the foreground',
    expect: (line) => line.startsWith('[STATE]') && line.includes('menu=yes'),
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key l', label: 'selection moved off "New Case" onto the first existing case',
    expect: (line) => line === '[CMD] key l', timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key enter', label: 'first case opened',
    expect: (line) => line === ENTER_ECHO, timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'state', probe: 'first-case-opened',
    label: 'state with the first case open, before any mode has started',
    expect: (line) => line.startsWith('[STATE]') && line.includes('menu=no'),
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key w', probe: null,
    // begin() is the only thing that restores a baseline, so the count cannot be read
    // before a mode has started -- this step is what makes the next one meaningful.
    label: 'Watson started, which is what runs WatsonMode::loadBaseline()',
    expect: (line) => line === '[KEY] Watson selected', timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'state', probe: 'first-case-watson',
    label: 'state after Watson restored a baseline under the first case',
    expect: (line) => line.startsWith('[STATE]') && line.includes('mode=Watson'),
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key c', label: 'case menu reopened from the running firmware',
    expect: (line) => line === '[KEY] Case Menu', timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key l', label: 'selection moved to the second existing case',
    expect: (line) => line === '[CMD] key l', timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key enter', label: 'second case opened',
    expect: (line) => line === ENTER_ECHO, timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'state', probe: 'second-case-opened',
    // The measurement. If ssids= is unchanged across a case switch, the baseline the modes
    // score against is not the case's.
    label: 'state after switching to the second case',
    expect: (line) => line.startsWith('[STATE]') && line.includes('menu=no'),
    timeoutMs: STEP_TIMEOUT_MS,
  },
];

/** Split a `[STATE] mode=… running=… menu=… case=… ssids=…` report into its fields. */
function parseState(line) {
  const field = (name) => {
    const found = new RegExp(`\\b${name}=(\\S+)`).exec(line);
    if (found === null) {
      throw new VerifyFailure(`state report has no ${name}= field: ${line}`);
    }
    return found[1];
  };
  return {
    mode: field('mode'), menu: field('menu'), name: field('case'), ssids: field('ssids'),
  };
}

async function main() {
  const python = resolvePython();
  const port = resolvePort();
  const baud = resolveBaud();
  console.log(`baseline-probe: env=${ENVIRONMENT} port=${port} baud=${baud}`);

  const recording = { gate: 'baseline-probe', environment: ENVIRONMENT };
  const { outcome } = await recordRun(recording, async ({ buildLog, transcript }) => {
    console.log('baseline-probe: building and uploading…');
    await runPio(['run', '-e', ENVIRONMENT, '-t', 'upload'], buildLog);

    console.log('baseline-probe: opening one case, starting Watson, then switching case…');
    const { observed } = await driveDevice({
      python, port, baud,
      readyMarker: READY_MARKER, readyTimeoutMs: BOOT_TIMEOUT_MS,
      steps: STEPS, transcript,
    });

    const states = new Map();
    STEPS.forEach((step, index) => {
      if (step.probe) states.set(step.probe, parseState(observed[index].line));
    });

    const row = (key) => {
      const state = states.get(key);
      return `  ${key.padEnd(20)} case=${state.name.padEnd(12)} ` +
             `mode=${state.mode.padEnd(8)} ssids=${state.ssids}`;
    };
    const first = states.get('first-case-watson');
    const second = states.get('second-case-opened');
    return [
      `environment:      ${ENVIRONMENT}`,
      `port:             ${port} @ ${baud}`,
      '',
      'observed state at each point:',
      row('boot'),
      row('first-case-opened'),
      row('first-case-watson'),
      row('second-case-opened'),
      '',
      `case switched:    ${first.name} -> ${second.name} ` +
      `(${first.name === second.name ? 'NO — same case, measurement is void' : 'yes'})`,
      `baseline count:   ${first.ssids} -> ${second.ssids} ` +
      `(${first.ssids === second.ssids ? 'unchanged' : 'changed'})`,
      '',
      'reference: WatsonMode::saveBaseline writes at most 8 SSIDs to NVS of the',
      '           DUPIN_MAX_SSIDS = 64 the in-memory baseline can hold, and at most',
      '           4 BSSIDs per SSID of DUPIN_MAX_BSSIDS_PER_SSID = 8.',
    ];
  });
  if (outcome === 'FAIL') process.exitCode = 1;
}

await main();
