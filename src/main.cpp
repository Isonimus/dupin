/**
 * @file main.cpp
 * @brief Dupin - Defensive Wi-Fi Intelligence Toolkit
 * 
 * Incremental build - adding back ModeManager and modes
 */

#include <Arduino.h>

#ifndef NATIVE_BUILD
#include <M5Cardputer.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <type_traits>
#endif

// Core
#include "core/mode_manager.h"
#include "core/dupin_core.h"
#include "capture/wifi_capture.h"
#include "capture/frame_parser.h"

// Modes
#include "modes/watson/watson.h"
#include "modes/marple/marple.h"
#include "modes/poirot/poirot.h"
#include "modes/holmes/holmes.h"
#include "modes/baskerville/baskerville.h"

// UI
#include "ui/serial_logger.h"
#include "ui/text_wrap.h"
#include "ui/serial_command.h"
#include "hal/display/canvas_types.h"
#include "ui/display.h"
#include "ui/case_list.h"

// Storage
#include "storage/session_manager.h"

// Configuration
#include "config/dupin_config.h"

// DUPIN_BOARD_NAME and DUPIN_VERSION_STRING, for the boot banner below.
#include "dupin.h"

using namespace dupin;

// Serial control-channel bytes drained per loop pass. Bounded so a flood -- or a key held
// down in somebody's terminal -- cannot starve the capture path; whatever is left waits one
// loop, which is about 10 ms (ADR-0014).
constexpr size_t kMaxCommandBytesPerPoll = 64;

// Widest decimal rendering of a uint32_t (4294967295), excluding the terminator.
// Display buffers for %u of a uint32_t are sized from this rather than estimated.
constexpr size_t kUint32MaxDigits = 10;

// Global config (loaded from SD card)
static DupinConfig g_config;

// Global instances
static ModeManager g_modeManager;
static WiFiCapture g_wifiCapture;

// Detective modes
static WatsonMode g_watson;
static MarpleMode g_marple;
static PoirotMode g_poirot;
static HolmesMode g_holmes;
static BaskervilleMode g_baskerville;
static DupinCore g_dupin;

// State
static bool g_running = false;
static IMode* g_activeMode = nullptr;
static int g_scrollPos = 0;          // For list scrolling
static uint32_t g_startTime = 0;     // For uptime
static uint32_t g_toastEndTime = 0;  // Toast disappears after this
// Sized to what the toast box can actually show, not to a round number. showToast()
// fits incoming text to this, so the buffer and the panel agree by construction.
static char g_toastText[kToastMaxChars + 1] = {0};  // Toast message
static uint16_t g_toastColor = 0;    // Toast color

// Popup and selection state
static bool g_popupActive = false;       // Detail popup is showing
static int g_selectedIndex = 0;          // Currently highlighted item (0-5 visible range)
static size_t g_currentItemCount = 0;    // Total items in current view

// Alert badges
static bool g_hasUnseenAnomaly = false;   // Show "M" badge until Marple viewed
static bool g_hasUnseenFinding = false;   // Show "P" badge until Poirot viewed
static bool g_hasUnseenHypothesis = false; // Show "H" badge until Holmes viewed

// Activity tracking
static uint32_t g_frameCount = 0;         // Total frames for rate calc
static uint32_t g_lastRateCalcTime = 0;   // Last time we calculated rate
static uint32_t g_framesPerSec = 0;       // Packets per second
static uint32_t g_lastFrameTime = 0;      // For activity dot

// Status ticker (shows mode activity in footer)
static char g_statusMessage[48] = {0};    // Current status message
static uint32_t g_statusTime = 0;         // When status was set

// Baseline autosave
static uint32_t g_lastBaselineSave = 0;   // Last autosave time
static size_t g_lastBaselineCount = 0;    // SSID count at last save (for change detection)

// The one crossing between the Wi-Fi task and the loop task, and the reason there is no
// lock anywhere in src/ (ADR-0024). onFrame parses a frame and posts it here; loop() takes
// it out and hands it to the modes, so every mode table, every callback those modes fire,
// the toast and tone they raise, the renderer they reach through showToast() and the SD
// writer are all driven from one task. Single-writer becomes a property of the structure
// rather than a rule every future callback has to remember.
//
// Guarded because the types are FreeRTOS's, not because the guard selects a behaviour:
// platformio.ini excludes main.cpp from the native env, so there is no second path here.
#ifndef NATIVE_BUILD
// The depth is a choice and is recorded as one. scripts/race-verify.mjs measured 23 frames
// arriving between two loop() passes in one run and 5 in the next -- same firmware, same
// room, same duration -- so no depth can be derived from that peak; this is headroom over
// the worse of the two. Raising it costs one Observation of heap per slot, which `probe`
// reports as slotBytes= rather than restating here, because the struct is free to grow
// without this comment noticing.
constexpr UBaseType_t kObservationQueueDepth = 64;

// A FreeRTOS queue copies by memcpy, so an Observation that grew a field owning memory would
// be duplicated as a bare pointer and freed twice -- a corruption whose first symptom is
// somewhere else entirely. The struct is fixed-size by design (core/observation.h); this is
// what makes that a compile error instead of a comment.
static_assert(std::is_trivially_copyable<Observation>::value,
              "Observation crosses tasks by memcpy and must stay trivially copyable");

// How often a queue that is dropping may say so on the wire. Unbounded, the report becomes
// the flood it is describing.
constexpr uint32_t kDropReportIntervalMs = 1000;

static QueueHandle_t g_observationQueue = nullptr;

// Frames are conserved: accepted == drained + dropped + still queued, and with the producer
// stopped the last term is 0 and the identity is exact. That is what make verify-race
// asserts, rather than asserting a drop count of 0 -- which passes just as well on firmware
// that captured nothing (invariant 17). Accepted and dropped have one writer, the Wi-Fi
// task; drained has one writer, the loop task. A 32-bit aligned load on this target is not
// torn, so a reader of the other task's counter sees a whole figure, if sometimes a stale
// one, and staleness is why the gate reads them only after the producer has stopped.
static volatile uint32_t g_framesAccepted = 0;
static volatile uint32_t g_framesDropped = 0;
static uint32_t g_framesDrained = 0;
#endif

#ifndef NATIVE_BUILD
// Concurrency probe, read out by the `probe` command and driven by scripts/race-verify.mjs.
//
// Written to answer a design question -- how many frames arrive between two loop() passes,
// how long the renderer and an SD save hold the CPU, and whether two tasks are ever inside
// the renderer at once -- and kept after ADR-0024 answered it, because the same figures are
// what catch the defect coming back. rendersOffLoopTask and renderCollisions must now read 0
// forever: a future callback that reaches the renderer from another task is caught by the
// counter that found the first one. They are deliberately unsynchronised --
// a lock taken to measure a missing lock would change the thing being measured. Each is
// written by one task, except the render pair, whose whole purpose is to catch two.
// The task onFrame runs on is sampled rather than held: stopping capture deletes the Wi-Fi
// task, and a handle kept across that is a pointer into a freed TCB -- the first run of
// scripts/race-verify.mjs read one back as a different task's name, which is the benign face
// of a use-after-free. Only the name and the stack figure survive the sample, so nothing
// here can outlive the task it describes.
constexpr uint32_t kProbeSampleEveryFrames = 64;
constexpr size_t kTaskNameChars = 16;  ///< configMAX_TASK_NAME_LEN in ESP-IDF.
constexpr uint32_t kStackUnsampled = UINT32_MAX;
static char g_frameTaskName[kTaskNameChars] = {0};
static char g_loopTaskName[kTaskNameChars] = {0};
// Lowest free stack the Wi-Fi task has reported. A minimum across samples rather than a last
// reading, because the task is destroyed and recreated between captures and each incarnation
// starts its own high-water mark afresh.
static uint32_t g_frameStackFreeBytes = kStackUnsampled;
static TaskHandle_t g_loopTask = nullptr;    ///< The task loop() runs on.
// The task currently inside updateDisplay(), or nullptr. A collision is counted only when
// the task already drawing is a *different* one, so the figure misses overlaps rather than
// inventing them -- evidence that under-reports is worth more here than evidence that
// over-reports.
//
// That holds while rendersOffLoopTask is 0, and the precondition is worth stating because
// the two figures are read together. The only handle stored here other than the loop task's
// is the Wi-Fi task's, and the loop task is never deleted; so if the Wi-Fi task were ever
// destroyed mid-render, the handle left behind would be dead and every later render would
// count a collision against it. Reaching that state requires the Wi-Fi task to have entered
// the renderer at all, which is exactly what rendersOffLoopTask counts -- so a report with
// rendersOffLoopTask=0 cannot be carrying an inflated collision count, and one above 0 has
// already said the thing the collision count was there to find.
static volatile TaskHandle_t g_renderingTask = nullptr;
static volatile uint32_t g_renderCollisions = 0;
static volatile uint32_t g_rendersOffLoopTask = 0;
static volatile uint32_t g_renderMaxMicros = 0;
static uint32_t g_framesAtLastPass = 0;
static uint32_t g_maxFramesPerPass = 0;  ///< Peak frames arriving between two loop() passes.
static uint32_t g_maxPassMicros = 0;     ///< Longest gap between two loop() passes.
static uint32_t g_saveMaxMicros = 0;     ///< Longest saveBaseline() observed.
#endif

// Double-buffered canvas (sprite)
#ifndef NATIVE_BUILD
// The parent is taken from M5's own Display member, not M5Cardputer.Display. The latter is
// a reference member (`M5GFX &Display = M5.Display`) that is only bound when M5Cardputer's
// constructor runs, and nothing orders that before this static initialiser — so reading it
// here captured a null parent, and the first pushSprite() dereferenced it. M5.Display is a
// real member object, so its address is a link-time constant and safe to take this early
// (ADR-0010). Off-screen buffer per ADR-0005.
static dupin::Canvas g_canvas(&M5.Display);

// 16-bit colour: what the Cardputer panel renders and what the UI code assumes.
static constexpr uint8_t CANVAS_COLOR_DEPTH_BITS = 16;
#endif

// Case menu state
// The notice is the case menu's own surface for saying why something was refused. It is
// not a toast and not the status ticker: showToast() and setStatus() both render through
// updateDisplay(), which draws the main screen, and the menu's modal loop repaints
// renderCaseMenu() every 50 ms -- so either one raised from in here is erased within a
// frame. Sized from the panel geometry so the buffer and the renderer agree (ADR-0019).
static char g_caseNotice[kCaseNoticeMaxChars + 1] = {0};
static bool g_inCaseMenu = false;       // Currently showing case menu
static bool g_inTextInput = false;         // Currently in text input mode
static int g_caseMenuSelection = 0;     // Selected item in case menu
// `x` asked to delete the selected case and the next key decides. Anything but Enter withdraws
// it, so a navigation key cannot re-aim a pending delete at another case (ADR-0020).
static bool g_deleteArmed = false;
static SessionMeta g_cases[kMaxListedCases];  // Cached case list (ADR-0023)
static size_t g_caseCount = 0;          // Number of cases cached
static bool g_caseListTruncated = false;  // The card holds more than the cache took
// Sized from the one session-name bound rather than to 32: what is typed here becomes a
// case name, so the buffer, the validator and the storage fields are the same fact
// (ADR-0013 invariant 13).
static char g_inputBuffer[kMaxSessionNameChars + 1] = {0};  // Text input buffer
static size_t g_inputPos = 0;              // Cursor position in input
static bool g_renameMode = false;          // Rename vs new case


void printBanner();
void initializeModes();
void handleKeyboard();
void updateDisplay();
#ifndef NATIVE_BUILD
// Declared here rather than at its definition: loop() pumps it, and so does the modal
// case-menu loop inside dispatchKey(), and both sit above it in this file (ADR-0014).
static void pollSerialCommands();

/**
 * @brief One keypress, from the physical keyboard or injected over the control channel.
 *
 * A flattened view of what the case-menu handler actually reads out of
 * Keyboard_Class::KeysState -- the four modifier-ish keys it tests, plus the characters the
 * press produced. Deliberately physical rather than logical: this is not the InputAction
 * enum ADR-0005 defers, which maps keys and GPIO buttons onto navigation intents for a
 * second board. This shim only lets a key arrive from somewhere other than the keyboard
 * (ADR-0014).
 */
struct KeyEvent {
    bool enter = false;
    bool del = false;   ///< The Cardputer's del key: ESC in the menu, backspace while typing.
    bool fn = false;
    bool tab = false;
    const char* chars = "";  ///< NUL-terminated characters this press produced.
};

/** @brief Longest character run carried by one KeyEvent. */
constexpr size_t kMaxKeyEventChars = 8;

static bool keyEventHasChar(const KeyEvent& ev, char c) {
    for (const char* p = ev.chars; *p != '\0'; ++p) {
        if (*p == c) return true;
    }
    return false;
}

static void dispatchCaseMenuKey(const KeyEvent& ev);
#endif
void showToast(const char* text, uint16_t color, uint16_t durationMs);
void onFrame(const uint8_t* data, size_t len, int8_t rssi, uint8_t channel);
void onAnomalyDetected(const Anomaly& anomaly);
void onFindingDetected(const Finding& finding);
void onHypothesisDetected(const Hypothesis& hypothesis);
void onSSIDDiscovered(const char* ssid, const uint8_t* bssid);

// Case menu functions
void refreshCaseList();
void renderCaseMenu();
void handleCaseMenuInput();
void showTextInput(const char* title, bool isRename);


// Status ticker helper
void setStatus(const char* msg);

// Word wrap for popup text now lives in ui/text_wrap.{h,cpp} so its logic is
// host-testable (ADR-0005): dupin::printWrapped, reached via `using namespace dupin`.

#ifndef NATIVE_BUILD
/**
 * @brief Stop the boot with a visible reason instead of continuing into a crash.
 *
 * A device whose canvas did not come up cannot render anything, and carrying on only moves
 * the failure somewhere less obvious — which is exactly what happened when the canvas was
 * built with a null parent: the panic surfaced inside LovyanGFX, four frames from the
 * mistake. The `[ERROR]` prefix is this codebase's log convention and is what the device
 * verify harness watches for (ADR-0009).
 */
static void haltBootFailure(const char* reason) {
    Serial.printf("[ERROR] boot aborted: %s\n", reason);
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_RED);
    M5Cardputer.Display.setCursor(10, 55);
    M5Cardputer.Display.print("BOOT FAILED");
    M5Cardputer.Display.setTextColor(TFT_WHITE);
    M5Cardputer.Display.setCursor(10, 75);
    M5Cardputer.Display.print(reason);
    for (;;) {
        delay(1000);
    }
}

// The addresses I2C_Class::scanID probes, [first, end): the reserved ones below 0x08 hang the
// S3 when probed, and the scan leaves them unwritten rather than false.
constexpr size_t kI2cFirstScannedAddress = 0x08;
constexpr size_t kI2cScannedAddressEnd = 0x78;

/**
 * @brief Say whether M5Unified found a clock chip, which board it detected, and every address
 * that answers on the internal I2C bus.
 *
 * Neither board has a clock chip, which is why a case has no creation time (ADR-0030). The line
 * stays so a later board revision is answered by rerunning scripts/rtc-probe.mjs, which parses
 * it. The library tries one part, a PCF8563/BM8563 at 0x51, so its "none" rules out that part
 * rather than a clock: the scan is what tells a board with no clock chip from one with a
 * different one. The board is the one detected at runtime, because DUPIN_BOARD_NAME names the
 * env that built the image, not the hardware it runs on.
 */
static void reportClockChip() {
    const char* const clock = M5.Rtc.isEnabled() ? "found" : "none";
    const m5::board_t board = M5.getBoard();
    const char* const detected = board == m5::board_t::board_M5CardputerADV ? "CardputerADV"
                               : board == m5::board_t::board_M5Cardputer    ? "Cardputer"
                                                                            : "other";
    if (!M5.In_I2C.isEnabled()) {
        Serial.printf("[BOOT] RTC: %s; board=%s; no internal I2C bus\n", clock, detected);
        return;
    }
    bool answered[kI2cScannedAddressEnd] = {};
    M5.In_I2C.scanID(answered);
    char addresses[(kI2cScannedAddressEnd - kI2cFirstScannedAddress) * (sizeof(" 0x00") - 1) + 1] = "";
    size_t written = 0;
    for (size_t address = kI2cFirstScannedAddress; address < kI2cScannedAddressEnd; ++address) {
        if (!answered[address]) continue;
        written += static_cast<size_t>(snprintf(addresses + written, sizeof(addresses) - written,
                                                " 0x%02X", static_cast<unsigned>(address)));
    }
    Serial.printf("[BOOT] RTC: %s; board=%s; internal I2C answers at%s\n", clock, detected,
                  written == 0 ? " nothing" : addresses);
}
#endif

void setup() {
    // Initialize serial FIRST
    Serial.begin(115200);
    delay(1000);
    
    Serial.println();
    Serial.println("=== DUPIN v" DUPIN_VERSION_STRING " ===");
    // Which env built this image. The board envs compile to byte-identical .text, so with
    // the board unnamed a boot transcript -- the artifact scripts/boot-verify.mjs keeps --
    // could not be told apart from the other env's (ADR-0011). Literal concatenation, not
    // a format argument, so there is nothing here for invariant 6 to get wrong.
    Serial.println("[BOOT] Board: " DUPIN_BOARD_NAME);
    Serial.println("[BOOT] Serial OK");
    
#ifndef NATIVE_BUILD
    // Initialize M5Cardputer
    Serial.println("[BOOT] Initializing M5Cardputer...");
    M5Cardputer.begin();
    Serial.println("[BOOT] M5Cardputer OK");
    reportClockChip();
    
    // === SPLASH SCREEN WITH INIT PROGRESS ===
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    
    // Title - big and centered
    M5Cardputer.Display.setTextSize(3);
    M5Cardputer.Display.setTextColor(TFT_CYAN);
    M5Cardputer.Display.setCursor(70, 25);
    M5Cardputer.Display.print("DUPIN");
    
    // Subtitle - smaller, centered. X is derived from the string width (6 px per glyph at
    // text size 1, as the progress-status centering below does) rather than a hand-placed
    // cursor, so the line is truly centered instead of guessed.
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(TFT_WHITE);
    const char* kSplashSubtitle = "Defensive Wi-Fi Intelligence";
    int subtitleX = (dupin::SCREEN_WIDTH - static_cast<int>(strlen(kSplashSubtitle)) * 6) / 2;
    M5Cardputer.Display.setCursor(subtitleX, 60);
    M5Cardputer.Display.print(kSplashSubtitle);
    
    // Version - at bottom
    M5Cardputer.Display.setTextColor(TFT_DARKGREY);
    M5Cardputer.Display.setCursor(95, 115);
    M5Cardputer.Display.print("v" DUPIN_VERSION_STRING);
    
    // Progress bar background
    const int barX = 30, barY = 85, barW = 180, barH = 8;
    M5Cardputer.Display.drawRect(barX, barY, barW, barH, TFT_DARKGREY);
    
    // Helper lambda to update progress
    auto updateProgress = [&](int percent, const char* status) {
        // Fill progress bar
        int fillW = (barW - 2) * percent / 100;
        M5Cardputer.Display.fillRect(barX + 1, barY + 1, fillW, barH - 2, TFT_CYAN);
        
        // Show status text
        M5Cardputer.Display.fillRect(0, 100, dupin::SCREEN_WIDTH, 10, TFT_BLACK);  // Clear status line
        M5Cardputer.Display.setTextColor(TFT_DARKGREY);
        int textX = (dupin::SCREEN_WIDTH - strlen(status) * 6) / 2;  // Center text
        M5Cardputer.Display.setCursor(textX, 100);
        M5Cardputer.Display.print(status);
        
        Serial.printf("[BOOT] %d%% - %s\n", percent, status);
        delay(200);  // Brief pause for visual feedback
    };
    
    // === INITIALIZATION STEPS ===
    updateProgress(10, "Initializing display...");
    
    // Initialize double-buffer canvas. Colour depth is set first on purpose: setColorDepth()
    // on a sprite that already holds a buffer deletes it and allocates again, so the old
    // order paid for this ~63 KB allocation twice and discarded the second result.
    g_canvas.setColorDepth(CANVAS_COLOR_DEPTH_BITS);
    if (g_canvas.createSprite(dupin::SCREEN_WIDTH, dupin::SCREEN_HEIGHT) == nullptr) {
        haltBootFailure("canvas allocation failed");
    }
    // Cheap, and it is the check whose absence turned a null parent into a panic four call
    // frames away instead of an error at the point the canvas was built.
    if (g_canvas.getParent() == nullptr) {
        haltBootFailure("canvas has no parent display");
    }
    updateProgress(30, "Canvas ready");

    // Built here rather than when capture starts, because onFrame posts into it and the
    // callback is registered long before: a queue created late is a window in which frames
    // are dropped for no reason anyone would ever look for. Halting on failure rather than
    // capturing without it -- a firmware that ingests into nothing looks exactly like a
    // quiet room, which is the failure ADR-0024 exists to remove.
    g_observationQueue = xQueueCreate(kObservationQueueDepth, sizeof(Observation));
    if (g_observationQueue == nullptr) {
        haltBootFailure("frame queue allocation failed");
    }

    // The Wi-Fi driver posts every event it raises -- a start, a stop, each channel hop -- into
    // the default event loop, and WiFiCapture calls esp_wifi_init directly, bypassing the
    // Arduino Network library that would otherwise create it. With no loop every post failed
    // and logged, ten lines a second while hopping (ADR-0031). Created once here rather than in
    // WiFiCapture::start(): the loop outlives capture windows, and a second create fails.
    const esp_err_t eventLoopResult = esp_event_loop_create_default();
    if (eventLoopResult != ESP_OK) {
        Serial.printf("[ERROR] esp_event_loop_create_default: %s\n",
                      esp_err_to_name(eventLoopResult));
        haltBootFailure("event loop creation failed");
    }

    // Initialize SD card for session storage
    updateProgress(40, "Checking SD card...");
    bool sdAvailable = g_sessionManager.begin();
    
    if (sdAvailable) {
        updateProgress(60, "Loading configuration...");
        loadConfig(g_config);  // Creates dupin.conf if missing
        
        updateProgress(80, "SD card ready");
    } else {
        updateProgress(60, "No SD card");
        delay(500);
    }
    
    updateProgress(100, "Ready!");
    delay(500);
    
    Serial.println("[BOOT] Initialization complete");
    
    // Show case selection menu if SD available
    if (sdAvailable) {
        // Show case selection menu
        g_inCaseMenu = true;
        refreshCaseList();
        
        while (g_inCaseMenu) {
            renderCaseMenu();
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isChange()) {
                handleCaseMenuInput();
            }
            // loop() has not started yet and does not until a case is chosen, so this is
            // the only place the control channel can be served during it. Without this the
            // firmware is unreachable over serial from boot on any device with an SD card,
            // which is every device that matters (ADR-0014).
            pollSerialCommands();
            delay(50);
        }
    } else {
        Serial.println("[BOOT] SD card not available - using default case");
    }
#endif
    
    // Initialize modes
    Serial.println("[BOOT] Initializing modes...");
    initializeModes();
    Serial.println("[BOOT] Modes OK");
    
    printBanner();
    
#ifndef NATIVE_BUILD
    g_canvas.println("");
    g_canvas.println("Ready!");
    g_canvas.println("");
    g_canvas.println("W=Watson M=Marple");
    g_canvas.println("P=Poirot H=Holmes");
    g_canvas.println("B=Baskerville");
    g_canvas.println("");
    g_canvas.println("ENTER=Start ESC=Stop");
#endif
    
    g_startTime = millis();  // Record start time for uptime
    
    Serial.println();
    Serial.println("[DUPIN] Ready! Press W/M/P/H/B to select mode");
    Serial.println("[DUPIN] ENTER to start capture, ESC to stop");
    Serial.println();
}

#ifndef NATIVE_BUILD
/**
 * @brief Write the baseline to the open case, recording how long the card took.
 *
 * The duration is the probe figure that decides what a lock would cost: the autosave runs
 * on the loop task while onFrame keeps inserting, so a mutex held across this call is a
 * mutex the Wi-Fi task waits on for exactly this long. Both save sites go through here so
 * the figure is the longest save observed, whichever path ran it.
 */
static bool saveBaselineTimed(const Baseline& baseline) {
    const uint32_t startMicros = micros();
    const bool saved = g_sessionManager.saveBaseline(baseline);
    const uint32_t elapsedMicros = micros() - startMicros;
    if (elapsedMicros > g_saveMaxMicros) g_saveMaxMicros = elapsedMicros;
    return saved;
}

/**
 * @brief Hand every queued frame to the modes, on the loop task.
 *
 * The consumer half of ADR-0024. Everything downstream of this call -- the mode tables, the
 * anomaly and finding callbacks, the toast and the tone they raise, the renderer -- runs
 * here, on the one task that also renders on its timer and autosaves to the card.
 *
 * Bounded by the queue's own depth rather than run until empty: the Wi-Fi task keeps posting
 * while this runs, so an unbounded drain under load never hands control back to the rest of
 * the pass -- the channel hop, the render, the save -- which is the work the queue exists to
 * let happen. Whatever arrives past the bound waits one pass, measured at up to 131 ms.
 */
static void drainObservations() {
    Observation obs;
    for (UBaseType_t taken = 0; taken < kObservationQueueDepth; ++taken) {
        if (xQueueReceive(g_observationQueue, &obs, 0) != pdTRUE) break;
        ++g_framesDrained;
        g_modeManager.broadcastObservation(obs);
        g_dupin.recordObservation();
    }
}

/**
 * @brief Say on the wire that the queue is dropping, at most once a second.
 *
 * `[QUEUE]`, not `[ERROR]`: the verify harness fails a run on that prefix (ADR-0009), and a
 * full queue is a load figure rather than a firmware fault. It is said at all because a
 * queue that discards frames in silence reads exactly like a room that went quiet, which is
 * the failure ADR-0024 decision 4 exists to remove.
 */
static void reportDropsIfAny(uint32_t now) {
    static uint32_t lastReportMs = 0;
    static uint32_t lastReportedDrops = 0;

    const uint32_t dropped = g_framesDropped;
    if (dropped == lastReportedDrops) return;
    if (lastReportMs != 0 && now - lastReportMs < kDropReportIntervalMs) return;

    Serial.printf("[QUEUE] dropped=%u accepted=%u depth=%u\n",
                  static_cast<unsigned>(dropped),
                  static_cast<unsigned>(g_framesAccepted),
                  static_cast<unsigned>(kObservationQueueDepth));
    lastReportedDrops = dropped;
    lastReportMs = now;
}
#endif

void loop() {
    static uint32_t lastHeartbeat = 0;
    uint32_t now = millis();
    
#ifndef NATIVE_BUILD
    // Probe sampling, before any work this pass: what a queue would have to absorb is the
    // frames that arrived while the previous pass was busy, so both figures are read at the
    // same point every time.
    if (g_loopTask == nullptr) {
        g_loopTask = xTaskGetCurrentTaskHandle();
        strncpy(g_loopTaskName, pcTaskGetName(g_loopTask), sizeof(g_loopTaskName) - 1);
    }
    {
        static uint32_t lastPassMicros = 0;
        const uint32_t passMicros = micros();
        const uint32_t frames = g_frameCount;
        const uint32_t arrived = frames - g_framesAtLastPass;
        g_framesAtLastPass = frames;
        if (arrived > g_maxFramesPerPass) g_maxFramesPerPass = arrived;
        if (lastPassMicros != 0) {
            const uint32_t gap = passMicros - lastPassMicros;
            if (gap > g_maxPassMicros) g_maxPassMicros = gap;
        }
        lastPassMicros = passMicros;
    }

    // Before any of this pass reads mode state: the frames the previous pass was too busy
    // to take are handed to the modes here, on this task, and nowhere else (ADR-0024).
    drainObservations();
    reportDropsIfAny(now);

    M5Cardputer.update();
    pollSerialCommands();
#endif
    
    // Handle keyboard
    handleKeyboard();
    
    // Update WiFi capture (channel hopping)
    if (g_running) {
        g_wifiCapture.update();
    }
    
    // Update modes periodically
    g_modeManager.update();
    
    // Display refresh every 1 second (for uptime timer and toast)
    static uint32_t lastDisplayRefresh = 0;
    static uint32_t lastFrameCount = 0;
    if (now - lastDisplayRefresh >= 1000) {
        // Calculate frames per second
        g_framesPerSec = g_frameCount - lastFrameCount;
        lastFrameCount = g_frameCount;
        
        lastDisplayRefresh = now;
#ifndef NATIVE_BUILD
        updateDisplay();
#endif
    }
    
    // Serial heartbeat every 5 seconds
    if (now - lastHeartbeat >= 5000) {
        lastHeartbeat = now;
        
        const char* modeName = g_activeMode ? g_activeMode->name() : "none";
        Serial.print("[HB] Mode: ");
        Serial.print(modeName);
        Serial.print(" | Running: ");
        Serial.print(g_running ? "YES" : "NO");
        
        if (g_running) {
            const auto& stats = g_wifiCapture.getStats();
            Serial.print(" | Frames: ");
            Serial.print(stats.framesReceived);
            Serial.print(" | Ch: ");
            Serial.print(g_wifiCapture.getCurrentChannel());
        }
        Serial.println();
    }
    
    // Baseline autosave. hasSession() as well as isAvailable(), for the same reason the stop
    // path checks both: with no case open there is nowhere to write, and the failure branch
    // below would otherwise turn a card-mounted-but-no-case board into an [ERROR] every
    // interval (ADR-0025).
    if (g_running && g_sessionManager.isAvailable() && g_sessionManager.hasSession()
        && g_config.baselineAutosaveInterval > 0) {
        uint32_t autosaveMs = g_config.baselineAutosaveInterval * 1000;
        if (now - g_lastBaselineSave >= autosaveMs) {
            // Check for changes before saving
            const Baseline& bl = g_watson.getBaseline();
            size_t currentCount = bl.count();
            
            if (currentCount != g_lastBaselineCount || g_lastBaselineSave == 0) {
                // Changes detected - save and show status
                int diff = static_cast<int>(currentCount) - static_cast<int>(g_lastBaselineCount);
                
                if (saveBaselineTimed(bl)) {
                    // Show colored status message
                    char statusBuf[48];
                    if (diff > 0) {
                        snprintf(statusBuf, sizeof(statusBuf), "Baseline: +%d SSID%s", diff, diff == 1 ? "" : "s");
                    } else if (diff < 0) {
                        snprintf(statusBuf, sizeof(statusBuf), "Baseline: %d SSID%s", diff, diff == -1 ? "" : "s");
                    } else {
                        snprintf(statusBuf, sizeof(statusBuf), "Baseline saved (%zu SSIDs)", currentCount);
                    }
                    setStatus(statusBuf);
                    g_lastBaselineCount = currentCount;
                } else {
                    // Said out loud like the stop path, but under [SD] rather than [ERROR],
                    // because this failure is retryable and that one is not: g_lastBaselineCount
                    // stays where it was, so the next interval sees the count still differ and
                    // writes again. [ERROR] fails a verify run outright (ADR-0009), and a
                    // transient the firmware recovers from on its own is not a run to fail --
                    // make verify-race sizes its first window to contain one autosave, so this
                    // is a path a gate deliberately drives. A card that is really gone still
                    // reaches [ERROR] at the stop, which is where the retry runs out (ADR-0025).
                    Serial.println("[SD] baseline autosave did not reach the card, retrying");
                    setStatus("Autosave FAILED");
                }
            }
            
            g_lastBaselineSave = now;
        }
    }
    
    delay(10);
}

#ifndef NATIVE_BUILD
/**
 * @brief Draw the header row: the open case's name at the left margin, the scan number right.
 *
 * One function for the two screens that draw this row, and both fields placed from the panel's
 * geometry rather than at literals. The scan number sat at x=180, which holds ten characters, so
 * the field clipped at the panel edge once the session number passed four digits; the name is
 * fitted to what the number leaves, because the two share one row and a full-length name reaches
 * further right than the number's origin (ADR-0013).
 */
static void drawCaseHeader(const char* caseName, uint32_t sessionNumber) {
    char scanText[sizeof("Scan #") + kUint32MaxDigits] = "";
    int scanChars = 0;
    if (sessionNumber > 0) {
        scanChars = snprintf(scanText, sizeof(scanText), "Scan #%u",
                             static_cast<unsigned>(sessionNumber));
    }

    char fittedName[kMaxSessionNameChars + 1];
    dupin::fitText(fittedName, sizeof(fittedName), caseName, dupin::headerNameMaxChars(scanChars));

    g_canvas.setCursor(dupin::kPanelMarginPx, dupin::kHeaderRowTopPx);
    g_canvas.setTextColor(TFT_GREEN);
    g_canvas.print("Case: ");
    g_canvas.setTextColor(TFT_WHITE);
    g_canvas.print(fittedName);

    if (scanChars > 0) {
        g_canvas.setCursor(dupin::rightAlignedX(scanChars), dupin::kHeaderRowTopPx);
        g_canvas.setTextColor(TFT_DARKGREY);
        g_canvas.print(scanText);
    }
}
#endif

/**
 * @brief Update the display with current mode information
 */
void updateDisplay() {
#ifndef NATIVE_BUILD
    // The renderer used to be reachable from two tasks: showToast() draws from wherever it
    // was called, and onAnomalyDetected() called it on the Wi-Fi task. ADR-0024 closed that
    // by moving ingestion behind a queue, so every path into here now begins on the loop
    // task. These two counters are what found the defect and they stay to catch it coming
    // back -- both must read 0 forever, and `make verify-race` asserts it. Counting is still
    // how that is known rather than read off the call graph.
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    const TaskHandle_t alreadyDrawing = g_renderingTask;
    if (alreadyDrawing != nullptr && alreadyDrawing != self) ++g_renderCollisions;
    // Guarded on g_loopTask being known: it is set on the first loop() pass, and a render
    // before that -- setup() draws -- is not evidence of a second task, only of an early one.
    if (g_loopTask != nullptr && self != g_loopTask) ++g_rendersOffLoopTask;
    g_renderingTask = self;
    const uint32_t renderStartMicros = micros();

    g_canvas.fillScreen(TFT_BLACK);
    
    char buf[48];
    
    // === HEADER ===
    g_canvas.fillRect(0, 0, 240, 16, TFT_NAVY);
    g_canvas.setTextSize(2);
    g_canvas.setTextColor(TFT_WHITE, TFT_NAVY);
    g_canvas.setCursor(4, 0);
    
    if (g_activeMode) {
        g_canvas.print(g_activeMode->name());
        // Show session name in smaller text, vertically centered
        if (g_sessionManager.isAvailable() && g_sessionManager.currentSessionName()[0] != '\0') {
            int xPos = g_canvas.getCursorX();  // Save current X after mode name
            g_canvas.setTextSize(1);
            g_canvas.setTextColor(TFT_CYAN, TFT_NAVY);
            g_canvas.setCursor(xPos, 4);  // Y=4 to vertically center size 1 text
            g_canvas.print(" (");
            // Truncate session name if too long
            char sessName[10];
            strncpy(sessName, g_sessionManager.currentSessionName(), 8);
            sessName[8] = '\0';
            g_canvas.print(sessName);
            g_canvas.print(")");
        }
    } else {
        g_canvas.print("DUPIN");
    }
    
    // Running indicator
    g_canvas.setTextSize(1);
    g_canvas.setCursor(200, 4);
    if (g_running) {
        g_canvas.setTextColor(TFT_GREEN, TFT_NAVY);
        g_canvas.print("[ON]");
    } else {
        g_canvas.setTextColor(TFT_RED, TFT_NAVY);
        g_canvas.print("[--]");
    }
    
    // Alert badges (static - use mode initials as key hints)
    if (g_hasUnseenAnomaly) {
        g_canvas.setCursor(165, 4);
        g_canvas.setTextColor(TFT_YELLOW, TFT_NAVY);
        g_canvas.print("M");  // Press M for Marple
    }
    if (g_hasUnseenFinding) {
        g_canvas.setCursor(175, 4);
        g_canvas.setTextColor(TFT_ORANGE, TFT_NAVY);
        g_canvas.print("P");  // Press P for Poirot
    }
    if (g_hasUnseenHypothesis) {
        g_canvas.setCursor(185, 4);
        g_canvas.setTextColor(TFT_RED, TFT_NAVY);
        g_canvas.print("H");  // Press H for Holmes
    }
    
    // === BODY ===
    g_canvas.setTextSize(1);
    g_canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    
    if (!g_running) {
        // Case info at top (if case is selected)
        if (g_sessionManager.hasSession()) {
            drawCaseHeader(g_sessionManager.currentSessionName(),
                           g_watson.getBaseline().getCurrentSession());
        } else {
            g_canvas.setCursor(4, 18);
            g_canvas.setTextColor(TFT_YELLOW);
            g_canvas.print("No case selected");
        }
        
        // Mode selection
        g_canvas.setCursor(4, 32);
        g_canvas.setTextColor(TFT_CYAN);
        g_canvas.println("Select mode:");
        g_canvas.setTextColor(TFT_WHITE);
        g_canvas.println(" D for Dupin (Stats)");
        g_canvas.println(" W for Watson (Observe)");
        g_canvas.println(" M for Marple (Detect)");
        g_canvas.println(" P for Poirot (Investigate)");
        g_canvas.println(" H for Holmes (Correlate)");
        g_canvas.println(" B for Baskerville (Hunt)");
        g_canvas.println(" C for Cases (Select/Create)");
        g_canvas.println("");
        g_canvas.setTextColor(TFT_GREEN);
        g_canvas.println(" ENTER to start capture");
    }
    else if (!g_activeMode) {
        // DUPIN stats screen when running but no mode selected
        const auto& wifiStats = g_wifiCapture.getStats();
        const Baseline& bl = g_watson.getBaseline();
        char buf[48];
        
        // Case info
        if (g_sessionManager.hasSession()) {
            drawCaseHeader(g_sessionManager.currentSessionName(), bl.getCurrentSession());
        } else {
            g_canvas.setCursor(4, 18);
            g_canvas.setTextColor(TFT_YELLOW);
            g_canvas.println("No case selected");
        }
        
        // Stats
        int y = 32;
        g_canvas.setTextColor(TFT_LIGHTGREY);
        g_canvas.setCursor(4, y);
        snprintf(buf, sizeof(buf), "Packets: %u  Beacons: %u",
                 static_cast<unsigned>(wifiStats.framesReceived),
                 static_cast<unsigned>(wifiStats.beacons));
        g_canvas.println(buf);
        y += 10;
        
        g_canvas.setCursor(4, y);
        snprintf(buf, sizeof(buf), "SSIDs: %zu  Probes: %u", bl.count(),
                 static_cast<unsigned>(wifiStats.probeRequests));
        g_canvas.println(buf);
        y += 14;
        
        // Detection counts
        g_canvas.setTextColor(TFT_YELLOW);
        g_canvas.setCursor(4, y);
        g_canvas.println("Detections:");
        y += 10;
        
        size_t anomalies = g_marple.getAnomalyCount();
        size_t findings = g_poirot.getFindingCount();
        size_t hypotheses = g_holmes.getHypothesisCount();
        
        g_canvas.setCursor(4, y);
        g_canvas.setTextColor(anomalies > 0 ? TFT_ORANGE : TFT_DARKGREY);
        snprintf(buf, sizeof(buf), " Anomalies: %zu", anomalies);
        g_canvas.print(buf);
        
        g_canvas.setTextColor(findings > 0 ? TFT_YELLOW : TFT_DARKGREY);
        snprintf(buf, sizeof(buf), "  Findings: %zu", findings);
        g_canvas.println(buf);
        y += 10;
        
        g_canvas.setCursor(4, y);
        g_canvas.setTextColor(hypotheses > 0 ? TFT_RED : TFT_DARKGREY);
        snprintf(buf, sizeof(buf), " Hypotheses: %zu", hypotheses);
        g_canvas.println(buf);
    }
    else {
        const char* modeName = g_activeMode->name();
        
        // Mode description line
        g_canvas.setCursor(4, 18);
        g_canvas.setTextColor(TFT_DARKGREY);
        if (strcmp(modeName, "Watson") == 0) {
            g_canvas.print("Building wireless baseline");
        } else if (strcmp(modeName, "Marple") == 0) {
            g_canvas.print("Detecting anomalies");
        } else if (strcmp(modeName, "Poirot") == 0) {
            g_canvas.print("Investigating findings");
        } else if (strcmp(modeName, "Holmes") == 0) {
            g_canvas.print("Correlating threats");
        } else if (strcmp(modeName, "Baskerville") == 0) {
            g_canvas.print("Physical AP hunting");
        }
        g_canvas.setTextColor(TFT_WHITE);
        
        int yPos = 30;
        
        // Watson - SSID/BSSID discovery list
        if (strcmp(modeName, "Watson") == 0) {
            const Baseline& bl = g_watson.getBaseline();
            size_t ssidCount = bl.count();
            g_currentItemCount = ssidCount;  // For cycling scroll
            const auto& stats = g_wifiCapture.getStats();
            
            // Compact stats line
            snprintf(buf, sizeof(buf), "SSIDs:%zu Bcn:%u Prb:%u Dth:%u",
                     ssidCount, static_cast<unsigned>(stats.beacons),
                     static_cast<unsigned>(stats.probeRequests),
                     static_cast<unsigned>(stats.deauths));
            g_canvas.setCursor(4, yPos);
            g_canvas.println(buf);
            yPos += 12;
            
            // Scrollable list
            g_canvas.setTextColor(TFT_YELLOW);
            g_canvas.setCursor(4, yPos);
            g_canvas.print("SSID");
            g_canvas.setCursor(120, yPos);
            g_canvas.print("Sec");
            g_canvas.setCursor(160, yPos);
            g_canvas.print("APs");
            g_canvas.setCursor(190, yPos);
            g_canvas.println("dBm");
            g_canvas.setTextColor(TFT_WHITE);
            yPos += 10;
            
            // Show 6 items from scroll position
            int shown = 0;
            for (size_t i = g_scrollPos; i < DUPIN_MAX_SSIDS && shown < 6; i++) {
                const SSIDBaseline* entry = bl.at(i);
                if (entry && entry->isActive()) {
                    // Highlight selected item
                    bool isSelected = (shown == g_selectedIndex);
                    if (isSelected) {
                        g_canvas.setTextColor(TFT_CYAN);
                        g_canvas.setCursor(0, yPos);
                        g_canvas.print(">");
                    } else {
                        g_canvas.setTextColor(TFT_WHITE);
                    }
                    
                    g_canvas.setCursor(8, yPos);
                    snprintf(buf, sizeof(buf), "%.18s", entry->ssid);
                    g_canvas.print(buf);
                    
                    // Security type with color coding
                    g_canvas.setCursor(120, yPos);
                    switch (entry->security.type) {
                        case SecurityType::WPA3:
                            g_canvas.setTextColor(TFT_GREEN);
                            g_canvas.print("WPA3");
                            break;
                        case SecurityType::WPA2:
                            g_canvas.setTextColor(TFT_CYAN);
                            g_canvas.print("WPA2");
                            break;
                        case SecurityType::WPA:
                            g_canvas.setTextColor(TFT_YELLOW);
                            g_canvas.print("WPA");
                            break;
                        case SecurityType::WEP:
                            g_canvas.setTextColor(TFT_ORANGE);
                            g_canvas.print("WEP");
                            break;
                        case SecurityType::Open:
                        default:
                            g_canvas.setTextColor(TFT_RED);
                            g_canvas.print("Open");
                            break;
                    }
                    
                    // AP count
                    g_canvas.setTextColor(isSelected ? TFT_CYAN : TFT_WHITE);
                    g_canvas.setCursor(160, yPos);
                    snprintf(buf, sizeof(buf), "%d", entry->bssidCount);
                    g_canvas.print(buf);
                    
                    // RSSI with color coding
                    int8_t rssi = entry->rssiMax;
                    g_canvas.setCursor(190, yPos);
                    if (rssi > -50) {
                        g_canvas.setTextColor(TFT_GREEN);
                    } else if (rssi > -70) {
                        g_canvas.setTextColor(TFT_YELLOW);
                    } else if (rssi > -85) {
                        g_canvas.setTextColor(TFT_ORANGE);
                    } else {
                        g_canvas.setTextColor(TFT_RED);
                    }
                    snprintf(buf, sizeof(buf), "%d", rssi);
                    g_canvas.println(buf);
                    g_canvas.setTextColor(TFT_WHITE);
                    yPos += 10;
                    shown++;
                }
            }
            
            // Scroll hint
            if (ssidCount > 6) {
                g_canvas.setCursor(200, 115);
                g_canvas.setTextColor(TFT_DARKGREY);
                g_canvas.print("^v");
            }
        }
        // Marple - Anomaly list (scrollable)
        else if (strcmp(modeName, "Marple") == 0) {
            size_t count = g_marple.getAnomalyCount();
            snprintf(buf, sizeof(buf), "Anomalies: %zu", count);
            g_canvas.setCursor(4, yPos);
            g_canvas.println(buf);
            yPos += 12;
            
            const Anomaly* anomalies = g_marple.getAnomalies();
            
            // Count active anomalies for scroll bounds
            int activeCount = 0;
            for (size_t i = 0; i < MAX_ANOMALIES; i++) {
                if (anomalies[i].type != AnomalyType::Unknown) activeCount++;
            }
            g_currentItemCount = activeCount;  // For cycling scroll
            
            // Show 6 items from scroll position (unified with Watson)
            int shown = 0;
            int skipped = 0;
            for (size_t i = 0; i < MAX_ANOMALIES && shown < 6; i++) {
                if (anomalies[i].type != AnomalyType::Unknown) {
                    // Skip items before scroll position
                    if (skipped < g_scrollPos) {
                        skipped++;
                        continue;
                    }
                    
                    // Highlight selected item
                    bool isSelected = (shown == g_selectedIndex);
                    if (isSelected) {
                        g_canvas.setCursor(0, yPos);
                        g_canvas.setTextColor(TFT_CYAN);
                        g_canvas.print(">");
                    }
                    
                    g_canvas.setCursor(8, yPos);
                    // Color by severity (or cyan if selected)
                    if (isSelected) {
                        g_canvas.setTextColor(TFT_CYAN);
                    } else if (anomalies[i].severity == Severity::Critical) {
                        g_canvas.setTextColor(TFT_RED);
                    } else if (anomalies[i].severity == Severity::High) {
                        g_canvas.setTextColor(TFT_ORANGE);
                    } else {
                        g_canvas.setTextColor(TFT_WHITE);
                    }
                    
                    // SSID (truncated)
                    snprintf(buf, sizeof(buf), "%.11s", anomalies[i].ssid);
                    g_canvas.print(buf);
                    
                    // Anomaly type description (grey or cyan)
                    g_canvas.setTextColor(isSelected ? TFT_CYAN : TFT_DARKGREY);
                    g_canvas.print(" ");
                    g_canvas.println(anomalyTypeName(anomalies[i].type));
                    
                    yPos += 10;
                    shown++;
                }
            }
            g_canvas.setTextColor(TFT_WHITE);
            
            // Scroll hint
            if (activeCount > 6) {
                g_canvas.setCursor(220, 115);
                g_canvas.setTextColor(TFT_DARKGREY);
                g_canvas.print("^v");
            }
            
            if (count == 0) {
                g_canvas.setCursor(4, yPos + 10);
                g_canvas.setTextColor(TFT_GREEN);
                g_canvas.println("No anomalies detected");
            }
        }
        // Holmes - Hypothesis list (scrollable, 6 items)
        else if (strcmp(modeName, "Holmes") == 0) {
            size_t count = g_holmes.getHypothesisCount();
            snprintf(buf, sizeof(buf), "Hypotheses: %zu", count);
            g_canvas.setCursor(4, yPos);
            g_canvas.println(buf);
            yPos += 12;
            
            const Hypothesis* hyps = g_holmes.getHypotheses();
            
            // Count active hypotheses for scroll bounds
            int activeCount = 0;
            for (size_t i = 0; i < MAX_HYPOTHESES; i++) {
                if (hyps[i].isActive()) activeCount++;
            }
            g_currentItemCount = activeCount;  // For cycling scroll
            
            // Show 6 items from scroll position
            int shown = 0;
            int skipped = 0;
            for (size_t i = 0; i < MAX_HYPOTHESES && shown < 6; i++) {
                if (hyps[i].isActive()) {
                    if (skipped < g_scrollPos) {
                        skipped++;
                        continue;
                    }
                    
                    // Highlight selected item
                    bool isSelected = (shown == g_selectedIndex);
                    if (isSelected) {
                        g_canvas.setCursor(0, yPos);
                        g_canvas.setTextColor(TFT_CYAN);
                        g_canvas.print(">");
                    }
                    
                    g_canvas.setCursor(8, yPos);
                    // Color by threat (or cyan if selected)
                    if (isSelected) {
                        g_canvas.setTextColor(TFT_CYAN);
                    } else if (hyps[i].threat >= ThreatLevel::High) {
                        g_canvas.setTextColor(TFT_RED);
                    } else if (hyps[i].threat == ThreatLevel::Medium) {
                        g_canvas.setTextColor(TFT_ORANGE);
                    } else {
                        g_canvas.setTextColor(TFT_WHITE);
                    }
                    snprintf(buf, sizeof(buf), "%.9s: %.11s", 
                             hypothesisTypeName(hyps[i].type), hyps[i].ssid);
                    g_canvas.println(buf);
                    yPos += 10;
                    shown++;
                }
            }
            g_canvas.setTextColor(TFT_WHITE);
            
            // Scroll hint
            if (activeCount > 6) {
                g_canvas.setCursor(220, 115);
                g_canvas.setTextColor(TFT_DARKGREY);
                g_canvas.print("^v");
            }
        }
        // Baskerville - Hunt mode
        else if (strcmp(modeName, "Baskerville") == 0) {
            if (g_baskerville.isHunting()) {
                const auto& status = g_baskerville.getStatus();
                
                g_canvas.setCursor(4, yPos);
                g_canvas.setTextColor(TFT_CYAN);
                snprintf(buf, sizeof(buf), "Target: %.16s", status.targetSSID);
                g_canvas.println(buf);
                yPos += 12;
                
                // RSSI and signal quality
                g_canvas.setTextColor(TFT_WHITE);
                snprintf(buf, sizeof(buf), "RSSI: %d dBm (%s)", 
                         status.currentRSSI, signalQualityName(status.quality));
                g_canvas.setCursor(4, yPos);
                g_canvas.println(buf);
                yPos += 16;
                
                // Big HOT/COLD indicator - 7 tier system with centered text
                int8_t indicator = g_baskerville.getHotColdIndicator();
                g_canvas.setTextSize(2);
                
                const char* statusText = nullptr;
                uint16_t statusColor = TFT_WHITE;
                
                if (indicator > 50) {
                    statusText = "VERY HOT!";
                    statusColor = TFT_RED;
                } else if (indicator > 20) {
                    statusText = "HOT!";
                    statusColor = TFT_ORANGE;
                } else if (indicator > 5) {
                    statusText = "WARM";
                    statusColor = TFT_YELLOW;
                } else if (indicator >= -5) {
                    statusText = "SEARCHING";
                    statusColor = TFT_DARKGREY;
                } else if (indicator >= -20) {
                    statusText = "COOL";
                    statusColor = TFT_CYAN;
                } else if (indicator >= -50) {
                    statusText = "COLD";
                    statusColor = TFT_BLUE;
                } else {
                    statusText = "VERY COLD!";
                    statusColor = TFT_BLUE;
                }
                
                // Center status text (12 pixels per char at size 2)
                int textWidth = strlen(statusText) * 12;
                int centerX = (240 - textWidth) / 2;
                
                g_canvas.setTextColor(statusColor);
                g_canvas.setCursor(centerX, yPos);
                g_canvas.println(statusText);
                yPos += 24;
                
                // Trend indicator
                g_canvas.setTextSize(1);
                g_canvas.setTextColor(TFT_WHITE);
                int trendX = (240 - strlen(g_baskerville.getStatusText()) * 6) / 2;
                if (trendX < 4) trendX = 4;
                g_canvas.setCursor(trendX, yPos);
                g_canvas.println(g_baskerville.getStatusText());
                
            } else {
                g_canvas.setCursor(4, yPos);
                g_canvas.println("No target selected");
                g_canvas.println("");
                g_canvas.println("Detect threat in Holmes mode");
                g_canvas.println("then switch here to hunt!");
            }
        }
        // Poirot - Findings (scrollable, 6 items)
        else if (strcmp(modeName, "Poirot") == 0) {
            size_t count = g_poirot.getFindingCount();
            snprintf(buf, sizeof(buf), "Findings: %zu", count);
            g_canvas.setCursor(4, yPos);
            g_canvas.println(buf);
            yPos += 12;
            
            const Finding* findings = g_poirot.getFindings();
            
            // Count active findings for scroll bounds
            int activeCount = 0;
            for (size_t i = 0; i < MAX_FINDINGS; i++) {
                if (findings[i].type != FindingType::Unknown) activeCount++;
            }
            g_currentItemCount = activeCount;  // For cycling scroll
            
            // Show 6 items from scroll position
            int shown = 0;
            int skipped = 0;
            for (size_t i = 0; i < MAX_FINDINGS && shown < 6; i++) {
                if (findings[i].type != FindingType::Unknown) {
                    if (skipped < g_scrollPos) {
                        skipped++;
                        continue;
                    }
                    
                    // Highlight selected item
                    bool isSelected = (shown == g_selectedIndex);
                    if (isSelected) {
                        g_canvas.setCursor(0, yPos);
                        g_canvas.setTextColor(TFT_CYAN);
                        g_canvas.print(">");
                    }
                    
                    g_canvas.setCursor(8, yPos);
                    // Color by strength (or cyan if selected)
                    if (isSelected) {
                        g_canvas.setTextColor(TFT_CYAN);
                    } else if (findings[i].strength >= FindingStrength::Strong) {
                        g_canvas.setTextColor(TFT_GREEN);
                    } else if (findings[i].strength == FindingStrength::Moderate) {
                        g_canvas.setTextColor(TFT_YELLOW);
                    } else {
                        g_canvas.setTextColor(TFT_WHITE);
                    }
                    snprintf(buf, sizeof(buf), "%.11s", findings[i].ssid);
                    g_canvas.print(buf);
                    g_canvas.setTextColor(isSelected ? TFT_CYAN : TFT_DARKGREY);
                    g_canvas.print(" ");
                    g_canvas.println(findingTypeName(findings[i].type));
                    yPos += 10;
                    shown++;
                }
            }
            g_canvas.setTextColor(TFT_WHITE);
            
            // Scroll hint
            if (activeCount > 6) {
                g_canvas.setCursor(220, 115);
                g_canvas.setTextColor(TFT_DARKGREY);
                g_canvas.print("^v");
            }
            
            if (count == 0) {
                g_canvas.setCursor(4, yPos + 10);
                g_canvas.println("Investigating anomalies...");
            }
        }
        // Default capture stats
        else {
            const auto& stats = g_wifiCapture.getStats();
            snprintf(buf, sizeof(buf), "Frames: %u", static_cast<unsigned>(stats.framesReceived));
            g_canvas.setCursor(4, yPos);
            g_canvas.println(buf);
        }
    }
    
    // === FOOTER ===
    g_canvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
    g_canvas.setTextSize(1);
    
    // Activity dot (blinks when receiving frames)
    bool isActive = (millis() - g_lastFrameTime) < 500;  // Active if frame in last 500ms
    if (isActive) {
        g_canvas.fillCircle(8, 128, 4, TFT_GREEN);  // Green dot when active
    } else {
        g_canvas.fillCircle(8, 128, 4, TFT_DARKGREY);  // Grey when idle
    }
    
    // Channel + Frame rate (left, after dot)
    g_canvas.setCursor(16, 125);
    uint8_t ch = g_wifiCapture.getCurrentChannel();
    // Color-code channel by activity
    if (isActive && g_framesPerSec > 50) {
        g_canvas.setTextColor(TFT_GREEN, TFT_BLACK);
    } else if (isActive) {
        g_canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
    } else {
        g_canvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
    }
    snprintf(buf, sizeof(buf), "Ch:%d %u/s", ch, static_cast<unsigned>(g_framesPerSec));
    g_canvas.print(buf);
    
    // Status ticker (center) - show recent mode activity
    if (g_statusMessage[0] != '\0' && (millis() - g_statusTime) < 5000) {
        // Status fades after 3 seconds
        uint32_t age = millis() - g_statusTime;
        if (age < 3000) {
            g_canvas.setTextColor(TFT_CYAN, TFT_BLACK);
        } else {
            g_canvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
        }
        g_canvas.setCursor(85, 125);  // Center area
        g_canvas.print(g_statusMessage);
    }
    
    // Uptime (right edge) - mm:ss format
    g_canvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
    uint32_t uptimeSecs = (millis() - g_startTime) / 1000;
    uint32_t mins = uptimeSecs / 60;
    uint32_t secs = uptimeSecs % 60;
    snprintf(buf, sizeof(buf), "%u:%02u", static_cast<unsigned>(mins), static_cast<unsigned>(secs));
    // Right-align
    int uptimeX = dupin::SCREEN_WIDTH - (strlen(buf) * kGlyphWidthPx) - kFooterMarginPx;
    g_canvas.setCursor(uptimeX, 125);
    g_canvas.print(buf);

    // Frames the queue could not hold, on the surface the operator actually has in front of
    // them (ADR-0024 decision 4). Drawn only when there are any, and in red, because it is
    // the one figure in this footer that says the panel above it is incomplete. Right-aligned
    // against the uptime rather than trailing the frame rate, which would run into the status
    // ticker.
    const uint32_t droppedFrames = g_framesDropped;
    if (droppedFrames > 0) {
        char dropBuf[sizeof("!") + kUint32MaxDigits];
        snprintf(dropBuf, sizeof(dropBuf), "!%u", static_cast<unsigned>(droppedFrames));
        g_canvas.setTextColor(TFT_RED, TFT_BLACK);
        g_canvas.setCursor(uptimeX - (strlen(dropBuf) * kGlyphWidthPx) - kFooterMarginPx, 125);
        g_canvas.print(dropBuf);
    }
    
    // === TOAST OVERLAY ===
    if (millis() < g_toastEndTime && g_toastText[0] != '\0') {
        // Draw centered toast box
        int textLen = strlen(g_toastText);
        int boxW = (textLen * kToastGlyphWidthPx) + kToastBoxPaddingPx;
        int boxH = 30;
        int boxX = (SCREEN_WIDTH - boxW) / 2;
        int boxY = 50;
        
        // Background with border
        g_canvas.fillRect(boxX, boxY, boxW, boxH, TFT_BLACK);
        g_canvas.drawRect(boxX, boxY, boxW, boxH, g_toastColor);
        
        // Text
        g_canvas.setTextSize(2);
        g_canvas.setTextColor(g_toastColor, TFT_BLACK);
        g_canvas.setCursor(boxX + 10, boxY + 7);
        g_canvas.print(g_toastText);
        g_canvas.setTextSize(1);
    }
    
    // === POPUP OVERLAY ===
    if (g_popupActive && g_activeMode != nullptr) {
        const char* modeName = g_activeMode->name();
        
        // Draw popup background (centered)
        int popupX = 15;
        int popupY = 20;
        int popupW = 210;
        int popupH = 95;
        
        g_canvas.fillRect(popupX, popupY, popupW, popupH, TFT_BLACK);
        g_canvas.drawRect(popupX, popupY, popupW, popupH, TFT_CYAN);
        g_canvas.drawRect(popupX+1, popupY+1, popupW-2, popupH-2, TFT_CYAN);
        
        int yPos = popupY + 5;
        int xPos = popupX + 5;
        
        // Mode-specific popup content
        if (strcmp(modeName, "Watson") == 0) {
            // Watson: AP details
            const Baseline& bl = g_watson.getBaseline();
            size_t idx = static_cast<size_t>(g_scrollPos + g_selectedIndex);
            if (idx < bl.count()) {
                const SSIDBaseline* entry = bl.at(idx);
                if (entry && entry->isActive()) {
                    // === SSID Header Block ===
                    g_canvas.setTextColor(TFT_CYAN);
                    g_canvas.setCursor(xPos, yPos);
                    snprintf(buf, sizeof(buf), "%.20s", entry->ssid);
                    g_canvas.println(buf); yPos += 12;  // Extra space after header
                    
                    // === BSSIDs Block ===
                    g_canvas.setTextColor(TFT_DARKGREY);
                    g_canvas.setCursor(xPos, yPos);
                    g_canvas.println("Known BSSIDs:"); yPos += 9;
                    
                    g_canvas.setTextColor(TFT_WHITE);
                    char macStr[18];
                    for (uint8_t b = 0; b < entry->bssidCount && b < 2; b++) {
                        macFormat(entry->knownBSSIDs[b], macStr);
                        g_canvas.setCursor(xPos + 8, yPos);  // Consistent indent
                        g_canvas.println(macStr); yPos += 9;
                    }
                    if (entry->bssidCount > 2) {
                        g_canvas.setCursor(xPos + 8, yPos);
                        g_canvas.setTextColor(TFT_DARKGREY);
                        snprintf(buf, sizeof(buf), "+%d more", entry->bssidCount - 2);
                        g_canvas.println(buf); yPos += 9;
                    }
                    yPos += 3;  // Space after BSSIDs block
                    
                    // === Info Block (Channel + Security + Signal + Beacons) ===
                    g_canvas.setTextColor(TFT_WHITE);
                    snprintf(buf, sizeof(buf), "Ch:%d  %s", 
                             entry->channelCount, securityTypeName(entry->security.type));
                    g_canvas.setCursor(xPos, yPos);
                    g_canvas.println(buf); yPos += 11;
                    
                    // Signal quality bar - full width with equal margins
                    int avgRSSI = (entry->rssiMin + entry->rssiMax) / 2;
                    int barMargin = 5;  // Match xPos margin from popup edge
                    int dBmLabelWidth = 42;  // Reserve space for "-99dBm"
                    int barWidth = popupW - (barMargin * 2) - dBmLabelWidth - 4;
                    int barHeight = 6;
                    int barX = xPos;
                    int barY = yPos;
                    
                    // Background
                    g_canvas.fillRect(barX, barY, barWidth, barHeight, TFT_DARKGREY);
                    
                    // Calculate fill based on RSSI (-100 to -30 dBm range)
                    int fillPercent = constrain((avgRSSI + 100) * 100 / 70, 0, 100);
                    int fillWidth = barWidth * fillPercent / 100;
                    
                    // Color based on signal strength
                    uint16_t barColor;
                    if (avgRSSI > -50) barColor = TFT_GREEN;
                    else if (avgRSSI > -65) barColor = TFT_YELLOW;
                    else if (avgRSSI > -80) barColor = TFT_ORANGE;
                    else barColor = TFT_RED;
                    
                    g_canvas.fillRect(barX, barY, fillWidth, barHeight, barColor);
                    g_canvas.drawRect(barX, barY, barWidth, barHeight, TFT_WHITE);
                    
                    // dBm label at end of bar
                    g_canvas.setTextColor(TFT_WHITE);
                    g_canvas.setCursor(barX + barWidth + 4, barY - 1);
                    snprintf(buf, sizeof(buf), "%ddBm", avgRSSI);
                    g_canvas.println(buf);
                    yPos += 10;
                    
                    // Beacons at bottom
                    g_canvas.setTextColor(TFT_DARKGREY);
                    g_canvas.setCursor(xPos, yPos);
                    snprintf(buf, sizeof(buf), "Beacons: %u", static_cast<unsigned>(entry->beaconCount));
                    g_canvas.println(buf);
                }
            }
        }
        else if (strcmp(modeName, "Marple") == 0) {
            // Marple: Anomaly details
            const Anomaly* anomalies = g_marple.getAnomalies();
            int targetIdx = g_scrollPos + g_selectedIndex;
            int found = 0;
            for (size_t i = 0; i < MAX_ANOMALIES; i++) {
                if (anomalies[i].type != AnomalyType::Unknown) {
                    if (found == targetIdx) {
                        const Anomaly& a = anomalies[i];
                        g_canvas.setTextColor(TFT_CYAN);
                        g_canvas.setCursor(xPos, yPos);
                        snprintf(buf, sizeof(buf), "SSID: %.18s", a.ssid);
                        g_canvas.println(buf); yPos += 10;
                        
                        g_canvas.setTextColor(a.severity >= Severity::High ? TFT_RED : TFT_WHITE);
                        snprintf(buf, sizeof(buf), "Type: %s", anomalyTypeName(a.type));
                        g_canvas.setCursor(xPos, yPos);
                        g_canvas.println(buf); yPos += 10;
                        
                        g_canvas.setTextColor(TFT_WHITE);
                        snprintf(buf, sizeof(buf), "Severity: %s", severityName(a.severity));
                        g_canvas.setCursor(xPos, yPos);
                        g_canvas.println(buf); yPos += 10;
                        
                        snprintf(buf, sizeof(buf), "Occurrences: %u", static_cast<unsigned>(a.occurrences));
                        g_canvas.setCursor(xPos, yPos);
                        g_canvas.println(buf); yPos += 10;
                        
                        snprintf(buf, sizeof(buf), "Confidence: %.0f%%", a.confidence * 100);
                        g_canvas.setCursor(xPos, yPos);
                        g_canvas.println(buf);
                        break;
                    }
                    found++;
                }
            }
        }
        else if (strcmp(modeName, "Poirot") == 0) {
            // Poirot: Finding details
            const Finding* findings = g_poirot.getFindings();
            int targetIdx = g_scrollPos + g_selectedIndex;
            int found = 0;
            for (size_t i = 0; i < MAX_FINDINGS; i++) {
                if (findings[i].type != FindingType::Unknown) {
                    if (found == targetIdx) {
                        const Finding& f = findings[i];
                        g_canvas.setTextColor(TFT_CYAN);
                        g_canvas.setCursor(xPos, yPos);
                        snprintf(buf, sizeof(buf), "SSID: %.18s", f.ssid);
                        g_canvas.println(buf); yPos += 10;
                        
                        g_canvas.setTextColor(TFT_GREEN);
                        snprintf(buf, sizeof(buf), "Type: %s", findingTypeName(f.type));
                        g_canvas.setCursor(xPos, yPos);
                        g_canvas.println(buf); yPos += 10;
                        
                        g_canvas.setTextColor(TFT_WHITE);
                        snprintf(buf, sizeof(buf), "Strength: %s", findingStrengthName(f.strength));
                        g_canvas.setCursor(xPos, yPos);
                        g_canvas.println(buf); yPos += 10;
                        
                        snprintf(buf, sizeof(buf), "Anomaly #%u", static_cast<unsigned>(f.anomalyId));
                        g_canvas.setCursor(xPos, yPos);
                        g_canvas.println(buf); yPos += 10;
                        // Word-wrapped explanation
                        g_canvas.setTextColor(TFT_DARKGREY);
                        printWrapped(g_canvas, xPos, yPos, f.explanation, popupW - 15, 10);
                        break;
                    }
                    found++;
                }
            }
        }
        else if (strcmp(modeName, "Holmes") == 0) {
            // Holmes: Hypothesis details + Start Hunt option
            const Hypothesis* hyps = g_holmes.getHypotheses();
            int targetIdx = g_scrollPos + g_selectedIndex;
            int found = 0;
            for (size_t i = 0; i < MAX_HYPOTHESES; i++) {
                if (hyps[i].isActive()) {
                    if (found == targetIdx) {
                        const Hypothesis& h = hyps[i];
                        g_canvas.setTextColor(TFT_CYAN);
                        g_canvas.setCursor(xPos, yPos);
                        snprintf(buf, sizeof(buf), "%.18s", h.ssid);
                        g_canvas.println(buf); yPos += 10;
                        
                        g_canvas.setTextColor(h.threat >= ThreatLevel::High ? TFT_RED : TFT_WHITE);
                        snprintf(buf, sizeof(buf), "%s [%s]", 
                                 hypothesisTypeName(h.type), threatLevelName(h.threat));
                        g_canvas.setCursor(xPos, yPos);
                        g_canvas.println(buf); yPos += 10;
                        
                        g_canvas.setTextColor(TFT_WHITE);
                        snprintf(buf, sizeof(buf), "Confidence: %.0f%% (%d evidence)", 
                                 h.confidence * 100, h.evidenceCount);
                        g_canvas.setCursor(xPos, yPos);
                        g_canvas.println(buf); yPos += 10;
                        // Word-wrapped summary
                        g_canvas.setTextColor(TFT_DARKGREY);
                        printWrapped(g_canvas, xPos, yPos, h.summary, popupW - 15, 10);
                        break;
                    }
                    found++;
                }
            }
        }
        
        // === POPUP FOOTER ===
        // Left side: navigation hints
        g_canvas.setTextColor(TFT_DARKGREY);
        g_canvas.setCursor(popupX + 4, popupY + popupH - 10);
        g_canvas.print("^v:Nav");
        
        // Right side: close hint
        g_canvas.setCursor(popupX + popupW - 60, popupY + popupH - 10);
        g_canvas.print("ESC:Close");
        
        // Mode-specific center action
        if (strcmp(modeName, "Holmes") == 0 && g_currentItemCount > 0) {
            g_canvas.setTextColor(TFT_GREEN);
            g_canvas.setCursor(popupX + 60, popupY + popupH - 10);
            g_canvas.print("ENTER:Hunt");
        } else if ((strcmp(modeName, "Marple") == 0 || strcmp(modeName, "Poirot") == 0) 
                   && g_currentItemCount > 0) {
            g_canvas.setTextColor(TFT_YELLOW);
            g_canvas.setCursor(popupX + 60, popupY + popupH - 10);
            g_canvas.print("DEL:Dismiss");
        }
    }
    
    // === PUSH TO DISPLAY (smooth, no flicker) ===
    g_canvas.pushSprite(0, 0);

    const uint32_t renderMicros = micros() - renderStartMicros;
    if (renderMicros > g_renderMaxMicros) g_renderMaxMicros = renderMicros;
    g_renderingTask = nullptr;
#endif
}

/**
 * @brief Show a centered toast message
 */
void showToast(const char* text, uint16_t color, uint16_t durationMs) {
    // The renderer centres the box on the text width, so text wider than the panel gives
    // a negative origin and draws off the left edge -- silently, because nothing here is a
    // format call for -Wformat-truncation to see. Fitting at the boundary means no caller
    // can produce that, and a shortened string is marked as shortened (ADR-0013).
    fitText(g_toastText, sizeof(g_toastText), text, kToastMaxChars);
    g_toastColor = color;
    g_toastEndTime = millis() + durationMs;
    
#ifndef NATIVE_BUILD
    // Immediately refresh display to show toast
    updateDisplay();
#endif
}

void printBanner() {
    Serial.println();
    Serial.println("╔═══════════════════════════════════════╗");
    // The box is 39 characters wide inside the borders and the padding here is literal,
    // so it stays square only while the version is five characters. A wider one (0.10.0)
    // needs the padding adjusted -- check-version cannot see alignment, only duplication.
    Serial.println("║           D U P I N  v" DUPIN_VERSION_STRING "           ║");
    Serial.println("║   Defensive Wi-Fi Intelligence        ║");
    Serial.println("╚═══════════════════════════════════════╝");
    Serial.println();
}

void initializeModes() {
    // Register all modes
    g_modeManager.registerMode(&g_watson);
    g_modeManager.registerMode(&g_marple);
    g_modeManager.registerMode(&g_poirot);
    g_modeManager.registerMode(&g_holmes);
    g_modeManager.registerMode(&g_baskerville);
    g_modeManager.registerMode(&g_dupin);
    
    // Connect Marple and Poirot to Watson baseline
    g_marple.setBaseline(&g_watson.getBaseline());
    g_poirot.setBaseline(&g_watson.getBaseline());
    
    // Set alert callbacks
    g_watson.setSSIDCallback(onSSIDDiscovered);
    g_marple.setAnomalyCallback(onAnomalyDetected);
    g_poirot.setFindingCallback(onFindingDetected);
    g_holmes.setHypothesisCallback(onHypothesisDetected);
    
    // Setup WiFi frame callback
    g_wifiCapture.setCallback(onFrame);
    
    Serial.print("  Registered ");
    Serial.print(g_modeManager.getModeCount());
    Serial.println(" modes");
}

#ifndef NATIVE_BUILD
/**
 * @brief The one key dispatch. Both the keyboard and the serial channel run this chain.
 *
 * @p keyIsActive answers "is this key the one being pressed". The physical path passes
 * M5Cardputer.Keyboard.isKeyPressed, so its behaviour is unchanged to the bit -- including
 * the priority the else-if order encodes. The serial path passes an equality test against
 * one injected key, which is what makes an injected key *the* key path rather than a copy
 * of it: there is no second chain to drift (ADR-0014).
 *
 * Not host-tested, and deliberately so: every branch drives the display, the mode manager
 * or the session manager, so what it does is device behaviour rather than logic. The
 * host-testable half of this channel is ui/serial_command; this half is what
 * scripts/input-verify.mjs exists to cover (ADR-0002).
 */
template <typename KeyIsActive>
static void dispatchKey(KeyIsActive keyIsActive) {
    // Update item count for current mode (needed BEFORE checking Enter key)
    if (g_activeMode != nullptr) {
        const char* modeName = g_activeMode->name();
        if (strcmp(modeName, "Watson") == 0) {
            g_currentItemCount = g_watson.getBaseline().count();
        } else if (strcmp(modeName, "Marple") == 0) {
            size_t count = 0;
            const Anomaly* anomalies = g_marple.getAnomalies();
            for (size_t i = 0; i < MAX_ANOMALIES; i++) {
                if (anomalies[i].type != AnomalyType::Unknown) count++;
            }
            g_currentItemCount = count;
        } else if (strcmp(modeName, "Poirot") == 0) {
            size_t count = 0;
            const Finding* findings = g_poirot.getFindings();
            for (size_t i = 0; i < MAX_FINDINGS; i++) {
                if (findings[i].type != FindingType::Unknown) count++;
            }
            g_currentItemCount = count;
        } else if (strcmp(modeName, "Holmes") == 0) {
            size_t count = 0;
            const Hypothesis* hyps = g_holmes.getHypotheses();
            for (size_t i = 0; i < MAX_HYPOTHESES; i++) {
                if (hyps[i].isActive()) count++;
            }
            g_currentItemCount = count;
        } else {
            g_currentItemCount = 0;  // Baskerville or other modes
        }
    }
    
    // Mode switching
    if (keyIsActive('w')) {
        g_activeMode = g_modeManager.getMode("Watson");
        g_modeManager.setActiveMode("Watson");
        g_scrollPos = 0;  // Reset scroll
        g_selectedIndex = 0;  // Reset selection
        g_popupActive = false;  // Close any open popup
        setStatus("Observing...");
        Serial.println("[KEY] Watson selected");
    }
    else if (keyIsActive('m')) {
        g_activeMode = g_modeManager.getMode("Marple");
        g_modeManager.setActiveMode("Marple");
        g_scrollPos = 0;  // Reset scroll
        g_selectedIndex = 0;  // Reset selection
        g_popupActive = false;  // Close any open popup
        g_hasUnseenAnomaly = false;  // Clear badge
        setStatus("Detecting...");
        Serial.println("[KEY] Marple selected");
    }
    else if (keyIsActive('p')) {
        g_activeMode = g_modeManager.getMode("Poirot");
        g_modeManager.setActiveMode("Poirot");
        g_scrollPos = 0;  // Reset scroll
        g_selectedIndex = 0;  // Reset selection
        g_popupActive = false;  // Close any open popup
        g_hasUnseenFinding = false;  // Clear badge
        setStatus("Investigating...");
        Serial.println("[KEY] Poirot selected");
    }
    else if (keyIsActive('h')) {
        g_activeMode = g_modeManager.getMode("Holmes");
        g_modeManager.setActiveMode("Holmes");
        g_scrollPos = 0;  // Reset scroll
        g_selectedIndex = 0;  // Reset selection
        g_popupActive = false;  // Close any open popup
        g_hasUnseenHypothesis = false;  // Clear badge
        setStatus("Correlating...");
        Serial.println("[KEY] Holmes selected");
    }
    else if (keyIsActive('b')) {
        g_activeMode = g_modeManager.getMode("Baskerville");
        g_modeManager.setActiveMode("Baskerville");
        g_scrollPos = 0;  // Reset scroll
        g_selectedIndex = 0;  // Reset selection
        g_popupActive = false;  // Close any open popup
        setStatus("Hunting...");
        Serial.println("[KEY] Baskerville selected");
    }
    else if (keyIsActive('c')) {
        // Case selection menu
        Serial.println("[KEY] Case Menu");

        // Refused at the door rather than per action inside: the menu's modal loop stalls
        // channel hopping, and opening a case clears Watson's baseline while onFrame is still
        // inserting into it from the Wi-Fi task. The toast is seen, because this is the main
        // screen and not the menu's 50 ms repaint (ADR-0022).
        if (g_running) {
            Serial.println("[CASE] Case menu refused: capture is running");
            showToast("Stop capture first", TFT_YELLOW, 1500);
            return;
        }

        // If SD card not available, try to reinit (useful after M5Launcher)
        if (!g_sessionManager.isAvailable()) {
            showToast("Reinit SD...", TFT_YELLOW, 500);
            delay(500);
            
            Serial.println("[SD] Attempting reinit after user request...");
            if (g_sessionManager.begin()) {
                Serial.println("[SD] Reinit successful!");
                showToast("SD Card OK!", TFT_GREEN, 1000);
            } else {
                Serial.println("[SD] Reinit failed - try removing and reinserting card");
                // 18 characters, the toast ceiling: "SD Failed - Reinsert" was 20 and
                // drew off the panel edge. fitText() would now shorten it, but a message
                // authored to fit says more than one ending in "..." (ADR-0013).
                showToast("SD Fail - Reinsert", TFT_RED, 2000);
                return;  // Don't show menu if SD still not available
            }
        }
        
        g_inCaseMenu = true;
        refreshCaseList();
        
        while (g_inCaseMenu) {
            renderCaseMenu();
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isChange()) {
                handleCaseMenuInput();
            }
            // This loop blocks loop(), so without pumping the channel here the device goes
            // deaf for as long as the menu is open -- and an injected `key c` would wedge
            // it until someone reached for the keyboard (ADR-0014).
            pollSerialCommands();
            delay(50);
        }
        
        // Show confirmation toast
        // Sized to hold the longest real case name rather than to the toast ceiling:
        // shortening is showToast()'s job and it marks what it shortened, whereas losing
        // characters here would be silent (ADR-0013).
        char toast[sizeof("Case: ") + kMaxSessionNameChars];
        if (g_sessionManager.hasSession()) {
            snprintf(toast, sizeof(toast), "Case: %s", g_sessionManager.currentSessionName());
        } else {
            snprintf(toast, sizeof(toast), "No Case");
        }
        showToast(toast, TFT_CYAN, 1500);
    }
    else if (keyIsActive('d')) {
        // Return to Dupin home screen (clear active mode)
        Serial.println("[KEY] Dupin Home");
        g_activeMode = nullptr;
        g_scrollPos = 0;
        g_selectedIndex = 0;
        g_popupActive = false;
    }
    // UNIFIED Enter handler: start capture OR popup/hunt
    else if (keyIsActive(KEY_ENTER) || 
             keyIsActive('\n') ||
             keyIsActive('\r') ||
             keyIsActive(0x0D) ||
             keyIsActive(0x28)) {
        
        if (!g_running) {
            // Not running: start capture
            Serial.println("[KEY] Starting capture...");
            
            // Increment session counter and set on baseline
            Baseline& baseline = g_watson.getBaseline();
            uint32_t newSession = baseline.getCurrentSession() + 1;
            baseline.setCurrentSession(newSession);
            Serial.printf("[BASELINE] Session #%u started\n", static_cast<unsigned>(newSession));
            
            // Expire stale SSIDs based on config threshold
            size_t expired = baseline.expireStaleEntries(g_config.staleSessionThreshold);
            if (expired > 0) {
                Serial.printf("[BASELINE] Expired %zu stale SSIDs\n", expired);
            }
            
            CaptureConfig config;
            config.enableChannelHopping = true;
            config.channelHopIntervalMs = 100;
            
            if (g_wifiCapture.start(config)) {
                g_running = true;
                g_modeManager.startAll();
                setStatus("Scanning...");
                Serial.println("[CAPTURE] Started!");
                showToast("STARTED", TFT_GREEN, 1500);
            } else {
                Serial.println("[CAPTURE] Failed to start!");
                showToast("FAILED!", TFT_RED, 2000);
            }
        } else if (g_popupActive && g_activeMode != nullptr && strcmp(g_activeMode->name(), "Holmes") == 0) {
            // Holmes popup: Start Hunt action
            const Hypothesis* hyps = g_holmes.getHypotheses();
            int targetIdx = g_scrollPos + g_selectedIndex;
            int foundIdx = 0;
            for (size_t i = 0; i < MAX_HYPOTHESES; i++) {
                if (hyps[i].isActive()) {
                    if (foundIdx == targetIdx) {
                        if (g_baskerville.startHunt(hyps[i])) {
                            g_activeMode = g_modeManager.getMode("Baskerville");
                            g_modeManager.setActiveMode("Baskerville");
                            g_scrollPos = 0;
                            g_selectedIndex = 0;
                            g_popupActive = false;
                            setStatus("Hunting...");
                            Serial.printf("[KEY] Hunt started: %s\n", hyps[i].ssid);
                            showToast("HUNTING", TFT_MAGENTA, 1500);
                        }
                        break;
                    }
                    foundIdx++;
                }
            }
        } else if (!g_popupActive && g_currentItemCount > 0 && g_activeMode != nullptr) {
            // Open popup for selected item
            g_popupActive = true;
        } else if (g_popupActive) {
            // In non-Holmes popup, close it
            g_popupActive = false;
        }
    }
    // UNIFIED ESC/Backtick handler: close popup OR stop scan
    else if (keyIsActive(96) || keyIsActive('`')) {
        if (g_popupActive) {
            // Priority: close popup first
            g_popupActive = false;
            g_selectedIndex = 0;
            Serial.println("[KEY] Popup closed");
        } else if (g_running) {
            // No popup: stop capture
            Serial.println("[KEY] Stop detected - Stopping capture...");
            g_wifiCapture.stop();
            // The producer is gone; what it already posted is still in the queue, and the
            // baseline written a few lines below is what those frames belong in. Draining
            // here is also what makes the conservation identity exact at rest, which is
            // what make verify-race asserts (ADR-0024 decision 5).
            drainObservations();
            g_modeManager.stopAll();
            g_running = false;
            
            // Stopping is about the radio, so the card stays mounted and the case stays open:
            // the next capture window in this boot writes where this one did. Putting
            // g_sessionManager.end() back here is the plausible "fix" that silently lost every
            // window after the first -- it clears m_currentSession, so both saves below return
            // false without being reached (ADR-0025).
            if (!g_sessionManager.isAvailable() || !g_sessionManager.hasSession()) {
                // Not a fault: a board with no card, or one whose operator closed the case menu
                // without opening a case, is a declared configuration. Said out loud anyway,
                // because a capture that wrote nothing must not read like a room that was quiet.
                Serial.println("[SD] No case open, nothing saved");
                showToast("No case: not saved", TFT_YELLOW, 2000);
            } else {
                const bool baselineSaved = saveBaselineTimed(g_watson.getBaseline());
                
                // Build and save case stats
                const auto& wifiStats = g_wifiCapture.getStats();
                const Baseline& bl = g_watson.getBaseline();
                
                CaseStats caseStats;
                caseStats.totalUptime = (millis() - g_startTime) / 1000;
                caseStats.sessionCount = 1;  // TODO: Load and increment from existing case.json
                caseStats.packetsTotal = wifiStats.framesReceived;
                caseStats.beacons = wifiStats.beacons;
                caseStats.probes = wifiStats.probeRequests;
                caseStats.deauths = wifiStats.deauths;
                caseStats.ssidCount = static_cast<uint16_t>(bl.count());
                caseStats.bssidCount = 0;  // Would need to sum across all SSIDs
                caseStats.anomalyCount = static_cast<uint16_t>(g_marple.getAnomalyCount());
                caseStats.findingCount = static_cast<uint16_t>(g_poirot.getFindingCount());
                caseStats.hypothesisCount = static_cast<uint16_t>(g_holmes.getHypothesisCount());
                
                const bool statsSaved = g_sessionManager.saveCaseStats(caseStats);
                
                if (baselineSaved && statsSaved) {
                    Serial.printf("[SD] Saved to case %s\n", g_sessionManager.currentSessionName());
                    showToast("STOPPED", TFT_RED, 1500);
                } else {
                    // [ERROR] rather than a notice, because the card was there and the write did
                    // not land -- the one outcome the operator cannot recover by looking
                    // elsewhere. ADR-0009 makes this fail a verify run, which is the point: the
                    // old line printed "Session saved" after discarding both of these (ADR-0025).
                    Serial.printf("[ERROR] stop could not write to case %s: baseline=%s stats=%s\n",
                                  g_sessionManager.currentSessionName(),
                                  baselineSaved ? "ok" : "failed",
                                  statsSaved ? "ok" : "failed");
                    showToast("SAVE FAILED", TFT_RED, 3000);
                }
            }
            
            Serial.println("[CAPTURE] Stopped");
        }
    }
    // DEL/Backspace key (0x2A HID): dismiss selected item (Marple anomaly or Poirot finding)
    else if (keyIsActive(0x2A) && g_popupActive && g_activeMode != nullptr) {
        const char* modeName = g_activeMode->name();
        int targetIdx = g_scrollPos + g_selectedIndex;
        
        if (strcmp(modeName, "Marple") == 0 && g_currentItemCount > 0) {
            // Dismiss anomaly and cascade to Poirot/Holmes
            uint32_t anomalyId = g_marple.dismissAnomaly(static_cast<size_t>(targetIdx));
            if (anomalyId > 0) {
                // Cascade: remove related findings and hypotheses
                g_poirot.removeFindingsForAnomaly(anomalyId);
                g_holmes.removeHypothesesForAnomaly(anomalyId);
                
                // Close popup and adjust selection if needed
                g_popupActive = false;
                if (g_selectedIndex > 0 && 
                    static_cast<size_t>(g_scrollPos + g_selectedIndex) >= g_marple.getAnomalyCount()) {
                    g_selectedIndex--;
                }
                showToast("DISMISSED", TFT_YELLOW, 1000);
            }
        } else if (strcmp(modeName, "Poirot") == 0 && g_currentItemCount > 0) {
            // Dismiss finding
            if (g_poirot.dismissFinding(static_cast<size_t>(targetIdx))) {
                // Close popup and adjust selection if needed
                g_popupActive = false;
                if (g_selectedIndex > 0 && 
                    static_cast<size_t>(g_scrollPos + g_selectedIndex) >= g_poirot.getFindingCount()) {
                    g_selectedIndex--;
                }
                showToast("DISMISSED", TFT_YELLOW, 1000);
            }
        }
    }
    // Navigate up (o key on Cardputer) - selection-first model
    else if (keyIsActive('o') ||
             keyIsActive(';')) {
        if (g_popupActive) {
            // In popup: navigate to previous item, respecting bounds
            if (g_selectedIndex > 0) {
                g_selectedIndex--;
            } else if (g_scrollPos > 0) {
                g_scrollPos--;  // Scroll up when at top of visible area
            }
        } else if (g_currentItemCount > 0) {
            // Selection-first navigation: move selection up, scroll if at top
            if (g_selectedIndex > 0) {
                // Move selection up within visible area
                g_selectedIndex--;
            } else if (g_scrollPos > 0) {
                // At top of visible area, scroll up
                g_scrollPos--;
            } else if (g_currentItemCount > 6) {
                // At very top, wrap to bottom
                g_scrollPos = static_cast<int>(g_currentItemCount) - 6;
                g_selectedIndex = 5;  // Select last visible item
            }
        }
    }
    // Navigate down (l key on Cardputer) - selection-first model
    else if (keyIsActive('l') ||
             keyIsActive('.')) {
        if (g_popupActive) {
            // In popup: navigate to next item but respect bounds
            int maxIndex = static_cast<int>(g_currentItemCount) - 1;
            if (g_scrollPos + g_selectedIndex < maxIndex) {
                if (g_selectedIndex < 5) {
                    g_selectedIndex++;
                } else if (g_scrollPos < maxIndex - 5) {
                    g_scrollPos++;
                }
            }
        } else if (g_currentItemCount > 0) {
            // Calculate max visible items (min of 6 or item count)
            int maxVisible = g_currentItemCount > 6 ? 6 : static_cast<int>(g_currentItemCount);
            int maxSelection = maxVisible - 1;
            
            // Selection-first navigation: move selection down, scroll if at bottom
            if (g_selectedIndex < maxSelection) {
                // Move selection down within visible area
                g_selectedIndex++;
            } else if (g_scrollPos < static_cast<int>(g_currentItemCount) - 6) {
                // At bottom of visible area, scroll down
                g_scrollPos++;
            } else if (g_currentItemCount > 6) {
                // At very bottom, wrap to top
                g_scrollPos = 0;
                g_selectedIndex = 0;
                Serial.println("[KEY] Wrap to top");
            }
        }
    }
    
    // Refresh display immediately on any keypress
    updateDisplay();
}
#endif

#ifndef NATIVE_BUILD
/**
 * @brief Report the state a verify script can assert on.
 *
 * Serial.print rather than printf: there is no format string here, so there is nothing for
 * invariant 6's contract to get wrong. The fields are the ones a gate needs to tell one
 * firmware state from another -- which mode is active, whether capture is running, whether
 * the modal case menu has the foreground, and what the baseline holds.
 */
static void reportState() {
    Serial.print("[STATE] mode=");
    Serial.print(g_activeMode != nullptr ? g_activeMode->name() : "none");
    Serial.print(" running=");
    Serial.print(g_running ? "yes" : "no");
    Serial.print(" menu=");
    Serial.print(g_inCaseMenu ? "yes" : "no");
    Serial.print(" case=");
    Serial.print(g_sessionManager.hasSession() ? g_sessionManager.currentSessionName() : "-");
    Serial.print(" ssids=");
    Serial.print(g_watson.getBaseline().count());
    // Frames the queue could not hold. Here as well as on the panel and in `probe` because
    // this is the field a gate reads, and a drop nobody can see is the one ADR-0024 decision
    // 4 is about.
    Serial.print(" dropped=");
    Serial.print(g_framesDropped);
    // The highlighted row. The card decides where a case sits in the list, so this is how a
    // gate finds one by navigating rather than by a parallel command (invariant 14). '+' is
    // [+] New Case and '-' a closed menu; neither is a name character, so no case reads as
    // either (ADR-0020).
    Serial.print(" sel=");
    const int selectedCase = g_caseMenuSelection - 1;
    if (!g_inCaseMenu) {
        Serial.print("-");
    } else if (g_caseMenuSelection == 0) {
        Serial.print("+");
    } else if (selectedCase < static_cast<int>(g_caseCount)) {
        Serial.print(g_cases[selectedCase].name);
    } else {
        Serial.print("-");
    }
    // Last, and runs to end of line: the notice is a sentence with spaces in it. Without
    // this field a gate could only assert that something did *not* happen, which is the
    // assertion that also passes on firmware that does nothing at all (ADR-0019). It is
    // read-only and reports what is already on the panel, so it grants no capability the
    // operator lacks -- invariant 14 still holds.
    Serial.print(" notice=");
    Serial.println(g_caseNotice[0] != '\0' ? g_caseNotice : "-");
}

/**
 * @brief Report the concurrency figures scripts/race-verify.mjs measures.
 *
 * Read-only and volunteers nothing the operator could not reach at the keyboard, so
 * invariant 14 holds as it does for `state`. Serial.print rather than printf for the same
 * reason reportState() uses it: no format string, nothing for invariant 6 to get wrong.
 * Stack figures are bytes -- ESP-IDF's uxTaskGetStackHighWaterMark returns bytes, not the
 * words vanilla FreeRTOS returns.
 */
static void reportProbe() {
    Serial.print("[PROBE] frameTask=");
    Serial.print(g_frameTaskName[0] != '\0' ? g_frameTaskName : "-");
    Serial.print(" loopTask=");
    Serial.print(g_loopTaskName[0] != '\0' ? g_loopTaskName : "-");
    Serial.print(" frameStackFreeBytes=");
    if (g_frameStackFreeBytes == kStackUnsampled) {
        Serial.print("-");
    } else {
        Serial.print(g_frameStackFreeBytes);
    }
    Serial.print(" frames=");
    Serial.print(g_frameCount);
    Serial.print(" maxFramesPerPass=");
    Serial.print(g_maxFramesPerPass);
    Serial.print(" maxPassUs=");
    Serial.print(g_maxPassMicros);
    Serial.print(" maxRenderUs=");
    Serial.print(g_renderMaxMicros);
    Serial.print(" rendersOffLoopTask=");
    Serial.print(g_rendersOffLoopTask);
    Serial.print(" renderCollisions=");
    Serial.print(g_renderCollisions);
    Serial.print(" maxSaveUs=");
    Serial.print(g_saveMaxMicros);
    // How the boot mount went. A power-cycled or launcher-started boot printed its [SD] lines
    // before any host was listening, and those are the boots scripts/sd-mount-probe.mjs needs
    // (ADR-0032). sdMountedHz=0 is a card that did not mount.
    const MountReport& mount = g_sessionManager.mountReport();
    Serial.print(" sdAttempts=");
    Serial.print(static_cast<unsigned>(mount.attempts));
    Serial.print(" sdMountedHz=");
    Serial.print(mount.mountedHz);
    // Frame conservation (ADR-0024 decision 5). With the producer stopped, queued is 0 and
    // accepted == drained + dropped exactly; that identity is the assertion, because a
    // dropped=0 check passes just as well on firmware that captured nothing. slotBytes and
    // queueDepth are reported rather than assumed, so the memory the depth costs is read off
    // the device instead of recomputed from a struct definition that may have moved on.
    Serial.print(" accepted=");
    Serial.print(g_framesAccepted);
    Serial.print(" drained=");
    Serial.print(g_framesDrained);
    Serial.print(" dropped=");
    Serial.print(g_framesDropped);
    Serial.print(" queued=");
    Serial.print(static_cast<uint32_t>(uxQueueMessagesWaiting(g_observationQueue)));
    Serial.print(" queueDepth=");
    Serial.print(static_cast<uint32_t>(kObservationQueueDepth));
    Serial.print(" slotBytes=");
    Serial.println(static_cast<uint32_t>(sizeof(Observation)));
}

static CommandReader g_commandReader;

/**
 * @brief Refuse the line just read, quoting it so trailing spaces and stray bytes show.
 *
 * Something unidentified writes to this port around an attach, and `[CMD] err unknown` alone
 * said only that it had (ADR-0032).
 */
static void reportRefusedLine(const char* reason) {
    char received[kEscapedCommandSize];
    g_commandReader.escapedLine(received);
    Serial.printf("[CMD] err %s \"%s\"\n", reason, received);
}

/**
 * @brief Drain and act on pending serial input.
 *
 * Every reply is prefixed `[CMD]` or `[STATE]`, never `[ERROR]` or `[FATAL]`: those are the
 * patterns the verify harness fails on (ADR-0009), and a refused command is not a firmware
 * fault. A command that cannot be honoured says so rather than being dropped -- a gate that
 * cannot tell "refused" from "never arrived" reports the wrong thing.
 */
static void pollSerialCommands() {
    size_t budget = kMaxCommandBytesPerPoll;
    while (budget > 0 && Serial.available() > 0) {
        --budget;

        Command cmd;
        if (!g_commandReader.feed(static_cast<char>(Serial.read()), cmd)) continue;

        switch (cmd.kind) {
            case CommandKind::None:
                break;

            case CommandKind::Ping:
                Serial.println("[CMD] pong");
                break;

            case CommandKind::State:
                reportState();
                break;

            case CommandKind::Probe:
                reportProbe();
                break;

            case CommandKind::Key: {
                Serial.print("[CMD] key ");
                if (cmd.key >= 0x20 && cmd.key < 0x7f) {
                    Serial.println(cmd.key);
                } else {
                    // enter/del/tab have no readable character; echo the code so a
                    // transcript says which key was asked for.
                    Serial.print("0x");
                    Serial.println(static_cast<int>(static_cast<unsigned char>(cmd.key)), HEX);
                }

                // Which dispatch a key belongs to is a property of what has the foreground,
                // exactly as it is for the physical keyboard: the case menu owns input while
                // it is open, and loop() owns it otherwise.
                if (g_inCaseMenu) {
                    char one[2] = {cmd.key, '\0'};
                    KeyEvent ev;
                    switch (cmd.key) {
                        case kKeyEnter: ev.enter = true; one[0] = '\0'; break;
                        case kKeyDel:   ev.del = true;   one[0] = '\0'; break;
                        case kKeyTab:   ev.tab = true;   one[0] = '\0'; break;
                        default: break;
                    }
                    ev.chars = one;
                    dispatchCaseMenuKey(ev);
                } else {
                    dispatchKey([&cmd](int k) {
                        return k == static_cast<int>(static_cast<unsigned char>(cmd.key));
                    });
                }
                break;
            }

            case CommandKind::Unknown:
                reportRefusedLine("unknown");
                break;

            case CommandKind::TooLong:
                reportRefusedLine("too-long");
                break;
        }
    }
}
#endif

void handleKeyboard() {
#ifndef NATIVE_BUILD
    // Update keyboard state
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;

    // The KeysState this used to read was never looked at; the chain tests keys through
    // the predicate instead.
    dispatchKey([](int k) { return M5Cardputer.Keyboard.isKeyPressed(k); });
#endif
}

void onFrame(const uint8_t* data, size_t len, int8_t rssi, uint8_t channel) {
    // Track activity
    g_frameCount++;

#ifndef NATIVE_BUILD
    // Measured rather than inferred: which task the promiscuous callback runs on is what
    // decides whether the modes are reached from one task or two. Sampled every few frames
    // because reading a task's high-water mark walks its stack, and a probe that slowed the
    // callback down would be measuring itself.
    if ((g_frameCount % kProbeSampleEveryFrames) == 1) {
        const TaskHandle_t self = xTaskGetCurrentTaskHandle();
        strncpy(g_frameTaskName, pcTaskGetName(self), sizeof(g_frameTaskName) - 1);
        const uint32_t freeBytes = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(self));
        if (freeBytes < g_frameStackFreeBytes) g_frameStackFreeBytes = freeBytes;
    }
#endif
    g_lastFrameTime = millis();
    
    // Parse frame into observation
    Observation obs;
    if (!FrameParser::parse(data, len, rssi, channel, obs)) {
        return;
    }
    
    obs.timestamp = millis();

#ifndef NATIVE_BUILD
    // Parse and post, and nothing else (ADR-0024). This used to call straight into every
    // active mode from here, so Marple could raise an anomaly, which raised a toast, which
    // ran a full render and pushSprite() -- on the Wi-Fi task, into the same canvas loop()
    // draws. Handing the frame over instead is what moves all of that onto one task.
    //
    // Zero ticks, never a block: this is the Wi-Fi stack's own task, and a producer that
    // waits here stalls the radio. A frame the queue cannot hold is counted and reported
    // rather than waited for -- what is lost is one observation, and what is not lost is
    // the knowledge that it was (decision 4).
    ++g_framesAccepted;
    if (xQueueSend(g_observationQueue, &obs, 0) != pdTRUE) {
        ++g_framesDropped;
    }
#endif
}

/**
 * @brief Called when Marple detects an anomaly
 */
void onAnomalyDetected(const Anomaly& anomaly) {
    g_hasUnseenAnomaly = true;
    
    // Update status ticker
    char statusBuf[48];
    snprintf(statusBuf, sizeof(statusBuf), "! %.16s", anomaly.ssid);
    setStatus(statusBuf);
    
    Serial.print("[ALERT] Anomaly detected: ");
    Serial.println(anomaly.ssid);
    
    // Pass anomaly to Poirot for follow-up investigation
    if (g_poirot.isActive()) {
        if (g_poirot.investigate(anomaly)) {
            Serial.println("[POIROT] Investigation opened");
        }
    }
    
    // Pass anomaly to Holmes for hypothesis generation
    if (g_holmes.isActive()) {
        g_holmes.processAnomaly(anomaly);
    }
    
#ifndef NATIVE_BUILD
    // Beep! Short high tone
    M5Cardputer.Speaker.tone(2000, 100);  // 2kHz for 100ms
    
    // Show toast if not on Marple screen
    if (!g_activeMode || strcmp(g_activeMode->name(), "Marple") != 0) {
        showToast("ANOMALY!", TFT_YELLOW, 2000);
    }
#endif
}

/**
 * @brief Called when Poirot makes a finding from investigation
 */
void onFindingDetected(const Finding& finding) {
    // Update status ticker
    char statusBuf[48];
    snprintf(statusBuf, sizeof(statusBuf), ">> %.16s", finding.ssid);
    setStatus(statusBuf);
    
    Serial.print("[FINDING] ");
    Serial.print(findingTypeName(finding.type));
    Serial.print(" - ");
    Serial.println(finding.explanation);
    
    // Set badge for unseen finding
    g_hasUnseenFinding = true;
    
    // Pass finding to Holmes for correlation and hypothesis generation
    if (g_holmes.isActive()) {
        g_holmes.processFinding(finding);
    }
}

/**
 * @brief Called when Holmes generates a hypothesis
 */
void onHypothesisDetected(const Hypothesis& hypothesis) {
    g_hasUnseenHypothesis = true;
    
    // Update status ticker
    char statusBuf[48];
    snprintf(statusBuf, sizeof(statusBuf), "H: %.12s!", hypothesisTypeName(hypothesis.type));
    setStatus(statusBuf);
    
    Serial.print("[ALERT] Hypothesis: ");
    Serial.print(hypothesisTypeName(hypothesis.type));
    Serial.print(" - ");
    Serial.println(hypothesis.ssid);
    
#ifndef NATIVE_BUILD
    // Beep! Double beep for threats
    if (hypothesis.threat >= ThreatLevel::High) {
        M5Cardputer.Speaker.tone(1500, 150);
        delay(50);
        M5Cardputer.Speaker.tone(1500, 150);
        
        showToast("THREAT!", TFT_RED, 2500);
    }
#endif
}

/**
 * @brief Called when Watson discovers a new SSID
 */
void onSSIDDiscovered(const char* ssid, const uint8_t* /* bssid */) {
    // Update status ticker with new SSID
    char statusBuf[48];
    snprintf(statusBuf, sizeof(statusBuf), "+%.18s", ssid);
    setStatus(statusBuf);
}

// ============================================
// Session Menu Functions
// ============================================

void refreshCaseList() {
    const SessionListing listing = g_sessionManager.listSessions(g_cases, kMaxListedCases);
    g_caseCount = listing.count;
    g_caseListTruncated = listing.truncated;

    // The overflow is said out loud on both surfaces the operator has. A card holding more
    // cases than the cache takes used to read exactly like a card holding this many
    // (ADR-0023); the title carries the same fact as a trailing '+'.
    Serial.printf("[MENU] Found %u case(s)%s\n", static_cast<unsigned>(g_caseCount),
                  g_caseListTruncated ? ", card holds more than the list can cache" : "");
}

void renderCaseMenu() {
#ifndef NATIVE_BUILD
    g_canvas.fillScreen(TFT_BLACK);
    g_canvas.setTextSize(1);
    
    // Which cases this panel shows. Selection 0 is the "[+] New Case" row, so the case the
    // window has to keep on screen is one below it (ADR-0023).
    const CaseListWindow window = caseListWindow(
        g_caseMenuSelection > 0 ? static_cast<size_t>(g_caseMenuSelection - 1) : 0,
        g_caseCount, static_cast<size_t>(kCaseMenuCaseRows));

    // Title. It carries the window's place in the list whenever the list outruns the panel,
    // and a trailing '+' when the card holds more cases than the cache took -- the two facts
    // that were previously visible nowhere (ADR-0023).
    g_canvas.setTextColor(TFT_CYAN);
    g_canvas.setCursor(10, 5);
    if (window.count < g_caseCount || g_caseListTruncated) {
        // Sized for the widest uint32_t per %u, because that is the bound the compiler puts
        // on one and -Werror=format is on. That the figures fit the panel is the tighter
        // claim, and it rests on every one of them being bounded by kMaxListedCases
        // (ADR-0013).
        static_assert(sizeof("DUPIN - Select Case ") - 1 + kCaseCounterMaxChars
                          <= static_cast<size_t>(kCaseNoticeMaxChars),
                      "the case-menu title must fit the panel at the margin it shares with "
                      "the notice");
        char title[sizeof("DUPIN - Select Case ") + 3 * kUint32MaxDigits + sizeof("-/+")];
        snprintf(title, sizeof(title), "DUPIN - Select Case %u-%u/%u%c",
                 static_cast<unsigned>(window.first + 1),
                 static_cast<unsigned>(window.first + window.count),
                 static_cast<unsigned>(g_caseCount),
                 g_caseListTruncated ? '+' : ' ');
        g_canvas.print(title);
    } else {
        g_canvas.print("DUPIN - Select Case");
    }
    
    // Horizontal line
    g_canvas.drawFastHLine(0, 18, 240, TFT_DARKGREY);
    
    // Text input mode
    if (g_inTextInput) {
        g_canvas.setTextColor(TFT_WHITE);
        g_canvas.setCursor(10, 30);
        g_canvas.print(g_renameMode ? "Rename Case:" : "New Case Name:");
        
        // Input box
        g_canvas.drawRect(10, 45, 220, 20, TFT_WHITE);
        g_canvas.setCursor(14, 50);
        g_canvas.print(g_inputBuffer);
        
        // Cursor blink
        if ((millis() / 500) % 2 == 0) {
            int cursorX = 14 + g_inputPos * 6;
            g_canvas.drawFastVLine(cursorX, 47, 14, TFT_CYAN);
        }
        
        // Instructions
        g_canvas.setTextColor(TFT_DARKGREY);
        g_canvas.setCursor(10, 75);
        // States the rule isValidNameChar() applies; the refusal message is pinned to it by
        // `make test`, but this line is in main.cpp and is kept in step by review (ADR-0019).
        g_canvas.print("A-Z a-z 0-9 _ | ENTER=OK | DEL=Back");

        // The refusal, under the help line rather than over it: the operator needs both
        // at once, because the reason is what they are about to act on (ADR-0019).
        if (g_caseNotice[0] != '\0') {
            g_canvas.setTextColor(TFT_RED);
            g_canvas.setCursor(10, 95);
            g_canvas.print(g_caseNotice);
        }
        
        g_canvas.pushSprite(0, 0);
        return;
    }
    
    // Session list
    int y = kCaseMenuListTopPx;

    // "New Session" option always first, and pinned: it is an action rather than a case, so
    // it does not scroll away with the window below it.
    bool selected = (g_caseMenuSelection == 0);
    if (selected) {
        g_canvas.fillRect(0, y, 240, 12, TFT_NAVY);
    }
    g_canvas.setTextColor(selected ? TFT_YELLOW : TFT_GREEN);
    g_canvas.setCursor(10, y);
    g_canvas.print("[+] New Case");
    y += kCaseMenuRowHeightPx;

    // Existing sessions, one window's worth. Drawing the first rows regardless of where the
    // selection sits is what let it walk off the bottom of the list unseen: the highlight bar
    // below is drawn only for a row this loop reaches, so the operator was opening, renaming
    // and deleting cases that were not on the panel (ADR-0023).
    for (size_t row = 0; row < window.count; row++) {
        const size_t i = window.first + row;
        selected = (g_caseMenuSelection == static_cast<int>(i) + 1);
        if (selected) {
            g_canvas.fillRect(0, y, 240, 12, TFT_NAVY);
        }
        
        bool isCurrent = (strcmp(g_cases[i].name, g_sessionManager.currentSessionName()) == 0);
        g_canvas.setTextColor(selected ? TFT_WHITE : (isCurrent ? TFT_CYAN : TFT_LIGHTGREY));
        g_canvas.setCursor(10, y);
        
        // The precision is not cosmetic. g_cases[i] has a runtime index, so the compiler
        // bounds the %s by the whole 704-byte array rather than by the 31-byte field and
        // reports "up to 703 bytes"; stating the real bound is what makes the buffer
        // provably sufficient, and it also stops a name that lost its terminator from
        // being read past its own field (ADR-0013).
        char line[sizeof("> ") + kMaxSessionNameChars];
        snprintf(line, sizeof(line), "%s%.*s",
                 isCurrent ? "> " : "  ",
                 static_cast<int>(kMaxSessionNameChars),
                 g_cases[i].name);
        g_canvas.print(line);

        y += kCaseMenuRowHeightPx;
    }

    // Instructions at bottom -- or the refusal, in the same slot. The list stops one row
    // above this line now rather than drawing its last row over it (ADR-0023), and what is
    // left is this single row: the reason something was refused outranks a static help
    // string in that moment (ADR-0019).
    g_canvas.setCursor(10, kCaseMenuFooterTopPx);
    if (g_caseNotice[0] != '\0') {
        g_canvas.setTextColor(TFT_RED);
        g_canvas.print(g_caseNotice);
    } else {
        g_canvas.setTextColor(TFT_DARKGREY);
        g_canvas.print("O/L=Nav ENTER=Open TAB=Ren X=Del");
    }
    
    g_canvas.pushSprite(0, 0);
#endif
}

#ifndef NATIVE_BUILD
/**
 * @brief Open case @p name and give Watson that case's baseline.
 *
 * The one place a case is opened. Both menu paths -- creating a case and selecting an
 * existing one -- ran identical code after loadSession(), and what that code was missing was
 * the baseline: loadSession() switches the directory logs are written to and touches nothing
 * else, so every case shared whichever baseline happened to be in memory (ADR-0016).
 *
 * The clear happens whether or not the case turns out to have a stored baseline. A case with
 * none starts empty; leaving the previous case's entries in place is the defect itself, and
 * the path where nothing is loaded is exactly where it would come back. A failure to open the
 * case at all returns before the clear, because then the previous case is still the open one
 * and its baseline is still the right one to be holding.
 */
/**
 * @brief Put @p text on the case menu as the reason something was refused.
 *
 * fitText() rather than strncpy for the reason ADR-0013 gives showToast(): a message too
 * wide for the panel is shortened and *marked* as shortened, so the operator can tell a
 * clipped reason from a complete one.
 */
static void setCaseNotice(const char* text) {
    fitText(g_caseNotice, sizeof(g_caseNotice), text, kCaseNoticeMaxChars);
}

static void clearCaseNotice() {
    g_caseNotice[0] = '\0';
}

static void openCase(const char* name) {
    if (!g_sessionManager.loadSession(name)) {
        // Loud, because the operator picked this case and is about to be shown another
        // one's data if we carry on quietly.
        Serial.printf("[ERROR] Could not open case %s\n", name);
        // The notice, not a toast: both callers run inside the case menu's modal loop,
        // which repaints over a toast within 50 ms, so the toast this used to raise was
        // never seen (ADR-0019).
        setCaseNotice("Case load failed");
        return;
    }

    Baseline& baseline = g_watson.getBaseline();
    baseline.clear();
    // The count reported is the one that came out of *this case's* file, deliberately not
    // baseline.count(). While the log printed the running total, the total and the figure it
    // was checked against were the same number, so a baseline merged onto the previous
    // case's read exactly like one that had been replaced (ADR-0016).
    const size_t restored = g_sessionManager.loadBaseline(baseline);
    if (restored > 0) {
        Serial.printf("[BASELINE] Restored %zu SSIDs for case %s\n", restored, name);
    } else {
        Serial.printf("[BASELINE] No stored baseline for case %s\n", name);
    }

    // Badges belong to the case that raised them.
    g_hasUnseenAnomaly = false;
    g_hasUnseenFinding = false;
    g_hasUnseenHypothesis = false;
    g_inCaseMenu = false;
}

/**
 * @brief The one case-menu dispatch. Both the keyboard and the serial channel run it.
 *
 * Split from handleCaseMenuInput() for the same reason dispatchKey() was split from
 * handleKeyboard(): setup() sits in a modal loop on this handler whenever an SD card is
 * present, so without a way in that does not come from the physical keyboard, an unattended
 * gate can never get past boot (ADR-0014).
 */
/**
 * Delete the case under the selection, after the operator confirmed it. Capture cannot be
 * running: the menu is refused while it runs (ADR-0022), and nothing inside the modal menu
 * can start it, so it is not checked here. When the deleted case is the open one, its baseline goes with it: no
 * baseline outlives its case (ADR-0016), and one left in memory would be scored against and
 * saved into a case that no longer exists (ADR-0020).
 */
static void deleteSelectedCase() {
    const int sessionIdx = g_caseMenuSelection - 1;
    if (sessionIdx < 0 || sessionIdx >= static_cast<int>(g_caseCount)) {
        Serial.println("[CASE] Delete refused: no case selected");
        setCaseNotice("No case selected to delete");
        return;
    }

    char name[kMaxSessionNameChars + 1];
    copySessionName(name, sizeof(name), g_cases[sessionIdx].name);
    const bool wasOpen = g_sessionManager.hasSession() &&
                         strcmp(g_sessionManager.currentSessionName(), name) == 0;

    const SessionResult result = g_sessionManager.deleteSession(name);
    if (result == SessionResult::Ok) {
        if (wasOpen) {
            g_watson.getBaseline().clear();
        }
    } else {
        // A failed delete keeps the open case's baseline: its folder still exists, and the next
        // save rewrites whatever files the failure had already removed.
        Serial.printf("[CASE] Delete of %s refused: %s\n", name, sessionResultMessage(result));
        setCaseNotice(sessionResultMessage(result));
    }

    // Refreshed whatever the outcome, because a delete that failed partway changed the card too.
    refreshCaseList();
    // Row 0 is [+] New Case, so the last case sits at g_caseCount.
    const int lastRow = static_cast<int>(g_caseCount);
    if (g_caseMenuSelection > lastRow) {
        g_caseMenuSelection = lastRow;
    }
}

static void dispatchCaseMenuKey(const KeyEvent& ev) {
    // Text input mode handling
    if (g_inTextInput) {
        // ESC = cancel. Backspace only cancels once there is nothing left to delete.
        //
        // It used to cancel unconditionally, which made the backspace branch below
        // unreachable: `ev.del` matched here first, so a single mistyped character cost the
        // whole name and there was no way to correct one at all. That is what made keeping
        // a refused name on screen worth doing and simultaneously useless -- the operator
        // could read the name that was refused and still not edit it (ADR-0019).
        const bool cancelRequested = (ev.fn && keyEventHasChar(ev, '`'))
                                     || (ev.del && g_inputPos == 0);
        if (cancelRequested) {
            g_inTextInput = false;
            g_inputBuffer[0] = '\0';
            g_inputPos = 0;
            clearCaseNotice();
            return;
        }
        
        // Enter = confirm
        if (ev.enter) {
            if (g_inputPos == 0) {
                // The one name the character filter cannot stop reaching here, and so the
                // only reachable half of the "invalid name" this used to drop (ADR-0019).
                setCaseNotice("Name cannot be empty");
                return;
            }

            SessionResult result = SessionResult::Ok;
            if (g_renameMode) {
                const int sessionIdx = g_caseMenuSelection - 1;
                if (sessionIdx < 0 || sessionIdx >= static_cast<int>(g_caseCount)) {
                    setCaseNotice("No case selected to rename");
                    return;
                }
                // The return value used to be discarded outright, so a refused rename
                // repainted the list under the old name and read as a dead keypress.
                result = g_sessionManager.renameSession(g_cases[sessionIdx].name,
                                                       g_inputBuffer);
            } else {
                result = g_sessionManager.createSession(g_inputBuffer);
            }

            if (result != SessionResult::Ok) {
                // The input stays open with the text intact: a name refused for already
                // existing is one character away from one that works, and clearing it made
                // the operator retype it to find that out (ADR-0019).
                setCaseNotice(sessionResultMessage(result));
                return;
            }

            // Close the editor *before* acting on the success. openCase() reports a load
            // failure through the notice, and clearing the editor afterwards would wipe
            // that report the moment it was written.
            char name[kMaxSessionNameChars + 1];
            copySessionName(name, sizeof(name), g_inputBuffer);
            const bool wasRename = g_renameMode;
            clearCaseNotice();
            g_inTextInput = false;
            g_inputBuffer[0] = '\0';
            g_inputPos = 0;

            if (wasRename) {
                refreshCaseList();
            } else {
                openCase(name);
            }
            return;
        }
        
        // Backspace
        // Reachable now that cancel no longer swallows every del.
        if (ev.del && g_inputPos > 0) {
            g_inputPos--;
            g_inputBuffer[g_inputPos] = '\0';
            clearCaseNotice();
            return;
        }
        
        // Character input
        for (const char* p = ev.chars; *p != '\0'; ++p) {
            if (g_inputPos < kMaxSessionNameChars) {
                // The validator's own predicate, not a second copy of the character class.
                // The copy is what made isValidName()'s rejection unreachable from here,
                // and an unreachable rejection is one nobody notices is silent (ADR-0019).
                if (SessionManager::isValidNameChar(*p)) {
                    g_inputBuffer[g_inputPos++] = *p;
                    g_inputBuffer[g_inputPos] = '\0';
                    clearCaseNotice();
                }
            }
        }
        return;
    }
    
    // A pending delete takes this key whatever it is (ADR-0020).
    if (g_deleteArmed) {
        g_deleteArmed = false;
        clearCaseNotice();
        if (ev.enter) {
            deleteSelectedCase();
        }
        return;
    }

    // Menu navigation
    int maxItems = 1 + g_caseCount;  // New + existing sessions

    // Check for navigation keys among the event's characters (more reliable on ADV)
    bool upPressed = false;
    bool downPressed = false;
    bool deletePressed = false;
    for (const char* p = ev.chars; *p != '\0'; ++p) {
        const char c = *p;
        if (c == 'o' || c == 'O' || c == ';') upPressed = true;
        if (c == 'l' || c == 'L' || c == '.') downPressed = true;
        if (c == 'x' || c == 'X') deletePressed = true;
    }

    // O or ; = up
    if (upPressed) {
        g_caseMenuSelection--;
        if (g_caseMenuSelection < 0) g_caseMenuSelection = maxItems - 1;
    }
    // L or . = down
    else if (downPressed) {
        g_caseMenuSelection++;
        if (g_caseMenuSelection >= maxItems) g_caseMenuSelection = 0;
    }
    // X = delete (only for existing cases), confirmed by the next key
    else if (deletePressed && g_caseMenuSelection > 0
             && g_caseMenuSelection - 1 < static_cast<int>(g_caseCount)) {
        g_deleteArmed = true;
        char fittedName[kMaxSessionNameChars + 1];
        fitText(fittedName, sizeof(fittedName), g_cases[g_caseMenuSelection - 1].name,
                kDeletePromptNameChars);
        // Sized from the format, so the notice's own ceiling is the only bound that can
        // refuse the name (ADR-0013).
        char prompt[sizeof(kDeletePromptPrefix) + kMaxSessionNameChars
                    + sizeof(kDeletePromptSuffix)];
        snprintf(prompt, sizeof(prompt), "%s%s%s", kDeletePromptPrefix, fittedName,
                 kDeletePromptSuffix);
        setCaseNotice(prompt);
    }
    // Tab = rename (only for existing sessions)
    else if (ev.tab && g_caseMenuSelection > 0) {
        int sessionIdx = g_caseMenuSelection - 1;
        if (sessionIdx < (int)g_caseCount) {
            copySessionName(g_inputBuffer, sizeof(g_inputBuffer), g_cases[sessionIdx].name);
            g_inputPos = strlen(g_inputBuffer);
            g_renameMode = true;
            g_inTextInput = true;
        }
    }
    // Enter = select
    else if (ev.enter) {
        if (g_caseMenuSelection == 0) {
            // New session - show text input
            g_inputBuffer[0] = '\0';
            g_inputPos = 0;
            g_renameMode = false;
            g_inTextInput = true;
            clearCaseNotice();
        } else {
            // Load existing session
            int sessionIdx = g_caseMenuSelection - 1;
            if (sessionIdx < (int)g_caseCount) {
                openCase(g_cases[sessionIdx].name);
            }
        }
    }
    // ESC = skip session selection (use default)
    else if (ev.del) {
        g_inCaseMenu = false;
    }
}
#endif

void handleCaseMenuInput() {
#ifndef NATIVE_BUILD
    if (!M5Cardputer.Keyboard.isPressed()) return;

    Keyboard_Class::KeysState state = M5Cardputer.Keyboard.keysState();

    // Flatten the pressed characters into a bounded, NUL-terminated buffer, so the dispatch
    // takes a plain value the injected path can build too.
    char chars[kMaxKeyEventChars + 1] = {0};
    size_t count = 0;
    for (auto c : state.word) {
        if (count < kMaxKeyEventChars) chars[count++] = c;
    }
    chars[count] = '\0';

    // The fn+backtick ESC tested a raw key rather than the character list, so carry it in as
    // a character. Neither the navigation keys nor the name filter accept '`', so it cannot
    // reach anything else.
    if (count < kMaxKeyEventChars && M5Cardputer.Keyboard.isKeyPressed('`')
        && strchr(chars, '`') == nullptr) {
        chars[count++] = '`';
        chars[count] = '\0';
    }

    KeyEvent ev;
    ev.enter = state.enter;
    ev.del = state.del;
    ev.fn = state.fn;
    ev.tab = state.tab;
    ev.chars = chars;
    dispatchCaseMenuKey(ev);
#endif
}

void showTextInput(const char* title, bool isRename) {
    g_inputBuffer[0] = '\0';
    g_inputPos = 0;
    g_renameMode = isRename;
    g_inTextInput = true;
}

/**
 * @brief Set status message for footer ticker
 */
void setStatus(const char* msg) {
    if (msg == nullptr) {
        g_statusMessage[0] = '\0';
    } else {
        strncpy(g_statusMessage, msg, sizeof(g_statusMessage) - 1);
        g_statusMessage[sizeof(g_statusMessage) - 1] = '\0';
    }
    g_statusTime = millis();
}

