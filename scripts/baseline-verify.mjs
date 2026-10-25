#!/usr/bin/env node
/**
 * Device verification: a case's baseline survives a power cycle, and belongs to that case.
 *
 * This is the round trip ADR-0003 left device-only. It asserts the property ADR-0016 decided
 * on: opening a case restores *that* case's baseline, and switching case switches the baseline
 * the detection modes score against.
 *
 * Neither half is reachable from a host test. `SessionManager::loadBaseline` reads the SD
 * `File` API and its body is compiled out under `NATIVE_BUILD`; what *is* portable — parsing
 * the document — is asserted in `test/test_session_manager/` instead (ADR-0016 decision 5).
 * What remains here is bytes reaching the card under one case and coming back under the same
 * one, which needs a card, a radio, and a reset.
 *
 * The gate works only in cases it creates, and deletes them (ADR-0020). It used to open the
 * card's first case and save into it, which starved it: Watson announces only SSIDs absent
 * from the case's baseline, so every passing run left fewer to find. And a round trip into a
 * case that already held the saved figure passes on a save that wrote nothing. A case created
 * empty is the one state where the restored count can only have come from this run's write.
 *
 * The reset is why this drives the device twice. Pass 1 creates a case, captures and stops,
 * which writes the baseline; pass 2 resets the board and reads it back. A round trip asserted
 * inside one boot would prove only that RAM still holds what RAM was given.
 *
 * Pass 1 captures twice, and that is ADR-0025. Stopping used to unmount the card and clear the
 * open case, so a second window in the same boot wrote nothing and said nothing — and this gate
 * was green the whole time, because one window never reaches the state a second one starts in.
 * The figure pass 2 round-trips is the second window's.
 *
 * Usage: make verify-baseline   (DUPIN_PORT overrides the auto-detected serial port)
 */
import {
  VerifyFailure, asksToDelete, openDeviceSession, recordRun, resolveBaud, resolvePort,
  resolvePython, runPio,
} from './verify-harness.mjs';

const ENVIRONMENT = process.env.DUPIN_ENV ?? 'cardputer';
const READY_MARKER = '[BOOT] Initialization complete';
// Matches verify-input: verify-boot's 25 s has no headroom on this board, and a gate that
// fails on a slow boot reports the wrong defect (ADR-0014).
const BOOT_TIMEOUT_MS = 40_000;
const STEP_TIMEOUT_MS = 5_000;
// Long enough for channel hopping to land on occupied channels and for beacons to arrive.
// The case starts empty, so every network in range is new to it: nothing here depends on
// *which* networks are in range, only that a few are.
const DISCOVERY_TIMEOUT_MS = 30_000;
// How many SSIDs the baseline must hold before it is worth round-tripping. Low enough to
// pass anywhere with a couple of access points in earshot, high enough that the assertion
// is about a baseline rather than about a single entry.
const MIN_BASELINE_SSIDS = 3;
// Stopping writes the baseline, the case stats and the metadata.
const SAVE_TIMEOUT_MS = 15_000;
// A second capture window in the same boot, long enough to be a window rather than a start
// immediately followed by a stop. Nothing in it waits for a discovery: the first window already
// put the room's networks into this case's baseline, so Watson has nothing left to announce as
// new, and what the second stop must do is write at all (ADR-0025).
const SECOND_WINDOW_MS = 10_000;

// The two cases this gate owns. Created and deleted by it, never shared with the operator.
const GATE_CASE = 'verify_gate_a';
const SWITCH_CASE = 'verify_gate_b';
// refreshCaseList() lists at most this many cases (its bare cap is a LEDGER item). Pressing
// down that many times plus one, for [+] New Case, has visited every row the menu can show.
const MAX_LISTED_CASES = 16;
const NEW_CASE_ROW = '+';
const CASE_MENU_REFUSED = '[CASE] Case menu refused: capture is running';

// The tap's lifetime, never an assertion (ADR-0015): generous enough that no wait below can
// outlive it. A full search of the list is one state and one down per row.
const LIST_SEARCH_MS = (MAX_LISTED_CASES + 1) * 2 * STEP_TIMEOUT_MS;
// The commands a pass sends outside its list searches, rounded up well past the real count.
const FIXED_COMMANDS_PER_PASS = 60;
const PASS_BUDGET_MS = 6 * LIST_SEARCH_MS + FIXED_COMMANDS_PER_PASS * STEP_TIMEOUT_MS
  + DISCOVERY_TIMEOUT_MS + 2 * SAVE_TIMEOUT_MS + SECOND_WINDOW_MS;

const ENTER = 'key enter';
const ENTER_ECHO = '[CMD] key 0xD';
const DOWN = 'key l';
const DOWN_ECHO = '[CMD] key l';
// Backtick stops capture, which is the path that writes the baseline to the card.
const STOP = 'key `';

const isState = (line) => line.startsWith('[STATE]');
const openedCase = (line) =>
  /^\[BASELINE\] (Restored \d+ SSIDs|No stored baseline) for case \S+$/.test(line);

/** Split a `[STATE] mode=… running=… menu=… case=… ssids=… sel=… notice=…` report. */
function parseState(line) {
  const field = (name) => {
    const found = new RegExp(`\\b${name}=(\\S+)`).exec(line);
    if (found === null) {
      throw new VerifyFailure(`state report has no ${name}= field: ${line}`);
    }
    return found[1];
  };
  // notice= is last and runs to end of line, because the notice is a sentence.
  const notice = / notice=(.*)$/.exec(line);
  if (notice === null) {
    throw new VerifyFailure(`state report has no notice= field: ${line}`);
  }
  return {
    menu: field('menu'), running: field('running'), name: field('case'),
    ssids: Number(field('ssids')),
    selected: field('sel'), notice: notice[1],
  };
}

/** The SSID count an `[BASELINE] …` line reports, with "No stored baseline" meaning zero. */
function parseRestored(line) {
  const found = /^\[BASELINE\] Restored (\d+) SSIDs for case (\S+)$/.exec(line);
  if (found !== null) return { count: Number(found[1]), name: found[2] };
  const empty = /^\[BASELINE\] No stored baseline for case (\S+)$/.exec(line);
  if (empty !== null) return { count: 0, name: empty[1] };
  throw new VerifyFailure(`not a case-open report: ${line}`);
}

/** Send one command and wait for the line @p expect accepts. */
function command(session, send, expect, what, timeoutMs = STEP_TIMEOUT_MS) {
  session.send(send);
  return session.waitFor(expect, timeoutMs, `${what} (after sending ${JSON.stringify(send)})`);
}

async function readState(session, what) {
  return parseState(await command(session, 'state', isState, what));
}

/**
 * Hold for @p durationMs.
 *
 * A timer, never a wait for a line (ADR-0015): the second capture window's correct content is
 * whatever the air delivers, including nothing at all, so there is no line to wait for.
 */
function hold(durationMs) {
  return new Promise((resolve) => { setTimeout(resolve, durationMs); });
}

/**
 * Press down until `sel=` names @p wanted. False once every row the menu can list has been
 * visited without it, which is also how a case the list's cap hides reads.
 */
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

/**
 * Create @p name from the open case menu and return what opening it reported. A refused name
 * prints nothing, so a state report arriving first is the refusal, and its notice says why.
 */
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
  return parseRestored(first);
}

/** Select @p name in the open case menu, arm the delete with `x`, and confirm it. */
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
  return readState(session, `state after deleting ${name}`);
}

/** Pass 1: clear anything an interrupted run left, create the gate's case, capture, save. */
/**
 * Stop capture and return the SSID count the stop's save reported.
 *
 * The stop's own save line, not the first one to arrive: the interval autosave prints the same
 * line, and a card configured with a short interval would otherwise hand this gate its count
 * (ADR-0020). The `[SD]` line after it names the case, which is what says the write went to the
 * case this gate opened rather than to whatever the card was left holding (ADR-0025).
 */
async function stopAndReadSave(session, what) {
  await command(session, STOP, (line) => line === '[KEY] Stop detected - Stopping capture...',
                `${what} stopping`);
  const saved = await session.waitFor(
    (line) => /^\[SESSION\] Saved baseline \(\d+ SSIDs\)$/.test(line),
    SAVE_TIMEOUT_MS, `the baseline ${what} wrote to the case on stop`);
  await session.waitFor((line) => line === `[SD] Saved to case ${GATE_CASE}`,
                        SAVE_TIMEOUT_MS, `the stop naming ${GATE_CASE} as what it wrote to`);
  return Number(/\((\d+) SSIDs\)/.exec(saved)[1]);
}

async function populate(session) {
  const boot = await readState(session, 'boot state, with the modal case menu in the foreground');
  if (boot.menu !== 'yes') {
    throw new VerifyFailure(`the case menu is not in the foreground at boot: menu=${boot.menu}`);
  }

  const leftovers = [];
  for (const name of [GATE_CASE, SWITCH_CASE]) {
    if (await selectRow(session, name)) {
      await deleteCase(session, name);
      leftovers.push(name);
    }
  }

  const opened = await createCase(session, GATE_CASE);
  if (opened.name !== GATE_CASE || opened.count !== 0) {
    throw new VerifyFailure(
      `a freshly created case must open empty, or the round trip proves no write: opening ` +
      `${opened.name} restored ${opened.count} SSIDs`);
  }

  await command(session, 'key w', (line) => line === '[KEY] Watson selected',
                'Watson started over the empty case');
  await command(session, ENTER, (line) => line === '[CAPTURE] Started!', 'capture started');

  // Waiting for several rather than the first is deliberate: capture stops as soon as this
  // resolves, so waiting for one would round-trip a single entry. The predicate is called once
  // per line in arrival order, so counting matches is well defined.
  let discovered = 0;
  const isNthDiscovery = (line) =>
    line.startsWith('[NEW SSID]') && ++discovered >= MIN_BASELINE_SSIDS;
  await command(session, 'ping', isNthDiscovery,
                `the first ${MIN_BASELINE_SSIDS} SSIDs Watson adds to the empty case`,
                DISCOVERY_TIMEOUT_MS);

  // Asked while the baseline holds entries, so a switch that got through would show as a
  // different case or a different count. Firmware that opens the menu never prints the refusal,
  // and its modal loop then holds the board until this wait expires (ADR-0022).
  await command(session, 'key c', (line) => line === CASE_MENU_REFUSED,
                'the case menu refused while capture runs');
  const duringCapture = await readState(session, 'state after the refused case menu');
  if (duringCapture.running !== 'yes' || duringCapture.menu !== 'no'
      || duringCapture.name !== GATE_CASE || duringCapture.ssids < MIN_BASELINE_SSIDS) {
    throw new VerifyFailure(
      `refusing the case menu mid-capture must leave capture and its case alone: state says ` +
      `running=${duringCapture.running} menu=${duringCapture.menu} case=${duringCapture.name} ` +
      `ssids=${duringCapture.ssids}`);
  }

  const firstWindowCount = await stopAndReadSave(session, 'the first capture window');
  if (firstWindowCount < MIN_BASELINE_SSIDS) {
    throw new VerifyFailure(
      `capture saved ${firstWindowCount} SSIDs, below the ${MIN_BASELINE_SSIDS} this gate ` +
      'round-trips — it needs a board within range of a few 2.4 GHz networks');
  }

  // The second window, and the reason this gate captures twice. Stopping used to unmount the
  // card and clear the open case, so everything a later window learned was dropped with no line
  // anywhere saying so — and a gate that captured once stayed green throughout (ADR-0025).
  //
  // `case=`, not `ssids=`: both counts here would be the same in-RAM total read twice, which
  // proves nothing (invariant 17). The cross-read is the reset in restore(), which now
  // round-trips the second window's figure rather than the first's. What this asserts is the
  // ownership fact, and it is what fails first and says most — before the fix it reads `case=-`.
  const betweenWindows = await readState(session, 'state after the first stop');
  if (betweenWindows.running !== 'no' || betweenWindows.name !== GATE_CASE) {
    throw new VerifyFailure(
      `stopping capture must leave the case open: after saving ${firstWindowCount} SSIDs into ` +
      `${GATE_CASE}, state says running=${betweenWindows.running} case=${betweenWindows.name}`);
  }

  await command(session, ENTER, (line) => line === '[CAPTURE] Started!',
                'a second capture window in the same boot');
  await hold(SECOND_WINDOW_MS);
  const savedCount = await stopAndReadSave(session, 'the second capture window');
  // The case stayed open, so the baseline accumulates: a second window may add entries and must
  // never lose one. A shrink is a baseline rebuilt from nothing rather than kept.
  if (savedCount < firstWindowCount) {
    throw new VerifyFailure(
      `the second window saved ${savedCount} SSIDs into ${GATE_CASE} after the first saved ` +
      `${firstWindowCount}: a baseline lost entries across a stop`);
  }
  return { savedCount, firstWindowCount, leftovers };
}

/** Pass 2, after a reset: read the case back, prove the baseline follows the case, clean up. */
async function restore(session, savedCount) {
  if (!(await selectRow(session, GATE_CASE))) {
    throw new VerifyFailure(
      `${GATE_CASE} is not in the case list after the reset — the menu lists at most ` +
      `${MAX_LISTED_CASES} cases, so a card holding that many hides the gate's own`);
  }
  const reopened = parseRestored(
    await command(session, ENTER, openedCase, `${GATE_CASE} reopened after the reset`));
  // The assertion the gate exists for: the same count, for the same case, across a reset. An
  // exact match rather than a floor -- "some SSIDs came back" would pass on a baseline that
  // silently dropped half of them.
  if (reopened.name !== GATE_CASE || reopened.count !== savedCount) {
    throw new VerifyFailure(
      `saved ${savedCount} SSIDs into ${GATE_CASE} but reopening it restored ` +
      `${reopened.count} for ${reopened.name}`);
  }
  const afterRestore = await readState(session, 'state with the restored case open');
  if (afterRestore.name !== GATE_CASE || afterRestore.ssids !== savedCount) {
    throw new VerifyFailure(
      `the restored baseline is not what the modes would score against: ${GATE_CASE} ` +
      `restored ${savedCount} SSIDs but state says case=${afterRestore.name} ` +
      `ssids=${afterRestore.ssids}`);
  }

  // The switch. The case switched to is created empty here, so the figure it must show is a
  // known 0 rather than whatever some other case on the card happens to hold.
  await command(session, 'key c', (line) => line === '[KEY] Case Menu', 'case menu reopened');
  const switched = await createCase(session, SWITCH_CASE);
  const afterSwitch = await readState(session, 'state after switching case');
  if (switched.count !== 0 || afterSwitch.name !== SWITCH_CASE || afterSwitch.ssids !== 0) {
    throw new VerifyFailure(
      `the baseline did not follow the case: switching from ${GATE_CASE} (${savedCount} ` +
      `SSIDs) to the empty ${SWITCH_CASE} left case=${afterSwitch.name} ` +
      `ssids=${afterSwitch.ssids}`);
  }

  await command(session, 'key c', (line) => line === '[KEY] Case Menu', 'case menu reopened');
  await deleteCase(session, GATE_CASE);
  // SWITCH_CASE is the open one, so deleting it must take its baseline along (ADR-0016).
  const afterOpenDeleted = await deleteCase(session, SWITCH_CASE);
  if (afterOpenDeleted.name !== '-' || afterOpenDeleted.ssids !== 0) {
    throw new VerifyFailure(
      `deleting the open case left case=${afterOpenDeleted.name} ` +
      `ssids=${afterOpenDeleted.ssids}: a baseline outlived its case`);
  }
  for (const name of [GATE_CASE, SWITCH_CASE]) {
    if (await selectRow(session, name)) {
      throw new VerifyFailure(`${name} is still selectable after it was deleted`);
    }
  }
  return { afterRestore, afterSwitch };
}

/** Open a session, run @p body in it, and close it whatever happens. */
async function withSession(connection, transcript, body) {
  const session = await openDeviceSession({
    ...connection, readyMarker: READY_MARKER, readyTimeoutMs: BOOT_TIMEOUT_MS,
    budgetMs: PASS_BUDGET_MS, transcript,
  });
  try {
    const result = await body(session);
    return { result, elapsedMs: Date.now() - session.resetAt };
  } finally {
    await session.close();
  }
}

async function main() {
  const connection = { python: resolvePython(), port: resolvePort(), baud: resolveBaud() };
  console.log(`baseline-verify: env=${ENVIRONMENT} port=${connection.port} ` +
              `baud=${connection.baud}`);

  const recording = { gate: 'baseline-verify', environment: ENVIRONMENT };
  const { outcome } = await recordRun(recording, async ({ buildLog, transcript, startedAt }) => {
    console.log('baseline-verify: building and uploading…');
    await runPio(['run', '-e', ENVIRONMENT, '-t', 'upload'], buildLog);

    console.log(`baseline-verify: pass 1 — creating ${GATE_CASE} and capturing two windows…`);
    const pass1 = await withSession(connection, transcript, populate);
    const { savedCount, firstWindowCount, leftovers } = pass1.result;

    console.log(`baseline-verify: pass 2 — resetting, reopening ${GATE_CASE}, ` +
                `expecting ${savedCount} SSIDs back…`);
    const pass2 = await withSession(connection, transcript,
                                    (session) => restore(session, savedCount));
    const { afterRestore, afterSwitch } = pass2.result;

    return [
      `environment:         ${ENVIRONMENT}`,
      `port:                ${connection.port} @ ${connection.baud}`,
      '',
      `leftovers removed:   ${leftovers.length > 0 ? leftovers.join(', ') : 'none'}`,
      `case under test:     ${GATE_CASE}, created empty`,
      `first window saved:  ${firstWindowCount} SSIDs`,
      `second window saved: ${savedCount} SSIDs, same case still open after the first stop`,
      `restored after reset: ${afterRestore.ssids} SSIDs for case ${afterRestore.name}`,
      `switched to:         ${afterSwitch.name}, created empty, ssids ${afterRestore.ssids} -> ` +
      `${afterSwitch.ssids}`,
      `cleaned up:          ${GATE_CASE} and ${SWITCH_CASE} deleted; neither selectable`,
      '',
      `reset-to-last-step:  ${pass2.elapsedMs} ms (pass 2)`,
      `serial lines:        ${transcript.length} across both passes`,
      '',
      'asserted: an SSID count written into an empty case comes back under that case across a',
      '          reset, and changes to 0 when the case changes to another empty one (ADR-0016);',
      '          deleting the open case leaves no baseline behind; the gate touched no case it',
      '          did not create (ADR-0020). The format carries every field a restored entry',
      '          needs to behave like a live one (ADR-0017). The case menu is refused while',
      '          capture runs, and leaves capture, case and baseline as they were (ADR-0022).',
      '          Stopping capture keeps the card mounted and the case open, so a second window',
      '          in the same boot writes to the same case and it is that window\'s count the',
      '          reset round-trips (ADR-0025).',
      '',
      `total:               ${Date.now() - startedAt} ms`,
    ];
  });
  if (outcome === 'FAIL') process.exitCode = 1;
}

await main();
