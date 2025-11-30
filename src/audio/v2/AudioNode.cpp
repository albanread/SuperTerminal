//
//  AudioNode.cpp
//  SuperTerminal Framework - Audio Graph v2.0
//
//  Implementation of base audio processing node class
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "AudioNode.h"
#include "AudioBuffer.h"
#include <algorithm>
#include <chrono>
#include <iostream>


// === AudioNode Base Class Implementation ===

void AudioNode::connect(std::shared_ptr<AudioNode> destination) {
    if (!destination || destination.get() == this) {
        return; // Can't connect to null or self
    }
    
    std::lock_guard<std::mutex> lock(connectionMutex);
    
    // Check if already connected
    for (const auto& weakPtr : outputs) {
        if (auto existing = weakPtr.lock()) {
            if (existing == destination) {
                return; // Already connected
            }
        }
    }
    
    // Add to outputs
    outputs.push_back(destination);
    
    // Add to destination's inputs
    {
        std::lock_guard<std::mutex> destLock(destination->connectionMutex);
        destination->inputs.push_back(shared_from_this());
    }
    
    cleanupConnections();
}

void AudioNode::disconnect(std::shared_ptr<AudioNode> destination) {
    if (!destination) return;
    
    std::lock_guard<std::mutex> lock(connectionMutex);
    
    // Remove from outputs
    outputs.erase(
        std::remove_if(outputs.begin(), outputs.end(),
            [destination](const std::weak_ptr<AudioNode>& weakPtr) {
                auto ptr = weakPtr.lock();
                return !ptr || ptr == destination;
            }),
        outputs.end()
    );
    
    // Remove from destination's inputs
    {
        std::lock_guard<std::mutex> destLock(destination->connectionMutex);
        destination->inputs.erase(
            std::remove_if(destination->inputs.begin(), destination->inputs.end(),
                [this](const std::weak_ptr<AudioNode>& weakPtr) {
                    auto ptr = weakPtr.lock();
                    return !ptr || ptr.get() == this;
                }),
            destination->inputs.end()
        );
    }
}

void AudioNode::disconnectAll() {
    std::lock_guard<std::mutex> lock(connectionMutex);
    
    // Disconnect all outputs
    for (const auto& weakPtr : outputs) {
        if (auto output = weakPtr.lock()) {
            std::lock_guard<std::mutex> outputLock(output->connectionMutex);
            output->inputs.erase(
                std::remove_if(output->inputs.begin(), output->inputs.end(),
                    [this](const std::weak_ptr<AudioNode>& weakInput) {
                        auto ptr = weakInput.lock();
                        return !ptr || ptr.get() == this;
                    }),
                output->inputs.end()
            );
        }
    }
    
    // Disconnect all inputs
    for (const auto& weakPtr : inputs) {
        if (auto input = weakPtr.lock()) {
            std::lock_guard<std::mutex> inputLock(input->connectionMutex);
            input->outputs.erase(
                std::remove_if(input->outputs.begin(), input->outputs.end(),
                    [this](const std::weak_ptr<AudioNode>& weakOutput) {
                        auto ptr = weakOutput.lock();
                        return !ptr || ptr.get() == this;
                    }),
                input->outputs.end()
            );
        }
    }
    
    // Clear all connections
    outputs.clear();
    inputs.clear();
}

void AudioNode::setParameter(const std::string& name, float value) {
    // Try custom handler first
    if (handleSetParameter(name, value)) {
        return;
    }
    
    // Handle built-in parameters
    std::lock_guard<std::mutex> lock(parameterMutex);
    
    auto it = parameters.find(name);
    if (it != parameters.end()) {
        // Clamp to valid range
        float clampedValue = std::min(std::max(value, it->second.minValue), it->second.maxValue);
        Parameter& param = it->second;
        param.value = clampedValue;
        param.atomicValue.store(clampedValue);
    }
}

float AudioNode::getParameter(const std::string& name) const {
    // Try custom handler first
    float value;
    if (handleGetParameter(name, value)) {
        return value;
    }
    
    // Handle built-in parameters
    std::lock_guard<std::mutex> lock(parameterMutex);
    
    auto it = parameters.find(name);
    if (it != parameters.end()) {
        return it->second.atomicValue.load();
    }
    
    return 0.0f;
}

std::vector<std::string> AudioNode::getParameterNames() const {
    std::lock_guard<std::mutex> lock(parameterMutex);
    
    std::vector<std::string> names;
    names.reserve(parameters.size());
    
    for (const auto& pair : parameters) {
        names.push_back(pair.first);
    }
    
    return names;
}

void AudioNode::registerParameter(const std::string& name, float defaultValue, float minValue, float maxValue) {
    std::lock_guard<std::mutex> lock(parameterMutex);
    
    parameters[name] = Parameter(defaultValue, minValue, maxValue);
}

void AudioNode::applyVolumeAndBypass(const AudioBuffer& inputBuffer, AudioBuffer& outputBuffer) {
    float currentVolume = volume.load();
    bool isBypassed = bypass.load();
    
    if (isBypassed) {
        // Bypass: copy input to output
        outputBuffer.copyFrom(inputBuffer);
    } else if (currentVolume != 1.0f) {
        // Apply volume if not unity
        outputBuffer.applyGain(currentVolume);
    }
}

void AudioNode::cleanupConnections() {
    // Remove expired weak_ptr connections
    outputs.erase(
        std::remove_if(outputs.begin(), outputs.end(),
            [](const std::weak_ptr<AudioNode>& weakPtr) {
                return weakPtr.expired();
            }),
        outputs.end()
    );
    
    inputs.erase(
        std::remove_if(inputs.begin(), inputs.end(),
            [](const std::weak_ptr<AudioNode>& weakPtr) {
                return weakPtr.expired();
            }),
        inputs.end()
    );
}

// === AudioSourceNode Implementation ===

void AudioSourceNode::process(const AudioBuffer& inputBuffer, AudioBuffer& outputBuffer) {
    if (!getEnabledRef().load()) {
        outputBuffer.clear();
        return;
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Generate audio (implemented by derived classes)
    generateAudio(outputBuffer);
    
    // Apply volume and bypass
    AudioBuffer tempBuffer = outputBuffer; // For bypass comparison
    applyVolumeAndBypass(tempBuffer, outputBuffer);
    
    // Update CPU usage
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    float cpuPercent = (duration.count() / 1000000.0f) * 100.0f; // Convert to percentage
    updateCpuUsage(cpuPercent);
}

// === AudioEffectNode Implementation ===

void AudioEffectNode::process(const AudioBuffer& inputBuffer, AudioBuffer& outputBuffer) {
    if (!getEnabledRef().load()) {
        outputBuffer.copyFrom(inputBuffer);
        return;
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Process audio (implemented by derived classes)
    processAudio(inputBuffer, outputBuffer);
    
    // Apply volume and bypass
    applyVolumeAndBypass(inputBuffer, outputBuffer);
    
    // Update CPU usage
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    float cpuPercent = (duration.count() / 1000000.0f) * 100.0f;
    updateCpuUsage(cpuPercent);
}

// === AudioMixerNode Implementation ===

AudioMixerNode::AudioMixerNode(size_t numChannels) {
    channels.resize(numChannels);
    
    // Register mixer parameters
    registerParameter("master_volume", 1.0f, 0.0f, 2.0f);
    
    for (size_t i = 0; i < numChannels; ++i) {
        registerParameter("channel_" + std::to_string(i) + "_volume", 1.0f, 0.0f, 2.0f);
        registerParameter("channel_" + std::to_string(i) + "_pan", 0.0f, -1.0f, 1.0f);
    }
}

void AudioMixerNode::process(const AudioBuffer& inputBuffer, AudioBuffer& outputBuffer) {
    if (!getEnabledRef().load()) {
        outputBuffer.clear();
        return;
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Clear output buffer
    outputBuffer.clear();
    
    // Get all input connections
    std::vector<std::shared_ptr<AudioNode>> inputNodes;
    {
        std::lock_guard<std::mutex> lock(getConnectionMutexRef());
        for (const auto& weakInput : getInputsRef()) {
            if (auto input = weakInput.lock()) {
                inputNodes.push_back(input);
            }
        }
    }
    
    // Mix inputs
    AudioBuffer tempBuffer(outputBuffer.getFrameCount(), outputBuffer.getChannelCount(), outputBuffer.getSampleRate());
    
    for (size_t i = 0; i < inputNodes.size() && i < channels.size(); ++i) {
        // Process input node (this would normally be done by the audio graph)
        // For now, we'll assume the input buffer contains the processed audio
        
        std::lock_guard<std::mutex> channelLock(channelMutex);
        if (channels[i].enabled) {
            float channelVolume = channels[i].volume;
            float channelPan = channels[i].pan;
            
            // Copy input and apply channel settings
            tempBuffer.copyFrom(inputBuffer);
            tempBuffer.applyGain(channelVolume);
            
            if (outputBuffer.getChannelCount() >= 2) {
                tempBuffer.applyPan(channelPan);
            }
            
            // Mix into output
            outputBuffer.mixFrom(tempBuffer, 1.0f);
        }
    }
    
    // Apply master volume
    float masterVol = getParameter("master_volume");
    if (masterVol != 1.0f) {
        outputBuffer.applyGain(masterVol);
    }
    
    // Apply node volume and bypass
    AudioBuffer originalOutput = outputBuffer;
    applyVolumeAndBypass(originalOutput, outputBuffer);
    
    // Update CPU usage
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    float cpuPercent = (duration.count() / 1000000.0f) * 100.0f;
    updateCpuUsage(cpuPercent);
}

void AudioMixerNode::setChannelVolume(size_t channel, float volume) {
    if (channel < channels.size()) {
        std::lock_guard<std::mutex> lock(channelMutex);
        channels[channel].volume = std::min(std::max(volume, 0.0f), 2.0f);
        setParameter("channel_" + std::to_string(channel) + "_volume", volume);
    }
}

float AudioMixerNode::getChannelVolume(size_t channel) const {
    if (channel < channels.size()) {
        std::lock_guard<std::mutex> lock(channelMutex);
        return channels[channel].volume;
    }
    return 0.0f;
}

void AudioMixerNode::setChannelEnabled(size_t channel, bool enabled) {
    if (channel < channels.size()) {
        std::lock_guard<std::mutex> lock(channelMutex);
        channels[channel].enabled = enabled;
    }
}

bool AudioMixerNode::isChannelEnabled(size_t channel) const {
    if (channel < channels.size()) {
        std::lock_guard<std::mutex> lock(channelMutex);
        return channels[channel].enabled;
    }
    return false;
}

void AudioMixerNode::setChannelPan(size_t channel, float pan) {
    if (channel < channels.size()) {
        std::lock_guard<std::mutex> lock(channelMutex);
        channels[channel].pan = std::min(std::max(pan, -1.0f), 1.0f);
        setParameter("channel_" + std::to_string(channel) + "_pan", pan);
    }
}

float AudioMixerNode::getChannelPan(size_t channel) const {
    if (channel < channels.size()) {
        std::lock_guard<std::mutex> lock(channelMutex);
        return channels[channel].pan;
    }
    return 0.0f;
}

// AudioOutputNode Implementation

AudioOutputNode::AudioOutputNode() {
    registerParameter("master_volume", 1.0f, 0.0f, 2.0f);
    registerParameter("muted", 0.0f, 0.0f, 1.0f);
    
    // Initialize level meters (assume stereo for now)
    peakLevels.resize(2, 0.0f);
    rmsLevels.resize(2, 0.0f);
}

void AudioOutputNode::process(const AudioBuffer& inputBuffer, AudioBuffer& outputBuffer) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Copy input to output
    outputBuffer.copyFrom(inputBuffer);
    
    // Apply master volume
    float masterVol;
    bool isMuted;
    {
        std::lock_guard<std::mutex> lock(levelMeterMutex);
        masterVol = masterVolume;
        isMuted = muted;
    }
    
    if (masterVol != 1.0f) {
        outputBuffer.applyGain(masterVol);
    }
    
    // Apply mute
    if (isMuted) {
        outputBuffer.clear();
    }
    
    // Update level meters
    updateLevelMeters(outputBuffer);
    
    // Apply node volume and bypass (though bypass doesn't make sense for output)
    if (!getEnabledRef().load()) {
        outputBuffer.clear();
    }
    
    // Update CPU usage
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    float cpuPercent = (duration.count() / 1000000.0f) * 100.0f;
    updateCpuUsage(cpuPercent);
}

void AudioOutputNode::setMasterVolume(float volume) {
    std::lock_guard<std::mutex> lock(levelMeterMutex);
    masterVolume = std::min(std::max(volume, 0.0f), 2.0f);
    setParameter("master_volume", volume);
}

float AudioOutputNode::getMasterVolume() const {
    std::lock_guard<std::mutex> lock(levelMeterMutex);
    return masterVolume;
}

void AudioOutputNode::setMuted(bool mute) {
    std::lock_guard<std::mutex> lock(levelMeterMutex);
    muted = mute;
    setParameter("muted", mute ? 1.0f : 0.0f);
}

bool AudioOutputNode::isMuted() const {
    std::lock_guard<std::mutex> lock(levelMeterMutex);
    return muted;
}

float AudioOutputNode::getPeakLevel(size_t channel) const {
    std::lock_guard<std::mutex> lock(levelMeterMutex);
    if (channel < peakLevels.size()) {
        return peakLevels[channel];
    }
    return 0.0f;
}

float AudioOutputNode::getRMSLevel(size_t channel) const {
    std::lock_guard<std::mutex> lock(levelMeterMutex);
    if (channel < rmsLevels.size()) {
        return rmsLevels[channel];
    }
    return 0.0f;
}

bool AudioOutputNode::isClipping() const {
    std::lock_guard<std::mutex> lock(levelMeterMutex);
    return clipping;
}

void AudioOutputNode::updateLevelMeters(const AudioBuffer& buffer) {
    std::lock_guard<std::mutex> lock(levelMeterMutex);
    
    // Ensure we have enough level meters
    if (peakLevels.size() < buffer.getChannelCount()) {
        peakLevels.resize(buffer.getChannelCount(), 0.0f);
        rmsLevels.resize(buffer.getChannelCount(), 0.0f);
    }
    
    bool hasClipping = false;
    
    for (uint32_t ch = 0; ch < buffer.getChannelCount() && ch < peakLevels.size(); ++ch) {
        float peak = buffer.getPeakLevel(ch);
        float rms = buffer.getRMSLevel(ch);
        
        // Smooth the level meters (simple exponential smoothing)
        float currentPeak = peakLevels[ch];
        float currentRMS = rmsLevels[ch];
        
        float alpha = 0.1f; // Smoothing factor
        float newPeak = alpha * peak + (1.0f - alpha) * currentPeak;
        float newRMS = alpha * rms + (1.0f - alpha) * currentRMS;
        
        peakLevels[ch] = newPeak;
        rmsLevels[ch] = newRMS;
        
        if (peak >= 0.99f) {
            hasClipping = true;
        }
    }
    
    clipping = hasClipping;
}

// === AudioNodeUtils Implementation ===

namespace AudioNodeUtils {

bool connectNodes(std::shared_ptr<AudioNode> source, std::shared_ptr<AudioNode> destination) {
    if (!source || !destination) {
        return false;
    }
    
    try {
        source->connect(destination);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to connect nodes: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::shared_ptr<AudioNode>> createEffectChain(
    std::shared_ptr<AudioNode> source,
    const std::vector<std::shared_ptr<AudioNode>>& effects,
    std::shared_ptr<AudioNode> destination) {
    
    std::vector<std::shared_ptr<AudioNode>> chain;
    
    if (!source) {
        return chain;
    }
    
    chain.push_back(source);
    
    // Connect source to first effect (or destination if no effects)
    std::shared_ptr<AudioNode> current = source;
    
    for (const auto& effect : effects) {
        if (effect) {
            connectNodes(current, effect);
            chain.push_back(effect);
            current = effect;
        }
    }
    
    // Connect last node to destination
    if (destination) {
        connectNodes(current, destination);
        chain.push_back(destination);
    }
    
    return chain;
}

std::vector<std::shared_ptr<AudioNode>> findPath(
    std::shared_ptr<AudioNode> source,
    std::shared_ptr<AudioNode> destination) {
    
    std::vector<std::shared_ptr<AudioNode>> path;
    
    if (!source || !destination) {
        return path;
    }
    
    // Simple breadth-first search
    std::vector<std::shared_ptr<AudioNode>> visited;
    std::vector<std::vector<std::shared_ptr<AudioNode>>> queue;
    
    queue.push_back({source});
    visited.push_back(source);
    
    while (!queue.empty()) {
        auto currentPath = queue.front();
        queue.erase(queue.begin());
        
        auto current = currentPath.back();
        
        if (current == destination) {
            return currentPath;
        }
        
        // Check all outputs
        for (const auto& weakOutput : current->getOutputs()) {
            if (auto output = weakOutput.lock()) {
                // Check if already visited
                bool alreadyVisited = false;
                for (const auto& visitedNode : visited) {
                    if (visitedNode == output) {
                        alreadyVisited = true;
                        break;
                    }
                }
                
                if (!alreadyVisited) {
                    visited.push_back(output);
                    auto newPath = currentPath;
                    newPath.push_back(output);
                    queue.push_back(newPath);
                }
            }
        }
    }
    
    return path; // Empty if no path found
}

} // namespace AudioNodeUtils