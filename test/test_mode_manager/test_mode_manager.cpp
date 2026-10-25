/**
 * @file test_mode_manager.cpp
 * @brief Unit tests for Mode Manager
 */

#include <unity.h>
#include "core/mode_manager.h"
#include "utils/time_utils.h"

using namespace dupin;

// Simple test mode
class TestMode : public IMode {
public:
    TestMode(const char* name) : m_name(name) {}
    
    const char* name() const override { return m_name; }
    void onObservation(const Observation& obs) override { 
        (void)obs;
        observationCount++; 
    }
    void update() override { updateCount++; }
    void begin() override { setActive(true); beginCount++; }
    void end() override { setActive(false); endCount++; }
    
    int observationCount = 0;
    int updateCount = 0;
    int beginCount = 0;
    int endCount = 0;
    
private:
    const char* m_name;
};

static ModeManager g_manager;
static TestMode g_watson("Watson");
static TestMode g_marple("Marple");
static TestMode g_poirot("Poirot");

void setUp(void) {
    setMockMillis(1000);
    g_manager = ModeManager();
    g_watson = TestMode("Watson");
    g_marple = TestMode("Marple");
    g_poirot = TestMode("Poirot");
}

void tearDown(void) {
    g_manager.stopAll();
}

// ============================================
// Registration
// ============================================

void test_manager_registers_modes(void) {
    TEST_ASSERT_TRUE(g_manager.registerMode(&g_watson));
    TEST_ASSERT_TRUE(g_manager.registerMode(&g_marple));
    
    TEST_ASSERT_EQUAL(2, g_manager.getModeCount());
}

void test_manager_rejects_duplicate_registration(void) {
    g_manager.registerMode(&g_watson);
    
    bool result = g_manager.registerMode(&g_watson);
    
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL(1, g_manager.getModeCount());
}

void test_manager_finds_mode_by_name(void) {
    g_manager.registerMode(&g_watson);
    g_manager.registerMode(&g_marple);
    
    IMode* found = g_manager.getMode("Marple");
    
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("Marple", found->name());
}

void test_manager_returns_null_for_unknown_mode(void) {
    g_manager.registerMode(&g_watson);
    
    IMode* found = g_manager.getMode("Holmes");
    
    TEST_ASSERT_NULL(found);
}

// ============================================
// Mode Lifecycle
// ============================================

void test_manager_starts_mode(void) {
    g_manager.registerMode(&g_watson);
    
    bool result = g_manager.startMode("Watson");
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_watson.isActive());
    TEST_ASSERT_EQUAL(1, g_watson.beginCount);
}

void test_manager_stops_mode(void) {
    g_manager.registerMode(&g_watson);
    g_manager.startMode("Watson");
    
    g_manager.stopMode("Watson");
    
    TEST_ASSERT_FALSE(g_watson.isActive());
    TEST_ASSERT_EQUAL(1, g_watson.endCount);
}

void test_manager_starts_all(void) {
    g_manager.registerMode(&g_watson);
    g_manager.registerMode(&g_marple);
    g_manager.registerMode(&g_poirot);
    
    g_manager.startAll();
    
    TEST_ASSERT_EQUAL(3, g_manager.getActiveModeCount());
}

void test_manager_stops_all(void) {
    g_manager.registerMode(&g_watson);
    g_manager.registerMode(&g_marple);
    g_manager.startAll();
    
    g_manager.stopAll();
    
    TEST_ASSERT_EQUAL(0, g_manager.getActiveModeCount());
}

// ============================================
// Active Mode
// ============================================

void test_manager_sets_active_mode(void) {
    g_manager.registerMode(&g_watson);
    g_manager.registerMode(&g_marple);
    
    bool result = g_manager.setActiveMode("Marple");
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(&g_marple, g_manager.getActiveMode());
    TEST_ASSERT_TRUE(g_marple.isActive());  // Also starts the mode
}

// ============================================
// Observation Broadcasting
// ============================================

void test_manager_broadcasts_to_active_modes(void) {
    g_manager.registerMode(&g_watson);
    g_manager.registerMode(&g_marple);
    g_manager.startMode("Watson");
    // Marple not started
    
    Observation obs;
    obs.frameType = FrameType::Beacon;
    
    g_manager.broadcastObservation(obs);
    
    TEST_ASSERT_EQUAL(1, g_watson.observationCount);
    TEST_ASSERT_EQUAL(0, g_marple.observationCount);  // Not active
}

void test_manager_broadcasts_to_all_active(void) {
    g_manager.registerMode(&g_watson);
    g_manager.registerMode(&g_marple);
    g_manager.startAll();
    
    Observation obs;
    obs.frameType = FrameType::Beacon;
    
    g_manager.broadcastObservation(obs);
    
    TEST_ASSERT_EQUAL(1, g_watson.observationCount);
    TEST_ASSERT_EQUAL(1, g_marple.observationCount);
}

// ============================================
// Update
// ============================================

void test_manager_updates_active_modes(void) {
    g_manager.registerMode(&g_watson);
    g_manager.registerMode(&g_marple);
    g_manager.startMode("Watson");
    // Marple not started
    
    ModeManagerConfig config = g_manager.getConfig();
    config.updateIntervalMs = 0;  // No rate limiting
    g_manager.setConfig(config);
    
    advanceMockMillis(100);
    g_manager.update();
    
    TEST_ASSERT_EQUAL(1, g_watson.updateCount);
    TEST_ASSERT_EQUAL(0, g_marple.updateCount);  // Not active
}

// ============================================
// Test Runner
// ============================================

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Registration
    RUN_TEST(test_manager_registers_modes);
    RUN_TEST(test_manager_rejects_duplicate_registration);
    RUN_TEST(test_manager_finds_mode_by_name);
    RUN_TEST(test_manager_returns_null_for_unknown_mode);
    
    // Lifecycle
    RUN_TEST(test_manager_starts_mode);
    RUN_TEST(test_manager_stops_mode);
    RUN_TEST(test_manager_starts_all);
    RUN_TEST(test_manager_stops_all);
    
    // Active mode
    RUN_TEST(test_manager_sets_active_mode);
    
    // Broadcasting
    RUN_TEST(test_manager_broadcasts_to_active_modes);
    RUN_TEST(test_manager_broadcasts_to_all_active);
    
    // Update
    RUN_TEST(test_manager_updates_active_modes);
    
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_manager_registers_modes);
    RUN_TEST(test_manager_rejects_duplicate_registration);
    RUN_TEST(test_manager_finds_mode_by_name);
    RUN_TEST(test_manager_returns_null_for_unknown_mode);
    RUN_TEST(test_manager_starts_mode);
    RUN_TEST(test_manager_stops_mode);
    RUN_TEST(test_manager_starts_all);
    RUN_TEST(test_manager_stops_all);
    RUN_TEST(test_manager_sets_active_mode);
    RUN_TEST(test_manager_broadcasts_to_active_modes);
    RUN_TEST(test_manager_broadcasts_to_all_active);
    RUN_TEST(test_manager_updates_active_modes);
    UNITY_END();
}
void loop() {}
#endif
