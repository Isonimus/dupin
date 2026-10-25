/**
 * @file serial_command.h
 * @brief Line parser for the serial control channel (ADR-0014).
 *
 * The firmware was write-only over serial, so a device gate could assert what it
 * volunteered and nothing more (ADR-0009). This is the reading half, split the same way
 * ui/text_wrap is: parsing is pure and host-tested here, and main.cpp holds the part that
 * touches the port and the display.
 *
 * The vocabulary is deliberately small and, for `key`, deliberately not a new capability:
 * an injected key runs the same dispatch a physical keypress runs, so the channel exposes
 * nothing the operator could not already do at the keyboard (CLAUDE.md §4, invariant 14).
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace dupin {

// Longest command line accepted, excluding the terminator. Nothing in the vocabulary comes
// close; the bound exists so a flood or a wedged sender cannot grow the buffer, and a line
// past it is refused rather than truncated -- truncating "key wxyz" into a valid "key w"
// would execute something the sender did not ask for.
constexpr size_t kMaxCommandChars = 32;

// A refused line is echoed with each byte a transcript could misread spelled `\xNN`, so the
// widest a byte can become is four characters, and the echo buffer is derived from both.
constexpr size_t kMaxEscapedByteChars = 4;
constexpr size_t kEscapedCommandSize = kMaxCommandChars * kMaxEscapedByteChars + 1;

// Control codes the named `key` arguments resolve to. These are the codes the firmware's
// own handlers test, so `key enter` is the Enter its dispatch reads rather than a synonym
// for it. `del` is the Cardputer's del key, which the firmware reads as ESC in the case
// menu and as backspace while typing -- one physical key with two roles, named here after
// the key rather than after either role (ADR-0014).
constexpr char kKeyEnter = '\r';
constexpr char kKeyDel = '\x1b';
constexpr char kKeyTab = '\t';

enum class CommandKind : uint8_t {
    None,     ///< Blank line. Not an error; a bare newline is how a terminal user probes.
    Ping,     ///< Liveness.
    State,    ///< Report observable state.
    Probe,    ///< Report the concurrency figures a design decision needs (ADR-0006 probe).
    Key,      ///< Inject a keypress. Command::key carries it.
    Unknown,  ///< A line that is not a command.
    TooLong,  ///< A line past kMaxCommandChars, refused rather than truncated.
};

struct Command {
    CommandKind kind = CommandKind::None;
    char key = '\0';  ///< Valid only when kind == CommandKind::Key.
};

/**
 * @brief Parse one complete line. Pure: no port, no display, no allocation.
 *
 * Accepts `ping`, `state`, and `key <arg>`, where <arg> is a single character or one of
 * the names `enter`, `del`, `tab` for the keys no printable character can express. Leading
 * and trailing spaces are ignored; everything else is CommandKind::Unknown. Matching is
 * exact and lower-case, because the caller is a script rather than a person and a gate that
 * guesses at intent is a gate that can pass for the wrong reason.
 */
Command parseCommand(const char* line);

/**
 * @brief Accumulates bytes into lines and parses each one.
 *
 * feed() returns true exactly when @p out has been filled, and resets itself in the same
 * step -- there is no "remember to clear the buffer" for a caller to forget. `\r` is
 * ignored so a CRLF terminal behaves like a bare-LF script.
 */
class CommandReader {
public:
    /** @brief Consume one byte. Returns true when a line completed and @p out was filled. */
    bool feed(char c, Command& out);

    /**
     * @brief The last completed line as it arrived, for a refusal to name what it refused.
     *
     * Printable ASCII passes through, a backslash becomes `\\` and every other byte `\xNN`,
     * NULs included, so the echo says exactly which bytes came in. Something unidentified
     * writes to this port around an attach (ADR-0032), and an error that drops the line it
     * rejected cannot help find it. A TooLong line is its first kMaxCommandChars bytes, the
     * part that was kept. Empty before any line completes.
     */
    void escapedLine(char (&out)[kEscapedCommandSize]) const;

private:
    char m_line[kMaxCommandChars + 1] = {0};
    size_t m_len = 0;
    // The bytes of the last completed line. Counted rather than terminated, because a NUL
    // is one of the bytes it has to be able to report.
    char m_completed[kMaxCommandChars] = {0};
    size_t m_completedLen = 0;
    bool m_overflowed = false;
};

} // namespace dupin
