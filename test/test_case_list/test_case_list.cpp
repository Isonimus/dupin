/**
 * @file test_case_list.cpp
 * @brief Host tests for the case menu's row count and scroll window (ADR-0023).
 *
 * Both halves are pure, which is why they live outside main.cpp: the row count is the
 * panel's own arithmetic, and the window is what keeps the selection on a row the
 * renderer actually draws. Before this, the renderer drew the first seven cases and the
 * selection walked past them unseen.
 */

#include <unity.h>
#include <cstdio>
#include <cstring>

#include "storage/session_manager.h"
#include "ui/case_list.h"
#include "ui/display.h"
#include "ui/text_wrap.h"

using namespace dupin;

void setUp(void) {}
void tearDown(void) {}

// ---- Row count: derived from the panel, not chosen -------------------------

void test_every_row_is_drawn_above_the_footer(void) {
    // The last row's text must end above the footer line. The literal 7 case rows put that
    // row at y=123 under a footer at y=120, which is the collision this derivation fixes.
    const int lastRowBottom = kCaseMenuListTopPx
                            + (kCaseMenuRows - 1) * kCaseMenuRowHeightPx
                            + kGlyphHeightPx;
    TEST_ASSERT_TRUE(lastRowBottom <= kCaseMenuFooterTopPx);
}

void test_one_more_row_would_reach_the_footer(void) {
    // The count is the largest that fits, not merely one that fits -- otherwise a smaller
    // number would pass this file while wasting a row of the panel.
    const int oneMoreBottom = kCaseMenuListTopPx
                            + kCaseMenuRows * kCaseMenuRowHeightPx
                            + kGlyphHeightPx;
    TEST_ASSERT_TRUE(oneMoreBottom > kCaseMenuFooterTopPx);
}

void test_the_new_case_action_costs_one_row(void) {
    TEST_ASSERT_EQUAL_INT(kCaseMenuRows - 1, kCaseMenuCaseRows);
    TEST_ASSERT_TRUE(kCaseMenuCaseRows > 0);
}

// ---- The window: the selection is always on a drawn row --------------------

void test_a_list_shorter_than_the_panel_is_drawn_whole(void) {
    const CaseListWindow window = caseListWindow(2, 3, 6);
    TEST_ASSERT_EQUAL_UINT(0, window.first);
    TEST_ASSERT_EQUAL_UINT(3, window.count);
}

void test_the_selection_is_always_inside_the_window(void) {
    // The property the renderer depends on: a highlight bar is drawn only for a row the
    // loop reaches, so a selection outside the window is an invisible one.
    const size_t caseCount = kMaxListedCases;
    const size_t rows = 6;
    for (size_t selected = 0; selected < caseCount; ++selected) {
        const CaseListWindow window = caseListWindow(selected, caseCount, rows);
        char message[64];
        snprintf(message, sizeof(message), "case %u fell outside the window",
                 static_cast<unsigned>(selected));
        TEST_ASSERT_TRUE_MESSAGE(selected >= window.first, message);
        TEST_ASSERT_TRUE_MESSAGE(selected < window.first + window.count, message);
    }
}

void test_the_window_never_exceeds_the_panel(void) {
    const size_t rows = 6;
    for (size_t selected = 0; selected < kMaxListedCases; ++selected) {
        const CaseListWindow window = caseListWindow(selected, kMaxListedCases, rows);
        TEST_ASSERT_TRUE(window.count <= rows);
        TEST_ASSERT_TRUE(window.first + window.count <= kMaxListedCases);
    }
}

void test_pages_turn_on_the_row_boundary(void) {
    // Paged, not centred: the page holds still for six moves and then turns whole.
    TEST_ASSERT_EQUAL_UINT(0, caseListWindow(5, 16, 6).first);
    TEST_ASSERT_EQUAL_UINT(6, caseListWindow(6, 16, 6).first);
    TEST_ASSERT_EQUAL_UINT(6, caseListWindow(11, 16, 6).first);
    TEST_ASSERT_EQUAL_UINT(12, caseListWindow(12, 16, 6).first);
}

void test_the_last_page_is_short_rather_than_padded(void) {
    const CaseListWindow window = caseListWindow(15, 16, 6);
    TEST_ASSERT_EQUAL_UINT(12, window.first);
    TEST_ASSERT_EQUAL_UINT(4, window.count);
}

void test_a_selection_past_the_end_lands_on_the_last_case(void) {
    // A list that shrank under a stale selection must still draw something sane.
    const CaseListWindow window = caseListWindow(99, 16, 6);
    TEST_ASSERT_TRUE(window.count > 0);
    TEST_ASSERT_TRUE(window.first + window.count == 16);
}

void test_an_empty_list_draws_nothing(void) {
    const CaseListWindow window = caseListWindow(0, 0, 6);
    TEST_ASSERT_EQUAL_UINT(0, window.first);
    TEST_ASSERT_EQUAL_UINT(0, window.count);
}

void test_a_panel_with_no_rows_draws_nothing(void) {
    const CaseListWindow window = caseListWindow(3, 16, 0);
    TEST_ASSERT_EQUAL_UINT(0, window.first);
    TEST_ASSERT_EQUAL_UINT(0, window.count);
}

// ---- The title counter fits the panel --------------------------------------

void test_the_counter_ceiling_matches_the_widest_counter(void) {
    // kCaseCounterMaxChars bounds "<first>-<last>/<count>+" for counts up to the cache
    // bound; the title is drawn at the same margin as the notice, so it must fit there too.
    char widest[64];
    snprintf(widest, sizeof(widest), "%u-%u/%u+",
             static_cast<unsigned>(kMaxListedCases - 1),
             static_cast<unsigned>(kMaxListedCases),
             static_cast<unsigned>(kMaxListedCases));
    TEST_ASSERT_EQUAL_UINT(kCaseCounterMaxChars, strlen(widest));
    TEST_ASSERT_TRUE(static_cast<int>(kCaseCounterMaxChars) <= kCaseNoticeMaxChars);
}


// ---- The delete confirmation names the case (ADR-0020) ---------------------

/** Compose the question exactly as the case menu does, for @p caseName. */
static void buildDeletePrompt(char* out, size_t outSize, const char* caseName) {
    char fitted[kMaxSessionNameChars + 1];
    fitText(fitted, sizeof(fitted), caseName, kDeletePromptNameChars);
    snprintf(out, outSize, "%s%s%s", kDeletePromptPrefix, fitted, kDeletePromptSuffix);
}

void test_the_question_still_fits_when_the_name_is_longest(void) {
    // The notice fits whatever it is handed, so a prompt over the ceiling would lose its own
    // tail -- the operator would be asked a question with the answer keys cut off it.
    char longest[kMaxSessionNameChars + 1];
    memset(longest, 'W', kMaxSessionNameChars);
    longest[kMaxSessionNameChars] = '\0';

    char prompt[sizeof(kDeletePromptPrefix) + kMaxSessionNameChars + sizeof(kDeletePromptSuffix)];
    buildDeletePrompt(prompt, sizeof(prompt), longest);

    TEST_ASSERT_EQUAL_INT(kCaseNoticeMaxChars, static_cast<int>(strlen(prompt)));
    TEST_ASSERT_EQUAL_STRING(kDeletePromptSuffix, prompt + strlen(prompt) - (sizeof(kDeletePromptSuffix) - 1));
}

void test_a_shortened_name_still_says_it_was_shortened(void) {
    char longest[kMaxSessionNameChars + 1];
    memset(longest, 'W', kMaxSessionNameChars);
    longest[kMaxSessionNameChars] = '\0';

    char prompt[sizeof(kDeletePromptPrefix) + kMaxSessionNameChars + sizeof(kDeletePromptSuffix)];
    buildDeletePrompt(prompt, sizeof(prompt), longest);

    TEST_ASSERT_NOT_NULL(strstr(prompt, kFitMarker));
    // Room for the marker and something to mark: a budget at or under the marker's own width
    // would replace the name with an ellipsis and say less than nothing.
    TEST_ASSERT_TRUE(kDeletePromptNameChars > kFitMarkerChars);
}

void test_a_name_that_fits_is_asked_about_in_full(void) {
    char prompt[sizeof(kDeletePromptPrefix) + kMaxSessionNameChars + sizeof(kDeletePromptSuffix)];
    buildDeletePrompt(prompt, sizeof(prompt), "kitchen");
    TEST_ASSERT_EQUAL_STRING("Del kitchen? ENTER=yes/any=no", prompt);
}

#ifdef NATIVE_BUILD
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_every_row_is_drawn_above_the_footer);
    RUN_TEST(test_one_more_row_would_reach_the_footer);
    RUN_TEST(test_the_new_case_action_costs_one_row);
    RUN_TEST(test_a_list_shorter_than_the_panel_is_drawn_whole);
    RUN_TEST(test_the_selection_is_always_inside_the_window);
    RUN_TEST(test_the_window_never_exceeds_the_panel);
    RUN_TEST(test_pages_turn_on_the_row_boundary);
    RUN_TEST(test_the_last_page_is_short_rather_than_padded);
    RUN_TEST(test_a_selection_past_the_end_lands_on_the_last_case);
    RUN_TEST(test_an_empty_list_draws_nothing);
    RUN_TEST(test_a_panel_with_no_rows_draws_nothing);
    RUN_TEST(test_the_counter_ceiling_matches_the_widest_counter);
    RUN_TEST(test_the_question_still_fits_when_the_name_is_longest);
    RUN_TEST(test_a_shortened_name_still_says_it_was_shortened);
    RUN_TEST(test_a_name_that_fits_is_asked_about_in_full);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_every_row_is_drawn_above_the_footer);
    RUN_TEST(test_one_more_row_would_reach_the_footer);
    RUN_TEST(test_the_new_case_action_costs_one_row);
    RUN_TEST(test_a_list_shorter_than_the_panel_is_drawn_whole);
    RUN_TEST(test_the_selection_is_always_inside_the_window);
    RUN_TEST(test_the_window_never_exceeds_the_panel);
    RUN_TEST(test_pages_turn_on_the_row_boundary);
    RUN_TEST(test_the_last_page_is_short_rather_than_padded);
    RUN_TEST(test_a_selection_past_the_end_lands_on_the_last_case);
    RUN_TEST(test_an_empty_list_draws_nothing);
    RUN_TEST(test_a_panel_with_no_rows_draws_nothing);
    RUN_TEST(test_the_counter_ceiling_matches_the_widest_counter);
    RUN_TEST(test_the_question_still_fits_when_the_name_is_longest);
    RUN_TEST(test_a_shortened_name_still_says_it_was_shortened);
    RUN_TEST(test_a_name_that_fits_is_asked_about_in_full);

    UNITY_END();
}
void loop() {}
#endif
