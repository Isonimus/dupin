#!/usr/bin/env node
/**
 * Asserts that frames cross tasks through the queue, and that none go missing on the way.
 *
 * This started as the probe that measured the race: `onFrame` ran on the Wi-Fi task and
 * called straight into every mode, so Marple could raise an anomaly, which raised a toast,
 * which ran a full render and `pushSprite()` — on the Wi-Fi task, into the canvas `loop()`
 * draws. Its figures are what ADR-0024 was decided on, and the same figures are what catch
 * the defect coming back, so it is now the gate that decision names (invariant 26).
 *
 * Four things are asserted, and none of them is "a counter stayed 0":
 *
 *   1. Frames are conserved. With capture stopped the producer is gone and the stop path
 *      has drained, so `accepted == drained + dropped` exactly, with nothing left queued.
 *      A drop-count check would pass on firmware that captured nothing; this one fails the
 *      moment ingestion goes anywhere but into the modes (ADR-0016 decision 3, invariant 17).
 *   2. Ingestion is not vacuous. Frames were accepted, frames were drained, and the
 *      baseline came out non-empty — three readings that can disagree, the last of them
 *      taken from Watson rather than from the queue's own bookkeeping.
 *   3. Nothing renders off the loop task. `rendersOffLoopTask` and `renderCollisions` are
 *      what found the original defect and must now read 0 for good.
 *   4. The drop count reaches `state`, so an operator and a gate read the same figure. A
 *      run with no drops proves the field is reported, not that the drop path works — the
 *      air decides whether a queue this deep ever fills, and a command to fill it would be
 *      a capability the keyboard does not have (invariant 14).
 *
 * It writes only into a case it creates and deletes (invariant 22), and keeps its measured
 * figures in the run's artifacts, which is the half that needs eyes rather than assertions.
 *
 * Usage: node scripts/race-verify.mjs   (DUPIN_PORT overrides the auto-detected port)
 */
import {
  VerifyFailure, asksToDelete, openDeviceSession, recordRun, resolveBaud, resolvePort,
  resolvePython, runPio,
} from './verify-harness.mjs';

const ENVIRONMENT = process.env.DUPIN_ENV ?? 'cardputer';
const READY_MARKER = '[BOOT] Initialization complete';
// Matches the other device scripts: verify-boot's 25 s has no headroom on this board, and a
// run that fails on a slow boot measures the wrong thing (ADR-0014).
const BOOT_TIMEOUT_MS = 40_000;
const STEP_TIMEOUT_MS = 5_000;

// `baselineAutosaveInterval` defaults to 60 s (src/config/dupin_config.h), and the autosave
// is the save that runs *while* onFrame keeps inserting — the one a lock would have to be
// held across. The first window is sized to contain one, with margin for a slow card.
const AUTOSAVE_INTERVAL_MS = 60_000;
const FIRST_WINDOW_MS = AUTOSAVE_INTERVAL_MS + 20_000;
// The second window scores against the baseline the first one built, which is what an
// anomaly needs: Marple raises one on a deviation, and an empty baseline has nothing to
// deviate from. An anomaly is the only path that reaches the renderer from the Wi-Fi task,
// and whether one fires is a property of the air rather than of the firmware, so this
// window is an opportunity rather than an assertion.
const SECOND_WINDOW_MS = 45_000;

// The gate owns exactly one case, creates it empty and deletes it before exiting. Writing
// into an operator's case would be the defect ADR-0020 removed from the baseline gate.
const GATE_CASE = 'verify_race';
// refreshCaseList caches at most this many cases (ADR-0023); pressing down that many times
// plus one, for [+] New Case, has visited every row the menu can show.
const MAX_LISTED_CASES = 16;
const NEW_CASE_ROW = '+';

const ENTER = 'key enter';
const ENTER_ECHO = '[CMD] key 0xD';
const DOWN = 'key l';
const DOWN_ECHO = '[CMD] key l';
const STOP = 'key `';

// The tap's lifetime, never an assertion (ADR-0015): generous enough that no wait below can
// outlive it. A full search of the list is one state and one down per row.
const LIST_SEARCH_MS = (MAX_LISTED_CASES + 1) * 2 * STEP_TIMEOUT_MS;
const FIXED_COMMANDS = 60;
const BUDGET_MS = 4 * LIST_SEARCH_MS + FIXED_COMMANDS * STEP_TIMEOUT_MS
  + FIRST_WINDOW_MS + SECOND_WINDOW_MS;

const isState = (line) => line.startsWith('[STATE]');
const isProbe = (line) => line.startsWith('[PROBE]');
const openedCase = (line) =>
  /^\[BASELINE\] (Restored \d+ SSIDs|No stored baseline) for case \S+$/.test(line);

/** Split a `<tag> name=value …` report into its fields. Values never contain spaces here. */
function parseFields(line, names) {
  const fields = {};
  for (const name of names) {
    const found = new RegExp(`\\b${name}=(\\S+)`).exec(line);
    if (found === null) throw new VerifyFailure(`report has no ${name}= field: ${line}`);
    fields[name] = found[1];
  }
  return fields;
}

function parseState(line) {
  const fields = parseFields(line, ['menu', 'running', 'case', 'ssids', 'sel', 'dropped']);
  const notice = / notice=(.*)$/.exec(line);
  if (notice === null) throw new VerifyFailure(`state report has no notice= field: ${line}`);
  return {
    menu: fields.menu, running: fields.running, name: fields.case,
    ssids: Number(fields.ssids), dropped: Number(fields.dropped),
    selected: fields.sel, notice: notice[1],
  };
}

const PROBE_FIELDS = [
  'frameTask', 'loopTask', 'frameStackFreeBytes', 'frames', 'maxFramesPerPass',
  'maxPassUs', 'maxRenderUs', 'rendersOffLoopTask', 'renderCollisions', 'maxSaveUs',
  'accepted', 'drained', 'dropped', 'queued', 'queueDepth', 'slotBytes',
];

function parseProbe(line) {
  const fields = parseFields(line, PROBE_FIELDS);
  const numeric = {};
  for (const name of PROBE_FIELDS.slice(2)) numeric[name] = Number(fields[name]);
  return { frameTask: fields.frameTask, loopTask: fields.loopTask, ...numeric };
}

/** Send one command and wait for the line @p expect accepts. */
function command(session, send, expect, what, timeoutMs = STEP_TIMEOUT_MS) {
  session.send(send);
  return session.waitFor(expect, timeoutMs, `${what} (after sending ${JSON.stringify(send)})`);
}

async function readState(session, what) {
  return parseState(await command(session, 'state', isState, what));
}

async function readProbe(session, what) {
  return parseProbe(await command(session, 'probe', isProbe, what));
}

/**
 * Hold for @p durationMs while the tap keeps reading.
 *
 * A timer, not the arrival of a line (invariant 15): the board is near-silent between
 * discoveries, so waiting on output would end the window at whatever the air happened to
 * deliver rather than after the interval this needs to contain.
 */
function hold(durationMs) {
  return new Promise((resolve) => { setTimeout(resolve, durationMs); });
}

/** Press down until `sel=` names @p wanted, or every row the menu can list has been seen. */
async function selectRow(session, wanted) {
  for (let row = 0; row <= MAX_LISTED_CASES; row++) {
    const state = await readState(session, `state while looking for ${wanted}`);
    if (state.menu !== 'yes') {
      throw new VerifyFailure(`the case menu closed while looking for ${wanted}`);
    }
    if (state.selected === wanted) return true;
    await command(session, DOWN, (line) => line === DOWN_ECHO, 'echo of down');
  }
  return false;
}

async function createCase(session, name) {
  if (!(await selectRow(session, NEW_CASE_ROW))) {
    throw new VerifyFailure('the case menu has no [+] New Case row');
  }
  await command(session, ENTER, (line) => line === ENTER_ECHO, 'enter on [+] New Case');
  for (const character of name) {
    await command(session, `key ${character}`, (line) => line === `[CMD] key ${character}`,
                  `echo of ${JSON.stringify(character)} typed into the name`);
  }
  session.send(ENTER);
  session.send('state');
  const first = await session.waitFor((line) => openedCase(line) || isState(line),
                                      STEP_TIMEOUT_MS, `${name} created and opened`);
  if (isState(first)) {
    throw new VerifyFailure(`creating ${name} was refused: ${parseState(first).notice}`);
  }
  await session.waitFor(isState, STEP_TIMEOUT_MS, `state after creating ${name}`);
}

async function deleteCase(session, name) {
  if (!(await selectRow(session, name))) {
    throw new VerifyFailure(`${name} is not in the case list, so it cannot be deleted`);
  }
  await command(session, 'key x', (line) => line === '[CMD] key x', `echo of x on ${name}`);
  const armed = await readState(session, `state with the delete of ${name} armed`);
  if (!asksToDelete(armed.notice, name)) {
    throw new VerifyFailure(`x on ${name} did not ask to confirm: notice=${armed.notice}`);
  }
  const outcome = await command(
    session, ENTER,
    (line) => line === `[SD] Deleted case ${name}` || line.startsWith(`[CASE] Delete of ${name}`),
    `${name} deleted from the card`);
  if (!outcome.startsWith('[SD] Deleted case')) {
    throw new VerifyFailure(`deleting ${name} was refused: ${outcome}`);
  }
}

/** Count `[ALERT] Anomaly detected:` lines for the whole run, from the transcript. */
function countAnomalies(transcript) {
  return transcript.filter((line) => line.includes('[ALERT] Anomaly detected:')).length;
}

/**
 * Everything the run is allowed to fail on, in one place.
 *
 * Read after the last stop and never during capture: the counters have one writer each and
 * are read without a lock, so a reading taken while the Wi-Fi task is still posting is a
 * true figure of an instant that has already passed. With the producer gone and the stop
 * path drained, the identity below is exact rather than nearly so.
 */
function assertFrameConservation(probe, state) {
  if (probe.queued !== 0) {
    throw new VerifyFailure(
      `${probe.queued} frames were still queued after the stop drained the queue`);
  }
  const accountedFor = probe.drained + probe.dropped;
  if (accountedFor !== probe.accepted) {
    throw new VerifyFailure(
      `frames went missing: accepted=${probe.accepted} but drained=${probe.drained} + ` +
      `dropped=${probe.dropped} = ${accountedFor}`);
  }
  // Without this the identity above holds trivially on firmware that never captured a
  // frame -- 0 == 0 + 0 -- which is exactly the shape of gate ADR-0016 was written against.
  if (probe.accepted === 0) {
    throw new VerifyFailure('no frame was ever accepted, so conservation proves nothing');
  }
  if (probe.drained === 0) {
    throw new VerifyFailure(
      `all ${probe.accepted} accepted frames were dropped; none reached the modes`);
  }
  // A third reading, from Watson rather than from the queue's own bookkeeping: the two can
  // disagree, and if they do it is the drain that is lying (invariant 17).
  if (state.ssids === 0) {
    throw new VerifyFailure(
      `${probe.drained} frames were drained but the baseline is empty, so nothing reached ` +
      'the modes the drain hands them to');
  }
  if (state.dropped !== probe.dropped) {
    throw new VerifyFailure(
      `state reports dropped=${state.dropped} and probe reports ${probe.dropped}; the ` +
      'operator and the gate are reading different figures');
  }
}

/**
 * The renderer belongs to the loop task, now and from here on.
 *
 * These two counters are what found the original defect, and ADR-0024 keeps them for this:
 * a later callback that reaches showToast() from another task shows up here rather than as
 * a torn sprite somebody notices once.
 */
function assertRenderingStaysOnLoopTask(probe) {
  if (probe.rendersOffLoopTask !== 0) {
    throw new VerifyFailure(
      `${probe.rendersOffLoopTask} renders began on a task other than ${probe.loopTask}`);
  }
  if (probe.renderCollisions !== 0) {
    throw new VerifyFailure(
      `${probe.renderCollisions} renders began while another task was already drawing`);
  }
}

async function measure(session) {
  const boot = await readState(session, 'boot state, with the modal case menu in the foreground');
  if (boot.menu !== 'yes') {
    throw new VerifyFailure(`the case menu is not in the foreground at boot: menu=${boot.menu}`);
  }

  let leftoverRemoved = false;
  if (await selectRow(session, GATE_CASE)) {
    await deleteCase(session, GATE_CASE);
    leftoverRemoved = true;
  }
  await createCase(session, GATE_CASE);

  await command(session, 'key w', (line) => line === '[KEY] Watson selected',
                'Watson started over the empty case');
  await command(session, ENTER, (line) => line === '[CAPTURE] Started!', 'capture started');

  const firstWindowStart = Date.now();
  await hold(FIRST_WINDOW_MS);
  const firstWindowMs = Date.now() - firstWindowStart;
  const afterFirst = await readProbe(session, 'probe figures after the first capture window');

  // Stop writes the baseline, which is what the second window will score against. Marple is
  // already running either way -- starting capture calls ModeManager::startAll() -- so the
  // displayed mode is deliberately left on Watson: onAnomalyDetected() raises its toast only
  // when Marple is *not* the mode on screen, so switching to it would suppress the very path
  // this window exists to exercise.
  await command(session, STOP, (line) => line === '[KEY] Stop detected - Stopping capture...',
                'capture stopping');
  await session.waitFor((line) => /^\[SESSION\] Saved baseline \(\d+ SSIDs\)$/.test(line),
                        15_000, 'the baseline written to the case on stop');

  await command(session, ENTER, (line) => line === '[CAPTURE] Started!', 'capture restarted');

  const secondWindowStart = Date.now();
  await hold(SECOND_WINDOW_MS);
  const secondWindowMs = Date.now() - secondWindowStart;
  const afterSecond = await readProbe(session, 'probe figures after the second capture window');

  // No save is waited for here, and that is a measurement rather than an omission: stopping
  // calls SessionManager::end(), which unmounts the card and forgets the open case, so the
  // second window's work is never written. The state read below is what says so.
  await command(session, STOP, (line) => line === '[KEY] Stop detected - Stopping capture...',
                'capture stopping for the last time');
  await session.waitFor((line) => line === '[CAPTURE] Stopped', STEP_TIMEOUT_MS,
                        'capture stopped');
  const final = await readState(session, 'state after the last stop');
  // Re-read rather than reusing afterSecond: that reading was taken while the Wi-Fi task
  // was still posting, and conservation is only exact once the producer has stopped and the
  // stop path has drained what it left behind.
  const atRest = await readProbe(session, 'probe figures at rest, after the last stop');

  assertFrameConservation(atRest, final);
  assertRenderingStaysOnLoopTask(atRest);

  return {
    leftoverRemoved, afterFirst, afterSecond, atRest, firstWindowMs, secondWindowMs,
    caseAfterStop: final.name, ssidsAfterStop: final.ssids,
  };
}

/**
 * Second boot: delete the case this run made.
 *
 * It cannot be done in the first, because the first stop unmounted the card — the case menu
 * has nothing to list. A reset remounts it, which is also the cheapest confirmation that the
 * card is fine and it was the firmware that let go of it. (invariant 22)
 */
async function cleanUp(session) {
  const boot = await readState(session, 'boot state on the second boot');
  if (boot.menu !== 'yes') {
    throw new VerifyFailure(`the case menu is not in the foreground at boot: menu=${boot.menu}`);
  }
  if (!(await selectRow(session, GATE_CASE))) {
    throw new VerifyFailure(`${GATE_CASE} is not in the list, so this run cannot clean up`);
  }
  return deleteCase(session, GATE_CASE);
}

function summarise(measured, transcript, connection) {
  const { afterFirst, afterSecond, atRest, firstWindowMs, secondWindowMs } = measured;
  const captureMs = firstWindowMs + secondWindowMs;
  const framesPerSecond = afterSecond.frames / (captureMs / 1000);
  const framesPerMaxSave = framesPerSecond * (afterSecond.maxSaveUs / 1_000_000);
  const anomalies = countAnomalies(transcript);

  const row = (label, first, second) =>
    `  ${label.padEnd(22)} ${String(first).padStart(10)} ${String(second).padStart(10)}`;

  return [
    `environment:            ${ENVIRONMENT}`,
    `port:                   ${connection.port} @ ${connection.baud}`,
    `case:                   ${GATE_CASE}, created empty and deleted` +
    `${measured.leftoverRemoved ? ' (a leftover was removed first)' : ''}`,
    `capture windows:        ${(firstWindowMs / 1000).toFixed(1)} s over an empty ` +
    `baseline, then ${(secondWindowMs / 1000).toFixed(1)} s over the one it built`,
    `after the last stop:    case=${measured.caseAfterStop} ssids=${measured.ssidsAfterStop}`,
    '',
    'which task runs what:',
    `  onFrame runs on       ${afterSecond.frameTask}`,
    `  loop() runs on        ${afterSecond.loopTask}`,
    `  same task             ${afterSecond.frameTask === afterSecond.loopTask ? 'YES' : 'NO'}`,
    `  Wi-Fi stack free      ${afterSecond.frameStackFreeBytes} bytes at its lowest`,
    '',
    `cumulative figures        ${'window 1'.padStart(10)} ${'+ window 2'.padStart(10)}`,
    row('frames', afterFirst.frames, afterSecond.frames),
    row('maxFramesPerPass', afterFirst.maxFramesPerPass, afterSecond.maxFramesPerPass),
    row('maxPassUs', afterFirst.maxPassUs, afterSecond.maxPassUs),
    row('maxRenderUs', afterFirst.maxRenderUs, afterSecond.maxRenderUs),
    row('maxSaveUs', afterFirst.maxSaveUs, afterSecond.maxSaveUs),
    row('rendersOffLoopTask', afterFirst.rendersOffLoopTask, afterSecond.rendersOffLoopTask),
    row('renderCollisions', afterFirst.renderCollisions, afterSecond.renderCollisions),
    '',
    'frames, at rest after the last stop:',
    `  accepted by onFrame   ${atRest.accepted}`,
    `  drained into modes    ${atRest.drained}`,
    `  dropped, queue full   ${atRest.dropped}`,
    `  still queued          ${atRest.queued}`,
    `  conserved             ${atRest.drained + atRest.dropped === atRest.accepted
                                ? 'YES' : 'NO'}`,
    `  baseline holds        ${measured.ssidsAfterStop} SSIDs`,
    '',
    'what the figures imply:',
    `  mean frame rate       ${framesPerSecond.toFixed(1)} frames/s over ` +
    `${(captureMs / 1000).toFixed(1)} s of capture`,
    `  the queue costs       ${atRest.queueDepth * atRest.slotBytes} bytes of heap ` +
    `= ${atRest.queueDepth} slots at ${atRest.slotBytes} B each`,
    `  deepest it had to go  ${afterSecond.maxFramesPerPass} frames between two passes, ` +
    `${(100 * afterSecond.maxFramesPerPass / atRest.queueDepth).toFixed(0)}% of the depth`,
    `  a lock across a save  would have blocked the Wi-Fi task ${afterSecond.maxSaveUs} us, ` +
    `about ${framesPerMaxSave.toFixed(1)} frames at the mean rate`,
    `  anomalies raised      ${anomalies}` +
    `${anomalies === 0 ? ' — no toast was raised in this run' : ''}`,
    '',
    'reading these:',
    '  The drop count is the one figure here a passing run does not prove. Whether a queue',
    '  this deep ever fills is a property of the air, so dropped=0 says the field is reported',
    '  and agrees with what `state` shows the operator — not that the drop path works.',
    '  maxFramesPerPass is the headroom check to watch over time: it is measured, it moved',
    '  4.6x between the two runs ADR-0024 was decided on, and a run that reaches the depth',
    '  is the one that turns this gate from green into a decision about raising it.',
    '  renderCollisions counts only two *different* tasks inside updateDisplay() at once, so',
    '  it under-reports and never invents; rendersOffLoopTask counts every render begun',
    '  anywhere but loop(). Read the second first: the collision count is only trustworthy',
    '  while it is 0, because the marker a render leaves behind is cleared at the end of that',
    '  render, and a task destroyed mid-render would leave a dead handle every later render',
    '  counts against. Reaching that needs a render off the loop task, which is the row above.',
  ];
}

/** Open a session, run @p body in it, and close it whatever happens. */
async function withSession(connection, transcript, body) {
  const session = await openDeviceSession({
    ...connection, readyMarker: READY_MARKER, readyTimeoutMs: BOOT_TIMEOUT_MS,
    budgetMs: BUDGET_MS, transcript,
  });
  try {
    return await body(session);
  } finally {
    await session.close();
  }
}

async function main() {
  const connection = { python: resolvePython(), port: resolvePort(), baud: resolveBaud() };
  console.log(`race-verify: env=${ENVIRONMENT} port=${connection.port} baud=${connection.baud}`);

  const recording = { gate: 'race-verify', environment: ENVIRONMENT };
  const { outcome } = await recordRun(recording, async ({ buildLog, transcript }) => {
    console.log('race-verify: building and uploading…');
    await runPio(['run', '-e', ENVIRONMENT, '-t', 'upload'], buildLog);

    console.log(`race-verify: capturing for ${(FIRST_WINDOW_MS + SECOND_WINDOW_MS) / 1000} s ` +
                'in two windows…');
    const measured = await withSession(connection, transcript, measure);

    console.log('race-verify: resetting to delete the case this run made…');
    await withSession(connection, transcript, cleanUp);

    return summarise(measured, transcript, connection);
  });
  if (outcome === 'FAIL') process.exitCode = 1;
}

await main();
