//
//  SynthNode.h
//  SuperTerminal Framework - Audio Graph v2.0
//
//  Node that wraps the existing SynthEngine to preserve all synthesis capabilities
//  while integrating with the new clean audio architecture
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

#include "AudioNode.h"
#include "../SynthEngine.h"
#include <memory>
#include <queue>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <chrono>

// Forward declarations
class AudioBuffer;
class SynthEngine;

// Command for synthesis operations (thread-safe communication)
struct SynthCommand {
    enum Type {
        GENERATE_SOUND,
        PLAY_SOUND,
        STOP_SOUND,
        CLEAR_ALL,
        SET_GLOBAL_VOLUME,
        SET_GLOBAL_FILTER,
        DELETE_SOUND
    };
    
    Type type;
    uint32_t soundId = 0;
    float param1 = 0.0f;
    float param2 = 0.0f;
    float param3 = 0.0f;
    float param4 = 0.0f;
    SoundEffectType effectType = SoundEffectType::BEEP;
    std::unique_ptr<SynthSoundEffect> customEffect;
    FilterParams filterParams;
    
    SynthCommand(Type t) : type(t) {}
};

// Active synthesis voice (playing sound)
struct SynthVoice {
    uint32_t soundId;
    std::unique_ptr<SynthAudioBuffer> buffer;
    size_t currentSample = 0;
    float volume = 1.0f;
    float pitch = 1.0f;
    float pan = 0.0f;
    bool isPlaying = true;
    std::chrono::steady_clock::time_point startTime;
    
    SynthVoice(uint32_t id, std::unique_ptr<SynthAudioBuffer> buf)
        : soundId(id), buffer(std::move(buf)), startTime(std::chrono::steady_clock::now()) {}
    
    bool isFinished() const {
        return !isPlaying || currentSample >= buffer->getSampleCount();
    }
    
    void reset() {
        currentSample = 0;
        isPlaying = true;
        startTime = std::chrono::steady_clock::now();
    }
};

// Node that integrates existing SynthEngine with new audio graph
class SynthNode : public AudioSourceNode {
public:
    SynthNode();
    explicit SynthNode(std::unique_ptr<SynthEngine> existingEngine);
    ~SynthNode();
    
    // AudioNode interface
    void generateAudio(AudioBuffer& outputBuffer) override;
    std::string getNodeType() const override { return "Synthesizer"; }
    
    // Initialize with existing or create new SynthEngine
    bool initialize(const SynthConfig& config = SynthConfig{});
    void shutdown();
    bool isInitialized() const { return initialized.load(); }
    
    // === PRESERVE ALL EXISTING SYNTHESIS METHODS ===
    
    // Memory-based sound generation (returns sound ID for immediate use)
    uint32_t generateBeepToMemory(float frequency = 800.0f, float duration = 0.2f);
    uint32_t generateExplodeToMemory(float size = 1.0f, float duration = 1.0f);
    uint32_t generateCoinToMemory(float pitch = 1.0f, float duration = 0.4f);
    uint32_t generateShootToMemory(float intensity = 1.0f, float duration = 0.2f);
    uint32_t generateClickToMemory(float intensity = 1.0f, float duration = 0.05f);
    uint32_t generateJumpToMemory(float power = 1.0f, float duration = 0.3f);
    uint32_t generatePowerupToMemory(float intensity = 1.0f, float duration = 0.8f);
    uint32_t generateHurtToMemory(float intensity = 1.0f, float duration = 0.4f);
    uint32_t generatePickupToMemory(float pitch = 1.0f, float duration = 0.25f);
    uint32_t generateBlipToMemory(float pitch = 1.0f, float duration = 0.1f);
    uint32_t generateZapToMemory(float frequency = 2000.0f, float duration = 0.15f);
    
    // Advanced explosion types
    uint32_t generateBigExplosionToMemory(float size = 1.0f, float duration = 2.0f);
    uint32_t generateSmallExplosionToMemory(float intensity = 1.0f, float duration = 0.5f);
    uint32_t generateDistantExplosionToMemory(float distance = 1.0f, float duration = 1.5f);
    uint32_t generateMetalExplosionToMemory(float shrapnel = 1.0f, float duration = 1.2f);
    
    // Sweep effects
    uint32_t generateSweepUpToMemory(float startFreq = 200.0f, float endFreq = 2000.0f, float duration = 0.5f);
    uint32_t generateSweepDownToMemory(float startFreq = 2000.0f, float endFreq = 200.0f, float duration = 0.5f);
    
    // Custom synthesis
    uint32_t generateOscillatorToMemory(WaveformType waveform, float frequency, float duration,
                                       float attack = 0.01f, float decay = 0.1f, 
                                       float sustain = 0.7f, float release = 0.2f);
    
    // Random generation
    uint32_t generateRandomBeepToMemory(uint32_t seed = 0, float duration = 0.3f);
    
    // Custom sound effects
    uint32_t generateCustomSoundToMemory(const SynthSoundEffect& effect);
    uint32_t generatePredefinedSoundToMemory(SoundEffectType type, float duration = 0.0f);
    
    // === SOUND PLAYBACK CONTROL ===
    
    // Play generated sounds (non-blocking)
    void playSound(uint32_t soundId, float volume = 1.0f, float pitch = 1.0f, float pan = 0.0f);
    void stopSound(uint32_t soundId);
    void stopAllSounds();
    bool isSoundPlaying(uint32_t soundId) const;
    
    // Global synthesis controls
    void setGlobalVolume(float volume);
    float getGlobalVolume() const { return globalVolume.load(); }
    void setGlobalFilter(const FilterParams& filter);
    FilterParams getGlobalFilter() const;
    
    // Voice management
    size_t getActiveVoiceCount() const;
    size_t getMaxVoices() const { return maxVoices; }
    void setMaxVoices(size_t max) { maxVoices = max; }
    void clearAllVoices();
    
    // === REAL-TIME SYNTHESIS (NEW CAPABILITIES) ===
    
    // Real-time oscillator control
    uint32_t createRealTimeOscillator(WaveformType waveform = WAVE_SINE, float frequency = 440.0f);
    void setOscillatorFrequency(uint32_t oscId, float frequency);
    void setOscillatorWaveform(uint32_t oscId, WaveformType waveform);
    void setOscillatorAmplitude(uint32_t oscId, float amplitude);
    void setOscillatorEnvelope(uint32_t oscId, const EnvelopeADSR& envelope);
    void setOscillatorFilter(uint32_t oscId, const FilterParams& filter);
    void triggerOscillator(uint32_t oscId);
    void releaseOscillator(uint32_t oscId);
    void deleteOscillator(uint32_t oscId);
    
    // Performance and monitoring
    float getCpuUsage() const;
    size_t getMemoryUsage() const;
    void runGarbageCollection();
    
    // Direct access to underlying SynthEngine (for advanced users)
    SynthEngine* getSynthEngine() { return synthEngine.get(); }
    const SynthEngine* getSynthEngine() const { return synthEngine.get(); }
    
protected:
    // AudioNode parameter handling
    bool handleSetParameter(const std::string& name, float value) override;
    bool handleGetParameter(const std::string& name, float& value) const override;
    
    // Internal initialization
    void registerParameters();
    
    // Command handling helpers
    void playCommandSound(uint32_t soundId, float volume, float pitch, float pan);
    void stopCommandSound(uint32_t soundId);
    
private:
    // Core synthesis engine (preserved existing functionality)
    std::unique_ptr<SynthEngine> synthEngine;
    std::atomic<bool> initialized{false};
    
    // Thread-safe command processing
    std::queue<SynthCommand> commandQueue;
    mutable std::mutex commandQueueMutex;
    void processCommands();
    void enqueueCommand(SynthCommand command);
    
    // Voice management
    std::vector<std::unique_ptr<SynthVoice>> activeVoices;
    mutable std::mutex voicesMutex;
    std::atomic<uint32_t> nextSoundId{1};
    size_t maxVoices = 64;
    
    // Real-time oscillators
    struct RealTimeOscillator {
        uint32_t id;
        WaveformType waveform = WAVE_SINE;
        float frequency = 440.0f;
        float amplitude = 1.0f;
        float phase = 0.0f;
        EnvelopeADSR envelope;
        FilterParams filter;
        bool isTriggered = false;
        bool isReleased = false;
        float envelopeTime = 0.0f;
        
        RealTimeOscillator(uint32_t oscId) : id(oscId) {}
    };
    
    std::unordered_map<uint32_t, std::unique_ptr<RealTimeOscillator>> realTimeOscillators;
    mutable std::mutex oscillatorsMutex;
    std::atomic<uint32_t> nextOscillatorId{1};
    
    // Global state
    std::atomic<float> globalVolume{1.0f};
    FilterParams globalFilter;
    mutable std::mutex globalFilterMutex;
    
    // Performance monitoring
    std::chrono::steady_clock::time_point lastProcessTime;
    std::atomic<float> cpuUsage{0.0f};
    void updateCpuUsage(std::chrono::steady_clock::time_point startTime);
    
    // Audio processing helpers
    void renderVoices(AudioBuffer& outputBuffer);
    void renderRealTimeOscillators(AudioBuffer& outputBuffer);
    void applyGlobalEffects(AudioBuffer& outputBuffer);
    void limitVoices();  // Remove oldest voices if over limit
    
    // Utility functions
    uint32_t generateNextSoundId() { return nextSoundId.fetch_add(1); }
    uint32_t generateNextOscillatorId() { return nextOscillatorId.fetch_add(1); }
    
    // Sample generation for real-time oscillators
    float generateOscillatorSample(RealTimeOscillator& osc, float sampleRate);
    float applyEnvelope(const EnvelopeADSR& envelope, float time, float noteLength, bool isReleased);
    float applyFilter(const FilterParams& filter, float sample, float& filterState);
};

// Factory function for easy creation
inline std::shared_ptr<SynthNode> createSynthNode(const SynthConfig& config = SynthConfig{}) {
    auto node = std::make_shared<SynthNode>();
    if (node->initialize(config)) {
        return node;
    }
    return nullptr;
}

// Factory function to wrap existing SynthEngine
inline std::shared_ptr<SynthNode> createSynthNodeFromEngine(std::unique_ptr<SynthEngine> engine) {
    return std::make_shared<SynthNode>(std::move(engine));
}