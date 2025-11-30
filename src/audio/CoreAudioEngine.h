//
//  CoreAudioEngine.h
//  SuperTerminal Framework - Core Audio Engine
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#pragma once

#include "AudioSystem.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>

#ifdef __OBJC__
@class AVAudioEngine;
@class AVAudioPlayerNode;
@class AVAudioMixerNode;
@class AVAudioEnvironmentNode;
@class AVAudioFile;
@class AVAudioPCMBuffer;
@class AVAudioFormat;
#else
struct AVAudioEngine;
struct AVAudioPlayerNode;
struct AVAudioMixerNode;
struct AVAudioEnvironmentNode;
struct AVAudioFile;
struct AVAudioPCMBuffer;
struct AVAudioFormat;
#endif

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>

// Music track information
struct AudioMusicTrack {
    uint32_t id;
    std::string filename;
    AVAudioFile* audioFile;
    AVAudioPlayerNode* playerNode;
    bool isPlaying;
    bool isLooping;
    float volume;
    float duration;
    
    AudioMusicTrack() : id(0), audioFile(nullptr), playerNode(nullptr), 
                   isPlaying(false), isLooping(false), volume(1.0f), duration(0.0f) {}
};

// Sound effect information
struct SoundEffect {
    uint32_t id;
    std::string filename;
    AVAudioPCMBuffer* buffer;
    AVAudioFormat* format;
    float duration;
    size_t frameLength;
    
    SoundEffect() : id(0), buffer(nullptr), format(nullptr), duration(0.0f), frameLength(0) {}
};

// Active sound instance
struct ActiveSound {
    uint32_t instance_id;
    uint32_t sound_id;
    AVAudioPlayerNode* playerNode;
    float volume;
    float pitch;
    float pan;
    bool isPlaying;
    
    ActiveSound() : instance_id(0), sound_id(0), playerNode(nullptr),
                    volume(1.0f), pitch(1.0f), pan(0.0f), isPlaying(false) {}
};

// Core Audio Engine - handles native macOS audio playback
class CoreAudioEngine {
public:
    CoreAudioEngine();
    ~CoreAudioEngine();
    
    // Initialization
    bool initialize(const AudioConfig& config);
    void shutdown();
    bool isInitialized() const { return initialized.load(); }
    
    // Music playback (streaming, for long files)
    bool loadMusicFile(const std::string& filename, uint32_t music_id);
    void playMusic(uint32_t music_id, float volume = 1.0f, bool loop = true);
    void stopMusic(uint32_t music_id);
    void pauseMusic(uint32_t music_id);
    void resumeMusic(uint32_t music_id);
    void setMusicVolume(uint32_t music_id, float volume);
    bool isMusicPlaying(uint32_t music_id) const;
    float getMusicPosition(uint32_t music_id) const;
    void setMusicPosition(uint32_t music_id, float position);
    
    // Sound effects (cached in memory, for short files)
    bool loadSoundFile(const std::string& filename, uint32_t sound_id);
    bool loadSoundFromBuffer(const float* samples, size_t sampleCount, 
                            uint32_t sampleRate, uint32_t channels, uint32_t sound_id);
    uint32_t playSoundEffect(uint32_t sound_id, float volume = 1.0f, float pitch = 1.0f, float pan = 0.0f);
    
    // Export sound to WAV bytes for saving to database
    bool exportSoundToWAVBytes(uint32_t sound_id, std::vector<uint8_t>& outWAVData);
    
    // Export sound to raw PCM bytes (with simple header)
    bool exportSoundToPCMBytes(uint32_t sound_id, std::vector<uint8_t>& outPCMData);
    
    void stopSoundEffect(uint32_t instance_id);
    void stopAllSounds();
    
    // 3D Spatial Audio (future expansion)
    void setSoundPosition(uint32_t instance_id, float x, float y, float z);
    void setListenerPosition(float x, float y, float z);
    void setListenerOrientation(float forward_x, float forward_y, float forward_z,
                               float up_x, float up_y, float up_z);
    
    // System control
    void setMasterVolume(float volume);
    float getMasterVolume() const;
    void setMuted(bool muted);
    bool getMuted() const;
    
    // Information
    size_t getLoadedMusicCount() const;
    size_t getLoadedSoundCount() const;
    size_t getActiveSoundCount() const;
    std::vector<uint32_t> getLoadedMusicIds() const;
    std::vector<uint32_t> getLoadedSoundIds() const;
    
    // Memory management
    void unloadMusic(uint32_t music_id);
    void unloadSound(uint32_t sound_id);
    void clearMusicCache();
    void clearSoundCache();
    size_t getMemoryUsage() const;
    
    // Audio session management (macOS specific)
    bool configureAudioSession();
    void handleAudioInterruption(bool interrupted);
    void handleRouteChange();
    
private:
    // Core Audio components
    AVAudioEngine* audioEngine;
    AVAudioMixerNode* mainMixer;
    AVAudioEnvironmentNode* spatialMixer;  // For 3D audio
    AVAudioFormat* standardFormat;
    
    // Configuration
    AudioConfig config;
    std::atomic<bool> initialized{false};
    
    // Music tracks
    std::unordered_map<uint32_t, std::unique_ptr<AudioMusicTrack>> musicTracks;
    mutable std::mutex musicMutex;
    
    // Sound effects
    std::unordered_map<uint32_t, std::unique_ptr<SoundEffect>> soundEffects;
    mutable std::mutex soundsMutex;
    
    // Active sound instances
    std::unordered_map<uint32_t, std::unique_ptr<ActiveSound>> activeSounds;
    mutable std::mutex activesoundsMutex;
    std::atomic<uint32_t> nextInstanceId{1};
    
    // System state
    std::atomic<float> masterVolume{1.0f};
    std::atomic<bool> mutedState{false};
    
    // Internal helpers
    bool setupAudioEngine();
    void cleanupAudioEngine();
    
    // Music helpers
    AudioMusicTrack* getMusicTrack(uint32_t music_id);
    const AudioMusicTrack* getMusicTrack(uint32_t music_id) const;
    void cleanupMusicTrack(AudioMusicTrack* track);
    
    // Sound effect helpers
    SoundEffect* getSoundEffect(uint32_t sound_id);
    const SoundEffect* getSoundEffect(uint32_t sound_id) const;
    void cleanupSoundEffect(SoundEffect* sound);
    uint32_t generateInstanceId();
    void cleanupActiveSound(uint32_t instance_id);
    void cleanupFinishedSounds();
    
    // Audio file loading
    AVAudioFile* loadAudioFile(const std::string& filename);
    AVAudioPCMBuffer* loadAudioBuffer(const std::string& filename, AVAudioFormat** outFormat);
    
    // Node management
    AVAudioPlayerNode* createPlayerNode();
    void configurePlayerNode(AVAudioPlayerNode* node, float volume, float pitch, float pan);
    void releasePlayerNode(AVAudioPlayerNode* node);
    
    // Format conversion
    bool convertFormat(AVAudioPCMBuffer* sourceBuffer, AVAudioFormat* targetFormat,
                      AVAudioPCMBuffer** outBuffer);
    
    // Audio session callbacks
    static void audioInterruptionCallback(void* inUserData, UInt32 inInterruptionState);
    static void audioRouteChangeCallback(void* inUserData, AudioSessionPropertyID inPropertyID,
                                       UInt32 inPropertyValueSize, const void* inPropertyValue);
    
    // Thread safety
    mutable std::mutex systemMutex;
    
    // Memory tracking
    std::atomic<size_t> memoryUsage{0};
    void updateMemoryUsage();
};