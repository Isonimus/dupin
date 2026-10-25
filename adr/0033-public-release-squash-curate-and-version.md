---
id: '0033'
title: "Public release: squash history, curate the tree, and set the first public version"
type: architecture
status: accepted
date: 2026-10-25
supersedes: []
superseded_by: []
---

## Context

Dupin has been developed privately: 84 commits, no remote, no tags. It is being published at
`github.com/Isonimus/dupin` under the Isonimus open-source identity. Going public raises
questions the private repository never had to answer.

1. **Every commit carries a work email.** All 84 commits are authored and committed as
   `Iker <iker.laforga@grupogestionet.com>`, an employer address. Publishing the history as-is
   puts that domain on a public surface forever, and a per-commit audit for secrets or captured
   data (real BSSIDs, locations) across 84 commits is expensive and error-prone. A clean scan of
   the current tree does not prove the history is clean.

2. **The version literal is 0.2.0, and nothing ever released it.** `DUPIN_VERSION_STRING` in
   `include/dupin.h` reads `0.2.0` (ADR-0012 settled it there, read once, restated nowhere). There
   is no public tag, so no user has ever seen a 0.2.0 build. The first public release should be the
   first release number.

3. **No license and no community surface.** There is no `LICENSE`, no `SECURITY.md`, no
   `CODE_OF_CONDUCT.md`, no `DISCLAIMER`, no issue or pull-request templates, no dependency
   automation, and no static analysis beyond "does it compile". A public reader hits a bare,
   contributor-hostile repository, and `README.md`/`CONTRIBUTING.md` carry rot: a misspelt board
   name, a placeholder clone URL, a "Known Issues" section that predates ADR-0032, and a
   contributing guide written for an AI assistant rather than a person.

4. **Maintainer-only material is mixed into the tree.** The repository carries the Stele
   decision-record method and its machinery, an open-work `LEDGER.md`, Claude Code operator config
   under `.claude/`, internal planning docs, a `Claude.md` architecture spec that collides
   case-wise with `CLAUDE.md` on a case-insensitive filesystem, and a `THE_ADVERSARY_MANIFESTO.md`
   left over from using the sibling project as a template. Outside contributors will not use Stele,
   and this material is noise (or a broken-linter trap) in a public clone.

Dupin is a **passive** tool: monitor mode only, it transmits nothing. So unlike the sibling
offensive project (adversary:ADR-0012), its public framing is not "do not attack without
authorization" but "operating a monitor-mode radio is the operator's legal responsibility in their
jurisdiction". The release scaffolding is modelled on adversary:ADR-0012 and adversary:slice-0011;
the disclaimer is not.

## Decision

1. **Squash to a single "initial public release" commit.** The public history begins at one commit
   containing exactly the curated tree, authored under the Isonimus identity. The full pre-public
   history (84 commits) is preserved in an offline git bundle kept in `local_repo/` beside the
   bundles the sibling projects already keep, outside the public repository. This removes the
   work-email exposure and the history-audit burden without a per-commit review, and gives a clean
   first release.

2. **Public identity is Isonimus; no personal or employer address on any public surface.** The
   security and conduct contact is the GitHub handle [@Isonimus](https://github.com/Isonimus),
   alongside GitHub private vulnerability reporting. The license is **MIT**, copyright "The Dupin
   Project", 2026.

3. **The first public version is 0.1.0.** `DUPIN_VERSION_STRING` is corrected from `0.2.0` to
   `0.1.0` in the same change as this record. Nothing read the old value but the on-screen banner
   and `make check-version`, no build carrying it was ever released, and a first public tag of
   `v0.1.0` above an internal literal of `0.2.0` would be the kind of silent disagreement ADR-0012
   exists to prevent. The literal stays single; `make check-version` is unaffected.

4. **Curate the public tree.** Publish the firmware and everything a user or contributor needs;
   keep maintainer-only material local (gitignored, not deleted, so the maintainer keeps using it).

   | Published | Kept local (gitignored) |
   | --- | --- |
   | `src/ test/ include/ lib/`, `platformio.ini`, `sdkconfig.defaults`, `Makefile` | `.claude/` and `.agent/` (Stele hook, commands, operator config) |
   | `README LICENSE DISCLAIMER CHANGELOG CONTRIBUTING CODE_OF_CONDUCT SECURITY CLAUDE.md` | `LEDGER.md` (open-work list) |
   | `adr/ adr/INDEX.md` (decision records, as static artifacts) | `docs/epics/` (internal planning) |
   | `scripts/*-verify.mjs scripts/*-probe.mjs scripts/verify-harness.* scripts/serial-tap.py` (Dupin's own harness) | the Stele *workflow* as a process |
   | `scripts/lint-docs.mjs build-index.mjs check-immutable.mjs` (tracked per stele:ADR-0018) | |
   | `.github/workflows/firmware.yml` + the new release and static-analysis workflows | `.github/workflows/docs.yml` (runs the Stele doc invariants) |

   The decision records (`adr/`) ship as **static artifacts**; the Stele machinery that maintains
   them does not, and public CI does not run the doc invariants, so a public clone cannot fail on
   them. The three doc-linters stay tracked because the maintainer's local pre-commit hook validates
   the staged copies of exactly those files (stele:ADR-0018); they are inert in a public clone,
   which carries no hook. `CONTRIBUTING.md` tells contributors they need not touch `adr/`.

5. **Add the community-health, supply-chain, and static-analysis scaffolding**, modelled on
   adversary:slice-0011: `LICENSE`, `DISCLAIMER`, `SECURITY.md`, `CODE_OF_CONDUCT.md`,
   `CHANGELOG.md`, a rewritten human-facing `CONTRIBUTING.md`, `.github/` issue forms and a PR
   template, `.github/dependabot.yml` (github-actions weekly, the one ecosystem it can track here),
   and CodeQL plus cppcheck over `src/`. A tag-triggered `release.yml` builds the cardputer factory
   and app images with SHA-256 sums; the esp-web-tools browser flasher and GitHub Pages are
   deferred (`LEDGER.md`).

6. **Resolve the `Claude.md` case collision.** It is renamed into `docs/` and cited from
   `CLAUDE.md` so it is no longer orphaned under the taxonomy (stele:ADR-0010), and
   `THE_ADVERSARY_MANIFESTO.md` is removed from the tree as a template leftover.

## Consequences

- The public repository's `git blame`/history reaches back only to the initial commit; granular
  history lives solely in the offline bundle, and the `adr/` records carry the "why" that blame
  would otherwise supply.
- The maintainer continues working in this local repository (which retains the gitignored
  `.claude/`, `.agent/`, `docs/epics/`, `LEDGER.md`, and `docs.yml`) and pushes normally to the
  public remote afterwards. The squash is a one-time operation, not a per-release habit.
- `LEDGER.md` is deliberately off the public repository. If a public roadmap or known-issues
  surface is wanted later, it moves to GitHub Issues or Discussions: a separate decision.
- Reversing the squash on the public record is not possible, but nothing is lost: the bundle is the
  complete backup, and a fresh audit could re-derive any excluded file.
- Opening any currently-excluded material later (for example `LEDGER.md` or the Stele machinery) is
  a normal commit, not a second squash.
- The browser flasher is absent at 0.1.0, so a first-time user installs with `pio run -t upload` or
  esptool against the release binaries rather than from a web page; `LEDGER.md` carries the flasher
  and Pages as a follow-up.
