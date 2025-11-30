//
//  AudioNode.h
//  SuperTerminal Framework - Audio Graph v2.0
//
//  Base class for all audio processing nodes in the new clean architecture
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

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <algorithm>

// Forward declarations
class AudioBuffer;
class AudioGraph;

// Audio processing node interface
class AudioNode : public std::enable_shared_from_this<AudioNode> {
public:
    virtual ~AudioNode() = default;
    
    // Core processing - called from audio thread
    virtual void process(const AudioBuffer& inputBuffer, AudioBuffer& outputBuffer) = 0;
    
    // Node graph connections
    virtual void connect(std::shared_ptr<AudioNode> destination);
    virtual void disconnect(std::shared_ptr<AudioNode> destination);
    virtual void disconnectAll();
    
    // Parameter control (thread-safe)
    virtual void setParameter(const std::string& name, float value);
    virtual float getParameter(const std::string& name) const;
    virtual std::vector<std::string> getParameterNames() const;
    
    // Node control
    void setEnabled(bool enabled) { this->enabled.store(enabled); }
    bool isEnabled() const { return enabled.load(); }
    
    void setVolume(float volume) { this->volume.store(volume); }
    float getVolume() const { return volume.load(); }
    
    void setBypass(bool bypass) { this->bypass.store(bypass); }
    bool isBypassed() const { return bypass.load(); }
    
    // Node information
    virtual std::string getNodeType() const = 0;
    virtual std::string getNodeName() const { return nodeName; }
    virtual void setNodeName(const std::string& name) { nodeName = name; }
    
    // Connection information
    size_t getInputCount() const { return inputs.size(); }
    size_t getOutputCount() const { return outputs.size(); }
    std::vector<std::weak_ptr<AudioNode>> getInputs() const { return inputs; }
    std::vector<std::weak_ptr<AudioNode>> getOutputs() const { return outputs; }
    
    // CPU usage monitoring
    float getCpuUsage() const { return cpuUsage.load(); }
    
protected:
    // Override these for custom parameter handling
    virtual bool handleSetParameter(const std::string& name, float value) { return false; }
    virtual bool handleGetParameter(const std::string& name, float& value) const { return false; }
    
    // Utility for parameter management
    void registerParameter(const std::string& name, float defaultValue, float minValue, float maxValue);
    void updateCpuUsage(float usage) { cpuUsage.store(usage); }
    
    // Apply volume and bypass to output buffer
    void applyVolumeAndBypass(const AudioBuffer& inputBuffer, AudioBuffer& outputBuffer);

protected:
    // Protected access for derived classes
    std::atomic<bool>& getEnabledRef() { return enabled; }
    std::mutex& getConnectionMutexRef() { return connectionMutex; }
    std::vector<std::weak_ptr<AudioNode>>& getInputsRef() { return inputs; }
    
private:
    // Connection management
    std::vector<std::weak_ptr<AudioNode>> inputs;
    std::vector<std::weak_ptr<AudioNode>> outputs;
    mutable std::mutex connectionMutex;
    
    // Node parameters (thread-safe)
    struct Parameter {
        float value;
        float minValue;
        float maxValue;
        std::atomic<float> atomicValue;
        
        Parameter(float val = 0.0f, float minVal = 0.0f, float maxVal = 1.0f)
            : value(val), minValue(minVal), maxValue(maxVal), atomicValue(val) {}
        
        // Make it copyable and movable
        Parameter(const Parameter& other) 
            : value(other.value), minValue(other.minValue), maxValue(other.maxValue), atomicValue(other.atomicValue.load()) {}
        
        Parameter& operator=(const Parameter& other) {
            if (this != &other) {
                value = other.value;
                minValue = other.minValue;
                maxValue = other.maxValue;
                atomicValue.store(other.atomicValue.load());
            }
            return *this;
        }
    };
    std::unordered_map<std::string, Parameter> parameters;
    mutable std::mutex parameterMutex;
    
    // Node state
    std::atomic<bool> enabled{true};
    std::atomic<float> volume{1.0f};
    std::atomic<bool> bypass{false};
    std::atomic<float> cpuUsage{0.0f};
    std::string nodeName = "AudioNode";
    
    // Remove expired weak_ptr connections
    void cleanupConnections();
};

// Specialized base classes for common node types

// Source node (generates audio, no inputs)
class AudioSourceNode : public AudioNode {
public:
    void process(const AudioBuffer& inputBuffer, AudioBuffer& outputBuffer) override final;
    
protected:
    // Override this to generate audio
    virtual void generateAudio(AudioBuffer& outputBuffer) = 0;
};

// Effect node (processes audio, 1 input -> 1 output)
class AudioEffectNode : public AudioNode {
public:
    void process(const AudioBuffer& inputBuffer, AudioBuffer& outputBuffer) override final;
    
protected:
    // Override this to process audio
    virtual void processAudio(const AudioBuffer& inputBuffer, AudioBuffer& outputBuffer) = 0;
};

// Mixer node (combines multiple inputs into one output)
class AudioMixerNode : public AudioNode {
public:
    AudioMixerNode(size_t numChannels = 2);
    
    void process(const AudioBuffer& inputBuffer, AudioBuffer& outputBuffer) override;
    std::string getNodeType() const override { return "Mixer"; }
    
    // Channel control
    void setChannelVolume(size_t channel, float volume);
    float getChannelVolume(size_t channel) const;
    void setChannelEnabled(size_t channel, bool enabled);
    bool isChannelEnabled(size_t channel) const;
    void setChannelPan(size_t channel, float pan);  // -1.0 (left) to 1.0 (right)
    float getChannelPan(size_t channel) const;
    
private:
    struct MixerChannel {
        float volume = 1.0f;
        bool enabled = true;
        float pan = 0.0f;  // Center
        
        MixerChannel() = default;
        MixerChannel(const MixerChannel& other) 
            : volume(other.volume), enabled(other.enabled), pan(other.pan) {}
        MixerChannel& operator=(const MixerChannel& other) {
            if (this != &other) {
                volume = other.volume;
                enabled = other.enabled;
                pan = other.pan;
            }
            return *this;
        }
    };
    
    std::vector<MixerChannel> channels;
    mutable std::mutex channelMutex;
};

// Output node (final destination, typically connects to audio driver)
class AudioOutputNode : public AudioNode {
public:
    AudioOutputNode();
    
    void process(const AudioBuffer& inputBuffer, AudioBuffer& outputBuffer) override;
    std::string getNodeType() const override { return "Output"; }
    
    // Master output controls
    void setMasterVolume(float volume);
    float getMasterVolume() const;
    void setMuted(bool muted);
    bool isMuted() const;
    
    // Output monitoring
    float getPeakLevel(size_t channel) const;
    float getRMSLevel(size_t channel) const;
    bool isClipping() const;
    
private:
    float masterVolume = 1.0f;
    bool muted = false;
    
    // Level monitoring (smoothed) - protected by mutex
    std::vector<float> peakLevels;
    std::vector<float> rmsLevels;
    bool clipping = false;
    mutable std::mutex levelMeterMutex;
    
    void updateLevelMeters(const AudioBuffer& buffer);
};

// Utility functions
namespace AudioNodeUtils {
    // Helper to connect nodes with error checking
    bool connectNodes(std::shared_ptr<AudioNode> source, std::shared_ptr<AudioNode> destination);
    
    // Helper to create common node chains
    std::vector<std::shared_ptr<AudioNode>> createEffectChain(
        std::shared_ptr<AudioNode> source,
        const std::vector<std::shared_ptr<AudioNode>>& effects,
        std::shared_ptr<AudioNode> destination
    );
    
    // Helper to find shortest path between nodes (for debugging)
    std::vector<std::shared_ptr<AudioNode>> findPath(
        std::shared_ptr<AudioNode> source,
        std::shared_ptr<AudioNode> destination
    );
}