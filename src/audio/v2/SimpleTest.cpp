//
//  SimpleTest.cpp
//  SuperTerminal Framework - Audio Graph v2.0
//
//  One simple test to verify basic functionality
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "AudioBuffer.h"
#include "SynthNode.h"
#include <iostream>
#include <memory>

// Simple test to verify AudioBuffer works
bool testAudioBuffer() {
    std::cout << "Testing AudioBuffer..." << std::endl;
    
    try {
        // Create a simple audio buffer
        AudioBuffer buffer(1024, 2, 44100);
        
        if (buffer.isEmpty()) {
            std::cout << "ERROR: AudioBuffer should not be empty" << std::endl;
            return false;
        }
        
        if (buffer.getFrameCount() != 1024) {
            std::cout << "ERROR: Expected 1024 frames, got " << buffer.getFrameCount() << std::endl;
            return false;
        }
        
        if (buffer.getChannelCount() != 2) {
            std::cout << "ERROR: Expected 2 channels, got " << buffer.getChannelCount() << std::endl;
            return false;
        }
        
        // Test setting and getting samples
        buffer.setSample(0, 0, 0.5f);
        float sample = buffer.getSample(0, 0);
        
        if (sample != 0.5f) {
            std::cout << "ERROR: Expected sample 0.5, got " << sample << std::endl;
            return false;
        }
        
        std::cout << "✅ AudioBuffer test passed!" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: AudioBuffer test threw exception: " << e.what() << std::endl;
        return false;
    }
}

// Simple test to verify SynthNode works
bool testSynthNode() {
    std::cout << "Testing SynthNode..." << std::endl;
    
    try {
        // Create a SynthNode
        auto synthNode = std::make_shared<SynthNode>();
        
        if (!synthNode) {
            std::cout << "ERROR: Failed to create SynthNode" << std::endl;
            return false;
        }
        
        // Initialize it
        if (!synthNode->initialize()) {
            std::cout << "ERROR: Failed to initialize SynthNode" << std::endl;
            return false;
        }
        
        // Test basic synthesis
        uint32_t beepId = synthNode->generateBeepToMemory(440.0f, 0.1f);
        
        if (beepId == 0) {
            std::cout << "ERROR: Failed to generate beep - got ID 0" << std::endl;
            return false;
        }
        
        // Test audio generation
        AudioBuffer outputBuffer(512, 2, 44100);
        synthNode->generateAudio(outputBuffer);
        
        // Buffer should be valid after generation
        if (!outputBuffer.isValid()) {
            std::cout << "ERROR: Output buffer is not valid after audio generation" << std::endl;
            return false;
        }
        
        std::cout << "✅ SynthNode test passed!" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: SynthNode test threw exception: " << e.what() << std::endl;
        return false;
    }
}

// Run all simple tests
int runSimpleTests() {
    std::cout << "🎵 Running Simple Audio v2.0 Tests 🎵" << std::endl;
    std::cout << "=====================================" << std::endl;
    
    int failedTests = 0;
    
    if (!testAudioBuffer()) {
        failedTests++;
    }
    
    if (!testSynthNode()) {
        failedTests++;
    }
    
    std::cout << std::endl;
    std::cout << "=====================================" << std::endl;
    
    if (failedTests == 0) {
        std::cout << "🎉 All tests passed!" << std::endl;
        std::cout << "✅ Audio v2.0 basic functionality working" << std::endl;
    } else {
        std::cout << "❌ " << failedTests << " test(s) failed!" << std::endl;
    }
    
    return failedTests;
}

// C interface for SuperTerminal integration
extern "C" {
    int simple_audio_v2_test() {
        return runSimpleTests();
    }
    
    bool simple_audio_buffer_test() {
        return testAudioBuffer();
    }
    
    bool simple_synth_node_test() {
        return testSynthNode();
    }
}