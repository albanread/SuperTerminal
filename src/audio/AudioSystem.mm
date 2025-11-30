//
//  AudioSystem.mm
//  SuperTerminal Framework - Audio System Implementation
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "AudioSystem.h"
#include "CoreAudioEngine.h"
#include "SynthEngine.h"
#include "MidiEngine.h"
#include "MusicPlayer.h"
#include "../GlobalShutdown.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace SuperTerminal;

// Global instance
std::unique_ptr<AudioSystem> g_audioSystem = nullptr;

// AudioSystem Implementation

AudioSystem::AudioSystem()
    : coreAudioEngine(nullptr)
    , synthEngine(nullptr)
    , midiEngine(nullptr)
    , musicPlayer(nullptr)
    , initialized(false)
    , frameCounter(0)
    , nextSoundId(1)
    , nextOscillatorId(1)
    , nextMidiId(1)
    , nextSequenceId(1)
{
    std::cout << "AudioSystem: Constructor called" << std::endl;
}

AudioSystem::~AudioSystem() {
    if (initialized.load()) {
        shutdown();
    }
    std::cout << "AudioSystem: Destructor called" << std::endl;
}

bool AudioSystem::initialize(const AudioConfig& audioConfig) {
    std::lock_guard<std::mutex> lock(systemMutex);

    if (initialized) {
        std::cout << "AudioSystem: Already initialized" << std::endl;
        return true;
    }

    // Register as active subsystem for shutdown coordination
    register_active_subsystem();

    std::cout << "AudioSystem: Initializing..." << std::endl;

    // Store configuration
    config = audioConfig;

    // Initialize Core Audio engine
    coreAudioEngine = std::make_unique<::CoreAudioEngine>();
    if (!coreAudioEngine->initialize(config)) {
        std::cerr << "AudioSystem: Failed to initialize Core Audio engine" << std::endl;
        coreAudioEngine.reset();
        return false;
    }

    // Initialize synthesis engine
    synthEngine = std::make_unique<::SynthEngine>();
    if (!synthEngine->initialize()) {
        std::cerr << "AudioSystem: Failed to initialize synthesis engine" << std::endl;
        coreAudioEngine.reset();
        return false;
    }

    // Initialize MIDI engine
    midiEngine = std::make_unique<SuperTerminal::MidiEngine>();
    if (!midiEngine->initialize(coreAudioEngine.get())) {
        std::cerr << "AudioSystem: Failed to initialize MIDI engine" << std::endl;
        synthEngine.reset();
        coreAudioEngine.reset();
        return false;
    }

    // Initialize Music Player with both MIDI and Synth engines
    musicPlayer = std::make_unique<SuperTerminal::ST_MusicPlayer>();
    if (!musicPlayer->initialize(midiEngine.get(), synthEngine.get())) {
        std::cerr << "AudioSystem: Failed to initialize Music Player" << std::endl;
        midiEngine.reset();
        synthEngine.reset();
        coreAudioEngine.reset();
        return false;
    }

    // Start dual background processing threads
    stopEffectsProcessing.store(false);
    stopMusicProcessing.store(false);
    effectsProcessingThread = std::make_unique<std::thread>(&AudioSystem::effectsProcessingThreadFunction, this);
    musicProcessingThread = std::make_unique<std::thread>(&AudioSystem::musicProcessingThreadFunction, this);

    initialized.store(true);
    std::cout << "AudioSystem: Initialization complete" << std::endl;
    return true;
}

void AudioSystem::checkEmergencyShutdown() {
    // Check if emergency shutdown is requested and we haven't shut down yet
    if (is_emergency_shutdown_requested() && initialized.load()) {
        std::cout << "AudioSystem: Emergency shutdown detected, initiating shutdown..." << std::endl;
        shutdown();
    }
}

void AudioSystem::shutdown() {
    std::lock_guard<std::mutex> lock(systemMutex);

    if (!initialized.load()) {
        return;
    }

    std::cout << "AudioSystem: Shutting down..." << std::endl;

    // Stop both background processing threads
    stopEffectsProcessing.store(true);
    stopMusicProcessing.store(true);
    effectsQueueCondition.notify_all();
    musicQueueCondition.notify_all();

    if (effectsProcessingThread && effectsProcessingThread->joinable()) {
        effectsProcessingThread->join();
    }
    effectsProcessingThread.reset();

    if (musicProcessingThread && musicProcessingThread->joinable()) {
        musicProcessingThread->join();
    }
    musicProcessingThread.reset();

    // Clear both command queues
    {
        std::lock_guard<std::mutex> effectsLock(effectsQueueMutex);
        while (!effectsQueue.empty()) {
            effectsQueue.pop();
        }
    }
    {
        std::lock_guard<std::mutex> musicLock(musicQueueMutex);
        while (!musicQueue.empty()) {
            musicQueue.pop();
        }
    }

    // Shutdown components in reverse order
    if (musicPlayer) {
        musicPlayer->shutdown();
        musicPlayer.reset();
    }

    if (midiEngine) {
        midiEngine->shutdown();
        midiEngine.reset();
    }

    if (synthEngine) {
        synthEngine->shutdown();
        synthEngine.reset();
    }

    if (coreAudioEngine) {
        coreAudioEngine->shutdown();
        coreAudioEngine.reset();
    }

    // Clear assets
    {
        std::lock_guard<std::mutex> assetsLock(assetsMutex);
        loadedAssets.clear();
    }

    initialized.store(false);

    // Unregister from shutdown system
    unregister_active_subsystem();

    std::cout << "AudioSystem: Shutdown complete" << std::endl;
}

void AudioSystem::enqueueCommand(const AudioCommand& command) {
    if (!initialized.load()) {
        std::cerr << "AudioSystem: Cannot enqueue command - system not initialized" << std::endl;
        return;
    }

    // Add timestamp
    AudioCommand timestampedCommand = command;
    timestampedCommand.timestamp = frameCounter.load();

    // Route to appropriate queue
    routeCommandToQueue(timestampedCommand);
}

void AudioSystem::processAudioCommands() {
    if (!initialized.load()) {
        return;
    }

    std::queue<AudioCommand> effectsToProcess;
    std::queue<AudioCommand> musicToProcess;

    // Extract commands from both queues
    {
        std::lock_guard<std::mutex> effectsLock(effectsQueueMutex);
        effectsToProcess.swap(effectsQueue);
    }
    {
        std::lock_guard<std::mutex> musicLock(musicQueueMutex);
        musicToProcess.swap(musicQueue);
    }

    // Process effects commands first (higher priority)
    while (!effectsToProcess.empty()) {
        const AudioCommand& command = effectsToProcess.front();
        processCommand(command);
        effectsToProcess.pop();
    }

    // Process music commands
    while (!musicToProcess.empty()) {
        const AudioCommand& command = musicToProcess.front();
        processCommand(command);
        musicToProcess.pop();
    }

    // Increment frame counter
    frameCounter.fetch_add(1);
}

bool AudioSystem::isQueueEmpty() const {
    std::lock_guard<std::mutex> effectsLock(effectsQueueMutex);
    std::lock_guard<std::mutex> musicLock(musicQueueMutex);
    return effectsQueue.empty() && musicQueue.empty();
}

void AudioSystem::waitQueueEmpty() {
    const int maxWaitMs = 10000;  // 10 seconds max wait
    const int sleepMs = 10;       // Check every 10ms
    int waitedMs = 0;

    // First wait for command queue to be empty
    while (!isQueueEmpty() && waitedMs < maxWaitMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        waitedMs += sleepMs;
    }

    // Then wait for actual audio playback to complete
    while (getActiveVoiceCount() > 0 && waitedMs < maxWaitMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        waitedMs += sleepMs;
    }

    if (waitedMs >= maxWaitMs) {
        std::cerr << "AudioSystem: Warning - waitQueueEmpty() timed out after " << maxWaitMs << "ms" << std::endl;
        std::cerr << "  Effects queue empty: " << effectsQueue.empty() << std::endl;
        std::cerr << "  Music queue empty: " << musicQueue.empty() << std::endl;
        std::cerr << "  Active voices: " << getActiveVoiceCount() << std::endl;
    }
}

void AudioSystem::routeCommandToQueue(const AudioCommand& command) {
    // Route commands to appropriate queue based on type
    bool isEffectCommand = false;

    switch (command.type) {
        // Sound effects - high priority, low latency
        case AUDIO_LOAD_SOUND:
        case AUDIO_PLAY_SOUND:
        case AUDIO_STOP_SOUND:
        case AUDIO_CREATE_OSCILLATOR:
        case AUDIO_SET_OSC_FREQUENCY:
        case AUDIO_SET_OSC_WAVEFORM:
        case AUDIO_SET_OSC_VOLUME:
        case AUDIO_DELETE_OSCILLATOR:
            isEffectCommand = true;
            break;

        // Music - lower priority, buffered
        case AUDIO_LOAD_MUSIC:
        case AUDIO_PLAY_MUSIC:
        case AUDIO_STOP_MUSIC:
        case AUDIO_SET_MUSIC_VOLUME:
        case AUDIO_MIDI_LOAD_FILE:
        case AUDIO_MIDI_PLAY_SEQUENCE:
        case AUDIO_MIDI_STOP_SEQUENCE:
        case AUDIO_MIDI_PLAY_NOTE:
        case AUDIO_MIDI_STOP_NOTE:
        case AUDIO_MIDI_CONTROL_CHANGE:
        case AUDIO_MIDI_PROGRAM_CHANGE:
            isEffectCommand = false;
            break;

        // System commands - route to effects for immediate processing
        case AUDIO_INITIALIZE:
        case AUDIO_SHUTDOWN:
        case AUDIO_FLUSH_COMMANDS:
        case AUDIO_SYNC_POINT:
        default:
            isEffectCommand = true;
            break;
    }

    if (isEffectCommand) {
        {
            std::lock_guard<std::mutex> lock(effectsQueueMutex);
            effectsQueue.push(command);
        }
        effectsQueueCondition.notify_one();
    } else {
        {
            std::lock_guard<std::mutex> lock(musicQueueMutex);
            musicQueue.push(command);
        }
        musicQueueCondition.notify_one();
    }
}

void AudioSystem::effectsProcessingThreadFunction() {
    std::cout << "AudioSystem: Effects processing thread started (high priority)" << std::endl;

    while (!stopEffectsProcessing.load() && !is_emergency_shutdown_requested()) {
        std::unique_lock<std::mutex> lock(effectsQueueMutex);

        // Wait for commands or shutdown signal
        effectsQueueCondition.wait(lock, [this]() {
            return !effectsQueue.empty() || stopEffectsProcessing.load() || is_emergency_shutdown_requested();
        });

        // Check for emergency shutdown
        if (is_emergency_shutdown_requested()) {
            std::cout << "AudioSystem: Effects thread detected emergency shutdown, exiting..." << std::endl;
            break;
        }

        // Process all available effects commands with minimal latency
        while (!effectsQueue.empty() && !stopEffectsProcessing.load() && !is_emergency_shutdown_requested()) {
            AudioCommand command = effectsQueue.front();
            effectsQueue.pop();

            // Release lock while processing command (allows new commands to be queued)
            lock.unlock();

            // Process the command immediately
            processCommand(command);

            // Reacquire lock for next iteration
            lock.lock();
        }
    }

    std::cout << "AudioSystem: Effects processing thread finished" << std::endl;
}

void AudioSystem::musicProcessingThreadFunction() {
    std::cout << "AudioSystem: Music processing thread started (lower priority)" << std::endl;

    while (!stopMusicProcessing.load() && !is_emergency_shutdown_requested()) {
        std::unique_lock<std::mutex> lock(musicQueueMutex);

        // Wait for commands or shutdown signal (with longer timeout for batching)
        musicQueueCondition.wait_for(lock, std::chrono::milliseconds(50), [this]() {
            return !musicQueue.empty() || stopMusicProcessing.load() || is_emergency_shutdown_requested();
        });

        // Check for emergency shutdown
        if (is_emergency_shutdown_requested()) {
            std::cout << "AudioSystem: Music thread detected emergency shutdown, exiting..." << std::endl;
            break;
        }

        // Process available music commands in batches
        int processedCount = 0;
        while (!musicQueue.empty() && !stopMusicProcessing.load() && !is_emergency_shutdown_requested() && processedCount < 5) {
            AudioCommand command = musicQueue.front();
            musicQueue.pop();
            processedCount++;

            // Release lock while processing command
            lock.unlock();

            // Process the command
            processCommand(command);

            // Reacquire lock for next iteration
            lock.lock();
        }
    }

    std::cout << "AudioSystem: Music processing thread finished" << std::endl;
}

void AudioSystem::processCommand(const AudioCommand& command) {
    try {
        switch (command.type) {
            case AUDIO_LOAD_MUSIC: {
                if (coreAudioEngine) {
                    bool success = coreAudioEngine->loadMusicFile(
                        command.load_music.filename,
                        command.target_id
                    );
                    if (success) {
                        std::lock_guard<std::mutex> lock(assetsMutex);
                        AudioAsset asset;
                        asset.id = command.target_id;
                        asset.filename = command.load_music.filename;
                        asset.loaded = true;
                        loadedAssets[command.target_id] = asset;
                    }
                }
                break;
            }

            case AUDIO_PLAY_MUSIC: {
                if (coreAudioEngine) {
                    coreAudioEngine->playMusic(
                        command.play_music.music_id,
                        command.play_music.volume,
                        command.play_music.loop
                    );
                }
                break;
            }

            case AUDIO_STOP_MUSIC: {
                if (coreAudioEngine) {
                    coreAudioEngine->stopMusic(command.target_id);
                }
                break;
            }

            case AUDIO_SET_MUSIC_VOLUME: {
                if (coreAudioEngine) {
                    coreAudioEngine->setMusicVolume(
                        command.target_id,
                        command.set_music_volume.volume
                    );
                }
                break;
            }

            case AUDIO_LOAD_SOUND: {
                if (coreAudioEngine) {
                    bool success = coreAudioEngine->loadSoundFile(
                        command.load_sound.filename,
                        command.target_id
                    );
                    if (success) {
                        std::lock_guard<std::mutex> lock(assetsMutex);
                        AudioAsset asset;
                        asset.id = command.target_id;
                        asset.filename = command.load_sound.filename;
                        asset.loaded = true;
                        loadedAssets[command.target_id] = asset;
                    }
                }
                break;
            }

            case AUDIO_PLAY_SOUND: {
                if (coreAudioEngine) {
                    coreAudioEngine->playSoundEffect(
                        command.play_sound.sound_id,
                        command.play_sound.volume,
                        command.play_sound.pitch,
                        command.play_sound.pan
                    );
                }
                break;
            }

            case AUDIO_STOP_SOUND: {
                if (coreAudioEngine) {
                    coreAudioEngine->stopSoundEffect(command.target_id);
                }
                break;
            }

            // TODO: Add synthesis and MIDI commands in Phase 2
            case AUDIO_CREATE_OSCILLATOR:
            case AUDIO_SET_OSC_FREQUENCY:
            case AUDIO_SET_OSC_WAVEFORM:
            case AUDIO_SET_OSC_VOLUME:
            case AUDIO_DELETE_OSCILLATOR:
            case AUDIO_MIDI_LOAD_FILE:
            case AUDIO_MIDI_PLAY_SEQUENCE:
            case AUDIO_MIDI_STOP_SEQUENCE:
                std::cout << "AudioSystem: Command type " << command.type << " not implemented yet (Phase 2)" << std::endl;
                break;

            case AUDIO_MIDI_PLAY_NOTE: {
                if (midiEngine) {
                    midiEngine->playNote(
                        command.midi_note_on.channel,
                        command.midi_note_on.note,
                        command.midi_note_on.velocity
                    );
                }
                break;
            }

            case AUDIO_MIDI_STOP_NOTE: {
                if (midiEngine) {
                    midiEngine->stopNote(
                        command.midi_note_off.channel,
                        command.midi_note_off.note
                    );
                }
                break;
            }

            case AUDIO_MIDI_CONTROL_CHANGE: {
                if (midiEngine) {
                    midiEngine->sendControlChange(
                        command.midi_control_change.channel,
                        command.midi_control_change.controller,
                        command.midi_control_change.value
                    );
                }
                break;
            }

            case AUDIO_MIDI_PROGRAM_CHANGE: {
                if (midiEngine) {
                    midiEngine->sendProgramChange(
                        command.midi_program_change.channel,
                        command.midi_program_change.program
                    );
                }
                break;
            }

            default:
                std::cerr << "AudioSystem: Unknown command type: " << command.type << std::endl;
                break;
        }
    } catch (const std::exception& e) {
        std::cerr << "AudioSystem: Error processing command " << command.type << ": " << e.what() << std::endl;
    }
}

// Music Control API

bool AudioSystem::loadMusic(const std::string& filename, uint32_t music_id) {
    AudioCommand command;
    command.type = AUDIO_LOAD_MUSIC;
    command.target_id = music_id;
    strncpy(command.load_music.filename, filename.c_str(), sizeof(command.load_music.filename) - 1);
    command.load_music.filename[sizeof(command.load_music.filename) - 1] = '\0';
    command.load_music.volume = 1.0f;
    command.load_music.loop = true;

    enqueueCommand(command);
    return true;  // Command queued successfully
}

void AudioSystem::playMusic(uint32_t music_id, float volume, bool loop) {
    AudioCommand command;
    command.type = AUDIO_PLAY_MUSIC;
    command.play_music.music_id = music_id;
    command.play_music.volume = volume;
    command.play_music.loop = loop;

    enqueueCommand(command);
}

void AudioSystem::stopMusic(uint32_t music_id) {
    AudioCommand command;
    command.type = AUDIO_STOP_MUSIC;
    command.target_id = music_id;

    enqueueCommand(command);
}

void AudioSystem::setMusicVolume(uint32_t music_id, float volume) {
    AudioCommand command;
    command.type = AUDIO_SET_MUSIC_VOLUME;
    command.target_id = music_id;
    command.set_music_volume.volume = volume;

    enqueueCommand(command);
}

// Sound Effects API

uint32_t AudioSystem::loadSound(const std::string& filename) {
    uint32_t sound_id = generateSoundId();
    loadSound(filename, sound_id);
    return sound_id;
}

bool AudioSystem::loadSound(const std::string& filename, uint32_t sound_id) {
    AudioCommand command;
    command.type = AUDIO_LOAD_SOUND;
    command.target_id = sound_id;
    strncpy(command.load_sound.filename, filename.c_str(), sizeof(command.load_sound.filename) - 1);
    command.load_sound.filename[sizeof(command.load_sound.filename) - 1] = '\0';

    enqueueCommand(command);
    return true;  // Command queued successfully
}

void AudioSystem::playSound(uint32_t sound_id, float volume, float pitch, float pan) {
    AudioCommand command;
    command.type = AUDIO_PLAY_SOUND;
    command.play_sound.sound_id = sound_id;
    command.play_sound.volume = volume;
    command.play_sound.pitch = pitch;
    command.play_sound.pan = pan;

    enqueueCommand(command);
}

void AudioSystem::stopSound(uint32_t sound_id) {
    AudioCommand command;
    command.type = AUDIO_STOP_SOUND;
    command.target_id = sound_id;
    enqueueCommand(command);
}

uint32_t AudioSystem::loadSoundFromBuffer(const float* samples, size_t sampleCount,
                                         uint32_t sampleRate, uint32_t channels) {
    if (!coreAudioEngine || !samples || sampleCount == 0) {
        return 0;
    }

    uint32_t sound_id = generateSoundId();

    // Load directly into CoreAudioEngine from buffer
    bool success = coreAudioEngine->loadSoundFromBuffer(samples, sampleCount,
                                                       sampleRate, channels, sound_id);

    if (success) {
        std::lock_guard<std::mutex> lock(assetsMutex);
        AudioAsset asset;
        asset.id = sound_id;
        asset.filename = "memory_buffer_" + std::to_string(sound_id);
        asset.loaded = true;
        asset.size = sampleCount * channels * sizeof(float);
        asset.duration = static_cast<float>(sampleCount) / static_cast<float>(sampleRate * channels);
        loadedAssets[sound_id] = asset;
    }

    return success ? sound_id : 0;
}

bool AudioSystem::loadSoundFromBuffer(const float* samples, size_t sampleCount,
                                     uint32_t sampleRate, uint32_t channels, uint32_t sound_id) {
    if (!coreAudioEngine || !samples || sampleCount == 0) {
        return false;
    }

    // Load directly into CoreAudioEngine with specified sound ID
    bool success = coreAudioEngine->loadSoundFromBuffer(samples, sampleCount,
                                                       sampleRate, channels, sound_id);

    if (success) {
        std::lock_guard<std::mutex> lock(assetsMutex);
        AudioAsset asset;
        asset.id = sound_id;
        asset.filename = "database_asset_" + std::to_string(sound_id);
        asset.loaded = true;
        asset.size = sampleCount * channels * sizeof(float);
        asset.duration = static_cast<float>(sampleCount) / static_cast<float>(sampleRate * channels);
        loadedAssets[sound_id] = asset;
    }

    return success;
}

bool AudioSystem::exportSoundToWAVBytes(uint32_t sound_id, std::vector<uint8_t>& outWAVData) {
    if (!coreAudioEngine) {
        return false;
    }
    return coreAudioEngine->exportSoundToWAVBytes(sound_id, outWAVData);
}

bool AudioSystem::exportSoundToPCMBytes(uint32_t sound_id, std::vector<uint8_t>& outPCMData) {
    if (!coreAudioEngine) {
        return false;
    }
    return coreAudioEngine->exportSoundToPCMBytes(sound_id, outPCMData);
}

// Real-time Synthesis API (Phase 2 - stubs for now)

uint32_t AudioSystem::createOscillator(float frequency, WaveformType waveform) {
    std::cout << "AudioSystem: createOscillator() - Phase 2 feature" << std::endl;
    return 0;  // TODO: Implement in Phase 2
}

void AudioSystem::setOscillatorFrequency(uint32_t osc_id, float frequency) {
    std::cout << "AudioSystem: setOscillatorFrequency() - Phase 2 feature" << std::endl;
    // TODO: Implement in Phase 2
}

void AudioSystem::setOscillatorWaveform(uint32_t osc_id, WaveformType waveform) {
    std::cout << "AudioSystem: setOscillatorWaveform() - Phase 2 feature" << std::endl;
    // TODO: Implement in Phase 2
}

void AudioSystem::setOscillatorVolume(uint32_t osc_id, float volume) {
    std::cout << "AudioSystem: setOscillatorVolume() - Phase 2 feature" << std::endl;
    // TODO: Implement in Phase 2
}

void AudioSystem::deleteOscillator(uint32_t osc_id) {
    std::cout << "AudioSystem: deleteOscillator() - Phase 2 feature" << std::endl;
    // TODO: Implement in Phase 2
}

// MIDI API (Phase 2 - stubs for now)

bool AudioSystem::loadMIDI(const std::string& filename, uint32_t midi_id) {
    std::cout << "AudioSystem: loadMIDI() - Phase 2 feature" << std::endl;
    return false;  // TODO: Implement in Phase 2
}

void AudioSystem::playMIDI(uint32_t midi_id, float tempo) {
    std::cout << "AudioSystem: playMIDI() - Phase 2 feature" << std::endl;
    // TODO: Implement in Phase 2
}

void AudioSystem::stopMIDI(uint32_t midi_id) {
    std::cout << "AudioSystem: stopMIDI() - Phase 2 feature" << std::endl;
    // TODO: Implement in Phase 2
}

void AudioSystem::sendMIDINoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    AudioCommand command;
    command.type = AUDIO_MIDI_PLAY_NOTE;
    command.midi_note_on.channel = channel;
    command.midi_note_on.note = note;
    command.midi_note_on.velocity = velocity;
    enqueueCommand(command);
}

void AudioSystem::sendMIDINoteOff(uint8_t channel, uint8_t note) {
    AudioCommand command;
    command.type = AUDIO_MIDI_STOP_NOTE;
    command.midi_note_off.channel = channel;
    command.midi_note_off.note = note;
    enqueueCommand(command);
}

void AudioSystem::sendMIDIControlChange(uint8_t channel, uint8_t controller, uint8_t value) {
    AudioCommand command;
    command.type = AUDIO_MIDI_CONTROL_CHANGE;
    command.midi_control_change.channel = channel;
    command.midi_control_change.controller = controller;
    command.midi_control_change.value = value;
    enqueueCommand(command);
}

void AudioSystem::sendMIDIProgramChange(uint8_t channel, uint8_t program) {
    AudioCommand command;
    command.type = AUDIO_MIDI_PROGRAM_CHANGE;
    command.midi_program_change.channel = channel;
    command.midi_program_change.program = program;
    enqueueCommand(command);
}

// System Information

size_t AudioSystem::getLoadedSoundCount() const {
    std::lock_guard<std::mutex> lock(assetsMutex);
    return loadedAssets.size();
}

size_t AudioSystem::getActiveVoiceCount() const {
    if (coreAudioEngine) {
        return coreAudioEngine->getActiveSoundCount();
    }
    return 0;
}

float AudioSystem::getCPUUsage() const {
    // TODO: Implement CPU usage monitoring
    return 0.0f;
}

std::vector<AudioAsset> AudioSystem::getLoadedAssets() const {
    std::lock_guard<std::mutex> lock(assetsMutex);
    std::vector<AudioAsset> assets;
    for (const auto& pair : loadedAssets) {
        assets.push_back(pair.second);
    }
    return assets;
}

void AudioSystem::unloadAsset(uint32_t asset_id) {
    std::lock_guard<std::mutex> lock(assetsMutex);
    loadedAssets.erase(asset_id);

    // TODO: Also unload from core audio engine
}

void AudioSystem::clearCache() {
    std::lock_guard<std::mutex> lock(assetsMutex);
    loadedAssets.clear();

    if (coreAudioEngine) {
        coreAudioEngine->clearMusicCache();
        coreAudioEngine->clearSoundCache();
    }
}

// Internal Helpers

uint32_t AudioSystem::generateSoundId() {
    return nextSoundId.fetch_add(1);
}

uint32_t AudioSystem::generateOscillatorId() {
    return nextOscillatorId.fetch_add(1);
}

uint32_t AudioSystem::generateMidiId() {
    return nextMidiId.fetch_add(1);
}

// C Interface Implementation

extern "C" {

bool audio_initialize() {
    if (!g_audioSystem) {
        g_audioSystem = std::make_unique<AudioSystem>();
    }
    return g_audioSystem->initialize();
}

void audio_shutdown() {
    // Check for emergency shutdown
    if (is_emergency_shutdown_requested()) {
        std::cout << "AudioSystem: Emergency shutdown requested" << std::endl;
    }

    if (g_audioSystem) {
        g_audioSystem->shutdown();
        g_audioSystem.reset();
    }
}

void audio_check_emergency_shutdown() {
    if (g_audioSystem) {
        g_audioSystem->checkEmergencyShutdown();
    }
}

bool audio_is_initialized() {
    return g_audioSystem && g_audioSystem->isInitialized();
}

void audio_wait_queue_empty() {
    if (g_audioSystem) {
        g_audioSystem->waitQueueEmpty();
    }
}

// Music functions

bool audio_load_music(const char* filename, uint32_t music_id) {
    if (!g_audioSystem || !filename) {
        return false;
    }
    return g_audioSystem->loadMusic(filename, music_id);
}

void audio_play_music(uint32_t music_id, float volume, bool loop) {
    if (g_audioSystem) {
        g_audioSystem->playMusic(music_id, volume, loop);
    }
}

void audio_stop_music(uint32_t music_id) {
    if (g_audioSystem) {
        g_audioSystem->stopMusic(music_id);
    }
}

void audio_set_music_volume(uint32_t music_id, float volume) {
    if (g_audioSystem) {
        g_audioSystem->setMusicVolume(music_id, volume);
    }
}

// Sound effect functions

uint32_t audio_load_sound(const char* filename) {
    if (!g_audioSystem || !filename) {
        return 0;
    }
    return g_audioSystem->loadSound(filename);
}

bool audio_load_sound_with_id(const char* filename, uint32_t sound_id) {
    if (!g_audioSystem || !filename) {
        return false;
    }
    return g_audioSystem->loadSound(filename, sound_id);
}

uint32_t audio_load_sound_from_buffer(const float* samples, size_t sampleCount,
                                     uint32_t sampleRate, uint32_t channels) {
    if (!g_audioSystem || !samples) {
        return 0;
    }
    return g_audioSystem->loadSoundFromBuffer(samples, sampleCount, sampleRate, channels);
}

bool audio_load_sound_from_buffer_with_id(const float* samples, size_t sampleCount,
                                          uint32_t sampleRate, uint32_t channels,
                                          uint32_t sound_id) {
    if (!g_audioSystem || !samples) {
        return false;
    }
    return g_audioSystem->loadSoundFromBuffer(samples, sampleCount, sampleRate, channels, sound_id);
}

void audio_play_sound(uint32_t sound_id, float volume, float pitch, float pan) {
    if (g_audioSystem) {
        g_audioSystem->playSound(sound_id, volume, pitch, pan);
    }
}

void audio_stop_sound(uint32_t sound_id) {
    if (g_audioSystem) {
        g_audioSystem->stopSound(sound_id);
    }
}

// Synthesis functions (Phase 2)

uint32_t audio_create_oscillator(float frequency, int waveform) {
    if (g_audioSystem) {
        return g_audioSystem->createOscillator(frequency, static_cast<WaveformType>(waveform));
    }
    return 0;
}

void audio_set_oscillator_frequency(uint32_t osc_id, float frequency) {
    if (g_audioSystem) {
        g_audioSystem->setOscillatorFrequency(osc_id, frequency);
    }
}

void audio_set_oscillator_waveform(uint32_t osc_id, int waveform) {
    if (g_audioSystem) {
        g_audioSystem->setOscillatorWaveform(osc_id, static_cast<WaveformType>(waveform));
    }
}

void audio_set_oscillator_volume(uint32_t osc_id, float volume) {
    if (g_audioSystem) {
        g_audioSystem->setOscillatorVolume(osc_id, volume);
    }
}

void audio_delete_oscillator(uint32_t osc_id) {
    if (g_audioSystem) {
        g_audioSystem->deleteOscillator(osc_id);
    }
}

// MIDI functions (Phase 2)

bool audio_load_midi(const char* filename, uint32_t midi_id) {
    if (!g_audioSystem || !filename) {
        return false;
    }
    return g_audioSystem->loadMIDI(filename, midi_id);
}

void audio_play_midi(uint32_t midi_id, float tempo) {
    if (g_audioSystem) {
        g_audioSystem->playMIDI(midi_id, tempo);
    }
}

void audio_stop_midi(uint32_t midi_id) {
    if (g_audioSystem) {
        g_audioSystem->stopMIDI(midi_id);
    }
}

void audio_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (g_audioSystem) {
        g_audioSystem->sendMIDINoteOn(channel, note, velocity);
    }
}

void audio_midi_note_off(uint8_t channel, uint8_t note) {
    if (g_audioSystem) {
        g_audioSystem->sendMIDINoteOff(channel, note);
    }
}

void audio_midi_control_change(uint8_t channel, uint8_t controller, uint8_t value) {
    if (g_audioSystem) {
        g_audioSystem->sendMIDIControlChange(channel, controller, value);
    }
}

void audio_midi_program_change(uint8_t channel, uint8_t program) {
    if (g_audioSystem) {
        g_audioSystem->sendMIDIProgramChange(channel, program);
    }
}

// MIDI timing and synchronization functions

void audio_midi_set_tempo(double bpm) {
    if (g_audioSystem && g_audioSystem->getMidiEngine()) {
        g_audioSystem->getMidiEngine()->setMasterTempo(bpm);
    }
}

void audio_midi_wait_beats(double beats) {
    if (g_audioSystem && g_audioSystem->getMidiEngine()) {
        // Calculate milliseconds based on current tempo
        double bpm = 120.0; // Default tempo, should get from MIDI engine
        double millisecondsPerBeat = 60000.0 / bpm;
        int waitTime = static_cast<int>(beats * millisecondsPerBeat);

        // Use a proper wait that doesn't block the audio thread
        std::this_thread::sleep_for(std::chrono::milliseconds(waitTime));
    }
}

void audio_midi_wait_ticks(int ticks) {
    if (g_audioSystem && g_audioSystem->getMidiEngine()) {
        // MIDI ticks are typically 24 per quarter note
        double beats = static_cast<double>(ticks) / 24.0;
        audio_midi_wait_beats(beats);
    }
}

void audio_midi_wait_for_completion() {
    if (g_audioSystem && g_audioSystem->getMidiEngine()) {
        g_audioSystem->getMidiEngine()->waitForPlaybackComplete();
    }
}

// Information functions

size_t audio_get_loaded_sound_count() {
    if (g_audioSystem) {
        return g_audioSystem->getLoadedSoundCount();
    }
    return 0;
}

size_t audio_get_active_voice_count() {
    if (g_audioSystem) {
        return g_audioSystem->getActiveVoiceCount();
    }
    return 0;
}

float audio_get_cpu_usage() {
    if (g_audioSystem) {
        return g_audioSystem->getCPUUsage();
    }
    return 0.0f;
}

// High-level music functions (ABC notation)

bool music_play(const char* abc_notation, const char* name, int tempo_bpm, int instrument) {
    std::cout << "*** music_play CALLED with XPC client, abc='" << (abc_notation ? abc_notation : "NULL") << "'" << std::endl;

    // Use XPC client instead of built-in music player
    if (!abc_client_is_initialized()) {
        abc_client_initialize();
    }

    bool result = abc_client_play_abc(abc_notation, name);
    std::cout << "*** music_play: XPC client returned " << (result ? "TRUE" : "FALSE") << std::endl;
    return result;
}

uint32_t queue_music_slot(const char* abc_notation, const char* name, int tempo_bpm, int instrument, bool loop) {
    // Use XPC client instead of built-in music player
    if (!abc_client_is_initialized()) {
        abc_client_initialize();
    }

    // Queue the ABC notation through XPC service
    // Note: XPC service doesn't return slot IDs yet, so we return a dummy value
    bool success = abc_client_play_abc(abc_notation, name);
    return success ? 1 : 0;
}

uint32_t queue_music_slot_with_wait(const char* abc_notation, const char* name, int tempo_bpm, int instrument, bool loop, int wait_after_ms) {
    // Use XPC client instead of built-in music player
    if (!abc_client_is_initialized()) {
        abc_client_initialize();
    }

    // Queue the ABC notation through XPC service
    // Note: wait_after_ms is not yet supported by XPC service, consider adding rest to ABC notation
    bool success = abc_client_play_abc(abc_notation, name);
    return success ? 1 : 0;
}

bool music_queue(const char* abc_notation, const char* name, int tempo_bpm, int instrument, bool loop) {
    // Use XPC client instead of built-in music player
    if (!abc_client_is_initialized()) {
        abc_client_initialize();
    }

    return abc_client_play_abc(abc_notation, name);
}

void music_stop() {
    // Use XPC client instead of built-in music player
    if (abc_client_is_initialized()) {
        abc_client_stop();
    }
}

void music_pause() {
    // Use XPC client instead of built-in music player
    if (abc_client_is_initialized()) {
        abc_client_pause();
    }
}

void music_resume() {
    // Use XPC client instead of built-in music player
    if (abc_client_is_initialized()) {
        abc_client_resume();
    }
}

void music_clear_queue() {
    // Use XPC client instead of built-in music player
    if (abc_client_is_initialized()) {
        abc_client_clear_queue();
    }
}

bool music_is_playing() {
    // Use XPC client instead of built-in music player
    if (abc_client_is_initialized()) {
        return abc_client_is_playing();
    }
    return false;
}

bool music_is_paused() {
    // Use XPC client instead of built-in music player
    if (abc_client_is_initialized()) {
        return abc_client_is_paused();
    }
    return false;
}

int music_get_queue_size() {
    // Use XPC client instead of built-in music player
    if (abc_client_is_initialized()) {
        return abc_client_get_queue_size();
    }
    return 0;
}

const char* music_get_current_song_name() {
    // Use XPC client instead of built-in music player
    // Note: XPC service doesn't provide current song name yet
    return "";
}

void music_set_volume(float volume) {
    // Use XPC client instead of built-in music player
    if (abc_client_is_initialized()) {
        abc_client_set_volume(volume);
    }
}

float music_get_volume() {
    // Use XPC client instead of built-in music player
    if (abc_client_is_initialized()) {
        return abc_client_get_volume();
    }
    return 1.0f;
}

void music_set_tempo(float multiplier) {
    // Use XPC client instead of built-in music player
    // Note: XPC service doesn't support tempo adjustment yet
    // This would need to be added to the XPC service
}

float music_get_tempo() {
    // Use XPC client instead of built-in music player
    // Note: XPC service doesn't support tempo adjustment yet
    return 1.0f;
}

bool music_play_simple(const char* abc_notation) {
    std::cout << "*** music_play_simple CALLED with abc='" << (abc_notation ? abc_notation : "NULL") << "'" << std::endl;
    bool result = music_play(abc_notation, "", 120, 0);
    std::cout << "*** music_play_simple returning " << (result ? "TRUE" : "FALSE") << std::endl;
    return result;
}

bool music_queue_simple(const char* abc_notation) {
    return music_queue(abc_notation, "", 120, 0, false);
}

uint32_t queue_music_slot_simple(const char* abc_notation) {
    // Delegates to queue_music_slot which now uses XPC client
    return queue_music_slot(abc_notation, "", 120, 0, false);
}

bool remove_music_slot(uint32_t slot_id) {
    // Use XPC client instead of built-in music player
    // Note: XPC service doesn't support slot-based removal yet
    // Consider adding this functionality to the XPC service
    return false;
}

bool has_music_slot(uint32_t slot_id) {
    if (g_audioSystem && g_audioSystem->getMusicPlayer()) {
        return g_audioSystem->getMusicPlayer()->hasMusicSlot(slot_id);
    }
    return false;
}

int get_music_slot_count() {
    if (g_audioSystem && g_audioSystem->getMusicPlayer()) {
        return static_cast<int>(g_audioSystem->getMusicPlayer()->getQueuedSlotIds().size());
    }
    return 0;
}

const char* music_get_test_song(const char* song_name) {
    static std::string testSong;
    if (song_name) {
        testSong = SuperTerminal::ST_MusicPlayer::getTestSong(song_name);
        return testSong.c_str();
    }
    return "";
}

// Wait-related functions removed - using ABC rests instead for cleaner musical pauses

} // extern "C"

// C++ functions (not extern "C") - these use C++ types like std::vector
bool audio_export_sound_to_wav_bytes(uint32_t sound_id, std::vector<uint8_t>& outWAVData) {
    if (!g_audioSystem) {
        return false;
    }
    return g_audioSystem->exportSoundToWAVBytes(sound_id, outWAVData);
}

bool audio_export_sound_to_pcm_bytes(uint32_t sound_id, std::vector<uint8_t>& outPCMData) {
    if (!g_audioSystem) {
        return false;
    }
    return g_audioSystem->exportSoundToPCMBytes(sound_id, outPCMData);
}
