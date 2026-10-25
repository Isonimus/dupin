#!/usr/bin/env node
/**
 * Device verification: an injected key reaches the firmware's real dispatch and has its
 * documented effect.
 *
 * This is the gap CLAUDE.md §3 is built around. The tree where every board guard was
 * misspelled `DDUPIN_*` compiled clean and booted with no working input at all, and nothing
 * mechanical would have caught it: `make build` was green, `make test` cannot reach
 * main.cpp, and `make verify-boot` only asserts that the firmware came up. This asserts
 * that pressing a key does something.
 *
 * What it proves, precisely: that the dispatch chains run and change observable state. It
 * does **not** prove the physical keys work — an injected key enters at the dispatch and
 * bypasses M5Cardputer.Keyboard entirely, so the keyboard hardware itself is untestable
 * unattended and is not claimed here (ADR-0014).
 *
 * The sequence is deliberately ordered so each step proves something the previous one could
 * not. `state` while the modal boot menu holds the foreground proves the channel is served
 * from inside setup(); `key del` then `state` proves an injected key reached the case-menu
 * dispatch, because nothing else can clear that loop; `key w` then `key m` proves the main
 * dispatch runs and that the reported mode tracks the key rather than being a fixed string.
 *
 * Before leaving the menu it also asserts a refusal reaches the operator (ADR-0019): enter on
 * an empty name must leave the editor open with `notice=` naming the rule, a typed character
 * must clear it, and del must delete that character rather than cancel — proved by a second
 * empty-name refusal, which only an editor still open can produce. Every name it types is
 * refused or deleted, so the gate never creates a case on the card.
 *
 * It then asserts a delete waits for confirmation (ADR-0020): `x` on an existing case must
 * put the question in `notice=`, and a key other than Enter must withdraw it without moving
 * the selection. Enter is never sent while a delete is armed, so the gate never deletes a
 * case either — which is also why it needs one on the card, and says so when there is none.
 *
 * Usage: make verify-input   (DUPIN_PORT overrides the auto-detected serial port)
 */
import {
  VerifyFailure, asksToDelete, driveDevice, recordRun, resolveBaud, resolvePort, resolvePython,
  runPio,
} from './verify-harness.mjs';

const ENVIRONMENT = process.env.DUPIN_ENV ?? 'cardputer';
// The same marker make verify-boot uses: the last line setup() prints on every path
// (ADR-0010). After it, with an SD card present, setup() sits in the modal case menu.
const READY_MARKER = '[BOOT] Initialization complete';
// Longer than verify-boot's 25 s: that one measured 25.2 s to the marker on this board, so
// its budget has no headroom, and a gate that fails on a slow boot reports the wrong defect.
const BOOT_TIMEOUT_MS = 40_000;
// Each reply follows within one loop pass (~10 ms) or one modal-loop pass (50 ms). Seconds
// of budget is not optimism, it is room for a device that is busy rather than broken.
const STEP_TIMEOUT_MS = 5_000;
const selectionOf = (line) => /\bsel=(\S+)/.exec(line)?.[1] ?? null;
// Set by the step that selects a case, read by the one that checks the selection held.
let selectedBeforeDelete = null;

const STEPS = [
  {
    send: 'state',
    label: 'state report showing the modal case menu has the foreground',
    // menu=yes is the proof that setup()'s own loop is pumping the channel. Before this
    // existed the firmware was unreachable over serial from boot on any device with a card.
    // notice=- is the baseline the refusal below is compared against: without it, a
    // firmware that printed the notice unconditionally would pass the refusal step.
    expect: (line) => line.startsWith('[STATE]') && line.includes('menu=yes')
      && line.endsWith(' notice=-'),
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key enter',
    label: 'echo of enter on [+] New Case, opening the name editor',
    expect: (line) => line === '[CMD] key 0xD',
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key enter',
    label: 'echo of enter confirming an empty name',
    expect: (line) => line === '[CMD] key 0xD',
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'state',
    label: 'state report carrying the refusal the panel shows',
    // The case menu used to close the editor and clear the buffer here with nothing shown.
    // menu=yes alongside the notice is the refusal staying on screen rather than being
    // reported on the way out (ADR-0019).
    expect: (line) => line.startsWith('[STATE]') && line.includes('menu=yes')
      && line.endsWith(' notice=Name cannot be empty'),
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key a',
    label: 'echo of a name character typed into the editor',
    expect: (line) => line === '[CMD] key a',
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'state',
    label: 'state report showing the refusal cleared once the operator acted on it',
    expect: (line) => line.startsWith('[STATE]') && line.endsWith(' notice=-'),
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key del',
    label: 'echo of del with one character in the editor',
    expect: (line) => line === '[CMD] key 0x1B',
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key enter',
    label: 'echo of enter after the del',
    expect: (line) => line === '[CMD] key 0xD',
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'state',
    label: 'state report proving del deleted one character rather than cancelling',
    // The discriminating step for the backspace fix. Had del cancelled, enter would have
    // landed on the list and reopened an empty editor, leaving notice=-. Only an editor
    // that is still open, with its one character removed, refuses an empty name here.
    expect: (line) => line.startsWith('[STATE]') && line.includes('menu=yes')
      && line.endsWith(' notice=Name cannot be empty'),
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key del',
    label: 'echo of del on an empty editor, which cancels it',
    expect: (line) => line === '[CMD] key 0x1B',
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'state',
    label: 'state report showing cancel cleared the refusal with the editor',
    expect: (line) => line.startsWith('[STATE]') && line.includes('menu=yes')
      && line.endsWith(' notice=-'),
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key l',
    label: 'echo of down, moving the selection off [+] New Case',
    expect: (line) => line === '[CMD] key l',
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'state',
    label: 'state report with an existing case selected (the card needs at least one case)',
    // `x` on [+] New Case does nothing, so the delete below needs a case under the selection.
    // The name is kept to prove the withdrawing key did not also move it.
    expect: (line) => {
      if (!line.startsWith('[STATE]') || !line.includes('menu=yes')) return false;
      const selected = selectionOf(line);
      // Down from [+] New Case lands back on it only when the list holds nothing else. Said
      // outright, because a timeout here would read like a dispatch that did not run.
      if (selected === '+') {
        throw new VerifyFailure(
          'the card holds no case, and the delete confirmation needs one to arm against — ' +
          'create any case on the card and run the gate again');
      }
      if (selected === null || selected === '-') return false;
      selectedBeforeDelete = selected;
      return true;
    },
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key x',
    label: 'echo of x asking to delete the selected case',
    expect: (line) => line === '[CMD] key x',
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'state',
    label: 'state report carrying the delete question, naming the selected case',
    expect: (line) => line.startsWith('[STATE]') && line.includes('menu=yes')
      && asksToDelete(line.slice(line.indexOf(' notice=') + ' notice='.length),
                      selectedBeforeDelete),
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key l',
    label: 'echo of down while the delete is armed',
    expect: (line) => line === '[CMD] key l',
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'state',
    label: 'state report showing the other key withdrew the delete and moved nothing',
    // Both halves discriminate. A key that withdrew the delete and also navigated would leave
    // sel naming another row -- the list always has at least two, [+] New Case and this case
    // -- and one that did not withdraw it would leave the question up (ADR-0020).
    expect: (line) => line.startsWith('[STATE]') && line.includes('menu=yes')
      && selectionOf(line) === selectedBeforeDelete && line.endsWith(' notice=-'),
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key del',
    label: 'echo of the injected del key',
    expect: (line) => line.startsWith('[CMD] key 0x1B'),
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'state',
    label: 'state report showing the case menu was left',
    // The assertion that carries this gate: only the case-menu dispatch clears that modal
    // loop, so menu=no means the injected key arrived there and acted.
    expect: (line) => line.startsWith('[STATE]') && line.includes('menu=no'),
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key w',
    label: 'Watson selected by the main dispatch',
    expect: (line) => line === '[KEY] Watson selected',
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'state',
    label: 'state report showing Watson active',
    expect: (line) => line.startsWith('[STATE]') && line.includes('mode=Watson'),
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'key m',
    label: 'Marple selected by the main dispatch',
    expect: (line) => line === '[KEY] Marple selected',
    timeoutMs: STEP_TIMEOUT_MS,
  },
  {
    send: 'state',
    label: 'state report showing Marple active',
    // A second, different mode. Without it the previous step would also pass on firmware
    // that reported a constant mode=Watson.
    expect: (line) => line.startsWith('[STATE]') && line.includes('mode=Marple'),
    timeoutMs: STEP_TIMEOUT_MS,
  },
];

async function main() {
  const python = resolvePython();
  const port = resolvePort();
  const baud = resolveBaud();
  console.log(`input-verify: env=${ENVIRONMENT} port=${port} baud=${baud} python=${python}`);

  const recording = { gate: 'input-verify', environment: ENVIRONMENT };
  const { outcome } = await recordRun(recording, async ({ buildLog, transcript, startedAt }) => {
    console.log('input-verify: building and uploading…');
    await runPio(['run', '-e', ENVIRONMENT, '-t', 'upload'], buildLog);

    console.log(`input-verify: resetting, then driving ${STEPS.length} steps over the ` +
                'serial control channel…');
    // The elapsed time comes back from the drive, measured on the device's side of the
    // reset, so it is the board's span and not this process's (ADR-0015).
    const { observed, elapsedMs } = await driveDevice({
      python, port, baud,
      readyMarker: READY_MARKER, readyTimeoutMs: BOOT_TIMEOUT_MS,
      steps: STEPS, transcript,
    });

    return [
      `environment:        ${ENVIRONMENT}`,
      `port:               ${port} @ ${baud}`,
      `marker:             ${READY_MARKER}`,
      `steps:              ${observed.length}/${STEPS.length} satisfied`,
      `reset-to-last-step: ${elapsedMs} ms`,
      `serial lines:       ${transcript.length}`,
      '',
      'observed:',
      ...observed.map(({ send, line }) => `  ${send.padEnd(10)} -> ${line}`),
      '',
      `total:              ${Date.now() - startedAt} ms`,
    ];
  });
  if (outcome === 'FAIL') process.exitCode = 1;
}

await main();
