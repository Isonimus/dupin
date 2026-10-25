/**
 * @file session_manager.h
 * @brief SD Card Session Management for Dupin
 * 
 * Manages named sessions (Home, Office, Airport) stored on SD card.
 * Each session has its own baseline, anomaly log, and metadata.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include "dupin.h"
// Not a forward declaration any more: writeBaselineDocument() below is a template and has to
// see Baseline's members to be instantiated by its callers (ADR-0026 decision 6).
#include "core/baseline.h"

#ifndef NATIVE_BUILD
#include <SD.h>
#include <FS.h>
#endif

namespace dupin {

// Forward declarations
struct Anomaly;

// Longest session name, in characters, excluding the terminator. The session name is
// also its folder name on the card, so this is the one bound for both.
//
// It used to be written three ways -- `len > 30` in isValidName(), and `[32]` on each of
// the two fields that store a name -- which is how the UI came to size a display buffer
// for 31 characters against a validator that admits 30. Stated once here so the fields,
// the validator and the callers that size buffers from it cannot disagree (ADR-0013).
constexpr size_t kMaxSessionNameChars = 30;

/**
 * @brief Why a session operation did or did not happen.
 *
 * These calls each know exactly which rule was broken and used to return `false` for all
 * of them, so a caller had nothing to report and the operator was told nothing at all
 * (ADR-0019). An `enum class` rather than a bool or a bare enum: the old call sites tested
 * the result directly, and a scoped type makes every one of them fail to compile until it
 * says which outcome it meant.
 */
enum class SessionResult {
    Ok,             ///< The operation completed.
    NoCard,         ///< No SD card is mounted, so there is nowhere to write.
    InvalidName,    ///< Empty, over kMaxSessionNameChars, or a character outside the class.
    AlreadyExists,  ///< A session of that name is already on the card.
    NotFound,       ///< The session named does not exist.
    UnexpectedContent, ///< The folder holds a subfolder, which Dupin never writes; left whole.
    DeleteIncomplete,  ///< A delete failed after removing files, so the case is partly gone.
    WriteFailed     ///< The card accepted the request and the operation still failed.
};

/**
 * @brief The operator-facing reason for @p result, short enough for the case-menu notice.
 *
 * Declared beside the enum rather than in the UI on purpose: a new SessionResult value and
 * the sentence that explains it have to arrive together, and the switch here has no
 * `default`, so adding one without a message fails the build instead of silently rendering
 * nothing. Being portable, it is also assertable by `make test` -- including that every
 * message fits the panel -- which a string living in main.cpp would not be (ADR-0019).
 *
 * Never returns nullptr; SessionResult::Ok maps to the empty string.
 */
const char* sessionResultMessage(SessionResult result);

/**
 * @brief Copy @p src into a fixed-size name field, always NUL-terminated.
 *
 * Four sites copy a name into a 32-byte field; two of them terminated explicitly and two
 * relied on the field's zero-initialiser leaving the last byte clear -- true, but an
 * invariant nobody stated and nothing enforced, with a bounded strcmp() downstream that
 * would have read past the field if it ever stopped holding (ADR-0013). This is the one
 * way a name is copied, so there is no second way to get it wrong.
 */
void copySessionName(char* dst, size_t dstSize, const char* src);

// ============================================
// The `<case>/baseline.json` document (ADR-0016, ADR-0017)
// ============================================
//
// Both halves are free functions outside any `#ifndef NATIVE_BUILD`, because formatting and
// parsing are portable and only the SD read and write are not. While they lived inside the
// platform cutout their host tests asserted the `#else` line and nothing more, which is how
// a parser that named every entry with a slice of the raw JSON shipped and stayed shipped
// (ADR-0003 decision 4).

// The document's own text, named once so the writer that emits it and the reader that checks
// for it cannot drift apart (ADR-0026 decision 3).
constexpr char kBaselineOpenFormat[] = "{\"currentSession\":%u,\"ssids\":[\n";
// The envelope at its widest: the format with an 11-digit counter in place of %u.
constexpr size_t kBaselineOpenBytes = sizeof(kBaselineOpenFormat) + 11;
constexpr char kBaselineEntrySeparator[] = "\n,\n";
// How a finished document ends. The newline carries the check: an entry is one snprintf with
// no newline in it and every octet that could carry one is escaped (ADR-0017), so a document
// cut short ends in entry text, in `]}` or in `\n,` -- never in `\n]}`. Without it the ending
// would be indistinguishable from any entry's last two characters.
constexpr char kBaselineDocumentEnd[] = "\n]}\n";
// The key that says a document carries the envelope, and so was written by a writer whose
// ending is the constant above. A document without it predates the envelope or is the empty
// one createSession writes, and is not checked (ADR-0026 decision 4). An SSID cannot forge
// it: a quote off the air always arrives escaped, and this one is not.
constexpr char kBaselineSessionKey[] = "\"currentSession\":";

// Every octet of an SSID can need escaping, and a control character costs six bytes as
// \uXXXX. SSIDs are arbitrary octets on the air, so this is a real bound, not a hypothetical.
constexpr size_t kEscapedSsidBytes = DUPIN_SSID_MAX_LEN * 6;
// "AA:BB:CC:DD:EE:FF" with its quotes and separating comma.
constexpr size_t kBssidTextBytes = 20;
// A channel number and its comma, for the 1..14 list.
constexpr size_t kChannelTextBytes = 3;
// The keys and punctuation of one entry, taken from the format itself so the two cannot
// drift; sizeof over-counts by one NUL per fragment, which is slack in the safe direction.
constexpr size_t kEntryKeyBytes = sizeof(
    "  {\"ssid\":\"\",\"security\":\"\",\"bssidCount\":,\"rssiMin\":,\"rssiMax\":,"
    "\"beacons\":,\"rssiSum\":,\"rssiCount\":,\"lastSeenSession\":,\"channels\":[],"
    "\"bssids\":[]}\n,\n");
// Widest each value can print: three 32-bit fields at 11 characters, the 64-bit rssiSum at 20
// (ADR-0029), two int8 at 4, one uint8 at 3, and the longest security name.
constexpr size_t kEntryValueBytes = 3 * 11 + 20 + 2 * 4 + 3 + sizeof("Unknown");

/** Ceiling on one serialised entry. `formatBaselineEntry` never exceeds it. */
constexpr size_t kMaxBaselineEntryBytes =
    kEntryKeyBytes + kEntryValueBytes + kEscapedSsidBytes +
    DUPIN_MAX_BSSIDS_PER_SSID * kBssidTextBytes +
    DUPIN_MAX_CHANNELS * kChannelTextBytes;

/**
 * @brief Ceiling on a whole baseline document, read into RAM before parsing.
 *
 * Derived from the format rather than written down. The number this replaced was an
 * estimate -- "roughly 12KB" against a real worst case of 19589 bytes -- so a full baseline
 * was written without complaint and then refused on read, which `openCase` reported as the
 * case having no baseline at all (ADR-0017). A ceiling that does not follow the format is a
 * ceiling that goes stale the first time a field is added.
 */
constexpr size_t kMaxBaselineFileBytes =
    kBaselineOpenBytes + sizeof(kBaselineDocumentEnd) +
    DUPIN_MAX_SSIDS * kMaxBaselineEntryBytes;

/**
 * @brief Serialise one baseline entry as the document's JSON object.
 *
 * @param entry   Entry to write. Its SSID is JSON-escaped, so an SSID containing a quote or
 *                a backslash cannot end the field early or inject structure (ADR-0017).
 * @param out     Destination, NUL-terminated on success.
 * @param outSize Size of @p out; `kMaxBaselineEntryBytes` always suffices.
 * @return Characters written, excluding the terminator; 0 if @p out was too small, which
 *         the caller must treat as a failure rather than as an empty entry.
 */
size_t formatBaselineEntry(const SSIDBaseline& entry, char* out, size_t outSize);

/**
 * @brief How writing a baseline document ended.
 *
 * Three outcomes rather than a bool, because two of them are faults in this firmware and one
 * is a fault in the card, and a caller that cannot tell them apart reports the wrong cause to
 * the operator (ADR-0026 decision 5).
 */
enum class BaselineWrite : uint8_t {
    Complete,       ///< Every byte of the document reached the sink.
    ShortWrite,     ///< The sink took fewer bytes than it was handed: the card is full or gone.
    FormatOverflow  ///< A fragment would not fit the buffer derived for it -- the ceiling and
                    ///< the format have drifted apart, which is a bug here, not a card fault.
};

struct BaselineWriteResult {
    /// Bytes the sink accepted -- the document's length when the write completed, and how far
    /// into it the card got when it did not.
    size_t bytes = 0;
    /// Fails closed: a path that returns without saying it finished reports a failure, not a
    /// save. Only the end of writeBaselineDocument() sets Complete.
    BaselineWrite status = BaselineWrite::ShortWrite;
};

/**
 * @brief Write @p baseline as its `<case>/baseline.json` document into @p sink.
 *
 * @tparam Sink Anything with `size_t write(const char* text, size_t length)` returning the
 *              number of bytes it accepted. The device hands it the open `File`; `make test`
 *              hands it one that goes short at a chosen byte. That second instantiation is
 *              the point: no gate can reach this, because making a card refuse a write
 *              mid-document needs a capability invariant 14 keeps off the serial channel, so
 *              the host test is the only thing that ever drives the failure (ADR-0026).
 *
 * Every write is checked against its own length. `fwrite`, which is what a `File` write
 * reaches, returns fewer bytes than asked when the card is full or failing -- and the four
 * returns this loop used to discard are why a stop could report a save that left the document
 * without its ending (ADR-0026 decision 1).
 *
 * Stops at the first short write: the rest would be handed to a card that has already said
 * no, and the caller reports a failure either way.
 */
template <typename Sink>
BaselineWriteResult writeBaselineDocument(const Baseline& baseline, Sink& sink) {
    BaselineWriteResult result;

    // What the sink accepted is counted whether or not it accepted all of it. The bytes a card
    // took before it stopped are the only thing that says *where* it stopped, and the failure
    // this reports is read off a log line that quotes this number (ADR-0026).
    const auto put = [&](const char* text, size_t length) {
        const size_t accepted = sink.write(text, length);
        result.bytes += accepted;
        return accepted == length;
    };

    char open[kBaselineOpenBytes];
    const int opened = snprintf(open, sizeof(open), kBaselineOpenFormat,
                                static_cast<unsigned>(baseline.getCurrentSession()));
    if (opened < 0 || static_cast<size_t>(opened) >= sizeof(open)) {
        result.status = BaselineWrite::FormatOverflow;
        return result;
    }
    if (!put(open, static_cast<size_t>(opened))) return result;

    char entryText[kMaxBaselineEntryBytes];
    bool first = true;
    for (size_t i = 0; i < baseline.capacity(); i++) {
        const SSIDBaseline* entry = baseline.at(i);
        if (!entry || !entry->isActive()) continue;

        const size_t entryBytes = formatBaselineEntry(*entry, entryText, sizeof(entryText));
        if (entryBytes == 0) {
            // kMaxBaselineEntryBytes is derived from this very format, so this cannot happen
            // without the two having drifted apart -- which is exactly when writing a
            // half-formed document would be worse than writing none.
            result.status = BaselineWrite::FormatOverflow;
            return result;
        }

        if (!first && !put(kBaselineEntrySeparator, sizeof(kBaselineEntrySeparator) - 1)) {
            return result;
        }
        first = false;
        if (!put(entryText, entryBytes)) return result;
    }

    if (!put(kBaselineDocumentEnd, sizeof(kBaselineDocumentEnd) - 1)) return result;

    result.status = BaselineWrite::Complete;
    return result;
}

/**
 * @brief How reading a baseline document ended.
 *
 * The reader used to return a count for all three, so a document that stopped halfway came
 * back as a case that simply holds fewer SSIDs than the operator remembers (ADR-0026).
 */
enum class BaselineDocument : uint8_t {
    Complete,   ///< The document ended the way the writer ends one, or predates that writer.
    Truncated,  ///< It stops before its end: the write that produced it did not finish.
    Overflowed  ///< The baseline filled before the document ran out -- more SSIDs than fit.
};

struct BaselineReadResult {
    size_t entries = 0;  ///< Entries parsed, which are kept even when the document is damaged.
    BaselineDocument status = BaselineDocument::Complete;
};

/**
 * @brief Parse a `<case>/baseline.json` document into @p baseline.
 *
 * @param json     NUL-terminated document.
 * @param baseline Destination. Entries are added to whatever it already holds, so the
 *                 caller clears it first when replacing rather than merging (ADR-0016).
 * @return The entries parsed, and which of three ways the document ended. Entries are kept
 *         when it ended badly: they are real observations, and the defect this reports was
 *         the silence, not the partial data (ADR-0026 decision 8).
 *
 * Also sets @p baseline's session counter from the document's envelope, or to zero when the
 * document carries none. The counter belongs to the document rather than to the entries read
 * out of it, and leaving the previous case's value in place would carry stale-entry expiry
 * across a case boundary exactly as the entries themselves once were (ADR-0017).
 *
 * Documents written before the envelope existed are a bare array with no `currentSession`
 * and no per-entry `lastSeenSession`, `channels`, `rssiSum` or `rssiCount`. They parse: every
 * field is looked up by key and absent keys leave their defaults. They are also reported
 * `Complete` without being checked, because their ending is not the one this writer emits and
 * nothing distinguishes it from a truncation (ADR-0026 decision 4).
 */
BaselineReadResult parseBaselineJson(const char* json, Baseline& baseline);

/**
 * @brief Session metadata
 */
struct SessionMeta {
    char name[kMaxSessionNameChars + 1] = {0};
    uint32_t created = 0;
    uint32_t lastUsed = 0;
    uint16_t ssidCount = 0;
    uint16_t anomalyCount = 0;
    
    bool isValid() const { return name[0] != '\0'; }
};

/**
 * @brief What a listing found, and whether it fitted.
 *
 * A bare count cannot tell a complete list from one that filled the caller's array, which
 * is how the seventeenth case used to disappear without a word — the same rule invariant 21
 * states for failures, applied to an incomplete answer (ADR-0023).
 */
struct SessionListing {
    size_t count = 0;        ///< Entries written into the caller's array.
    bool truncated = false;  ///< The card holds at least one more case than fitted.
};

/**
 * @brief Case statistics for comprehensive session metrics
 */
struct CaseStats {
    uint32_t totalUptime = 0;       // Total seconds of capture time
    uint32_t sessionCount = 1;      // Number of capture sessions
    
    // Packet counts
    uint32_t packetsTotal = 0;
    uint32_t beacons = 0;
    uint32_t probes = 0;
    uint32_t deauths = 0;
    
    // Baseline counts
    uint16_t ssidCount = 0;
    uint16_t bssidCount = 0;
    
    // Detection counts
    uint16_t anomalyCount = 0;
    uint16_t findingCount = 0;
    uint16_t hypothesisCount = 0;
};

// ============================================
// Case metadata document (`<case>/case.json`)
// ============================================
//
// Outside any `#ifndef NATIVE_BUILD`, like the baseline half above and for the same reason:
// formatting is portable and only the SD write is not. While this document was two literals
// inside the cutout, the host compiled neither, so `-Werror=format` -- the gate invariant 6
// rests on -- was never handed the specifiers to check, and three bare `%lu` sat in a
// `uint32_t` argument list for as long as nobody read them (ADR-0027, ADR-0003 decision 4).

/**
 * @brief The document's text, named once so the ceiling below is derived from it.
 *
 * It carries no timestamp. `created` and `lastSaved` used to sit here holding
 * `millis() / 1000` -- seconds since *that* boot, on a build with no RTC anywhere in it -- so
 * `created` recorded how long after power-on the case was made and `lastSaved` moved backwards
 * across a reset. A key that cannot mean what it says is deleted rather than renamed
 * (ADR-0027 decision 4).
 */
constexpr char kCaseMetadataFormat[] =
    "{\"name\":\"%s\",\"sessions\":%u,\"totalUptime\":%u,"
    "\"stats\":{\"packetsTotal\":%u,\"beacons\":%u,\"probes\":%u,\"deauths\":%u},"
    "\"baseline\":{\"ssidCount\":%u,\"bssidCount\":%u},"
    "\"detections\":{\"anomalies\":%u,\"findings\":%u,\"hypotheses\":%u}}";

// The keys and punctuation alone, taken from the format with its specifiers removed so the two
// cannot drift. The NUL sizeof counts is not slack: it is the terminator snprintf writes, and
// the widest document fills this ceiling to the byte -- 455 characters and a NUL in 456.
constexpr size_t kCaseMetadataKeyBytes = sizeof(
    "{\"name\":\"\",\"sessions\":,\"totalUptime\":,"
    "\"stats\":{\"packetsTotal\":,\"beacons\":,\"probes\":,\"deauths\":},"
    "\"baseline\":{\"ssidCount\":,\"bssidCount\":},"
    "\"detections\":{\"anomalies\":,\"findings\":,\"hypotheses\":}}");
// Widest each value can print: six `uint32_t` at ten digits, five `uint16_t` at five.
constexpr size_t kCaseMetadataValueBytes = 6 * 10 + 5 * 5;
// A case name is escaped like any other text going into a structured document, and an escaped
// octet costs six bytes as \uXXXX (ADR-0027 decision 6).
constexpr size_t kEscapedCaseNameBytes = kMaxSessionNameChars * 6;

/** Ceiling on the whole document. `formatCaseMetadata` never exceeds it (invariant 20). */
constexpr size_t kMaxCaseMetadataBytes =
    kCaseMetadataKeyBytes + kCaseMetadataValueBytes + kEscapedCaseNameBytes;

/**
 * @brief Serialise a case's metadata document.
 *
 * One formatter for both writers. A new case is the same document with a default-constructed
 * @p stats, whose `sessionCount` is already 1 -- when creation and saving held a literal each,
 * the saving one had no `created` key, so the first save deleted a field the creation wrote
 * (ADR-0027 decision 3).
 *
 * @param name    Case name, JSON-escaped on the way in. A name reaching the saving path came
 *                off a folder on the card rather than through `isValidName`, and escaping is a
 *                property of the document rather than a claim about today's callers
 *                (invariant 19).
 * @param stats   Figures to record.
 * @param out     Destination, NUL-terminated on success.
 * @param outSize Size of @p out; `kMaxCaseMetadataBytes` always suffices.
 * @return Characters written, excluding the NUL, or 0 when it did not fit. A document that
 *         does not fit is not written: a truncated one is invalid JSON, and a caller that
 *         wrote it anyway would leave a case whose metadata no parser can open.
 */
size_t formatCaseMetadata(const char* name, const CaseStats& stats, char* out, size_t outSize);

/**
 * @brief How the boot mount went, kept for the read-only `probe` report.
 *
 * The boot log says the same, but only to a host that was listening when the card was mounted.
 * A board powered up off the bench, or launched from M5Launcher, has printed it to nobody by the
 * time the port is reopened, and those are the boots scripts/sd-mount-probe.mjs observes
 * (ADR-0032).
 */
struct MountReport {
    uint8_t attempts = 0;
    /// The SPI clock the card mounted at; 0 when no attempt mounted it.
    uint32_t mountedHz = 0;
};

/**
 * @brief SD Card Session Manager
 * 
 * Provides persistent storage for session data on SD card.
 * Sessions are stored in /dupin/<session_name>/ folders.
 */
class SessionManager {
public:
    SessionManager() = default;
    ~SessionManager();
    
    // ============================================
    // SD Card Lifecycle
    // ============================================
    
    /**
     * @brief Initialize SD card
     * @return true if SD card mounted successfully
     */
    bool begin();
    
    /**
     * @brief Check if SD card is available
     */
    bool isAvailable() const { return m_sdAvailable; }

    /** @brief What the mounting begin() call did; default-constructed until one has run. */
    const MountReport& mountReport() const { return m_mountReport; }

    /**
     * @brief Unmount SD card safely
     */
    void end();
    
    // ============================================
    // Session Management
    // ============================================
    
    /**
     * @brief Get list of available sessions
     * @param sessions Array to fill with session metadata
     * @param maxSessions Max sessions to return
     * @return How many were written, and whether the card held more (ADR-0023)
     *
     * `truncated` is a flag rather than a count of the surplus: counting it means walking
     * the rest of a directory of unknown length for a number the operator cannot act on,
     * while knowing the card holds more is what tells them to delete or rename.
     */
    SessionListing listSessions(SessionMeta* sessions, size_t maxSessions);
    
    /**
     * @brief Create a new session
     * @param name Session name (alphanumeric + underscore only)
     * @return SessionResult::Ok, or which rule refused it (ADR-0019)
     */
    SessionResult createSession(const char* name);
    
    /**
     * @brief Delete a session's folder and every file in it
     *
     * Checks the whole folder before removing anything, so a folder holding a subfolder or an
     * entry whose path does not fit is refused untouched rather than half-deleted. Removal
     * stops at the first file the card will not remove (ADR-0020).
     *
     * @param name Session name
     * @return SessionResult::Ok, or which rule refused it
     */
    SessionResult deleteSession(const char* name);
    
    /**
     * @brief Rename a session
     * @param oldName Current name
     * @param newName New name
     * @return SessionResult::Ok, or which rule refused it (ADR-0019)
     */
    SessionResult renameSession(const char* oldName, const char* newName);
    
    /**
     * @brief Load a session
     * @param name Session name
     * @return true if loaded successfully
     */
    bool loadSession(const char* name);
    
    /**
     * @brief Get current session name
     */
    const char* currentSessionName() const { return m_currentSession; }
    
    /**
     * @brief Check if a session is currently loaded
     */
    bool hasSession() const { return m_currentSession[0] != '\0'; }
    
    // ============================================
    // Data I/O
    // ============================================
    
    /**
     * @brief Save baseline to current session
     */
    bool saveBaseline(const Baseline& baseline);
    
    /**
     * @brief Load the current case's baseline into @p baseline.
     * @return Number of entries read out of this case's file; 0 if there were none.
     *
     * Adds to whatever @p baseline holds. The caller clears it first, because it must be
     * cleared even when this refuses -- a baseline belongs to exactly one case, and
     * leaving the previous case's entries behind on the failure path is the whole defect
     * ADR-0016 removes.
     *
     * The count is returned rather than a bool so that callers -- and the gate reading
     * their logs -- can tell what this case held apart from what the baseline now totals.
     * While those two were the same number, nothing could distinguish a baseline that was
     * replaced from one that was merged onto the previous case's.
     */
    size_t loadBaseline(Baseline& baseline);
    
    /**
     * @brief Append anomaly to session log
     */
    bool logAnomaly(const Anomaly& anomaly);
    
    /**
     * @brief Append finding to session log
     */
    bool logFinding(const struct Finding& finding);
    
    /**
     * @brief Append hypothesis to session log
     */
    bool logHypothesis(const struct Hypothesis& hypothesis);
    
    /**
     * @brief Get anomaly count from current session
    /**
     * @brief Get anomaly count from current session
     */
    uint32_t getAnomalyCount();
    
    /**
     * @brief Save comprehensive case statistics to case.json
     */
    bool saveCaseStats(const CaseStats& stats);
    
    // ============================================
    // File Helpers (for testing)
    // ============================================
    
    /**
     * @brief Write string to file
     */
    bool writeFile(const char* path, const char* data);
    
    /**
     * @brief Read file contents
     * @param path File path
     * @param buffer Output buffer
     * @param maxLen Max bytes to read
     * @return Bytes read, or -1 on error
     */
    int readFile(const char* path, char* buffer, size_t maxLen);
    
    /**
     * @brief Append string to file
     */
    bool appendFile(const char* path, const char* data);
    
    /**
     * @brief Check if file exists
     */
    bool fileExists(const char* path);
    
    /**
     * @brief Create directory (recursive)
     */
    bool createDir(const char* path);
    
    // ============================================
    // Validation
    // ============================================
    
    /**
     * @brief Is @p c admissible in a session name?
     *
     * The one statement of the character class. The case-menu input filter held a
     * byte-for-byte copy of it, which is how the UI came to reject bad characters at the
     * keyboard while the validator's own rejection path sat unreachable and untested
     * (ADR-0019). Both call this now.
     */
    static bool isValidNameChar(char c);

    /**
     * @brief Validate session name (alphanumeric + underscore)
     */
    static bool isValidName(const char* name);

private:
    bool m_sdAvailable = false;
    MountReport m_mountReport;
    char m_currentSession[kMaxSessionNameChars + 1] = {0};
    
    // Base path for all sessions
    static constexpr const char* BASE_PATH = "/dupin";
    
    // Get full path for session folder
    void getSessionPath(const char* name, char* path, size_t pathLen);
    
    // Get path for specific file in session
    void getFilePath(const char* filename, char* path, size_t pathLen);
};

// Global session manager instance
extern SessionManager g_sessionManager;

} // namespace dupin
