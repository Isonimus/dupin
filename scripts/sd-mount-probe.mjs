#!/usr/bin/env node
/**
 * Probe: does the card in the slot mount, boot after boot, in the state a boot leaves it in?
 *
 * Written to decide whether the hand-rolled CMD0 reset `SessionManager::begin()` ran before
 * `SD.begin()` mounted an 8 GB card or wedged it: on 2026-09-14 that card drew three `crc error`
 * replies and `GO_IDLE_STATE failed`. Run across two cards, with and without the sequence, it
 * counted 45 mounts in 45 boots, the M5Launcher boot the sequence existed for included, so the
 * sequence was deleted and the failure is recorded as not reproduced (ADR-0032). It stays for the
 * day that failure comes back: run it in the state the card was in when it did.
 *
 * One invocation is one card, across up to three arms:
 *
 * - `reset`    the harness resets the chip, which leaves the card powered across the reboot;
 * - `power`    you remove all power, so the card starts from cold;
 * - `launcher` you boot through M5Launcher into Dupin, a card M5Launcher has just been using.
 *
 * The last two are boots the harness must not cause, so it attaches without a reset and reads
 * the mount from the read-only `probe` report: their boot log went to a port nobody had open.
 * The launcher runs whatever build it installed, so that arm builds nothing.
 *
 * A card that fails to mount is a finding, not a failed run; the run fails only when the harness
 * does. It answers a question and the numbers land in an ADR, so it is not a `make` target
 * (CLAUDE.md §3). Run it from a terminal of your own: the power and launcher arms wait for Enter.
 *
 * Usage: DUPIN_SD_CARD=8gb node scripts/sd-mount-probe.mjs
 *   DUPIN_SD_CARD   plain label for the card in the slot (required)
 *   DUPIN_SD_ARMS   comma list of reset, power, launcher (default reset,power)
 *   DUPIN_SD_RUNS   boots per arm (default 3)
 *   DUPIN_ENV       defaults to cardputer_adv, the board the failure was seen on
 */
import { createInterface } from 'node:readline/promises';

import {
  VerifyFailure, openDeviceSession, recordRun, resolveBaud, resolvePort, resolvePython, runPio,
} from './verify-harness.mjs';

const ENVIRONMENT = process.env.DUPIN_ENV ?? 'cardputer_adv';
const READY_MARKER = '[BOOT] Initialization complete';
// Matches verify-input: the 25 s boot budget has no headroom on these boards (ADR-0014).
const BOOT_TIMEOUT_MS = 40_000;
const REPLY_TIMEOUT_MS = 5_000;
// An attached board may still be booting when Enter is pressed: one attach landed mid-boot and
// its `probe` drew no reply. So it is pinged until it answers, inside one deadline as long as a
// boot is allowed, and asked for the report only once it has.
const PING_INTERVAL_MS = 1_000;
const ARMS = ['reset', 'power', 'launcher'];
const ATTACHED_ARMS = new Set(['power', 'launcher']);
// What the boot says about the card: Dupin's own lines and the Arduino SD driver's warnings,
// which are where `crc error` and `GO_IDLE_STATE failed` come from.
const MOUNT_LINE = /\[SD\]|sd_diskio|vfs_api|sdcard/;
const PROBE_FIELD = (name) => new RegExp(`\\b${name}=(\\S+)`);

const INSTRUCTIONS = {
  power: 'Unplug the USB cable and switch the battery off, so the card loses power. Wait 5 s, ' +
         'switch on and plug the cable back in, then press Enter once Dupin is on screen.',
  launcher: 'Power-cycle into M5Launcher (unplug, battery off, 5 s, back on), launch Dupin from ' +
            'it, then press Enter once Dupin is on screen.',
};

function requiredChoice(name, allowed) {
  const value = process.env[name];
  if (value === undefined || !allowed.test(value)) {
    throw new VerifyFailure(`${name} must be set and match ${allowed.source}, ` +
                            `got ${JSON.stringify(value)}`);
  }
  return value;
}

function parseArms(text) {
  const arms = text.split(',').map((arm) => arm.trim());
  const unknown = arms.filter((arm) => !ARMS.includes(arm));
  if (arms.length === 0 || unknown.length > 0) {
    throw new VerifyFailure(`DUPIN_SD_ARMS takes ${ARMS.join(', ')}; got ${JSON.stringify(text)}`);
  }
  return arms;
}

function parseRuns(text) {
  const runs = Number(text);
  if (!Number.isInteger(runs) || runs < 1) {
    throw new VerifyFailure(`DUPIN_SD_RUNS must be a positive integer, got ${JSON.stringify(text)}`);
  }
  return runs;
}

/** The mount fields of a `[PROBE]` line, failing loudly when one is missing. */
function parseMount(line) {
  const read = (name) => {
    const found = PROBE_FIELD(name).exec(line);
    if (found === null) throw new VerifyFailure(`probe report has no ${name}= field: ${line}`);
    return found[1];
  };
  return {
    attempts: Number(read('sdAttempts')),
    mountedHz: Number(read('sdMountedHz')),
  };
}

function describeMount({ attempts, mountedHz }) {
  return mountedHz === 0
    ? `FAILED after ${attempts} attempt(s)`
    : `mounted at ${mountedHz / 1_000_000} MHz on attempt ${attempts}`;
}

/** One boot: reset it or attach to it, then read the mount back from `probe`. */
async function observeBoot({ arm, python, port, baud, transcript }) {
  const attach = ATTACHED_ARMS.has(arm);
  const firstLine = transcript.length;
  const session = await openDeviceSession({
    python, port, baud, transcript,
    reset: !attach,
    readyMarker: attach ? null : READY_MARKER,
    readyTimeoutMs: attach ? 0 : BOOT_TIMEOUT_MS,
    budgetMs: attach ? BOOT_TIMEOUT_MS + REPLY_TIMEOUT_MS : REPLY_TIMEOUT_MS,
  });
  try {
    if (attach) {
      session.send('ping');
      const pinger = setInterval(() => session.send('ping'), PING_INTERVAL_MS);
      try {
        await session.waitFor((candidate) => candidate === '[CMD] pong', BOOT_TIMEOUT_MS,
                              'a pong from the attached board');
      } finally {
        clearInterval(pinger);
      }
    }
    session.send('probe');
    const line = await session.waitFor((candidate) => candidate.startsWith('[PROBE]'),
                                       REPLY_TIMEOUT_MS, 'the probe report');
    const bootLines = transcript.slice(firstLine).filter((candidate) => MOUNT_LINE.test(candidate));
    return { mount: parseMount(line), bootLines };
  } finally {
    await session.close();
  }
}

async function main() {
  const card = requiredChoice('DUPIN_SD_CARD', /^[A-Za-z0-9_-]+$/);
  const arms = parseArms(process.env.DUPIN_SD_ARMS ?? 'reset,power');
  const runs = parseRuns(process.env.DUPIN_SD_RUNS ?? '3');
  const python = resolvePython();
  const port = resolvePort();
  const baud = resolveBaud();
  console.log(`sd-mount-probe: env=${ENVIRONMENT} card=${card} ` +
              `arms=${arms.join(',')} runs=${runs} port=${port}`);

  // The artifact directory carries the card, so runs on different cards are told apart by their
  // path rather than by opening each summary.
  const recording = { gate: `sd-mount-probe-${card}`, environment: ENVIRONMENT };
  const prompt = createInterface({ input: process.stdin, output: process.stdout });
  try {
    const { outcome } = await recordRun(recording, async ({ buildLog, transcript, startedAt }) => {
      // Every arm but the launcher flashes this tree first. A power-only run once trusted whatever
      // was on the board and found a build other than the one it was asked about: something had
      // reflashed it since the probe last did.
      if (arms.some((arm) => arm !== 'launcher')) {
        console.log('sd-mount-probe: building and uploading…');
        await runPio(['run', '-e', ENVIRONMENT, '-t', 'upload'], buildLog);
      }

      const results = [];
      for (const arm of arms) {
        for (let run = 1; run <= runs; run += 1) {
          if (ATTACHED_ARMS.has(arm)) {
            await prompt.question(`\n[${arm} ${run}/${runs}] ${INSTRUCTIONS[arm]}`);
          }
          transcript.push(`<probe> ---- ${arm} ${run}/${runs} ----`);
          const { mount, bootLines } = await observeBoot({ arm, python, port, baud, transcript });
          console.log(`  ${arm} ${run}/${runs}: ${describeMount(mount)}`);
          results.push({ arm, run, mount, bootLines });
        }
      }

      const tally = arms.map((arm) => {
        const ofArm = results.filter((result) => result.arm === arm);
        const mounted = ofArm.filter((result) => result.mount.mountedHz !== 0).length;
        return `  ${arm.padEnd(9)} ${mounted}/${ofArm.length} mounted`;
      });
      return [
        `environment:        ${ENVIRONMENT}`,
        `card:               ${card}`,
        `port:               ${port} @ ${baud}`,
        '',
        'tally:',
        ...tally,
        '',
        'boots:',
        ...results.flatMap(({ arm, run, mount, bootLines }) => [
          `  ${arm} ${run}: ${describeMount(mount)}`,
          ...bootLines.map((line) => `      ${line}`),
        ]),
        '',
        `total:              ${Date.now() - startedAt} ms`,
      ];
    });
    if (outcome === 'FAIL') process.exitCode = 1;
  } finally {
    prompt.close();
  }
}

await main();
