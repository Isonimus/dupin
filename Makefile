# Doc-invariant checks and firmware gates.
#
# This is the repo's single named entry point for the stele method kit's commands
# (ADR-0002). The kit ships written for JavaScript repos and calls `npm run <x>`;
# Dupin is C++/PlatformIO and has no npm manifest, so the kit's prose calls these
# targets instead. A checker invoked only by hand is a checker that runs once.

.PHONY: check lint index index-check check-immutable check-version check-formats test-scripts build test cppcheck verify-boot verify-boot-adv verify-input verify-baseline verify-race

# What runs without hardware attached. The device half of the harness is local and
# named in the slice (ADR-0002), never wired here — it cannot run unattended.
check: lint index-check check-version check-formats test-scripts

lint:
	node scripts/lint-docs.mjs .

index:
	node scripts/build-index.mjs .

index-check:
	node scripts/build-index.mjs --check .

# One version literal in the tree, in include/dupin.h. src/ reads it and never restates it.
# This is a grep rather than a script because it needs no parsing to be exact: the pattern
# matches a quoted dotted triple, and on the commit that added it the only four hits in the
# repo were the one definition and the three copies it replaced (ADR-0012). It joins `check`
# because it needs no hardware, so CI enforces it alongside the doc invariants.
#
# grep's exits are told apart for the reason check-formats gives below: this recipe was once
# `! grep`, which printed the success line with a real version literal on screen whenever grep
# also failed to read a path (ADR-0028).
VERSION_DIRS ?= src/
check-version:
	@status=0; grep -rn '"[^"]*[0-9]\+\.[0-9]\+\.[0-9]\+[^"]*"' $(VERSION_DIRS) || status=$$?; \
	case $$status in \
	  0) echo "check-version: version literal in $(VERSION_DIRS) — read DUPIN_VERSION_STRING instead (ADR-0012)"; exit 1 ;; \
	  1) echo "check-version: one version literal, in include/dupin.h" ;; \
	  *) echo "check-version: grep could not read all of $(VERSION_DIRS) (exit $$status), so the scan is incomplete"; exit 2 ;; \
	esac

# No `l` or `ll` length modifier in a format string. The compilers check specifiers jointly, but
# only in code they are given: the host skips every #ifndef NATIVE_BUILD region and all of
# main.cpp, and `long` is the one width where a fixed-width typedef means a different type on
# each platform -- so a specifier right on the device and wrong on the host sits there unread.
# Four did (ADR-0027, ADR-0028). The scan is per line and requires the modifier inside a
# double-quoted literal, so prose naming the form in backticks passes; it has no exemptions.
#
# grep's three exits are told apart rather than inverted. `! grep` reads 2 -- an unreadable or
# missing directory -- as "no match", and grep returns 2 over 1 even when it also found a match,
# so one mistyped path turned the whole gate green with real hits on screen. Measured on the
# change that added this target (ADR-0028).
FORMAT_DIRS ?= src/ include/
check-formats:
	@status=0; grep -rnE '"[^"]*%[-+ #0-9.*]*l{1,2}[diouxX][^"]*"' $(FORMAT_DIRS) || status=$$?; \
	case $$status in \
	  0) echo "check-formats: long length modifier in a format string — cast to a 32-bit type, or use PRIu64/PRId64 (ADR-0028)"; exit 1 ;; \
	  1) echo "check-formats: no line holds a format literal with a long length modifier" ;; \
	  *) echo "check-formats: grep could not read all of $(FORMAT_DIRS) (exit $$status), so the scan is incomplete"; exit 2 ;; \
	esac

# The host half of the verify harness: where a run's evidence lands. `node --test` ships with
# Node, so this needs no npm manifest (ADR-0002), and it joins `check` because it needs no
# board. Fixed artifact names once let a rerun erase the only transcript of a failure (ADR-0021).
test-scripts:
	node --test scripts/verify-harness.test.mjs

# The third checker the pre-commit hook runs, which `check` above cannot bundle: it
# compares two git revisions rather than reading a directory, so it needs a range and
# has no meaning on a working tree. The defaults ask the question a developer means by
# hand — "did the commit I just wrote edit an immutable body?" — and CI overrides them
# with the range its event actually covers (ADR-0007).
IMMUTABLE_BASE ?= HEAD~1
IMMUTABLE_HEAD ?= HEAD

check-immutable:
	node scripts/check-immutable.mjs $(IMMUTABLE_BASE) $(IMMUTABLE_HEAD)

build:
	pio run -e cardputer

test:
	pio test -e native

# Static analysis over our own sources only (lib deps live in .pio/libdeps and are not ours
# to patch). Gates on cppcheck's default `error` severity -- the low-false-positive class
# (null deref, buffer overrun, use-after-free) -- rather than the noisier warning/style
# levels, which on an existing tree are a coin flip between dead-red on day one and a linter
# nobody enforces. False positives are handled with in-source `// cppcheck-suppress` carrying
# the why, never by loosening the gate. Called by .github/workflows/firmware.yml so CI and a
# developer run the identical command (ADR-0007). Needs cppcheck on PATH.
cppcheck:
	cppcheck --std=c++17 --language=c++ --error-exitcode=1 \
	  --inline-suppr --suppress=missingIncludeSystem -I src -I include src/

# The device half of the harness (ADR-0002 splits enforcement because this one cannot run
# unattended: it needs a board on a USB port). Never wired into `check` for that reason —
# it is named here because a checker with no entry point runs once (ADR-0009).
# DUPIN_PORT overrides the auto-detected serial port.
verify-boot:
	node scripts/boot-verify.mjs

# The same gate on the ADV env, which needs its own entry point rather than a remembered
# DUPIN_ENV= prefix: this is the regression proof for ADR-0011, and a proof reachable only
# by recalling an environment variable is the "runs once" failure ADR-0009 describes.
# Both targets are needed because the two envs are separate build configurations — the
# defect they caught lived in platformio.ini, which `verify-boot` alone never exercises.
verify-boot-adv:
	DUPIN_ENV=cardputer_adv node scripts/boot-verify.mjs

# Asserts that pressing a key does something -- the gap CLAUDE.md §3 is built around, and
# the one `make build`, `make test` and `make verify-boot` all leave open between them
# (ADR-0014). Hardware-attached like the other two, so deliberately not part of `make check`.
verify-input:
	node scripts/input-verify.mjs

# Asserts the SD baseline round trip: a count written under one case comes back under that
# case across a reset, and changes when the case changes (ADR-0016). Drives the board twice,
# because a round trip asserted inside one boot proves only that RAM kept what RAM was given.
# Hardware-attached like the other three, so deliberately not part of `make check`.
verify-baseline:
	node scripts/baseline-verify.mjs

# Asserts that every frame onFrame accepts is drained into the modes or counted as dropped,
# and that nothing renders off the loop task (ADR-0024). Captures for two minutes, because
# the figures it also records -- how deep the queue went, what a pass costs -- are only worth
# reading over a window long enough to contain an autosave. Hardware-attached like the other
# four, so deliberately not part of `make check`.
verify-race:
	node scripts/race-verify.mjs
