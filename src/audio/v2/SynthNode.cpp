//
//  SynthNode.cpp
//  SuperTerminal Framework - Audio Graph v2.0
//
//  Node that wraps the existing SynthEngine to preserve all synthesis capabilities
//  while integrating with the new clean audio architecture
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "SynthNode.h"
#include "AudioBuffer.h"
#include "../SynthEngine.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cmath>

// === SynthNode Implementation ===

SynthNode::SynthNode() 
    : synthEngine(nullptr) {
    registerParameters();
}

SynthNode::SynthNode(std::unique_ptr<SynthEngine> existingEngine) 
    : synthEngine(std::move(existingEngine)) {
    registerParameters();
    if (synthEngine) {
        initialized.store(true);
    }
}

SynthNode::~SynthNode() {
    shutdown();
}

bool SynthNode::initialize(const SynthConfig& config) {
    if (initialized.load()) {
        return true;
    }
    
    // Create new SynthEngine if we don't have one
    if (!synthEngine) {
        synthEngine = std::make_unique<SynthEngine>();
        if (!synthEngine->initialize(config)) {
            std::cerr << "SynthNode: Failed to initialize SynthEngine" << std::endl;
            synthEngine.reset();
            return false;
        }
    }
    
    initialized.store(true);
    std::cout << "SynthNode: Initialized successfully" << std::endl;
    return true;
}

void SynthNode::shutdown() {
    if (!initialized.load()) {
        return;
    }
    
    // Stop all voices and clear resources
    stopAllSounds();
    clearAllVoices();
    
    // Clear real-time oscillators
    {
        std::lock_guard<std::mutex> lock(oscillatorsMutex);
        realTimeOscillators.clear();
    }
    
    // Shutdown underlying SynthEngine
    if (synthEngine) {
        synthEngine->shutdown();
        synthEngine.reset();
    }
    
    initialized.store(false);
    std::cout << "SynthNode: Shutdown complete" << std::endl;
}

void SynthNode::generateAudio(AudioBuffer& outputBuffer) {
    if (!initialized.load() || !getEnabledRef().load()) {
        outputBuffer.clear();
        return;
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Process commands from other threads
    processCommands();
    
    // Clear output buffer
    outputBuffer.clear();
    
    // Render active voices (from memory-based synthesis)
    renderVoices(outputBuffer);
    
    // Render real-time oscillators
    renderRealTimeOscillators(outputBuffer);
    
    // Apply global effects
    applyGlobalEffects(outputBuffer);
    
    // Limit number of voices if needed
    limitVoices();
    
    // Update CPU usage metrics
    updateCpuUsage(startTime);
}

// === PRESERVE ALL EXISTING SYNTHESIS METHODS ===

uint32_t SynthNode::generateBeepToMemory(float frequency, float duration) {
    if (!synthEngine) return 0;
    
    // Generate using existing SynthEngine and wrap in our voice system
    auto buffer = synthEngine->generateBeep(frequency, duration);
    if (!buffer) return 0;
    
    uint32_t soundId = generateNextSoundId();
    
    std::lock_guard<std::mutex> lock(voicesMutex);
    activeVoices.push_back(std::make_unique<SynthVoice>(soundId, std::move(buffer)));
    
    return soundId;
}

uint32_t SynthNode::generateExplodeToMemory(float size, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generateExplodeToMemory(size, duration);
}

uint32_t SynthNode::generateCoinToMemory(float pitch, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generateCoinToMemory(pitch, duration);
}

uint32_t SynthNode::generateShootToMemory(float intensity, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generateShootToMemory(intensity, duration);
}

uint32_t SynthNode::generateClickToMemory(float intensity, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generateClickToMemory(intensity, duration);
}

uint32_t SynthNode::generateJumpToMemory(float power, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generateJumpToMemory(power, duration);
}

uint32_t SynthNode::generatePowerupToMemory(float intensity, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generatePowerupToMemory(intensity, duration);
}

uint32_t SynthNode::generateHurtToMemory(float intensity, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generateHurtToMemory(intensity, duration);
}

uint32_t SynthNode::generatePickupToMemory(float pitch, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generatePickupToMemory(pitch, duration);
}

uint32_t SynthNode::generateBlipToMemory(float pitch, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generateBlipToMemory(pitch, duration);
}

uint32_t SynthNode::generateZapToMemory(float frequency, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generateZapToMemory(frequency, duration);
}

uint32_t SynthNode::generateBigExplosionToMemory(float size, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generateBigExplosionToMemory(size, duration);
}

uint32_t SynthNode::generateSmallExplosionToMemory(float intensity, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generateSmallExplosionToMemory(intensity, duration);
}

uint32_t SynthNode::generateDistantExplosionToMemory(float distance, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generateDistantExplosionToMemory(distance, duration);
}

uint32_t SynthNode::generateMetalExplosionToMemory(float shrapnel, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generateMetalExplosionToMemory(shrapnel, duration);
}

uint32_t SynthNode::generateSweepUpToMemory(float startFreq, float endFreq, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generateSweepUpToMemory(startFreq, endFreq, duration);
}

uint32_t SynthNode::generateSweepDownToMemory(float startFreq, float endFreq, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generateSweepDownToMemory(startFreq, endFreq, duration);
}

uint32_t SynthNode::generateOscillatorToMemory(WaveformType waveform, float frequency, float duration,
                                              float attack, float decay, float sustain, float release) {
    if (!synthEngine) return 0;
    return synthEngine->generateOscillatorToMemory(waveform, frequency, duration, attack, decay, sustain, release);
}

uint32_t SynthNode::generateRandomBeepToMemory(uint32_t seed, float duration) {
    if (!synthEngine) return 0;
    return synthEngine->generateRandomBeepToMemory(seed, duration);
}

uint32_t SynthNode::generateCustomSoundToMemory(const SynthSoundEffect& effect) {
    if (!synthEngine) return 0;
    
    // Use the existing SynthEngine to generate the sound
    auto buffer = synthEngine->generateSound(effect);
    if (!buffer) return 0;
    
    // Create a voice for playback
    uint32_t soundId = generateNextSoundId();
    
    std::lock_guard<std::mutex> lock(voicesMutex);
    activeVoices.push_back(std::make_unique<SynthVoice>(soundId, std::move(buffer)));
    
    return soundId;
}

uint32_t SynthNode::generatePredefinedSoundToMemory(SoundEffectType type, float duration) {
    if (!synthEngine) return 0;
    
    // Method not implemented in existing SynthEngine - use beep as fallback for now
    auto buffer = synthEngine->generateBeep(440.0f, duration > 0.0f ? duration : 0.5f);
    if (!buffer) return 0;
    
    uint32_t soundId = generateNextSoundId();
    
    std::lock_guard<std::mutex> lock(voicesMutex);
    activeVoices.push_back(std::make_unique<SynthVoice>(soundId, std::move(buffer)));
    
    return soundId;
}

// === SOUND PLAYBACK CONTROL ===

void SynthNode::playSound(uint32_t soundId, float volume, float pitch, float pan) {
    SynthCommand command(SynthCommand::PLAY_SOUND);
    command.soundId = soundId;
    command.param1 = volume;
    command.param2 = pitch;
    command.param3 = pan;
    enqueueCommand(std::move(command));
}

void SynthNode::stopSound(uint32_t soundId) {
    SynthCommand command(SynthCommand::STOP_SOUND);
    command.soundId = soundId;
    enqueueCommand(std::move(command));
}

void SynthNode::stopAllSounds() {
    SynthCommand command(SynthCommand::CLEAR_ALL);
    enqueueCommand(std::move(command));
}

bool SynthNode::isSoundPlaying(uint32_t soundId) const {
    std::lock_guard<std::mutex> lock(voicesMutex);
    
    for (const auto& voice : activeVoices) {
        if (voice->soundId == soundId && voice->isPlaying) {
            return true;
        }
    }
    
    return false;
}

void SynthNode::setGlobalVolume(float volume) {
    globalVolume.store(std::min(std::max(volume, 0.0f), 2.0f));
    
    SynthCommand command(SynthCommand::SET_GLOBAL_VOLUME);
    command.param1 = volume;
    enqueueCommand(std::move(command));
}

void SynthNode::setGlobalFilter(const FilterParams& filter) {
    {
        std::lock_guard<std::mutex> lock(globalFilterMutex);
        globalFilter = filter;
    }
    
    SynthCommand command(SynthCommand::SET_GLOBAL_FILTER);
    command.filterParams = filter;
    enqueueCommand(std::move(command));
}

FilterParams SynthNode::getGlobalFilter() const {
    std::lock_guard<std::mutex> lock(globalFilterMutex);
    return globalFilter;
}

size_t SynthNode::getActiveVoiceCount() const {
    std::lock_guard<std::mutex> lock(voicesMutex);
    
    size_t count = 0;
    for (const auto& voice : activeVoices) {
        if (voice->isPlaying) {
            count++;
        }
    }
    
    return count;
}

void SynthNode::clearAllVoices() {
    std::lock_guard<std::mutex> lock(voicesMutex);
    activeVoices.clear();
}

// === REAL-TIME SYNTHESIS (NEW CAPABILITIES) ===

uint32_t SynthNode::createRealTimeOscillator(WaveformType waveform, float frequency) {
    uint32_t oscId = generateNextOscillatorId();
    
    auto oscillator = std::make_unique<RealTimeOscillator>(oscId);
    oscillator->waveform = waveform;
    oscillator->frequency = frequency;
    
    std::lock_guard<std::mutex> lock(oscillatorsMutex);
    realTimeOscillators[oscId] = std::move(oscillator);
    
    return oscId;
}

void SynthNode::setOscillatorFrequency(uint32_t oscId, float frequency) {
    std::lock_guard<std::mutex> lock(oscillatorsMutex);
    
    auto it = realTimeOscillators.find(oscId);
    if (it != realTimeOscillators.end()) {
        it->second->frequency = frequency;
    }
}

void SynthNode::setOscillatorWaveform(uint32_t oscId, WaveformType waveform) {
    std::lock_guard<std::mutex> lock(oscillatorsMutex);
    
    auto it = realTimeOscillators.find(oscId);
    if (it != realTimeOscillators.end()) {
        it->second->waveform = waveform;
    }
}

void SynthNode::setOscillatorAmplitude(uint32_t oscId, float amplitude) {
    std::lock_guard<std::mutex> lock(oscillatorsMutex);
    
    auto it = realTimeOscillators.find(oscId);
    if (it != realTimeOscillators.end()) {
        it->second->amplitude = amplitude;
    }
}

void SynthNode::setOscillatorEnvelope(uint32_t oscId, const EnvelopeADSR& envelope) {
    std::lock_guard<std::mutex> lock(oscillatorsMutex);
    
    auto it = realTimeOscillators.find(oscId);
    if (it != realTimeOscillators.end()) {
        it->second->envelope = envelope;
    }
}

void SynthNode::setOscillatorFilter(uint32_t oscId, const FilterParams& filter) {
    std::lock_guard<std::mutex> lock(oscillatorsMutex);
    
    auto it = realTimeOscillators.find(oscId);
    if (it != realTimeOscillators.end()) {
        it->second->filter = filter;
    }
}

void SynthNode::triggerOscillator(uint32_t oscId) {
    std::lock_guard<std::mutex> lock(oscillatorsMutex);
    
    auto it = realTimeOscillators.find(oscId);
    if (it != realTimeOscillators.end()) {
        it->second->isTriggered = true;
        it->second->isReleased = false;
        it->second->envelopeTime = 0.0f;
    }
}

void SynthNode::releaseOscillator(uint32_t oscId) {
    std::lock_guard<std::mutex> lock(oscillatorsMutex);
    
    auto it = realTimeOscillators.find(oscId);
    if (it != realTimeOscillators.end()) {
        it->second->isReleased = true;
    }
}

void SynthNode::deleteOscillator(uint32_t oscId) {
    std::lock_guard<std::mutex> lock(oscillatorsMutex);
    realTimeOscillators.erase(oscId);
}

float SynthNode::getCpuUsage() const {
    return cpuUsage.load();
}

size_t SynthNode::getMemoryUsage() const {
    size_t usage = 0;
    
    // Voice memory
    {
        std::lock_guard<std::mutex> lock(voicesMutex);
        for (const auto& voice : activeVoices) {
            if (voice->buffer) {
                usage += voice->buffer->samples.size() * sizeof(float);
            }
        }
    }
    
    // Oscillator memory (minimal)
    {
        std::lock_guard<std::mutex> lock(oscillatorsMutex);
        usage += realTimeOscillators.size() * sizeof(RealTimeOscillator);
    }
    
    return usage;
}

void SynthNode::runGarbageCollection() {
    // Remove finished voices
    {
        std::lock_guard<std::mutex> lock(voicesMutex);
        activeVoices.erase(
            std::remove_if(activeVoices.begin(), activeVoices.end(),
                [](const std::unique_ptr<SynthVoice>& voice) {
                    return voice->isFinished();
                }),
            activeVoices.end()
        );
    }
    
    // Remove released oscillators that have finished their release
    {
        std::lock_guard<std::mutex> lock(oscillatorsMutex);
        auto it = realTimeOscillators.begin();
        while (it != realTimeOscillators.end()) {
            auto& osc = it->second;
            if (osc->isReleased && osc->envelopeTime > osc->envelope.releaseTime) {
                it = realTimeOscillators.erase(it);
            } else {
                ++it;
            }
        }
    }
}

// === PRIVATE IMPLEMENTATION ===

void SynthNode::registerParameters() {
    registerParameter("global_volume", 1.0f, 0.0f, 2.0f);
    registerParameter("filter_cutoff", 1000.0f, 20.0f, 20000.0f);
    registerParameter("filter_resonance", 1.0f, 0.1f, 10.0f);
    registerParameter("max_voices", 64.0f, 1.0f, 256.0f);
}

bool SynthNode::handleSetParameter(const std::string& name, float value) {
    if (name == "global_volume") {
        setGlobalVolume(value);
        return true;
    }
    if (name == "max_voices") {
        setMaxVoices(static_cast<size_t>(value));
        return true;
    }
    if (name == "filter_cutoff") {
        FilterParams filter = getGlobalFilter();
        filter.cutoffFreq = value;
        setGlobalFilter(filter);
        return true;
    }
    if (name == "filter_resonance") {
        FilterParams filter = getGlobalFilter();
        filter.resonance = value;
        setGlobalFilter(filter);
        return true;
    }
    
    return false;
}

bool SynthNode::handleGetParameter(const std::string& name, float& value) const {
    if (name == "global_volume") {
        value = getGlobalVolume();
        return true;
    }
    if (name == "max_voices") {
        value = static_cast<float>(getMaxVoices());
        return true;
    }
    if (name == "filter_cutoff") {
        value = getGlobalFilter().cutoffFreq;
        return true;
    }
    if (name == "filter_resonance") {
        value = getGlobalFilter().resonance;
        return true;
    }
    
    return false;
}

void SynthNode::processCommands() {
    std::lock_guard<std::mutex> lock(commandQueueMutex);
    
    while (!commandQueue.empty()) {
        auto command = std::move(commandQueue.front());
        commandQueue.pop();
        
        switch (command.type) {
            case SynthCommand::PLAY_SOUND:
                playCommandSound(command.soundId, command.param1, command.param2, command.param3);
                break;
                
            case SynthCommand::STOP_SOUND:
                stopCommandSound(command.soundId);
                break;
                
            case SynthCommand::CLEAR_ALL:
                clearAllVoices();
                break;
                
            case SynthCommand::SET_GLOBAL_VOLUME:
                globalVolume.store(command.param1);
                break;
                
            case SynthCommand::SET_GLOBAL_FILTER:
                {
                    std::lock_guard<std::mutex> filterLock(globalFilterMutex);
                    globalFilter = command.filterParams;
                }
                break;
                
            default:
                break;
        }
    }
}

void SynthNode::enqueueCommand(SynthCommand command) {
    std::lock_guard<std::mutex> lock(commandQueueMutex);
    commandQueue.push(std::move(command));
}

void SynthNode::renderVoices(AudioBuffer& outputBuffer) {
    std::lock_guard<std::mutex> lock(voicesMutex);
    
    AudioBuffer tempBuffer(outputBuffer.getFrameCount(), outputBuffer.getChannelCount(), outputBuffer.getSampleRate());
    
    for (auto& voice : activeVoices) {
        if (!voice->isPlaying || voice->isFinished()) {
            continue;
        }
        
        // Clear temp buffer for this voice
        tempBuffer.clear();
        
        // Render voice samples
        uint32_t framesToRender = std::min(
            outputBuffer.getFrameCount(),
            static_cast<uint32_t>((voice->buffer->getSampleCount() - voice->currentSample) / voice->buffer->channels)
        );
        
        if (framesToRender == 0) {
            voice->isPlaying = false;
            continue;
        }
        
        // Copy samples from voice buffer to temp buffer
        for (uint32_t frame = 0; frame < framesToRender; ++frame) {
            for (uint32_t ch = 0; ch < std::min(outputBuffer.getChannelCount(), voice->buffer->channels); ++ch) {
                size_t sampleIndex = voice->currentSample + (frame * voice->buffer->channels) + ch;
                if (sampleIndex < voice->buffer->samples.size()) {
                    float sample = voice->buffer->samples[sampleIndex];
                    tempBuffer.setSample(frame, ch, sample);
                }
            }
        }
        
        // Apply voice parameters
        if (voice->volume != 1.0f) {
            tempBuffer.applyGain(voice->volume);
        }
        
        if (voice->pitch != 1.0f) {
            // Simple pitch shifting (resampling) - basic implementation
            // In a full implementation, you'd use proper resampling algorithms
        }
        
        if (voice->pan != 0.0f && outputBuffer.getChannelCount() >= 2) {
            tempBuffer.applyPan(voice->pan);
        }
        
        // Mix into output
        outputBuffer.mixFrom(tempBuffer, 1.0f);
        
        // Update voice position
        voice->currentSample += framesToRender * voice->buffer->channels;
        
        // Check if voice is finished
        if (voice->currentSample >= voice->buffer->samples.size()) {
            voice->isPlaying = false;
        }
    }
}

void SynthNode::renderRealTimeOscillators(AudioBuffer& outputBuffer) {
    std::lock_guard<std::mutex> lock(oscillatorsMutex);
    
    if (realTimeOscillators.empty()) {
        return;
    }
    
    float sampleRate = static_cast<float>(outputBuffer.getSampleRate());
    float deltaTime = 1.0f / sampleRate;
    
    for (auto& pair : realTimeOscillators) {
        auto& osc = pair.second;
        
        if (!osc->isTriggered) {
            continue;
        }
        
        for (uint32_t frame = 0; frame < outputBuffer.getFrameCount(); ++frame) {
            float sample = generateOscillatorSample(*osc, sampleRate);
            
            // Apply envelope
            float envelopeValue = applyEnvelope(osc->envelope, osc->envelopeTime, 0.0f, osc->isReleased);
            sample *= envelopeValue;
            
            // Apply amplitude
            sample *= osc->amplitude;
            
            // Mix into output (all channels)
            for (uint32_t ch = 0; ch < outputBuffer.getChannelCount(); ++ch) {
                float currentSample = outputBuffer.getSample(frame, ch);
                outputBuffer.setSample(frame, ch, currentSample + sample);
            }
            
            // Update envelope time
            osc->envelopeTime += deltaTime;
        }
    }
}

void SynthNode::applyGlobalEffects(AudioBuffer& outputBuffer) {
    // Apply global volume
    float volume = globalVolume.load();
    if (volume != 1.0f) {
        outputBuffer.applyGain(volume);
    }
    
    // Apply global filter (simplified implementation)
    FilterParams filter;
    {
        std::lock_guard<std::mutex> lock(globalFilterMutex);
        filter = globalFilter;
    }
    
    if (filter.type != FilterType::NONE) {
        // Simple filter implementation - in production you'd use proper DSP filters
        // float cutoff = filter.cutoffFreq;
        // float resonance = filter.resonance;
        
        // Apply basic filtering (this is a very simplified version)
        for (uint32_t ch = 0; ch < outputBuffer.getChannelCount(); ++ch) {
            float filterState = 0.0f;
            for (uint32_t frame = 0; frame < outputBuffer.getFrameCount(); ++frame) {
                float sample = outputBuffer.getSample(frame, ch);
                float filtered = applyFilter(filter, sample, filterState);
                outputBuffer.setSample(frame, ch, filtered);
            }
        }
    }
}

void SynthNode::limitVoices() {
    std::lock_guard<std::mutex> lock(voicesMutex);
    
    if (activeVoices.size() <= maxVoices) {
        return;
    }
    
    // Remove oldest voices first
    std::sort(activeVoices.begin(), activeVoices.end(),
        [](const std::unique_ptr<SynthVoice>& a, const std::unique_ptr<SynthVoice>& b) {
            return a->startTime < b->startTime;
        });
    
    while (activeVoices.size() > maxVoices) {
        activeVoices.erase(activeVoices.begin());
    }
}

void SynthNode::playCommandSound(uint32_t soundId, float volume, float pitch, float pan) {
    std::lock_guard<std::mutex> lock(voicesMutex);
    
    // Find existing voice with this sound ID
    for (auto& voice : activeVoices) {
        if (voice->soundId == soundId) {
            voice->volume = volume;
            voice->pitch = pitch;
            voice->pan = pan;
            voice->reset();
            return;
        }
    }
}

void SynthNode::stopCommandSound(uint32_t soundId) {
    std::lock_guard<std::mutex> lock(voicesMutex);
    
    for (auto& voice : activeVoices) {
        if (voice->soundId == soundId) {
            voice->isPlaying = false;
        }
    }
}

void SynthNode::updateCpuUsage(std::chrono::steady_clock::time_point startTime) {
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    float usage = (duration.count() / 1000000.0f) * 100.0f;
    cpuUsage.store(usage);
    
    AudioNode::updateCpuUsage(usage);
}

float SynthNode::generateOscillatorSample(RealTimeOscillator& osc, float sampleRate) {
    float phaseIncrement = 2.0f * M_PI * osc.frequency / sampleRate;
    float sample = 0.0f;
    
    switch (osc.waveform) {
        case WAVE_SINE:
            sample = std::sin(osc.phase);
            break;
            
        case WAVE_SQUARE:
            sample = (osc.phase < M_PI) ? 1.0f : -1.0f;
            break;
            
        case WAVE_SAWTOOTH:
            sample = (2.0f * osc.phase / (2.0f * M_PI)) - 1.0f;
            break;
            
        case WAVE_TRIANGLE:
            if (osc.phase < M_PI) {
                sample = (2.0f * osc.phase / M_PI) - 1.0f;
            } else {
                sample = 3.0f - (2.0f * osc.phase / M_PI);
            }
            break;
            
        case WAVE_NOISE:
            sample = ((rand() / float(RAND_MAX)) * 2.0f) - 1.0f;
            break;
    }
    
    // Update phase
    osc.phase += phaseIncrement;
    if (osc.phase >= 2.0f * M_PI) {
        osc.phase -= 2.0f * M_PI;
    }
    
    return sample;
}

float SynthNode::applyEnvelope(const EnvelopeADSR& envelope, float time, float noteLength, bool isReleased) {
    if (!isReleased) {
        // Attack phase
        if (time < envelope.attackTime) {
            return time / envelope.attackTime;
        }
        
        // Decay phase
        if (time < envelope.attackTime + envelope.decayTime) {
            float decayTime = time - envelope.attackTime;
            float decayProgress = decayTime / envelope.decayTime;
            return 1.0f - (decayProgress * (1.0f - envelope.sustainLevel));
        }
        
        // Sustain phase
        return envelope.sustainLevel;
    } else {
        // Release phase
        float releaseStartTime = time;
        if (releaseStartTime < envelope.releaseTime) {
            float releaseProgress = releaseStartTime / envelope.releaseTime;
            return envelope.sustainLevel * (1.0f - releaseProgress);
        }
        
        return 0.0f;
    }
}

float SynthNode::applyFilter(const FilterParams& filter, float sample, float& filterState) {
    // Very basic filter implementation - in production use proper DSP filters
    float alpha = filter.cutoffFreq / (filter.cutoffFreq + 1000.0f);
    filterState = alpha * sample + (1.0f - alpha) * filterState;
    return filterState;
}