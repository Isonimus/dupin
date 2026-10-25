/**
 * @file test_header_row.cpp
 * @brief Host tests for the header row's two fields (ADR-0013).
 *
 * The row carries the open case's name and the scan number, and both used to be placed at
 * literals: the name from x=4 and the number from x=180. Ten characters fit at x=180, so
 * "Scan #" plus a session number past four digits ran off the panel. Right-aligning the
 * number moves it left as it grows, which is where it meets the other field — a full-length
 * case name reaches x=220 on its own. Both halves are arithmetic over the panel's geometry,
 * so both are assertable here rather than on the device.
 */

#include <unity.h>
#include <cstdio>
#include <cstring>

#include "storage/session_manager.h"
#include "ui/display.h"
#include "ui/text_wrap.h"

using namespace dupin;

void setUp(void) {}
void tearDown(void) {}

// The widest text the field can hold: "Scan #" plus the ten digits of UINT32_MAX.
constexpr int kWidestScanChars = 16;

// ---- The scan field stays on the panel -------------------------------------

void test_the_widest_scan_field_ends_at_the_right_margin(void) {
    const int end = rightAlignedX(kWidestScanChars) + kWidestScanChars * kGlyphWidthPx;
    TEST_ASSERT_EQUAL_INT(SCREEN_WIDTH - kPanelMarginPx, end);
}

void test_the_old_fixed_origin_ran_off_the_panel(void) {
    // The defect this closes, stated as arithmetic: at the literal x=180 the widest field
    // ended past the panel and the operator lost the last digits.
    constexpr int kOldScanOriginPx = 180;
    TEST_ASSERT_TRUE(kOldScanOriginPx + kWidestScanChars * kGlyphWidthPx > SCREEN_WIDTH);
}

void test_every_scan_width_starts_inside_the_panel(void) {
    for (int chars = 0; chars <= kWidestScanChars; ++chars) {
        TEST_ASSERT_TRUE(rightAlignedX(chars) >= kPanelMarginPx);
    }
}

// ---- The name never reaches the scan field ---------------------------------

void test_the_name_stops_clear_of_the_scan_field(void) {
    // At every width the number can take, the label plus the fitted name plus the gap must
    // end at or before the number's origin. This is the collision right-alignment would
    // otherwise create: the number moves left as it grows, into the name's space.
    for (int chars = 0; chars <= kWidestScanChars; ++chars) {
        const int nameEnd = kPanelMarginPx
                          + (kHeaderLabelChars + headerNameMaxChars(chars)) * kGlyphWidthPx;
        TEST_ASSERT_TRUE(nameEnd + kHeaderFieldGapChars * kGlyphWidthPx <= rightAlignedX(chars));
    }
}

void test_a_wider_scan_field_leaves_a_shorter_name(void) {
    TEST_ASSERT_TRUE(headerNameMaxChars(kWidestScanChars) < headerNameMaxChars(0));
}

void test_a_full_length_name_is_whole_when_no_scan_number_is_drawn(void) {
    // A case with no scan number yet must show its name in full: shortening a name the row
    // has room for would report a truncation that never happened.
    TEST_ASSERT_TRUE(headerNameMaxChars(0) >= static_cast<int>(kMaxSessionNameChars));

    char longest[kMaxSessionNameChars + 1];
    memset(longest, 'W', kMaxSessionNameChars);
    longest[kMaxSessionNameChars] = '\0';

    char fitted[kMaxSessionNameChars + 1];
    fitText(fitted, sizeof(fitted), longest, headerNameMaxChars(0));
    TEST_ASSERT_EQUAL_STRING(longest, fitted);
}

void test_a_full_length_name_is_marked_when_the_scan_number_is_widest(void) {
    char longest[kMaxSessionNameChars + 1];
    memset(longest, 'W', kMaxSessionNameChars);
    longest[kMaxSessionNameChars] = '\0';

    char fitted[kMaxSessionNameChars + 1];
    fitText(fitted, sizeof(fitted), longest, headerNameMaxChars(kWidestScanChars));

    TEST_ASSERT_EQUAL_INT(headerNameMaxChars(kWidestScanChars),
                          static_cast<int>(strlen(fitted)));
    // Shortened, and saying so -- a silently cut name reads as a real one (ADR-0013).
    TEST_ASSERT_EQUAL_STRING(kFitMarker, fitted + strlen(fitted) - kFitMarkerChars);
}

void test_the_scan_buffer_holds_the_widest_field(void) {
    // The same sizing the renderer uses, asserted against what snprintf actually writes.
    char scanText[sizeof("Scan #") + 10];
    const int written = snprintf(scanText, sizeof(scanText), "Scan #%u", 4294967295u);
    TEST_ASSERT_EQUAL_INT(kWidestScanChars, written);
    TEST_ASSERT_EQUAL_STRING("Scan #4294967295", scanText);
}

#ifdef NATIVE_BUILD
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_the_widest_scan_field_ends_at_the_right_margin);
    RUN_TEST(test_the_old_fixed_origin_ran_off_the_panel);
    RUN_TEST(test_every_scan_width_starts_inside_the_panel);
    RUN_TEST(test_the_name_stops_clear_of_the_scan_field);
    RUN_TEST(test_a_wider_scan_field_leaves_a_shorter_name);
    RUN_TEST(test_a_full_length_name_is_whole_when_no_scan_number_is_drawn);
    RUN_TEST(test_a_full_length_name_is_marked_when_the_scan_number_is_widest);
    RUN_TEST(test_the_scan_buffer_holds_the_widest_field);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_the_widest_scan_field_ends_at_the_right_margin);
    RUN_TEST(test_the_old_fixed_origin_ran_off_the_panel);
    RUN_TEST(test_every_scan_width_starts_inside_the_panel);
    RUN_TEST(test_the_name_stops_clear_of_the_scan_field);
    RUN_TEST(test_a_wider_scan_field_leaves_a_shorter_name);
    RUN_TEST(test_a_full_length_name_is_whole_when_no_scan_number_is_drawn);
    RUN_TEST(test_a_full_length_name_is_marked_when_the_scan_number_is_widest);
    RUN_TEST(test_the_scan_buffer_holds_the_widest_field);
    UNITY_END();
}
void loop() {}
#endif
