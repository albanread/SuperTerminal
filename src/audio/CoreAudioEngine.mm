//
//  CoreAudioEngine.mm
//  SuperTerminal Framework - Core Audio Engine Implementation
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "CoreAudioEngine.h"
#include <iostream>
#include <algorithm>
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

// CoreAudioEngine Implementation

CoreAudioEngine::CoreAudioEngine()
    : audioEngine(nil)
    , mainMixer(nil)
    , spatialMixer(nil)
    , standardFormat(nil)
    , initialized(false)
    , nextInstanceId(1)
    , masterVolume(1.0f)
    , mutedState(false)
    , memoryUsage(0)
{
    std::cout << "CoreAudioEngine: Constructor called" << std::endl;
}

CoreAudioEngine::~CoreAudioEngine() {
    if (initialized.load()) {
        shutdown();
    }
    std::cout << "CoreAudioEngine: Destructor called" << std::endl;
}

bool CoreAudioEngine::initialize(const AudioConfig& audioConfig) {
    std::lock_guard<std::mutex> lock(systemMutex);

    if (initialized.load()) {
        std::cout << "CoreAudioEngine: Already initialized" << std::endl;
        return true;
    }

    std::cout << "CoreAudioEngine: Initializing..." << std::endl;

    // Store configuration
    config = audioConfig;

    // Configure audio session first
    if (!configureAudioSession()) {
        std::cerr << "CoreAudioEngine: Failed to configure audio session" << std::endl;
        return false;
    }

    // Setup AVAudioEngine
    if (!setupAudioEngine()) {
        std::cerr << "CoreAudioEngine: Failed to setup audio engine" << std::endl;
        cleanupAudioEngine();
        return false;
    }

    initialized.store(true);
    std::cout << "CoreAudioEngine: Initialization complete" << std::endl;
    std::cout << "  Sample Rate: " << config.sampleRate << " Hz" << std::endl;
    std::cout << "  Buffer Size: " << config.bufferSize << " samples" << std::endl;
    std::cout << "  Max Voices: " << config.maxVoices << std::endl;

    return true;
}

void CoreAudioEngine::shutdown() {
    std::lock_guard<std::mutex> lock(systemMutex);

    if (!initialized.load()) {
        return;
    }

    std::cout << "CoreAudioEngine: Shutting down..." << std::endl;

    // Stop all playback
    stopAllSounds();
    clearMusicCache();
    clearSoundCache();

    // Cleanup audio engine
    cleanupAudioEngine();

    initialized.store(false);
    std::cout << "CoreAudioEngine: Shutdown complete" << std::endl;
}

bool CoreAudioEngine::setupAudioEngine() {
    @autoreleasepool {
        // Create AVAudioEngine
        audioEngine = [[AVAudioEngine alloc] init];
        if (!audioEngine) {
            std::cerr << "CoreAudioEngine: Failed to create AVAudioEngine" << std::endl;
            return false;
        }

        // Get the main mixer node
        mainMixer = [audioEngine mainMixerNode];

        // Create environment node for 3D audio
        if (config.enableSpatialAudio) {
            spatialMixer = [[AVAudioEnvironmentNode alloc] init];
            [audioEngine attachNode:spatialMixer];
            [audioEngine connect:spatialMixer to:mainMixer format:nil];
        }

        // Set up standard format
        standardFormat = [[AVAudioFormat alloc]
            initStandardFormatWithSampleRate:config.sampleRate
            channels:2];  // Stereo

        if (!standardFormat) {
            std::cerr << "CoreAudioEngine: Failed to create standard format" << std::endl;
            return false;
        }

        // Start the audio engine
        NSError* error = nil;
        BOOL success = [audioEngine startAndReturnError:&error];
        if (!success) {
            std::cerr << "CoreAudioEngine: Failed to start audio engine: "
                      << [[error localizedDescription] UTF8String] << std::endl;
            return false;
        }

        std::cout << "CoreAudioEngine: AVAudioEngine started successfully" << std::endl;
        return true;
    }
}

void CoreAudioEngine::cleanupAudioEngine() {
    @autoreleasepool {
        if (audioEngine) {
            [audioEngine stop];
            audioEngine = nil;
        }

        mainMixer = nil;
        spatialMixer = nil;
        standardFormat = nil;
    }
}

bool CoreAudioEngine::configureAudioSession() {
    // On macOS, audio session configuration is simpler than iOS
    // The system handles most of this automatically
    std::cout << "CoreAudioEngine: Audio session configured (macOS - automatic)" << std::endl;
    return true;
}

// Music playback implementation

bool CoreAudioEngine::loadMusicFile(const std::string& filename, uint32_t music_id) {
    std::lock_guard<std::mutex> lock(musicMutex);

    @autoreleasepool {
        // Check if already loaded
        if (musicTracks.find(music_id) != musicTracks.end()) {
            std::cout << "CoreAudioEngine: Music ID " << music_id << " already loaded" << std::endl;
            return true;
        }

        // Create music track
        auto track = std::make_unique<AudioMusicTrack>();
        track->id = music_id;
        track->filename = filename;

        // Load audio file
        NSString* nsFilename = [NSString stringWithUTF8String:filename.c_str()];
        NSURL* fileURL = [NSURL fileURLWithPath:nsFilename];

        NSError* error = nil;
        track->audioFile = [[AVAudioFile alloc] initForReading:fileURL error:&error];

        if (!track->audioFile) {
            std::cerr << "CoreAudioEngine: Failed to load music file '" << filename << "': "
                      << [[error localizedDescription] UTF8String] << std::endl;
            return false;
        }

        // Get file duration
        track->duration = (float)[track->audioFile length] / [track->audioFile processingFormat].sampleRate;

        // Create player node
        track->playerNode = [[AVAudioPlayerNode alloc] init];
        [audioEngine attachNode:track->playerNode];

        // Connect to mixer
        AVAudioNode* targetNode = config.enableSpatialAudio ? spatialMixer : mainMixer;
        [audioEngine connect:track->playerNode to:targetNode format:[track->audioFile processingFormat]];

        // Store the track
        musicTracks[music_id] = std::move(track);

        std::cout << "CoreAudioEngine: Loaded music file '" << filename
                  << "' with ID " << music_id
                  << " (duration: " << musicTracks[music_id]->duration << "s)" << std::endl;

        return true;
    }
}

void CoreAudioEngine::playMusic(uint32_t music_id, float volume, bool loop) {
    std::lock_guard<std::mutex> lock(musicMutex);

    AudioMusicTrack* track = getMusicTrack(music_id);
    if (!track) {
        std::cerr << "CoreAudioEngine: Cannot play music - ID " << music_id << " not found" << std::endl;
        return;
    }

    @autoreleasepool {
        // Stop if already playing
        if (track->isPlaying) {
            [track->playerNode stop];
        }

        // Update track settings
        track->volume = volume;
        track->isLooping = loop;

        // Set volume
        track->playerNode.volume = volume * masterVolume.load();

        // Schedule the file for playback
        if (loop) {
            [track->playerNode scheduleFile:track->audioFile atTime:nil completionHandler:^{
                // For looping, we'll reschedule in the completion handler
                if (track->isLooping && track->isPlaying) {
                    [track->playerNode scheduleFile:track->audioFile atTime:nil completionHandler:nil];
                }
            }];
        } else {
            [track->playerNode scheduleFile:track->audioFile atTime:nil completionHandler:^{
                track->isPlaying = false;
            }];
        }

        // Start playback
        [track->playerNode play];
        track->isPlaying = true;

        std::cout << "CoreAudioEngine: Playing music ID " << music_id
                  << " (volume: " << volume << ", loop: " << (loop ? "yes" : "no") << ")" << std::endl;
    }
}

void CoreAudioEngine::stopMusic(uint32_t music_id) {
    std::lock_guard<std::mutex> lock(musicMutex);

    AudioMusicTrack* track = getMusicTrack(music_id);
    if (!track) {
        std::cerr << "CoreAudioEngine: Cannot stop music - ID " << music_id << " not found" << std::endl;
        return;
    }

    if (track->isPlaying) {
        [track->playerNode stop];
        track->isPlaying = false;
        track->isLooping = false;
        std::cout << "CoreAudioEngine: Stopped music ID " << music_id << std::endl;
    }
}

void CoreAudioEngine::setMusicVolume(uint32_t music_id, float volume) {
    std::lock_guard<std::mutex> lock(musicMutex);

    AudioMusicTrack* track = getMusicTrack(music_id);
    if (!track) {
        std::cerr << "CoreAudioEngine: Cannot set music volume - ID " << music_id << " not found" << std::endl;
        return;
    }

    track->volume = volume;
    track->playerNode.volume = volume * masterVolume.load();

    std::cout << "CoreAudioEngine: Set music ID " << music_id << " volume to " << volume << std::endl;
}

bool CoreAudioEngine::isMusicPlaying(uint32_t music_id) const {
    std::lock_guard<std::mutex> lock(musicMutex);

    const AudioMusicTrack* track = getMusicTrack(music_id);
    return track ? track->isPlaying : false;
}

// Sound effects implementation

bool CoreAudioEngine::loadSoundFile(const std::string& filename, uint32_t sound_id) {
    std::lock_guard<std::mutex> lock(soundsMutex);

    @autoreleasepool {
        // Check if already loaded
        if (soundEffects.find(sound_id) != soundEffects.end()) {
            std::cout << "CoreAudioEngine: Sound ID " << sound_id << " already loaded" << std::endl;
            return true;
        }

        // Create sound effect
        auto sound = std::make_unique<SoundEffect>();
        sound->id = sound_id;
        sound->filename = filename;

        // Load audio buffer
        sound->buffer = loadAudioBuffer(filename, &sound->format);
        if (!sound->buffer) {
            std::cerr << "CoreAudioEngine: Failed to load sound file '" << filename << "'" << std::endl;
            return false;
        }

        // Get buffer info
        sound->frameLength = [sound->buffer frameLength];
        sound->duration = (float)sound->frameLength / [sound->format sampleRate];

        // Store the sound
        soundEffects[sound_id] = std::move(sound);

        // Update memory usage
        updateMemoryUsage();

        std::cout << "CoreAudioEngine: Loaded sound file '" << filename
                  << "' with ID " << sound_id
                  << " (duration: " << soundEffects[sound_id]->duration << "s, "
                  << soundEffects[sound_id]->frameLength << " frames)" << std::endl;

        return true;
    }
}

bool CoreAudioEngine::loadSoundFromBuffer(const float* samples, size_t sampleCount,
                                         uint32_t sampleRate, uint32_t channels, uint32_t sound_id) {
    std::lock_guard<std::mutex> lock(soundsMutex);

    @autoreleasepool {
        // Check if already loaded
        if (soundEffects.find(sound_id) != soundEffects.end()) {
            std::cout << "CoreAudioEngine: Sound ID " << sound_id << " already loaded" << std::endl;
            return true;
        }

        // Create sound effect
        auto sound = std::make_unique<SoundEffect>();
        sound->id = sound_id;
        sound->filename = "memory_buffer_" + std::to_string(sound_id);

        // Create AVAudioFormat for the buffer
        AVAudioFormat* format = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
                                                                 sampleRate:sampleRate
                                                                   channels:channels
                                                                interleaved:YES];

        if (!format) {
            std::cerr << "CoreAudioEngine: Failed to create audio format for buffer" << std::endl;
            return false;
        }

        // Calculate frame count (samples / channels)
        AVAudioFrameCount frameCount = static_cast<AVAudioFrameCount>(sampleCount / channels);

        // Create PCM buffer
        AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:frameCount];
        if (!buffer) {
            std::cerr << "CoreAudioEngine: Failed to create PCM buffer" << std::endl;
            return false;
        }

        // Copy sample data to buffer
        buffer.frameLength = frameCount;
        float* bufferData = buffer.floatChannelData[0]; // Interleaved data

        for (size_t i = 0; i < sampleCount; i++) {
            bufferData[i] = samples[i];
        }

        // Store in sound effect
        sound->buffer = buffer;
        sound->format = format;
        sound->frameLength = frameCount;
        sound->duration = static_cast<float>(frameCount) / sampleRate;

        // Store the sound
        soundEffects[sound_id] = std::move(sound);

        // Update memory usage
        updateMemoryUsage();

        std::cout << "CoreAudioEngine: Loaded sound from buffer with ID " << sound_id
                  << " (duration: " << soundEffects[sound_id]->duration << "s, "
                  << soundEffects[sound_id]->frameLength << " frames)" << std::endl;

        return true;
    }
}

bool CoreAudioEngine::exportSoundToWAVBytes(uint32_t sound_id, std::vector<uint8_t>& outWAVData) {
    std::lock_guard<std::mutex> lock(soundsMutex);

    SoundEffect* sound = getSoundEffect(sound_id);
    if (!sound || !sound->buffer) {
        std::cerr << "CoreAudioEngine: Cannot export sound - ID " << sound_id << " not found" << std::endl;
        return false;
    }

    @autoreleasepool {
        AVAudioPCMBuffer* buffer = sound->buffer;
        AVAudioFormat* format = buffer.format;

        uint32_t sampleRate = (uint32_t)format.sampleRate;
        uint32_t channels = format.channelCount;
        uint32_t frameCount = (uint32_t)buffer.frameLength;
        uint32_t byteRate = sampleRate * channels * 2; // 16-bit samples
        uint32_t dataSize = frameCount * channels * 2;

        // WAV header
        outWAVData.clear();
        outWAVData.reserve(44 + dataSize);

        // RIFF header
        outWAVData.push_back('R'); outWAVData.push_back('I'); outWAVData.push_back('F'); outWAVData.push_back('F');
        uint32_t fileSize = 36 + dataSize;
        outWAVData.push_back(fileSize & 0xFF); outWAVData.push_back((fileSize >> 8) & 0xFF);
        outWAVData.push_back((fileSize >> 16) & 0xFF); outWAVData.push_back((fileSize >> 24) & 0xFF);
        outWAVData.push_back('W'); outWAVData.push_back('A'); outWAVData.push_back('V'); outWAVData.push_back('E');

        // fmt chunk
        outWAVData.push_back('f'); outWAVData.push_back('m'); outWAVData.push_back('t'); outWAVData.push_back(' ');
        uint32_t fmtSize = 16;
        outWAVData.push_back(fmtSize & 0xFF); outWAVData.push_back((fmtSize >> 8) & 0xFF);
        outWAVData.push_back((fmtSize >> 16) & 0xFF); outWAVData.push_back((fmtSize >> 24) & 0xFF);
        uint16_t audioFormat = 1; // PCM
        outWAVData.push_back(audioFormat & 0xFF); outWAVData.push_back((audioFormat >> 8) & 0xFF);
        outWAVData.push_back(channels & 0xFF); outWAVData.push_back((channels >> 8) & 0xFF);
        outWAVData.push_back(sampleRate & 0xFF); outWAVData.push_back((sampleRate >> 8) & 0xFF);
        outWAVData.push_back((sampleRate >> 16) & 0xFF); outWAVData.push_back((sampleRate >> 24) & 0xFF);
        outWAVData.push_back(byteRate & 0xFF); outWAVData.push_back((byteRate >> 8) & 0xFF);
        outWAVData.push_back((byteRate >> 16) & 0xFF); outWAVData.push_back((byteRate >> 24) & 0xFF);
        uint16_t blockAlign = channels * 2;
        outWAVData.push_back(blockAlign & 0xFF); outWAVData.push_back((blockAlign >> 8) & 0xFF);
        uint16_t bitsPerSample = 16;
        outWAVData.push_back(bitsPerSample & 0xFF); outWAVData.push_back((bitsPerSample >> 8) & 0xFF);

        // data chunk
        outWAVData.push_back('d'); outWAVData.push_back('a'); outWAVData.push_back('t'); outWAVData.push_back('a');
        outWAVData.push_back(dataSize & 0xFF); outWAVData.push_back((dataSize >> 8) & 0xFF);
        outWAVData.push_back((dataSize >> 16) & 0xFF); outWAVData.push_back((dataSize >> 24) & 0xFF);

        // Convert float samples to 16-bit PCM
        const float* floatData = buffer.floatChannelData[0];
        for (uint32_t i = 0; i < frameCount * channels; i++) {
            float sample = floatData[i];
            // Clamp to [-1.0, 1.0]
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            // Convert to 16-bit
            int16_t sample16 = (int16_t)(sample * 32767.0f);
            outWAVData.push_back(sample16 & 0xFF);
            outWAVData.push_back((sample16 >> 8) & 0xFF);
        }

        std::cout << "CoreAudioEngine: Exported sound ID " << sound_id << " to WAV ("
                  << outWAVData.size() << " bytes)" << std::endl;
        return true;
    }
}

bool CoreAudioEngine::exportSoundToPCMBytes(uint32_t sound_id, std::vector<uint8_t>& outPCMData) {
    std::lock_guard<std::mutex> lock(soundsMutex);

    SoundEffect* sound = getSoundEffect(sound_id);
    if (!sound || !sound->buffer) {
        std::cerr << "CoreAudioEngine: Cannot export sound - ID " << sound_id << " not found" << std::endl;
        return false;
    }

    @autoreleasepool {
        AVAudioPCMBuffer* buffer = sound->buffer;
        AVAudioFormat* format = buffer.format;

        uint32_t sampleRate = (uint32_t)format.sampleRate;
        uint32_t channels = format.channelCount;
        uint32_t frameCount = (uint32_t)buffer.frameLength;
        uint32_t sampleCount = frameCount * channels;

        // Simple header: sampleRate (4 bytes) + channels (4 bytes) + sampleCount (4 bytes)
        outPCMData.clear();
        outPCMData.reserve(12 + sampleCount * sizeof(float));

        // Write header
        outPCMData.push_back(sampleRate & 0xFF);
        outPCMData.push_back((sampleRate >> 8) & 0xFF);
        outPCMData.push_back((sampleRate >> 16) & 0xFF);
        outPCMData.push_back((sampleRate >> 24) & 0xFF);

        outPCMData.push_back(channels & 0xFF);
        outPCMData.push_back((channels >> 8) & 0xFF);
        outPCMData.push_back((channels >> 16) & 0xFF);
        outPCMData.push_back((channels >> 24) & 0xFF);

        outPCMData.push_back(sampleCount & 0xFF);
        outPCMData.push_back((sampleCount >> 8) & 0xFF);
        outPCMData.push_back((sampleCount >> 16) & 0xFF);
        outPCMData.push_back((sampleCount >> 24) & 0xFF);

        // Copy raw float samples
        const float* floatData = buffer.floatChannelData[0];
        const uint8_t* byteData = reinterpret_cast<const uint8_t*>(floatData);
        size_t byteCount = sampleCount * sizeof(float);
        outPCMData.insert(outPCMData.end(), byteData, byteData + byteCount);

        std::cout << "CoreAudioEngine: Exported sound ID " << sound_id << " to PCM ("
                  << outPCMData.size() << " bytes, " << sampleCount << " samples)" << std::endl;
        return true;
    }
}

uint32_t CoreAudioEngine::playSoundEffect(uint32_t sound_id, float volume, float pitch, float pan) {
    std::lock_guard<std::mutex> soundsLock(soundsMutex);

    SoundEffect* sound = getSoundEffect(sound_id);
    if (!sound) {
        std::cerr << "CoreAudioEngine: Cannot play sound - ID " << sound_id << " not found" << std::endl;
        return 0;
    }

    @autoreleasepool {
        // Create active sound instance
        auto activeSound = std::make_unique<ActiveSound>();
        activeSound->instance_id = generateInstanceId();
        activeSound->sound_id = sound_id;
        activeSound->volume = volume;
        activeSound->pitch = pitch;
        activeSound->pan = pan;

        // Create player node for this instance
        activeSound->playerNode = createPlayerNode();
        if (!activeSound->playerNode) {
            std::cerr << "CoreAudioEngine: Failed to create player node for sound " << sound_id << std::endl;
            return 0;
        }

        // Configure the player node
        configurePlayerNode(activeSound->playerNode, volume, pitch, pan);

        // Connect to mixer
        AVAudioNode* targetNode = config.enableSpatialAudio ? spatialMixer : mainMixer;
        [audioEngine connect:activeSound->playerNode to:targetNode format:sound->format];

        // Schedule the buffer
        uint32_t instance_id_capture = activeSound->instance_id;
        [activeSound->playerNode scheduleBuffer:sound->buffer completionHandler:^{
            // Mark as finished - find the sound by ID since activeSound pointer may be invalid
            std::lock_guard<std::mutex> lock(activesoundsMutex);
            auto it = activeSounds.find(instance_id_capture);
            if (it != activeSounds.end()) {
                it->second->isPlaying = false;
            }
        }];

        // Start playback
        [activeSound->playerNode play];
        activeSound->isPlaying = true;

        uint32_t instance_id = activeSound->instance_id;

        // Store active sound
        {
            std::lock_guard<std::mutex> activeLock(activesoundsMutex);
            activeSounds[instance_id] = std::move(activeSound);
        }

        std::cout << "CoreAudioEngine: Playing sound ID " << sound_id
                  << " (instance: " << instance_id
                  << ", volume: " << volume
                  << ", pitch: " << pitch
                  << ", pan: " << pan << ")" << std::endl;

        return instance_id;
    }
}

void CoreAudioEngine::stopSoundEffect(uint32_t instance_id) {
    std::lock_guard<std::mutex> lock(activesoundsMutex);

    auto it = activeSounds.find(instance_id);
    if (it != activeSounds.end()) {
        ActiveSound* activeSound = it->second.get();
        if (activeSound->isPlaying) {
            [activeSound->playerNode stop];
            activeSound->isPlaying = false;
            std::cout << "CoreAudioEngine: Stopped sound instance " << instance_id << std::endl;
        }
    }
}

void CoreAudioEngine::stopAllSounds() {
    std::cout << "CoreAudioEngine: Stopping all sounds..." << std::endl;

    // Stop all music
    {
        std::lock_guard<std::mutex> lock(musicMutex);
        for (auto& pair : musicTracks) {
            AudioMusicTrack* track = pair.second.get();
            if (track->isPlaying) {
                [track->playerNode stop];
                track->isPlaying = false;
                track->isLooping = false;
            }
        }
    }

    // Stop all active sound effects
    {
        std::lock_guard<std::mutex> lock(activesoundsMutex);
        for (auto& pair : activeSounds) {
            ActiveSound* activeSound = pair.second.get();
            if (activeSound->isPlaying) {
                [activeSound->playerNode stop];
                activeSound->isPlaying = false;
            }
        }
    }

    std::cout << "CoreAudioEngine: All sounds stopped" << std::endl;
}

// System control

void CoreAudioEngine::setMasterVolume(float volume) {
    masterVolume.store(std::clamp(volume, 0.0f, 1.0f));

    if (mainMixer) {
        mainMixer.volume = masterVolume.load();
    }

    std::cout << "CoreAudioEngine: Set master volume to " << masterVolume.load() << std::endl;
}

float CoreAudioEngine::getMasterVolume() const {
    return masterVolume.load();
}

void CoreAudioEngine::setMuted(bool muted) {
    mutedState.store(muted);

    if (mainMixer) {
        mainMixer.volume = muted ? 0.0f : masterVolume.load();
    }

    std::cout << "CoreAudioEngine: " << (muted ? "Muted" : "Unmuted") << " audio" << std::endl;
}

bool CoreAudioEngine::getMuted() const {
    return mutedState.load();
}

// Information methods

size_t CoreAudioEngine::getLoadedMusicCount() const {
    std::lock_guard<std::mutex> lock(musicMutex);
    return musicTracks.size();
}

size_t CoreAudioEngine::getLoadedSoundCount() const {
    std::lock_guard<std::mutex> lock(soundsMutex);
    return soundEffects.size();
}

size_t CoreAudioEngine::getActiveSoundCount() const {
    std::lock_guard<std::mutex> lock(activesoundsMutex);

    // Clean up finished sounds and count active ones
    size_t activeCount = 0;
    for (const auto& pair : activeSounds) {
        if (pair.second->isPlaying) {
            activeCount++;
        }
    }

    return activeCount;
}

// Memory management

void CoreAudioEngine::clearMusicCache() {
    std::lock_guard<std::mutex> lock(musicMutex);

    std::cout << "CoreAudioEngine: Clearing music cache (" << musicTracks.size() << " tracks)..." << std::endl;

    for (auto& pair : musicTracks) {
        cleanupMusicTrack(pair.second.get());
    }

    musicTracks.clear();
    updateMemoryUsage();
}

void CoreAudioEngine::clearSoundCache() {
    std::lock_guard<std::mutex> lock(soundsMutex);

    std::cout << "CoreAudioEngine: Clearing sound cache (" << soundEffects.size() << " sounds)..." << std::endl;

    soundEffects.clear();
    updateMemoryUsage();
}

size_t CoreAudioEngine::getMemoryUsage() const {
    return memoryUsage.load();
}

// Helper methods

AudioMusicTrack* CoreAudioEngine::getMusicTrack(uint32_t music_id) {
    auto it = musicTracks.find(music_id);
    return (it != musicTracks.end()) ? it->second.get() : nullptr;
}

const AudioMusicTrack* CoreAudioEngine::getMusicTrack(uint32_t music_id) const {
    auto it = musicTracks.find(music_id);
    return (it != musicTracks.end()) ? it->second.get() : nullptr;
}

SoundEffect* CoreAudioEngine::getSoundEffect(uint32_t sound_id) {
    auto it = soundEffects.find(sound_id);
    return (it != soundEffects.end()) ? it->second.get() : nullptr;
}

const SoundEffect* CoreAudioEngine::getSoundEffect(uint32_t sound_id) const {
    auto it = soundEffects.find(sound_id);
    return (it != soundEffects.end()) ? it->second.get() : nullptr;
}

void CoreAudioEngine::cleanupMusicTrack(AudioMusicTrack* track) {
    if (!track) return;

    @autoreleasepool {
        if (track->playerNode) {
            if (track->isPlaying) {
                [track->playerNode stop];
            }
            [audioEngine detachNode:track->playerNode];
            track->playerNode = nil;
        }

        track->audioFile = nil;
        track->isPlaying = false;
        track->isLooping = false;
    }
}

uint32_t CoreAudioEngine::generateInstanceId() {
    return nextInstanceId.fetch_add(1);
}

AVAudioFile* CoreAudioEngine::loadAudioFile(const std::string& filename) {
    @autoreleasepool {
        NSString* nsFilename = [NSString stringWithUTF8String:filename.c_str()];
        NSURL* fileURL = [NSURL fileURLWithPath:nsFilename];

        NSError* error = nil;
        AVAudioFile* audioFile = [[AVAudioFile alloc] initForReading:fileURL error:&error];

        if (!audioFile) {
            std::cerr << "CoreAudioEngine: Failed to load audio file '" << filename << "': "
                      << [[error localizedDescription] UTF8String] << std::endl;
        }

        return audioFile;
    }
}

AVAudioPCMBuffer* CoreAudioEngine::loadAudioBuffer(const std::string& filename, AVAudioFormat** outFormat) {
    @autoreleasepool {
        AVAudioFile* audioFile = loadAudioFile(filename);
        if (!audioFile) {
            return nil;
        }

        AVAudioFormat* fileFormat = [audioFile processingFormat];
        AVAudioFrameCount frameCount = (AVAudioFrameCount)[audioFile length];

        AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:fileFormat frameCapacity:frameCount];

        NSError* error = nil;
        BOOL success = [audioFile readIntoBuffer:buffer error:&error];

        if (!success) {
            std::cerr << "CoreAudioEngine: Failed to read audio file '" << filename << "' into buffer: "
                      << [[error localizedDescription] UTF8String] << std::endl;
            return nil;
        }

        if (outFormat) {
            *outFormat = fileFormat;
        }

        return buffer;
    }
}

AVAudioPlayerNode* CoreAudioEngine::createPlayerNode() {
    @autoreleasepool {
        AVAudioPlayerNode* playerNode = [[AVAudioPlayerNode alloc] init];
        [audioEngine attachNode:playerNode];
        return playerNode;
    }
}

void CoreAudioEngine::configurePlayerNode(AVAudioPlayerNode* node, float volume, float pitch, float pan) {
    if (!node) return;

    // Set volume
    node.volume = volume * masterVolume.load();

    // Set pitch (rate)
    if (pitch != 1.0f) {
        // TODO: Implement pitch shifting using AVAudioUnitTimePitch in Phase 2
        // For now, just log that pitch is requested
        if (pitch != 1.0f) {
            std::cout << "CoreAudioEngine: Pitch shifting (" << pitch << ") not implemented yet - Phase 2 feature" << std::endl;
        }
    }

    // Set pan
    if (pan != 0.0f) {
        node.pan = std::clamp(pan, -1.0f, 1.0f);
    }
}

void CoreAudioEngine::updateMemoryUsage() {
    size_t totalSize = 0;

    // Calculate sound buffer sizes
    for (const auto& pair : soundEffects) {
        const SoundEffect* sound = pair.second.get();
        if (sound->buffer) {
            // Rough estimation: frameLength * channels * sizeof(float)
            totalSize += sound->frameLength * [sound->format channelCount] * sizeof(float);
        }
    }

    memoryUsage.store(totalSize);
}

// Audio session management

void CoreAudioEngine::handleAudioInterruption(bool interrupted) {
    if (interrupted) {
        std::cout << "CoreAudioEngine: Audio interrupted - pausing playback" << std::endl;
        // TODO: Pause all active playback
    } else {
        std::cout << "CoreAudioEngine: Audio interruption ended - resuming playback" << std::endl;
        // TODO: Resume paused playback
    }
}

void CoreAudioEngine::handleRouteChange() {
    std::cout << "CoreAudioEngine: Audio route changed" << std::endl;
    // TODO: Handle route changes (headphones plugged/unplugged, etc.)
}
