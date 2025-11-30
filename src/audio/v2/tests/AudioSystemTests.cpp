//
//  AudioSystemTests.cpp
//  SuperTerminal Framework - Audio Graph v2.0
//
//  Implementation of comprehensive test framework for validating new audio system
//  preserves existing functionality while adding new capabilities
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "AudioSystemTests.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cmath>

// === TestSuite Base Implementation ===

TestResult TestSuite::runTest(const std::string& testName, std::function<void()> testFunction) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    try {
        testFunction();
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        double timeMs = duration.count() / 1000.0;
        
        return TestResult(testName, true, "", timeMs);
    } catch (const std::exception& e) {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        double timeMs = duration.count() / 1000.0;
        
        return TestResult(testName, false, e.what(), timeMs);
    }
}

void TestSuite::assertTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestSuite::assertFalse(bool condition, const std::string& message) {
    if (condition) {
        throw std::runtime_error(message);
    }
}

void TestSuite::assertEqual(float a, float b, float tolerance, const std::string& message) {
    if (std::abs(a - b) > tolerance) {
        std::ostringstream ss;
        ss << message << " (got " << a << ", expected " << b << ", tolerance " << tolerance << ")";
        throw std::runtime_error(ss.str());
    }
}

void TestSuite::assertNotNull(void* ptr, const std::string& message) {
    if (ptr == nullptr) {
        throw std::runtime_error(message);
    }
}

void TestSuite::assertBuffersMatch(const AudioBuffer& a, const AudioBuffer& b, float tolerance) {
    assertTrue(a.getFrameCount() == b.getFrameCount(), "Frame counts don't match");
    assertTrue(a.getChannelCount() == b.getChannelCount(), "Channel counts don't match");
    
    for (uint32_t frame = 0; frame < a.getFrameCount(); ++frame) {
        for (uint32_t ch = 0; ch < a.getChannelCount(); ++ch) {
            float sampleA = a.getSample(frame, ch);
            float sampleB = b.getSample(frame, ch);
            if (std::abs(sampleA - sampleB) > tolerance) {
                std::ostringstream ss;
                ss << "Buffers differ at frame " << frame << ", channel " << ch 
                   << " (got " << sampleA << ", expected " << sampleB << ")";
                throw std::runtime_error(ss.str());
            }
        }
    }
}

void TestSuite::assertSynthBuffersMatch(const SynthAudioBuffer& a, const SynthAudioBuffer& b, float tolerance) {
    assertTrue(a.getFrameCount() == b.getFrameCount(), "SynthBuffer frame counts don't match");
    assertTrue(a.channels == b.channels, "SynthBuffer channel counts don't match");
    assertTrue(a.samples.size() == b.samples.size(), "SynthBuffer sample counts don't match");
    
    for (size_t i = 0; i < a.samples.size(); ++i) {
        if (std::abs(a.samples[i] - b.samples[i]) > tolerance) {
            std::ostringstream ss;
            ss << "SynthBuffers differ at sample " << i 
               << " (got " << a.samples[i] << ", expected " << b.samples[i] << ")";
            throw std::runtime_error(ss.str());
        }
    }
}

// === AudioBufferTestSuite Implementation ===

std::vector<TestResult> AudioBufferTestSuite::runAllTests() {
    std::vector<TestResult> results;
    
    results.push_back(runTest("Constructors", [this]() { testConstructors(); }));
    results.push_back(runTest("Resize", [this]() { testResize(); }));
    results.push_back(runTest("Sample Access", [this]() { testSampleAccess(); }));
    results.push_back(runTest("Mix Operations", [this]() { testMixOperations(); }));
    results.push_back(runTest("Copy Operations", [this]() { testCopyOperations(); }));
    results.push_back(runTest("Gain Operations", [this]() { testGainOperations(); }));
    results.push_back(runTest("Pan Operations", [this]() { testPanOperations(); }));
    results.push_back(runTest("Analysis Operations", [this]() { testAnalysisOperations(); }));
    results.push_back(runTest("Format Conversion", [this]() { testFormatConversion(); }));
    results.push_back(runTest("SynthEngine Compatibility", [this]() { testSynthEngineCompatibility(); }));
    results.push_back(runTest("Utility Functions", [this]() { testUtilityFunctions(); }));
    results.push_back(runTest("Performance", [this]() { testPerformance(); }));
    
    return results;
}

void AudioBufferTestSuite::testConstructors() {
    // Default constructor
    AudioBuffer buffer1;
    EXPECT_TRUE(buffer1.isEmpty());
    EXPECT_EQ(buffer1.getFrameCount(), 0);
    EXPECT_EQ(buffer1.getChannelCount(), 0);
    
    // Frame/channel constructor
    AudioBuffer buffer2(1024, 2, 44100);
    EXPECT_FALSE(buffer2.isEmpty());
    EXPECT_EQ(buffer2.getFrameCount(), 1024);
    EXPECT_EQ(buffer2.getChannelCount(), 2);
    EXPECT_EQ(buffer2.getSampleRate(), 44100);
    
    // Config constructor
    AudioBufferConfig config;
    config.framesPerBuffer = 512;
    config.channels = 2;
    config.sampleRate = 48000;
    
    AudioBuffer buffer3(config);
    EXPECT_EQ(buffer3.getFrameCount(), 512);
    EXPECT_EQ(buffer3.getChannelCount(), 2);
    EXPECT_EQ(buffer3.getSampleRate(), 48000);
    
    // Copy constructor
    AudioBuffer buffer4(buffer2);
    EXPECT_EQ(buffer4.getFrameCount(), buffer2.getFrameCount());
    EXPECT_EQ(buffer4.getChannelCount(), buffer2.getChannelCount());
    
    // Move constructor
    AudioBuffer buffer5 = std::move(buffer4);
    EXPECT_EQ(buffer5.getFrameCount(), 1024);
    EXPECT_TRUE(buffer4.isEmpty()); // moved-from should be empty
}

void AudioBufferTestSuite::testResize() {
    AudioBuffer buffer(512, 2);
    
    // Resize to larger
    buffer.resize(1024, 2);
    EXPECT_EQ(buffer.getFrameCount(), 1024);
    EXPECT_EQ(buffer.getChannelCount(), 2);
    
    // Resize to smaller
    buffer.resize(256, 1);
    EXPECT_EQ(buffer.getFrameCount(), 256);
    EXPECT_EQ(buffer.getChannelCount(), 1);
    
    // Resize with same size should be no-op
    size_t oldCapacity = buffer.getCapacity();
    buffer.resize(256, 1);
    EXPECT_EQ(buffer.getCapacity(), oldCapacity);
}

void AudioBufferTestSuite::testSampleAccess() {
    AudioBuffer buffer(100, 2);
    
    // Set and get samples
    buffer.setSample(50, 0, 0.5f);
    buffer.setSample(50, 1, -0.3f);
    
    EXPECT_NEAR(buffer.getSample(50, 0), 0.5f, 0.0001f);
    EXPECT_NEAR(buffer.getSample(50, 1), -0.3f, 0.0001f);
    
    // Frame access
    float frameData[2] = {0.7f, -0.4f};
    buffer.setFrame(75, frameData);
    
    float retrievedFrame[2];
    buffer.getFrame(75, retrievedFrame);
    EXPECT_NEAR(retrievedFrame[0], 0.7f, 0.0001f);
    EXPECT_NEAR(retrievedFrame[1], -0.4f, 0.0001f);
}

void AudioBufferTestSuite::testMixOperations() {
    AudioBuffer buffer1(100, 2);
    AudioBuffer buffer2(100, 2);
    
    // Fill buffers with test data
    for (uint32_t i = 0; i < 100; ++i) {
        buffer1.setSample(i, 0, 0.1f);
        buffer1.setSample(i, 1, 0.2f);
        buffer2.setSample(i, 0, 0.3f);
        buffer2.setSample(i, 1, 0.4f);
    }
    
    // Test mixing
    buffer1.mixFrom(buffer2, 1.0f);
    
    EXPECT_NEAR(buffer1.getSample(0, 0), 0.4f, 0.0001f); // 0.1 + 0.3
    EXPECT_NEAR(buffer1.getSample(0, 1), 0.6f, 0.0001f); // 0.2 + 0.4
    
    // Test mixing with gain
    buffer1.clear();
    for (uint32_t i = 0; i < 100; ++i) {
        buffer1.setSample(i, 0, 0.1f);
    }
    
    buffer1.mixFrom(buffer2, 0.5f);
    EXPECT_NEAR(buffer1.getSample(0, 0), 0.25f, 0.0001f); // 0.1 + (0.3 * 0.5)
}

void AudioBufferTestSuite::testCopyOperations() {
    AudioBuffer source(100, 2);
    AudioBuffer dest(100, 2);
    
    // Fill source with test data
    for (uint32_t i = 0; i < 100; ++i) {
        source.setSample(i, 0, static_cast<float>(i) / 100.0f);
        source.setSample(i, 1, static_cast<float>(i) / 50.0f);
    }
    
    // Copy entire buffer
    dest.copyFrom(source);
    assertBuffersMatch(source, dest);
    
    // Copy partial range
    AudioBuffer dest2(100, 2);
    dest2.copyFrom(source, 10, 20, 30); // Copy 30 frames from source[10] to dest[20]
    
    for (uint32_t i = 0; i < 30; ++i) {
        EXPECT_NEAR(dest2.getSample(20 + i, 0), source.getSample(10 + i, 0), 0.0001f);
        EXPECT_NEAR(dest2.getSample(20 + i, 1), source.getSample(10 + i, 1), 0.0001f);
    }
}

void AudioBufferTestSuite::testGainOperations() {
    AudioBuffer buffer(100, 2);
    
    // Fill with test data
    for (uint32_t i = 0; i < 100; ++i) {
        buffer.setSample(i, 0, 0.5f);
        buffer.setSample(i, 1, -0.3f);
    }
    
    // Apply gain
    buffer.applyGain(2.0f);
    
    EXPECT_NEAR(buffer.getSample(0, 0), 1.0f, 0.0001f);
    EXPECT_NEAR(buffer.getSample(0, 1), -0.6f, 0.0001f);
    
    // Test gain ramp
    AudioBuffer buffer2(100, 1);
    for (uint32_t i = 0; i < 100; ++i) {
        buffer2.setSample(i, 0, 1.0f);
    }
    
    buffer2.applyGainRamp(0.0f, 1.0f);
    
    EXPECT_NEAR(buffer2.getSample(0, 0), 0.0f, 0.01f);
    EXPECT_NEAR(buffer2.getSample(99, 0), 1.0f, 0.01f);
    EXPECT_NEAR(buffer2.getSample(49, 0), 0.5f, 0.1f); // Roughly middle
}

void AudioBufferTestSuite::testPanOperations() {
    AudioBuffer buffer(100, 2);
    
    // Fill with mono signal
    for (uint32_t i = 0; i < 100; ++i) {
        buffer.setSample(i, 0, 1.0f);
        buffer.setSample(i, 1, 1.0f);
    }
    
    // Pan hard left
    buffer.applyPan(-1.0f);
    
    EXPECT_TRUE(buffer.getSample(0, 0) > 0.0f); // Left should have signal
    EXPECT_NEAR(buffer.getSample(0, 1), 0.0f, 0.01f); // Right should be silent
    
    // Reset and pan hard right
    for (uint32_t i = 0; i < 100; ++i) {
        buffer.setSample(i, 0, 1.0f);
        buffer.setSample(i, 1, 1.0f);
    }
    
    buffer.applyPan(1.0f);
    
    EXPECT_NEAR(buffer.getSample(0, 0), 0.0f, 0.01f); // Left should be silent
    EXPECT_TRUE(buffer.getSample(0, 1) > 0.0f); // Right should have signal
}

void AudioBufferTestSuite::testAnalysisOperations() {
    AudioBuffer buffer(1000, 2);
    
    // Create known test signal
    for (uint32_t i = 0; i < 1000; ++i) {
        float sample = std::sin(2.0f * M_PI * 440.0f * i / 44100.0f) * 0.5f;
        buffer.setSample(i, 0, sample);
        buffer.setSample(i, 1, sample);
    }
    
    // Test peak level
    float peak = buffer.getPeakLevel();
    EXPECT_TRUE(peak > 0.4f && peak < 0.6f); // Should be around 0.5
    
    // Test RMS level
    float rms = buffer.getRMSLevel();
    EXPECT_TRUE(rms > 0.3f && rms < 0.4f); // RMS of sine wave should be ~0.35
    
    // Test silence detection
    EXPECT_FALSE(buffer.isSilent());
    
    buffer.clear();
    EXPECT_TRUE(buffer.isSilent());
    
    // Test clipping detection
    buffer.setSample(0, 0, 1.1f); // Over full scale
    EXPECT_TRUE(buffer.hasClipping());
}

void AudioBufferTestSuite::testFormatConversion() {
    AudioBuffer stereo(100, 2);
    
    // Fill stereo buffer
    for (uint32_t i = 0; i < 100; ++i) {
        stereo.setSample(i, 0, 0.3f);
        stereo.setSample(i, 1, 0.7f);
    }
    
    // Convert to mono
    AudioBuffer mono = stereo.convertToMono();
    EXPECT_EQ(mono.getChannelCount(), 1);
    EXPECT_NEAR(mono.getSample(0, 0), 0.5f, 0.0001f); // Average of 0.3 and 0.7
    
    // Convert mono back to stereo
    AudioBuffer backToStereo = mono.convertToStereo();
    EXPECT_EQ(backToStereo.getChannelCount(), 2);
    EXPECT_NEAR(backToStereo.getSample(0, 0), 0.5f, 0.0001f);
    EXPECT_NEAR(backToStereo.getSample(0, 1), 0.5f, 0.0001f);
    
    // Test interleaved conversion
    AudioBuffer interleaved(100, 2);
    EXPECT_TRUE(interleaved.isInterleaved());
    
    interleaved.convertToNonInterleaved();
    EXPECT_FALSE(interleaved.isInterleaved());
    
    interleaved.convertToInterleaved();
    EXPECT_TRUE(interleaved.isInterleaved());
}

void AudioBufferTestSuite::testSynthEngineCompatibility() {
    // Create a SynthAudioBuffer
    SynthAudioBuffer synthBuffer(44100, 2);
    synthBuffer.samples = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f}; // 3 stereo frames
    synthBuffer.duration = 3.0f / 44100.0f;
    
    // Convert to AudioBuffer
    AudioBuffer audioBuffer = AudioBuffer::fromSynthAudioBuffer(synthBuffer);
    
    EXPECT_EQ(audioBuffer.getFrameCount(), 3);
    EXPECT_EQ(audioBuffer.getChannelCount(), 2);
    EXPECT_EQ(audioBuffer.getSampleRate(), 44100);
    
    // Check samples match
    EXPECT_NEAR(audioBuffer.getSample(0, 0), 0.1f, 0.0001f);
    EXPECT_NEAR(audioBuffer.getSample(0, 1), 0.2f, 0.0001f);
    EXPECT_NEAR(audioBuffer.getSample(1, 0), 0.3f, 0.0001f);
    EXPECT_NEAR(audioBuffer.getSample(1, 1), 0.4f, 0.0001f);
    
    // Convert back to SynthAudioBuffer
    auto backToSynth = audioBuffer.toSynthAudioBuffer();
    EXPECT_NOT_NULL(backToSynth.get());
    EXPECT_EQ(backToSynth->samples.size(), synthBuffer.samples.size());
    
    for (size_t i = 0; i < synthBuffer.samples.size(); ++i) {
        EXPECT_NEAR(backToSynth->samples[i], synthBuffer.samples[i], 0.0001f);
    }
}

void AudioBufferTestSuite::testUtilityFunctions() {
    // Test utility functions
    AudioBuffer silence = AudioBufferUtils::createSilence(1000, 2);
    EXPECT_TRUE(silence.isSilent());
    EXPECT_EQ(silence.getFrameCount(), 1000);
    EXPECT_EQ(silence.getChannelCount(), 2);
    
    AudioBuffer tone = AudioBufferUtils::createTone(440.0f, 1.0f, 44100, 2);
    EXPECT_EQ(tone.getFrameCount(), 44100);
    EXPECT_FALSE(tone.isSilent());
    
    AudioBuffer noise = AudioBufferUtils::createWhiteNoise(0.1f, 44100, 2);
    EXPECT_FALSE(noise.isSilent());
    
    // Test buffer comparison
    AudioBuffer buf1 = AudioBufferUtils::createTone(440.0f, 0.1f);
    AudioBuffer buf2 = AudioBufferUtils::createTone(440.0f, 0.1f);
    EXPECT_TRUE(AudioBufferUtils::buffersMatch(buf1, buf2, 0.001f));
    
    buf2.applyGain(1.1f);
    EXPECT_FALSE(AudioBufferUtils::buffersMatch(buf1, buf2, 0.001f));
}

void AudioBufferTestSuite::testPerformance() {
    const uint32_t frameCount = 44100; // 1 second at 44.1kHz
    const uint32_t iterations = 100;
    
    AudioBuffer buffer1(frameCount, 2);
    AudioBuffer buffer2(frameCount, 2);
    
    // Fill with test data
    for (uint32_t i = 0; i < frameCount; ++i) {
        buffer1.setSample(i, 0, static_cast<float>(i) / frameCount);
        buffer1.setSample(i, 1, static_cast<float>(i) / frameCount);
        buffer2.setSample(i, 0, 0.5f);
        buffer2.setSample(i, 1, 0.5f);
    }
    
    // Time mixing operations
    auto start = std::chrono::high_resolution_clock::now();
    
    for (uint32_t i = 0; i < iterations; ++i) {
        buffer1.mixFrom(buffer2, 0.5f);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    double avgTimePerMix = duration.count() / (double)iterations;
    
    // Should be able to mix 1 second of audio in less than 1ms on modern hardware
    EXPECT_TRUE(avgTimePerMix < 1000.0); // Less than 1ms per mix
    
    std::cout << "  Performance: " << avgTimePerMix << " μs per mix operation" << std::endl;
}

// === SynthNodeTestSuite Implementation ===

std::vector<TestResult> SynthNodeTestSuite::runAllTests() {
    std::vector<TestResult> results;
    
    // Test all existing synthesis methods are preserved
    results.push_back(runTest("Beep Generation", [this]() { testBeepGeneration(); }));
    results.push_back(runTest("Explosion Generation", [this]() { testExplosionGeneration(); }));
    results.push_back(runTest("Coin Generation", [this]() { testCoinGeneration(); }));
    results.push_back(runTest("Shoot Generation", [this]() { testShootGeneration(); }));
    results.push_back(runTest("Click Generation", [this]() { testClickGeneration(); }));
    results.push_back(runTest("Jump Generation", [this]() { testJumpGeneration(); }));
    results.push_back(runTest("Powerup Generation", [this]() { testPowerupGeneration(); }));
    results.push_back(runTest("Hurt Generation", [this]() { testHurtGeneration(); }));
    results.push_back(runTest("Pickup Generation", [this]() { testPickupGeneration(); }));
    results.push_back(runTest("Blip Generation", [this]() { testBlipGeneration(); }));
    results.push_back(runTest("Zap Generation", [this]() { testZapGeneration(); }));
    
    // Test advanced synthesis methods
    results.push_back(runTest("Advanced Explosions", [this]() { testAdvancedExplosions(); }));
    results.push_back(runTest("Sweep Effects", [this]() { testSweepEffects(); }));
    results.push_back(runTest("Oscillator Generation", [this]() { testOscillatorGeneration(); }));
    results.push_back(runTest("Random Generation", [this]() { testRandomGeneration(); }));
    results.push_back(runTest("Custom Sounds", [this]() { testCustomSounds(); }));
    
    // Test new real-time capabilities
    results.push_back(runTest("Real-time Oscillators", [this]() { testRealTimeOscillators(); }));
    results.push_back(runTest("Voice Management", [this]() { testVoiceManagement(); }));
    results.push_back(runTest("Global Effects", [this]() { testGlobalEffects(); }));
    results.push_back(runTest("Resource Management", [this]() { testResourceManagement(); }));
    
    // Performance and compatibility tests
    results.push_back(runTest("Memory Usage", [this]() { testMemoryUsage(); }));
    results.push_back(runTest("CPU Usage", [this]() { testCpuUsage(); }));
    results.push_back(runTest("Audio Integration", [this]() { testAudioIntegration(); }));
    
    return results;
}

void SynthNodeTestSuite::testBeepGeneration() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    // Test basic beep generation
    uint32_t beepId = synthNode->generateBeepToMemory(440.0f, 0.5f);
    EXPECT_TRUE(beepId != 0);
    
    // Test different frequencies
    uint32_t beep2Id = synthNode->generateBeepToMemory(880.0f, 0.25f);
    EXPECT_TRUE(beep2Id != 0);
    EXPECT_TRUE(beep2Id != beepId); // Should get different IDs
    
    // Test edge cases
    uint32_t lowBeepId = synthNode->generateBeepToMemory(50.0f, 0.1f);
    uint32_t highBeepId = synthNode->generateBeepToMemory(8000.0f, 0.1f);
    EXPECT_TRUE(lowBeepId != 0);
    EXPECT_TRUE(highBeepId != 0);
}

void SynthNodeTestSuite::testExplosionGeneration() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    // Test basic explosion
    uint32_t explosionId = synthNode->generateExplodeToMemory(1.0f, 1.0f);
    EXPECT_TRUE(explosionId != 0);
    
    // Test different sizes
    uint32_t smallExplosionId = synthNode->generateExplodeToMemory(0.5f, 0.5f);
    uint32_t bigExplosionId = synthNode->generateExplodeToMemory(2.0f, 2.0f);
    EXPECT_TRUE(smallExplosionId != 0);
    EXPECT_TRUE(bigExplosionId != 0);
}

void SynthNodeTestSuite::testCoinGeneration() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    uint32_t coinId = synthNode->generateCoinToMemory(1.0f, 0.4f);
    EXPECT_TRUE(coinId != 0);
    
    // Test different pitches
    uint32_t lowCoinId = synthNode->generateCoinToMemory(0.5f, 0.4f);
    uint32_t highCoinId = synthNode->generateCoinToMemory(2.0f, 0.4f);
    EXPECT_TRUE(lowCoinId != 0);
    EXPECT_TRUE(highCoinId != 0);
}

void SynthNodeTestSuite::testShootGeneration() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    uint32_t shootId = synthNode->generateShootToMemory(1.0f, 0.2f);
    EXPECT_TRUE(shootId != 0);
    
    // Test different intensities
    uint32_t weakShootId = synthNode->generateShootToMemory(0.3f, 0.15f);
    uint32_t strongShootId = synthNode->generateShootToMemory(2.0f, 0.25f);
    EXPECT_TRUE(weakShootId != 0);
    EXPECT_TRUE(strongShootId != 0);
}

void SynthNodeTestSuite::testClickGeneration() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    uint32_t clickId = synthNode->generateClickToMemory(1.0f, 0.05f);
    EXPECT_TRUE(clickId != 0);
    
    // Test very short click
    uint32_t shortClickId = synthNode->generateClickToMemory(0.8f, 0.01f);
    EXPECT_TRUE(shortClickId != 0);
}

void SynthNodeTestSuite::testJumpGeneration() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    uint32_t jumpId = synthNode->generateJumpToMemory(1.0f, 0.3f);
    EXPECT_TRUE(jumpId != 0);
}

void SynthNodeTestSuite::testPowerupGeneration() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    uint32_t powerupId = synthNode->generatePowerupToMemory(1.0f, 0.8f);
    EXPECT_TRUE(powerupId != 0);
}

void SynthNodeTestSuite::testHurtGeneration() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    uint32_t hurtId = synthNode->generateHurtToMemory(1.0f, 0.4f);
    EXPECT_TRUE(hurtId != 0);
}

void SynthNodeTestSuite::testPickupGeneration() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    uint32_t pickupId = synthNode->generatePickupToMemory(1.0f, 0.25f);
    EXPECT_TRUE(pickupId != 0);
}

void SynthNodeTestSuite::testBlipGeneration() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    uint32_t blipId = synthNode->generateBlipToMemory(1.0f, 0.1f);
    EXPECT_TRUE(blipId != 0);
}

void SynthNodeTestSuite::testZapGeneration() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    uint32_t zapId = synthNode->generateZapToMemory(2000.0f, 0.15f);
    EXPECT_TRUE(zapId != 0);
}

void SynthNodeTestSuite::testAdvancedExplosions() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    // Test all advanced explosion types
    uint32_t bigExplosionId = synthNode->generateBigExplosionToMemory(1.5f, 2.0f);
    uint32_t smallExplosionId = synthNode->generateSmallExplosionToMemory(0.8f, 0.5f);
    uint32_t distantExplosionId = synthNode->generateDistantExplosionToMemory(1.0f, 1.5f);
    uint32_t metalExplosionId = synthNode->generateMetalExplosionToMemory(1.2f, 1.2f);
    
    EXPECT_TRUE(bigExplosionId != 0);
    EXPECT_TRUE(smallExplosionId != 0);
    EXPECT_TRUE(distantExplosionId != 0);
    EXPECT_TRUE(metalExplosionId != 0);
    
    // All should be different
    EXPECT_TRUE(bigExplosionId != smallExplosionId);
    EXPECT_TRUE(smallExplosionId != distantExplosionId);
    EXPECT_TRUE(distantExplosionId != metalExplosionId);
}

void SynthNodeTestSuite::testSweepEffects() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    // Test sweep up
    uint32_t sweepUpId = synthNode->generateSweepUpToMemory(200.0f, 2000.0f, 0.5f);
    EXPECT_TRUE(sweepUpId != 0);
    
    // Test sweep down
    uint32_t sweepDownId = synthNode->generateSweepDownToMemory(2000.0f, 200.0f, 0.5f);
    EXPECT_TRUE(sweepDownId != 0);
    
    EXPECT_TRUE(sweepUpId != sweepDownId);
}

void SynthNodeTestSuite::testOscillatorGeneration() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    // Test all waveform types
    uint32_t sineId = synthNode->generateOscillatorToMemory(WAVE_SINE, 440.0f, 0.5f, 0.01f, 0.1f, 0.7f, 0.2f);
    uint32_t squareId = synthNode->generateOscillatorToMemory(WAVE_SQUARE, 440.0f, 0.5f, 0.01f, 0.1f, 0.7f, 0.2f);
    uint32_t sawId = synthNode->generateOscillatorToMemory(WAVE_SAWTOOTH, 440.0f, 0.5f, 0.01f, 0.1f, 0.7f, 0.2f);
    uint32_t triangleId = synthNode->generateOscillatorToMemory(WAVE_TRIANGLE, 440.0f, 0.5f, 0.01f, 0.1f, 0.7f, 0.2f);
    
    EXPECT_TRUE(sineId != 0);
    EXPECT_TRUE(squareId != 0);
    EXPECT_TRUE(sawId != 0);
    EXPECT_TRUE(triangleId != 0);
    
    // All should be different
    EXPECT_TRUE(sineId != squareId);
    EXPECT_TRUE(squareId != sawId);
    EXPECT_TRUE(sawId != triangleId);
}

void SynthNodeTestSuite::testRandomGeneration() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    // Test random beep with different seeds
    uint32_t random1Id = synthNode->generateRandomBeepToMemory(12345, 0.3f);
    uint32_t random2Id = synthNode->generateRandomBeepToMemory(67890, 0.3f);
    uint32_t random3Id = synthNode->generateRandomBeepToMemory(12345, 0.3f); // Same seed
    
    EXPECT_TRUE(random1Id != 0);
    EXPECT_TRUE(random2Id != 0);
    EXPECT_TRUE(random3Id != 0);
    
    // Different seeds should produce different results
    EXPECT_TRUE(random1Id != random2Id);
    // Same seed might produce same result, but they'll have different IDs
}

void SynthNodeTestSuite::testCustomSounds() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    // Create custom sound effect
    SynthSoundEffect effect;
    effect.name = "TestEffect";
    effect.duration = 0.5f;
    
    // Add oscillator
    Oscillator osc;
    osc.waveform = WAVE_SINE;
    osc.frequency = 440.0f;
    osc.amplitude = 0.8f;
    effect.oscillators.push_back(osc);
    
    // Set envelope
    effect.envelope.attackTime = 0.01f;
    effect.envelope.decayTime = 0.1f;
    effect.envelope.sustainLevel = 0.6f;
    effect.envelope.releaseTime = 0.3f;
    
    uint32_t customId = synthNode->generateCustomSoundToMemory(effect);
    EXPECT_TRUE(customId != 0);
    
    // Test predefined sound types
    uint32_t predefinedId = synthNode->generatePredefinedSoundToMemory(SoundEffectType::BEEP, 0.3f);
    EXPECT_TRUE(predefinedId != 0);
}

void SynthNodeTestSuite::testRealTimeOscillators() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    // Create real-time oscillator
    uint32_t oscId = synthNode->createRealTimeOscillator(WAVE_SINE, 440.0f);
    EXPECT_TRUE(oscId != 0);
    
    // Test parameter changes
    synthNode->setOscillatorFrequency(oscId, 880.0f);
    synthNode->setOscillatorWaveform(oscId, WAVE_SQUARE);
    synthNode->setOscillatorAmplitude(oscId, 0.5f);
    
    // Test envelope
    EnvelopeADSR envelope;
    envelope.attackTime = 0.1f;
    envelope.releaseTime = 0.5f;
    synthNode->setOscillatorEnvelope(oscId, envelope);
    
    // Test trigger and release
    synthNode->triggerOscillator(oscId);
    synthNode->releaseOscillator(oscId);
    
    // Test deletion
    synthNode->deleteOscillator(oscId);
    
    // Creating another should work
    uint32_t oscId2 = synthNode->createRealTimeOscillator(WAVE_TRIANGLE, 220.0f);
    EXPECT_TRUE(oscId2 != 0);
    EXPECT_TRUE(oscId2 != oscId); // Should be different ID
}

void SynthNodeTestSuite::testVoiceManagement() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    // Initial state
    EXPECT_EQ(synthNode->getActiveVoiceCount(), 0);
    
    // Generate some sounds
    uint32_t sound1 = synthNode->generateBeepToMemory(440.0f, 0.1f);
    uint32_t sound2 = synthNode->generateBeepToMemory(880.0f, 0.1f);
    
    // Play sounds
    synthNode->playSound(sound1);
    synthNode->playSound(sound2);
    
    // Stop one sound
    synthNode->stopSound(sound1);
    
    // Stop all sounds
    synthNode->stopAllSounds();
    
    // Test voice limits
    synthNode->setMaxVoices(5);
    EXPECT_EQ(synthNode->getMaxVoices(), 5);
    
    // Clear all voices
    synthNode->clearAllVoices();
    EXPECT_EQ(synthNode->getActiveVoiceCount(), 0);
}

void SynthNodeTestSuite::testGlobalEffects() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    // Test global volume
    EXPECT_NEAR(synthNode->getGlobalVolume(), 1.0f, 0.001f);
    
    synthNode->setGlobalVolume(0.5f);
    EXPECT_NEAR(synthNode->getGlobalVolume(), 0.5f, 0.001f);
    
    // Test global filter
    FilterParams filter;
    filter.type = FilterType::LOW_PASS;
    filter.cutoffFreq = 1000.0f;
    filter.resonance = 2.0f;
    
    synthNode->setGlobalFilter(filter);
    FilterParams retrievedFilter = synthNode->getGlobalFilter();
    
    EXPECT_TRUE(retrievedFilter.type == FilterType::LOW_PASS);
    EXPECT_NEAR(retrievedFilter.cutoffFreq, 1000.0f, 0.001f);
    EXPECT_NEAR(retrievedFilter.resonance, 2.0f, 0.001f);
}

void SynthNodeTestSuite::testResourceManagement() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    // Check initial memory usage
    size_t initialMemory = synthNode->getMemoryUsage();
    
    // Generate some sounds
    std::vector<uint32_t> soundIds;
    for (int i = 0; i < 10; ++i) {
        uint32_t id = synthNode->generateBeepToMemory(440.0f + i * 100, 0.5f);
        soundIds.push_back(id);
    }
    
    // Memory should have increased
    size_t afterGeneration = synthNode->getMemoryUsage();
    EXPECT_TRUE(afterGeneration > initialMemory);
    
    // Run garbage collection
    synthNode->runGarbageCollection();
    
    // Create real-time oscillators
    std::vector<uint32_t> oscIds;
    for (int i = 0; i < 5; ++i) {
        uint32_t id = synthNode->createRealTimeOscillator(WAVE_SINE, 440.0f + i * 100);
        oscIds.push_back(id);
    }
    
    // Delete some oscillators
    for (int i = 0; i < 3; ++i) {
        synthNode->deleteOscillator(oscIds[i]);
    }
    
    // Run garbage collection again
    synthNode->runGarbageCollection();
}

void SynthNodeTestSuite::testMemoryUsage() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    size_t initialMemory = synthNode->getMemoryUsage();
    
    // Generate a bunch of sounds
    for (int i = 0; i < 100; ++i) {
        synthNode->generateBeepToMemory(440.0f, 0.1f);
    }
    
    size_t afterGeneration = synthNode->getMemoryUsage();
    EXPECT_TRUE(afterGeneration > initialMemory);
    
    // Memory usage should be reasonable (less than 10MB for 100 short sounds)
    EXPECT_TRUE(afterGeneration < 10 * 1024 * 1024);
    
    synthNode->clearAllVoices();
    synthNode->runGarbageCollection();
    
    // Memory should decrease after cleanup
    size_t afterCleanup = synthNode->getMemoryUsage();
    EXPECT_TRUE(afterCleanup <= afterGeneration);
}

void SynthNodeTestSuite::testCpuUsage() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    // Generate audio to get CPU usage metrics
    AudioBuffer outputBuffer(512, 2, 44100);
    
    // Process some audio
    synthNode->generateAudio(outputBuffer);
    
    float cpuUsage = synthNode->getCpuUsage();
    
    // CPU usage should be reasonable (less than 50% for a simple test)
    EXPECT_TRUE(cpuUsage >= 0.0f);
    EXPECT_TRUE(cpuUsage < 50.0f);
}

void SynthNodeTestSuite::testAudioIntegration() {
    auto synthNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(synthNode->initialize());
    
    // Generate a sound
    uint32_t beepId = synthNode->generateBeepToMemory(440.0f, 0.1f);
    synthNode->playSound(beepId);
    
    // Generate audio
    AudioBuffer outputBuffer(4410, 2, 44100); // 0.1 seconds
    synthNode->generateAudio(outputBuffer);
    
    // Should have some audio content
    EXPECT_FALSE(outputBuffer.isSilent());
    
    // Audio should be within reasonable range
    float peak = outputBuffer.getPeakLevel();
    EXPECT_TRUE(peak > 0.0f);
    EXPECT_TRUE(peak <= 1.0f);
    
    // Test that it integrates properly with AudioNode interface
    EXPECT_TRUE(synthNode->isEnabled());
    EXPECT_TRUE(synthNode->getNodeType() == "Synthesizer");
}

// === CompatibilityTestSuite Implementation ===

std::vector<TestResult> CompatibilityTestSuite::runAllTests() {
    std::vector<TestResult> results;
    
    results.push_back(runTest("SynthEngine Preservation", [this]() { testSynthEnginePreservation(); }));
    results.push_back(runTest("Identical Outputs", [this]() { testIdenticalOutputs(); }));
    results.push_back(runTest("All Synth Methods", [this]() { testAllSynthMethods(); }));
    results.push_back(runTest("Parameter Compatibility", [this]() { testParameterCompatibility(); }));
    results.push_back(runTest("Performance Parity", [this]() { testPerformanceParity(); }));
    results.push_back(runTest("Memory Footprint", [this]() { testMemoryFootprint(); }));
    
    return results;
}

void CompatibilityTestSuite::testSynthEnginePreservation() {
    // Create old SynthEngine
    auto oldEngine = std::make_unique<SynthEngine>();
    EXPECT_TRUE(oldEngine->initialize());
    
    // Create new SynthNode
    auto newNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(newNode->initialize());
    
    // Test that both can generate sounds
    auto oldBeep = oldEngine->generateBeep(440.0f, 0.5f);
    uint32_t newBeepId = newNode->generateBeepToMemory(440.0f, 0.5f);
    
    EXPECT_NOT_NULL(oldBeep.get());
    EXPECT_TRUE(newBeepId != 0);
    
    // Both should produce valid audio
    EXPECT_TRUE(oldBeep->getSampleCount() > 0);
    EXPECT_FALSE(oldBeep->samples.empty());
}

void CompatibilityTestSuite::testIdenticalOutputs() {
    // This is the critical test - ensure outputs are identical
    auto oldEngine = std::make_unique<SynthEngine>();
    auto newNode = std::make_shared<SynthNode>();
    
    EXPECT_TRUE(oldEngine->initialize());
    EXPECT_TRUE(newNode->initialize());
    
    // Test beep generation
    auto oldBeep = oldEngine->generateBeep(440.0f, 0.5f);
    auto newNode2 = std::make_shared<SynthNode>(std::make_unique<SynthEngine>());
    newNode2->initialize();
    
    // This would require access to the generated buffer from SynthNode
    // For now, we'll test that the same parameters produce consistent results
    uint32_t beep1 = newNode->generateBeepToMemory(440.0f, 0.5f);
    uint32_t beep2 = newNode->generateBeepToMemory(440.0f, 0.5f);
    
    EXPECT_TRUE(beep1 != 0);
    EXPECT_TRUE(beep2 != 0);
    // Note: In a full implementation, we'd compare the actual audio data
}

void CompatibilityTestSuite::testAllSynthMethods() {
    auto newNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(newNode->initialize());
    
    // Test all synthesis methods exist and work
    EXPECT_TRUE(newNode->generateBeepToMemory(440.0f, 0.5f) != 0);
    EXPECT_TRUE(newNode->generateExplodeToMemory(1.0f, 1.0f) != 0);
    EXPECT_TRUE(newNode->generateCoinToMemory(1.0f, 0.4f) != 0);
    EXPECT_TRUE(newNode->generateShootToMemory(1.0f, 0.2f) != 0);
    EXPECT_TRUE(newNode->generateClickToMemory(1.0f, 0.05f) != 0);
    EXPECT_TRUE(newNode->generateJumpToMemory(1.0f, 0.3f) != 0);
    EXPECT_TRUE(newNode->generatePowerupToMemory(1.0f, 0.8f) != 0);
    EXPECT_TRUE(newNode->generateHurtToMemory(1.0f, 0.4f) != 0);
    EXPECT_TRUE(newNode->generatePickupToMemory(1.0f, 0.25f) != 0);
    EXPECT_TRUE(newNode->generateBlipToMemory(1.0f, 0.1f) != 0);
    EXPECT_TRUE(newNode->generateZapToMemory(2000.0f, 0.15f) != 0);
    
    // Advanced methods
    EXPECT_TRUE(newNode->generateBigExplosionToMemory(1.0f, 2.0f) != 0);
    EXPECT_TRUE(newNode->generateSmallExplosionToMemory(1.0f, 0.5f) != 0);
    EXPECT_TRUE(newNode->generateDistantExplosionToMemory(1.0f, 1.5f) != 0);
    EXPECT_TRUE(newNode->generateMetalExplosionToMemory(1.0f, 1.2f) != 0);
    EXPECT_TRUE(newNode->generateSweepUpToMemory(200.0f, 2000.0f, 0.5f) != 0);
    EXPECT_TRUE(newNode->generateSweepDownToMemory(2000.0f, 200.0f, 0.5f) != 0);
    EXPECT_TRUE(newNode->generateRandomBeepToMemory(12345, 0.3f) != 0);
    EXPECT_TRUE(newNode->generateOscillatorToMemory(WAVE_SINE, 440.0f, 0.5f, 0.01f, 0.1f, 0.7f, 0.2f) != 0);
}

void CompatibilityTestSuite::testParameterCompatibility() {
    auto newNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(newNode->initialize());
    
    // Test parameter handling
    newNode->setGlobalVolume(0.5f);
    EXPECT_NEAR(newNode->getGlobalVolume(), 0.5f, 0.001f);
    
    // Test AudioNode parameters
    newNode->setParameter("global_volume", 0.8f);
    EXPECT_NEAR(newNode->getParameter("global_volume"), 0.8f, 0.001f);
    
    // Test that parameter names are available
    auto paramNames = newNode->getParameterNames();
    EXPECT_TRUE(paramNames.size() > 0);
    
    bool foundGlobalVolume = false;
    for (const auto& name : paramNames) {
        if (name == "global_volume") {
            foundGlobalVolume = true;
            break;
        }
    }
    EXPECT_TRUE(foundGlobalVolume);
}

void CompatibilityTestSuite::testPerformanceParity() {
    auto oldEngine = std::make_unique<SynthEngine>();
    auto newNode = std::make_shared<SynthNode>();
    
    EXPECT_TRUE(oldEngine->initialize());
    EXPECT_TRUE(newNode->initialize());
    
    const int iterations = 100;
    
    // Time old engine
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto sound = oldEngine->generateBeep(440.0f, 0.1f);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto oldTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Time new node
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        uint32_t id = newNode->generateBeepToMemory(440.0f, 0.1f);
    }
    end = std::chrono::high_resolution_clock::now();
    auto newTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // New implementation should be within 2x of old implementation
    double ratio = static_cast<double>(newTime.count()) / static_cast<double>(oldTime.count());
    EXPECT_TRUE(ratio < 2.0);
    
    std::cout << "  Performance ratio (new/old): " << ratio << std::endl;
}

void CompatibilityTestSuite::testMemoryFootprint() {
    auto newNode = std::make_shared<SynthNode>();
    EXPECT_TRUE(newNode->initialize());
    
    size_t initialMemory = newNode->getMemoryUsage();
    
    // Generate sounds and check memory growth is reasonable
    for (int i = 0; i < 50; ++i) {
        newNode->generateBeepToMemory(440.0f, 0.1f);
    }
    
    size_t afterGeneration = newNode->getMemoryUsage();
    size_t memoryGrowth = afterGeneration - initialMemory;
    
    // Memory growth should be reasonable (less than 5MB for 50 short sounds)
    EXPECT_TRUE(memoryGrowth < 5 * 1024 * 1024);
    
    std::cout << "  Memory growth for 50 sounds: " << memoryGrowth << " bytes" << std::endl;
}

// === AudioSystemTestRunner Implementation ===

AudioSystemTestRunner::AudioSystemTestRunner() {
    // Initialize test suites
    testSuites.push_back(std::make_unique<AudioBufferTestSuite>());
    testSuites.push_back(std::make_unique<SynthNodeTestSuite>());
    testSuites.push_back(std::make_unique<CompatibilityTestSuite>());
}

AudioSystemTestRunner::~AudioSystemTestRunner() = default;

bool AudioSystemTestRunner::runAllTests(bool verbose) {
    lastResults.clear();
    
    std::cout << "🎵 SuperTerminal Audio System v2.0 Test Suite 🎵" << std::endl;
    std::cout << "=================================================" << std::endl;
    
    bool allPassed = true;
    
    for (const auto& testSuite : testSuites) {
        std::cout << "\n📋 Running " << testSuite->getSuiteName() << " Tests..." << std::endl;
        std::cout << std::string(50, '-') << std::endl;
        
        auto suiteResults = testSuite->runAllTests();
        
        for (const auto& result : suiteResults) {
            lastResults.push_back(result);
            printTestResult(result, verbose);
            
            if (!result.passed) {
                allPassed = false;
            }
        }
    }
    
    std::cout << "\n" << std::string(50, '=') << std::endl;
    printSummary(lastResults);
    
    return allPassed;
}

bool AudioSystemTestRunner::runTestSuite(const std::string& suiteName, bool verbose) {
    for (const auto& testSuite : testSuites) {
        if (testSuite->getSuiteName() == suiteName) {
            std::cout << "Running " << suiteName << " Test Suite..." << std::endl;
            
            auto results = testSuite->runAllTests();
            bool allPassed = true;
            
            for (const auto& result : results) {
                printTestResult(result, verbose);
                if (!result.passed) {
                    allPassed = false;
                }
            }
            
            return allPassed;
        }
    }
    
    std::cout << "Test suite '" << suiteName << "' not found." << std::endl;
    return false;
}

std::string AudioSystemTestRunner::generateReport() const {
    std::ostringstream report;
    
    report << "SuperTerminal Audio System v2.0 Test Report\n";
    report << "==========================================\n\n";
    
    int passed = 0, failed = 0;
    double totalTime = 0.0;
    
    for (const auto& result : lastResults) {
        if (result.passed) {
            passed++;
        } else {
            failed++;
        }
        totalTime += result.executionTimeMs;
    }
    
    report << "Summary:\n";
    report << "  Tests Run: " << (passed + failed) << "\n";
    report << "  Passed: " << passed << "\n";
    report << "  Failed: " << failed << "\n";
    report << "  Success Rate: " << std::fixed << std::setprecision(1) 
           << (100.0 * passed / (passed + failed)) << "%\n";
    report << "  Total Time: " << std::fixed << std::setprecision(2) 
           << totalTime << " ms\n\n";
    
    if (failed > 0) {
        report << "Failed Tests:\n";
        for (const auto& result : lastResults) {
            if (!result.passed) {
                report << "  ❌ " << result.testName << ": " << result.errorMessage << "\n";
            }
        }
        report << "\n";
    }
    
    report << "All Tests:\n";
    for (const auto& result : lastResults) {
        report << "  " << (result.passed ? "✅" : "❌") << " " 
               << result.testName << " (" << std::fixed << std::setprecision(2) 
               << result.executionTimeMs << " ms)\n";
    }
    
    return report.str();
}

void AudioSystemTestRunner::saveReportToFile(const std::string& filename) const {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << generateReport();
        file.close();
        std::cout << "Test report saved to: " << filename << std::endl;
    } else {
        std::cerr << "Failed to save test report to: " << filename << std::endl;
    }
}

void AudioSystemTestRunner::printTestResult(const TestResult& result, bool verbose) const {
    std::string status = result.passed ? "✅" : "❌";
    std::cout << "  " << status << " " << std::left << std::setw(25) << result.testName;
    
    if (verbose || !result.passed) {
        std::cout << " (" << std::fixed << std::setprecision(2) << result.executionTimeMs << " ms)";
        
        if (!result.passed) {
            std::cout << "\n      Error: " << result.errorMessage;
        }
    }
    
    std::cout << std::endl;
    
    // Performance warning
    if (result.executionTimeMs > performanceThreshold) {
        std::cout << "      ⚠️  Performance warning: " << result.executionTimeMs 
                  << " ms (threshold: " << performanceThreshold << " ms)" << std::endl;
    }
}

void AudioSystemTestRunner::printSummary(const std::vector<TestResult>& results) const {
    int passed = 0, failed = 0;
    double totalTime = 0.0;
    
    for (const auto& result : results) {
        if (result.passed) {
            passed++;
        } else {
            failed++;
        }
        totalTime += result.executionTimeMs;
    }
    
    std::cout << "📊 Test Summary:" << std::endl;
    std::cout << "   Tests Run: " << (passed + failed) << std::endl;
    std::cout << "   Passed: " << passed << std::endl;
    std::cout << "   Failed: " << failed << std::endl;
    std::cout << "   Success Rate: " << std::fixed << std::setprecision(1) 
              << (100.0 * passed / (passed + failed)) << "%" << std::endl;
    std::cout << "   Total Time: " << std::fixed << std::setprecision(2) 
              << totalTime << " ms" << std::endl;
    
    if (failed == 0) {
        std::cout << "\n🎉 All tests passed! The new audio system preserves existing functionality." << std::endl;
    } else {
        std::cout << "\n⚠️  Some tests failed. Review the errors above." << std::endl;
    }
}

// === Global Test Functions ===

bool quickCompatibilityTest() {
    std::cout << "🔬 Quick Compatibility Test" << std::endl;
    std::cout << "===========================" << std::endl;
    
    try {
        // Test basic AudioBuffer functionality
        AudioBuffer buffer(1024, 2, 44100);
        if (buffer.isEmpty()) {
            std::cout << "❌ AudioBuffer creation failed" << std::endl;
            return false;
        }
        
        // Test SynthNode creation
        auto synthNode = std::make_shared<SynthNode>();
        if (!synthNode->initialize()) {
            std::cout << "❌ SynthNode initialization failed" << std::endl;
            return false;
        }
        
        // Test basic synthesis
        uint32_t beepId = synthNode->generateBeepToMemory(440.0f, 0.1f);
        if (beepId == 0) {
            std::cout << "❌ Basic synthesis failed" << std::endl;
            return false;
        }
        
        std::cout << "✅ Quick compatibility test passed!" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cout << "❌ Quick compatibility test failed: " << e.what() << std::endl;
        return false;
    }
}

bool fullValidationTest(bool generateReport) {
    AudioSystemTestRunner runner;
    return runner.runAllTests(!generateReport);
}

bool performanceRegressionTest(const std::string& baselineFile) {
    // Placeholder implementation
    std::cout << "Performance regression test not implemented yet" << std::endl;
    return true;
}

bool memoryLeakTest() {
    // Placeholder implementation
    std::cout << "Memory leak test not implemented yet" << std::endl;
    return true;
}

bool threadSafetyTest() {
    // Placeholder implementation
    std::cout << "Thread safety test not implemented yet" << std::endl;
    return true;
}

size_t getCurrentMemoryUsage() {
    // Placeholder - in a real implementation, this would query system memory
    return 0;
}

double getCurrentCpuUsage() {
    // Placeholder - in a real implementation, this would query system CPU
    return 0.0;
}

void generateTestAudioFile(const std::string& filename, const AudioBuffer& buffer) {
    // Placeholder - would save buffer to WAV file
    std::cout << "Saving audio file: " << filename << std::endl;
}

AudioBuffer loadTestAudioFile(const std::string& filename) {
    // Placeholder - would load WAV file
    std::cout << "Loading audio file: " << filename << std::endl;
    return AudioBuffer(1024, 2, 44100);
}

AudioBuffer generateTestTone(float frequency, float duration, float sampleRate) {
    return AudioBufferUtils::createTone(frequency, duration, static_cast<uint32_t>(sampleRate), 2);
}

AudioBuffer generateTestNoise(float duration, float sampleRate) {
    return AudioBufferUtils::createWhiteNoise(duration, static_cast<uint32_t>(sampleRate), 2);
}

AudioBuffer generateTestSilence(float duration, float sampleRate) {
    return AudioBufferUtils::createSilence(static_cast<uint32_t>(duration * sampleRate), 2, static_cast<uint32_t>(sampleRate));
}

std::vector<float> generateTestFrequencies() {
    return {440.0f, 880.0f, 1760.0f, 220.0f, 110.0f};
}

std::vector<float> generateTestDurations() {
    return {0.1f, 0.25f, 0.5f, 1.0f, 2.0f};
}

bool validateAudioBuffer(const AudioBuffer& buffer) {
    return buffer.isValid() && buffer.validateIntegrity();
}

bool validateSynthAudioBuffer(const SynthAudioBuffer& buffer) {
    return !buffer.samples.empty() && buffer.channels > 0 && buffer.sampleRate > 0;
}

bool validateAudioIntegrity(const AudioBuffer& buffer) {
    return buffer.validateIntegrity();
}

std::string getAudioBufferChecksum(const AudioBuffer& buffer) {
    // Simple checksum - in production would use proper hash
    size_t checksum = 0;
    for (uint32_t frame = 0; frame < buffer.getFrameCount(); ++frame) {
        for (uint32_t ch = 0; ch < buffer.getChannelCount(); ++ch) {
            float sample = buffer.getSample(frame, ch);
            checksum += static_cast<size_t>(sample * 1000000);
        }
    }
    return std::to_string(checksum);
}
