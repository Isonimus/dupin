---
id: '0028'
title: "A format string carries no long length modifier"
type: architecture
status: accepted
date: 2026-09-17
supersedes: []
superseded_by: []
---

## Context

1. **The compiler gate cannot see code it is not given.** Invariant 6 is enforced by
   `-Werror=format` on both envs, jointly: the host catches a specifier wrong on the host, the
   device one wrong on the device (ADR-0006). ADR-0027 measured the hole in that: three bare
   `%lu` sat in the case document for as long as it lived inside `#ifndef NATIVE_BUILD`, because
   the device — the only env that compiled them — is the one where `uint32_t` *is*
   `unsigned long`. A third place the host never compiles is a whole file:
   `build_src_filter` excludes `main.cpp` and `capture/wifi_capture.cpp` from the native env.
   A fourth is an `#ifdef` inside an argument list, which changes the argument's type per arm
   so each arm type-checks against a specifier that is wrong for the other.

2. **The hole is one direction, and it has two widths.** The device compiles every source
   file, so a specifier wrong *on the device* is always caught. What escapes is a specifier
   right on the device and wrong on the host, in code only the device compiles. Measured on
   2026-09-17 with `grep -rnE '%[-+ #0-9.*]*l{1,2}[diouxX]' src/ include/`, the tree holds one
   such site beyond the three ADR-0027 removed: `[SD] Card size: %llu MB` in
   `SessionManager::begin()`, inside the cutout, passing a `uint64_t` — `unsigned long long` on
   the device, `unsigned long` on x86-64 Linux. It is the `%lu` defect one width up, and a rule
   written against `%lu` alone would have passed it.

3. The same grep returned six more hits, none of them wrong today: four in the host mock
   `MockSerialClass` (`src/ui/serial_logger.h`), whose `long` and `unsigned long` overloads
   print exactly the type their parameter declares; `rssiSum` as `%ld` with
   `static_cast<long>`, correct on both; and three comments that mention `%lu` in backticks,
   which a pattern requiring the specifier inside a double-quoted literal does not match.

4. **`long` is exactly the width that moves.** The fixed-width typedefs map onto different
   fundamental types on the two platforms only at `long`: `uint32_t` is `unsigned long` on the
   device and `unsigned int` on the host, `uint64_t` is `unsigned long long` on the device and
   `unsigned long` on the host. `int` is 32 bits on both, and `size_t` has its own `z`
   modifier. A specifier with no `l` in it cannot be right on one platform and wrong on the
   other for a value that was cast to the type the specifier names.

5. `make check-version` already enforces a rule the compiler cannot, as a grep over `src/`
   that fails on a quoted pattern, and joins `make check` so CI runs it (ADR-0012).

## Decision

1. **No format string literal under `src/` or `include/` carries an `l` or `ll` length
   modifier** on an integer conversion (`d i o u x X`). A 32-bit value prints as `%u` or `%d`
   with an explicit cast (ADR-0006). A value that genuinely needs 64 bits prints through
   `PRIu64` or `PRId64` from `<cinttypes>`, whose expansion is each platform's own and which
   `-Werror=format` then checks.

2. **`make check-formats` enforces it**, as a grep over `src/` and `include/`, and joins
   `make check`. It scans one line at a time and matches the modifier only inside a
   double-quoted literal, so prose that names the form in backticks does not trip it. It tells
   grep's three exits apart — 0 a match, 1 none, 2 an error — rather than inverting the status
   with `!` the way `check-version` does: measured on this change, `! grep` reads an unreadable
   or missing directory as "no match", and grep returns 2 over 1 even when it also found a
   match, so a single mistyped path printed the success line with a real hit on screen above
   it. An error fails the gate and says the scan is incomplete.

3. **It has no exemptions.** The mock's four `long` overloads print through `std::to_string`
   rather than a specifier. They were correct, but an allow-list is the one place a rule like
   this buries the site that is inconvenient to fix, and four lines are cheaper than a
   mechanism for excusing them.

4. **The two sites that were correct change anyway**: `rssiSum`, an `int32_t`, prints as `%d`
   with `static_cast<int>`; the card size, a `uint64_t`, prints through `PRIu64`. The second is
   the latent defect in context item 2; the first is the cost of a rule simple enough to be a
   grep.

5. **The gate's reach is stated, not implied.** It sees every source file whether or not the
   host compiles it — which is the point — and it sees only the specifier. It cannot pair a
   specifier with its argument, so a bare `%u` given a `uint32_t` still relies on `make build`,
   and a `%u` given a `uint64_t` still relies on whichever env compiles it.

## Alternatives rejected

- **Ban `%lu` only**, as first proposed. It covers the three sites that prompted the rule and
  passes the `%llu` one sitting in the tree today.

- **Ban `l` and `ll`, with an exemption for `serial_logger.h`.** Excluding the file would also
  excuse any device-side specifier added to it later; excluding lines by pattern is an
  allow-list that grows. Rewriting four correct lines removes the need for either.

- **A script that tracks `#ifndef NATIVE_BUILD` regions and flags a `long` specifier only
  inside one.** It would leave correct host-compiled sites alone, at the cost of parsing
  nested conditionals, `#if !defined(...)`, and `build_src_filter` to know which whole files
  the host skips — and it would still miss an `#ifdef` inside an argument list, whose specifier
  sits in host-compiled code. The grep catches all four shapes with one pattern.

- **`PRIu32` for every `uint32_t`.** Correct on both platforms, and a reversal of ADR-0006's
  cast-based form across the tree for no defect that form has. Reserved for 64-bit values,
  where no 32-bit cast is honest.

- **Leave invariant 6 at `make test` and `make build` jointly.** That is the enforcement that
  missed four sites; restating it would be claiming a guarantee the tree has measured false.

## Consequences

- Invariant 6 gains a gate that reaches code no compiler in this repo checks from both sides,
  and `make check` runs it, so CI does too.

- A `long` printed deliberately, now or later, is written without a length modifier: through
  a cast to a 32-bit type when it fits, through `PRId64` when it does not, or through
  `std::to_string` in host-only test scaffolding.

- A double-quoted literal in a comment that contains `%lu`, or a `%%lu` meaning a literal
  percent, fails the gate. So does a line whose quotes are unbalanced by a character literal
  and which then takes a modulo by a name starting with `l` — `if (ch == '"') idx = idx % limit;`
  followed by any string on the same line — because the scan cannot tell a `'"'` from the start
  of a literal. All three fail closed and loudly, and none exists in the tree. Removing the
  space from the flag class would silence the third at the cost of letting `"% ld"` through,
  which trades a false red for a false green.

- A specifier split across adjacent literals (`"%l" "u"`), on one line or across two, passes.
  The tree wraps long format literals between fields, where the whole specifier stays on one
  line and is caught, and on 2026-09-17 no line under `src/` or `include/` ended a literal
  partway through a specifier.
