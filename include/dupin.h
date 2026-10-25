/**
 * @file dupin.h
 * @brief Main header for Dupin firmware
 * 
 * Include this header to access all Dupin core functionality.
 */

#pragma once

// Version information.
//
// This literal is the only version in the tree, and `make check-version` fails the build if
// a second one appears in src/ (ADR-0012). It used to be one of two: main.cpp hardcoded
// v0.2.1 at three sites while this said 0.2.0, and nothing read this, so the number nobody
// saw was the number nobody corrected.
//
// The separate MAJOR/MINOR/PATCH macros are gone with it. They were a second representation
// of the same fact, also unread, and keeping them means keeping them in step by hand --
// which is the defect, not a mitigation. Unlike the DUPIN_HAS_* guards ADR-0011 kept unread
// on purpose, their absence cannot silently disable anything: a `#if DUPIN_VERSION_MAJOR`
// that needs them is a compile error, not a feature that quietly vanishes.
//
// Corrected from 0.2.0 down to 0.1.0 for the first public release: no build carrying 0.2.0 was
// ever released, and a public v0.1.0 tag above an internal 0.2.0 literal is the silent
// disagreement ADR-0012 exists to prevent (ADR-0033).
#define DUPIN_VERSION_STRING "0.1.0"

// ============================================
// Board Detection
// ============================================
#if defined(DUPIN_BOARD_CARDPUTER)
    #define DUPIN_BOARD_NAME "M5Stack Cardputer"
#elif defined(DUPIN_BOARD_CARDPUTER_ADV)
    #define DUPIN_BOARD_NAME "M5Stack Cardputer ADV"
#elif defined(DUPIN_BOARD_GENERIC_S3)
    #define DUPIN_BOARD_NAME "Generic ESP32-S3"
#elif defined(NATIVE_BUILD)
    #define DUPIN_BOARD_NAME "Native (Test)"
#else
    #define DUPIN_BOARD_NAME "Unknown"
#endif

// ============================================
// Memory Configuration
// ============================================

// Observation ring buffer size
#ifndef DUPIN_MAX_OBSERVATIONS
    #define DUPIN_MAX_OBSERVATIONS 256
#endif

// Maximum tracked SSIDs in baseline
#ifndef DUPIN_MAX_SSIDS
    #define DUPIN_MAX_SSIDS 64
#endif

// Maximum BSSIDs per SSID
#ifndef DUPIN_MAX_BSSIDS_PER_SSID
    #define DUPIN_MAX_BSSIDS_PER_SSID 8
#endif

// Maximum channels to track
#ifndef DUPIN_MAX_CHANNELS
    #define DUPIN_MAX_CHANNELS 14
#endif

// Maximum active anomalies
#ifndef DUPIN_MAX_ANOMALIES
    #define DUPIN_MAX_ANOMALIES 32
#endif

// ============================================
// Timing Configuration
// ============================================

// Channel hop interval (ms)
#ifndef DUPIN_CHANNEL_HOP_INTERVAL_MS
    #define DUPIN_CHANNEL_HOP_INTERVAL_MS 100
#endif

// Baseline auto-save interval (ms)
#ifndef DUPIN_BASELINE_SAVE_INTERVAL_MS
    #define DUPIN_BASELINE_SAVE_INTERVAL_MS 60000
#endif

// Anomaly confidence decay interval (ms)
#ifndef DUPIN_CONFIDENCE_DECAY_INTERVAL_MS
    #define DUPIN_CONFIDENCE_DECAY_INTERVAL_MS 30000
#endif

// ============================================
// 802.11 Frame Constants
// ============================================

#define DUPIN_SSID_MAX_LEN 32
#define DUPIN_MAC_LEN 6

// ============================================
// Namespace for all Dupin code
// ============================================
namespace dupin {

// Forward declarations - will be populated as we add components

} // namespace dupin
