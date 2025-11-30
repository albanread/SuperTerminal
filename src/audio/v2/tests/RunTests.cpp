//
//  RunTests.cpp
//  SuperTerminal Framework - Audio Graph v2.0
//
//  Simple test runner application to validate new audio system
//  preserves existing functionality while adding new capabilities
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "AudioSystemTests.h"
#include <iostream>
#include <string>
#include <vector>

// Simple main function to run tests
int main(int argc, char* argv[]) {
    std::cout << "🎵 SuperTerminal Audio System v2.0 Test Runner 🎵" << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << std::endl;
    
    // Parse command line arguments
    bool verbose = true;
    bool saveReport = false;
    std::string reportFile = "audio_test_results.txt";
    std::string specificSuite = "";
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--quiet" || arg == "-q") {
            verbose = false;
        } else if (arg == "--report" || arg == "-r") {
            saveReport = true;
        } else if (arg == "--suite" || arg == "-s") {
            if (i + 1 < argc) {
                specificSuite = argv[++i];
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --quiet, -q      Run tests with minimal output" << std::endl;
            std::cout << "  --report, -r     Save test report to file" << std::endl;
            std::cout << "  --suite, -s      Run specific test suite" << std::endl;
            std::cout << "  --help, -h       Show this help message" << std::endl;
            std::cout << std::endl;
            std::cout << "Available test suites:" << std::endl;
            std::cout << "  AudioBuffer      Test AudioBuffer class" << std::endl;
            std::cout << "  SynthNode        Test SynthNode wrapper" << std::endl;
            std::cout << "  Compatibility    Test backward compatibility" << std::endl;
            return 0;
        }
    }
    
    try {
        // Create test runner
        AudioSystemTestRunner runner;
        
        // Configure test runner
        runner.setStrictMode(true);              // Strict compatibility mode
        runner.setPerformanceThreshold(10.0);   // 10ms max per test
        runner.setMemoryThreshold(1024 * 1024); // 1MB max per test
        
        bool success = false;
        
        if (!specificSuite.empty()) {
            // Run specific test suite
            std::cout << "Running specific test suite: " << specificSuite << std::endl;
            std::cout << std::endl;
            success = runner.runTestSuite(specificSuite, verbose);
        } else {
            // Run all tests
            std::cout << "Running all test suites..." << std::endl;
            std::cout << std::endl;
            success = runner.runAllTests(verbose);
        }
        
        // Save report if requested
        if (saveReport) {
            std::cout << std::endl;
            runner.saveReportToFile(reportFile);
        }
        
        // Print final result
        std::cout << std::endl;
        std::cout << "========================================" << std::endl;
        
        if (success) {
            std::cout << "🎉 ALL TESTS PASSED!" << std::endl;
            std::cout << "✅ New audio system preserves existing functionality" << std::endl;
            std::cout << "✅ Ready for production use" << std::endl;
        } else {
            std::cout << "❌ SOME TESTS FAILED!" << std::endl;
            std::cout << "⚠️  Review test failures before deployment" << std::endl;
            std::cout << "📝 Check test report for details" << std::endl;
        }
        
        std::cout << "========================================" << std::endl;
        
        return success ? 0 : 1;
        
    } catch (const std::exception& e) {
        std::cerr << "💥 Test runner crashed with exception: " << e.what() << std::endl;
        return -1;
    } catch (...) {
        std::cerr << "💥 Test runner crashed with unknown exception" << std::endl;
        return -1;
    }
}

// Additional test utilities

namespace TestUtils {

// Quick smoke test - just verify basic functionality works
bool smokeTest() {
    std::cout << "🔥 Running Smoke Test..." << std::endl;
    
    try {
        // Test AudioBuffer creation
        AudioBuffer buffer(1024, 2, 44100);
        if (buffer.isEmpty()) {
            std::cerr << "❌ AudioBuffer creation failed" << std::endl;
            return false;
        }
        
        // Test SynthNode creation
        auto synthNode = std::make_shared<SynthNode>();
        if (!synthNode->initialize()) {
            std::cerr << "❌ SynthNode initialization failed" << std::endl;
            return false;
        }
        
        // Test basic synthesis
        uint32_t beepId = synthNode->generateBeepToMemory(440.0f, 0.1f);
        if (beepId == 0) {
            std::cerr << "❌ Basic synthesis failed" << std::endl;
            return false;
        }
        
        // Test audio generation
        AudioBuffer outputBuffer(512, 2, 44100);
        synthNode->generateAudio(outputBuffer);
        
        std::cout << "✅ Smoke test passed!" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Smoke test failed: " << e.what() << std::endl;
        return false;
    }
}

// Performance benchmark test
bool performanceBenchmark() {
    std::cout << "⚡ Running Performance Benchmark..." << std::endl;
    
    try {
        auto synthNode = std::make_shared<SynthNode>();
        if (!synthNode->initialize()) {
            return false;
        }
        
        const int iterations = 1000;
        const int bufferSize = 512;
        
        // Benchmark synthesis generation
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; ++i) {
            uint32_t id = synthNode->generateBeepToMemory(440.0f + i, 0.1f);
            (void)id; // Suppress unused variable warning
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto synthTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // Benchmark audio processing
        AudioBuffer buffer(bufferSize, 2, 44100);
        
        start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; ++i) {
            synthNode->generateAudio(buffer);
        }
        
        end = std::chrono::high_resolution_clock::now();
        auto audioTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // Print results
        double synthTimeMs = synthTime.count() / 1000.0;
        double audioTimeMs = audioTime.count() / 1000.0;
        
        std::cout << "📊 Benchmark Results:" << std::endl;
        std::cout << "  Synthesis: " << synthTimeMs << " ms (" 
                  << (synthTimeMs / iterations) << " ms per sound)" << std::endl;
        std::cout << "  Audio Processing: " << audioTimeMs << " ms (" 
                  << (audioTimeMs / iterations) << " ms per buffer)" << std::endl;
        
        // Performance thresholds
        bool synthGood = (synthTimeMs / iterations) < 1.0; // < 1ms per sound
        bool audioGood = (audioTimeMs / iterations) < 0.5; // < 0.5ms per buffer
        
        if (synthGood && audioGood) {
            std::cout << "✅ Performance benchmark passed!" << std::endl;
            return true;
        } else {
            std::cout << "⚠️  Performance benchmark results are concerning" << std::endl;
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Performance benchmark failed: " << e.what() << std::endl;
        return false;
    }
}

// Memory usage test
bool memoryTest() {
    std::cout << "💾 Running Memory Test..." << std::endl;
    
    try {
        auto synthNode = std::make_shared<SynthNode>();
        if (!synthNode->initialize()) {
            return false;
        }
        
        size_t initialMemory = synthNode->getMemoryUsage();
        std::cout << "  Initial memory: " << initialMemory << " bytes" << std::endl;
        
        // Generate lots of sounds
        std::vector<uint32_t> soundIds;
        for (int i = 0; i < 100; ++i) {
            uint32_t id = synthNode->generateBeepToMemory(440.0f + i * 10, 0.1f);
            soundIds.push_back(id);
        }
        
        size_t afterGeneration = synthNode->getMemoryUsage();
        std::cout << "  After generation: " << afterGeneration << " bytes" << std::endl;
        std::cout << "  Memory increase: " << (afterGeneration - initialMemory) << " bytes" << std::endl;
        
        // Run garbage collection
        synthNode->runGarbageCollection();
        
        size_t afterGC = synthNode->getMemoryUsage();
        std::cout << "  After GC: " << afterGC << " bytes" << std::endl;
        
        // Memory increase should be reasonable
        size_t memoryIncrease = afterGeneration - initialMemory;
        bool memoryGood = memoryIncrease < (10 * 1024 * 1024); // < 10MB
        
        if (memoryGood) {
            std::cout << "✅ Memory test passed!" << std::endl;
            return true;
        } else {
            std::cout << "⚠️  Memory usage is too high" << std::endl;
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Memory test failed: " << e.what() << std::endl;
        return false;
    }
}

// Stress test
bool stressTest() {
    std::cout << "💪 Running Stress Test..." << std::endl;
    
    try {
        auto synthNode = std::make_shared<SynthNode>();
        if (!synthNode->initialize()) {
            return false;
        }
        
        // Generate many sounds rapidly
        const int numSounds = 500;
        const int numOscillators = 50;
        
        std::cout << "  Generating " << numSounds << " sounds..." << std::endl;
        
        for (int i = 0; i < numSounds; ++i) {
            uint32_t id = synthNode->generateBeepToMemory(
                440.0f + (i % 100) * 10, 
                0.05f + (i % 10) * 0.01f
            );
            
            if (i % 10 == 0) {
                synthNode->playSound(id);
            }
        }
        
        std::cout << "  Creating " << numOscillators << " real-time oscillators..." << std::endl;
        
        std::vector<uint32_t> oscIds;
        for (int i = 0; i < numOscillators; ++i) {
            uint32_t oscId = synthNode->createRealTimeOscillator(
                static_cast<WaveformType>(i % 4), 
                440.0f + i * 20
            );
            oscIds.push_back(oscId);
            
            if (i % 5 == 0) {
                synthNode->triggerOscillator(oscId);
            }
        }
        
        // Process audio under load
        std::cout << "  Processing audio under load..." << std::endl;
        
        AudioBuffer buffer(512, 2, 44100);
        for (int i = 0; i < 100; ++i) {
            synthNode->generateAudio(buffer);
        }
        
        // Check system is still responsive
        size_t activeVoices = synthNode->getActiveVoiceCount();
        float cpuUsage = synthNode->getCpuUsage();
        
        std::cout << "  Active voices: " << activeVoices << std::endl;
        std::cout << "  CPU usage: " << cpuUsage << "%" << std::endl;
        
        // Cleanup
        for (uint32_t oscId : oscIds) {
            synthNode->deleteOscillator(oscId);
        }
        
        synthNode->clearAllVoices();
        synthNode->runGarbageCollection();
        
        // System should still be stable
        bool systemStable = (cpuUsage < 90.0f); // Less than 90% CPU
        
        if (systemStable) {
            std::cout << "✅ Stress test passed!" << std::endl;
            return true;
        } else {
            std::cout << "⚠️  System became unstable under stress" << std::endl;
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Stress test failed: " << e.what() << std::endl;
        return false;
    }
}

} // namespace TestUtils

// Alternative main for quick testing
int quickMain() {
    std::cout << "🚀 Quick Test Mode" << std::endl;
    std::cout << "==================" << std::endl;
    
    bool allPassed = true;
    
    allPassed &= TestUtils::smokeTest();
    std::cout << std::endl;
    
    allPassed &= TestUtils::performanceBenchmark();
    std::cout << std::endl;
    
    allPassed &= TestUtils::memoryTest();
    std::cout << std::endl;
    
    allPassed &= TestUtils::stressTest();
    std::cout << std::endl;
    
    if (allPassed) {
        std::cout << "🎉 All quick tests passed!" << std::endl;
    } else {
        std::cout << "❌ Some quick tests failed!" << std::endl;
    }
    
    return allPassed ? 0 : 1;
}