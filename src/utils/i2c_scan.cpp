/**
 * @file i2c_scan.cpp
 * @brief Implementation of the I2C scan line formatter (ADR-0034).
 */
#include "utils/i2c_scan.h"

#include <cstdio>

namespace dupin {

size_t formatI2cScanAddresses(char* out, size_t outSize, const bool* answered,
                              size_t first, size_t end) {
    if (out == nullptr || outSize == 0) return 0;
    out[0] = '\0';
    if (answered == nullptr) return 0;

    size_t written = 0;
    for (size_t address = first; address < end; ++address) {
        if (!answered[address]) continue;

        const size_t remaining = outSize - written;
        const int printed = snprintf(out + written, remaining, " 0x%02X",
                                     static_cast<unsigned>(address));
        // snprintf returns what it WOULD have written: a negative result is an encoding
        // error, and a result >= remaining means it truncated. Either way, stop before
        // folding that length into `written`, which would drive the next `remaining` below
        // zero and wrap size_t, handing snprintf a buffer it treats as enormous (CWE-190).
        if (printed < 0 || static_cast<size_t>(printed) >= remaining) {
            out[written] = '\0';  // drop the partial the truncating call left behind
            break;
        }
        written += static_cast<size_t>(printed);
    }
    return written;
}

} // namespace dupin
