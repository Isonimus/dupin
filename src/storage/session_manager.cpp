/**
 * @file session_manager.cpp
 * @brief SD Card Session Management implementation
 */

#include "storage/session_manager.h"
#include "core/baseline.h"
#include "core/anomaly.h"
#include "core/finding.h"
#include "core/hypothesis.h"
#include "utils/mac_utils.h"
#include "utils/file_utils.h"
#include <cinttypes>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifndef NATIVE_BUILD
#include <SD.h>
#include <SPI.h>
#endif

namespace dupin {

void copySessionName(char* dst, size_t dstSize, const char* src) {
    if (dst == nullptr || dstSize == 0) return;
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    // strncpy pads but does not terminate when the source fills the field, so the
    // terminator is written unconditionally rather than left to the caller to remember.
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

// Global instance
SessionManager g_sessionManager;

// M5Cardputer ADV SD card SPI pins (from M5Stack documentation)
// https://github.com/m5stack/M5Cardputer
#ifndef NATIVE_BUILD
constexpr int SD_CS_PIN = 12;    // Chip Select
constexpr int SD_MOSI_PIN = 14;  // MOSI (NOT 11!)
constexpr int SD_MISO_PIN = 39;  // MISO
constexpr int SD_CLK_PIN = 40;   // Clock (SCK)
#endif

SessionManager::~SessionManager() {
    end();
}

bool SessionManager::begin() {
#ifndef NATIVE_BUILD
    if (m_sdAvailable) {
        return true;  // Already initialized
    }
    
    // No hand-rolled reset ahead of SD.begin(), which already clocks the card idle and sends CMD0
    // at 400 kHz itself. One was here to recover a card M5Launcher left mid-command; with and
    // without it the cards on hand mounted 45 boots out of 45, the launcher boot included, so it
    // was 60 lines of SPI with nothing measured behind them (ADR-0032).
    Serial.println("[SD] Initializing...");

    // First, ensure any previous SD state is cleaned up
    SD.end();
    SPI.end();
    delay(100);

    // Progressive SPI speeds to retry - some cards need slower initial speed
    const uint32_t speeds[] = { 10000000, 20000000, 25000000 };  // 10, 20, 25 MHz
    const int numSpeeds = sizeof(speeds) / sizeof(speeds[0]);
    
    for (int attempt = 0; attempt < numSpeeds; attempt++) {
        uint32_t speed = speeds[attempt];
        m_mountReport.attempts = static_cast<uint8_t>(attempt + 1);
        Serial.printf("[SD] Init attempt %d: %u MHz\n", attempt + 1,
                      static_cast<unsigned>(speed / 1000000));
        
        // Ensure CS is high before SPI init
        digitalWrite(SD_CS_PIN, HIGH);
        
        // Initialize SPI with M5Cardputer ADV pins
        SPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
        
        // Try to mount SD card
        if (SD.begin(SD_CS_PIN, SPI, speed)) {
            // Check card type
            uint8_t cardType = SD.cardType();
            if (cardType != CARD_NONE) {
                // Success!
                Serial.printf("[SD] Card type: ");
                switch (cardType) {
                    case CARD_MMC:  Serial.println("MMC"); break;
                    case CARD_SD:   Serial.println("SDSC"); break;
                    case CARD_SDHC: Serial.println("SDHC"); break;
                    default:        Serial.println("UNKNOWN"); break;
                }
                
                uint64_t cardSize = SD.cardSize() / (1024 * 1024);
                // PRIu64, not %llu: uint64_t is `unsigned long long` here and `unsigned long`
                // on the host, and this line sits where the host compiler never looks (ADR-0028).
                Serial.printf("[SD] Card size: %" PRIu64 " MB\n", cardSize);
                
                // Create base directory if it doesn't exist
                if (!SD.exists(BASE_PATH)) {
                    if (!SD.mkdir(BASE_PATH)) {
                        Serial.println("[SD] Failed to create /dupin directory");
                        m_sdAvailable = false;
                        return false;
                    }
                    Serial.println("[SD] Created /dupin directory");
                }
                
                m_sdAvailable = true;
                m_mountReport.mountedHz = speed;
                Serial.printf("[SD] Initialized successfully at %u MHz\n",
                              static_cast<unsigned>(speed / 1000000));
                return true;
            }
        }
        
        // Clean up before next attempt
        SD.end();
        SPI.end();
        
        if (attempt < numSpeeds - 1) {
            Serial.println("[SD] Retrying in 100ms...");
            delay(100);
        }
    }
    
    Serial.println("[SD] Mount failed after all attempts - no card or incompatible");
    m_sdAvailable = false;
    return false;
#else
    // Native build - mock success
    m_sdAvailable = true;
    return true;
#endif
}

void SessionManager::end() {
#ifndef NATIVE_BUILD
    if (m_sdAvailable) {
        SD.end();
        Serial.println("[SD] Unmounted");
    }
#endif
    // Always reset state (works in both native and device builds)
    m_sdAvailable = false;
    m_currentSession[0] = '\0';
}

// ============================================
// Session Management
// ============================================

SessionListing SessionManager::listSessions(SessionMeta* sessions, size_t maxSessions) {
    SessionListing listing;
    if (!m_sdAvailable || sessions == nullptr || maxSessions == 0) {
        return listing;
    }

#ifndef NATIVE_BUILD
    File root = SD.open(BASE_PATH);
    if (!root || !root.isDirectory()) {
        return listing;
    }

    File entry;
    while ((entry = root.openNextFile())) {
        if (entry.isDirectory()) {
            const char* name = entry.name();

            // Extract folder name from path
            const char* lastSlash = strrchr(name, '/');
            const char* folderName = lastSlash ? lastSlash + 1 : name;

            // Hidden folders are not cases. The `continue` this replaces skipped the close
            // below, leaking a handle per hidden directory (ADR-0023).
            if (folderName[0] != '.' && isValidName(folderName)) {
                if (listing.count == maxSessions) {
                    // One case beyond the array is all the caller needs to know; walking the
                    // rest of the directory to count them costs reads for a number nobody
                    // acts on (ADR-0023).
                    listing.truncated = true;
                    entry.close();
                    break;
                }

                copySessionName(sessions[listing.count].name,
                                sizeof(sessions[listing.count].name), folderName);

                // TODO: Load metadata from session.json
                sessions[listing.count].ssidCount = 0;
                sessions[listing.count].anomalyCount = 0;

                listing.count++;
            }
        }
        entry.close();
    }
    root.close();

    return listing;
#else
    return listing;
#endif
}

SessionResult SessionManager::createSession(const char* name) {
    // Each refusal is named rather than collapsed into one `false`. The reason is known
    // here and nowhere else, so a bare bool left the caller with nothing to show the
    // operator standing at the case menu (ADR-0019).
    if (!m_sdAvailable) {
        return SessionResult::NoCard;
    }
    if (name == nullptr || !isValidName(name)) {
        return SessionResult::InvalidName;
    }
    
    char sessionPath[64];
    getSessionPath(name, sessionPath, sizeof(sessionPath));
    
#ifndef NATIVE_BUILD
    // Check if already exists
    if (SD.exists(sessionPath)) {
        Serial.printf("[SD] Session '%s' already exists\n", name);
        return SessionResult::AlreadyExists;
    }
    
    // Create session folder
    if (!SD.mkdir(sessionPath)) {
        Serial.printf("[SD] Failed to create session folder: %s\n", sessionPath);
        return SessionResult::WriteFailed;
    }
    
    // Create empty baseline.json
    char filePath[80];
    snprintf(filePath, sizeof(filePath), "%s/baseline.json", sessionPath);
    // Both writes are checked: returning Ok over a write that failed would open a case
    // the card never received, which is the silence this return type exists to end
    // (ADR-0019). The folder is left in place on failure -- it is openable, and a case
    // with no baseline.json reads as an empty one, not a corrupt one.
    //
    // This literal is deliberately not assembled from the writer's constants and deliberately
    // carries no envelope. It parses to exactly what the writer's empty document parses to --
    // an absent currentSession reads as zero -- and having no envelope is what keeps the
    // reader from checking it for an ending it was never given (ADR-0026 decision 4). Writing
    // it through writeBaselineDocument() would mean standing up a 64-entry Baseline on the
    // stack of a task that has no use for one.
    if (!writeFile(filePath, "{\"ssids\":[]}")) {
        Serial.printf("[SD] Failed to write %s\n", filePath);
        return SessionResult::WriteFailed;
    }
    
    // Create case.json with initial stats
    snprintf(filePath, sizeof(filePath), "%s/case.json", sessionPath);
    // A default CaseStats is a new case: no capture time, no packets, no detections, and
    // sessionCount already 1. The initial document is the saved one with those figures, so a
    // key cannot be added to one writer and forgotten in the other (ADR-0027 decision 3).
    char meta[kMaxCaseMetadataBytes];
    if (formatCaseMetadata(name, CaseStats{}, meta, sizeof(meta)) == 0) {
        Serial.printf("[ERROR] case metadata for %s exceeded its own format ceiling\n", name);
        return SessionResult::WriteFailed;
    }
    if (!writeFile(filePath, meta)) {
        Serial.printf("[SD] Failed to write %s\n", filePath);
        return SessionResult::WriteFailed;
    }
    
    Serial.printf("[SD] Created session: %s\n", name);
    return SessionResult::Ok;
#else
    return SessionResult::Ok;
#endif
}

#ifndef NATIVE_BUILD
// Room for "/dupin/<name>/<file>" with any name Dupin writes to spare; an entry that does not
// fit was not written by Dupin, and is refused rather than truncated into another file's path.
static constexpr size_t kCaseFilePathChars = 96;

/**
 * Format @p entry's full path under @p folder. False when it does not fit: a truncated path
 * would name a different file, or none.
 */
static bool formatEntryPath(char* out, size_t outLen, const char* folder, File& entry) {
    // Depending on the core, name() is the base name or the full path; take the base either way.
    const char* entryName = entry.name();
    const char* lastSlash = strrchr(entryName, '/');
    const char* baseName = lastSlash ? lastSlash + 1 : entryName;
    const int written = snprintf(out, outLen, "%s/%s", folder, baseName);
    return written >= 0 && static_cast<size_t>(written) < outLen;
}
#endif

SessionResult SessionManager::deleteSession(const char* name) {
    if (!m_sdAvailable) {
        return SessionResult::NoCard;
    }
    if (name == nullptr || !isValidName(name)) {
        return SessionResult::InvalidName;
    }

#ifndef NATIVE_BUILD
    char sessionPath[64];
    getSessionPath(name, sessionPath, sizeof(sessionPath));

    if (!SD.exists(sessionPath)) {
        return SessionResult::NotFound;
    }

    // Account for everything before removing anything: a folder holding what Dupin did not
    // write is left whole, not half-deleted and then refused (ADR-0020).
    char filePath[kCaseFilePathChars];
    File dir = SD.open(sessionPath);
    if (!dir || !dir.isDirectory()) {
        return SessionResult::UnexpectedContent;
    }
    for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
        const bool accountedFor = !entry.isDirectory() &&
                                  formatEntryPath(filePath, sizeof(filePath), sessionPath, entry);
        entry.close();
        if (!accountedFor) {
            dir.close();
            return SessionResult::UnexpectedContent;
        }
    }
    dir.close();

    // Removing entries from a directory while iterating it is not a traversal FAT promises to
    // finish, so each pass reopens the folder and removes its first file. A pass either removes
    // one file or returns, so the loop ends.
    //
    // Once one file is gone a failure is no longer "nothing happened": the case still lists,
    // but not with what it held, and saying only that a write failed would let the operator
    // believe it intact (ADR-0020).
    bool removedAny = false;
    const auto failure = [&removedAny]() {
        return removedAny ? SessionResult::DeleteIncomplete : SessionResult::WriteFailed;
    };
    for (;;) {
        File folder = SD.open(sessionPath);
        if (!folder) {
            return failure();
        }
        File entry = folder.openNextFile();
        if (!entry) {
            folder.close();
            break;
        }
        const bool fits = formatEntryPath(filePath, sizeof(filePath), sessionPath, entry);
        entry.close();
        folder.close();
        if (!fits) {
            // Every name was checked above, so this is the card answering differently now.
            return removedAny ? SessionResult::DeleteIncomplete : SessionResult::UnexpectedContent;
        }
        if (!SD.remove(filePath)) {
            Serial.printf("[ERROR] Could not remove %s\n", filePath);
            return failure();
        }
        removedAny = true;
    }

    if (!SD.rmdir(sessionPath)) {
        Serial.printf("[ERROR] Could not remove folder %s\n", sessionPath);
        return failure();
    }
    Serial.printf("[SD] Deleted case %s\n", name);

    if (strcmp(m_currentSession, name) == 0) {
        m_currentSession[0] = '\0';
    }
    return SessionResult::Ok;
#else
    // No card to delete from on the host; begin() claims one, so say the write did not happen.
    return SessionResult::WriteFailed;
#endif
}

SessionResult SessionManager::renameSession(const char* oldName, const char* newName) {
    // Five distinct refusals used to leave here as one `false`, and main.cpp did not even
    // read it -- a failed rename repainted the list under its old name and looked like a
    // keypress that did nothing (ADR-0019).
    if (!m_sdAvailable) {
        return SessionResult::NoCard;
    }
    if (oldName == nullptr || newName == nullptr) {
        return SessionResult::InvalidName;
    }
    if (!isValidName(oldName) || !isValidName(newName)) {
        return SessionResult::InvalidName;
    }
    
#ifndef NATIVE_BUILD
    char oldPath[64], newPath[64];
    getSessionPath(oldName, oldPath, sizeof(oldPath));
    getSessionPath(newName, newPath, sizeof(newPath));
    
    if (!SD.exists(oldPath)) {
        return SessionResult::NotFound;
    }
    if (SD.exists(newPath)) {
        Serial.printf("[SD] Cannot rename: '%s' already exists\n", newName);
        return SessionResult::AlreadyExists;
    }
    
    if (SD.rename(oldPath, newPath)) {
        Serial.printf("[SD] Renamed '%s' to '%s'\n", oldName, newName);
        
        // Update current session name if needed
        if (strcmp(m_currentSession, oldName) == 0) {
            copySessionName(m_currentSession, sizeof(m_currentSession), newName);
        }
        return SessionResult::Ok;
    }
    return SessionResult::WriteFailed;
#else
    // The host build has no card to rename on, and saying Ok here would let a test assert
    // a rename that never happened.
    return SessionResult::WriteFailed;
#endif
}

bool SessionManager::loadSession(const char* name) {
    if (!m_sdAvailable || name == nullptr || !isValidName(name)) {
        return false;
    }
    
#ifndef NATIVE_BUILD
    char sessionPath[64];
    getSessionPath(name, sessionPath, sizeof(sessionPath));
    
    if (!SD.exists(sessionPath)) {
        Serial.printf("[SD] Session not found: %s\n", name);
        return false;
    }
    
    copySessionName(m_currentSession, sizeof(m_currentSession), name);
    
    Serial.printf("[SD] Loaded session: %s\n", name);
    return true;
#else
    copySessionName(m_currentSession, sizeof(m_currentSession), name);
    return true;
#endif
}

// ============================================
// File I/O Helpers
// ============================================

bool SessionManager::writeFile(const char* path, const char* data) {
    if (!m_sdAvailable || path == nullptr || data == nullptr) {
        return false;
    }
    return dupin::writeFile(path, data);
}

int SessionManager::readFile(const char* path, char* buffer, size_t maxLen) {
    if (!m_sdAvailable || path == nullptr || buffer == nullptr || maxLen == 0) {
        return -1;
    }
    size_t bytesRead = dupin::readFile(path, buffer, maxLen);
    return bytesRead > 0 ? static_cast<int>(bytesRead) : -1;
}

bool SessionManager::appendFile(const char* path, const char* data) {
    if (!m_sdAvailable || path == nullptr || data == nullptr) {
        return false;
    }
    return dupin::appendLine(path, data);
}

bool SessionManager::fileExists(const char* path) {
    if (!m_sdAvailable || path == nullptr) {
        return false;
    }
    return dupin::fileExists(path);
}

bool SessionManager::createDir(const char* path) {
    if (!m_sdAvailable || path == nullptr) {
        return false;
    }
    
#ifndef NATIVE_BUILD
    if (SD.exists(path)) {
        return true;  // Already exists
    }
    return SD.mkdir(path);
#else
    return true;
#endif
}

// ============================================
// Path Helpers
// ============================================

void SessionManager::getSessionPath(const char* name, char* path, size_t pathLen) {
    snprintf(path, pathLen, "%s/%s", BASE_PATH, name);
}

void SessionManager::getFilePath(const char* filename, char* path, size_t pathLen) {
    snprintf(path, pathLen, "%s/%s/%s", BASE_PATH, m_currentSession, filename);
}

// ============================================
// Validation
// ============================================

const char* sessionResultMessage(SessionResult result) {
    // No `default`: -Wswitch makes an unhandled value a build failure, which is the point
    // of putting this next to the enum (ADR-0019). Each string is kept inside
    // kCaseNoticeMaxChars, and `make test` asserts that rather than trusting the count.
    switch (result) {
        case SessionResult::Ok:            return "";
        case SessionResult::NoCard:        return "No SD card";
        case SessionResult::InvalidName:   return "Name: A-Z a-z 0-9 _ only, 1-30 chars";
        case SessionResult::AlreadyExists: return "A case of that name exists";
        case SessionResult::NotFound:      return "Case not found on card";
        case SessionResult::UnexpectedContent: return "Case folder has unknown content";
        case SessionResult::DeleteIncomplete:  return "Delete incomplete: card write failed";
        case SessionResult::WriteFailed:   return "Card write failed";
    }
    return "";
}

bool SessionManager::isValidNameChar(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           (c == '_');
}

bool SessionManager::isValidName(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    
    size_t len = strlen(name);
    if (len > kMaxSessionNameChars) {
        return false;  // Too long
    }
    
    for (size_t i = 0; i < len; i++) {
        if (!isValidNameChar(name[i])) {
            return false;
        }
    }
    
    return true;
}

// Note: saveBaseline, loadBaseline, logAnomaly, logFinding, logHypothesis
// are implemented at the end of this file with full JSON support

uint32_t SessionManager::getAnomalyCount() {
    if (!hasSession()) {
        return 0;
    }
    
    char path[80];
    getFilePath("anomalies.log", path, sizeof(path));
    
#ifndef NATIVE_BUILD
    char buffer[4096];
    int bytesRead = readFile(path, buffer, sizeof(buffer));
    if (bytesRead <= 0) {
        return 0;
    }
    
    // Count newlines
    uint32_t count = 0;
    for (int i = 0; i < bytesRead; i++) {
        if (buffer[i] == '\n') count++;
    }
    return count;
#else
    return 0;
#endif
}

bool SessionManager::saveCaseStats(const CaseStats& stats) {
    if (!hasSession()) {
        return false;
    }
    
    char path[80];
    getFilePath("case.json", path, sizeof(path));
    
    char meta[kMaxCaseMetadataBytes];
    if (formatCaseMetadata(m_currentSession, stats, meta, sizeof(meta)) == 0) {
        // The formatting is portable and the reporting is not, the same seam saveBaseline
        // sits on: the host mock Serial carries no printf, and it cannot gain one while
        // time_utils.h supplies the host's millis() -- including <Arduino.h> here would
        // define it twice. The refusal itself is asserted by make test through the return.
#ifndef NATIVE_BUILD
        Serial.printf("[ERROR] case metadata for %s exceeded its own format ceiling\n",
                      m_currentSession);
#endif
        return false;
    }

    return writeFile(path, meta);
}

bool SessionManager::logAnomaly(const Anomaly& anomaly) {
    if (!hasSession() || !m_sdAvailable) {
        return false;
    }
    
    char path[80];
    getFilePath("anomalies.json", path, sizeof(path));
    
    // Format as JSON object
    char json[256];
    snprintf(json, sizeof(json),
             "  {\"id\":%u,\"type\":\"%s\",\"ssid\":\"%s\",\"severity\":\"%s\",\"occurrences\":%u,\"confidence\":%.2f}",
             static_cast<unsigned>(anomaly.id),
             anomalyTypeName(anomaly.type),
             anomaly.ssid,
             severityName(anomaly.severity),
             static_cast<unsigned>(anomaly.occurrences),
             anomaly.confidence);
    
    return appendJsonObject(path, json);
}

bool SessionManager::logFinding(const Finding& finding) {
    if (!hasSession() || !m_sdAvailable) {
        return false;
    }
    
    char path[80];
    getFilePath("findings.json", path, sizeof(path));
    
    // Format as JSON object
    char json[384];
    snprintf(json, sizeof(json),
             "  {\"id\":%u,\"type\":\"%s\",\"ssid\":\"%s\",\"strength\":\"%s\",\"explanation\":\"%s\"}",
             static_cast<unsigned>(finding.id),
             findingTypeName(finding.type),
             finding.ssid,
             findingStrengthName(finding.strength),
             finding.explanation);
    
    return appendJsonObject(path, json);
}

bool SessionManager::logHypothesis(const Hypothesis& hypothesis) {
    if (!hasSession() || !m_sdAvailable) {
        return false;
    }
    
    char path[80];
    getFilePath("hypotheses.json", path, sizeof(path));
    
    // Format as JSON object
    char json[384];
    snprintf(json, sizeof(json),
             "  {\"id\":%u,\"type\":\"%s\",\"ssid\":\"%s\",\"threat\":\"%s\",\"confidence\":%.2f,\"summary\":\"%s\"}",
             static_cast<unsigned>(hypothesis.id),
             hypothesisTypeName(hypothesis.type),
             hypothesis.ssid,
             threatLevelName(hypothesis.threat),
             hypothesis.confidence,
             hypothesis.summary);
    
    return appendJsonObject(path, json);
}

bool SessionManager::saveBaseline(const Baseline& baseline) {
    if (!hasSession() || !m_sdAvailable) {
        return false;
    }
    
    char path[80];
    getFilePath("baseline.json", path, sizeof(path));
    
#ifndef NATIVE_BUILD

    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[SESSION] Failed to create baseline.json\n");
        return false;
    }

    // The sink is the whole platform-specific half of the write. The document itself is built
    // by portable code, so the checking below can be driven by a host test with a sink that
    // goes short -- which is the only thing that ever drives it, since no gate can make a card
    // refuse a write (ADR-0026 decision 6).
    struct FileSink {
        File& file;
        size_t write(const char* text, size_t length) {
            return file.write(reinterpret_cast<const uint8_t*>(text), length);
        }
    } sink{file};

    const BaselineWriteResult written = writeBaselineDocument(baseline, sink);
    file.close();

    switch (written.status) {
        case BaselineWrite::FormatOverflow:
            Serial.println("[ERROR] baseline entry exceeded its own format ceiling");
            return false;
        case BaselineWrite::ShortWrite:
            Serial.printf("[ERROR] baseline.json for case %s went short at %zu bytes\n",
                          m_currentSession, written.bytes);
            return false;
        case BaselineWrite::Complete:
            break;
    }

    // The card's own figure, not the one we counted. fwrite buffers, and File::close() and the
    // fclose under it both discard their result, so a card that fills while the buffer drains
    // fails after the last write has already returned its full count. Re-opening the file and
    // asking how long it is, is the only check that sees that (ADR-0026 decision 2).
    if (!fileIsExactly(path, written.bytes)) {
        Serial.printf("[ERROR] baseline.json for case %s did not land: %zu bytes written\n",
                      m_currentSession, written.bytes);
        return false;
    }

    Serial.printf("[SESSION] Saved baseline (%zu SSIDs)\n", baseline.count());
    return true;
#else
    // No filesystem on the host: this path is device-verified only (ADR-0003).
    return false;
#endif
}

/**
 * @brief Write @p in into @p out as the body of a JSON string, escaped.
 *
 * An SSID is up to 32 arbitrary octets off the air, not text we chose. Writing it raw let a
 * network named `My"Net` close the field early and have the rest of its name read as JSON
 * structure -- and the same escaping is what keeps a crafted SSID from forging the
 * `"ssid":"` key the parser scans for, because an injected quote always carries its
 * backslash (ADR-0017).
 *
 * @return true if the whole string fitted.
 */
static bool escapeJsonString(const char* in, char* out, size_t outSize) {
    size_t w = 0;
    const auto put = [&](char c) {
        if (w + 1 >= outSize) return false;
        out[w++] = c;
        return true;
    };
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in); *p; ++p) {
        switch (*p) {
            case '"':  if (!put('\\') || !put('"'))  return false; break;
            case '\\': if (!put('\\') || !put('\\')) return false; break;
            case '\n': if (!put('\\') || !put('n'))  return false; break;
            case '\r': if (!put('\\') || !put('r'))  return false; break;
            case '\t': if (!put('\\') || !put('t'))  return false; break;
            default:
                if (*p < 0x20) {
                    // JSON forbids a bare control character in a string. \uXXXX is the only
                    // form that covers the ones without a short escape.
                    static const char kHex[] = "0123456789abcdef";
                    if (!put('\\') || !put('u') || !put('0') || !put('0')) return false;
                    if (!put(kHex[(*p >> 4) & 0xF]) || !put(kHex[*p & 0xF])) return false;
                } else if (!put(static_cast<char>(*p))) {
                    return false;
                }
                break;
        }
    }
    if (w >= outSize) return false;
    out[w] = '\0';
    return true;
}

/**
 * @brief Read the JSON string starting at @p in (just past its opening quote) into @p out.
 *
 * @param end Set to the closing quote.
 * @return Characters written, clamped to @p outSize - 1.
 *
 * Escape-aware, because a `strchr` for the next quote stops at an escaped one and would cut
 * an SSID in half at the character that most needed handling.
 */
static size_t unescapeJsonString(const char* in, char* out, size_t outSize, const char** end) {
    static const auto hexValue = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    size_t w = 0;
    const char* p = in;
    for (; *p != '\0' && *p != '"'; ++p) {
        char decoded = *p;
        if (*p == '\\') {
            ++p;
            if (*p == '\0') break;
            switch (*p) {
                case 'n': decoded = '\n'; break;
                case 'r': decoded = '\r'; break;
                case 't': decoded = '\t'; break;
                case 'b': decoded = '\b'; break;
                case 'f': decoded = '\f'; break;
                case 'u': {
                    int value = 0;
                    int digits = 0;
                    for (; digits < 4 && p[1] != '\0'; ++digits) {
                        const int nibble = hexValue(p[1]);
                        if (nibble < 0) break;
                        value = value * 16 + nibble;
                        ++p;
                    }
                    // Only the escapes this writer emits are decoded: \u00XX for the control
                    // characters. Anything above stays out of a field that holds octets.
                    decoded = (digits == 4 && value <= 0xFF) ? static_cast<char>(value) : '?';
                    break;
                }
                default: decoded = *p; break;  // \" and \\ decode to themselves.
            }
        }
        if (w + 1 < outSize) out[w++] = decoded;
    }
    if (outSize > 0) out[w < outSize ? w : outSize - 1] = '\0';
    *end = p;
    return w;
}

/** True when @p found lies inside the entry ending at @p entryEnd (nullptr = the last). */
static bool withinEntry(const char* found, const char* entryEnd) {
    return found != nullptr && (entryEnd == nullptr || found < entryEnd);
}

// Both readers parse through `long long`, never `long`: `long` is 32 bits on the ESP32 and 64
// on the host, so strtol reads a 64-bit sum whole in `make test` and clamps it on the device --
// a difference no host test can see (ADR-0029).

/**
 * Read an unsigned decimal that follows @p key inside this entry, or @p fallback.
 *
 * A figure wider than 32 bits saturates rather than wraps: 2^32 wrapped is 0, and a zero
 * `rssiCount` or `lastSeenSession` switches off the check the field was persisted for.
 */
static uint32_t readUnsigned(const char* from, const char* entryEnd, const char* key,
                             uint32_t fallback) {
    const char* at = strstr(from, key);
    if (!withinEntry(at, entryEnd)) return fallback;
    const unsigned long long parsed = strtoull(at + strlen(key), nullptr, 10);
    return parsed > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(parsed);
}

/** Read a signed decimal that follows @p key inside this entry, or @p fallback. */
static int64_t readSigned(const char* from, const char* entryEnd, const char* key,
                          int64_t fallback) {
    const char* at = strstr(from, key);
    if (!withinEntry(at, entryEnd)) return fallback;
    return static_cast<int64_t>(strtoll(at + strlen(key), nullptr, 10));
}

/**
 * @brief Length of @p text with any trailing whitespace ignored.
 *
 * Spelled out rather than taken from isspace(), whose argument must be representable as an
 * unsigned char and whose answer depends on the locale -- neither of which is a property this
 * format should inherit.
 */
static size_t lengthWithoutTrailingSpace(const char* text) {
    size_t length = strlen(text);
    while (length > 0) {
        const char c = text[length - 1];
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') break;
        length--;
    }
    return length;
}

/**
 * @brief True when @p text ends with @p ending, trailing whitespace on either side ignored.
 *
 * Both sides are trimmed because the ending's own trailing newline is not what carries the
 * check: the newline *before* the brackets is, and no truncation can produce one (ADR-0026
 * decision 3). Demanding the exact bytes would call a document damaged over a newline
 * something else had eaten.
 */
static bool endsWith(const char* text, const char* ending) {
    const size_t textLength = lengthWithoutTrailingSpace(text);
    const size_t endingLength = lengthWithoutTrailingSpace(ending);
    if (endingLength == 0 || endingLength > textLength) return false;
    return memcmp(text + textLength - endingLength, ending, endingLength) == 0;
}

size_t formatCaseMetadata(const char* name, const CaseStats& stats, char* out, size_t outSize) {
    char escapedName[kEscapedCaseNameBytes + 1];
    if (!escapeJsonString(name, escapedName, sizeof(escapedName))) return 0;

    // %u with an explicit cast rather than %lu: uint32_t is `unsigned long` on the ESP32 and
    // `unsigned int` on the host, so neither bare specifier is right on both (ADR-0006). This
    // call is compiled by both envs now, so -Werror=format is what enforces that rather than
    // the next reader of this file (ADR-0027 decision 2).
    const int written = snprintf(
        out, outSize, kCaseMetadataFormat,
        escapedName,
        static_cast<unsigned>(stats.sessionCount),
        static_cast<unsigned>(stats.totalUptime),
        static_cast<unsigned>(stats.packetsTotal),
        static_cast<unsigned>(stats.beacons),
        static_cast<unsigned>(stats.probes),
        static_cast<unsigned>(stats.deauths),
        static_cast<unsigned>(stats.ssidCount),
        static_cast<unsigned>(stats.bssidCount),
        static_cast<unsigned>(stats.anomalyCount),
        static_cast<unsigned>(stats.findingCount),
        static_cast<unsigned>(stats.hypothesisCount));
    if (written < 0 || static_cast<size_t>(written) >= outSize) return 0;
    return static_cast<size_t>(written);
}

size_t formatBaselineEntry(const SSIDBaseline& entry, char* out, size_t outSize) {
    char ssid[kEscapedSsidBytes + 1];
    if (!escapeJsonString(entry.ssid, ssid, sizeof(ssid))) return 0;

    // %u with an explicit cast rather than %lu: uint32_t is `unsigned long` on the ESP32 and
    // `unsigned int` on the host, so neither bare specifier is right on both (ADR-0006). The
    // sum is 64 bits and goes through PRId64, whose expansion is each platform's own (ADR-0029).
    int written = snprintf(
        out, outSize,
        "  {\"ssid\":\"%s\",\"security\":\"%s\",\"bssidCount\":%u,\"rssiMin\":%d,"
        "\"rssiMax\":%d,\"beacons\":%u,\"rssiSum\":%" PRId64 ",\"rssiCount\":%u,"
        "\"lastSeenSession\":%u,\"channels\":[",
        ssid,
        securityTypeName(entry.security.type),
        static_cast<unsigned>(entry.bssidCount),
        static_cast<int>(entry.rssiMin),
        static_cast<int>(entry.rssiMax),
        static_cast<unsigned>(entry.beaconCount),
        entry.rssiSum,
        static_cast<unsigned>(entry.rssiCount),
        static_cast<unsigned>(entry.lastSeenSession));
    if (written < 0 || static_cast<size_t>(written) >= outSize) return 0;
    size_t w = static_cast<size_t>(written);

    const auto append = [&](const char* format, auto value) {
        const int n = snprintf(out + w, outSize - w, format, value);
        if (n < 0 || static_cast<size_t>(n) >= outSize - w) return false;
        w += static_cast<size_t>(n);
        return true;
    };

    for (uint8_t c = 0; c < entry.channelCount; c++) {
        if (!append(c > 0 ? ",%u" : "%u", static_cast<unsigned>(entry.channels[c]))) return 0;
    }
    if (!append("%s", "],\"bssids\":[")) return 0;

    char macStr[18];
    for (uint8_t b = 0; b < entry.bssidCount; b++) {
        macFormat(entry.knownBSSIDs[b], macStr);
        if (!append(b > 0 ? ",\"%s\"" : "\"%s\"", macStr)) return 0;
    }
    if (!append("%s", "]}")) return 0;
    return w;
}

BaselineReadResult parseBaselineJson(const char* json, Baseline& baseline) {
    BaselineReadResult result;
    if (json == nullptr) {
        baseline.setCurrentSession(0);
        return result;
    }

    // Deliberately not a JSON library: the document is written by formatBaselineEntry() above
    // and an ESP32-S3 with 320KB of DRAM does not spend it on a general parser.
    //
    // The envelope's counter is read before any entry, and set even when absent, so a case
    // whose document predates the envelope resets it rather than inheriting the previously
    // open case's (ADR-0017).
    const char* firstEntry = strstr(json, "\"ssid\":\"");
    baseline.setCurrentSession(readUnsigned(json, firstEntry, kBaselineSessionKey, 0));

    const char* ptr = json;

    while ((ptr = strstr(ptr, "\"ssid\":\"")) != nullptr) {
        ptr += 8;  // past "ssid":"

        // The entry is named from a terminated, unescaped copy, never from the document
        // itself. getOrCreate() used to receive the raw pointer while the length computed
        // beside it went unused, and setSSID() strncpy's to its own field width -- so every
        // restored entry was named `MySSID","security":"WPA2` rather than `MySSID`. The body
        // sat under a platform cutout, so the host test over it asserted the #else line and
        // never saw this (ADR-0016).
        char ssid[DUPIN_SSID_MAX_LEN + 1];
        const char* endQuote = nullptr;
        unescapeJsonString(ptr, ssid, sizeof(ssid), &endQuote);
        if (*endQuote != '"') {
            // The name never closed, so the document stops inside this entry. Reported rather
            // than merely stopped at: the entries already read are good, and the caller has to
            // know the rest is missing (ADR-0026).
            result.status = BaselineDocument::Truncated;
            break;
        }

        // Every field lookup is bounded to this entry, so a missing field cannot take the
        // next entry's value. The boundary is the next entry's key rather than a "},"
        // literal: the writer puts a newline between the brace and the comma, so the literal
        // this once searched for never appeared and no field was ever bounded -- which is why
        // no entry's security was read back at all (ADR-0016).
        const char* entryEnd = strstr(endQuote, "\"ssid\":\"");

        SSIDBaseline* entry = baseline.getOrCreate(ssid);
        if (entry == nullptr) {
            // Baseline full; the rest of the document cannot fit. A fault in this build's
            // ceiling rather than in the card, which is why it is its own outcome (ADR-0026).
            result.status = BaselineDocument::Overflowed;
            break;
        }

        const char* secPtr = strstr(ptr, "\"security\":\"");
        if (withinEntry(secPtr, entryEnd)) {
            secPtr += 12;
            // "Unknown" is matched rather than falling through to Open. While it did, an
            // entry whose security was never determined came back asserting it was an open
            // network -- a downgrade invented by the round trip (ADR-0017).
            if (strncmp(secPtr, "WPA3", 4) == 0) {
                entry->security.type = SecurityType::WPA3;
            } else if (strncmp(secPtr, "WPA2", 4) == 0) {
                entry->security.type = SecurityType::WPA2;
            } else if (strncmp(secPtr, "WPA", 3) == 0) {
                entry->security.type = SecurityType::WPA;
            } else if (strncmp(secPtr, "WEP", 3) == 0) {
                entry->security.type = SecurityType::WEP;
            } else if (strncmp(secPtr, "Open", 4) == 0) {
                entry->security.type = SecurityType::Open;
            } else {
                entry->security.type = SecurityType::Unknown;
            }
        }

        int64_t count = readSigned(ptr, entryEnd, "\"bssidCount\":", entry->bssidCount);
        if (count < 0) count = 0;
        if (count > DUPIN_MAX_BSSIDS_PER_SSID) count = DUPIN_MAX_BSSIDS_PER_SSID;
        entry->bssidCount = static_cast<uint8_t>(count);

        entry->rssiMin = static_cast<int8_t>(readSigned(ptr, entryEnd, "\"rssiMin\":",
                                                        entry->rssiMin));
        entry->rssiMax = static_cast<int8_t>(readSigned(ptr, entryEnd, "\"rssiMax\":",
                                                        entry->rssiMax));
        entry->beaconCount = readUnsigned(ptr, entryEnd, "\"beacons\":", entry->beaconCount);

        // Without these three a restored entry looked right and could not do its job:
        // rssiCount==0 makes isRSSIInRange() answer "no baseline yet" to every sample, and
        // lastSeenSession==0 exempts the entry from stale expiry forever (ADR-0017).
        entry->rssiSum = readSigned(ptr, entryEnd, "\"rssiSum\":", entry->rssiSum);
        entry->rssiCount = readUnsigned(ptr, entryEnd, "\"rssiCount\":", entry->rssiCount);
        entry->lastSeenSession = readUnsigned(ptr, entryEnd, "\"lastSeenSession\":",
                                              entry->lastSeenSession);

        // Channels are what Marple's unexpected-channel check needs: with channelCount at
        // zero it returns before ever comparing one (ADR-0017).
        const char* channelsPtr = strstr(ptr, "\"channels\":[");
        if (withinEntry(channelsPtr, entryEnd)) {
            for (const char* c = channelsPtr + 12; *c != '\0' && *c != ']'; ++c) {
                if (*c < '0' || *c > '9') continue;
                entry->addChannel(static_cast<uint8_t>(strtoul(c, nullptr, 10)));
                while (c[1] >= '0' && c[1] <= '9') ++c;
            }
        }

        const char* bssidsPtr = strstr(ptr, "\"bssids\":[");
        if (withinEntry(bssidsPtr, entryEnd)) {
            bssidsPtr += 10;
            uint8_t bIdx = 0;
            while (*bssidsPtr != '\0' && *bssidsPtr != ']' &&
                   bIdx < DUPIN_MAX_BSSIDS_PER_SSID) {
                if (*bssidsPtr == '"') {
                    bssidsPtr++;
                    // "AA:BB:CC:DD:EE:FF" -- 17 characters, colon-separated octets.
                    if (strlen(bssidsPtr) >= 17 && bssidsPtr[2] == ':') {
                        for (int i = 0; i < 6; i++) {
                            entry->knownBSSIDs[bIdx][i] =
                                static_cast<uint8_t>(strtol(bssidsPtr + i * 3, nullptr, 16));
                        }
                        bIdx++;
                        bssidsPtr += 17;
                    }
                }
                bssidsPtr++;
            }
            if (bIdx > 0) entry->bssidCount = bIdx;
        }

        result.entries++;
        ptr = endQuote + 1;
    }

    // Only a document carrying the envelope can be checked: it is the one this writer made,
    // so it ends the way kBaselineDocumentEnd says it ends. The trailing newline is not
    // required -- what is required is the newline *before* the brackets, which no truncation
    // can produce (ADR-0026 decisions 3 and 4).
    if (result.status == BaselineDocument::Complete && strstr(json, kBaselineSessionKey) &&
        !endsWith(json, kBaselineDocumentEnd)) {
        result.status = BaselineDocument::Truncated;
    }

    return result;
}

size_t SessionManager::loadBaseline(Baseline& baseline) {
    if (!hasSession() || !m_sdAvailable) {
        return 0;
    }

    char path[80];
    getFilePath("baseline.json", path, sizeof(path));

#ifndef NATIVE_BUILD

    if (!SD.exists(path)) {
        Serial.println("[SESSION] No baseline to load");
        return 0;
    }

    File file = SD.open(path, FILE_READ);
    if (!file) {
        Serial.println("[SESSION] Failed to open baseline.json");
        return 0;
    }

    size_t fileSize = file.size();
    if (fileSize == 0) {
        file.close();
        Serial.println("[SESSION] Baseline file is empty");
        return 0;
    }
    if (fileSize > kMaxBaselineFileBytes) {
        // Loud, and distinct from "no baseline". Returning 0 here made an unreadable
        // baseline indistinguishable from a case that never had one, so the operator was
        // told the case was fresh while its baseline sat on the card (ADR-0017).
        file.close();
        Serial.printf("[ERROR] baseline.json is %zu bytes, over the %zu the reader accepts\n",
                      fileSize, kMaxBaselineFileBytes);
        return 0;
    }

    char* buffer = new char[fileSize + 1];
    if (!buffer) {
        file.close();
        return 0;
    }

    file.readBytes(buffer, fileSize);
    buffer[fileSize] = '\0';
    file.close();

    const BaselineReadResult read = parseBaselineJson(buffer, baseline);
    delete[] buffer;

    // [ERROR] rather than a notice for either fault: neither has a retry behind it, which is
    // what separates the two prefixes (ADR-0025 decision 5). The entries that survived are
    // kept and reported -- they are real observations, and what made this a defect was the
    // silence, not the partial data (ADR-0026 decision 8).
    switch (read.status) {
        case BaselineDocument::Complete:
            Serial.printf("[SESSION] Loaded baseline (%zu SSIDs, %zu bytes)\n",
                          read.entries, fileSize);
            break;
        case BaselineDocument::Truncated:
            Serial.printf("[ERROR] baseline.json for case %s stops before its end: "
                          "%zu SSIDs read from %zu bytes\n",
                          m_currentSession, read.entries, fileSize);
            break;
        case BaselineDocument::Overflowed:
            Serial.printf("[ERROR] baseline.json for case %s holds more SSIDs than the %u "
                          "this build keeps: %zu read\n",
                          m_currentSession, static_cast<unsigned>(DUPIN_MAX_SSIDS),
                          read.entries);
            break;
    }
    return read.entries;
#else
    // Only the SD read is platform-specific; parseBaselineJson() above it is portable and
    // host-tested, so what this #else hides is the file read and nothing else (ADR-0016).
    return 0;
#endif
}

} // namespace dupin

