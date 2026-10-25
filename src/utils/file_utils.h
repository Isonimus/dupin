/**
 * @file file_utils.h
 * @brief File I/O utilities for SD card operations
 * 
 * Provides reusable functions for JSON file writing,
 * appending to files, and reading structured data.
 */

#pragma once

#ifndef NATIVE_BUILD
#include <FS.h>
#include <SD.h>
#endif

#include <cstdint>
#include <cstring>

namespace dupin {

/**
 * @brief True when the file at @p path is exactly @p expectedBytes long.
 *
 * The check a write cannot do for itself. `fwrite`, which is where a `File` write ends up,
 * buffers; `File::flush()` and `File::close()` both return void and the `fflush` and `fclose`
 * under them have their results discarded. So a card that fills while the buffer drains fails
 * *after* the last write has already returned its full count, and asking the card how long the
 * file is once it is closed is the only thing that sees it (ADR-0026 decision 2).
 *
 * `getWriteError()` is not the check it looks like: it reports a flag only
 * `Print::setWriteError` raises, and nothing in the FS or SD path calls it.
 *
 * What it cannot see is a card that reports a length it did not physically store: the size
 * comes back through the same driver that just wrote the file. Catching that needs the data
 * read back and compared, which is a different check with a different cost, and is not what
 * this one claims.
 */
inline bool fileIsExactly(const char* path, size_t expectedBytes) {
#ifndef NATIVE_BUILD
    File file = SD.open(path, FILE_READ);
    if (!file) return false;
    const size_t actual = file.size();
    file.close();
    return actual == expectedBytes;
#else
    (void)path;
    (void)expectedBytes;
    return false;
#endif
}

/**
 * @brief Append a line to a file (creates if doesn't exist)
 * @param path Full path to file
 * @param line Line to append (without newline)
 * @return true if successful
 */
inline bool appendLine(const char* path, const char* line) {
#ifndef NATIVE_BUILD
    File file = SD.open(path, FILE_APPEND);
    if (!file) {
        // Try creating new file
        file = SD.open(path, FILE_WRITE);
        if (!file) {
            Serial.printf("[FILE] Failed to open %s\n", path);
            return false;
        }
    }
    
    const size_t before = file.size();
    // println() puts a CR and an LF after the line, so the card has to take two bytes more
    // than the line itself. Checked rather than discarded: print() reaches fwrite, which
    // returns fewer bytes than asked when the card is full or gone (ADR-0026 decision 1).
    const size_t expected = strlen(line) + 2;
    const bool whole = file.println(line) == expected;
    file.close();

    if (!whole) {
        Serial.printf("[ERROR] %s took fewer than the %zu bytes appended to it\n",
                      path, expected);
        return false;
    }
    if (!fileIsExactly(path, before + expected)) {
        Serial.printf("[ERROR] %s did not land on the card\n", path);
        return false;
    }
    return true;
#else
    return false;
#endif
}

/**
 * @brief Write entire content to file (overwrites)
 * @param path Full path to file
 * @param content Content to write
 * @return true if successful
 */
inline bool writeFile(const char* path, const char* content) {
#ifndef NATIVE_BUILD
    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[FILE] Failed to create %s\n", path);
        return false;
    }
    
    const size_t expected = strlen(content);
    const bool whole = file.print(content) == expected;
    file.close();

    if (!whole) {
        Serial.printf("[ERROR] %s took fewer than the %zu bytes written to it\n",
                      path, expected);
        return false;
    }
    if (!fileIsExactly(path, expected)) {
        Serial.printf("[ERROR] %s did not land on the card\n", path);
        return false;
    }
    return true;
#else
    return false;
#endif
}

/**
 * @brief Append JSON object to array file
 * 
 * Handles the complexity of appending to a JSON array:
 * - Creates "[" + object + "]" if file doesn't exist
 * - Appends "," + object before final "]" if file exists
 * 
 * @param path Full path to JSON array file
 * @param jsonObject JSON object string (without surrounding array brackets)
 * @return true if successful
 */
inline bool appendJsonObject(const char* path, const char* jsonObject) {
#ifndef NATIVE_BUILD
    // Every fragment is checked against its own length, and the total is checked against the
    // card afterwards. These five prints all used to return into nothing, so a log that filled
    // the card kept reporting that it had been written (ADR-0026 decision 1).
    size_t expected = 0;
    bool whole = true;
    const auto put = [&](File& file, const char* text) {
        const size_t length = strlen(text);
        expected += length;
        if (file.print(text) != length) whole = false;
        return whole;
    };

    if (!SD.exists(path)) {
        // Create new array with first object
        File file = SD.open(path, FILE_WRITE);
        if (!file) return false;

        put(file, "[\n") && put(file, jsonObject) && put(file, "\n]");
        file.close();
        if (!whole) {
            Serial.printf("[ERROR] %s took fewer than the %zu bytes written to it\n",
                          path, expected);
            return false;
        }
        if (!fileIsExactly(path, expected)) {
            Serial.printf("[ERROR] %s did not land on the card\n", path);
            return false;
        }
        return true;
    }
    
    // File exists - read it, remove trailing "]", append new object
    File file = SD.open(path, FILE_READ);
    if (!file) return false;
    
    size_t fileSize = file.size();
    if (fileSize < 3) {
        file.close();
        // Corrupted, recreate
        return appendJsonObject(path, jsonObject);
    }
    
    // Read all content
    char* buffer = new char[fileSize + 1];
    if (!buffer) {
        file.close();
        return false;
    }
    
    file.readBytes(buffer, fileSize);
    buffer[fileSize] = '\0';
    file.close();
    
    // Find last ']' and replace with comma + new object
    char* lastBracket = strrchr(buffer, ']');
    if (!lastBracket) {
        delete[] buffer;
        return false;
    }
    
    *lastBracket = '\0';  // Truncate at ']'
    
    // Write back
    file = SD.open(path, FILE_WRITE);
    if (!file) {
        delete[] buffer;
        return false;
    }
    
    put(file, buffer) && put(file, ",\n") && put(file, jsonObject) && put(file, "\n]");
    file.close();

    delete[] buffer;
    if (!whole) {
        Serial.printf("[ERROR] %s took fewer than the %zu bytes written to it\n",
                      path, expected);
        return false;
    }
    if (!fileIsExactly(path, expected)) {
        Serial.printf("[ERROR] %s did not land on the card\n", path);
        return false;
    }
    return true;
#else
    return false;
#endif
}

/**
 * @brief Read entire file content
 * @param path Full path to file
 * @param buffer Output buffer (caller manages memory)
 * @param bufferSize Size of output buffer
 * @return Number of bytes read, 0 on failure
 */
inline size_t readFile(const char* path, char* buffer, size_t bufferSize) {
#ifndef NATIVE_BUILD
    File file = SD.open(path, FILE_READ);
    if (!file) {
        return 0;
    }
    
    size_t bytesRead = file.readBytes(buffer, bufferSize - 1);
    buffer[bytesRead] = '\0';
    file.close();
    return bytesRead;
#else
    return 0;
#endif
}

/**
 * @brief Check if file exists
 */
inline bool fileExists(const char* path) {
#ifndef NATIVE_BUILD
    return SD.exists(path);
#else
    return false;
#endif
}

/**
 * @brief Ensure directory exists
 */
inline bool ensureDir(const char* path) {
#ifndef NATIVE_BUILD
    if (SD.exists(path)) return true;
    return SD.mkdir(path);
#else
    return false;
#endif
}

} // namespace dupin
