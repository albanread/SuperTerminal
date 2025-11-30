//
//  AudioLuaBindings.cpp
//  SuperTerminal Framework - Audio Lua Bindings Implementation
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "AudioLuaBindings.h"
#include "AudioSystem.h"
#include "SynthEngine.h"
#include "MidiEventCapture.h"
#include "ABCPlayerXPCClient.h"
#include "SuperTerminal.h"

#include <iostream>
#include <cstring>

// External function from MidiLuaBindings.mm
extern void registerMidiLuaBindings(lua_State* L, SuperTerminal::MidiEngine* midiEngine);

// External references
extern std::unique_ptr<SynthEngine> g_synthEngine;

// Helper functions for Lua bindings
extern std::unique_ptr<AudioSystem> g_audioSystem;

// External C API functions
extern "C" {
    uint32_t queue_music_slot_with_wait(const char* abc_notation, const char* name, int tempo_bpm, int instrument, bool loop, int wait_after_ms);
    
    // ABC Player Client forward declarations
    const char* abc_client_get_current_song();
    
    // Advanced synthesis C interface
    bool synth_generate_additive(const char* filename, float fundamental, const float* harmonics, int numHarmonics, float duration);
    bool synth_generate_fm(const char* filename, float carrierFreq, float modulatorFreq, float modIndex, float duration);
    bool synth_generate_granular(const char* filename, float baseFreq, float grainSize, float overlap, float duration);
    bool synth_generate_physical_string(const char* filename, float frequency, float damping, float brightness, float duration);
    bool synth_generate_physical_bar(const char* filename, float frequency, float damping, float brightness, float duration);
    bool synth_generate_physical_tube(const char* filename, float frequency, float airPressure, float brightness, float duration);
    bool synth_generate_physical_drum(const char* filename, float frequency, float damping, float excitation, float duration);
    
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
    
    bool synth_save_preset(const char* presetName, const char* presetData);
    const char* synth_load_preset(const char* presetName);
    bool synth_apply_preset(uint32_t soundId, const char* presetName);
    
    // Music debug control functions
    void set_music_debug_output(bool enabled);
    bool get_music_debug_output();
    void set_music_parser_debug_output(bool enabled);
    bool get_music_parser_debug_output();
}

// Forward declarations for Lua wrapper functions
int lua_synth_generate_additive(lua_State* L);
int lua_synth_generate_fm(lua_State* L);  
int lua_synth_generate_granular(lua_State* L);
int lua_synth_generate_physical_string(lua_State* L);
int lua_synth_generate_physical_bar(lua_State* L);
int lua_synth_generate_physical_tube(lua_State* L);
int lua_synth_generate_physical_drum(lua_State* L);

int lua_synth_create_additive(lua_State* L);
int lua_synth_create_fm(lua_State* L);
int lua_synth_create_granular(lua_State* L);
int lua_synth_create_physical_string(lua_State* L);
int lua_synth_create_physical_bar(lua_State* L);
int lua_synth_create_physical_tube(lua_State* L);
int lua_synth_create_physical_drum(lua_State* L);

int lua_synth_add_effect(lua_State* L);
int lua_synth_remove_effect(lua_State* L);
int lua_synth_set_effect_param(lua_State* L);

int lua_synth_save_preset(lua_State* L);
int lua_synth_load_preset(lua_State* L);
int lua_synth_apply_preset(lua_State* L);

// ABC MIDI export functions
int lua_abc_export_midi(lua_State* L);
int lua_abc_export_midi_from_file(lua_State* L);

// Helper functions implementation

namespace AudioLuaHelpers {

bool checkAudioInitialized(lua_State* L) {
    if (!g_audioSystem || !g_audioSystem->isInitialized()) {
        luaError(L, "Audio system not initialized. Call audio_initialize() first.");
        return false;
    }
    return true;
}

bool validateMusicId(lua_State* L, int arg) {
    if (!lua_isnumber(L, arg)) {
        luaErrorf(L, "Expected number for music ID at argument %d", arg);
        return false;
    }
    
    lua_Number id = lua_tonumber(L, arg);
    if (id < 1 || id > UINT32_MAX) {
        luaErrorf(L, "Invalid music ID %g (must be between 1 and %u)", id, UINT32_MAX);
        return false;
    }
    
    return true;
}

bool validateSoundId(lua_State* L, int arg) {
    if (!lua_isnumber(L, arg)) {
        luaErrorf(L, "Expected number for sound ID at argument %d", arg);
        return false;
    }
    
    lua_Number id = lua_tonumber(L, arg);
    if (id < 1 || id > UINT32_MAX) {
        luaErrorf(L, "Invalid sound ID %g (must be between 1 and %u)", id, UINT32_MAX);
        return false;
    }
    
    return true;
}

bool validateVolume(lua_State* L, int arg, float& volume) {
    if (!lua_isnumber(L, arg)) {
        luaErrorf(L, "Expected number for volume at argument %d", arg);
        return false;
    }
    
    volume = (float)lua_tonumber(L, arg);
    if (volume < AudioLuaConstants::VOLUME_MIN || volume > AudioLuaConstants::VOLUME_MAX) {
        luaErrorf(L, "Invalid volume %g (must be between %g and %g)", 
                 volume, AudioLuaConstants::VOLUME_MIN, AudioLuaConstants::VOLUME_MAX);
        return false;
    }
    
    return true;
}

bool validatePitch(lua_State* L, int arg, float& pitch) {
    if (!lua_isnumber(L, arg)) {
        luaErrorf(L, "Expected number for pitch at argument %d", arg);
        return false;
    }
    
    pitch = (float)lua_tonumber(L, arg);
    if (pitch < AudioLuaConstants::PITCH_MIN || pitch > AudioLuaConstants::PITCH_MAX) {
        luaErrorf(L, "Invalid pitch %g (must be between %g and %g)", 
                 pitch, AudioLuaConstants::PITCH_MIN, AudioLuaConstants::PITCH_MAX);
        return false;
    }
    
    return true;
}

bool validatePan(lua_State* L, int arg, float& pan) {
    if (!lua_isnumber(L, arg)) {
        luaErrorf(L, "Expected number for pan at argument %d", arg);
        return false;
    }
    
    pan = (float)lua_tonumber(L, arg);
    if (pan < AudioLuaConstants::PAN_MIN || pan > AudioLuaConstants::PAN_MAX) {
        luaErrorf(L, "Invalid pan %g (must be between %g and %g)", 
                 pan, AudioLuaConstants::PAN_MIN, AudioLuaConstants::PAN_MAX);
        return false;
    }
    
    return true;
}

bool validateFrequency(lua_State* L, int arg, float& frequency) {
    if (!lua_isnumber(L, arg)) {
        luaErrorf(L, "Expected number for frequency at argument %d", arg);
        return false;
    }
    
    frequency = (float)lua_tonumber(L, arg);
    if (frequency < AudioLuaConstants::FREQUENCY_MIN || frequency > AudioLuaConstants::FREQUENCY_MAX) {
        luaErrorf(L, "Invalid frequency %g (must be between %g and %g)", 
                 frequency, AudioLuaConstants::FREQUENCY_MIN, AudioLuaConstants::FREQUENCY_MAX);
        return false;
    }
    
    return true;
}

bool validateWaveform(lua_State* L, int arg, int& waveform) {
    if (!lua_isnumber(L, arg)) {
        luaErrorf(L, "Expected number for waveform at argument %d", arg);
        return false;
    }
    
    waveform = (int)lua_tonumber(L, arg);
    if (waveform < AudioLuaConstants::WAVE_SINE || waveform > AudioLuaConstants::WAVE_NOISE) {
        luaErrorf(L, "Invalid waveform %d (must be between %d and %d)", 
                 waveform, AudioLuaConstants::WAVE_SINE, AudioLuaConstants::WAVE_NOISE);
        return false;
    }
    
    return true;
}

bool validateMIDIChannel(lua_State* L, int arg, uint8_t& channel) {
    if (!lua_isnumber(L, arg)) {
        luaErrorf(L, "Expected number for MIDI channel at argument %d", arg);
        return false;
    }
    
    int ch = (int)lua_tonumber(L, arg);
    if (ch < AudioLuaConstants::MIDI_CHANNEL_MIN || ch > AudioLuaConstants::MIDI_CHANNEL_MAX) {
        luaErrorf(L, "Invalid MIDI channel %d (must be between %d and %d)", 
                 ch, AudioLuaConstants::MIDI_CHANNEL_MIN, AudioLuaConstants::MIDI_CHANNEL_MAX);
        return false;
    }
    
    channel = (uint8_t)ch;
    return true;
}

bool validateMIDINote(lua_State* L, int arg, uint8_t& note) {
    if (!lua_isnumber(L, arg)) {
        luaErrorf(L, "Expected number for MIDI note at argument %d", arg);
        return false;
    }
    
    int n = (int)lua_tonumber(L, arg);
    if (n < AudioLuaConstants::MIDI_NOTE_MIN || n > AudioLuaConstants::MIDI_NOTE_MAX) {
        luaErrorf(L, "Invalid MIDI note %d (must be between %d and %d)", 
                 n, AudioLuaConstants::MIDI_NOTE_MIN, AudioLuaConstants::MIDI_NOTE_MAX);
        return false;
    }
    
    note = (uint8_t)n;
    return true;
}

bool validateMIDIVelocity(lua_State* L, int arg, uint8_t& velocity) {
    if (!lua_isnumber(L, arg)) {
        luaErrorf(L, "Expected number for MIDI velocity at argument %d", arg);
        return false;
    }
    
    int v = (int)lua_tonumber(L, arg);
    if (v < AudioLuaConstants::MIDI_VELOCITY_MIN || v > AudioLuaConstants::MIDI_VELOCITY_MAX) {
        luaErrorf(L, "Invalid MIDI velocity %d (must be between %d and %d)", 
                 v, AudioLuaConstants::MIDI_VELOCITY_MIN, AudioLuaConstants::MIDI_VELOCITY_MAX);
        return false;
    }
    
    velocity = (uint8_t)v;
    return true;
}

int luaError(lua_State* L, const char* message) {
    lua_pushstring(L, message);
    return lua_error(L);
}

int luaErrorf(lua_State* L, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    
    va_end(args);
    
    lua_pushstring(L, buffer);
    return lua_error(L);
}

bool isValidFilename(const char* filename) {
    if (!filename || strlen(filename) == 0) {
        return false;
    }
    
    // Basic filename validation
    size_t len = strlen(filename);
    if (len >= 256) {  // Match AudioCommand filename size
        return false;
    }
    
    return true;
}

const char* getFilename(lua_State* L, int arg) {
    if (!lua_isstring(L, arg)) {
        luaErrorf(L, "Expected string for filename at argument %d", arg);
        return nullptr;
    }
    
    const char* filename = lua_tostring(L, arg);
    if (!isValidFilename(filename)) {
        luaErrorf(L, "Invalid filename at argument %d", arg);
        return nullptr;
    }
    
    return filename;
}

} // namespace AudioLuaHelpers

// Lua binding function implementations

extern "C" {

// System functions

int lua_audio_initialize(lua_State* L) {
    bool success = audio_initialize();
    lua_pushboolean(L, success);
    return 1;
}

int lua_audio_shutdown(lua_State* L) {
    audio_shutdown();
    return 0;
}

int lua_audio_is_initialized(lua_State* L) {
    bool initialized = audio_is_initialized();
    lua_pushboolean(L, initialized);
    return 1;
}

int lua_audio_wait_queue_empty(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    audio_wait_queue_empty();
    return 0;
}

// Music functions

int lua_audio_load_music(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    // Arguments: filename, music_id
    if (lua_gettop(L) != 2) {
        return AudioLuaHelpers::luaError(L, "load_music() expects 2 arguments: filename, music_id");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    if (!AudioLuaHelpers::validateMusicId(L, 2)) return 0;
    uint32_t music_id = (uint32_t)lua_tonumber(L, 2);
    
    bool success = audio_load_music(filename, music_id);
    lua_pushboolean(L, success);
    return 1;
}

int lua_audio_play_music(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    // Arguments: music_id, [volume], [loop]
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "play_music() expects 1-3 arguments: music_id, [volume], [loop]");
    }
    
    if (!AudioLuaHelpers::validateMusicId(L, 1)) return 0;
    uint32_t music_id = (uint32_t)lua_tonumber(L, 1);
    
    float volume = 1.0f;
    if (argc >= 2) {
        if (!AudioLuaHelpers::validateVolume(L, 2, volume)) return 0;
    }
    
    bool loop = true;
    if (argc >= 3) {
        if (!lua_isboolean(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected boolean for loop at argument 3");
        }
        loop = lua_toboolean(L, 3);
    }
    
    audio_play_music(music_id, volume, loop);
    return 0;
}

int lua_audio_stop_music(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "stop_music() expects 1 argument: music_id");
    }
    
    if (!AudioLuaHelpers::validateMusicId(L, 1)) return 0;
    uint32_t music_id = (uint32_t)lua_tonumber(L, 1);
    
    audio_stop_music(music_id);
    return 0;
}

int lua_audio_set_music_volume(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 2) {
        return AudioLuaHelpers::luaError(L, "set_music_volume() expects 2 arguments: music_id, volume");
    }
    
    if (!AudioLuaHelpers::validateMusicId(L, 1)) return 0;
    uint32_t music_id = (uint32_t)lua_tonumber(L, 1);
    
    float volume;
    if (!AudioLuaHelpers::validateVolume(L, 2, volume)) return 0;
    
    audio_set_music_volume(music_id, volume);
    return 0;
}

// Sound effect functions

int lua_audio_load_sound(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "load_sound() expects 1 argument: filename");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    uint32_t sound_id = audio_load_sound(filename);
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_audio_load_sound_with_id(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 2) {
        return AudioLuaHelpers::luaError(L, "load_sound_with_id() expects 2 arguments: filename, sound_id");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    if (!AudioLuaHelpers::validateSoundId(L, 2)) return 0;
    uint32_t sound_id = (uint32_t)lua_tonumber(L, 2);
    
    bool success = audio_load_sound_with_id(filename, sound_id);
    lua_pushboolean(L, success);
    return 1;
}

int lua_audio_play_sound(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    // Arguments: sound_id, [volume], [pitch], [pan]
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 4) {
        return AudioLuaHelpers::luaError(L, "play_sound() expects 1-4 arguments: sound_id, [volume], [pitch], [pan]");
    }
    
    if (!AudioLuaHelpers::validateSoundId(L, 1)) return 0;
    uint32_t sound_id = (uint32_t)lua_tonumber(L, 1);
    
    float volume = 1.0f;
    if (argc >= 2) {
        if (!AudioLuaHelpers::validateVolume(L, 2, volume)) return 0;
    }
    
    float pitch = 1.0f;
    if (argc >= 3) {
        if (!AudioLuaHelpers::validatePitch(L, 3, pitch)) return 0;
    }
    
    float pan = 0.0f;
    if (argc >= 4) {
        if (!AudioLuaHelpers::validatePan(L, 4, pan)) return 0;
    }
    
    audio_play_sound(sound_id, volume, pitch, pan);
    return 0;
}

int lua_audio_stop_sound(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "stop_sound() expects 1 argument: sound_id");
    }
    
    if (!AudioLuaHelpers::validateSoundId(L, 1)) return 0;
    uint32_t sound_id = (uint32_t)lua_tonumber(L, 1);
    
    audio_stop_sound(sound_id);
    return 0;
}

// Synthesis functions (Phase 2 stubs)

int lua_audio_create_oscillator(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    // Arguments: frequency, [waveform]
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 2) {
        return AudioLuaHelpers::luaError(L, "create_oscillator() expects 1-2 arguments: frequency, [waveform]");
    }
    
    float frequency;
    if (!AudioLuaHelpers::validateFrequency(L, 1, frequency)) return 0;
    
    int waveform = AudioLuaConstants::WAVE_SINE;
    if (argc >= 2) {
        if (!AudioLuaHelpers::validateWaveform(L, 2, waveform)) return 0;
    }
    
    uint32_t osc_id = audio_create_oscillator(frequency, waveform);
    lua_pushnumber(L, osc_id);
    return 1;
}

int lua_audio_set_oscillator_frequency(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 2) {
        return AudioLuaHelpers::luaError(L, "set_oscillator_frequency() expects 2 arguments: osc_id, frequency");
    }
    
    if (!lua_isnumber(L, 1)) {
        return AudioLuaHelpers::luaError(L, "Expected number for oscillator ID at argument 1");
    }
    uint32_t osc_id = (uint32_t)lua_tonumber(L, 1);
    
    float frequency;
    if (!AudioLuaHelpers::validateFrequency(L, 2, frequency)) return 0;
    
    audio_set_oscillator_frequency(osc_id, frequency);
    return 0;
}

int lua_audio_set_oscillator_waveform(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 2) {
        return AudioLuaHelpers::luaError(L, "set_oscillator_waveform() expects 2 arguments: osc_id, waveform");
    }
    
    if (!lua_isnumber(L, 1)) {
        return AudioLuaHelpers::luaError(L, "Expected number for oscillator ID at argument 1");
    }
    uint32_t osc_id = (uint32_t)lua_tonumber(L, 1);
    
    int waveform;
    if (!AudioLuaHelpers::validateWaveform(L, 2, waveform)) return 0;
    
    audio_set_oscillator_waveform(osc_id, waveform);
    return 0;
}

int lua_audio_set_oscillator_volume(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 2) {
        return AudioLuaHelpers::luaError(L, "set_oscillator_volume() expects 2 arguments: osc_id, volume");
    }
    
    if (!lua_isnumber(L, 1)) {
        return AudioLuaHelpers::luaError(L, "Expected number for oscillator ID at argument 1");
    }
    uint32_t osc_id = (uint32_t)lua_tonumber(L, 1);
    
    float volume;
    if (!AudioLuaHelpers::validateVolume(L, 2, volume)) return 0;
    
    audio_set_oscillator_volume(osc_id, volume);
    return 0;
}

int lua_audio_delete_oscillator(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "delete_oscillator() expects 1 argument: osc_id");
    }
    
    if (!lua_isnumber(L, 1)) {
        return AudioLuaHelpers::luaError(L, "Expected number for oscillator ID at argument 1");
    }
    uint32_t osc_id = (uint32_t)lua_tonumber(L, 1);
    
    audio_delete_oscillator(osc_id);
    return 0;
}

// MIDI functions (Phase 2 stubs)

int lua_audio_load_midi(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 2) {
        return AudioLuaHelpers::luaError(L, "load_midi() expects 2 arguments: filename, midi_id");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    if (!lua_isnumber(L, 2)) {
        return AudioLuaHelpers::luaError(L, "Expected number for MIDI ID at argument 2");
    }
    uint32_t midi_id = (uint32_t)lua_tonumber(L, 2);
    
    bool success = audio_load_midi(filename, midi_id);
    lua_pushboolean(L, success);
    return 1;
}

int lua_audio_play_midi(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    // Arguments: midi_id, [tempo]
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 2) {
        return AudioLuaHelpers::luaError(L, "play_midi() expects 1-2 arguments: midi_id, [tempo]");
    }
    
    if (!lua_isnumber(L, 1)) {
        return AudioLuaHelpers::luaError(L, "Expected number for MIDI ID at argument 1");
    }
    uint32_t midi_id = (uint32_t)lua_tonumber(L, 1);
    
    float tempo = 1.0f;
    if (argc >= 2) {
        if (!lua_isnumber(L, 2)) {
            return AudioLuaHelpers::luaError(L, "Expected number for tempo at argument 2");
        }
        tempo = (float)lua_tonumber(L, 2);
        if (tempo <= 0.0f || tempo > 4.0f) {
            return AudioLuaHelpers::luaError(L, "Invalid tempo (must be between 0.1 and 4.0)");
        }
    }
    
    audio_play_midi(midi_id, tempo);
    return 0;
}

int lua_audio_stop_midi(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "stop_midi() expects 1 argument: midi_id");
    }
    
    if (!lua_isnumber(L, 1)) {
        return AudioLuaHelpers::luaError(L, "Expected number for MIDI ID at argument 1");
    }
    uint32_t midi_id = (uint32_t)lua_tonumber(L, 1);
    
    audio_stop_midi(midi_id);
    return 0;
}

int lua_audio_midi_note_on(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 3) {
        return AudioLuaHelpers::luaError(L, "midi_note_on() expects 3 arguments: channel, note, velocity");
    }
    
    uint8_t channel, note, velocity;
    if (!AudioLuaHelpers::validateMIDIChannel(L, 1, channel)) return 0;
    if (!AudioLuaHelpers::validateMIDINote(L, 2, note)) return 0;
    if (!AudioLuaHelpers::validateMIDIVelocity(L, 3, velocity)) return 0;
    
    audio_midi_note_on(channel, note, velocity);
    return 0;
}

int lua_audio_midi_note_off(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 2) {
        return AudioLuaHelpers::luaError(L, "midi_note_off() expects 2 arguments: channel, note");
    }
    
    uint8_t channel, note;
    if (!AudioLuaHelpers::validateMIDIChannel(L, 1, channel)) return 0;
    if (!AudioLuaHelpers::validateMIDINote(L, 2, note)) return 0;
    
    audio_midi_note_off(channel, note);
    return 0;
}

int lua_audio_midi_control_change(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 3) {
        return AudioLuaHelpers::luaError(L, "midi_control_change() expects 3 arguments: channel, controller, value");
    }
    
    uint8_t channel, controller, value;
    if (!AudioLuaHelpers::validateMIDIChannel(L, 1, channel)) return 0;
    
    if (!lua_isnumber(L, 2)) {
        return AudioLuaHelpers::luaError(L, "Expected number for controller at argument 2");
    }
    int ctrl = (int)lua_tonumber(L, 2);
    if (ctrl < 0 || ctrl > 127) {
        return AudioLuaHelpers::luaError(L, "Invalid controller (must be between 0 and 127)");
    }
    controller = (uint8_t)ctrl;
    
    if (!lua_isnumber(L, 3)) {
        return AudioLuaHelpers::luaError(L, "Expected number for value at argument 3");
    }
    int val = (int)lua_tonumber(L, 3);
    if (val < 0 || val > 127) {
        return AudioLuaHelpers::luaError(L, "Invalid value (must be between 0 and 127)");
    }
    value = (uint8_t)val;
    
    audio_midi_control_change(channel, controller, value);
    return 0;
}

int lua_audio_midi_program_change(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 2) {
        return AudioLuaHelpers::luaError(L, "midi_program_change() expects 2 arguments: channel, program");
    }
    
    uint8_t channel, program;
    if (!AudioLuaHelpers::validateMIDIChannel(L, 1, channel)) return 0;
    
    if (!lua_isnumber(L, 2)) {
        return AudioLuaHelpers::luaError(L, "Expected number for program at argument 2");
    }
    int prog = (int)lua_tonumber(L, 2);
    if (prog < 0 || prog > 127) {
        return AudioLuaHelpers::luaError(L, "Invalid program (must be between 0 and 127)");
    }
    program = (uint8_t)prog;
    
    audio_midi_program_change(channel, program);
    return 0;
}

// Information functions

int lua_audio_get_loaded_sound_count(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        lua_pushnumber(L, 0);
        return 1;
    }
    
    size_t count = audio_get_loaded_sound_count();
    lua_pushnumber(L, count);
    return 1;
}

int lua_audio_get_active_voice_count(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        lua_pushnumber(L, 0);
        return 1;
    }
    
    size_t count = audio_get_active_voice_count();
    lua_pushnumber(L, count);
    return 1;
}

int lua_audio_get_cpu_usage(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        lua_pushnumber(L, 0.0);
        return 1;
    }
    
    float usage = audio_get_cpu_usage();
    lua_pushnumber(L, usage);
    return 1;
}

// Utility functions

int lua_audio_set_master_volume(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "set_master_volume() expects 1 argument: volume");
    }
    
    float volume;
    if (!AudioLuaHelpers::validateVolume(L, 1, volume)) return 0;
    
    if (g_audioSystem) {
        // TODO: Add setMasterVolume to AudioSystem interface
        std::cout << "set_master_volume(" << volume << ") - not implemented yet" << std::endl;
    }
    
    return 0;
}

int lua_audio_get_master_volume(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        lua_pushnumber(L, 1.0);
        return 1;
    }
    
    // TODO: Add getMasterVolume to AudioSystem interface
    lua_pushnumber(L, 1.0);  // Default for now
    return 1;
}

int lua_audio_set_muted(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }
    
    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "set_muted() expects 1 argument: muted");
    }
    
    if (!lua_isboolean(L, 1)) {
        return AudioLuaHelpers::luaError(L, "Expected boolean for muted at argument 1");
    }
    
    bool muted = lua_toboolean(L, 1);
    
    // TODO: Add setMuted to AudioSystem interface
    std::cout << "set_muted(" << (muted ? "true" : "false") << ") - not implemented yet" << std::endl;
    
    return 0;
}

int lua_audio_is_muted(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        lua_pushboolean(L, false);
        return 1;
    }
    
    // TODO: Add isMuted to AudioSystem interface
    lua_pushboolean(L, false);  // Default for now
    return 1;
}

// Constants

int lua_audio_get_wave_constants(lua_State* L) {
    lua_newtable(L);
    
    lua_pushstring(L, "SINE");
    lua_pushnumber(L, AudioLuaConstants::WAVE_SINE);
    lua_settable(L, -3);
    
    lua_pushstring(L, "SQUARE");
    lua_pushnumber(L, AudioLuaConstants::WAVE_SQUARE);
    lua_settable(L, -3);
    
    lua_pushstring(L, "SAWTOOTH");
    lua_pushnumber(L, AudioLuaConstants::WAVE_SAWTOOTH);
    lua_settable(L, -3);
    
    lua_pushstring(L, "TRIANGLE");
    lua_pushnumber(L, AudioLuaConstants::WAVE_TRIANGLE);
    lua_settable(L, -3);
    
    lua_pushstring(L, "NOISE");
    lua_pushnumber(L, AudioLuaConstants::WAVE_NOISE);
    lua_settable(L, -3);
    
    return 1;
}

} // extern "C"

// Synthesis functions implementation

int lua_synth_initialize(lua_State* L) {
    bool success = synth_initialize();
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_shutdown(lua_State* L) {
    synth_shutdown();
    return 0;
}

int lua_synth_is_initialized(lua_State* L) {
    bool initialized = synth_is_initialized();
    lua_pushboolean(L, initialized);
    return 1;
}

int lua_synth_generate_beep(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_beep() expects 1-3 arguments: filename, [frequency], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float frequency = 800.0f;
    if (argc >= 2) {
        if (!AudioLuaHelpers::validateFrequency(L, 2, frequency)) return 0;
    }
    
    float duration = 0.2f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_beep(filename, frequency, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_bang(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_bang() expects 1-3 arguments: filename, [intensity], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float intensity = 1.0f;
    if (argc >= 2) {
        if (!lua_isnumber(L, 2)) {
            return AudioLuaHelpers::luaError(L, "Expected number for intensity at argument 2");
        }
        intensity = (float)lua_tonumber(L, 2);
        if (intensity <= 0.0f || intensity > 5.0f) {
            return AudioLuaHelpers::luaError(L, "Intensity must be between 0.0 and 5.0");
        }
    }
    
    float duration = 0.3f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_bang(filename, intensity, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_explode(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_explode() expects 1-3 arguments: filename, [size], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float size = 1.0f;
    if (argc >= 2) {
        if (!lua_isnumber(L, 2)) {
            return AudioLuaHelpers::luaError(L, "Expected number for size at argument 2");
        }
        size = (float)lua_tonumber(L, 2);
        if (size <= 0.0f || size > 5.0f) {
            return AudioLuaHelpers::luaError(L, "Size must be between 0.0 and 5.0");
        }
    }
    
    float duration = 1.0f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_explode(filename, size, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_big_explosion(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_big_explosion() expects 1-3 arguments: filename, [size], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float size = 1.0f;
    if (argc >= 2) {
        if (!lua_isnumber(L, 2)) {
            return AudioLuaHelpers::luaError(L, "Expected number for size at argument 2");
        }
        size = (float)lua_tonumber(L, 2);
        if (size <= 0.0f || size > 5.0f) {
            return AudioLuaHelpers::luaError(L, "Size must be between 0.0 and 5.0");
        }
    }
    
    float duration = 2.0f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_big_explosion(filename, size, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_small_explosion(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_small_explosion() expects 1-3 arguments: filename, [intensity], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float intensity = 1.0f;
    if (argc >= 2) {
        if (!lua_isnumber(L, 2)) {
            return AudioLuaHelpers::luaError(L, "Expected number for intensity at argument 2");
        }
        intensity = (float)lua_tonumber(L, 2);
        if (intensity <= 0.0f || intensity > 5.0f) {
            return AudioLuaHelpers::luaError(L, "Intensity must be between 0.0 and 5.0");
        }
    }
    
    float duration = 0.5f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_small_explosion(filename, intensity, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_distant_explosion(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_distant_explosion() expects 1-3 arguments: filename, [distance], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float distance = 1.0f;
    if (argc >= 2) {
        if (!lua_isnumber(L, 2)) {
            return AudioLuaHelpers::luaError(L, "Expected number for distance at argument 2");
        }
        distance = (float)lua_tonumber(L, 2);
        if (distance <= 0.0f || distance > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Distance must be between 0.0 and 10.0");
        }
    }
    
    float duration = 1.5f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_distant_explosion(filename, distance, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_metal_explosion(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_metal_explosion() expects 1-3 arguments: filename, [shrapnel], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float shrapnel = 1.0f;
    if (argc >= 2) {
        if (!lua_isnumber(L, 2)) {
            return AudioLuaHelpers::luaError(L, "Expected number for shrapnel at argument 2");
        }
        shrapnel = (float)lua_tonumber(L, 2);
        if (shrapnel <= 0.0f || shrapnel > 5.0f) {
            return AudioLuaHelpers::luaError(L, "Shrapnel must be between 0.0 and 5.0");
        }
    }
    
    float duration = 1.2f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_metal_explosion(filename, shrapnel, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_zap(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_zap() expects 1-3 arguments: filename, [frequency], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float frequency = 2000.0f;
    if (argc >= 2) {
        if (!AudioLuaHelpers::validateFrequency(L, 2, frequency)) return 0;
    }
    
    float duration = 0.15f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_zap(filename, frequency, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_coin(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_coin() expects 1-3 arguments: filename, [pitch], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float pitch = 1.0f;
    if (argc >= 2) {
        if (!AudioLuaHelpers::validatePitch(L, 2, pitch)) return 0;
    }
    
    float duration = 0.4f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_coin(filename, pitch, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_jump(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_jump() expects 1-3 arguments: filename, [height], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float height = 1.0f;
    if (argc >= 2) {
        if (!lua_isnumber(L, 2)) {
            return AudioLuaHelpers::luaError(L, "Expected number for height at argument 2");
        }
        height = (float)lua_tonumber(L, 2);
        if (height <= 0.0f || height > 5.0f) {
            return AudioLuaHelpers::luaError(L, "Height must be between 0.0 and 5.0");
        }
    }
    
    float duration = 0.3f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_jump(filename, height, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_powerup(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_powerup() expects 1-3 arguments: filename, [intensity], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float intensity = 1.0f;
    if (argc >= 2) {
        if (!lua_isnumber(L, 2)) {
            return AudioLuaHelpers::luaError(L, "Expected number for intensity at argument 2");
        }
        intensity = (float)lua_tonumber(L, 2);
        if (intensity <= 0.0f || intensity > 5.0f) {
            return AudioLuaHelpers::luaError(L, "Intensity must be between 0.0 and 5.0");
        }
    }
    
    float duration = 0.8f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_powerup(filename, intensity, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_hurt(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_hurt() expects 1-3 arguments: filename, [severity], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float severity = 1.0f;
    if (argc >= 2) {
        if (!lua_isnumber(L, 2)) {
            return AudioLuaHelpers::luaError(L, "Expected number for severity at argument 2");
        }
        severity = (float)lua_tonumber(L, 2);
        if (severity <= 0.0f || severity > 5.0f) {
            return AudioLuaHelpers::luaError(L, "Severity must be between 0.0 and 5.0");
        }
    }
    
    float duration = 0.4f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_hurt(filename, severity, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_shoot(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_shoot() expects 1-3 arguments: filename, [power], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float power = 1.0f;
    if (argc >= 2) {
        if (!lua_isnumber(L, 2)) {
            return AudioLuaHelpers::luaError(L, "Expected number for power at argument 2");
        }
        power = (float)lua_tonumber(L, 2);
        if (power <= 0.0f || power > 5.0f) {
            return AudioLuaHelpers::luaError(L, "Power must be between 0.0 and 5.0");
        }
    }
    
    float duration = 0.2f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_shoot(filename, power, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_click(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_click() expects 1-3 arguments: filename, [sharpness], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float sharpness = 1.0f;
    if (argc >= 2) {
        if (!lua_isnumber(L, 2)) {
            return AudioLuaHelpers::luaError(L, "Expected number for sharpness at argument 2");
        }
        sharpness = (float)lua_tonumber(L, 2);
        if (sharpness <= 0.0f || sharpness > 5.0f) {
            return AudioLuaHelpers::luaError(L, "Sharpness must be between 0.0 and 5.0");
        }
    }
    
    float duration = 0.05f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_click(filename, sharpness, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_pickup(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_pickup() expects 1-3 arguments: filename, [brightness], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float brightness = 1.0f;
    if (argc >= 2) {
        if (!lua_isnumber(L, 2)) {
            return AudioLuaHelpers::luaError(L, "Expected number for brightness at argument 2");
        }
        brightness = (float)lua_tonumber(L, 2);
        if (brightness <= 0.0f || brightness > 5.0f) {
            return AudioLuaHelpers::luaError(L, "Brightness must be between 0.0 and 5.0");
        }
    }
    
    float duration = 0.25f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_pickup(filename, brightness, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_blip(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_blip() expects 1-3 arguments: filename, [pitch], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float pitch = 1.0f;
    if (argc >= 2) {
        if (!AudioLuaHelpers::validatePitch(L, 2, pitch)) return 0;
    }
    
    float duration = 0.1f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_blip(filename, pitch, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_sweep_up(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 4) {
        return AudioLuaHelpers::luaError(L, "generate_sweep_up() expects 1-4 arguments: filename, [startFreq], [endFreq], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float startFreq = 200.0f;
    if (argc >= 2) {
        if (!AudioLuaHelpers::validateFrequency(L, 2, startFreq)) return 0;
    }
    
    float endFreq = 2000.0f;
    if (argc >= 3) {
        if (!AudioLuaHelpers::validateFrequency(L, 3, endFreq)) return 0;
    }
    
    float duration = 0.5f;
    if (argc >= 4) {
        if (!lua_isnumber(L, 4)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 4");
        }
        duration = (float)lua_tonumber(L, 4);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_sweep_up(filename, startFreq, endFreq, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_sweep_down(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 4) {
        return AudioLuaHelpers::luaError(L, "generate_sweep_down() expects 1-4 arguments: filename, [startFreq], [endFreq], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    float startFreq = 2000.0f;
    if (argc >= 2) {
        if (!AudioLuaHelpers::validateFrequency(L, 2, startFreq)) return 0;
    }
    
    float endFreq = 200.0f;
    if (argc >= 3) {
        if (!AudioLuaHelpers::validateFrequency(L, 3, endFreq)) return 0;
    }
    
    float duration = 0.5f;
    if (argc >= 4) {
        if (!lua_isnumber(L, 4)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 4");
        }
        duration = (float)lua_tonumber(L, 4);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_sweep_down(filename, startFreq, endFreq, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_random_beep(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 3) {
        return AudioLuaHelpers::luaError(L, "generate_random_beep() expects 1-3 arguments: filename, [seed], [duration]");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    uint32_t seed = 0;  // 0 means use current seed
    if (argc >= 2) {
        if (!lua_isnumber(L, 2)) {
            return AudioLuaHelpers::luaError(L, "Expected number for seed at argument 2");
        }
        seed = (uint32_t)lua_tonumber(L, 2);
    }
    
    float duration = 0.3f;
    if (argc >= 3) {
        if (!lua_isnumber(L, 3)) {
            return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 3");
        }
        duration = (float)lua_tonumber(L, 3);
        if (duration <= 0.0f || duration > 10.0f) {
            return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
        }
    }
    
    bool success = synth_generate_random_beep(filename, seed, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_oscillator(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc != 8) {
        return AudioLuaHelpers::luaError(L, "generate_oscillator() expects 8 arguments: filename, waveform, frequency, duration, attack, decay, sustain, release");
    }
    
    const char* filename = AudioLuaHelpers::getFilename(L, 1);
    if (!filename) return 0;
    
    int waveform;
    if (!AudioLuaHelpers::validateWaveform(L, 2, waveform)) return 0;
    
    float frequency;
    if (!AudioLuaHelpers::validateFrequency(L, 3, frequency)) return 0;
    
    if (!lua_isnumber(L, 4)) {
        return AudioLuaHelpers::luaError(L, "Expected number for duration at argument 4");
    }
    float duration = (float)lua_tonumber(L, 4);
    if (duration <= 0.0f || duration > 10.0f) {
        return AudioLuaHelpers::luaError(L, "Duration must be between 0.0 and 10.0 seconds");
    }
    
    if (!lua_isnumber(L, 5)) {
        return AudioLuaHelpers::luaError(L, "Expected number for attack at argument 5");
    }
    float attack = (float)lua_tonumber(L, 5);
    if (attack < 0.0f || attack > duration) {
        return AudioLuaHelpers::luaError(L, "Attack must be between 0.0 and duration");
    }
    
    if (!lua_isnumber(L, 6)) {
        return AudioLuaHelpers::luaError(L, "Expected number for decay at argument 6");
    }
    float decay = (float)lua_tonumber(L, 6);
    if (decay < 0.0f || decay > duration) {
        return AudioLuaHelpers::luaError(L, "Decay must be between 0.0 and duration");
    }
    
    if (!lua_isnumber(L, 7)) {
        return AudioLuaHelpers::luaError(L, "Expected number for sustain at argument 7");
    }
    float sustain = (float)lua_tonumber(L, 7);
    if (sustain < 0.0f || sustain > 1.0f) {
        return AudioLuaHelpers::luaError(L, "Sustain must be between 0.0 and 1.0");
    }
    
    if (!lua_isnumber(L, 8)) {
        return AudioLuaHelpers::luaError(L, "Expected number for release at argument 8");
    }
    float release = (float)lua_tonumber(L, 8);
    if (release < 0.0f || release > duration) {
        return AudioLuaHelpers::luaError(L, "Release must be between 0.0 and duration");
    }
    
    bool success = synth_generate_oscillator(filename, waveform, frequency, duration, attack, decay, sustain, release);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_sound_pack(lua_State* L) {
    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "generate_sound_pack() expects 1 argument: directory");
    }
    
    if (!lua_isstring(L, 1)) {
        return AudioLuaHelpers::luaError(L, "Expected string for directory at argument 1");
    }
    
    const char* directory = lua_tostring(L, 1);
    if (!directory || strlen(directory) == 0) {
        return AudioLuaHelpers::luaError(L, "Directory cannot be empty");
    }
    
    bool success = synth_generate_sound_pack(directory);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_note_to_frequency(lua_State* L) {
    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "note_to_frequency() expects 1 argument: midiNote");
    }
    
    if (!lua_isnumber(L, 1)) {
        return AudioLuaHelpers::luaError(L, "Expected number for MIDI note at argument 1");
    }
    
    int midiNote = (int)lua_tonumber(L, 1);
    if (midiNote < 0 || midiNote > 127) {
        return AudioLuaHelpers::luaError(L, "MIDI note must be between 0 and 127");
    }
    
    float frequency = synth_note_to_frequency(midiNote);
    lua_pushnumber(L, frequency);
    return 1;
}

int lua_synth_frequency_to_note(lua_State* L) {
    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "frequency_to_note() expects 1 argument: frequency");
    }
    
    float frequency;
    if (!AudioLuaHelpers::validateFrequency(L, 1, frequency)) return 0;
    
    int midiNote = synth_frequency_to_note(frequency);
    lua_pushnumber(L, midiNote);
    return 1;
}

int lua_synth_get_last_generation_time(lua_State* L) {
    float time = synth_get_last_generation_time();
    lua_pushnumber(L, time);
    return 1;
}

int lua_synth_get_generated_count(lua_State* L) {
    size_t count = synth_get_generated_count();
    lua_pushnumber(L, count);
    return 1;
}

// Memory-based sound creation functions (return sound ID for immediate use)

int lua_synth_create_beep(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_beep requires 2 arguments: frequency, duration");
    }

    float frequency = static_cast<float>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (frequency <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Frequency and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateBeepToMemory(frequency, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_explode(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_explode requires 2 arguments: size, duration");
    }

    float size = static_cast<float>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (size <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Size and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateExplodeToMemory(size, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_coin(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_coin requires 2 arguments: pitch, duration");
    }

    float pitch = static_cast<float>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (pitch <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Pitch and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateCoinToMemory(pitch, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_shoot(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_shoot requires 2 arguments: intensity, duration");
    }

    float intensity = static_cast<float>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (intensity <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Intensity and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateShootToMemory(intensity, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_click(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_click requires 2 arguments: intensity, duration");
    }

    float intensity = static_cast<float>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (intensity <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Intensity and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateClickToMemory(intensity, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_jump(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_jump requires 2 arguments: power, duration");
    }

    float power = static_cast<float>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (power <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Power and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateJumpToMemory(power, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_powerup(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_powerup requires 2 arguments: intensity, duration");
    }

    float intensity = static_cast<float>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (intensity <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Intensity and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generatePowerupToMemory(intensity, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_hurt(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_hurt requires 2 arguments: intensity, duration");
    }

    float intensity = static_cast<float>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (intensity <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Intensity and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateHurtToMemory(intensity, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_pickup(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_pickup requires 2 arguments: pitch, duration");
    }

    float pitch = static_cast<float>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (pitch <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Pitch and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generatePickupToMemory(pitch, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_blip(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_blip requires 2 arguments: pitch, duration");
    }

    float pitch = static_cast<float>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (pitch <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Pitch and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateBlipToMemory(pitch, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_zap(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_zap requires 2 arguments: frequency, duration");
    }

    float frequency = static_cast<float>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (frequency <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Frequency and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateZapToMemory(frequency, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_big_explosion(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_big_explosion requires 2 arguments: size, duration");
    }

    float size = static_cast<float>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (size <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Size and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateBigExplosionToMemory(size, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_small_explosion(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_small_explosion requires 2 arguments: intensity, duration");
    }

    float intensity = static_cast<float>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (intensity <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Intensity and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateSmallExplosionToMemory(intensity, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_distant_explosion(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_distant_explosion requires 2 arguments: distance, duration");
    }

    float distance = static_cast<float>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (distance <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Distance and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateDistantExplosionToMemory(distance, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_metal_explosion(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_metal_explosion requires 2 arguments: shrapnel, duration");
    }

    float shrapnel = static_cast<float>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (shrapnel <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Shrapnel and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateMetalExplosionToMemory(shrapnel, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_sweep_up(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 3) {
        return AudioLuaHelpers::luaError(L, "create_sweep_up requires 3 arguments: start_freq, end_freq, duration");
    }

    float startFreq = static_cast<float>(lua_tonumber(L, 1));
    float endFreq = static_cast<float>(lua_tonumber(L, 2));
    float duration = static_cast<float>(lua_tonumber(L, 3));

    if (startFreq <= 0 || endFreq <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Frequencies and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateSweepUpToMemory(startFreq, endFreq, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_sweep_down(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 3) {
        return AudioLuaHelpers::luaError(L, "create_sweep_down requires 3 arguments: start_freq, end_freq, duration");
    }

    float startFreq = static_cast<float>(lua_tonumber(L, 1));
    float endFreq = static_cast<float>(lua_tonumber(L, 2));
    float duration = static_cast<float>(lua_tonumber(L, 3));

    if (startFreq <= 0 || endFreq <= 0 || duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Frequencies and duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateSweepDownToMemory(startFreq, endFreq, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_oscillator(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 7) {
        return AudioLuaHelpers::luaError(L, "create_oscillator requires 7 arguments: waveform, frequency, duration, attack, decay, sustain, release");
    }

    int waveform = static_cast<int>(lua_tonumber(L, 1));
    float frequency = static_cast<float>(lua_tonumber(L, 2));
    float duration = static_cast<float>(lua_tonumber(L, 3));
    float attack = static_cast<float>(lua_tonumber(L, 4));
    float decay = static_cast<float>(lua_tonumber(L, 5));
    float sustain = static_cast<float>(lua_tonumber(L, 6));
    float release = static_cast<float>(lua_tonumber(L, 7));

    if (waveform < 0 || waveform > 4 || frequency <= 0 ||
        duration <= 0 || attack < 0 || decay < 0 || sustain < 0 || release < 0) {
        return AudioLuaHelpers::luaError(L, "Invalid waveform, frequency, or ADSR parameters");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateOscillatorToMemory(
        static_cast<WaveformType>(waveform), frequency, duration, attack, decay, sustain, release) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

int lua_synth_create_random_beep(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) < 2) {
        return AudioLuaHelpers::luaError(L, "create_random_beep requires 2 arguments: seed, duration");
    }

    uint32_t seed = static_cast<uint32_t>(lua_tonumber(L, 1));
    float duration = static_cast<float>(lua_tonumber(L, 2));

    if (duration <= 0) {
        return AudioLuaHelpers::luaError(L, "Duration must be positive");
    }

    uint32_t sound_id = g_synthEngine ? g_synthEngine->generateRandomBeepToMemory(seed, duration) : 0;
    lua_pushnumber(L, sound_id);
    return 1;
}

// ABC Multi-Voice Music Functions

int lua_music_queue_abc_slot(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    int argc = lua_gettop(L);
    if (argc < 1 || argc > 5) {
        return AudioLuaHelpers::luaError(L, "queue_abc_slot() expects 1-5 arguments: abc_notation, [name], [tempo], [instrument], [loop]");
    }

    // Get ABC notation (required)
    if (!lua_isstring(L, 1)) {
        return AudioLuaHelpers::luaError(L, "ABC notation must be a string");
    }
    const char* abc_notation = lua_tostring(L, 1);

    // Get optional parameters
    const char* name = argc > 1 && lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
    int tempo = argc > 2 && lua_isnumber(L, 3) ? static_cast<int>(lua_tonumber(L, 3)) : 120;
    int instrument = argc > 3 && lua_isnumber(L, 4) ? static_cast<int>(lua_tonumber(L, 4)) : 0;
    bool loop = argc > 4 && lua_isboolean(L, 5) ? lua_toboolean(L, 5) : false;

    // Validate parameters
    if (tempo <= 0 || tempo > 300) {
        return AudioLuaHelpers::luaError(L, "Tempo must be between 1 and 300 BPM");
    }
    if (instrument < 0 || instrument > 127) {
        return AudioLuaHelpers::luaError(L, "Instrument must be between 0 and 127 (General MIDI)");
    }

    uint32_t slot_id = queue_music_slot(abc_notation, name, tempo, instrument, loop);
    lua_pushnumber(L, slot_id);
    return 1;
}

int lua_music_queue_abc_simple(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "queue_abc_simple() expects 1 argument: abc_notation");
    }

    if (!lua_isstring(L, 1)) {
        return AudioLuaHelpers::luaError(L, "ABC notation must be a string");
    }

    const char* abc_notation = lua_tostring(L, 1);
    uint32_t slot_id = queue_music_slot_simple(abc_notation);
    lua_pushnumber(L, slot_id);
    return 1;
}

int lua_music_remove_slot(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "remove_music_slot() expects 1 argument: slot_id");
    }

    if (!lua_isnumber(L, 1)) {
        return AudioLuaHelpers::luaError(L, "Slot ID must be a number");
    }

    uint32_t slot_id = static_cast<uint32_t>(lua_tonumber(L, 1));
    bool removed = remove_music_slot(slot_id);
    lua_pushboolean(L, removed);
    return 1;
}

int lua_music_has_slot(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "has_music_slot() expects 1 argument: slot_id");
    }

    if (!lua_isnumber(L, 1)) {
        return AudioLuaHelpers::luaError(L, "Slot ID must be a number");
    }

    uint32_t slot_id = static_cast<uint32_t>(lua_tonumber(L, 1));
    bool exists = has_music_slot(slot_id);
    lua_pushboolean(L, exists);
    return 1;
}

int lua_music_get_slot_count(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    int count = get_music_slot_count();
    lua_pushnumber(L, count);
    return 1;
}

int lua_music_clear_queue(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    music_clear_queue();
    return 0;
}

int lua_music_is_playing(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    bool playing = music_is_playing();
    lua_pushboolean(L, playing);
    return 1;
}

int lua_music_is_paused(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    bool paused = music_is_paused();
    lua_pushboolean(L, paused);
    return 1;
}

int lua_music_get_queue_size(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    int size = music_get_queue_size();
    lua_pushnumber(L, size);
    return 1;
}

int lua_music_pause(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    music_pause();
    return 0;
}

int lua_music_resume(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    music_resume();
    return 0;
}

int lua_music_stop(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    music_stop();
    return 0;
}

// Wait-related Lua functions removed - using ABC rests instead for cleaner musical pauses

int lua_music_queue_abc_slot_with_wait(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    int argc = lua_gettop(L);
    if (argc < 2 || argc > 6) {
        return AudioLuaHelpers::luaError(L, "queue_abc_slot_with_wait() expects 2-6 arguments: abc_notation, wait_ms, [name], [tempo], [instrument], [loop]");
    }

    // Get ABC notation (required)
    if (!lua_isstring(L, 1)) {
        return AudioLuaHelpers::luaError(L, "ABC notation must be a string");
    }
    const char* abc_notation = lua_tostring(L, 1);

    // Get wait time (required)
    if (!lua_isnumber(L, 2)) {
        return AudioLuaHelpers::luaError(L, "Wait time must be a number (milliseconds)");
    }
    int wait_ms = (int)lua_tonumber(L, 2);

    // Optional arguments
    const char* name = (argc > 2 && lua_isstring(L, 3)) ? lua_tostring(L, 3) : "";
    int tempo = (argc > 3 && lua_isnumber(L, 4)) ? (int)lua_tonumber(L, 4) : 120;
    int instrument = (argc > 4 && lua_isnumber(L, 5)) ? (int)lua_tonumber(L, 5) : 0;
    bool loop = (argc > 5 && lua_isboolean(L, 6)) ? lua_toboolean(L, 6) : false;

    uint32_t slot_id = queue_music_slot_with_wait(abc_notation, name, tempo, instrument, loop, wait_ms);
    lua_pushnumber(L, slot_id);
    return 1;
}

int lua_set_music_debug_output(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    bool enabled = lua_toboolean(L, 1);
    set_music_debug_output(enabled);
    return 0;
}

int lua_get_music_debug_output(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    bool enabled = get_music_debug_output();
    lua_pushboolean(L, enabled);
    return 1;
}

int lua_set_music_parser_debug_output(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    bool enabled = lua_toboolean(L, 1);
    set_music_parser_debug_output(enabled);
    return 0;
}

int lua_get_music_parser_debug_output(lua_State* L) {
    if (!AudioLuaHelpers::checkAudioInitialized(L)) {
        return 0;
    }

    bool enabled = get_music_parser_debug_output();
    lua_pushboolean(L, enabled);
    return 1;
}

// ABC Player Client Lua Functions

int lua_abc_music_initialize(lua_State* L) {
    bool success = abc_client_initialize();
    lua_pushboolean(L, success);
    return 1;
}

int lua_abc_music_shutdown(lua_State* L) {
    abc_client_shutdown();
    return 0;
}

int lua_abc_music_is_initialized(lua_State* L) {
    bool initialized = abc_client_is_initialized();
    lua_pushboolean(L, initialized);
    return 1;
}

int lua_abc_play_music(lua_State* L) {
    int argc = lua_gettop(L);
    if (argc < 1 || argc > 2) {
        return AudioLuaHelpers::luaError(L, "abc_play_music() expects 1-2 arguments: abc_notation, [name]");
    }
    
    const char* abc_notation = lua_tostring(L, 1);
    const char* name = (argc > 1) ? lua_tostring(L, 2) : nullptr;
    
    if (!abc_notation) {
        return AudioLuaHelpers::luaError(L, "abc_play_music(): abc_notation must be a string");
    }
    
    bool success = abc_client_play_abc(abc_notation, name);
    lua_pushboolean(L, success);
    return 1;
}

int lua_abc_play_music_file(lua_State* L) {
    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "abc_play_music_file() expects 1 argument: filename");
    }
    
    const char* filename = lua_tostring(L, 1);
    if (!filename) {
        return AudioLuaHelpers::luaError(L, "abc_play_music_file(): filename must be a string");
    }
    
    bool success = abc_client_play_abc_file(filename);
    lua_pushboolean(L, success);
    return 1;
}

int lua_abc_stop_music(lua_State* L) {
    bool success = abc_client_stop();
    lua_pushboolean(L, success);
    return 1;
}

int lua_abc_pause_music(lua_State* L) {
    bool success = abc_client_pause();
    lua_pushboolean(L, success);
    return 1;
}

int lua_abc_resume_music(lua_State* L) {
    bool success = abc_client_resume();
    lua_pushboolean(L, success);
    return 1;
}

int lua_abc_clear_music_queue(lua_State* L) {
    bool success = abc_client_clear_queue();
    lua_pushboolean(L, success);
    return 1;
}

int lua_abc_set_music_volume(lua_State* L) {
    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "abc_set_music_volume() expects 1 argument: volume (0.0-1.0)");
    }
    
    float volume = lua_tonumber(L, 1);
    if (volume < 0.0f || volume > 1.0f) {
        return AudioLuaHelpers::luaError(L, "abc_set_music_volume(): volume must be between 0.0 and 1.0");
    }
    
    bool success = abc_client_set_volume(volume);
    lua_pushboolean(L, success);
    return 1;
}

int lua_abc_is_music_playing(lua_State* L) {
    bool playing = abc_client_is_playing();
    lua_pushboolean(L, playing);
    return 1;
}

int lua_abc_is_music_paused(lua_State* L) {
    bool paused = abc_client_is_paused();
    lua_pushboolean(L, paused);
    return 1;
}

int lua_abc_get_music_queue_size(lua_State* L) {
    int size = abc_client_get_queue_size();
    lua_pushnumber(L, size);
    return 1;
}

int lua_abc_get_music_volume(lua_State* L) {
    float volume = abc_client_get_volume();
    lua_pushnumber(L, volume);
    return 1;
}

int lua_abc_get_current_song(lua_State* L) {
    const char* song = abc_client_get_current_song();
    lua_pushstring(L, song);
    return 1;
}

int lua_abc_get_version(lua_State* L) {
    const char* version = abc_client_get_version();
    lua_pushstring(L, version);
    return 1;
}

int lua_abc_set_auto_start_server(lua_State* L) {
    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "abc_set_auto_start_server() expects 1 argument: auto_start (boolean)");
    }
    
    bool auto_start = lua_toboolean(L, 1);
    abc_client_set_auto_start_server(auto_start);
    return 0;
}

int lua_abc_set_debug_output(lua_State* L) {
    if (lua_gettop(L) != 1) {
        return AudioLuaHelpers::luaError(L, "abc_set_debug_output() expects 1 argument: debug (boolean)");
    }
    
    bool debug = lua_toboolean(L, 1);
    abc_client_set_debug_output(debug);
    return 0;
}

int lua_abc_export_midi(lua_State* L) {
    if (lua_gettop(L) != 2) {
        return AudioLuaHelpers::luaError(L, "abc_export_midi() expects 2 arguments: abc_notation, midi_filename");
    }
    
    const char* abc_notation = lua_tostring(L, 1);
    const char* midi_filename = lua_tostring(L, 2);
    
    if (!abc_notation) {
        return AudioLuaHelpers::luaError(L, "abc_export_midi(): abc_notation must be a string");
    }
    
    if (!midi_filename) {
        return AudioLuaHelpers::luaError(L, "abc_export_midi(): midi_filename must be a string");
    }
    
    bool success = abc_client_export_midi(abc_notation, midi_filename);
    lua_pushboolean(L, success);
    return 1;
}

int lua_abc_export_midi_from_file(lua_State* L) {
    if (lua_gettop(L) != 2) {
        return AudioLuaHelpers::luaError(L, "abc_export_midi_from_file() expects 2 arguments: abc_filename, midi_filename");
    }
    
    const char* abc_filename = lua_tostring(L, 1);
    const char* midi_filename = lua_tostring(L, 2);
    
    if (!abc_filename) {
        return AudioLuaHelpers::luaError(L, "abc_export_midi_from_file(): abc_filename must be a string");
    }
    
    if (!midi_filename) {
        return AudioLuaHelpers::luaError(L, "abc_export_midi_from_file(): midi_filename must be a string");
    }
    
    bool success = abc_client_export_midi_from_file(abc_filename, midi_filename);
    lua_pushboolean(L, success);
    return 1;
}

// Registration function

extern "C" void register_audio_lua_bindings(lua_State* L) {
    // System functions
    lua_register(L, "audio_initialize", lua_audio_initialize);
    lua_register(L, "audio_shutdown", lua_audio_shutdown);
    lua_register(L, "audio_is_initialized", lua_audio_is_initialized);
    lua_register(L, "audio_wait_queue_empty", lua_audio_wait_queue_empty);
    
    // MIDI Event Capture functions for testing
    register_midi_capture_lua_bindings(L);
    
    // Music functions
    lua_register(L, "load_music", lua_audio_load_music);
    lua_register(L, "play_music", lua_audio_play_music);
    lua_register(L, "stop_music", lua_audio_stop_music);
    lua_register(L, "set_music_volume", lua_audio_set_music_volume);
    
    // Sound effect functions
    lua_register(L, "load_sound", lua_audio_load_sound);
    lua_register(L, "load_sound_with_id", lua_audio_load_sound_with_id);
    lua_register(L, "play_sound", lua_audio_play_sound);
    lua_register(L, "stop_sound", lua_audio_stop_sound);
    
    // Synthesis functions (Phase 2)
    lua_register(L, "create_oscillator", lua_audio_create_oscillator);
    lua_register(L, "set_oscillator_frequency", lua_audio_set_oscillator_frequency);
    lua_register(L, "set_oscillator_waveform", lua_audio_set_oscillator_waveform);
    lua_register(L, "set_oscillator_volume", lua_audio_set_oscillator_volume);
    lua_register(L, "delete_oscillator", lua_audio_delete_oscillator);
    
    // MIDI functions (Phase 2)
    lua_register(L, "load_midi", lua_audio_load_midi);
    lua_register(L, "play_midi", lua_audio_play_midi);
    lua_register(L, "stop_midi", lua_audio_stop_midi);
    lua_register(L, "midi_note_on", lua_audio_midi_note_on);
    lua_register(L, "midi_note_off", lua_audio_midi_note_off);
    lua_register(L, "midi_control_change", lua_audio_midi_control_change);
    lua_register(L, "midi_program_change", lua_audio_midi_program_change);
    
    // Register MIDI tracker functions (they will check for initialization internally)
    registerMidiLuaBindings(L, nullptr);
    
    // Information functions
    lua_register(L, "audio_get_loaded_sound_count", lua_audio_get_loaded_sound_count);
    lua_register(L, "audio_get_active_voice_count", lua_audio_get_active_voice_count);
    lua_register(L, "audio_get_cpu_usage", lua_audio_get_cpu_usage);
    
    // Utility functions
    lua_register(L, "set_master_volume", lua_audio_set_master_volume);
    lua_register(L, "get_master_volume", lua_audio_get_master_volume);
    lua_register(L, "set_audio_muted", lua_audio_set_muted);
    lua_register(L, "is_audio_muted", lua_audio_is_muted);
    
    // ABC Multi-Voice Music functions
    lua_register(L, "queue_abc_slot", lua_music_queue_abc_slot);
    lua_register(L, "queue_abc_simple", lua_music_queue_abc_simple);
    lua_register(L, "remove_music_slot", lua_music_remove_slot);
    lua_register(L, "has_music_slot", lua_music_has_slot);
    lua_register(L, "get_music_slot_count", lua_music_get_slot_count);
    lua_register(L, "clear_music_queue", lua_music_clear_queue);
    lua_register(L, "music_is_playing", lua_music_is_playing);
    lua_register(L, "music_is_paused", lua_music_is_paused);
    lua_register(L, "get_music_queue_size", lua_music_get_queue_size);
    lua_register(L, "pause_music", lua_music_pause);
    lua_register(L, "resume_music", lua_music_resume);
    lua_register(L, "stop_music", lua_music_stop);
    
    // Wait-related Lua bindings removed - using ABC rests instead
    lua_register(L, "queue_abc_slot_with_wait", lua_music_queue_abc_slot_with_wait);
    
    // Debug control functions
    lua_register(L, "set_music_debug_output", lua_set_music_debug_output);
    lua_register(L, "get_music_debug_output", lua_get_music_debug_output);
    lua_register(L, "set_music_parser_debug_output", lua_set_music_parser_debug_output);
    lua_register(L, "get_music_parser_debug_output", lua_get_music_parser_debug_output);
    
    // ABC Player Client functions (New Multi-Voice Player)
    lua_register(L, "abc_music_initialize", lua_abc_music_initialize);
    lua_register(L, "abc_music_shutdown", lua_abc_music_shutdown);
    lua_register(L, "abc_music_is_initialized", lua_abc_music_is_initialized);
    lua_register(L, "abc_play_music", lua_abc_play_music);
    lua_register(L, "abc_play_music_file", lua_abc_play_music_file);
    lua_register(L, "abc_stop_music", lua_abc_stop_music);
    lua_register(L, "abc_pause_music", lua_abc_pause_music);
    lua_register(L, "abc_resume_music", lua_abc_resume_music);
    lua_register(L, "abc_clear_music_queue", lua_abc_clear_music_queue);
    lua_register(L, "abc_set_music_volume", lua_abc_set_music_volume);
    lua_register(L, "abc_is_music_playing", lua_abc_is_music_playing);
    lua_register(L, "abc_is_music_paused", lua_abc_is_music_paused);
    lua_register(L, "abc_get_music_queue_size", lua_abc_get_music_queue_size);
    lua_register(L, "abc_get_music_volume", lua_abc_get_music_volume);
    lua_register(L, "abc_get_current_song", lua_abc_get_current_song);
    lua_register(L, "abc_get_version", lua_abc_get_version);
    lua_register(L, "abc_set_auto_start_server", lua_abc_set_auto_start_server);
    lua_register(L, "abc_set_debug_output", lua_abc_set_debug_output);
    lua_register(L, "abc_export_midi", lua_abc_export_midi);
    lua_register(L, "abc_export_midi_from_file", lua_abc_export_midi_from_file);
    
    // Register wave constants as global variables
    lua_pushnumber(L, AudioLuaConstants::WAVE_SINE);
    lua_setglobal(L, "WAVE_SINE");
    
    lua_pushnumber(L, AudioLuaConstants::WAVE_SQUARE);
    lua_setglobal(L, "WAVE_SQUARE");
    
    lua_pushnumber(L, AudioLuaConstants::WAVE_SAWTOOTH);
    lua_setglobal(L, "WAVE_SAWTOOTH");
    
    lua_pushnumber(L, AudioLuaConstants::WAVE_TRIANGLE);
    lua_setglobal(L, "WAVE_TRIANGLE");
    
    lua_pushnumber(L, AudioLuaConstants::WAVE_NOISE);
    lua_setglobal(L, "WAVE_NOISE");
    
    // Synthesis functions
    lua_register(L, "synth_initialize", lua_synth_initialize);
    lua_register(L, "synth_shutdown", lua_synth_shutdown);
    lua_register(L, "synth_is_initialized", lua_synth_is_initialized);
    
    // Sound generation functions
    lua_register(L, "generate_beep", lua_synth_generate_beep);
    lua_register(L, "generate_bang", lua_synth_generate_bang);
    lua_register(L, "generate_explode", lua_synth_generate_explode);
    lua_register(L, "generate_big_explosion", lua_synth_generate_big_explosion);
    lua_register(L, "generate_small_explosion", lua_synth_generate_small_explosion);
    lua_register(L, "generate_distant_explosion", lua_synth_generate_distant_explosion);
    lua_register(L, "generate_metal_explosion", lua_synth_generate_metal_explosion);
    lua_register(L, "generate_zap", lua_synth_generate_zap);
    lua_register(L, "generate_coin", lua_synth_generate_coin);
    lua_register(L, "generate_jump", lua_synth_generate_jump);
    lua_register(L, "generate_powerup", lua_synth_generate_powerup);
    lua_register(L, "generate_hurt", lua_synth_generate_hurt);
    lua_register(L, "generate_shoot", lua_synth_generate_shoot);
    lua_register(L, "generate_click", lua_synth_generate_click);
    lua_register(L, "generate_pickup", lua_synth_generate_pickup);
    lua_register(L, "generate_blip", lua_synth_generate_blip);
    
    // Sweep sounds
    lua_register(L, "generate_sweep_up", lua_synth_generate_sweep_up);
    lua_register(L, "generate_sweep_down", lua_synth_generate_sweep_down);
    
    // Random/procedural sounds
    lua_register(L, "generate_random_beep", lua_synth_generate_random_beep);
    
    // Custom synthesis
    lua_register(L, "generate_oscillator", lua_synth_generate_oscillator);
    
    // Batch generation
    lua_register(L, "generate_sound_pack", lua_synth_generate_sound_pack);
    
    // Memory-based generation (returns sound ID for immediate use)
    lua_register(L, "create_beep", lua_synth_create_beep);
    lua_register(L, "create_explode", lua_synth_create_explode);
    lua_register(L, "create_coin", lua_synth_create_coin);
    lua_register(L, "create_shoot", lua_synth_create_shoot);
    lua_register(L, "create_click", lua_synth_create_click);
    lua_register(L, "create_jump", lua_synth_create_jump);
    lua_register(L, "create_powerup", lua_synth_create_powerup);
    lua_register(L, "create_hurt", lua_synth_create_hurt);
    lua_register(L, "create_pickup", lua_synth_create_pickup);
    lua_register(L, "create_blip", lua_synth_create_blip);
    lua_register(L, "create_zap", lua_synth_create_zap);
    lua_register(L, "create_big_explosion", lua_synth_create_big_explosion);
    lua_register(L, "create_small_explosion", lua_synth_create_small_explosion);
    lua_register(L, "create_distant_explosion", lua_synth_create_distant_explosion);
    lua_register(L, "create_metal_explosion", lua_synth_create_metal_explosion);
    lua_register(L, "create_sweep_up", lua_synth_create_sweep_up);
    lua_register(L, "create_sweep_down", lua_synth_create_sweep_down);
    lua_register(L, "create_oscillator", lua_synth_create_oscillator);
    lua_register(L, "create_random_beep", lua_synth_create_random_beep);
    
    // Advanced synthesis - file generation
    lua_register(L, "generate_additive", lua_synth_generate_additive);
    lua_register(L, "generate_fm", lua_synth_generate_fm);
    lua_register(L, "generate_granular", lua_synth_generate_granular);
    lua_register(L, "generate_physical_string", lua_synth_generate_physical_string);
    lua_register(L, "generate_physical_bar", lua_synth_generate_physical_bar);
    lua_register(L, "generate_physical_tube", lua_synth_generate_physical_tube);
    lua_register(L, "generate_physical_drum", lua_synth_generate_physical_drum);
    
    // Advanced synthesis - memory generation
    lua_register(L, "create_additive", lua_synth_create_additive);
    lua_register(L, "create_fm", lua_synth_create_fm);
    lua_register(L, "create_granular", lua_synth_create_granular);
    lua_register(L, "create_physical_string", lua_synth_create_physical_string);
    lua_register(L, "create_physical_bar", lua_synth_create_physical_bar);
    lua_register(L, "create_physical_tube", lua_synth_create_physical_tube);
    lua_register(L, "create_physical_drum", lua_synth_create_physical_drum);
    
    // Real-time effects control
    lua_register(L, "sound_add_effect", lua_synth_add_effect);
    lua_register(L, "sound_remove_effect", lua_synth_remove_effect);
    lua_register(L, "sound_set_effect_param", lua_synth_set_effect_param);
    
    // Preset management
    lua_register(L, "synth_save_preset", lua_synth_save_preset);
    lua_register(L, "synth_load_preset", lua_synth_load_preset);
    lua_register(L, "synth_apply_preset", lua_synth_apply_preset);
    
    // Utility functions
    lua_register(L, "note_to_frequency", lua_synth_note_to_frequency);
    lua_register(L, "frequency_to_note", lua_synth_frequency_to_note);
    lua_register(L, "synth_get_last_generation_time", lua_synth_get_last_generation_time);
    lua_register(L, "synth_get_generated_count", lua_synth_get_generated_count);
    
    // Register wave constants as global variables
    lua_pushnumber(L, AudioLuaConstants::WAVE_SINE);
    lua_setglobal(L, "WAVE_SINE");
    
    lua_pushnumber(L, AudioLuaConstants::WAVE_SQUARE);
    lua_setglobal(L, "WAVE_SQUARE");
    
    lua_pushnumber(L, AudioLuaConstants::WAVE_SAWTOOTH);
    lua_setglobal(L, "WAVE_SAWTOOTH");
    
    lua_pushnumber(L, AudioLuaConstants::WAVE_TRIANGLE);
    lua_setglobal(L, "WAVE_TRIANGLE");
    
    lua_pushnumber(L, AudioLuaConstants::WAVE_NOISE);
    lua_setglobal(L, "WAVE_NOISE");
    
    // Synthesis type constants
    lua_pushinteger(L, 0);  // SUBTRACTIVE
    lua_setglobal(L, "SYNTH_SUBTRACTIVE");
    
    lua_pushinteger(L, 1);  // ADDITIVE
    lua_setglobal(L, "SYNTH_ADDITIVE");
    
    lua_pushinteger(L, 2);  // FM
    lua_setglobal(L, "SYNTH_FM");
    
    lua_pushinteger(L, 3);  // GRANULAR
    lua_setglobal(L, "SYNTH_GRANULAR");
    
    lua_pushinteger(L, 4);  // PHYSICAL
    lua_setglobal(L, "SYNTH_PHYSICAL");
    
    // Physical model constants
    lua_pushinteger(L, 0);  // PLUCKED_STRING
    lua_setglobal(L, "MODEL_PLUCKED_STRING");
    
    lua_pushinteger(L, 1);  // STRUCK_BAR
    lua_setglobal(L, "MODEL_STRUCK_BAR");
    
    lua_pushinteger(L, 2);  // BLOWN_TUBE
    lua_setglobal(L, "MODEL_BLOWN_TUBE");
    
    lua_pushinteger(L, 3);  // DRUMHEAD
    lua_setglobal(L, "MODEL_DRUMHEAD");
    
    // Effect type constants
    lua_pushstring(L, "reverb");
    lua_setglobal(L, "EFFECT_REVERB");
    
    lua_pushstring(L, "distortion");
    lua_setglobal(L, "EFFECT_DISTORTION");
    
    lua_pushstring(L, "chorus");
    lua_setglobal(L, "EFFECT_CHORUS");
    
    lua_pushstring(L, "delay");
    lua_setglobal(L, "EFFECT_DELAY");
    
    lua_pushstring(L, "filter");
    lua_setglobal(L, "EFFECT_FILTER");
    
    std::cout << "AudioLuaBindings: Registered all audio functions and constants" << std::endl;
}

// Advanced synthesis Lua binding implementations
int lua_synth_generate_additive(lua_State* L) {
    if (lua_gettop(L) < 4) return luaL_error(L, "generate_additive() requires at least 4 arguments: filename, fundamental, harmonics_table, duration");
    
    const char* filename = luaL_checkstring(L, 1);
    float fundamental = (float)luaL_checknumber(L, 2);
    float duration = (float)luaL_checknumber(L, 4);
    
    if (!lua_istable(L, 3)) return luaL_error(L, "harmonics must be a table");
    
    std::vector<float> harmonics;
    lua_pushnil(L);
    while (lua_next(L, 3) != 0) {
        if (lua_isnumber(L, -1)) {
            harmonics.push_back((float)lua_tonumber(L, -1));
        }
        lua_pop(L, 1);
    }
    
    if (harmonics.empty()) return luaL_error(L, "harmonics table cannot be empty");
    
    bool success = synth_generate_additive(filename, fundamental, harmonics.data(), (int)harmonics.size(), duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_fm(lua_State* L) {
    if (lua_gettop(L) != 5) return luaL_error(L, "generate_fm() requires 5 arguments: filename, carrier_freq, modulator_freq, mod_index, duration");
    
    const char* filename = luaL_checkstring(L, 1);
    float carrierFreq = (float)luaL_checknumber(L, 2);
    float modulatorFreq = (float)luaL_checknumber(L, 3);
    float modIndex = (float)luaL_checknumber(L, 4);
    float duration = (float)luaL_checknumber(L, 5);
    
    bool success = synth_generate_fm(filename, carrierFreq, modulatorFreq, modIndex, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_granular(lua_State* L) {
    if (lua_gettop(L) != 5) return luaL_error(L, "generate_granular() requires 5 arguments: filename, base_freq, grain_size, overlap, duration");
    
    const char* filename = luaL_checkstring(L, 1);
    float baseFreq = (float)luaL_checknumber(L, 2);
    float grainSize = (float)luaL_checknumber(L, 3);
    float overlap = (float)luaL_checknumber(L, 4);
    float duration = (float)luaL_checknumber(L, 5);
    
    bool success = synth_generate_granular(filename, baseFreq, grainSize, overlap, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_physical_string(lua_State* L) {
    if (lua_gettop(L) != 5) return luaL_error(L, "generate_physical_string() requires 5 arguments: filename, frequency, damping, brightness, duration");
    
    const char* filename = luaL_checkstring(L, 1);
    float frequency = (float)luaL_checknumber(L, 2);
    float damping = (float)luaL_checknumber(L, 3);
    float brightness = (float)luaL_checknumber(L, 4);
    float duration = (float)luaL_checknumber(L, 5);
    
    bool success = synth_generate_physical_string(filename, frequency, damping, brightness, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_physical_bar(lua_State* L) {
    if (lua_gettop(L) != 5) return luaL_error(L, "generate_physical_bar() requires 5 arguments: filename, frequency, damping, brightness, duration");
    
    const char* filename = luaL_checkstring(L, 1);
    float frequency = (float)luaL_checknumber(L, 2);
    float damping = (float)luaL_checknumber(L, 3);
    float brightness = (float)luaL_checknumber(L, 4);
    float duration = (float)luaL_checknumber(L, 5);
    
    bool success = synth_generate_physical_bar(filename, frequency, damping, brightness, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_physical_tube(lua_State* L) {
    if (lua_gettop(L) != 5) return luaL_error(L, "generate_physical_tube() requires 5 arguments: filename, frequency, air_pressure, brightness, duration");
    
    const char* filename = luaL_checkstring(L, 1);
    float frequency = (float)luaL_checknumber(L, 2);
    float airPressure = (float)luaL_checknumber(L, 3);
    float brightness = (float)luaL_checknumber(L, 4);
    float duration = (float)luaL_checknumber(L, 5);
    
    bool success = synth_generate_physical_tube(filename, frequency, airPressure, brightness, duration);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_generate_physical_drum(lua_State* L) {
    if (lua_gettop(L) != 5) return luaL_error(L, "generate_physical_drum() requires 5 arguments: filename, frequency, damping, excitation, duration");
    
    const char* filename = luaL_checkstring(L, 1);
    float frequency = (float)luaL_checknumber(L, 2);
    float damping = (float)luaL_checknumber(L, 3);
    float excitation = (float)luaL_checknumber(L, 4);
    float duration = (float)luaL_checknumber(L, 5);
    
    bool success = synth_generate_physical_drum(filename, frequency, damping, excitation, duration);
    lua_pushboolean(L, success);
    return 1;
}

// Memory-based advanced synthesis
int lua_synth_create_additive(lua_State* L) {
    if (lua_gettop(L) < 3) return luaL_error(L, "create_additive() requires at least 3 arguments: fundamental, harmonics_table, duration");
    
    float fundamental = (float)luaL_checknumber(L, 1);
    float duration = (float)luaL_checknumber(L, 3);
    
    if (!lua_istable(L, 2)) return luaL_error(L, "harmonics must be a table");
    
    std::vector<float> harmonics;
    lua_pushnil(L);
    while (lua_next(L, 2) != 0) {
        if (lua_isnumber(L, -1)) {
            harmonics.push_back((float)lua_tonumber(L, -1));
        }
        lua_pop(L, 1);
    }
    
    if (harmonics.empty()) return luaL_error(L, "harmonics table cannot be empty");
    
    uint32_t soundId = synth_create_additive(fundamental, harmonics.data(), (int)harmonics.size(), duration);
    lua_pushinteger(L, soundId);
    return 1;
}

int lua_synth_create_fm(lua_State* L) {
    if (lua_gettop(L) != 4) return luaL_error(L, "create_fm() requires 4 arguments: carrier_freq, modulator_freq, mod_index, duration");
    
    float carrierFreq = (float)luaL_checknumber(L, 1);
    float modulatorFreq = (float)luaL_checknumber(L, 2);
    float modIndex = (float)luaL_checknumber(L, 3);
    float duration = (float)luaL_checknumber(L, 4);
    
    uint32_t soundId = synth_create_fm(carrierFreq, modulatorFreq, modIndex, duration);
    lua_pushinteger(L, soundId);
    return 1;
}

int lua_synth_create_granular(lua_State* L) {
    if (lua_gettop(L) != 4) return luaL_error(L, "create_granular() requires 4 arguments: base_freq, grain_size, overlap, duration");
    
    float baseFreq = (float)luaL_checknumber(L, 1);
    float grainSize = (float)luaL_checknumber(L, 2);
    float overlap = (float)luaL_checknumber(L, 3);
    float duration = (float)luaL_checknumber(L, 4);
    
    uint32_t soundId = synth_create_granular(baseFreq, grainSize, overlap, duration);
    lua_pushinteger(L, soundId);
    return 1;
}

int lua_synth_create_physical_string(lua_State* L) {
    if (lua_gettop(L) != 4) return luaL_error(L, "create_physical_string() requires 4 arguments: frequency, damping, brightness, duration");
    
    float frequency = (float)luaL_checknumber(L, 1);
    float damping = (float)luaL_checknumber(L, 2);
    float brightness = (float)luaL_checknumber(L, 3);
    float duration = (float)luaL_checknumber(L, 4);
    
    uint32_t soundId = synth_create_physical_string(frequency, damping, brightness, duration);
    lua_pushinteger(L, soundId);
    return 1;
}

int lua_synth_create_physical_bar(lua_State* L) {
    if (lua_gettop(L) != 4) return luaL_error(L, "create_physical_bar() requires 4 arguments: frequency, damping, brightness, duration");
    
    float frequency = (float)luaL_checknumber(L, 1);
    float damping = (float)luaL_checknumber(L, 2);
    float brightness = (float)luaL_checknumber(L, 3);
    float duration = (float)luaL_checknumber(L, 4);
    
    uint32_t soundId = synth_create_physical_bar(frequency, damping, brightness, duration);
    lua_pushinteger(L, soundId);
    return 1;
}

int lua_synth_create_physical_tube(lua_State* L) {
    if (lua_gettop(L) != 4) return luaL_error(L, "create_physical_tube() requires 4 arguments: frequency, air_pressure, brightness, duration");
    
    float frequency = (float)luaL_checknumber(L, 1);
    float airPressure = (float)luaL_checknumber(L, 2);
    float brightness = (float)luaL_checknumber(L, 3);
    float duration = (float)luaL_checknumber(L, 4);
    
    uint32_t soundId = synth_create_physical_tube(frequency, airPressure, brightness, duration);
    lua_pushinteger(L, soundId);
    return 1;
}

int lua_synth_create_physical_drum(lua_State* L) {
    if (lua_gettop(L) != 4) return luaL_error(L, "create_physical_drum() requires 4 arguments: frequency, damping, excitation, duration");
    
    float frequency = (float)luaL_checknumber(L, 1);
    float damping = (float)luaL_checknumber(L, 2);
    float excitation = (float)luaL_checknumber(L, 3);
    float duration = (float)luaL_checknumber(L, 4);
    
    uint32_t soundId = synth_create_physical_drum(frequency, damping, excitation, duration);
    lua_pushinteger(L, soundId);
    return 1;
}

// Effects control
int lua_synth_add_effect(lua_State* L) {
    if (lua_gettop(L) != 2) return luaL_error(L, "sound_add_effect() requires 2 arguments: sound_id, effect_type");
    
    uint32_t soundId = (uint32_t)luaL_checkinteger(L, 1);
    const char* effectType = luaL_checkstring(L, 2);
    
    bool success = synth_add_effect(soundId, effectType);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_remove_effect(lua_State* L) {
    if (lua_gettop(L) != 2) return luaL_error(L, "sound_remove_effect() requires 2 arguments: sound_id, effect_type");
    
    uint32_t soundId = (uint32_t)luaL_checkinteger(L, 1);
    const char* effectType = luaL_checkstring(L, 2);
    
    bool success = synth_remove_effect(soundId, effectType);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_set_effect_param(lua_State* L) {
    if (lua_gettop(L) != 4) return luaL_error(L, "sound_set_effect_param() requires 4 arguments: sound_id, effect_type, param_name, value");
    
    uint32_t soundId = (uint32_t)luaL_checkinteger(L, 1);
    const char* effectType = luaL_checkstring(L, 2);
    const char* paramName = luaL_checkstring(L, 3);
    float value = (float)luaL_checknumber(L, 4);
    
    bool success = synth_set_effect_param(soundId, effectType, paramName, value);
    lua_pushboolean(L, success);
    return 1;
}

// Preset management
int lua_synth_save_preset(lua_State* L) {
    if (lua_gettop(L) != 2) return luaL_error(L, "synth_save_preset() requires 2 arguments: preset_name, preset_data");
    
    const char* presetName = luaL_checkstring(L, 1);
    const char* presetData = luaL_checkstring(L, 2);
    
    bool success = synth_save_preset(presetName, presetData);
    lua_pushboolean(L, success);
    return 1;
}

int lua_synth_load_preset(lua_State* L) {
    if (lua_gettop(L) != 1) return luaL_error(L, "synth_load_preset() requires 1 argument: preset_name");
    
    const char* presetName = luaL_checkstring(L, 1);
    const char* presetData = synth_load_preset(presetName);
    
    if (presetData) {
        lua_pushstring(L, presetData);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int lua_synth_apply_preset(lua_State* L) {
    if (lua_gettop(L) != 2) return luaL_error(L, "synth_apply_preset() requires 2 arguments: sound_id, preset_name");
    
    uint32_t soundId = (uint32_t)luaL_checkinteger(L, 1);
    const char* presetName = luaL_checkstring(L, 2);
    
    bool success = synth_apply_preset(soundId, presetName);
    lua_pushboolean(L, success);
    return 1;
}