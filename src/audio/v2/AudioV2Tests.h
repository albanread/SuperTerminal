//
//  AudioV2Tests.h
//  SuperTerminal Framework - Audio Graph v2.0
//
//  Simple integrated test system that runs as SuperTerminal callbacks
//  on background threads to validate new audio system preserves existing functionality
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

// Forward declarations
class SynthEngine;
class SynthNode;
class AudioBuffer;

// Simple test result
struct AudioV2TestResult {
    std::string testName;
    bool passed;
    std::string message;
    double timeMs;
    
    AudioV2TestResult(const std::string& name, bool success, const std::string& msg = "", double time = 0.0)
        : testName(name), passed(success), message(msg), timeMs(time) {}
};

// Simple test callback signature
using AudioV2TestCallback = std::function<void(const std::vector<AudioV2TestResult>&)>;

// Main test runner that integrates with SuperTerminal's background thread system
class AudioV2TestRunner {
public:
    AudioV2TestRunner();
    ~AudioV2TestRunner();
    
    // Run compatibility tests as background callback
    // This will be called from SuperTerminal's background thread
    void runCompatibilityTests(AudioV2TestCallback callback);
    
    // Quick smoke test (can be called from any thread)
    bool quickSmokeTest();
    
    // Test specific synthesis method compatibility
    bool testSynthMethodCompatibility(const std::string& methodName);
    
    // Performance test
    void runPerformanceTest(AudioV2TestCallback callback);
    
    // Get last test results
    const std::vector<AudioV2TestResult>& getLastResults() const { return lastResults; }
    
    // Simple status check
    bool isNewSystemWorking() const;
    
private:
    std::vector<AudioV2TestResult> lastResults;
    
    // Individual test methods (called from background thread)
    AudioV2TestResult testAudioBufferBasics();
    AudioV2TestResult testSynthNodeCreation();
    AudioV2TestResult testBeepGeneration();
    AudioV2TestResult testExplosionGeneration();
    AudioV2TestResult testAllSynthMethods();
    AudioV2TestResult testPerformance();
    AudioV2TestResult testMemoryUsage();
    
    // Helper methods
    void logTestResult(const AudioV2TestResult& result);
    double getCurrentTimeMs() const;
};

// Global functions for easy integration with existing SuperTerminal code

// Register test callbacks with SuperTerminal's background thread system
void registerAudioV2Tests();

// Quick validation that can be called from Lua or main thread
bool audioV2QuickCheck();

// Get human-readable status
std::string getAudioV2Status();

// Callback function signatures for SuperTerminal integration
extern "C" {
    // These can be called from SuperTerminal's command queue system
    void audioV2_run_compatibility_tests();
    void audioV2_run_performance_tests();
    bool audioV2_quick_smoke_test();
    const char* audioV2_get_status();
}

// Simple macro for adding tests to SuperTerminal's background processing
#define REGISTER_AUDIO_V2_TEST(name) \
    do { \
        /* This would integrate with SuperTerminal's existing callback system */ \
        /* registerBackgroundCallback(name, audioV2_##name); */ \
    } while(0)

// Integration with existing Lua system
namespace AudioV2Lua {
    // Register Lua functions for testing
    void registerLuaTestFunctions(void* luaState);
    
    // Lua callable test functions
    int lua_audio_v2_run_tests(void* L);
    int lua_audio_v2_quick_test(void* L);
    int lua_audio_v2_get_status(void* L);
}

// Simple test data structures for validation
struct AudioV2TestData {
    // Test synthesis parameters
    struct {
        float frequency = 440.0f;
        float duration = 0.5f;
        float intensity = 1.0f;
        float pitch = 1.0f;
    } synthParams;
    
    // Expected results (for regression testing)
    struct {
        size_t expectedSampleCount = 0;
        float expectedPeakLevel = 0.0f;
        bool shouldNotBeEmpty = true;
    } expected;
    
    // Test configuration
    bool enableVerboseLogging = false;
    bool enablePerformanceTiming = true;
    size_t maxTestTimeMs = 5000; // 5 second timeout per test
};

// Utility class for comparing old vs new synthesis results
class SynthCompatibilityChecker {
public:
    // Compare synthesis methods between old SynthEngine and new SynthNode
    static bool compareBeepGeneration(SynthEngine* oldEngine, SynthNode* newNode, 
                                    float frequency, float duration, float tolerance = 0.1f);
    
    static bool compareExplosionGeneration(SynthEngine* oldEngine, SynthNode* newNode,
                                         float size, float duration, float tolerance = 0.1f);
    
    // Generic method comparison
    static bool compareSynthMethod(const std::string& methodName,
                                 SynthEngine* oldEngine, SynthNode* newNode,
                                 const std::vector<float>& params,
                                 float tolerance = 0.1f);
    
    // Check if audio outputs are "similar enough"
    static bool audioBuffersAreSimilar(const void* buffer1, const void* buffer2, 
                                     size_t sampleCount, float tolerance);
    
private:
    static double calculateBufferDifference(const float* samples1, const float* samples2, size_t count);
};

// Simple progress callback for long-running tests
using AudioV2ProgressCallback = std::function<void(const std::string& status, float progress)>;

// Extended test runner for more comprehensive validation
class AudioV2ExtendedTests {
public:
    // Run comprehensive test suite with progress reporting
    void runFullTestSuite(AudioV2TestCallback resultCallback, 
                         AudioV2ProgressCallback progressCallback = nullptr);
    
    // Stress testing
    void runStressTests(AudioV2TestCallback resultCallback);
    
    // Memory leak detection
    void runMemoryLeakTests(AudioV2TestCallback resultCallback);
    
    // Thread safety tests
    void runThreadSafetyTests(AudioV2TestCallback resultCallback);
    
private:
    void reportProgress(const std::string& status, float progress, 
                       AudioV2ProgressCallback callback);
};

// Configuration for test behavior
struct AudioV2TestConfig {
    bool enableCompatibilityTests = true;
    bool enablePerformanceTests = true;
    bool enableMemoryTests = true;
    bool enableStressTests = false; // Disabled by default
    
    // Timing
    size_t maxTestDurationMs = 10000;
    size_t performanceIterations = 100;
    
    // Tolerances
    float audioSimilarityTolerance = 0.1f;
    float performanceRegressionTolerance = 2.0f; // 2x slower is acceptable
    
    // Output
    bool verboseLogging = false;
    bool saveResultsToFile = false;
    std::string resultsFilePath = "audio_v2_test_results.txt";
};

// Global test configuration
extern AudioV2TestConfig g_audioV2TestConfig;

// Simple initialization function
bool initializeAudioV2Tests();
void shutdownAudioV2Tests();

// Integration point with SuperTerminal's main loop
void audioV2TestsTick(); // Call this periodically from main thread if needed