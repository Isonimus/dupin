/**
 * @file test_text_wrap.cpp
 * @brief Host tests for the word-wrap logic and the Canvas render seam (ADR-0005).
 *
 * Two halves, per ADR-0005: wrapText() asserted canvas-free and deterministic,
 * and printWrapped() asserted through the recording Canvas stub — the pair proves
 * both the algorithm and that the native stub actually carries rendered output.
 */

#include <unity.h>
#include <cstring>
#include <string>
#include <vector>

#include "ui/text_wrap.h"
#include "ui/display.h"

using namespace dupin;

void setUp(void) {}
void tearDown(void) {}

// Collect wrapText's output for assertion.
static std::vector<std::string> wrapToVec(const char* text, int maxCharsPerLine) {
    std::vector<std::string> out;
    wrapText(text, maxCharsPerLine, [&](const char* line) { out.emplace_back(line); });
    return out;
}

// ---- wrapText: the pure algorithm ------------------------------------------

void test_text_shorter_than_limit_is_one_line(void) {
    auto lines = wrapToVec("hello world", 40);
    TEST_ASSERT_EQUAL_INT(1, lines.size());
    TEST_ASSERT_EQUAL_STRING("hello world", lines[0].c_str());
}

void test_wraps_greedily_at_word_boundary(void) {
    // "hello"(5); "world" would make 11 > 10 -> break; "foo" fits on "world foo".
    auto lines = wrapToVec("hello world foo", 10);
    TEST_ASSERT_EQUAL_INT(2, lines.size());
    TEST_ASSERT_EQUAL_STRING("hello", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("world foo", lines[1].c_str());
}

void test_runs_of_spaces_collapse(void) {
    auto lines = wrapToVec("a   b", 10);
    TEST_ASSERT_EQUAL_INT(1, lines.size());
    TEST_ASSERT_EQUAL_STRING("a b", lines[0].c_str());
}

void test_word_longer_than_limit_is_emitted_whole(void) {
    // A word that exceeds the per-line char budget is never split mid-word.
    auto lines = wrapToVec("abcdefgh xy", 4);
    TEST_ASSERT_EQUAL_INT(2, lines.size());
    TEST_ASSERT_EQUAL_STRING("abcdefgh", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("xy", lines[1].c_str());
}

void test_empty_string_emits_nothing(void) {
    TEST_ASSERT_EQUAL_INT(0, wrapToVec("", 10).size());
}

void test_null_text_emits_nothing(void) {
    TEST_ASSERT_EQUAL_INT(0, wrapToVec(nullptr, 10).size());
}

void test_nonpositive_width_emits_nothing(void) {
    TEST_ASSERT_EQUAL_INT(0, wrapToVec("hello", 0).size());
}

void test_oversized_word_cannot_overflow_line_buffer(void) {
    // A 60-char word with no spaces cannot fit the buffer, so it is emitted in
    // buffer-sized pieces — the point is that no emitted line ever exceeds
    // kMaxLineChars. This is the regression for the old strcat-into-line[50],
    // which had no such bound.
    std::string big(60, 'x');
    auto lines = wrapToVec(big.c_str(), 4);
    TEST_ASSERT_GREATER_THAN_INT(0, lines.size());
    for (const auto& line : lines) {
        TEST_ASSERT_TRUE(line.size() <= (size_t)kMaxLineChars);
    }
}

void test_wide_limit_does_not_drop_words(void) {
    // A maxCharsPerLine wider than the line buffer must still wrap (bounded by
    // kMaxLineChars) rather than silently dropping the words that overflow the
    // first buffer. Regression for the dead zone between maxCharsPerLine and
    // kMaxLineChars: the break decision fired against maxCharsPerLine while the
    // copy guards clamped at kMaxLineChars, so words past the cap vanished.
    std::string text;
    for (int i = 0; i < 40; ++i) {
        if (i) text += ' ';
        text += 'a';
    }
    auto lines = wrapToVec(text.c_str(), 100);  // 100 > kMaxLineChars (48)

    int emittedWords = 0;
    for (const auto& line : lines) {
        TEST_ASSERT_TRUE(line.size() <= (size_t)kMaxLineChars);
        for (char c : line) {
            if (c == 'a') ++emittedWords;
        }
    }
    TEST_ASSERT_EQUAL_INT(40, emittedWords);  // every word survives the wrap
}

// ---- printWrapped: the Canvas render seam ----------------------------------

void test_printwrapped_records_each_line_on_the_canvas(void) {
    Canvas canvas;  // native recording stub
    // maxWidth 24 / 6px approx = 4 chars per line.
    int endY = printWrapped(canvas, 10, 20, "one two three", 24, 12);

    const auto& lines = canvas.lines();
    TEST_ASSERT_EQUAL_INT(3, lines.size());
    TEST_ASSERT_EQUAL_STRING("one", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("two", lines[1].c_str());
    TEST_ASSERT_EQUAL_STRING("three", lines[2].c_str());
    // y advanced by lineHeight per emitted line.
    TEST_ASSERT_EQUAL_INT(20 + 3 * 12, endY);
}

void test_printwrapped_empty_text_renders_nothing(void) {
    Canvas canvas;
    int endY = printWrapped(canvas, 10, 20, "", 120, 12);
    TEST_ASSERT_EQUAL_INT(0, canvas.lines().size());
    TEST_ASSERT_EQUAL_INT(20, endY);
}


// ---- fitText: shortening for a fixed-width renderer -------------------------

void test_fit_text_shorter_than_limit_is_copied_verbatim(void) {
    char dst[32];
    fitText(dst, sizeof(dst), "Case: Home", 18);
    TEST_ASSERT_EQUAL_STRING("Case: Home", dst);
}

void test_fit_text_at_exactly_the_limit_is_not_marked(void) {
    char dst[32];
    fitText(dst, sizeof(dst), "123456789012345678", 18);  // exactly 18
    TEST_ASSERT_EQUAL_STRING("123456789012345678", dst);
}

void test_fit_text_one_over_the_limit_is_marked(void) {
    char dst[32];
    fitText(dst, sizeof(dst), "1234567890123456789", 18);  // 19
    // 15 kept + "..." == 18, and the marker is what distinguishes this from a name
    // that genuinely ends there.
    TEST_ASSERT_EQUAL_STRING("123456789012345...", dst);
    TEST_ASSERT_EQUAL_INT(18, (int)strlen(dst));
}

void test_fit_text_output_never_exceeds_the_limit(void) {
    // The property the toast depends on: whatever comes in, the rendered width is
    // bounded. A regression in the marker arithmetic shows up here rather than as a
    // box drawn off the panel.
    char src[80];
    char dst[64];
    for (int len = 0; len < 70; ++len) {
        for (int i = 0; i < len; ++i) src[i] = 'A';
        src[len] = '\0';
        fitText(dst, sizeof(dst), src, kToastMaxChars);
        TEST_ASSERT_TRUE((int)strlen(dst) <= kToastMaxChars);
    }
}

void test_fit_text_honours_the_destination_buffer(void) {
    // maxChars wider than the buffer must not overflow it: the tighter ceiling wins.
    char dst[8] = {0};
    fitText(dst, sizeof(dst), "abcdefghijklmnop", 40);
    TEST_ASSERT_EQUAL_INT(7, (int)strlen(dst));
    TEST_ASSERT_EQUAL_STRING("abcd...", dst);
}

void test_fit_text_without_room_for_the_marker_keeps_characters(void) {
    char dst[8];
    fitText(dst, sizeof(dst), "abcdef", 3);
    TEST_ASSERT_EQUAL_STRING("abc", dst);
}

void test_fit_text_reads_no_further_than_the_bound(void) {
    // An unterminated source: fitText must stop at the bound rather than run to the
    // first NUL somewhere past the field.
    char src[64];
    memset(src, 'A', sizeof(src));  // deliberately no terminator
    char dst[24];
    fitText(dst, sizeof(dst), src, kToastMaxChars);
    TEST_ASSERT_EQUAL_INT(kToastMaxChars, (int)strlen(dst));
}

void test_fit_text_null_and_degenerate_inputs_are_empty(void) {
    char dst[16];
    fitText(dst, sizeof(dst), nullptr, 10);
    TEST_ASSERT_EQUAL_STRING("", dst);
    fitText(dst, sizeof(dst), "abc", 0);
    TEST_ASSERT_EQUAL_STRING("", dst);
    fitText(dst, sizeof(dst), "abc", -1);
    TEST_ASSERT_EQUAL_STRING("", dst);
}

// ---- the toast ceiling itself ----------------------------------------------

void test_toast_ceiling_is_the_exact_panel_boundary(void) {
    // main.cpp draws the box as textLen * kToastGlyphWidthPx + kToastBoxPaddingPx,
    // centred with (SCREEN_WIDTH - boxW) / 2. Asserting both directions is what makes
    // kToastMaxChars a measured boundary rather than a guess that happens to hold:
    // at the ceiling the box is on the panel, one character past it the origin is
    // negative and the box is drawn off the left edge (ADR-0013).
    const int atCeiling = kToastMaxChars * kToastGlyphWidthPx + kToastBoxPaddingPx;
    TEST_ASSERT_TRUE(atCeiling <= SCREEN_WIDTH);
    TEST_ASSERT_TRUE((SCREEN_WIDTH - atCeiling) / 2 >= 0);

    const int overCeiling = (kToastMaxChars + 1) * kToastGlyphWidthPx + kToastBoxPaddingPx;
    TEST_ASSERT_TRUE(overCeiling > SCREEN_WIDTH);
}

#ifdef NATIVE_BUILD
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_text_shorter_than_limit_is_one_line);
    RUN_TEST(test_wraps_greedily_at_word_boundary);
    RUN_TEST(test_runs_of_spaces_collapse);
    RUN_TEST(test_word_longer_than_limit_is_emitted_whole);
    RUN_TEST(test_empty_string_emits_nothing);
    RUN_TEST(test_null_text_emits_nothing);
    RUN_TEST(test_nonpositive_width_emits_nothing);
    RUN_TEST(test_oversized_word_cannot_overflow_line_buffer);
    RUN_TEST(test_wide_limit_does_not_drop_words);
    RUN_TEST(test_printwrapped_records_each_line_on_the_canvas);
    RUN_TEST(test_printwrapped_empty_text_renders_nothing);
    RUN_TEST(test_fit_text_shorter_than_limit_is_copied_verbatim);
    RUN_TEST(test_fit_text_at_exactly_the_limit_is_not_marked);
    RUN_TEST(test_fit_text_one_over_the_limit_is_marked);
    RUN_TEST(test_fit_text_output_never_exceeds_the_limit);
    RUN_TEST(test_fit_text_honours_the_destination_buffer);
    RUN_TEST(test_fit_text_without_room_for_the_marker_keeps_characters);
    RUN_TEST(test_fit_text_reads_no_further_than_the_bound);
    RUN_TEST(test_fit_text_null_and_degenerate_inputs_are_empty);
    RUN_TEST(test_toast_ceiling_is_the_exact_panel_boundary);

    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_text_shorter_than_limit_is_one_line);
    RUN_TEST(test_wraps_greedily_at_word_boundary);
    RUN_TEST(test_runs_of_spaces_collapse);
    RUN_TEST(test_word_longer_than_limit_is_emitted_whole);
    RUN_TEST(test_empty_string_emits_nothing);
    RUN_TEST(test_null_text_emits_nothing);
    RUN_TEST(test_nonpositive_width_emits_nothing);
    RUN_TEST(test_oversized_word_cannot_overflow_line_buffer);
    RUN_TEST(test_wide_limit_does_not_drop_words);
    RUN_TEST(test_printwrapped_records_each_line_on_the_canvas);
    RUN_TEST(test_printwrapped_empty_text_renders_nothing);
    RUN_TEST(test_fit_text_shorter_than_limit_is_copied_verbatim);
    RUN_TEST(test_fit_text_at_exactly_the_limit_is_not_marked);
    RUN_TEST(test_fit_text_one_over_the_limit_is_marked);
    RUN_TEST(test_fit_text_output_never_exceeds_the_limit);
    RUN_TEST(test_fit_text_honours_the_destination_buffer);
    RUN_TEST(test_fit_text_without_room_for_the_marker_keeps_characters);
    RUN_TEST(test_fit_text_reads_no_further_than_the_bound);
    RUN_TEST(test_fit_text_null_and_degenerate_inputs_are_empty);
    RUN_TEST(test_toast_ceiling_is_the_exact_panel_boundary);

    UNITY_END();
}
void loop() {}
#endif
