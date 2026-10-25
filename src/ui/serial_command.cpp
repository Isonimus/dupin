/**
 * @file serial_command.cpp
 * @brief Line parser for the serial control channel (ADR-0014).
 */
#include "ui/serial_command.h"

#include <cstring>

namespace dupin {
namespace {

/** @brief Length of @p s ignoring trailing spaces. */
size_t trimmedLen(const char* s) {
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == ' ') --n;
    return n;
}

/** @brief Does @p line equal @p word, ignoring trailing spaces? */
bool matchesWord(const char* line, const char* word) {
    const size_t wordLen = strlen(word);
    return trimmedLen(line) == wordLen && strncmp(line, word, wordLen) == 0;
}

constexpr char kKeyPrefix[] = "key ";

// The printable ASCII range a transcript shows as itself; 0x7f (DEL) is past its end.
constexpr unsigned char kFirstPrintable = 0x20;
constexpr unsigned char kPastLastPrintable = 0x7f;
constexpr char kHexDigits[] = "0123456789abcdef";

} // namespace

Command parseCommand(const char* line) {
    if (line == nullptr) return {};

    while (*line == ' ') ++line;
    if (*line == '\0') return {};

    if (matchesWord(line, "ping")) return {CommandKind::Ping, '\0'};
    if (matchesWord(line, "state")) return {CommandKind::State, '\0'};
    if (matchesWord(line, "probe")) return {CommandKind::Probe, '\0'};

    if (strncmp(line, kKeyPrefix, strlen(kKeyPrefix)) == 0) {
        const char* arg = line + strlen(kKeyPrefix);
        while (*arg == ' ') ++arg;
        // Exactly one character, and nothing but spaces after it. "key" with no argument,
        // or with several, is Unknown rather than a guess: dispatching the first character
        // of "key wm" would press a key the sender did not name.
        if (matchesWord(arg, "enter")) return {CommandKind::Key, kKeyEnter};
        if (matchesWord(arg, "del")) return {CommandKind::Key, kKeyDel};
        if (matchesWord(arg, "tab")) return {CommandKind::Key, kKeyTab};
        if (arg[0] != '\0' && trimmedLen(arg) == 1) {
            return {CommandKind::Key, arg[0]};
        }
        return {CommandKind::Unknown, '\0'};
    }

    return {CommandKind::Unknown, '\0'};
}

bool CommandReader::feed(char c, Command& out) {
    if (c == '\r') return false;

    if (c != '\n') {
        if (m_len < kMaxCommandChars) {
            m_line[m_len++] = c;
        } else {
            // Past the bound the rest of the line is dropped, not stored. The flag survives
            // to the newline so the completed line is reported as refused rather than as
            // whatever the first kMaxCommandChars happened to spell.
            m_overflowed = true;
        }
        return false;
    }

    m_line[m_len] = '\0';
    memcpy(m_completed, m_line, m_len);
    m_completedLen = m_len;
    if (m_overflowed) {
        out = {CommandKind::TooLong, '\0'};
    } else if (strlen(m_line) != m_len) {
        // A NUL would end the line early for the parser, so "key w<NUL>zz" would press a w
        // its sender never finished asking for: the same hazard TooLong refuses.
        out = {CommandKind::Unknown, '\0'};
    } else {
        out = parseCommand(m_line);
    }

    m_len = 0;
    m_overflowed = false;
    m_line[0] = '\0';
    return true;
}

void CommandReader::escapedLine(char (&out)[kEscapedCommandSize]) const {
    size_t written = 0;
    for (size_t i = 0; i < m_completedLen; ++i) {
        const auto byte = static_cast<unsigned char>(m_completed[i]);
        if (byte == '\\') {
            out[written++] = '\\';
            out[written++] = '\\';
        } else if (byte >= kFirstPrintable && byte < kPastLastPrintable) {
            out[written++] = static_cast<char>(byte);
        } else {
            out[written++] = '\\';
            out[written++] = 'x';
            out[written++] = kHexDigits[byte >> 4];
            out[written++] = kHexDigits[byte & 0x0f];
        }
    }
    out[written] = '\0';
}

} // namespace dupin
