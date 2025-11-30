//
//  AudioDaemonIntegration.h
//  SuperTerminal - Audio Daemon Integration Layer
//
//  Created by Assistant on 2024-11-25.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#pragma once

#include <cstdint>

namespace SuperTerminal {

// Forward declarations
namespace AudioDaemon {
    class SimpleAudioClient;
}

// Initialize audio daemon client
bool initializeAudioDaemon();

// Shutdown audio daemon client
void shutdownAudioDaemon();

// Get audio client (initialize if needed)
AudioDaemon::SimpleAudioClient* getAudioClient();

} // namespace SuperTerminal

// C API for compatibility with existing code
extern "C" {

// Basic audio functions
int audio_beep(float frequency, float duration, float volume);
int audio_play_abc(const char* abcData);
int audio_play_abc_file(const char* filename);
int audio_play_midi_file(const char* filename);
int audio_stop();
int audio_set_volume(float volume);
int audio_is_playing();

// Music slot compatibility functions
uint32_t queue_music_slot_with_wait(const char* abc_notation, const char* name, 
                                   int tempo_bpm, int instrument, bool loop, int wait_after_ms);

// Advanced synthesis stub functions (for future implementation)
bool synth_generate_additive(const char* filename, float fundamental, const float* harmonics, 
                           int numHarmonics, float duration);
bool synth_generate_fm(const char* filename, float carrierFreq, float modulatorFreq, 
                      float modIndex, float duration);
bool synth_generate_granular(const char* filename, float baseFreq, float grainSize, 
                           float overlap, float duration);
bool synth_generate_physical_string(const char* filename, float frequency, float damping, 
                                  float brightness, float duration);
bool synth_generate_physical_bar(const char* filename, float frequency, float damping, 
                                float brightness, float duration);
bool synth_generate_physical_tube(const char* filename, float frequency, float airPressure, 
                                 float brightness, float duration);
bool synth_generate_physical_drum(const char* filename, float frequency, float damping, 
                                 float excitation, float duration);

uint32_t synth_create_additive(float fundamental, const float* harmonics, int numHarmonics, float duration);
uint32_t synth_create_fm(float carrierFreq, float modulatorFreq, float modIndex, float duration);
uint32_t synth_create_granular(float baseFreq, float grainSize, float overlap, float duration);
uint32_t synth_create_physical_string(float frequency, float damping, float brightness, float duration);
uint32_t synth_create_physical_bar(float frequency, float damping, float brightness, float duration);
uint32_t synth_create_physical_tube(float frequency, float airPressure, float brightness, float duration);
uint32_t synth_create_physical_drum(float frequency, float damping, float excitation, float duration);

bool synth_add_effect(uint32_t soundId, const char* effectType);
bool synth_remove_effect(uint32_t soundId, const char* effectType);
bool synth_set_effect_param(uint32_t soundId, const char* effectType, const char* paramName, float value);

// Simple compatibility functions
void beep(float frequency, float duration);
void music_play(const char* abc_notation);
void music_stop();
bool music_is_playing();
void music_set_volume(float volume);
void music_play_file(const char* filename);

// System lifecycle
void audio_system_init();
void audio_system_shutdown();

// Extended control functions
float audio_get_volume();
void audio_pause();
void audio_resume();
double audio_get_position();
double audio_get_duration();
void audio_seek(double position);

}

// C++ API (not extern "C")
#include <vector>
bool audio_export_sound_to_wav_bytes(uint32_t sound_id, std::vector<uint8_t>& outWAVData);