/**
 * @file time_utils.cpp
 * @brief Time utilities implementation (mock millis for native)
 */

#include "utils/time_utils.h"

#ifdef NATIVE_BUILD
// Mock millis value for testing
uint32_t g_mockMillis = 0;
#endif
