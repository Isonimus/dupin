/**
 * @file serial_logger.cpp
 * @brief Serial logger implementation
 */

#include "ui/serial_logger.h"

namespace dupin {

// Global logger instance
SerialLogger logger;

} // namespace dupin

#ifdef NATIVE_BUILD
// Mock Serial for native
MockSerialClass Serial;
#endif
