//
//  AudioSystem.h
//  SuperTerminal Framework - Audio System
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>

#ifdef __OBJC__
@class AVAudioEngine;
@class AVAudioPlayerNode;
@class AVAudioMixerNode;
@class AVAudioEnvironmentNode;
@class AVAudioFile;
#else
struct AVAudioEngine;
struct AVAudioPlayerNode;
struct AVAudioMixerNode;
struct AVAudioEnvironmentNode;
struct AVAudioFile;
#endif

// Forward declarations
class CoreAudioEngine;
class SynthEngine;
namespace SuperTerminal {
    class MidiEngine;
    class ST_MusicPlayer;
}

// Audio command types for command queue integration
enum AudioCommandType {
    // Music Control
    AUDIO_LOAD_MUSIC = 1000,      // Start at 1000 to avoid conflicts
    AUDIO_PLAY_MUSIC,
    AUDIO_STOP_MUSIC,
    AUDIO_SET_MUSIC_VOLUME,
    
    // Sound Effects
    AUDIO_LOAD_SOUND,
    AUDIO_PLAY_SOUND,
    AUDIO_STOP_SOUND,
    
    // Real-time Synthesis
    AUDIO_SYNTH_GENERATE,
    AUDIO_SYNTH_PLAY,
    AUDIO_CREATE_OSCILLATOR,
    AUDIO_SET_OSC_FREQUENCY,
    AUDIO_SET_OSC_WAVEFORM,
    AUDIO_SET_OSC_VOLUME,
    AUDIO_DELETE_OSCILLATOR,
    
    // MIDI Control
    AUDIO_MIDI_LOAD_FILE,
    AUDIO_MIDI_PLAY_SEQUENCE,
    AUDIO_MIDI_STOP_SEQUENCE,
    AUDIO_MIDI_PLAY_NOTE,
    AUDIO_MIDI_STOP_NOTE,
    AUDIO_MIDI_PROGRAM_CHANGE,
    AUDIO_MIDI_CONTROL_CHANGE,
    
    // System Commands
    AUDIO_INITIALIZE,
    AUDIO_SHUTDOWN,
    AUDIO_FLUSH_COMMANDS,
    AUDIO_SYNC_POINT
};

// Audio command structure
struct AudioCommand {
    AudioCommandType type;
    uint32_t timestamp;     // Frame-accurate timing
    uint32_t target_id;     // Sound/oscillator/effect ID
    
    union {
        struct { 
            char filename[256]; 
            float volume; 
            bool loop; 
        } load_music;
        
        struct { 
            uint32_t music_id; 
            float volume; 
            bool loop; 
        } play_music;
        
        struct { 
            uint32_t music_id; 
            float volume; 
        } set_music_volume;
        
        struct { 
            char filename[256]; 
        } load_sound;
        
        struct { 
            uint32_t sound_id; 
            float volume; 
            float pitch; 
            float pan; 
        } play_sound;
        
        struct { 
            float frequency; 
            int waveform;  // 0=sine, 1=square, 2=sawtooth, 3=triangle
        } create_oscillator;
        
        struct { 
            uint32_t osc_id; 
            float frequency; 
        } set_frequency;
        
        struct { 
            uint32_t osc_id; 
            int waveform; 
        } set_waveform;
        
        struct { 
            uint32_t osc_id; 
            float volume; 
        } set_volume;
        
        struct { 
            char filename[256]; 
        } load_midi;
        
        struct { 
            uint32_t midi_id; 
            float tempo; 
        } play_midi;
        
        struct { 
            uint8_t channel; 
            uint8_t note; 
            uint8_t velocity; 
        } midi_note_on;
        
        struct { 
            uint8_t channel; 
            uint8_t note; 
        } midi_note_off;
        
        struct { 
            uint8_t channel; 
            uint8_t controller; 
            uint8_t value; 
        } midi_control_change;
        
        struct { 
            uint8_t channel; 
            uint8_t program; 
        } midi_program_change;
    };
};

// Audio configuration
struct AudioConfig {
    uint32_t sampleRate = 44100;
    uint32_t bufferSize = 512;      // Low latency
    uint32_t maxVoices = 64;        // Simultaneous sounds
    
    // Memory limits
    size_t maxCachedSounds = 100 * 1024 * 1024;  // 100MB
    size_t maxStreamingBuffers = 5 * 1024 * 1024; // 5MB
    
    // Threading
    uint32_t workerThreads = 6;     // Use 6 of 8 cores
    bool enableMultiCoreProcessing = true;
    
    // Quality settings
    bool enableSpatialAudio = true;
    bool enableEffects = true;
    
    // Platform-specific
    bool useAccelerateFramework = true;  // Apple Silicon optimization
};

// Waveform types for synthesis
enum WaveformType {
    WAVE_SINE = 0,
    WAVE_SQUARE = 1,
    WAVE_SAWTOOTH = 2,
    WAVE_TRIANGLE = 3,
    WAVE_NOISE = 4
};

// Audio asset information
struct AudioAsset {
    uint32_t id;
    std::string filename;
    bool loaded;
    size_t size;
    float duration;
};

// Main AudioSystem class
class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();
    
    // Initialization
    bool initialize(const AudioConfig& config = AudioConfig{});
    void shutdown();
    bool isInitialized() const { return initialized.load(); }
    
    // Command queue integration (called from main thread)
    void enqueueCommand(const AudioCommand& command);
    void processAudioCommands();  // Called from audio thread
    bool isQueueEmpty() const;
    void waitQueueEmpty();  // Like your wait_queue_empty()
    
    // Emergency shutdown detection (should be called from main thread)
    void checkEmergencyShutdown();
    
    // Memory-based sound loading
    uint32_t loadSoundFromBuffer(const float* samples, size_t sampleCount,
                                uint32_t sampleRate, uint32_t channels);
    bool loadSoundFromBuffer(const float* samples, size_t sampleCount,
                            uint32_t sampleRate, uint32_t channels, uint32_t sound_id);
    
    // Export sound to WAV bytes for saving to database
    bool exportSoundToWAVBytes(uint32_t sound_id, std::vector<uint8_t>& outWAVData);
    
    // Export sound to raw PCM bytes for saving to database
    bool exportSoundToPCMBytes(uint32_t sound_id, std::vector<uint8_t>& outPCMData);
    
    // Direct API (for C++ usage - these enqueue commands internally)
    
    // Music Control
    bool loadMusic(const std::string& filename, uint32_t music_id);
    void playMusic(uint32_t music_id, float volume = 1.0f, bool loop = true);
    void stopMusic(uint32_t music_id);
    void setMusicVolume(uint32_t music_id, float volume);
    
    // Sound Effects
    uint32_t loadSound(const std::string& filename);  // Returns auto-generated ID
    bool loadSound(const std::string& filename, uint32_t sound_id);
    void playSound(uint32_t sound_id, float volume = 1.0f, float pitch = 1.0f, float pan = 0.0f);
    void stopSound(uint32_t sound_id);
    
    // Real-time Synthesis
    uint32_t createOscillator(float frequency, WaveformType waveform = WAVE_SINE);
    void setOscillatorFrequency(uint32_t osc_id, float frequency);
    void setOscillatorWaveform(uint32_t osc_id, WaveformType waveform);
    void setOscillatorVolume(uint32_t osc_id, float volume);
    void deleteOscillator(uint32_t osc_id);
    
    // MIDI Support
    bool loadMIDI(const std::string& filename, uint32_t midi_id);
    void playMIDI(uint32_t midi_id, float tempo = 1.0f);
    void stopMIDI(uint32_t midi_id);
    void sendMIDINoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void sendMIDINoteOff(uint8_t channel, uint8_t note);
    void sendMIDIControlChange(uint8_t channel, uint8_t controller, uint8_t value);
    void sendMIDIProgramChange(uint8_t channel, uint8_t program);
    
    // MIDI Engine Access
    SuperTerminal::MidiEngine* getMidiEngine() { return midiEngine.get(); }
    SynthEngine* getSynthEngine() { return synthEngine.get(); }
    SuperTerminal::ST_MusicPlayer* getMusicPlayer() { return musicPlayer.get(); }
    
    // System Information
    AudioConfig getConfig() const { return config; }
    size_t getLoadedSoundCount() const;
    size_t getActiveVoiceCount() const;
    float getCPUUsage() const;
    
    // Asset Management
    std::vector<AudioAsset> getLoadedAssets() const;
    void unloadAsset(uint32_t asset_id);
    void clearCache();
    
private:
    // Core components
    std::unique_ptr<CoreAudioEngine> coreAudioEngine;
    std::unique_ptr<SynthEngine> synthEngine;
    std::unique_ptr<SuperTerminal::MidiEngine> midiEngine;
    std::unique_ptr<SuperTerminal::ST_MusicPlayer> musicPlayer;
    
    // Configuration
    AudioConfig config;
    std::atomic<bool> initialized{false};
    
    // Dual command queues for better performance
    std::queue<AudioCommand> effectsQueue;      // High priority, low latency
    std::queue<AudioCommand> musicQueue;        // Lower priority, buffered
    mutable std::mutex effectsQueueMutex;
    mutable std::mutex musicQueueMutex;
    std::condition_variable effectsQueueCondition;
    std::condition_variable musicQueueCondition;
    std::atomic<uint32_t> frameCounter{0};
    
    // Dual background processing threads
    std::unique_ptr<std::thread> effectsProcessingThread;
    std::unique_ptr<std::thread> musicProcessingThread;
    std::atomic<bool> stopEffectsProcessing{false};
    std::atomic<bool> stopMusicProcessing{false};
    
    // ID management
    std::atomic<uint32_t> nextSoundId{1};
    std::atomic<uint32_t> nextOscillatorId{1};
    std::atomic<uint32_t> nextMidiId{1};
    std::atomic<uint32_t> nextSequenceId{1};
    
    // Asset tracking
    std::unordered_map<uint32_t, AudioAsset> loadedAssets;
    mutable std::mutex assetsMutex;
    
    // Internal helpers
    uint32_t generateSoundId();
    uint32_t generateOscillatorId();
    uint32_t generateMidiId();
    void processCommand(const AudioCommand& command);
    void effectsProcessingThreadFunction();  // High-priority effects thread
    void musicProcessingThreadFunction();    // Lower-priority music thread
    void routeCommandToQueue(const AudioCommand& command);  // Route to appropriate queue
    
    // Thread safety
    mutable std::mutex systemMutex;
};

// Global instance (similar to your other systems)
extern std::unique_ptr<AudioSystem> g_audioSystem;

// C interface functions (for Lua bindings and C usage)
extern "C" {
    // System
    bool audio_initialize();
    void audio_shutdown();
    void audio_check_emergency_shutdown();
    bool audio_is_initialized();
    void audio_wait_queue_empty();
    
    // Music
    bool audio_load_music(const char* filename, uint32_t music_id);
    void audio_play_music(uint32_t music_id, float volume, bool loop);
    void audio_stop_music(uint32_t music_id);
    void audio_set_music_volume(uint32_t music_id, float volume);
    
    // Sound Effects
    uint32_t audio_load_sound(const char* filename);
    bool audio_load_sound_with_id(const char* filename, uint32_t sound_id);
    uint32_t audio_load_sound_from_buffer(const float* samples, size_t sampleCount, 
                                         uint32_t sampleRate, uint32_t channels);
    bool audio_load_sound_from_buffer_with_id(const float* samples, size_t sampleCount,
                                              uint32_t sampleRate, uint32_t channels,
                                              uint32_t sound_id);
    void audio_play_sound(uint32_t sound_id, float volume, float pitch, float pan);
    void audio_stop_sound(uint32_t sound_id);
    
    // Synthesis
    uint32_t audio_create_oscillator(float frequency, int waveform);
    void audio_set_oscillator_frequency(uint32_t osc_id, float frequency);
    void audio_set_oscillator_waveform(uint32_t osc_id, int waveform);
    void audio_set_oscillator_volume(uint32_t osc_id, float volume);
    void audio_delete_oscillator(uint32_t osc_id);
    
    // MIDI
    bool audio_load_midi(const char* filename, uint32_t midi_id);
    void audio_play_midi(uint32_t midi_id, float tempo);
    void audio_stop_midi(uint32_t midi_id);
    void audio_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
    void audio_midi_note_off(uint8_t channel, uint8_t note);
    void audio_midi_control_change(uint8_t channel, uint8_t controller, uint8_t value);
    void audio_midi_program_change(uint8_t channel, uint8_t program);
    
    // MIDI timing and synchronization
    void audio_midi_set_tempo(double bpm);
    void audio_midi_wait_beats(double beats);
    void audio_midi_wait_ticks(int ticks);
    void audio_midi_wait_for_completion();
    
    // High-level music functions (ABC notation)
    bool music_play(const char* abc_notation, const char* name, int tempo_bpm, int instrument);
    bool music_queue(const char* abc_notation, const char* name, int tempo_bpm, int instrument, bool loop);
    void music_stop();
    void music_pause();
    void music_resume();
    void music_clear_queue();
    bool music_is_playing();
    bool music_is_paused();
    int music_get_queue_size();
    const char* music_get_current_song_name();
    void music_set_volume(float volume);
    float music_get_volume();
    void music_set_tempo(float multiplier);
    float music_get_tempo();
    bool music_play_simple(const char* abc_notation);
    bool music_queue_simple(const char* abc_notation);
    const char* music_get_test_song(const char* song_name);
    
    // Wait-related functions removed - using ABC rests instead for cleaner musical pauses
    
    // Information
    size_t audio_get_loaded_sound_count();
    size_t audio_get_active_voice_count();
    float audio_get_cpu_usage();
}