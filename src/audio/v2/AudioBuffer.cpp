//
//  AudioBuffer.cpp
//  SuperTerminal Framework - Audio Graph v2.0
//
//  High-performance audio buffer implementation
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "AudioBuffer.h"
#include "../SynthEngine.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <random>
#include <sstream>

// === CONSTRUCTORS AND DESTRUCTORS ===

AudioBuffer::AudioBuffer() 
    : frameCount(0), channelCount(0), sampleRate(44100), capacity(0)
    , interleaved(true), useFloatSamples(true) {
}

AudioBuffer::AudioBuffer(uint32_t frameCount, uint32_t channelCount, uint32_t sampleRate)
    : frameCount(frameCount), channelCount(channelCount), sampleRate(sampleRate)
    , capacity(frameCount), interleaved(true), useFloatSamples(true) {
    allocateMemory();
}

AudioBuffer::AudioBuffer(const AudioBufferConfig& config)
    : frameCount(config.framesPerBuffer), channelCount(config.channels)
    , sampleRate(config.sampleRate), capacity(config.framesPerBuffer)
    , interleaved(config.isInterleaved), useFloatSamples(config.useFloatSamples) {
    allocateMemory();
}

AudioBuffer::AudioBuffer(const AudioBuffer& other)
    : samples(other.samples), frameCount(other.frameCount), channelCount(other.channelCount)
    , sampleRate(other.sampleRate), capacity(other.capacity)
    , interleaved(other.interleaved), useFloatSamples(other.useFloatSamples) {
}

AudioBuffer::AudioBuffer(AudioBuffer&& other) noexcept
    : samples(std::move(other.samples)), frameCount(other.frameCount), channelCount(other.channelCount)
    , sampleRate(other.sampleRate), capacity(other.capacity)
    , interleaved(other.interleaved), useFloatSamples(other.useFloatSamples) {
    
    // Clear the moved-from object
    other.frameCount = 0;
    other.channelCount = 0;
    other.capacity = 0;
}

AudioBuffer::~AudioBuffer() {
    // Vector destructor handles memory cleanup
}

// === ASSIGNMENT OPERATORS ===

AudioBuffer& AudioBuffer::operator=(const AudioBuffer& other) {
    if (this != &other) {
        frameCount = other.frameCount;
        channelCount = other.channelCount;
        sampleRate = other.sampleRate;
        capacity = other.capacity;
        interleaved = other.interleaved;
        useFloatSamples = other.useFloatSamples;
        samples = other.samples;
    }
    return *this;
}

AudioBuffer& AudioBuffer::operator=(AudioBuffer&& other) noexcept {
    if (this != &other) {
        frameCount = other.frameCount;
        channelCount = other.channelCount;
        sampleRate = other.sampleRate;
        capacity = other.capacity;
        interleaved = other.interleaved;
        useFloatSamples = other.useFloatSamples;
        samples = std::move(other.samples);
        
        // Clear the moved-from object
        other.frameCount = 0;
        other.channelCount = 0;
        other.capacity = 0;
    }
    return *this;
}

// === BUFFER MANAGEMENT ===

void AudioBuffer::resize(uint32_t newFrameCount, uint32_t newChannelCount) {
    if (newFrameCount == frameCount && newChannelCount == channelCount) {
        return; // No change needed
    }
    
    frameCount = newFrameCount;
    channelCount = newChannelCount;
    capacity = newFrameCount;
    
    allocateMemory();
    audioThreadSafe.store(false); // Resize is not audio-thread safe
}

void AudioBuffer::resize(uint32_t newFrameCount) {
    resize(newFrameCount, channelCount);
}

void AudioBuffer::reserve(uint32_t newFrameCount) {
    if (newFrameCount > capacity) {
        capacity = newFrameCount;
        samples.reserve(capacity * channelCount);
    }
}

void AudioBuffer::clear() {
    if (!samples.empty()) {
        std::fill(samples.begin(), samples.end(), 0.0f);
    }
}

void AudioBuffer::clearRange(uint32_t startFrame, uint32_t frameCount) {
    if (startFrame >= this->frameCount) return;
    
    uint32_t endFrame = std::min(startFrame + frameCount, this->frameCount);
    uint32_t startSample = startFrame * channelCount;
    uint32_t sampleCount = (endFrame - startFrame) * channelCount;
    
    if (interleaved) {
        std::fill(samples.begin() + startSample, samples.begin() + startSample + sampleCount, 0.0f);
    } else {
        for (uint32_t ch = 0; ch < channelCount; ++ch) {
            uint32_t channelOffset = ch * this->frameCount;
            std::fill(samples.begin() + channelOffset + startFrame, 
                     samples.begin() + channelOffset + endFrame, 0.0f);
        }
    }
}

// === BUFFER PROPERTIES ===

size_t AudioBuffer::getMemoryUsage() const {
    return samples.size() * sizeof(float);
}

// === FRAME ACCESS ===

void AudioBuffer::getFrame(uint32_t frameIndex, float* frameData) const {
    if (frameIndex >= frameCount) return;
    
    if (interleaved) {
        const float* source = samples.data() + (frameIndex * channelCount);
        std::memcpy(frameData, source, channelCount * sizeof(float));
    } else {
        for (uint32_t ch = 0; ch < channelCount; ++ch) {
            frameData[ch] = samples[ch * frameCount + frameIndex];
        }
    }
}

void AudioBuffer::setFrame(uint32_t frameIndex, const float* frameData) {
    if (frameIndex >= frameCount) return;
    
    if (interleaved) {
        float* dest = samples.data() + (frameIndex * channelCount);
        std::memcpy(dest, frameData, channelCount * sizeof(float));
    } else {
        for (uint32_t ch = 0; ch < channelCount; ++ch) {
            samples[ch * frameCount + frameIndex] = frameData[ch];
        }
    }
}

// === AUDIO PROCESSING OPERATIONS ===

void AudioBuffer::mixFrom(const AudioBuffer& source, float gain) {
    if (source.channelCount != channelCount || source.frameCount != frameCount) {
        return; // Incompatible formats
    }
    
    const uint32_t sampleCount = frameCount * channelCount;
    mixSamples(source.samples.data(), samples.data(), sampleCount, gain);
}

void AudioBuffer::mixFrom(const AudioBuffer& source, uint32_t sourceStart, uint32_t destStart, uint32_t frameCount, float gain) {
    if (sourceStart >= source.frameCount || destStart >= this->frameCount) return;
    
    uint32_t actualFrameCount = std::min({
        frameCount,
        source.frameCount - sourceStart,
        this->frameCount - destStart
    });
    
    if (interleaved && source.interleaved && channelCount == source.channelCount) {
        const float* sourcePtr = source.samples.data() + (sourceStart * channelCount);
        float* destPtr = samples.data() + (destStart * channelCount);
        mixSamples(sourcePtr, destPtr, actualFrameCount * channelCount, gain);
    } else {
        // Handle non-interleaved or channel mismatch case
        for (uint32_t frame = 0; frame < actualFrameCount; ++frame) {
            for (uint32_t ch = 0; ch < std::min(channelCount, source.channelCount); ++ch) {
                float sourceSample = source.getSample(sourceStart + frame, ch);
                float currentSample = getSample(destStart + frame, ch);
                setSample(destStart + frame, ch, currentSample + sourceSample * gain);
            }
        }
    }
}

void AudioBuffer::mixFromChannel(const AudioBuffer& source, uint32_t sourceChannel, uint32_t destChannel, float gain) {
    if (sourceChannel >= source.channelCount || destChannel >= channelCount) return;
    
    uint32_t actualFrameCount = std::min(frameCount, source.frameCount);
    
    for (uint32_t frame = 0; frame < actualFrameCount; ++frame) {
        float sourceSample = source.getSample(frame, sourceChannel);
        float currentSample = getSample(frame, destChannel);
        setSample(frame, destChannel, currentSample + sourceSample * gain);
    }
}

void AudioBuffer::copyFrom(const AudioBuffer& source) {
    if (source.channelCount != channelCount || source.frameCount != frameCount) {
        resize(source.frameCount, source.channelCount);
    }
    
    sampleRate = source.sampleRate;
    interleaved = source.interleaved;
    samples = source.samples;
}

void AudioBuffer::copyFrom(const AudioBuffer& source, uint32_t sourceStart, uint32_t destStart, uint32_t frameCount) {
    if (sourceStart >= source.frameCount || destStart >= this->frameCount) return;
    
    uint32_t actualFrameCount = std::min({
        frameCount,
        source.frameCount - sourceStart,
        this->frameCount - destStart
    });
    
    if (interleaved && source.interleaved && channelCount == source.channelCount) {
        const float* sourcePtr = source.samples.data() + (sourceStart * channelCount);
        float* destPtr = samples.data() + (destStart * channelCount);
        copySamples(sourcePtr, destPtr, actualFrameCount * channelCount);
    } else {
        for (uint32_t frame = 0; frame < actualFrameCount; ++frame) {
            for (uint32_t ch = 0; ch < std::min(channelCount, source.channelCount); ++ch) {
                float sourceSample = source.getSample(sourceStart + frame, ch);
                setSample(destStart + frame, ch, sourceSample);
            }
        }
    }
}

void AudioBuffer::copyFromChannel(const AudioBuffer& source, uint32_t sourceChannel, uint32_t destChannel) {
    if (sourceChannel >= source.channelCount || destChannel >= channelCount) return;
    
    uint32_t actualFrameCount = std::min(frameCount, source.frameCount);
    
    for (uint32_t frame = 0; frame < actualFrameCount; ++frame) {
        float sourceSample = source.getSample(frame, sourceChannel);
        setSample(frame, destChannel, sourceSample);
    }
}

void AudioBuffer::applyGain(float gain) {
    gainSamples(samples.data(), samples.size(), gain);
}

void AudioBuffer::applyGain(float gain, uint32_t startFrame, uint32_t frameCount) {
    if (startFrame >= this->frameCount) return;
    
    uint32_t endFrame = std::min(startFrame + frameCount, this->frameCount);
    uint32_t startSample = startFrame * channelCount;
    uint32_t sampleCount = (endFrame - startFrame) * channelCount;
    
    gainSamples(samples.data() + startSample, sampleCount, gain);
}

void AudioBuffer::applyChannelGain(uint32_t channel, float gain) {
    if (channel >= channelCount) return;
    
    if (interleaved) {
        for (uint32_t frame = 0; frame < frameCount; ++frame) {
            samples[frame * channelCount + channel] *= gain;
        }
    } else {
        float* channelData = samples.data() + (channel * frameCount);
        gainSamples(channelData, frameCount, gain);
    }
}

void AudioBuffer::applyGainRamp(float startGain, float endGain) {
    gainRampSamples(samples.data(), samples.size(), startGain, endGain);
}

void AudioBuffer::applyGainRamp(float startGain, float endGain, uint32_t startFrame, uint32_t frameCount) {
    if (startFrame >= this->frameCount) return;
    
    uint32_t endFrame = std::min(startFrame + frameCount, this->frameCount);
    uint32_t startSample = startFrame * channelCount;
    uint32_t sampleCount = (endFrame - startFrame) * channelCount;
    
    gainRampSamples(samples.data() + startSample, sampleCount, startGain, endGain);
}

void AudioBuffer::applyPan(float pan) {
    if (channelCount < 2) return; // Pan only works with stereo
    
    // Pan law: -3dB center
    float leftGain = std::sqrt(0.5f * (1.0f - pan));
    float rightGain = std::sqrt(0.5f * (1.0f + pan));
    
    applyChannelGain(0, leftGain);  // Left channel
    applyChannelGain(1, rightGain); // Right channel
}

void AudioBuffer::applyChannelPan(uint32_t sourceChannel, float pan) {
    if (sourceChannel >= channelCount || channelCount < 2) return;
    
    float leftGain = std::sqrt(0.5f * (1.0f - pan));
    float rightGain = std::sqrt(0.5f * (1.0f + pan));
    
    // Copy source channel to stereo channels with pan
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        float sample = getSample(frame, sourceChannel);
        setSample(frame, 0, sample * leftGain);  // Left
        if (channelCount > 1) {
            setSample(frame, 1, sample * rightGain); // Right
        }
    }
}

void AudioBuffer::reverse() {
    reverse(0, frameCount);
}

void AudioBuffer::reverse(uint32_t startFrame, uint32_t frameCount) {
    if (startFrame >= this->frameCount) return;
    
    uint32_t endFrame = std::min(startFrame + frameCount, this->frameCount);
    uint32_t actualFrameCount = endFrame - startFrame;
    
    std::vector<float> tempFrame(channelCount);
    
    for (uint32_t i = 0; i < actualFrameCount / 2; ++i) {
        uint32_t frontFrame = startFrame + i;
        uint32_t backFrame = endFrame - 1 - i;
        
        getFrame(frontFrame, tempFrame.data());
        float backFrameData[8]; // Assume max 8 channels
        getFrame(backFrame, backFrameData);
        
        setFrame(frontFrame, backFrameData);
        setFrame(backFrame, tempFrame.data());
    }
}

void AudioBuffer::fade(bool fadeIn, uint32_t fadeFrameCount) {
    uint32_t actualFrameCount = std::min(fadeFrameCount, frameCount);
    
    if (fadeIn) {
        applyGainRamp(0.0f, 1.0f, 0, actualFrameCount);
    } else {
        uint32_t startFrame = (frameCount > actualFrameCount) ? frameCount - actualFrameCount : 0;
        applyGainRamp(1.0f, 0.0f, startFrame, actualFrameCount);
    }
}

void AudioBuffer::crossfade(const AudioBuffer& other, float crossfadeRatio) {
    if (other.channelCount != channelCount || other.frameCount != frameCount) {
        return; // Incompatible formats
    }
    
    float thisGain = 1.0f - crossfadeRatio;
    float otherGain = crossfadeRatio;
    
    applyGain(thisGain);
    mixFrom(other, otherGain);
}

// === ANALYSIS FUNCTIONS ===

float AudioBuffer::getPeakLevel() const {
    if (samples.empty()) return 0.0f;
    
    float peak = 0.0f;
    for (const float& sample : samples) {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

float AudioBuffer::getPeakLevel(uint32_t channel) const {
    if (channel >= channelCount || samples.empty()) return 0.0f;
    
    float peak = 0.0f;
    
    if (interleaved) {
        for (uint32_t frame = 0; frame < frameCount; ++frame) {
            float sample = samples[frame * channelCount + channel];
            peak = std::max(peak, std::abs(sample));
        }
    } else {
        const float* channelData = samples.data() + (channel * frameCount);
        for (uint32_t frame = 0; frame < frameCount; ++frame) {
            peak = std::max(peak, std::abs(channelData[frame]));
        }
    }
    
    return peak;
}

float AudioBuffer::getRMSLevel() const {
    if (samples.empty()) return 0.0f;
    
    double sum = 0.0;
    for (const float& sample : samples) {
        sum += sample * sample;
    }
    
    return std::sqrt(sum / samples.size());
}

float AudioBuffer::getRMSLevel(uint32_t channel) const {
    if (channel >= channelCount || samples.empty()) return 0.0f;
    
    double sum = 0.0;
    
    if (interleaved) {
        for (uint32_t frame = 0; frame < frameCount; ++frame) {
            float sample = samples[frame * channelCount + channel];
            sum += sample * sample;
        }
    } else {
        const float* channelData = samples.data() + (channel * frameCount);
        for (uint32_t frame = 0; frame < frameCount; ++frame) {
            float sample = channelData[frame];
            sum += sample * sample;
        }
    }
    
    return std::sqrt(sum / frameCount);
}

float AudioBuffer::getRMSLevel(uint32_t startFrame, uint32_t frameCount) const {
    if (startFrame >= this->frameCount || samples.empty()) return 0.0f;
    
    uint32_t endFrame = std::min(startFrame + frameCount, this->frameCount);
    // uint32_t actualFrameCount = endFrame - startFrame; // unused for now
    
    double sum = 0.0;
    uint32_t sampleCount = 0;
    
    for (uint32_t frame = startFrame; frame < endFrame; ++frame) {
        for (uint32_t ch = 0; ch < channelCount; ++ch) {
            float sample = getSample(frame, ch);
            sum += sample * sample;
            sampleCount++;
        }
    }
    
    return (sampleCount > 0) ? std::sqrt(sum / sampleCount) : 0.0f;
}

bool AudioBuffer::hasClipping(float threshold) const {
    return getPeakLevel() >= threshold;
}

bool AudioBuffer::isSilent(float threshold) const {
    return getRMSLevel() < threshold;
}

bool AudioBuffer::isSilent(uint32_t startFrame, uint32_t frameCount, float threshold) const {
    return getRMSLevel(startFrame, frameCount) < threshold;
}

uint32_t AudioBuffer::findPeakFrame() const {
    if (samples.empty()) return 0;
    
    float peak = 0.0f;
    uint32_t peakFrame = 0;
    
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        float frameMax = 0.0f;
        for (uint32_t ch = 0; ch < channelCount; ++ch) {
            frameMax = std::max(frameMax, std::abs(getSample(frame, ch)));
        }
        if (frameMax > peak) {
            peak = frameMax;
            peakFrame = frame;
        }
    }
    
    return peakFrame;
}

uint32_t AudioBuffer::findPeakFrame(uint32_t channel) const {
    if (channel >= channelCount || samples.empty()) return 0;
    
    float peak = 0.0f;
    uint32_t peakFrame = 0;
    
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        float sample = std::abs(getSample(frame, channel));
        if (sample > peak) {
            peak = sample;
            peakFrame = frame;
        }
    }
    
    return peakFrame;
}

// === FORMAT CONVERSION ===

void AudioBuffer::convertToInterleaved() {
    if (interleaved) return;
    
    std::vector<float> newSamples(samples.size());
    
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        for (uint32_t ch = 0; ch < channelCount; ++ch) {
            newSamples[frame * channelCount + ch] = samples[ch * frameCount + frame];
        }
    }
    
    samples = std::move(newSamples);
    interleaved = true;
}

void AudioBuffer::convertToNonInterleaved() {
    if (!interleaved) return;
    
    std::vector<float> newSamples(samples.size());
    
    for (uint32_t ch = 0; ch < channelCount; ++ch) {
        for (uint32_t frame = 0; frame < frameCount; ++frame) {
            newSamples[ch * frameCount + frame] = samples[frame * channelCount + ch];
        }
    }
    
    samples = std::move(newSamples);
    interleaved = false;
}

AudioBuffer AudioBuffer::convertToMono() const {
    if (channelCount == 1) return *this;
    
    AudioBuffer mono(frameCount, 1, sampleRate);
    
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        float sum = 0.0f;
        for (uint32_t ch = 0; ch < channelCount; ++ch) {
            sum += getSample(frame, ch);
        }
        mono.setSample(frame, 0, sum / channelCount);
    }
    
    return mono;
}

AudioBuffer AudioBuffer::convertToStereo() const {
    if (channelCount == 2) return *this;
    
    AudioBuffer stereo(frameCount, 2, sampleRate);
    
    if (channelCount == 1) {
        // Mono to stereo - duplicate channel
        for (uint32_t frame = 0; frame < frameCount; ++frame) {
            float sample = getSample(frame, 0);
            stereo.setSample(frame, 0, sample);
            stereo.setSample(frame, 1, sample);
        }
    } else {
        // Multi-channel to stereo - mix down
        for (uint32_t frame = 0; frame < frameCount; ++frame) {
            float leftSum = 0.0f, rightSum = 0.0f;
            
            // Mix channels to stereo
            for (uint32_t ch = 0; ch < channelCount; ++ch) {
                float sample = getSample(frame, ch);
                if (ch % 2 == 0) {
                    leftSum += sample;
                } else {
                    rightSum += sample;
                }
            }
            
            uint32_t leftChannels = (channelCount + 1) / 2;
            uint32_t rightChannels = channelCount / 2;
            
            stereo.setSample(frame, 0, leftSum / leftChannels);
            stereo.setSample(frame, 1, rightSum / std::max(1u, rightChannels));
        }
    }
    
    return stereo;
}

void AudioBuffer::convertToMonoInPlace() {
    if (channelCount == 1) return;
    
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        float sum = 0.0f;
        for (uint32_t ch = 0; ch < channelCount; ++ch) {
            sum += getSample(frame, ch);
        }
        setSample(frame, 0, sum / channelCount);
    }
    
    // Resize to mono
    channelCount = 1;
    samples.resize(frameCount);
}

void AudioBuffer::convertToStereoInPlace() {
    if (channelCount == 2) return;
    
    AudioBuffer stereo = convertToStereo();
    *this = std::move(stereo);
}

// === COMPATIBILITY WITH EXISTING SYNTHENGINE ===

AudioBuffer AudioBuffer::fromSynthAudioBuffer(const SynthAudioBuffer& synthBuffer) {
    AudioBuffer buffer(synthBuffer.getFrameCount(), synthBuffer.channels, synthBuffer.sampleRate);
    
    const float* synthData = synthBuffer.samples.data();
    float* bufferData = buffer.samples.data();
    
    std::memcpy(bufferData, synthData, synthBuffer.samples.size() * sizeof(float));
    
    return buffer;
}

void AudioBuffer::copyFromSynthAudioBuffer(const SynthAudioBuffer& synthBuffer) {
    resize(synthBuffer.getFrameCount(), synthBuffer.channels);
    sampleRate = synthBuffer.sampleRate;
    
    const float* synthData = synthBuffer.samples.data();
    std::memcpy(samples.data(), synthData, synthBuffer.samples.size() * sizeof(float));
}

std::unique_ptr<SynthAudioBuffer> AudioBuffer::toSynthAudioBuffer() const {
    auto synthBuffer = std::make_unique<SynthAudioBuffer>(sampleRate, channelCount);
    synthBuffer->samples = samples;
    synthBuffer->duration = getDurationSeconds();
    
    return synthBuffer;
}

// === DEBUGGING AND VALIDATION ===

bool AudioBuffer::isValid() const {
    return frameCount > 0 && channelCount > 0 && 
           samples.size() == frameCount * channelCount &&
           sampleRate > 0;
}

std::string AudioBuffer::getDebugInfo() const {
    std::ostringstream ss;
    ss << "AudioBuffer Debug Info:\n";
    ss << "  Frame Count: " << frameCount << "\n";
    ss << "  Channel Count: " << channelCount << "\n";
    ss << "  Sample Rate: " << sampleRate << " Hz\n";
    ss << "  Duration: " << getDurationSeconds() << " seconds\n";
    ss << "  Sample Count: " << getSampleCount() << "\n";
    ss << "  Memory Usage: " << getMemoryUsage() << " bytes\n";
    ss << "  Format: " << (interleaved ? "Interleaved" : "Non-interleaved") << "\n";
    ss << "  Peak Level: " << getPeakLevel() << "\n";
    ss << "  RMS Level: " << getRMSLevel() << "\n";
    ss << "  Is Silent: " << (isSilent() ? "Yes" : "No") << "\n";
    ss << "  Has Clipping: " << (hasClipping() ? "Yes" : "No") << "\n";
    
    return ss.str();
}

void AudioBuffer::dumpToConsole(uint32_t maxFramesToShow) const {
    std::cout << getDebugInfo() << "\n";
    
    if (frameCount > 0) {
        std::cout << "Sample Data (first " << std::min(maxFramesToShow, frameCount) << " frames):\n";
        
        for (uint32_t frame = 0; frame < std::min(maxFramesToShow, frameCount); ++frame) {
            std::cout << "Frame " << std::setw(4) << frame << ": ";
            for (uint32_t ch = 0; ch < channelCount; ++ch) {
                std::cout << std::setw(8) << std::fixed << std::setprecision(4) 
                         << getSample(frame, ch);
                if (ch < channelCount - 1) std::cout << ", ";
            }
            std::cout << "\n";
        }
        
        if (frameCount > maxFramesToShow) {
            std::cout << "... (" << (frameCount - maxFramesToShow) << " more frames)\n";
        }
    }
}

bool AudioBuffer::validateIntegrity() const {
    if (!isValid()) return false;
    
    // Check for NaN or infinite values
    for (const float& sample : samples) {
        if (!std::isfinite(sample)) {
            return false;
        }
    }
    
    return true;
}

void AudioBuffer::assertValid() const {
    assert(isValid() && "AudioBuffer is not valid");
    assert(validateIntegrity() && "AudioBuffer contains invalid samples");
}

// === OPERATORS ===

AudioBuffer& AudioBuffer::operator+=(const AudioBuffer& other) {
    mixFrom(other, 1.0f);
    return *this;
}

AudioBuffer& AudioBuffer::operator-=(const AudioBuffer& other) {
    mixFrom(other, -1.0f);
    return *this;
}

AudioBuffer& AudioBuffer::operator*=(float gain) {
    applyGain(gain);
    return *this;
}

AudioBuffer& AudioBuffer::operator/=(float divisor) {
    if (divisor != 0.0f) {
        applyGain(1.0f / divisor);
    }
    return *this;
}

AudioBuffer AudioBuffer::operator+(const AudioBuffer& other) const {
    AudioBuffer result = *this;
    result += other;
    return result;
}

AudioBuffer AudioBuffer::operator-(const AudioBuffer& other) const {
    AudioBuffer result = *this;
    result -= other;
    return result;
}

AudioBuffer AudioBuffer::operator*(float gain) const {
    AudioBuffer result = *this;
    result *= gain;
    return result;
}

AudioBuffer AudioBuffer::operator/(float divisor) const {
    AudioBuffer result = *this;
    result /= divisor;
    return result;
}

bool AudioBuffer::operator==(const AudioBuffer& other) const {
    if (frameCount != other.frameCount || channelCount != other.channelCount) {
        return false;
    }
    
    return samples == other.samples;
}

bool AudioBuffer::operator!=(const AudioBuffer& other) const {
    return !(*this == other);
}

// === PRIVATE HELPER METHODS ===

void AudioBuffer::allocateMemory() {
    uint32_t totalSamples = frameCount * channelCount;
    samples.resize(totalSamples);
    std::fill(samples.begin(), samples.end(), 0.0f);
}

void AudioBuffer::deallocateMemory() {
    samples.clear();
    samples.shrink_to_fit();
}

void AudioBuffer::mixSamples(const float* source, float* dest, uint32_t sampleCount, float gain) const {
    for (uint32_t i = 0; i < sampleCount; ++i) {
        dest[i] += source[i] * gain;
    }
}

void AudioBuffer::copySamples(const float* source, float* dest, uint32_t sampleCount) const {
    std::memcpy(dest, source, sampleCount * sizeof(float));
}

void AudioBuffer::gainSamples(float* samples, uint32_t sampleCount, float gain) const {
    for (uint32_t i = 0; i < sampleCount; ++i) {
        samples[i] *= gain;
    }
}

void AudioBuffer::gainRampSamples(float* samples, uint32_t sampleCount, float startGain, float endGain) const {
    if (sampleCount == 0) return;
    
    float gainStep = (endGain - startGain) / (sampleCount - 1);
    
    for (uint32_t i = 0; i < sampleCount; ++i) {
        float currentGain = startGain + (gainStep * i);
        samples[i] *= currentGain;
    }
}

// === UTILITY FUNCTIONS ===

namespace AudioBufferUtils {

AudioBuffer createSilence(uint32_t frameCount, uint32_t channelCount, uint32_t sampleRate) {
    return AudioBuffer(frameCount, channelCount, sampleRate);
}

AudioBuffer createTone(float frequency, float duration, uint32_t sampleRate, uint32_t channels) {
    uint32_t frameCount = static_cast<uint32_t>(duration * sampleRate);
    AudioBuffer buffer(frameCount, channels, sampleRate);
    
    float phaseIncrement = 2.0f * M_PI * frequency / sampleRate;
    float phase = 0.0f;
    
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        float sample = std::sin(phase);
        
        for (uint32_t ch = 0; ch < channels; ++ch) {
            buffer.setSample(frame, ch, sample * 0.5f); // -6dB to avoid clipping
        }
        
        phase += phaseIncrement;
        if (phase >= 2.0f * M_PI) {
            phase -= 2.0f * M_PI;
        }
    }
    
    return buffer;
}

AudioBuffer createWhiteNoise(float duration, uint32_t sampleRate, uint32_t channels) {
    uint32_t frameCount = static_cast<uint32_t>(duration * sampleRate);
    AudioBuffer buffer(frameCount, channels, sampleRate);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            buffer.setSample(frame, ch, dist(gen) * 0.25f); // -12dB to avoid clipping
        }
    }
    
    return buffer;
}

AudioBuffer createPinkNoise(float duration, uint32_t sampleRate, uint32_t channels) {
    // Simplified pink noise generation using white noise filtering
    AudioBuffer whiteNoise = createWhiteNoise(duration, sampleRate, channels);
    
    // Apply simple IIR filter to approximate pink noise
    for (uint32_t ch = 0; ch < channels; ++ch) {
        float b0 = 0.99765f, b1 = -1.0f, b2 = 0.0f;
        float a1 = -0.99765f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        
        for (uint32_t frame = 0; frame < whiteNoise.getFrameCount(); ++frame) {
            float input = whiteNoise.getSample(frame, ch);
            float output = b0 * input + b1 * z1 + b2 * z2 - a1 * z1 - a2 * z2;
            
            z2 = z1;
            z1 = input;
            
            whiteNoise.setSample(frame, ch, output * 0.1f); // Scale down
        }
    }
    
    return whiteNoise;
}

bool buffersMatch(const AudioBuffer& a, const AudioBuffer& b, float tolerance) {
    if (a.getFrameCount() != b.getFrameCount() || 
        a.getChannelCount() != b.getChannelCount()) {
        return false;
    }
    
    for (uint32_t frame = 0; frame < a.getFrameCount(); ++frame) {
        for (uint32_t ch = 0; ch < a.getChannelCount(); ++ch) {
            float diff = std::abs(a.getSample(frame, ch) - b.getSample(frame, ch));
            if (diff > tolerance) {
                return false;
            }
        }
    }
    
    return true;
}

bool isValidSampleRate(uint32_t sampleRate) {
    return sampleRate >= 8000 && sampleRate <= 192000;
}

bool isValidChannelCount(uint32_t channels) {
    return channels >= 1 && channels <= 16;
}

bool isValidFrameCount(uint32_t frames) {
    return frames > 0 && frames <= (1024 * 1024 * 16); // 16M frames max
}

} // namespace AudioBufferUtils