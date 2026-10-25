/**
 * @file test_session_manager.cpp
 * @brief Unit tests for SessionManager
 */

#include <unity.h>
#include <cinttypes>
#include <cstring>
#include "storage/session_manager.h"
#include "core/baseline.h"
#include "core/anomaly.h"
#include "ui/display.h"  // kCaseNoticeMaxChars

using namespace dupin;

void setUp(void) {}
void tearDown(void) {}

// ============================================
// Name Validation Tests
// ============================================

void test_valid_name_alphanumeric(void) {
    TEST_ASSERT_TRUE(SessionManager::isValidName("Home"));
    TEST_ASSERT_TRUE(SessionManager::isValidName("office"));
    TEST_ASSERT_TRUE(SessionManager::isValidName("Airport2024"));
    TEST_ASSERT_TRUE(SessionManager::isValidName("my_network"));
    TEST_ASSERT_TRUE(SessionManager::isValidName("a"));
    TEST_ASSERT_TRUE(SessionManager::isValidName("A123_test"));
}

void test_invalid_name_empty(void) {
    TEST_ASSERT_FALSE(SessionManager::isValidName(""));
    TEST_ASSERT_FALSE(SessionManager::isValidName(nullptr));
}

void test_invalid_name_special_chars(void) {
    TEST_ASSERT_FALSE(SessionManager::isValidName("my-network"));
    TEST_ASSERT_FALSE(SessionManager::isValidName("my network"));
    TEST_ASSERT_FALSE(SessionManager::isValidName("home/office"));
    TEST_ASSERT_FALSE(SessionManager::isValidName("café"));
    TEST_ASSERT_FALSE(SessionManager::isValidName("test.log"));
    TEST_ASSERT_FALSE(SessionManager::isValidName("hello!"));
}

void test_invalid_name_too_long(void) {
    // 31 characters - should fail
    TEST_ASSERT_FALSE(SessionManager::isValidName(
        "abcdefghijklmnopqrstuvwxyz12345"));
}

void test_valid_name_max_length(void) {
    // 30 characters - should pass
    TEST_ASSERT_TRUE(SessionManager::isValidName(
        "abcdefghijklmnopqrstuvwxyz1234"));
}

// ============================================
// Session Manager Lifecycle (Mock SD)
// ============================================

void test_session_manager_begin_native(void) {
    SessionManager mgr;
    
    // In native build, begin() should succeed (mock)
    TEST_ASSERT_TRUE(mgr.begin());
    TEST_ASSERT_TRUE(mgr.isAvailable());
    
    mgr.end();
    TEST_ASSERT_FALSE(mgr.isAvailable());
}

void test_session_manager_no_session_initially(void) {
    SessionManager mgr;
    mgr.begin();
    
    TEST_ASSERT_FALSE(mgr.hasSession());
    TEST_ASSERT_EQUAL_STRING("", mgr.currentSessionName());
    
    mgr.end();
}

void test_session_manager_load_sets_current(void) {
    SessionManager mgr;
    mgr.begin();
    
    // In native build, load always succeeds
    TEST_ASSERT_TRUE(mgr.loadSession("test_session"));
    TEST_ASSERT_TRUE(mgr.hasSession());
    TEST_ASSERT_EQUAL_STRING("test_session", mgr.currentSessionName());
    
    mgr.end();
}

void test_session_manager_load_rejects_invalid_name(void) {
    SessionManager mgr;
    mgr.begin();
    
    TEST_ASSERT_FALSE(mgr.loadSession("invalid-name"));
    TEST_ASSERT_FALSE(mgr.loadSession(""));
    TEST_ASSERT_FALSE(mgr.loadSession(nullptr));
    TEST_ASSERT_FALSE(mgr.hasSession());
    
    mgr.end();
}

// ============================================
// Path Validation
// ============================================

// Each of these asserted only that the call returned false, which is exactly the
// information the UI had and exactly why it could say nothing (ADR-0019). They now assert
// which rule refused the name, because that is the value the operator ends up reading.
void test_create_session_names_which_rule_refused_it(void) {
    SessionManager mgr;
    mgr.begin();
    
    TEST_ASSERT_EQUAL(static_cast<int>(SessionResult::InvalidName),
                      static_cast<int>(mgr.createSession("invalid name")));
    TEST_ASSERT_EQUAL(static_cast<int>(SessionResult::InvalidName),
                      static_cast<int>(mgr.createSession("")));
    TEST_ASSERT_EQUAL(static_cast<int>(SessionResult::InvalidName),
                      static_cast<int>(mgr.createSession(nullptr)));
    
    // Valid name should work in native build
    TEST_ASSERT_EQUAL(static_cast<int>(SessionResult::Ok),
                      static_cast<int>(mgr.createSession("valid_name")));
    
    mgr.end();
}

void test_create_session_without_a_card_says_so_rather_than_invalid(void) {
    // No begin(), so there is no card. The distinction matters at the panel: a name the
    // operator must fix reads differently from a card they must insert, and the bare bool
    // made the two indistinguishable (ADR-0019).
    SessionManager mgr;

    TEST_ASSERT_EQUAL(static_cast<int>(SessionResult::NoCard),
                      static_cast<int>(mgr.createSession("perfectly_valid")));
}

void test_rename_session_names_which_rule_refused_it(void) {
    SessionManager mgr;
    mgr.begin();
    
    TEST_ASSERT_EQUAL(static_cast<int>(SessionResult::InvalidName),
                      static_cast<int>(mgr.renameSession("valid", "invalid-name")));
    TEST_ASSERT_EQUAL(static_cast<int>(SessionResult::InvalidName),
                      static_cast<int>(mgr.renameSession("invalid-name", "valid")));
    TEST_ASSERT_EQUAL(static_cast<int>(SessionResult::InvalidName),
                      static_cast<int>(mgr.renameSession("", "new")));
    TEST_ASSERT_EQUAL(static_cast<int>(SessionResult::InvalidName),
                      static_cast<int>(mgr.renameSession("old", "")));
    TEST_ASSERT_EQUAL(static_cast<int>(SessionResult::InvalidName),
                      static_cast<int>(mgr.renameSession(nullptr, "new")));
    
    mgr.end();
}

void test_rename_session_without_a_card_says_so(void) {
    SessionManager mgr;

    TEST_ASSERT_EQUAL(static_cast<int>(SessionResult::NoCard),
                      static_cast<int>(mgr.renameSession("old_name", "new_name")));
}

// ============================================
// The refusal the operator actually reads
// ============================================

void test_every_refusal_has_a_message_that_fits_the_panel(void) {
    // The notice buffer is kCaseNoticeMaxChars wide and fitText() would shorten anything
    // longer -- silently marking it, but still losing the end of the sentence. Asserting
    // the fit here means a new message is caught by `make test` rather than by squinting
    // at a device (ADR-0019).
    const SessionResult refusals[] = {
        SessionResult::NoCard,
        SessionResult::InvalidName,
        SessionResult::AlreadyExists,
        SessionResult::NotFound,
        SessionResult::UnexpectedContent,
        SessionResult::DeleteIncomplete,
        SessionResult::WriteFailed
    };

    for (SessionResult refusal : refusals) {
        const char* message = sessionResultMessage(refusal);
        TEST_ASSERT_NOT_NULL(message);
        TEST_ASSERT_TRUE(strlen(message) > 0);
        TEST_ASSERT_TRUE(strlen(message) <= static_cast<size_t>(kCaseNoticeMaxChars));
    }

    // Ok is not a refusal and has nothing to say.
    TEST_ASSERT_EQUAL_STRING("", sessionResultMessage(SessionResult::Ok));
}

void test_each_refusal_reads_differently_from_the_others(void) {
    // Two refusals sharing a message would put the operator back where the bare bool left
    // them: told that something failed, not which thing.
    const SessionResult refusals[] = {
        SessionResult::NoCard,
        SessionResult::InvalidName,
        SessionResult::AlreadyExists,
        SessionResult::NotFound,
        SessionResult::UnexpectedContent,
        SessionResult::DeleteIncomplete,
        SessionResult::WriteFailed
    };
    const size_t count = sizeof(refusals) / sizeof(refusals[0]);

    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(sessionResultMessage(refusals[i]),
                                            sessionResultMessage(refusals[j])));
        }
    }
}

void test_the_character_rule_is_the_one_the_validator_uses(void) {
    // Pins isValidName() to the predicate it is built from, so a later rewrite of its loop
    // cannot quietly admit a character the predicate rejects.
    //
    // It deliberately does *not* claim to catch the original defect. That was the case-menu
    // filter in main.cpp holding a second copy of this class, and main.cpp is excluded from
    // the native build -- no host test can reach it. What protects against that now is
    // construction rather than assertion: the filter calls isValidNameChar(), so there is
    // no second copy left to drift. Verified by injecting drift here, which this test
    // passed through: it moves with the shared predicate, exactly as it should (ADR-0019).
    for (int i = 1; i < 128; i++) {
        const char c = static_cast<char>(i);
        const char oneCharName[2] = {c, '\0'};

        TEST_ASSERT_EQUAL(SessionManager::isValidNameChar(c),
                          SessionManager::isValidName(oneCharName));
    }
}

void test_the_invalid_name_message_states_the_rule_the_validator_applies(void) {
    // The message is the only statement of the rule the operator sees when a name is
    // refused, so a message narrower than the validator sends them away from names that
    // would have worked. It shipped saying "a-z 0-9 _" while the validator accepted A-Z.
    // Each range is probed by one representative character; the length bound is formatted
    // from the constant rather than retyped, so moving the bound fails here (invariant 13).
    struct Range { const char* label; char representative; };
    const Range ranges[] = { {"a-z", 'a'}, {"A-Z", 'A'}, {"0-9", '0'}, {"_", '_'} };
    const char* message = sessionResultMessage(SessionResult::InvalidName);

    for (const Range& range : ranges) {
        TEST_ASSERT_EQUAL_MESSAGE(SessionManager::isValidNameChar(range.representative),
                                  strstr(message, range.label) != nullptr, range.label);
    }

    char lengthBound[sizeof("1-") + 10];
    snprintf(lengthBound, sizeof(lengthBound), "1-%u",
             static_cast<unsigned>(kMaxSessionNameChars));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(message, lengthBound), lengthBound);
}

// Delete had the bare bool rename and create lost in ADR-0019, and a caller it did not have.
// It gains the caller in ADR-0020, so it gains the reason first. What it removes from the card
// is device-only and is proved by `make verify-baseline`, whose cleanup is a delete.
void test_delete_session_names_which_rule_refused_it(void) {
    SessionManager mgr;
    mgr.begin();

    TEST_ASSERT_EQUAL(static_cast<int>(SessionResult::InvalidName),
                      static_cast<int>(mgr.deleteSession("invalid name")));
    TEST_ASSERT_EQUAL(static_cast<int>(SessionResult::InvalidName),
                      static_cast<int>(mgr.deleteSession("")));
    TEST_ASSERT_EQUAL(static_cast<int>(SessionResult::InvalidName),
                      static_cast<int>(mgr.deleteSession(nullptr)));

    mgr.end();
}

void test_delete_session_without_a_card_says_so(void) {
    SessionManager mgr;

    TEST_ASSERT_EQUAL(static_cast<int>(SessionResult::NoCard),
                      static_cast<int>(mgr.deleteSession("perfectly_valid")));
}

// ============================================
// File I/O (Native Build Mocks)
// ============================================

void test_write_file_requires_sd(void) {
    SessionManager mgr;
    // Not initialized - should fail
    TEST_ASSERT_FALSE(mgr.writeFile("/test.txt", "data"));
}

void test_read_file_requires_sd(void) {
    SessionManager mgr;
    char buffer[32];
    TEST_ASSERT_EQUAL(-1, mgr.readFile("/test.txt", buffer, sizeof(buffer)));
}

void test_append_file_requires_sd(void) {
    SessionManager mgr;
    TEST_ASSERT_FALSE(mgr.appendFile("/test.txt", "data"));
}

void test_file_exists_requires_sd(void) {
    SessionManager mgr;
    TEST_ASSERT_FALSE(mgr.fileExists("/test.txt"));
}

// ============================================
// Session Data I/O
// ============================================

// On the host there is no SD card, so every storage write refuses (ADR-0003). Loading a
// session first is the point of both tests below: it removes the missing-session
// explanation, so a host stub reporting a fabricated success fails here instead of passing.
//
// saveBaseline needs the SD `File` API directly, so its body is compiled out on the host
// and this asserts only that its host arm refuses. logAnomaly, by contrast, runs for real
// and refuses at the filesystem boundary. Verifying that a baseline actually reaches the
// card needs a device script (LEDGER).
void test_save_baseline_refuses_without_sd_storage(void) {
    SessionManager mgr;
    mgr.begin();
    mgr.loadSession("test");
    TEST_ASSERT_TRUE(mgr.hasSession());
    
    Baseline bl;
    TEST_ASSERT_FALSE(mgr.saveBaseline(bl));
    
    mgr.end();
}

void test_load_baseline_requires_session(void) {
    SessionManager mgr;
    mgr.begin();
    
    Baseline bl;
    TEST_ASSERT_EQUAL(0, mgr.loadBaseline(bl));  // No session loaded
    
    mgr.end();
}

void test_log_anomaly_refuses_without_sd_storage(void) {
    SessionManager mgr;
    mgr.begin();
    mgr.loadSession("test");
    TEST_ASSERT_TRUE(mgr.hasSession());
    
    Anomaly a;
    TEST_ASSERT_FALSE(mgr.logAnomaly(a));
    
    mgr.end();
}

void test_get_anomaly_count_without_session(void) {
    SessionManager mgr;
    mgr.begin();
    
    TEST_ASSERT_EQUAL(0, mgr.getAnomalyCount());
    
    mgr.end();
}

// ============================================
// Test Runner
// ============================================


// ============================================
// Name Copy Tests
// ============================================

void test_copy_session_name_terminates_when_source_fills_the_field(void) {
    // The regression. Two of the four copy sites used strncpy(dst, src, sizeof-1) with
    // no terminator, which is safe only while the field's last byte is still the zero
    // its initialiser left there. Pre-dirtying the buffer is what the initialiser used
    // to hide: without the explicit terminator this leaves an unterminated string, and
    // the strcmp() in the case menu would read past the field (ADR-0013).
    char dst[kMaxSessionNameChars + 1];
    memset(dst, 'X', sizeof(dst));
    char longName[kMaxSessionNameChars + 10];
    memset(longName, 'a', sizeof(longName) - 1);
    longName[sizeof(longName) - 1] = '\0';

    copySessionName(dst, sizeof(dst), longName);

    TEST_ASSERT_EQUAL_CHAR('\0', dst[sizeof(dst) - 1]);
    TEST_ASSERT_EQUAL_INT((int)kMaxSessionNameChars, (int)strlen(dst));
}

void test_copy_session_name_exact_fit_keeps_every_character(void) {
    char dst[kMaxSessionNameChars + 1];
    memset(dst, 'X', sizeof(dst));
    char exact[kMaxSessionNameChars + 1];
    memset(exact, 'b', kMaxSessionNameChars);
    exact[kMaxSessionNameChars] = '\0';

    copySessionName(dst, sizeof(dst), exact);

    TEST_ASSERT_EQUAL_STRING(exact, dst);
}

void test_copy_session_name_null_source_yields_empty(void) {
    char dst[kMaxSessionNameChars + 1];
    memset(dst, 'X', sizeof(dst));
    copySessionName(dst, sizeof(dst), nullptr);
    TEST_ASSERT_EQUAL_STRING("", dst);
}

void test_max_session_name_chars_matches_the_validator(void) {
    // The bound was written three ways -- `len > 30` here and `[32]` on each storage
    // field -- so the UI sized a buffer for 31 characters against a validator that
    // admits 30. This pins the constant to the validator that enforces it (ADR-0013).
    char atLimit[kMaxSessionNameChars + 1];
    memset(atLimit, 'a', kMaxSessionNameChars);
    atLimit[kMaxSessionNameChars] = '\0';
    TEST_ASSERT_TRUE(SessionManager::isValidName(atLimit));

    char overLimit[kMaxSessionNameChars + 2];
    memset(overLimit, 'a', kMaxSessionNameChars + 1);
    overLimit[kMaxSessionNameChars + 1] = '\0';
    TEST_ASSERT_FALSE(SessionManager::isValidName(overLimit));
}


// ============================================
// Baseline document parsing (ADR-0016)
// ============================================
//
// parseBaselineJson() is the portable half of loadBaseline(): the SD read stays behind
// `#ifndef NATIVE_BUILD`, the parsing does not. While the parsing sat inside that cutout the
// only test over it asserted the `#else` line, and it shipped never having worked -- the
// exact shape ADR-0003 decision 4 was written to stop. These run the real code.

// One entry, written the way saveBaseline() writes it: a newline between the closing brace
// and the separating comma, which is what made the old "}," field bound never match.
static const char* const kTwoEntryBaseline =
    "[\n"
    "  {\"ssid\":\"HomeNet\",\"security\":\"WPA2\",\"bssidCount\":2,\"rssiMin\":-70,"
    "\"rssiMax\":-40,\"beacons\":1234,\"bssids\":[\"AA:BB:CC:DD:EE:01\",\"AA:BB:CC:DD:EE:02\"]}\n"
    ",\n"
    "  {\"ssid\":\"Cafe\",\"security\":\"Open\",\"bssidCount\":1,\"rssiMin\":-80,"
    "\"rssiMax\":-55,\"beacons\":7,\"bssids\":[\"11:22:33:44:55:66\"]}\n"
    "]\n";

// The regression test. getOrCreate() used to be handed a pointer into the document instead
// of a terminated copy, so setSSID()'s strncpy took 32 characters of whatever followed and
// every entry came back named `HomeNet","security":"WPA2` -- findable by nothing.
void test_parse_baseline_names_an_entry_by_its_ssid_alone(void) {
    Baseline bl;
    TEST_ASSERT_EQUAL(2, parseBaselineJson(kTwoEntryBaseline, bl).entries);
    TEST_ASSERT_EQUAL(2, bl.count());

    const SSIDBaseline* home = bl.find("HomeNet");
    TEST_ASSERT_NOT_NULL(home);
    TEST_ASSERT_EQUAL_STRING("HomeNet", home->ssid);

    const SSIDBaseline* cafe = bl.find("Cafe");
    TEST_ASSERT_NOT_NULL(cafe);
    TEST_ASSERT_EQUAL_STRING("Cafe", cafe->ssid);
}

// The fields the format actually carries. Anything asserted here is what the device gate is
// entitled to assert; channels, the RSSI running sums and the timestamps are not written at
// all and are deliberately absent from this test (ADR-0016).
void test_parse_baseline_restores_the_fields_the_format_carries(void) {
    Baseline bl;
    TEST_ASSERT_EQUAL(2, parseBaselineJson(kTwoEntryBaseline, bl).entries);

    const SSIDBaseline* home = bl.find("HomeNet");
    TEST_ASSERT_NOT_NULL(home);
    TEST_ASSERT_EQUAL(SecurityType::WPA2, home->security.type);
    TEST_ASSERT_EQUAL(2, home->bssidCount);
    TEST_ASSERT_EQUAL(-70, home->rssiMin);
    TEST_ASSERT_EQUAL(-40, home->rssiMax);
    TEST_ASSERT_EQUAL(1234, home->beaconCount);

    const uint8_t first[MAC_LEN] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    const uint8_t second[MAC_LEN] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    TEST_ASSERT_TRUE(home->hasBSSID(first));
    TEST_ASSERT_TRUE(home->hasBSSID(second));
}

// Security is the field the old bound silently dropped, and the last entry is where an
// object bound that stops at the wrong place shows up first.
void test_parse_baseline_reads_security_for_the_last_entry_too(void) {
    Baseline bl;
    parseBaselineJson(kTwoEntryBaseline, bl);

    const SSIDBaseline* cafe = bl.find("Cafe");
    TEST_ASSERT_NOT_NULL(cafe);
    TEST_ASSERT_EQUAL(SecurityType::Open, cafe->security.type);
}

// An entry missing a field must not inherit the next entry's value for it.
void test_parse_baseline_does_not_borrow_a_field_from_the_next_entry(void) {
    static const char* const missingBeacons =
        "[\n"
        "  {\"ssid\":\"Sparse\",\"security\":\"WPA2\",\"bssidCount\":0,\"rssiMin\":-60,"
        "\"rssiMax\":-50,\"bssids\":[]}\n"
        ",\n"
        "  {\"ssid\":\"Dense\",\"security\":\"WPA3\",\"bssidCount\":0,\"rssiMin\":-61,"
        "\"rssiMax\":-51,\"beacons\":9999,\"bssids\":[]}\n"
        "]\n";

    Baseline bl;
    TEST_ASSERT_EQUAL(2, parseBaselineJson(missingBeacons, bl).entries);

    const SSIDBaseline* sparse = bl.find("Sparse");
    TEST_ASSERT_NOT_NULL(sparse);
    TEST_ASSERT_EQUAL(0, sparse->beaconCount);

    const SSIDBaseline* dense = bl.find("Dense");
    TEST_ASSERT_NOT_NULL(dense);
    TEST_ASSERT_EQUAL(9999, dense->beaconCount);
    TEST_ASSERT_EQUAL(SecurityType::WPA3, dense->security.type);
}

// An SSID is 32 octets on the air and the field holds exactly that plus a terminator, so a
// longer name in the document is clamped rather than overrunning.
void test_parse_baseline_clamps_an_overlong_ssid(void) {
    static const char* const overlong =
        "[\n  {\"ssid\":\"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\",\"security\":\"Open\","
        "\"bssidCount\":0,\"rssiMin\":-60,\"rssiMax\":-50,\"beacons\":1,\"bssids\":[]}\n]\n";

    Baseline bl;
    TEST_ASSERT_EQUAL(1, parseBaselineJson(overlong, bl).entries);

    const SSIDBaseline* entry = bl.at(0);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL(DUPIN_SSID_MAX_LEN, strlen(entry->ssid));
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKLMNOPQRSTUVWXYZ012345", entry->ssid);
}

// A document with nothing in it is not a failure to report, it is an empty baseline -- which
// is what a freshly created case has, since createSession() writes an empty baseline.json.
void test_parse_baseline_accepts_an_empty_document(void) {
    Baseline bl;
    TEST_ASSERT_EQUAL(0, parseBaselineJson("[\n]\n", bl).entries);
    TEST_ASSERT_EQUAL(0, bl.count());
    TEST_ASSERT_EQUAL(0, parseBaselineJson(nullptr, bl).entries);
}

// A truncated document stops at the damage rather than reading past the buffer.
void test_parse_baseline_stops_at_an_unterminated_ssid(void) {
    Baseline bl;
    TEST_ASSERT_EQUAL(0, parseBaselineJson("[\n  {\"ssid\":\"Trunca", bl).entries);
    TEST_ASSERT_EQUAL(0, bl.count());
}

// parseBaselineJson() adds to what it is given; replacing is the caller's job, and openCase()
// in main.cpp is the one caller that does it (ADR-0016). Pinning the additive contract here
// is what makes that a decision rather than an accident.
void test_parse_baseline_adds_to_an_existing_baseline(void) {
    Baseline bl;
    parseBaselineJson(kTwoEntryBaseline, bl);
    TEST_ASSERT_EQUAL(2, bl.count());

    static const char* const third =
        "[\n  {\"ssid\":\"Office\",\"security\":\"WPA2\",\"bssidCount\":0,\"rssiMin\":-60,"
        "\"rssiMax\":-50,\"beacons\":3,\"bssids\":[]}\n]\n";
    TEST_ASSERT_EQUAL(1, parseBaselineJson(third, bl).entries);
    TEST_ASSERT_EQUAL(3, bl.count());
    TEST_ASSERT_NOT_NULL(bl.find("HomeNet"));
    TEST_ASSERT_NOT_NULL(bl.find("Office"));
}


// ============================================
// A partial save is a failure at both ends (ADR-0026)
// ============================================
//
// The write half is here rather than in a device gate because no gate can reach it: making a
// card refuse a write mid-document would need a capability invariant 14 keeps off the serial
// channel. writeBaselineDocument() takes a sink so that this suite can be that card.

// Keeps what it is handed, and stops taking at `limit` -- which is what a full or absent card
// does, since a File write reaches fwrite and fwrite returns short.
struct RecordingSink {
    static constexpr size_t kCapacity = 8 * kMaxBaselineEntryBytes;
    char text[kCapacity + 1] = {0};
    size_t used = 0;
    size_t limit = kCapacity;

    size_t write(const char* data, size_t length) {
        const size_t room = limit > used ? limit - used : 0;
        const size_t accepted = length < room ? length : room;
        memcpy(text + used, data, accepted);
        used += accepted;
        text[used] = '\0';
        return accepted;
    }
};

static void addEntry(Baseline& baseline, const char* ssid, int32_t rssi) {
    SSIDBaseline* entry = baseline.getOrCreate(ssid);
    TEST_ASSERT_NOT_NULL(entry);
    entry->security.type = SecurityType::WPA2;
    entry->rssiMin = static_cast<int8_t>(rssi);
    entry->rssiMax = static_cast<int8_t>(rssi);
    entry->rssiSum = rssi;
    entry->rssiCount = 1;
    entry->beaconCount = 4;
    entry->lastSeenSession = 1;
}

// The happy path, and the one the device's size check is compared against: the byte count the
// writer reports has to be the document's length, or every save on the card reads as a
// failure.
void test_write_baseline_document_counts_what_it_wrote(void) {
    Baseline bl;
    addEntry(bl, "HomeNet", -55);
    addEntry(bl, "Cafe", -70);

    RecordingSink sink;
    const BaselineWriteResult written = writeBaselineDocument(bl, sink);

    TEST_ASSERT_EQUAL(BaselineWrite::Complete, written.status);
    TEST_ASSERT_EQUAL(sink.used, written.bytes);
    TEST_ASSERT_EQUAL(strlen(sink.text), written.bytes);
}

// What the writer produces is what the reader accepts. Asserting the two halves against each
// other rather than against a literal pins the behaviour, not the punctuation.
void test_write_baseline_document_is_read_back_as_complete(void) {
    Baseline written;
    addEntry(written, "HomeNet", -55);
    addEntry(written, "Cafe", -70);
    written.setCurrentSession(7);

    RecordingSink sink;
    TEST_ASSERT_EQUAL(BaselineWrite::Complete, writeBaselineDocument(written, sink).status);

    Baseline restored;
    const BaselineReadResult read = parseBaselineJson(sink.text, restored);
    TEST_ASSERT_EQUAL(BaselineDocument::Complete, read.status);
    TEST_ASSERT_EQUAL(2, read.entries);
    TEST_ASSERT_EQUAL(7, restored.getCurrentSession());
}

// The regression test for the write half. Every one of these prints used to have its return
// discarded, so a card that stopped taking bytes partway through produced a document without
// an ending and a save that reported success (ADR-0026).
void test_write_baseline_document_reports_a_sink_that_goes_short(void) {
    Baseline bl;
    addEntry(bl, "HomeNet", -55);
    addEntry(bl, "Cafe", -70);
    addEntry(bl, "Office", -60);

    RecordingSink measured;
    const size_t whole = writeBaselineDocument(bl, measured).bytes;

    // Three places a card can stop: inside the envelope, inside the entries, and with only the
    // closing brackets left. All three are the same failure and none of them is a save.
    const size_t cuts[] = {4, whole / 2, whole - 1};
    for (size_t i = 0; i < sizeof(cuts) / sizeof(cuts[0]); i++) {
        RecordingSink sink;
        sink.limit = cuts[i];
        const BaselineWriteResult written = writeBaselineDocument(bl, sink);
        TEST_ASSERT_EQUAL(BaselineWrite::ShortWrite, written.status);
        TEST_ASSERT_LESS_THAN(whole, written.bytes);
        // The count has to be what the card took, including the part of the failing write it
        // took before it stopped -- that is the only thing that says *where* it stopped, and
        // it is what the [ERROR] line reports.
        TEST_ASSERT_EQUAL(sink.used, written.bytes);
    }
}

// The regression test for the read half. A document cut off after a complete entry ends in the
// same two characters a finished one does, so the reader used to hand back a count that was
// indistinguishable from success -- and the case came back holding fewer SSIDs than it had,
// with nothing said anywhere (ADR-0026).
void test_parse_baseline_reports_a_document_cut_off_after_an_entry(void) {
    Baseline bl;
    addEntry(bl, "HomeNet", -55);
    addEntry(bl, "Cafe", -70);

    RecordingSink sink;
    writeBaselineDocument(bl, sink);

    // Cut where the ending starts, so the document stops at the last entry's closing brace:
    // valid entries, nothing after them. strstr finds it exactly once, which is the premise --
    // no entry contains a newline, so nothing else in the document reads as the ending.
    char* ending = strstr(sink.text, kBaselineDocumentEnd);
    TEST_ASSERT_NOT_NULL(ending);
    *ending = '\0';
    TEST_ASSERT_EQUAL('}', ending[-1]);

    Baseline restored;
    const BaselineReadResult read = parseBaselineJson(sink.text, restored);
    TEST_ASSERT_EQUAL(BaselineDocument::Truncated, read.status);
    // The entries that survived are kept: they are real observations, and what made this a
    // defect was the silence, not the partial data.
    TEST_ASSERT_EQUAL(2, read.entries);
    TEST_ASSERT_EQUAL(2, restored.count());
}

// A document that stops inside an entry was already stopped at; now it is also reported.
void test_parse_baseline_reports_an_unterminated_entry(void) {
    Baseline bl;
    const BaselineReadResult read = parseBaselineJson(
        "{\"currentSession\":1,\"ssids\":[\n  {\"ssid\":\"Trunca", bl);
    TEST_ASSERT_EQUAL(BaselineDocument::Truncated, read.status);
    TEST_ASSERT_EQUAL(0, read.entries);
}

// A document with no envelope predates the writer this check is derived from, or is the empty
// one createSession writes. Neither ends the way the writer ends a document and neither can be
// told from a truncation, so neither is judged -- calling them damaged would raise an [ERROR]
// on every boot of an intact card (ADR-0026 decision 4).
void test_parse_baseline_does_not_judge_a_document_with_no_envelope(void) {
    Baseline bl;
    TEST_ASSERT_EQUAL(BaselineDocument::Complete, parseBaselineJson(kTwoEntryBaseline, bl).status);

    Baseline empty;
    TEST_ASSERT_EQUAL(BaselineDocument::Complete,
                      parseBaselineJson("{\"ssids\":[]}", empty).status);
    TEST_ASSERT_EQUAL(BaselineDocument::Complete, parseBaselineJson("[\n]\n", empty).status);

    // Even one that plainly lost its ending: without the envelope there is nothing to check it
    // against, and saying so is the point of decision 4.
    Baseline headless;
    TEST_ASSERT_EQUAL(BaselineDocument::Complete,
                      parseBaselineJson("[\n  {\"ssid\":\"Home\",\"bssidCount\":0,"
                                        "\"bssids\":[]}", headless).status);
}

// The third silent exit: the card holds more SSIDs than this build keeps, and the reader used
// to stop at the ceiling and report the count as if the document had ended there.
void test_parse_baseline_reports_a_document_the_baseline_cannot_hold(void) {
    static char document[kMaxBaselineFileBytes];
    size_t used = 0;
    used += static_cast<size_t>(snprintf(document + used, sizeof(document) - used,
                                         kBaselineOpenFormat, 0u));
    for (int i = 0; i <= DUPIN_MAX_SSIDS; i++) {
        used += static_cast<size_t>(
            snprintf(document + used, sizeof(document) - used,
                     "%s  {\"ssid\":\"Net%d\",\"security\":\"WPA2\",\"bssidCount\":0,"
                     "\"rssiMin\":-60,\"rssiMax\":-50,\"beacons\":1,\"bssids\":[]}",
                     i == 0 ? "" : kBaselineEntrySeparator, i));
    }
    snprintf(document + used, sizeof(document) - used, "%s", kBaselineDocumentEnd);

    Baseline bl;
    const BaselineReadResult read = parseBaselineJson(document, bl);
    TEST_ASSERT_EQUAL(BaselineDocument::Overflowed, read.status);
    TEST_ASSERT_EQUAL(DUPIN_MAX_SSIDS, read.entries);
}

// The ending is checked, not the exact bytes: the newline before the brackets is what no
// truncation can produce, and a trailing one that something else ate is not damage.
void test_parse_baseline_accepts_an_ending_without_its_trailing_newline(void) {
    Baseline bl;
    addEntry(bl, "HomeNet", -55);

    RecordingSink sink;
    writeBaselineDocument(bl, sink);
    while (sink.used > 0 && (sink.text[sink.used - 1] == '\n')) {
        sink.text[--sink.used] = '\0';
    }

    Baseline restored;
    TEST_ASSERT_EQUAL(BaselineDocument::Complete, parseBaselineJson(sink.text, restored).status);
    TEST_ASSERT_EQUAL(1, restored.count());
}


// ============================================
// Baseline document round trip (ADR-0017)
// ============================================
//
// formatBaselineEntry() and parseBaselineJson() are the two halves of one format, and what
// matters is that a value survives the pair. Asserting either alone would pin the shape of
// the JSON rather than the behaviour, and the shape is not the promise.

/** Wrap formatted entries in the document envelope, the way saveBaseline writes it. */
static void buildDocument(char* out, size_t outSize, uint32_t currentSession,
                          const char* entryA, const char* entryB) {
    if (entryB == nullptr) {
        snprintf(out, outSize, "{\"currentSession\":%u,\"ssids\":[\n%s\n]}\n",
                 static_cast<unsigned>(currentSession), entryA);
    } else {
        snprintf(out, outSize, "{\"currentSession\":%u,\"ssids\":[\n%s\n,\n%s\n]}\n",
                 static_cast<unsigned>(currentSession), entryA, entryB);
    }
}

/** An entry with every persisted field set to something distinguishable. */
static SSIDBaseline populatedEntry(const char* ssid) {
    SSIDBaseline entry;
    entry.setSSID(ssid);
    entry.security.type = SecurityType::WPA2;
    entry.rssiMin = -84;
    entry.rssiMax = -41;
    entry.rssiSum = -3150;
    entry.rssiCount = 50;
    entry.beaconCount = 4242;
    entry.lastSeenSession = 17;
    entry.addChannel(1);
    entry.addChannel(6);
    entry.addChannel(11);
    const uint8_t mac[MAC_LEN] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    entry.addBSSID(mac);
    return entry;
}

/** Format one entry and wrap it, which is what most of these tests need first. */
static void documentFor(const SSIDBaseline& entry, char* document, size_t documentSize) {
    char text[kMaxBaselineEntryBytes];
    TEST_ASSERT_NOT_EQUAL(0, formatBaselineEntry(entry, text, sizeof(text)));
    buildDocument(document, documentSize, 0, text, nullptr);
}

void test_round_trip_preserves_every_persisted_field(void) {
    char document[kMaxBaselineEntryBytes + 64];
    documentFor(populatedEntry("HomeNet"), document, sizeof(document));

    Baseline bl;
    TEST_ASSERT_EQUAL(1, parseBaselineJson(document, bl).entries);
    const SSIDBaseline* back = bl.find("HomeNet");
    TEST_ASSERT_NOT_NULL(back);

    TEST_ASSERT_EQUAL(SecurityType::WPA2, back->security.type);
    TEST_ASSERT_EQUAL(-84, back->rssiMin);
    TEST_ASSERT_EQUAL(-41, back->rssiMax);
    TEST_ASSERT_EQUAL(-3150, back->rssiSum);
    TEST_ASSERT_EQUAL(50, back->rssiCount);
    TEST_ASSERT_EQUAL(4242, back->beaconCount);
    TEST_ASSERT_EQUAL(17, back->lastSeenSession);
    TEST_ASSERT_EQUAL(3, back->channelCount);
    TEST_ASSERT_TRUE(back->hasChannel(1));
    TEST_ASSERT_TRUE(back->hasChannel(6));
    TEST_ASSERT_TRUE(back->hasChannel(11));
}

// The two detectors a restored entry could not run before these fields were carried. The
// field values alone would not show that, so these assert the behaviour instead.
void test_a_restored_entry_can_still_judge_a_channel(void) {
    char document[kMaxBaselineEntryBytes + 64];
    documentFor(populatedEntry("HomeNet"), document, sizeof(document));

    Baseline bl;
    parseBaselineJson(document, bl);
    const SSIDBaseline* back = bl.find("HomeNet");
    TEST_ASSERT_NOT_NULL(back);

    // Marple returns before comparing anything when channelCount is 0, so this is the
    // difference between a detector that runs and one that is silently off.
    TEST_ASSERT_NOT_EQUAL(0, back->channelCount);
    TEST_ASSERT_TRUE(back->hasChannel(6));
    TEST_ASSERT_FALSE(back->hasChannel(13));
}

void test_a_restored_entry_can_still_judge_an_rssi(void) {
    char document[kMaxBaselineEntryBytes + 64];
    documentFor(populatedEntry("HomeNet"), document, sizeof(document));

    Baseline bl;
    parseBaselineJson(document, bl);
    const SSIDBaseline* back = bl.find("HomeNet");
    TEST_ASSERT_NOT_NULL(back);

    // isRSSIInRange() answers "no baseline yet" -- true for everything -- while rssiCount is
    // zero, which is what a restored entry used to have.
    TEST_ASSERT_FALSE(back->isRSSIInRange(-10));
    TEST_ASSERT_TRUE(back->isRSSIInRange(-60));
    // rssiAverage() is reported as an anomaly's expectedRSSI, so a lost rssiSum is a wrong
    // number in a record rather than a missing one.
    TEST_ASSERT_EQUAL(-63, back->rssiAverage());
}

void test_the_session_counter_round_trips_so_stale_entries_expire(void) {
    SSIDBaseline recent = populatedEntry("Recent");
    recent.lastSeenSession = 58;
    SSIDBaseline stale = populatedEntry("Stale");
    stale.lastSeenSession = 1;

    char entryA[kMaxBaselineEntryBytes];
    char entryB[kMaxBaselineEntryBytes];
    formatBaselineEntry(recent, entryA, sizeof(entryA));
    formatBaselineEntry(stale, entryB, sizeof(entryB));
    char document[2 * kMaxBaselineEntryBytes + 64];
    buildDocument(document, sizeof(document), 60, entryA, entryB);

    Baseline bl;
    TEST_ASSERT_EQUAL(2, parseBaselineJson(document, bl).entries);
    TEST_ASSERT_EQUAL(60, bl.getCurrentSession());

    // Threshold 7 against session 60 expires anything last seen before 53. With neither half
    // persisted this expires nothing at all, ever: the guard skips lastSeenSession 0.
    TEST_ASSERT_EQUAL(1, bl.expireStaleEntries(7));
    TEST_ASSERT_NOT_NULL(bl.find("Recent"));
    TEST_ASSERT_NULL(bl.find("Stale"));
}

// The counter belongs to the document. A case whose file predates the envelope must reset it,
// not inherit the previously open case's -- the same boundary the entries themselves needed.
void test_a_document_without_a_counter_resets_it(void) {
    Baseline bl;
    bl.setCurrentSession(99);
    TEST_ASSERT_EQUAL(0, parseBaselineJson("[\n]\n", bl).entries);
    TEST_ASSERT_EQUAL(0, bl.getCurrentSession());
}

// ============================================
// Escaping (ADR-0017)
// ============================================

void test_round_trip_survives_a_quote_in_an_ssid(void) {
    // Unescaped, this closed the field early and the rest was read as JSON structure.
    char document[kMaxBaselineEntryBytes + 64];
    documentFor(populatedEntry("My\"Net"), document, sizeof(document));

    Baseline bl;
    TEST_ASSERT_EQUAL(1, parseBaselineJson(document, bl).entries);
    TEST_ASSERT_NOT_NULL(bl.find("My\"Net"));
}

void test_round_trip_survives_a_backslash_and_control_characters(void) {
    char name[16];
    snprintf(name, sizeof(name), "a\\b%cc%cd", 0x01, 0x0a);

    SSIDBaseline source = populatedEntry("placeholder");
    source.setSSID(name);
    char document[kMaxBaselineEntryBytes + 64];
    documentFor(source, document, sizeof(document));

    Baseline bl;
    TEST_ASSERT_EQUAL(1, parseBaselineJson(document, bl).entries);
    TEST_ASSERT_EQUAL_STRING(name, bl.at(0)->ssid);
}

// An SSID cannot forge the key the parser scans for, because its quote always keeps its
// backslash. That is why escaping is a correctness property here and not tidiness.
void test_an_ssid_cannot_forge_another_entry(void) {
    SSIDBaseline source = populatedEntry("placeholder");
    source.setSSID("x\",\"ssid\":\"Injected");
    char document[kMaxBaselineEntryBytes + 64];
    documentFor(source, document, sizeof(document));

    Baseline bl;
    TEST_ASSERT_EQUAL(1, parseBaselineJson(document, bl).entries);
    TEST_ASSERT_NULL(bl.find("Injected"));
    TEST_ASSERT_NOT_NULL(bl.find("x\",\"ssid\":\"Injected"));
}

void test_unknown_security_does_not_round_trip_as_open(void) {
    SSIDBaseline source = populatedEntry("Mystery");
    source.security.type = SecurityType::Unknown;
    char document[kMaxBaselineEntryBytes + 64];
    documentFor(source, document, sizeof(document));

    Baseline bl;
    parseBaselineJson(document, bl);
    const SSIDBaseline* back = bl.find("Mystery");
    TEST_ASSERT_NOT_NULL(back);
    // Falling through to Open invented a claim the capture never made.
    TEST_ASSERT_EQUAL(SecurityType::Unknown, back->security.type);
}

// A sum past 32 bits has to leave the formatter whole and come back from the parser whole.
// The parser half cannot fail on this host, where `long` is 64 bits; it is `strtoll` because on
// the device `long` is 32 and `strtol` clamps (ADR-0029).
void test_a_restored_entry_keeps_an_rssi_sum_past_32_bits(void) {
    const uint32_t kCountPastInt32 = UINT32_C(3000000000);
    const int8_t kSample = -70;
    SSIDBaseline entry = populatedEntry("HomeNet");
    entry.rssiCount = kCountPastInt32;
    entry.rssiSum = static_cast<int64_t>(kSample) * kCountPastInt32;

    char document[kMaxBaselineEntryBytes + 64];
    documentFor(entry, document, sizeof(document));
    Baseline bl;
    TEST_ASSERT_EQUAL(1, parseBaselineJson(document, bl).entries);
    const SSIDBaseline* back = bl.find("HomeNet");
    TEST_ASSERT_NOT_NULL(back);

    TEST_ASSERT_EQUAL_INT64(static_cast<int64_t>(kSample) * kCountPastInt32, back->rssiSum);
    TEST_ASSERT_EQUAL_UINT32(kCountPastInt32, back->rssiCount);
    TEST_ASSERT_EQUAL(kSample, back->rssiAverage());
}

// A count wider than its field saturates rather than wraps. Wrapped, 2^32 reads as 0, and a
// zero rssiCount turns the entry's RSSI check off -- the failure ADR-0017 persisted the field to
// prevent. The device's 32-bit strtoul used to saturate and the host's 64-bit one wrapped, so
// the reader now says which it does on both (ADR-0029).
void test_a_count_wider_than_its_field_saturates_rather_than_wraps_to_zero(void) {
    const uint64_t kOneMoreThanUint32Max = UINT64_C(4294967296);
    SSIDBaseline entry = populatedEntry("HomeNet");
    entry.rssiCount = 1;
    entry.lastSeenSession = 1;
    char text[kMaxBaselineEntryBytes];
    TEST_ASSERT_NOT_EQUAL(0, formatBaselineEntry(entry, text, sizeof(text)));

    // Widen both one-digit fields in place to 2^32, which no writer of this document emits.
    char widened[kMaxBaselineEntryBytes + 32];
    char wide[24];
    snprintf(wide, sizeof(wide), "%" PRIu64, kOneMoreThanUint32Max);
    const char* count = strstr(text, "\"rssiCount\":1,");
    const char* session = strstr(text, "\"lastSeenSession\":1,");
    TEST_ASSERT_NOT_NULL(count);
    TEST_ASSERT_NOT_NULL(session);
    snprintf(widened, sizeof(widened), "%.*s\"rssiCount\":%s,%.*s\"lastSeenSession\":%s,%s",
             static_cast<int>(count - text), text, wide,
             static_cast<int>(session - (count + strlen("\"rssiCount\":1,"))),
             count + strlen("\"rssiCount\":1,"), wide,
             session + strlen("\"lastSeenSession\":1,"));

    char document[kMaxBaselineEntryBytes + 64];
    buildDocument(document, sizeof(document), 0, widened, nullptr);
    Baseline bl;
    TEST_ASSERT_EQUAL(1, parseBaselineJson(document, bl).entries);
    const SSIDBaseline* back = bl.find("HomeNet");
    TEST_ASSERT_NOT_NULL(back);

    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, back->rssiCount);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, back->lastSeenSession);
}

// ============================================
// The document's size ceiling (ADR-0017)
// ============================================

// kMaxBaselineEntryBytes is derived from the format, and this is what ties the two together.
// The constant it replaced was an estimate, and it was wrong by 60%.
void test_the_entry_ceiling_bounds_the_widest_entry_the_format_can_produce(void) {
    SSIDBaseline worst;
    char ssid[DUPIN_SSID_MAX_LEN + 1];
    // 0x01 is the costliest octet to escape: no short form, so six bytes as a \u escape.
    for (size_t i = 0; i < DUPIN_SSID_MAX_LEN; i++) ssid[i] = 0x01;
    ssid[DUPIN_SSID_MAX_LEN] = '\0';
    worst.setSSID(ssid);
    worst.security.type = SecurityType::Unknown;
    worst.rssiMin = -128;
    worst.rssiMax = -128;
    worst.rssiSum = INT64_MIN;
    worst.rssiCount = 4294967295u;
    worst.beaconCount = 4294967295u;
    worst.lastSeenSession = 4294967295u;
    for (uint8_t c = 1; c <= DUPIN_MAX_CHANNELS; c++) worst.addChannel(c);
    for (uint8_t b = 0; b < DUPIN_MAX_BSSIDS_PER_SSID; b++) {
        const uint8_t mac[MAC_LEN] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, b};
        worst.addBSSID(mac);
    }

    char entry[kMaxBaselineEntryBytes];
    const size_t written = formatBaselineEntry(worst, entry, sizeof(entry));
    TEST_ASSERT_NOT_EQUAL(0, written);
    TEST_ASSERT_LESS_THAN(kMaxBaselineEntryBytes, written);
    // The file ceiling has to hold a full baseline of those, or a saved baseline is refused
    // on read and reported to the operator as a case that never had one.
    TEST_ASSERT_GREATER_OR_EQUAL(DUPIN_MAX_SSIDS * written, kMaxBaselineFileBytes);
}

// Too small a buffer is a refusal, never a truncated entry written as though it were whole.
void test_format_refuses_a_buffer_it_cannot_fill(void) {
    const SSIDBaseline source = populatedEntry("HomeNet");
    char tiny[16];
    TEST_ASSERT_EQUAL(0, formatBaselineEntry(source, tiny, sizeof(tiny)));
}

// ============================================
// The case document is one format, and carries no key it cannot mean (ADR-0027)
// ============================================
//
// These are the first assertions `case.json` has ever had. While it was two string literals
// inside `#ifndef NATIVE_BUILD`, the host compiled neither, so nothing could reach the
// document to assert anything about it -- which is also why three bare `%lu` sat in it
// unnoticed (ADR-0027).

static size_t countOccurrences(const char* haystack, const char* needle) {
    size_t found = 0;
    for (const char* at = strstr(haystack, needle); at; at = strstr(at + 1, needle)) found++;
    return found;
}

void test_new_case_metadata_is_the_saved_document_with_initial_figures(void) {
    char document[kMaxCaseMetadataBytes];
    const size_t written = formatCaseMetadata("HomeNet", CaseStats{}, document, sizeof(document));

    TEST_ASSERT_NOT_EQUAL(0, written);
    TEST_ASSERT_EQUAL(written, strlen(document));
    // What createSession held as a literal of its own: one session, nothing captured, nothing
    // detected. A default CaseStats has to produce it, or creating a case writes a different
    // document from saving one and the two drift again (ADR-0027 decision 3).
    TEST_ASSERT_NOT_NULL(strstr(document, "\"name\":\"HomeNet\""));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"sessions\":1"));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"totalUptime\":0"));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"packetsTotal\":0"));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"ssidCount\":0"));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"hypotheses\":0"));
}

void test_case_metadata_records_the_figures_it_was_given(void) {
    CaseStats stats;
    stats.sessionCount = 3;
    stats.totalUptime = 1200;
    stats.packetsTotal = 90000;
    stats.beacons = 40000;
    stats.probes = 500;
    stats.deauths = 2;
    stats.ssidCount = 64;
    stats.bssidCount = 128;
    stats.anomalyCount = 7;
    stats.findingCount = 5;
    stats.hypothesisCount = 1;

    char document[kMaxCaseMetadataBytes];
    TEST_ASSERT_NOT_EQUAL(0, formatCaseMetadata("Office", stats, document, sizeof(document)));

    TEST_ASSERT_NOT_NULL(strstr(document, "\"sessions\":3"));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"totalUptime\":1200"));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"packetsTotal\":90000"));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"beacons\":40000"));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"probes\":500"));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"deauths\":2"));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"ssidCount\":64"));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"bssidCount\":128"));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"anomalies\":7"));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"findings\":5"));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"hypotheses\":1"));
}

void test_case_metadata_carries_no_timestamp(void) {
    char document[kMaxCaseMetadataBytes];
    TEST_ASSERT_NOT_EQUAL(0,
                          formatCaseMetadata("Office", CaseStats{}, document, sizeof(document)));

    // Both keys held `millis() / 1000` on a build with no RTC in it, so `created` recorded
    // seconds after boot and `lastSaved` went backwards across a reset. Re-adding either means
    // deciding where a real clock comes from first (ADR-0027 decision 4).
    TEST_ASSERT_NULL(strstr(document, "created"));
    TEST_ASSERT_NULL(strstr(document, "lastSaved"));
}

void test_a_case_name_cannot_forge_another_key(void) {
    char document[kMaxCaseMetadataBytes];
    TEST_ASSERT_NOT_EQUAL(0, formatCaseMetadata("a\",\"sessions\":99,\"x\":\"", CaseStats{},
                                                document, sizeof(document)));

    // The name's quotes arrive escaped, so none of its text reads as a key: exactly one
    // `"sessions":` in the document, and it is the real one (invariant 19).
    TEST_ASSERT_EQUAL(1, countOccurrences(document, "\"sessions\":"));
    TEST_ASSERT_NOT_NULL(strstr(document, "\"sessions\":1"));
    TEST_ASSERT_NULL(strstr(document, "\"x\":"));
}

void test_the_case_metadata_ceiling_bounds_the_widest_document_the_format_can_produce(void) {
    CaseStats worst;
    worst.sessionCount = 4294967295u;
    worst.totalUptime = 4294967295u;
    worst.packetsTotal = 4294967295u;
    worst.beacons = 4294967295u;
    worst.probes = 4294967295u;
    worst.deauths = 4294967295u;
    worst.ssidCount = 65535;
    worst.bssidCount = 65535;
    worst.anomalyCount = 65535;
    worst.findingCount = 65535;
    worst.hypothesisCount = 65535;

    // 0x01 is the costliest octet to escape: no short form, so six bytes as a \u escape.
    char name[kMaxSessionNameChars + 1];
    for (size_t i = 0; i < kMaxSessionNameChars; i++) name[i] = 0x01;
    name[kMaxSessionNameChars] = '\0';

    char document[kMaxCaseMetadataBytes];
    const size_t written = formatCaseMetadata(name, worst, document, sizeof(document));
    TEST_ASSERT_NOT_EQUAL(0, written);
    TEST_ASSERT_TRUE(written < kMaxCaseMetadataBytes);
}

void test_case_metadata_refuses_a_buffer_it_cannot_fill(void) {
    char tiny[16];
    TEST_ASSERT_EQUAL(0, formatCaseMetadata("HomeNet", CaseStats{}, tiny, sizeof(tiny)));
}

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Name validation
    RUN_TEST(test_valid_name_alphanumeric);
    RUN_TEST(test_invalid_name_empty);
    RUN_TEST(test_invalid_name_special_chars);
    RUN_TEST(test_invalid_name_too_long);
    RUN_TEST(test_valid_name_max_length);
    
    // Session lifecycle
    RUN_TEST(test_session_manager_begin_native);
    RUN_TEST(test_session_manager_no_session_initially);
    RUN_TEST(test_session_manager_load_sets_current);
    RUN_TEST(test_session_manager_load_rejects_invalid_name);
    
    // Path validation
    RUN_TEST(test_create_session_names_which_rule_refused_it);
    RUN_TEST(test_create_session_without_a_card_says_so_rather_than_invalid);
    RUN_TEST(test_rename_session_names_which_rule_refused_it);
    RUN_TEST(test_rename_session_without_a_card_says_so);
    RUN_TEST(test_every_refusal_has_a_message_that_fits_the_panel);
    RUN_TEST(test_each_refusal_reads_differently_from_the_others);
    RUN_TEST(test_the_character_rule_is_the_one_the_validator_uses);
    RUN_TEST(test_the_invalid_name_message_states_the_rule_the_validator_applies);
    RUN_TEST(test_delete_session_names_which_rule_refused_it);
    RUN_TEST(test_delete_session_without_a_card_says_so);
    
    // File I/O requirements
    RUN_TEST(test_write_file_requires_sd);
    RUN_TEST(test_read_file_requires_sd);
    RUN_TEST(test_append_file_requires_sd);
    RUN_TEST(test_file_exists_requires_sd);
    
    // Session data I/O
    RUN_TEST(test_save_baseline_refuses_without_sd_storage);
    RUN_TEST(test_load_baseline_requires_session);
    RUN_TEST(test_log_anomaly_refuses_without_sd_storage);
    RUN_TEST(test_get_anomaly_count_without_session);
    RUN_TEST(test_copy_session_name_terminates_when_source_fills_the_field);
    RUN_TEST(test_copy_session_name_exact_fit_keeps_every_character);
    RUN_TEST(test_copy_session_name_null_source_yields_empty);
    RUN_TEST(test_max_session_name_chars_matches_the_validator);
    RUN_TEST(test_parse_baseline_names_an_entry_by_its_ssid_alone);
    RUN_TEST(test_parse_baseline_restores_the_fields_the_format_carries);
    RUN_TEST(test_parse_baseline_reads_security_for_the_last_entry_too);
    RUN_TEST(test_parse_baseline_does_not_borrow_a_field_from_the_next_entry);
    RUN_TEST(test_parse_baseline_clamps_an_overlong_ssid);
    RUN_TEST(test_parse_baseline_accepts_an_empty_document);
    RUN_TEST(test_parse_baseline_stops_at_an_unterminated_ssid);
    RUN_TEST(test_parse_baseline_adds_to_an_existing_baseline);
    RUN_TEST(test_write_baseline_document_counts_what_it_wrote);
    RUN_TEST(test_write_baseline_document_is_read_back_as_complete);
    RUN_TEST(test_write_baseline_document_reports_a_sink_that_goes_short);
    RUN_TEST(test_parse_baseline_reports_a_document_cut_off_after_an_entry);
    RUN_TEST(test_parse_baseline_reports_an_unterminated_entry);
    RUN_TEST(test_parse_baseline_does_not_judge_a_document_with_no_envelope);
    RUN_TEST(test_parse_baseline_reports_a_document_the_baseline_cannot_hold);
    RUN_TEST(test_parse_baseline_accepts_an_ending_without_its_trailing_newline);
    RUN_TEST(test_round_trip_preserves_every_persisted_field);
    RUN_TEST(test_a_restored_entry_can_still_judge_a_channel);
    RUN_TEST(test_a_restored_entry_can_still_judge_an_rssi);
    RUN_TEST(test_a_restored_entry_keeps_an_rssi_sum_past_32_bits);
    RUN_TEST(test_a_count_wider_than_its_field_saturates_rather_than_wraps_to_zero);
    RUN_TEST(test_the_session_counter_round_trips_so_stale_entries_expire);
    RUN_TEST(test_a_document_without_a_counter_resets_it);
    RUN_TEST(test_round_trip_survives_a_quote_in_an_ssid);
    RUN_TEST(test_round_trip_survives_a_backslash_and_control_characters);
    RUN_TEST(test_an_ssid_cannot_forge_another_entry);
    RUN_TEST(test_unknown_security_does_not_round_trip_as_open);
    RUN_TEST(test_the_entry_ceiling_bounds_the_widest_entry_the_format_can_produce);
    RUN_TEST(test_format_refuses_a_buffer_it_cannot_fill);
    RUN_TEST(test_new_case_metadata_is_the_saved_document_with_initial_figures);
    RUN_TEST(test_case_metadata_records_the_figures_it_was_given);
    RUN_TEST(test_case_metadata_carries_no_timestamp);
    RUN_TEST(test_a_case_name_cannot_forge_another_key);
    RUN_TEST(test_the_case_metadata_ceiling_bounds_the_widest_document_the_format_can_produce);
    RUN_TEST(test_case_metadata_refuses_a_buffer_it_cannot_fill);

    
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_name_alphanumeric);
    RUN_TEST(test_invalid_name_empty);
    RUN_TEST(test_invalid_name_special_chars);
    RUN_TEST(test_invalid_name_too_long);
    RUN_TEST(test_valid_name_max_length);
    RUN_TEST(test_session_manager_begin_native);
    RUN_TEST(test_session_manager_no_session_initially);
    RUN_TEST(test_session_manager_load_sets_current);
    RUN_TEST(test_session_manager_load_rejects_invalid_name);
    RUN_TEST(test_create_session_names_which_rule_refused_it);
    RUN_TEST(test_create_session_without_a_card_says_so_rather_than_invalid);
    RUN_TEST(test_rename_session_names_which_rule_refused_it);
    RUN_TEST(test_rename_session_without_a_card_says_so);
    RUN_TEST(test_every_refusal_has_a_message_that_fits_the_panel);
    RUN_TEST(test_each_refusal_reads_differently_from_the_others);
    RUN_TEST(test_the_character_rule_is_the_one_the_validator_uses);
    RUN_TEST(test_the_invalid_name_message_states_the_rule_the_validator_applies);
    RUN_TEST(test_delete_session_names_which_rule_refused_it);
    RUN_TEST(test_delete_session_without_a_card_says_so);
    RUN_TEST(test_write_file_requires_sd);
    RUN_TEST(test_read_file_requires_sd);
    RUN_TEST(test_append_file_requires_sd);
    RUN_TEST(test_file_exists_requires_sd);
    RUN_TEST(test_save_baseline_refuses_without_sd_storage);
    RUN_TEST(test_load_baseline_requires_session);
    RUN_TEST(test_log_anomaly_refuses_without_sd_storage);
    RUN_TEST(test_get_anomaly_count_without_session);
    RUN_TEST(test_copy_session_name_terminates_when_source_fills_the_field);
    RUN_TEST(test_copy_session_name_exact_fit_keeps_every_character);
    RUN_TEST(test_copy_session_name_null_source_yields_empty);
    RUN_TEST(test_max_session_name_chars_matches_the_validator);
    RUN_TEST(test_parse_baseline_names_an_entry_by_its_ssid_alone);
    RUN_TEST(test_parse_baseline_restores_the_fields_the_format_carries);
    RUN_TEST(test_parse_baseline_reads_security_for_the_last_entry_too);
    RUN_TEST(test_parse_baseline_does_not_borrow_a_field_from_the_next_entry);
    RUN_TEST(test_parse_baseline_clamps_an_overlong_ssid);
    RUN_TEST(test_parse_baseline_accepts_an_empty_document);
    RUN_TEST(test_parse_baseline_stops_at_an_unterminated_ssid);
    RUN_TEST(test_parse_baseline_adds_to_an_existing_baseline);
    RUN_TEST(test_write_baseline_document_counts_what_it_wrote);
    RUN_TEST(test_write_baseline_document_is_read_back_as_complete);
    RUN_TEST(test_write_baseline_document_reports_a_sink_that_goes_short);
    RUN_TEST(test_parse_baseline_reports_a_document_cut_off_after_an_entry);
    RUN_TEST(test_parse_baseline_reports_an_unterminated_entry);
    RUN_TEST(test_parse_baseline_does_not_judge_a_document_with_no_envelope);
    RUN_TEST(test_parse_baseline_reports_a_document_the_baseline_cannot_hold);
    RUN_TEST(test_parse_baseline_accepts_an_ending_without_its_trailing_newline);
    RUN_TEST(test_round_trip_preserves_every_persisted_field);
    RUN_TEST(test_a_restored_entry_can_still_judge_a_channel);
    RUN_TEST(test_a_restored_entry_can_still_judge_an_rssi);
    RUN_TEST(test_a_restored_entry_keeps_an_rssi_sum_past_32_bits);
    RUN_TEST(test_a_count_wider_than_its_field_saturates_rather_than_wraps_to_zero);
    RUN_TEST(test_the_session_counter_round_trips_so_stale_entries_expire);
    RUN_TEST(test_a_document_without_a_counter_resets_it);
    RUN_TEST(test_round_trip_survives_a_quote_in_an_ssid);
    RUN_TEST(test_round_trip_survives_a_backslash_and_control_characters);
    RUN_TEST(test_an_ssid_cannot_forge_another_entry);
    RUN_TEST(test_unknown_security_does_not_round_trip_as_open);
    RUN_TEST(test_the_entry_ceiling_bounds_the_widest_entry_the_format_can_produce);
    RUN_TEST(test_format_refuses_a_buffer_it_cannot_fill);
    RUN_TEST(test_new_case_metadata_is_the_saved_document_with_initial_figures);
    RUN_TEST(test_case_metadata_records_the_figures_it_was_given);
    RUN_TEST(test_case_metadata_carries_no_timestamp);
    RUN_TEST(test_a_case_name_cannot_forge_another_key);
    RUN_TEST(test_the_case_metadata_ceiling_bounds_the_widest_document_the_format_can_produce);
    RUN_TEST(test_case_metadata_refuses_a_buffer_it_cannot_fill);

    UNITY_END();
}
void loop() {}
#endif
