//
//  AudioBuffer.h
//  SuperTerminal Framework - Audio Graph v2.0
//
//  High-performance audio buffer for the new clean audio architecture
//  Designed for real-time audio processing with zero allocations in audio thread
//
//  NOTE: Audio v2.0 is a complete rewrite with modern node-based architecture.
//        It is fully implemented and tested but NOT currently integrated.
//        Reserved for future adoption when advanced audio features are needed
//        (effect chains, dynamic routing, real-time synthesis).
//        Current system uses AudioSystem/SynthEngine/MidiEngine directly.
//
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#pragma once

#include <vector>
#include <memory>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <atomic>

// Audio buffer configuration
struct AudioBufferConfig {
    uint32_t sampleRate = 44100;
    uint32_t channels = 2;              // Stereo by default
    uint32_t framesPerBuffer = 512;     // Low latency
    bool useFloatSamples = true;        // 32-bit float vs 16-bit int
    bool isInterleaved = true;          // LRLRLR vs LLLRRR
};

// High-performance audio buffer class
class AudioBuffer {
public:
    // Constructors
    AudioBuffer();
    AudioBuffer(uint32_t frameCount, uint32_t channelCount, uint32_t sampleRate = 44100);
    AudioBuffer(const AudioBufferConfig& config);
    AudioBuffer(const AudioBuffer& other);
    AudioBuffer(AudioBuffer&& other) noexcept;
    ~AudioBuffer();
    
    // Assignment operators
    AudioBuffer& operator=(const AudioBuffer& other);
    AudioBuffer& operator=(AudioBuffer&& other) noexcept;
    
    // === BUFFER MANAGEMENT ===
    
    // Resize buffer (NOT safe to call from audio thread)
    void resize(uint32_t frameCount, uint32_t channelCount);
    void resize(uint32_t frameCount); // Keep same channel count
    void reserve(uint32_t frameCount); // Pre-allocate without changing size
    
    // Clear buffer contents (safe for audio thread)
    void clear();
    void clearRange(uint32_t startFrame, uint32_t frameCount);
    
    // === BUFFER PROPERTIES ===
    
    uint32_t getFrameCount() const { return frameCount; }
    uint32_t getChannelCount() const { return channelCount; }
    uint32_t getSampleRate() const { return sampleRate; }
    uint32_t getSampleCount() const { return frameCount * channelCount; }
    float getDurationSeconds() const { return static_cast<float>(frameCount) / sampleRate; }
    
    bool isEmpty() const { return frameCount == 0 || channelCount == 0; }
    bool isInterleaved() const { return interleaved; }
    bool isFloatFormat() const { return useFloatSamples; }
    
    // Memory usage
    size_t getMemoryUsage() const;
    size_t getCapacity() const { return capacity; }
    
    // === SAMPLE ACCESS ===
    
    // Direct sample access (audio thread safe)
    float* getChannelData(uint32_t channel);
    const float* getChannelData(uint32_t channel) const;
    float* getInterleavedData() { return samples.data(); }
    const float* getInterleavedData() const { return samples.data(); }
    
    // Safe sample access with bounds checking
    float getSample(uint32_t frame, uint32_t channel) const;
    void setSample(uint32_t frame, uint32_t channel, float value);
    
    // Frame access (all channels at once)
    void getFrame(uint32_t frameIndex, float* frameData) const;
    void setFrame(uint32_t frameIndex, const float* frameData);
    
    // === AUDIO PROCESSING OPERATIONS ===
    
    // Mix operations (accumulate)
    void mixFrom(const AudioBuffer& source, float gain = 1.0f);
    void mixFrom(const AudioBuffer& source, uint32_t sourceStart, uint32_t destStart, uint32_t frameCount, float gain = 1.0f);
    void mixFromChannel(const AudioBuffer& source, uint32_t sourceChannel, uint32_t destChannel, float gain = 1.0f);
    
    // Copy operations (replace)
    void copyFrom(const AudioBuffer& source);
    void copyFrom(const AudioBuffer& source, uint32_t sourceStart, uint32_t destStart, uint32_t frameCount);
    void copyFromChannel(const AudioBuffer& source, uint32_t sourceChannel, uint32_t destChannel);
    
    // Volume and gain operations
    void applyGain(float gain);
    void applyGain(float gain, uint32_t startFrame, uint32_t frameCount);
    void applyChannelGain(uint32_t channel, float gain);
    void applyGainRamp(float startGain, float endGain); // Smooth volume changes
    void applyGainRamp(float startGain, float endGain, uint32_t startFrame, uint32_t frameCount);
    
    // Pan operations (stereo)
    void applyPan(float pan); // -1.0 (left) to 1.0 (right)
    void applyChannelPan(uint32_t sourceChannel, float pan);
    
    // Utility operations
    void reverse(); // Reverse playback
    void reverse(uint32_t startFrame, uint32_t frameCount);
    void fade(bool fadeIn, uint32_t frameCount); // Fade in/out
    void crossfade(const AudioBuffer& other, float crossfadeRatio); // 0.0 = this, 1.0 = other
    
    // === ANALYSIS FUNCTIONS ===
    
    // Level measurement
    float getPeakLevel() const;
    float getPeakLevel(uint32_t channel) const;
    float getRMSLevel() const;
    float getRMSLevel(uint32_t channel) const;
    float getRMSLevel(uint32_t startFrame, uint32_t frameCount) const;
    
    // Detect clipping or silence
    bool hasClipping(float threshold = 1.0f) const;
    bool isSilent(float threshold = 0.0001f) const;
    bool isSilent(uint32_t startFrame, uint32_t frameCount, float threshold = 0.0001f) const;
    
    // Find peak location
    uint32_t findPeakFrame() const;
    uint32_t findPeakFrame(uint32_t channel) const;
    
    // === FORMAT CONVERSION ===
    
    // Convert between interleaved and non-interleaved
    void convertToInterleaved();
    void convertToNonInterleaved();
    
    // Convert sample rate (simple linear interpolation)
    AudioBuffer resample(uint32_t newSampleRate) const;
    void resampleInPlace(uint32_t newSampleRate);
    
    // Convert channel count
    AudioBuffer convertToMono() const;           // Mix all channels to mono
    AudioBuffer convertToStereo() const;         // Mono->stereo or multi->stereo
    void convertToMonoInPlace();
    void convertToStereoInPlace();
    
    // === COMPATIBILITY WITH EXISTING SYNTHENGINE ===
    
    // Convert from existing SynthAudioBuffer
    static AudioBuffer fromSynthAudioBuffer(const struct SynthAudioBuffer& synthBuffer);
    void copyFromSynthAudioBuffer(const struct SynthAudioBuffer& synthBuffer);
    
    // Create SynthAudioBuffer from this buffer
    std::unique_ptr<struct SynthAudioBuffer> toSynthAudioBuffer() const;
    
    // === DEBUGGING AND VALIDATION ===
    
    bool isValid() const;
    std::string getDebugInfo() const;
    void dumpToConsole(uint32_t maxFramesToShow = 32) const;
    
    // Validate buffer integrity
    bool validateIntegrity() const;
    void assertValid() const;
    
    // === OPERATORS ===
    
    // Arithmetic operators for mixing
    AudioBuffer& operator+=(const AudioBuffer& other);
    AudioBuffer& operator-=(const AudioBuffer& other);
    AudioBuffer& operator*=(float gain);
    AudioBuffer& operator/=(float divisor);
    
    AudioBuffer operator+(const AudioBuffer& other) const;
    AudioBuffer operator-(const AudioBuffer& other) const;
    AudioBuffer operator*(float gain) const;
    AudioBuffer operator/(float divisor) const;
    
    // Comparison operators
    bool operator==(const AudioBuffer& other) const;
    bool operator!=(const AudioBuffer& other) const;
    
    // === THREAD SAFETY ===
    
    // Some operations are marked as audio-thread safe
    // Others require calling from non-audio thread
    bool isAudioThreadSafe() const { return audioThreadSafe.load(); }
    void setAudioThreadSafe(bool safe) { audioThreadSafe.store(safe); }
    
private:
    // Buffer data
    std::vector<float> samples;          // Main sample data
    uint32_t frameCount = 0;            // Number of audio frames
    uint32_t channelCount = 0;          // Number of channels
    uint32_t sampleRate = 44100;        // Sample rate in Hz
    uint32_t capacity = 0;              // Allocated capacity in frames
    bool interleaved = true;            // Sample layout
    bool useFloatSamples = true;        // Sample format
    
    // Thread safety
    std::atomic<bool> audioThreadSafe{true};
    
    // Internal helpers
    void allocateMemory();
    void deallocateMemory();
    size_t calculateSampleIndex(uint32_t frame, uint32_t channel) const;
    void validateFrameAndChannel(uint32_t frame, uint32_t channel) const;
    
    // Format conversion helpers
    void interleaveSamples();
    void deinterleaveSamples();
    
    // Processing helpers
    void mixSamples(const float* source, float* dest, uint32_t sampleCount, float gain) const;
    void copySamples(const float* source, float* dest, uint32_t sampleCount) const;
    void gainSamples(float* samples, uint32_t sampleCount, float gain) const;
    void gainRampSamples(float* samples, uint32_t sampleCount, float startGain, float endGain) const;
    
    // Constants for optimization
    static constexpr float SILENCE_THRESHOLD = 0.0001f;
    static constexpr float CLIPPING_THRESHOLD = 0.99f;
    static constexpr uint32_t MIN_FRAMES_FOR_THREADING = 1024;
};

// === UTILITY FUNCTIONS ===

namespace AudioBufferUtils {
    // Create common buffer types
    AudioBuffer createSilence(uint32_t frameCount, uint32_t channelCount = 2, uint32_t sampleRate = 44100);
    AudioBuffer createTone(float frequency, float duration, uint32_t sampleRate = 44100, uint32_t channels = 2);
    AudioBuffer createWhiteNoise(float duration, uint32_t sampleRate = 44100, uint32_t channels = 2);
    AudioBuffer createPinkNoise(float duration, uint32_t sampleRate = 44100, uint32_t channels = 2);
    
    // Buffer analysis
    bool buffersMatch(const AudioBuffer& a, const AudioBuffer& b, float tolerance = 0.0001f);
    float calculateSNR(const AudioBuffer& signal, const AudioBuffer& noise);
    float calculateTHD(const AudioBuffer& buffer, float fundamentalFreq);
    
    // Buffer manipulation
    AudioBuffer concatenate(const std::vector<AudioBuffer>& buffers);
    std::vector<AudioBuffer> split(const AudioBuffer& buffer, uint32_t framesPerChunk);
    AudioBuffer window(const AudioBuffer& buffer, uint32_t startFrame, uint32_t frameCount);
    
    // Format validation
    bool isValidSampleRate(uint32_t sampleRate);
    bool isValidChannelCount(uint32_t channels);
    bool isValidFrameCount(uint32_t frames);
    
    // Performance helpers
    void prefaultMemory(AudioBuffer& buffer); // Pre-fault pages for real-time use
    void lockMemory(AudioBuffer& buffer);     // Lock in RAM (if supported)
    void unlockMemory(AudioBuffer& buffer);
}

// === INLINE IMPLEMENTATIONS (for performance) ===

inline float AudioBuffer::getSample(uint32_t frame, uint32_t channel) const {
    #ifdef DEBUG
    validateFrameAndChannel(frame, channel);
    #endif
    
    if (interleaved) {
        return samples[frame * channelCount + channel];
    } else {
        return samples[channel * frameCount + frame];
    }
}

inline void AudioBuffer::setSample(uint32_t frame, uint32_t channel, float value) {
    #ifdef DEBUG
    validateFrameAndChannel(frame, channel);
    #endif
    
    if (interleaved) {
        samples[frame * channelCount + channel] = value;
    } else {
        samples[channel * frameCount + frame] = value;
    }
}

inline float* AudioBuffer::getChannelData(uint32_t channel) {
    assert(channel < channelCount);
    
    if (interleaved) {
        return nullptr; // Not supported for interleaved format
    } else {
        return samples.data() + (channel * frameCount);
    }
}

inline const float* AudioBuffer::getChannelData(uint32_t channel) const {
    assert(channel < channelCount);
    
    if (interleaved) {
        return nullptr; // Not supported for interleaved format
    } else {
        return samples.data() + (channel * frameCount);
    }
}

inline size_t AudioBuffer::calculateSampleIndex(uint32_t frame, uint32_t channel) const {
    if (interleaved) {
        return frame * channelCount + channel;
    } else {
        return channel * frameCount + frame;
    }
}

inline void AudioBuffer::validateFrameAndChannel(uint32_t frame, uint32_t channel) const {
    assert(frame < frameCount && "Frame index out of bounds");
    assert(channel < channelCount && "Channel index out of bounds");
}