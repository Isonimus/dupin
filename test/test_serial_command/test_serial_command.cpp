/**
 * @file test_serial_command.cpp
 * @brief Host tests for the serial control channel's parser (ADR-0014).
 *
 * The parser is the host-testable half of the channel, split out for exactly that reason
 * (ADR-0002 invariant 1). What it cannot cover -- that an injected key reaches the real
 * dispatch on real hardware -- is what scripts/input-verify.mjs exists for.
 */

#include <unity.h>
#include <cstring>
#include <string>

#include "ui/serial_command.h"

using namespace dupin;

void setUp(void) {}
void tearDown(void) {}

// ---- parseCommand: the pure half -------------------------------------------

void test_parses_ping(void) {
    TEST_ASSERT_TRUE(parseCommand("ping").kind == CommandKind::Ping);
}

void test_parses_state(void) {
    TEST_ASSERT_TRUE(parseCommand("state").kind == CommandKind::State);
}

void test_parses_probe(void) {
    TEST_ASSERT_TRUE(parseCommand("probe").kind == CommandKind::Probe);
}

void test_probe_is_matched_exactly(void) {
    // Same rule as ping and state: a near miss is refused rather than guessed at, so a
    // probe run cannot report figures it was never asked for.
    TEST_ASSERT_TRUE(parseCommand("probes").kind == CommandKind::Unknown);
    TEST_ASSERT_TRUE(parseCommand("PROBE").kind == CommandKind::Unknown);
    TEST_ASSERT_TRUE(parseCommand("prob").kind == CommandKind::Unknown);
}

void test_parses_key_with_its_argument(void) {
    Command c = parseCommand("key w");
    TEST_ASSERT_TRUE(c.kind == CommandKind::Key);
    TEST_ASSERT_EQUAL_CHAR('w', c.key);
}

void test_surrounding_spaces_are_ignored(void) {
    TEST_ASSERT_TRUE(parseCommand("  ping  ").kind == CommandKind::Ping);
    Command c = parseCommand("  key   m   ");
    TEST_ASSERT_TRUE(c.kind == CommandKind::Key);
    TEST_ASSERT_EQUAL_CHAR('m', c.key);
}

void test_blank_line_is_not_an_error(void) {
    TEST_ASSERT_TRUE(parseCommand("").kind == CommandKind::None);
    TEST_ASSERT_TRUE(parseCommand("     ").kind == CommandKind::None);
    TEST_ASSERT_TRUE(parseCommand(nullptr).kind == CommandKind::None);
}

void test_unrecognised_line_is_unknown(void) {
    TEST_ASSERT_TRUE(parseCommand("reboot").kind == CommandKind::Unknown);
    TEST_ASSERT_TRUE(parseCommand("pingg").kind == CommandKind::Unknown);
    // Matching is exact and lower-case on purpose: a gate that guesses at intent can pass
    // for the wrong reason.
    TEST_ASSERT_TRUE(parseCommand("PING").kind == CommandKind::Unknown);
}

void test_key_without_exactly_one_argument_is_unknown(void) {
    // "key wm" must not dispatch 'w'. Guessing here would press a key nobody named.
    TEST_ASSERT_TRUE(parseCommand("key").kind == CommandKind::Unknown);
    TEST_ASSERT_TRUE(parseCommand("key ").kind == CommandKind::Unknown);
    TEST_ASSERT_TRUE(parseCommand("key wm").kind == CommandKind::Unknown);
}

void test_parses_named_keys(void) {
    // Enter, del and tab have no printable spelling, so the case menu is unreachable
    // without them (ADR-0014).
    TEST_ASSERT_EQUAL_CHAR(kKeyEnter, parseCommand("key enter").key);
    TEST_ASSERT_EQUAL_CHAR(kKeyDel, parseCommand("key del").key);
    TEST_ASSERT_EQUAL_CHAR(kKeyTab, parseCommand("key tab").key);
    TEST_ASSERT_TRUE(parseCommand("key enter").kind == CommandKind::Key);
}

void test_a_name_that_is_not_a_key_is_unknown(void) {
    TEST_ASSERT_TRUE(parseCommand("key escape").kind == CommandKind::Unknown);
    TEST_ASSERT_TRUE(parseCommand("key ent").kind == CommandKind::Unknown);
}

// ---- CommandReader: accumulation -------------------------------------------

static bool feedLine(CommandReader& reader, const char* text, Command& out) {
    bool complete = false;
    for (const char* p = text; *p != '\0'; ++p) {
        complete = reader.feed(*p, out);
    }
    return complete;
}

void test_reader_completes_a_line_only_at_the_newline(void) {
    CommandReader reader;
    Command out;
    TEST_ASSERT_FALSE(feedLine(reader, "ping", out));
    TEST_ASSERT_TRUE(reader.feed('\n', out));
    TEST_ASSERT_TRUE(out.kind == CommandKind::Ping);
}

void test_reader_ignores_carriage_returns(void) {
    CommandReader reader;
    Command out;
    TEST_ASSERT_TRUE(feedLine(reader, "state\r\n", out));
    TEST_ASSERT_TRUE(out.kind == CommandKind::State);
}

void test_reader_resets_between_lines(void) {
    CommandReader reader;
    Command out;
    TEST_ASSERT_TRUE(feedLine(reader, "ping\n", out));
    TEST_ASSERT_TRUE(out.kind == CommandKind::Ping);
    TEST_ASSERT_TRUE(feedLine(reader, "key b\n", out));
    TEST_ASSERT_TRUE(out.kind == CommandKind::Key);
    TEST_ASSERT_EQUAL_CHAR('b', out.key);
}

void test_overlong_line_is_refused_not_truncated(void) {
    // The one that matters. "key w" followed by enough padding to pass the bound must not
    // arrive as a valid "key w" -- executing the prefix of a line the sender never
    // completed would press a key on their behalf (ADR-0014).
    CommandReader reader;
    Command out;
    std::string flood = "key w";
    flood.append(kMaxCommandChars + 10, 'x');
    flood.push_back('\n');
    TEST_ASSERT_TRUE(feedLine(reader, flood.c_str(), out));
    TEST_ASSERT_TRUE(out.kind == CommandKind::TooLong);
}

void test_reader_recovers_after_an_overlong_line(void) {
    CommandReader reader;
    Command out;
    std::string flood(kMaxCommandChars + 5, 'z');
    flood.push_back('\n');
    feedLine(reader, flood.c_str(), out);
    TEST_ASSERT_TRUE(feedLine(reader, "ping\n", out));
    TEST_ASSERT_TRUE(out.kind == CommandKind::Ping);
}

void test_reader_accepts_a_line_exactly_at_the_bound(void) {
    CommandReader reader;
    Command out;
    std::string exact = "key ";
    exact.append(kMaxCommandChars - exact.size() - 1, ' ');
    exact.push_back('q');
    TEST_ASSERT_EQUAL_INT((int)kMaxCommandChars, (int)exact.size());
    exact.push_back('\n');
    TEST_ASSERT_TRUE(feedLine(reader, exact.c_str(), out));
    TEST_ASSERT_TRUE(out.kind == CommandKind::Key);
    TEST_ASSERT_EQUAL_CHAR('q', out.key);
}

void test_bare_newline_is_none(void) {
    CommandReader reader;
    Command out;
    TEST_ASSERT_TRUE(reader.feed('\n', out));
    TEST_ASSERT_TRUE(out.kind == CommandKind::None);
}

// ---- CommandReader: what a refusal reports ---------------------------------

/** @brief Feed @p length bytes, NULs included, which feedLine's C string cannot carry. */
static bool feedBytes(CommandReader& reader, const char* bytes, size_t length, Command& out) {
    bool complete = false;
    for (size_t i = 0; i < length; ++i) {
        complete = reader.feed(bytes[i], out);
    }
    return complete;
}

void test_reader_refuses_a_line_with_a_nul_byte(void) {
    // The parser reads a C string, so a NUL would end the line early: "key w<NUL>zz" would
    // press the w its sender never finished asking for, the hazard TooLong exists to refuse.
    CommandReader reader;
    Command out;
    const char line[] = {'k', 'e', 'y', ' ', 'w', '\0', 'z', 'z', '\n'};
    TEST_ASSERT_TRUE(feedBytes(reader, line, sizeof(line), out));
    TEST_ASSERT_TRUE(out.kind == CommandKind::Unknown);
}

void test_escaped_line_names_what_was_refused(void) {
    CommandReader reader;
    Command out;
    char escaped[kEscapedCommandSize];
    TEST_ASSERT_TRUE(feedLine(reader, "reboot now \n", out));
    reader.escapedLine(escaped);
    TEST_ASSERT_EQUAL_STRING("reboot now ", escaped);
}

void test_escaped_line_makes_every_unprintable_byte_visible(void) {
    // A stray writer's bytes are the evidence; a NUL, a control code or a high byte printed
    // raw would vanish from the transcript or garble it (ADR-0032).
    CommandReader reader;
    Command out;
    char escaped[kEscapedCommandSize];
    const char line[] = {'a', '\0', '\x01', '\\', '\x7f', '\xff', '\t', '\n'};
    TEST_ASSERT_TRUE(feedBytes(reader, line, sizeof(line), out));
    reader.escapedLine(escaped);
    TEST_ASSERT_EQUAL_STRING("a\\x00\\x01\\\\\\x7f\\xff\\x09", escaped);
}

void test_escaped_line_of_an_overlong_line_is_what_was_kept(void) {
    CommandReader reader;
    Command out;
    char escaped[kEscapedCommandSize];
    std::string flood(kMaxCommandChars, 'k');
    flood.append("dropped\n");
    TEST_ASSERT_TRUE(feedLine(reader, flood.c_str(), out));
    TEST_ASSERT_TRUE(out.kind == CommandKind::TooLong);
    reader.escapedLine(escaped);
    TEST_ASSERT_EQUAL_STRING(std::string(kMaxCommandChars, 'k').c_str(), escaped);
}

void test_escaped_line_fits_a_full_line_of_the_widest_bytes(void) {
    // The buffer is derived from the bound, so the worst line must fill it exactly.
    CommandReader reader;
    Command out;
    char escaped[kEscapedCommandSize];
    std::string widest(kMaxCommandChars, '\xff');
    widest.push_back('\n');
    TEST_ASSERT_TRUE(feedLine(reader, widest.c_str(), out));
    reader.escapedLine(escaped);
    TEST_ASSERT_EQUAL_size_t(kEscapedCommandSize - 1, strlen(escaped));
}

#ifdef NATIVE_BUILD
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_parses_ping);
    RUN_TEST(test_parses_state);
    RUN_TEST(test_parses_probe);
    RUN_TEST(test_probe_is_matched_exactly);
    RUN_TEST(test_parses_key_with_its_argument);
    RUN_TEST(test_surrounding_spaces_are_ignored);
    RUN_TEST(test_blank_line_is_not_an_error);
    RUN_TEST(test_unrecognised_line_is_unknown);
    RUN_TEST(test_key_without_exactly_one_argument_is_unknown);
    RUN_TEST(test_parses_named_keys);
    RUN_TEST(test_a_name_that_is_not_a_key_is_unknown);
    RUN_TEST(test_reader_completes_a_line_only_at_the_newline);
    RUN_TEST(test_reader_ignores_carriage_returns);
    RUN_TEST(test_reader_resets_between_lines);
    RUN_TEST(test_overlong_line_is_refused_not_truncated);
    RUN_TEST(test_reader_recovers_after_an_overlong_line);
    RUN_TEST(test_reader_accepts_a_line_exactly_at_the_bound);
    RUN_TEST(test_bare_newline_is_none);
    RUN_TEST(test_reader_refuses_a_line_with_a_nul_byte);
    RUN_TEST(test_escaped_line_names_what_was_refused);
    RUN_TEST(test_escaped_line_makes_every_unprintable_byte_visible);
    RUN_TEST(test_escaped_line_of_an_overlong_line_is_what_was_kept);
    RUN_TEST(test_escaped_line_fits_a_full_line_of_the_widest_bytes);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_parses_ping);
    RUN_TEST(test_parses_state);
    RUN_TEST(test_parses_probe);
    RUN_TEST(test_probe_is_matched_exactly);
    RUN_TEST(test_parses_key_with_its_argument);
    RUN_TEST(test_surrounding_spaces_are_ignored);
    RUN_TEST(test_blank_line_is_not_an_error);
    RUN_TEST(test_unrecognised_line_is_unknown);
    RUN_TEST(test_key_without_exactly_one_argument_is_unknown);
    RUN_TEST(test_parses_named_keys);
    RUN_TEST(test_a_name_that_is_not_a_key_is_unknown);
    RUN_TEST(test_reader_completes_a_line_only_at_the_newline);
    RUN_TEST(test_reader_ignores_carriage_returns);
    RUN_TEST(test_reader_resets_between_lines);
    RUN_TEST(test_overlong_line_is_refused_not_truncated);
    RUN_TEST(test_reader_recovers_after_an_overlong_line);
    RUN_TEST(test_reader_accepts_a_line_exactly_at_the_bound);
    RUN_TEST(test_bare_newline_is_none);
    RUN_TEST(test_reader_refuses_a_line_with_a_nul_byte);
    RUN_TEST(test_escaped_line_names_what_was_refused);
    RUN_TEST(test_escaped_line_makes_every_unprintable_byte_visible);
    RUN_TEST(test_escaped_line_of_an_overlong_line_is_what_was_kept);
    RUN_TEST(test_escaped_line_fits_a_full_line_of_the_widest_bytes);
    UNITY_END();
}
void loop() {}
#endif
