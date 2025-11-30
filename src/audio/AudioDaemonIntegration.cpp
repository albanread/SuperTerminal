//
//  AudioDaemonIntegration.cpp
//  SuperTerminal - Audio Daemon Integration Layer
//
//  Created by Assistant on 2024-11-25.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "AudioDaemonIntegration.h"
#include "daemon/AudioDaemonClient.h"
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>
#include <sstream>

namespace SuperTerminal {

// Global audio daemon client instance
static std::unique_ptr<AudioDaemon::SimpleAudioClient> g_audioClient;
static std::mutex g_audioClientMutex;
static bool g_audioInitialized = false;

// Initialize audio daemon client
bool initializeAudioDaemon() {
    std::lock_guard<std::mutex> lock(g_audioClientMutex);
    
    if (g_audioInitialized) {
        return true;
    }
    
    try {
        g_audioClient = std::make_unique<AudioDaemon::SimpleAudioClient>();
        g_audioInitialized = true;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize audio daemon client: " << e.what() << std::endl;
        return false;
    }
}

// Shutdown audio daemon client
void shutdownAudioDaemon() {
    std::lock_guard<std::mutex> lock(g_audioClientMutex);
    
    if (g_audioClient) {
        g_audioClient.reset();
    }
    g_audioInitialized = false;
}

// Get audio client (initialize if needed)
AudioDaemon::SimpleAudioClient* getAudioClient() {
    std::lock_guard<std::mutex> lock(g_audioClientMutex);
    
    if (!g_audioInitialized) {
        if (!initializeAudioDaemon()) {
            return nullptr;
        }
    }
    
    return g_audioClient.get();
}

} // namespace SuperTerminal

// C API Implementation for compatibility with existing code
extern "C" {

// Basic audio functions
int audio_beep(float frequency, float duration, float volume) {
    auto* client = SuperTerminal::getAudioClient();
    if (!client) {
        return 0;
    }
    
    return client->beep(frequency, duration) ? 1 : 0;
}

int audio_play_abc(const char* abcData) {
    auto* client = SuperTerminal::getAudioClient();
    if (!client || !abcData) {
        return 0;
    }
    
    return client->playABC(std::string(abcData)) ? 1 : 0;
}

int audio_play_abc_file(const char* filename) {
    auto* client = SuperTerminal::getAudioClient();
    if (!client || !filename) {
        return 0;
    }
    
    return client->playABCFile(std::string(filename)) ? 1 : 0;
}

int audio_play_midi_file(const char* filename) {
    auto* client = SuperTerminal::getAudioClient();
    if (!client || !filename) {
        return 0;
    }
    
    return client->playMIDIFile(std::string(filename)) ? 1 : 0;
}

int audio_stop() {
    auto* client = SuperTerminal::getAudioClient();
    if (!client) {
        return 0;
    }
    
    return client->stop() ? 1 : 0;
}

int audio_set_volume(float volume) {
    auto* client = SuperTerminal::getAudioClient();
    if (!client) {
        return 0;
    }
    
    return client->setVolume(volume) ? 1 : 0;
}

int audio_is_playing() {
    auto* client = SuperTerminal::getAudioClient();
    if (!client) {
        return 0;
    }
    
    return client->isPlaying() ? 1 : 0;
}

// Music slot compatibility functions (for existing ABC player integration)
uint32_t queue_music_slot_with_wait(const char* abc_notation, const char* name, int tempo_bpm, int instrument, bool loop, int wait_after_ms) {
    if (!abc_notation) {
        return 0;
    }
    
    // Convert parameters to ABC notation header
    std::ostringstream oss;
    
    if (name && strlen(name) > 0) {
        oss << "T:" << name << "\n";
    }
    
    if (tempo_bpm > 0) {
        oss << "Q:" << tempo_bpm << "\n";  
    }
    
    // Add key and meter defaults if not present
    std::string notation(abc_notation);
    if (notation.find("K:") == std::string::npos) {
        oss << "K:C\n";
    }
    if (notation.find("M:") == std::string::npos) {
        oss << "M:4/4\n";
    }
    if (notation.find("L:") == std::string::npos) {
        oss << "L:1/8\n";
    }
    
    oss << notation;
    
    auto* client = SuperTerminal::getAudioClient();
    if (!client) {
        return 0;
    }
    
    // For compatibility, we'll play immediately instead of queueing
    if (client->playABC(oss.str())) {
        return 1; // Return non-zero ID for success
    }
    
    return 0;
}

// Advanced synthesis stub functions (not implemented in daemon yet)
bool synth_generate_additive(const char* filename, float fundamental, const float* harmonics, int numHarmonics, float duration) {
    std::cerr << "synth_generate_additive: Not implemented in audio daemon yet" << std::endl;
    return false;
}

bool synth_generate_fm(const char* filename, float carrierFreq, float modulatorFreq, float modIndex, float duration) {
    std::cerr << "synth_generate_fm: Not implemented in audio daemon yet" << std::endl;
    return false;
}

bool synth_generate_granular(const char* filename, float baseFreq, float grainSize, float overlap, float duration) {
    std::cerr << "synth_generate_granular: Not implemented in audio daemon yet" << std::endl;
    return false;
}

bool synth_generate_physical_string(const char* filename, float frequency, float damping, float brightness, float duration) {
    std::cerr << "synth_generate_physical_string: Not implemented in audio daemon yet" << std::endl;
    return false;
}

bool synth_generate_physical_bar(const char* filename, float frequency, float damping, float brightness, float duration) {
    std::cerr << "synth_generate_physical_bar: Not implemented in audio daemon yet" << std::endl;
    return false;
}

bool synth_generate_physical_tube(const char* filename, float frequency, float airPressure, float brightness, float duration) {
    std::cerr << "synth_generate_physical_tube: Not implemented in audio daemon yet" << std::endl;
    return false;
}

bool synth_generate_physical_drum(const char* filename, float frequency, float damping, float excitation, float duration) {
    std::cerr << "synth_generate_physical_drum: Not implemented in audio daemon yet" << std::endl;
    return false;
}

uint32_t synth_create_additive(float fundamental, const float* harmonics, int numHarmonics, float duration) {
    std::cerr << "synth_create_additive: Not implemented in audio daemon yet" << std::endl;
    return 0;
}

uint32_t synth_create_fm(float carrierFreq, float modulatorFreq, float modIndex, float duration) {
    std::cerr << "synth_create_fm: Not implemented in audio daemon yet" << std::endl;
    return 0;
}

uint32_t synth_create_granular(float baseFreq, float grainSize, float overlap, float duration) {
    std::cerr << "synth_create_granular: Not implemented in audio daemon yet" << std::endl;
    return 0;
}

uint32_t synth_create_physical_string(float frequency, float damping, float brightness, float duration) {
    std::cerr << "synth_create_physical_string: Not implemented in audio daemon yet" << std::endl;
    return 0;
}

uint32_t synth_create_physical_bar(float frequency, float damping, float brightness, float duration) {
    std::cerr << "synth_create_physical_bar: Not implemented in audio daemon yet" << std::endl;
    return 0;
}

uint32_t synth_create_physical_tube(float frequency, float airPressure, float brightness, float duration) {
    std::cerr << "synth_create_physical_tube: Not implemented in audio daemon yet" << std::endl;
    return 0;
}

uint32_t synth_create_physical_drum(float frequency, float damping, float excitation, float duration) {
    std::cerr << "synth_create_physical_drum: Not implemented in audio daemon yet" << std::endl;
    return 0;
}

bool synth_add_effect(uint32_t soundId, const char* effectType) {
    std::cerr << "synth_add_effect: Not implemented in audio daemon yet" << std::endl;
    return false;
}

bool synth_remove_effect(uint32_t soundId, const char* effectType) {
    std::cerr << "synth_remove_effect: Not implemented in audio daemon yet" << std::endl;
    return false;
}

bool synth_set_effect_param(uint32_t soundId, const char* effectType, const char* paramName, float value) {
    std::cerr << "synth_set_effect_param: Not implemented in audio daemon yet" << std::endl;
    return false;
}

// Simple beep function for backward compatibility
void beep(float frequency, float duration) {
    audio_beep(frequency, duration, 0.5f);
}

// Music playback functions for backward compatibility
void music_play(const char* abc_notation) {
    audio_play_abc(abc_notation);
}

void music_stop() {
    audio_stop();
}

bool music_is_playing() {
    return audio_is_playing() != 0;
}

void music_set_volume(float volume) {
    audio_set_volume(volume);
}

// File playback functions
void music_play_file(const char* filename) {
    // Determine file type by extension
    if (!filename) return;
    
    std::string fname(filename);
    size_t dotPos = fname.find_last_of('.');
    if (dotPos != std::string::npos) {
        std::string ext = fname.substr(dotPos + 1);
        
        // Convert to lowercase
        for (char& c : ext) {
            c = tolower(c);
        }
        
        if (ext == "abc") {
            audio_play_abc_file(filename);
        } else if (ext == "mid" || ext == "midi") {
            audio_play_midi_file(filename);
        } else {
            // Try as ABC file by default
            audio_play_abc_file(filename);
        }
    } else {
        // No extension, try as ABC
        audio_play_abc_file(filename);
    }
}

// Initialize/shutdown functions
void audio_system_init() {
    SuperTerminal::initializeAudioDaemon();
}

void audio_system_shutdown() {
    SuperTerminal::shutdownAudioDaemon();
}

// Status and control functions
float audio_get_volume() {
    // Not easily retrievable from daemon, return default
    return 1.0f;
}

void audio_pause() {
    // Not implemented in simple client yet
    std::cerr << "audio_pause: Not implemented yet" << std::endl;
}

void audio_resume() {
    // Not implemented in simple client yet  
    std::cerr << "audio_resume: Not implemented yet" << std::endl;
}

double audio_get_position() {
    // Not implemented in simple client yet
    return 0.0;
}

double audio_get_duration() {
    // Not implemented in simple client yet
    return 0.0;
}

void audio_seek(double position) {
    // Not implemented in simple client yet
    std::cerr << "audio_seek: Not implemented yet" << std::endl;
}

} // extern "C"