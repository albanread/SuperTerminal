//
//  AudioSystemTests.h
//  SuperTerminal Framework - Audio Graph v2.0
//
//  Comprehensive test framework for validating new audio system
//  preserves existing functionality while adding new capabilities
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#pragma once

#include "../AudioBuffer.h"
#include "../AudioNode.h"
#include "../SynthNode.h"
#include "../../SynthEngine.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <cassert>

// Test result structure
struct TestResult {
    std::string testName;
    bool passed;
    std::string errorMessage;
    double executionTimeMs;
    
    TestResult(const std::string& name, bool success, const std::string& error = "", double timeMs = 0.0)
        : testName(name), passed(success), errorMessage(error), executionTimeMs(timeMs) {}
};

// Test suite base class
class TestSuite {
public:
    virtual ~TestSuite() = default;
    virtual std::vector<TestResult> runAllTests() = 0;
    virtual std::string getSuiteName() const = 0;
    
protected:
    TestResult runTest(const std::string& testName, std::function<void()> testFunction);
    void assertTrue(bool condition, const std::string& message = "Assertion failed");
    void assertFalse(bool condition, const std::string& message = "Assertion should be false");
    void assertEqual(float a, float b, float tolerance = 0.0001f, const std::string& message = "Values not equal");
    void assertNotNull(void* ptr, const std::string& message = "Pointer should not be null");
    void assertBuffersMatch(const AudioBuffer& a, const AudioBuffer& b, float tolerance = 0.0001f);
    void assertSynthBuffersMatch(const SynthAudioBuffer& a, const SynthAudioBuffer& b, float tolerance = 0.0001f);
};

// === AUDIO BUFFER TESTS ===

class AudioBufferTestSuite : public TestSuite {
public:
    std::vector<TestResult> runAllTests() override;
    std::string getSuiteName() const override { return "AudioBuffer"; }
    
private:
    void testConstructors();
    void testResize();
    void testSampleAccess();
    void testMixOperations();
    void testCopyOperations();
    void testGainOperations();
    void testPanOperations();
    void testAnalysisOperations();
    void testFormatConversion();
    void testSynthEngineCompatibility();
    void testUtilityFunctions();
    void testPerformance();
};

// === AUDIO NODE TESTS ===

class AudioNodeTestSuite : public TestSuite {
public:
    std::vector<TestResult> runAllTests() override;
    std::string getSuiteName() const override { return "AudioNode"; }
    
private:
    void testNodeConnections();
    void testParameterHandling();
    void testMixerNode();
    void testOutputNode();
    void testNodeUtilities();
    void testThreadSafety();
};

// === SYNTH NODE TESTS (Critical for backward compatibility) ===

class SynthNodeTestSuite : public TestSuite {
public:
    std::vector<TestResult> runAllTests() override;
    std::string getSuiteName() const override { return "SynthNode"; }
    
private:
    // Test all existing synthesis methods are preserved
    void testBeepGeneration();
    void testExplosionGeneration();
    void testCoinGeneration();
    void testShootGeneration();
    void testClickGeneration();
    void testJumpGeneration();
    void testPowerupGeneration();
    void testHurtGeneration();
    void testPickupGeneration();
    void testBlipGeneration();
    void testZapGeneration();
    
    // Test advanced synthesis methods
    void testAdvancedExplosions();
    void testSweepEffects();
    void testOscillatorGeneration();
    void testRandomGeneration();
    void testCustomSounds();
    
    // Test new real-time capabilities
    void testRealTimeOscillators();
    void testVoiceManagement();
    void testGlobalEffects();
    void testResourceManagement();
    
    // Performance and compatibility tests
    void testMemoryUsage();
    void testCpuUsage();
    void testAudioIntegration();
};

// === COMPATIBILITY TESTS (Most Important) ===

class CompatibilityTestSuite : public TestSuite {
public:
    std::vector<TestResult> runAllTests() override;
    std::string getSuiteName() const override { return "Compatibility"; }
    
private:
    void testSynthEnginePreservation();
    void testIdenticalOutputs();
    void testAllSynthMethods();
    void testParameterCompatibility();
    void testPerformanceParity();
    void testMemoryFootprint();
    void testLuaAPICompatibility();
};

// === INTEGRATION TESTS ===

class IntegrationTestSuite : public TestSuite {
public:
    std::vector<TestResult> runAllTests() override;
    std::string getSuiteName() const override { return "Integration"; }
    
private:
    void testAudioGraph();
    void testMIDIIntegration();
    void testComplexScenarios();
    void testResourceSharing();
    void testThreadSafety();
    void testErrorHandling();
};

// === PERFORMANCE TESTS ===

class PerformanceTestSuite : public TestSuite {
public:
    std::vector<TestResult> runAllTests() override;
    std::string getSuiteName() const override { return "Performance"; }
    
private:
    void testAudioBufferPerformance();
    void testSynthesisPerformance();
    void testMemoryPerformance();
    void testRealTimePerformance();
    void testScalabilityTests();
    void testRegressionTests();
};

// === MAIN TEST RUNNER ===

class AudioSystemTestRunner {
public:
    AudioSystemTestRunner();
    ~AudioSystemTestRunner();
    
    // Run all test suites
    bool runAllTests(bool verbose = true);
    
    // Run specific test suite
    bool runTestSuite(const std::string& suiteName, bool verbose = true);
    
    // Get test results
    std::vector<TestResult> getLastResults() const { return lastResults; }
    
    // Generate test report
    std::string generateReport() const;
    void saveReportToFile(const std::string& filename) const;
    
    // Test configuration
    void setStrictMode(bool strict) { strictMode = strict; }
    void setPerformanceThreshold(double thresholdMs) { performanceThreshold = thresholdMs; }
    void setMemoryThreshold(size_t thresholdBytes) { memoryThreshold = thresholdBytes; }
    
private:
    std::vector<std::unique_ptr<TestSuite>> testSuites;
    std::vector<TestResult> lastResults;
    bool strictMode = false;
    double performanceThreshold = 10.0; // 10ms max per test
    size_t memoryThreshold = 1024 * 1024; // 1MB max per test
    
    void printTestResult(const TestResult& result, bool verbose) const;
    void printSummary(const std::vector<TestResult>& results) const;
};

// === COMPATIBILITY VALIDATION HELPERS ===

class SynthEngineComparator {
public:
    static bool compareEngines(SynthEngine* oldEngine, SynthNode* newNode);
    static bool compareBeepGeneration(SynthEngine* oldEngine, SynthNode* newNode, float frequency, float duration);
    static bool compareExplosionGeneration(SynthEngine* oldEngine, SynthNode* newNode, float size, float duration);
    static bool compareAllMethods(SynthEngine* oldEngine, SynthNode* newNode);
    
private:
    static bool compareAudioBuffers(const SynthAudioBuffer* oldBuffer, const SynthAudioBuffer* newBuffer, float tolerance = 0.001f);
    static double calculateBufferDifference(const SynthAudioBuffer* a, const SynthAudioBuffer* b);
};

// === PERFORMANCE BENCHMARKING ===

class PerformanceBenchmark {
public:
    struct BenchmarkResult {
        std::string testName;
        double avgTimeMs;
        double minTimeMs;
        double maxTimeMs;
        size_t iterations;
        size_t memoryUsed;
        double cpuUsage;
    };
    
    static BenchmarkResult benchmarkSynthesis(SynthNode* synthNode, int iterations = 1000);
    static BenchmarkResult benchmarkAudioProcessing(AudioBuffer& buffer, int iterations = 1000);
    static BenchmarkResult benchmarkNodeProcessing(AudioNode* node, int iterations = 1000);
    
    static void printBenchmarkResult(const BenchmarkResult& result);
    static bool compareBenchmarks(const BenchmarkResult& oldResult, const BenchmarkResult& newResult, double tolerancePercent = 10.0);
};

// === STRESS TESTING ===

class StressTestSuite {
public:
    struct StressResult {
        bool passed;
        size_t maxVoices;
        double maxCpuUsage;
        size_t maxMemoryUsage;
        std::string errorMessage;
    };
    
    static StressResult stressTestVoices(SynthNode* synthNode, size_t maxVoices = 1000);
    static StressResult stressTestMemory(SynthNode* synthNode, size_t targetMemoryMB = 100);
    static StressResult stressTestContinuousPlayback(SynthNode* synthNode, double durationSeconds = 60.0);
    static StressResult stressTestRapidGeneration(SynthNode* synthNode, size_t soundsPerSecond = 100);
};

// === REGRESSION TESTING ===

class RegressionTestSuite {
public:
    // Test against known good outputs
    static bool testAgainstGoldenFiles(SynthNode* synthNode, const std::string& goldenDataPath);
    
    // Test that specific bugs don't reoccur
    static bool testKnownIssues(SynthNode* synthNode);
    
    // Test edge cases
    static bool testEdgeCases(SynthNode* synthNode);
    
    // Test error conditions
    static bool testErrorConditions(SynthNode* synthNode);
};

// === UTILITY MACROS FOR TESTING ===

#define EXPECT_TRUE(condition) assertTrue(condition, "Expected true: " #condition)
#define EXPECT_FALSE(condition) assertFalse(condition, "Expected false: " #condition)
#define EXPECT_EQ(a, b) assertEqual(a, b, 0.0001f, "Expected equal: " #a " == " #b)
#define EXPECT_NEAR(a, b, tolerance) assertEqual(a, b, tolerance, "Expected near: " #a " ~= " #b)
#define EXPECT_NOT_NULL(ptr) assertNotNull(ptr, "Expected not null: " #ptr)

// Timing macros
#define TIME_BLOCK(name, block) \
    do { \
        auto start = std::chrono::high_resolution_clock::now(); \
        block; \
        auto end = std::chrono::high_resolution_clock::now(); \
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start); \
        std::cout << "Timing [" << name << "]: " << duration.count() << " μs" << std::endl; \
    } while(0)

// Memory tracking macros (simplified)
#define TRACK_MEMORY(name, block) \
    do { \
        size_t memBefore = getCurrentMemoryUsage(); \
        block; \
        size_t memAfter = getCurrentMemoryUsage(); \
        std::cout << "Memory [" << name << "]: " << (memAfter - memBefore) << " bytes" << std::endl; \
    } while(0)

// === GLOBAL TEST FUNCTIONS ===

// Quick test to verify basic functionality
bool quickCompatibilityTest();

// Comprehensive validation of all features
bool fullValidationTest(bool generateReport = true);

// Performance regression test
bool performanceRegressionTest(const std::string& baselineFile = "");

// Memory leak detection test
bool memoryLeakTest();

// Thread safety test
bool threadSafetyTest();

// Utility functions
size_t getCurrentMemoryUsage();
double getCurrentCpuUsage();
void generateTestAudioFile(const std::string& filename, const AudioBuffer& buffer);
AudioBuffer loadTestAudioFile(const std::string& filename);

// Test data generators
AudioBuffer generateTestTone(float frequency, float duration, float sampleRate = 44100.0f);
AudioBuffer generateTestNoise(float duration, float sampleRate = 44100.0f);
AudioBuffer generateTestSilence(float duration, float sampleRate = 44100.0f);
std::vector<float> generateTestFrequencies();
std::vector<float> generateTestDurations();

// Validation helpers
bool validateAudioBuffer(const AudioBuffer& buffer);
bool validateSynthAudioBuffer(const SynthAudioBuffer& buffer);
bool validateAudioIntegrity(const AudioBuffer& buffer);
std::string getAudioBufferChecksum(const AudioBuffer& buffer);

// === EXAMPLE USAGE ===

/*
// Basic usage example:
int main() {
    AudioSystemTestRunner runner;
    
    // Run all tests
    bool success = runner.runAllTests(true);
    
    // Generate report
    std::string report = runner.generateReport();
    runner.saveReportToFile("test_results.txt");
    
    return success ? 0 : 1;
}

// Specific test example:
void testNewSynthNodePreservesOldBehavior() {
    // Create old SynthEngine
    auto oldEngine = std::make_unique<SynthEngine>();
    oldEngine->initialize();
    
    // Create new SynthNode
    auto newNode = std::make_shared<SynthNode>();
    newNode->initialize();
    
    // Test that they produce identical output
    auto oldBeep = oldEngine->generateBeep(440.0f, 0.5f);
    uint32_t newBeepId = newNode->generateBeepToMemory(440.0f, 0.5f);
    
    // Validate they're identical
    assertTrue(SynthEngineComparator::compareBeepGeneration(
        oldEngine.get(), newNode.get(), 440.0f, 0.5f
    ));
}
*/