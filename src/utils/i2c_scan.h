/**
 * @file i2c_scan.h
 * @brief Format an I2C scan result (a bool-per-address array) as a human line.
 *
 * Extracted from main.cpp's reportClockChip so the formatting loop is host-testable:
 * reportClockChip itself is device-only (it reads M5.In_I2C), but turning the scanned
 * addresses into " 0xNN 0xNN ..." is pure, and the snprintf-offset arithmetic it rests on
 * is exactly what a host test must guard (ADR-0034).
 */
#pragma once

#include <cstddef>

namespace dupin {

// Each answered address prints as " 0xNN": a leading space joins it to the previous one and
// to the "answers at" label, so the whole scan reads as one run. Five visible characters,
// named here so the buffer that must hold the full scan is sized from the format rather than
// from a literal count (invariant 20: a ceiling is derived from the format it bounds).
constexpr size_t kI2cAddressRunChars = sizeof(" 0x00") - 1;

/**
 * @brief Append " 0xNN" for every set entry of @p answered in [first, end) into @p out.
 *
 * Pure: no device dependency, so `pio test -e native` reaches it. Returns the number of
 * characters written, excluding the terminator. Always NUL-terminates when @p outSize > 0.
 *
 * Each snprintf return is checked against the space left before it is added to the running
 * offset: a write that would truncate stops the loop rather than advancing the offset past
 * the buffer. An unchecked snprintf return feeds its own would-have-written length back into
 * `outSize - written`, which underflows size_t and hands the next call a buffer it treats as
 * enormous -- the CWE-190 overflow CodeQL flagged (ADR-0034). The caller sizes @p out from
 * kI2cAddressRunChars so it cannot truncate in practice; the guard is what makes that a fact
 * rather than an assumption a later, wider format change would silently break.
 */
size_t formatI2cScanAddresses(char* out, size_t outSize, const bool* answered,
                              size_t first, size_t end);

} // namespace dupin
