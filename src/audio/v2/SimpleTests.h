//
//  SimpleTests.h
//  SuperTerminal Framework - Audio Graph v2.0
//
//  Simple tests that use SuperTerminal's command queue and wait functions
//  to validate new audio system preserves existing functionality
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#pragma once

#include <string>

// Forward declarations
class SynthNode;

// Simple test results
struct SimpleTestResult {
    bool passed;
    std::string message;
    
    SimpleTestResult(bool p = false, const std::string& msg = "") : passed(p), message(msg) {}
};

// Simple compatibility tests - one function per test
class SimpleAudioV2Tests {
public:
    // Basic functionality tests
    static SimpleTestResult testAudioBufferCreation();
    static SimpleTestResult testSynthNodeCreation();
    static SimpleTestResult testSynthNodeInitialization();
    
    // Synthesis method preservation tests (one per method)
    static SimpleTestResult testBeepGeneration();
    static SimpleTestResult testExplosionGeneration();
    static SimpleTestResult testCoinGeneration();
    static SimpleTestResult testShootGeneration();
    static SimpleTestResult testClickGeneration();
    static SimpleTestResult testJumpGeneration();
    static SimpleTestResult testPowerupGeneration();
    static SimpleTestResult testHurtGeneration();
    static SimpleTestResult testPickupGeneration();
    static SimpleTestResult testBlipGeneration();
    static SimpleTestResult testZapGeneration();
    
    // Advanced synthesis tests
    static SimpleTestResult testBigExplosionGeneration();
    static SimpleTestResult testSmallExplosionGeneration();
    static SimpleTestResult testDistantExplosionGeneration();
    static SimpleTestResult testMetalExplosionGeneration();
    static SimpleTestResult testSweepUpGeneration();
    static SimpleTestResult testSweepDownGeneration();
    static SimpleTestResult testRandomBeepGeneration();
    static SimpleTestResult testOscillatorGeneration();
    
    // Audio processing tests
    static SimpleTestResult testAudioGeneration();
    static SimpleTestResult testSoundPlayback();
    static SimpleTestResult testVolumeControl();
    
    // Simple performance tests
    static SimpleTestResult testGenerationSpeed();
    static SimpleTestResult testMemoryUsage();
    
    // Integration tests with command queue
    static SimpleTestResult testCommandQueueIntegration();
    static SimpleTestResult testWaitFunctionality();
    
    // Master test runner
    static int runAllTests();  // Returns number of failed tests
    static void printTestResult(const std::string& testName, const SimpleTestResult& result);
};

// C interface for SuperTerminal integration
extern "C" {
    // Single test functions that can be called from SuperTerminal
    bool simple_test_synth_node_creation();
    bool simple_test_beep_generation();
    bool simple_test_explosion_generation();
    
    // Run all tests and return pass/fail count
    int simple_run_all_audio_v2_tests();
    
    // Quick smoke test
    bool simple_audio_v2_smoke_test();
}

// Lua bindings for testing from Lua scripts
namespace SimpleAudioV2TestsLua {
    void registerLuaFunctions(void* luaState);
    
    // Lua test functions
    int lua_test_synth_node_creation(void* L);
    int lua_test_beep_generation(void* L);
    int lua_run_all_audio_v2_tests(void* L);
    int lua_audio_v2_smoke_test(void* L);
}

// Helper functions for integration
namespace SimpleTestHelpers {
    // Create a SynthNode for testing
    SynthNode* createTestSynthNode();
    void destroyTestSynthNode(SynthNode* node);
    
    // Simple audio validation
    bool isValidSoundId(uint32_t soundId);
    bool isAudioBufferValid(const void* buffer, size_t expectedSize);
    
    // Command queue integration helpers
    void enqueueTestCommand(const std::string& testName);
    void waitForTestCompletion();
    
    // Simple logging
    void logTestStart(const std::string& testName);
    void logTestResult(const std::string& testName, bool passed, const std::string& message = "");
    void logTestSummary(int totalTests, int passedTests, int failedTests);
}

// Simple test configuration
struct SimpleTestConfig {
    bool enableLogging = true;
    bool enablePerformanceTiming = false;
    bool stopOnFirstFailure = false;
    float testTimeoutSeconds = 5.0f;
};

extern SimpleTestConfig g_simpleTestConfig;

// Initialization
bool initializeSimpleTests();
void shutdownSimpleTests();