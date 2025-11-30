//
//  AudioLuaBindings.h
//  SuperTerminal Framework - Audio Lua Bindings
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#pragma once

#include <cstdint>

// Lua C API
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

// Forward declaration
class AudioSystem;

// Lua binding functions for audio system
extern "C" {
    // System functions
    int lua_audio_initialize(lua_State* L);
    int lua_audio_shutdown(lua_State* L);
    int lua_audio_is_initialized(lua_State* L);
    int lua_audio_wait_queue_empty(lua_State* L);
    
    // Music functions
    int lua_audio_load_music(lua_State* L);
    int lua_audio_play_music(lua_State* L);
    int lua_audio_stop_music(lua_State* L);
    int lua_audio_set_music_volume(lua_State* L);
    
    // Sound effect functions
    int lua_audio_load_sound(lua_State* L);
    int lua_audio_load_sound_with_id(lua_State* L);
    int lua_audio_play_sound(lua_State* L);
    int lua_audio_stop_sound(lua_State* L);
    
    // Synthesis functions (Phase 2)
    int lua_audio_create_oscillator(lua_State* L);
    int lua_audio_set_oscillator_frequency(lua_State* L);
    int lua_audio_set_oscillator_waveform(lua_State* L);
    int lua_audio_set_oscillator_volume(lua_State* L);
    int lua_audio_delete_oscillator(lua_State* L);
    
    // MIDI functions (Phase 2)
    int lua_audio_load_midi(lua_State* L);
    int lua_audio_play_midi(lua_State* L);
    int lua_audio_stop_midi(lua_State* L);
    int lua_audio_midi_note_on(lua_State* L);
    int lua_audio_midi_note_off(lua_State* L);
    int lua_audio_midi_control_change(lua_State* L);
    int lua_audio_midi_program_change(lua_State* L);
    
    // Information functions
    int lua_audio_get_loaded_sound_count(lua_State* L);
    int lua_audio_get_active_voice_count(lua_State* L);
    int lua_audio_get_cpu_usage(lua_State* L);
    
    // Utility functions
    int lua_audio_set_master_volume(lua_State* L);
    int lua_audio_get_master_volume(lua_State* L);
    int lua_audio_set_muted(lua_State* L);
    int lua_audio_is_muted(lua_State* L);
    
    // Synthesis functions
    int lua_synth_initialize(lua_State* L);
    int lua_synth_shutdown(lua_State* L);
    int lua_synth_is_initialized(lua_State* L);
    
    // Sound generation functions
    int lua_synth_generate_beep(lua_State* L);
    int lua_synth_generate_bang(lua_State* L);
    int lua_synth_generate_explode(lua_State* L);
    int lua_synth_generate_big_explosion(lua_State* L);
    int lua_synth_generate_small_explosion(lua_State* L);
    int lua_synth_generate_distant_explosion(lua_State* L);
    int lua_synth_generate_metal_explosion(lua_State* L);
    int lua_synth_generate_zap(lua_State* L);
    int lua_synth_generate_coin(lua_State* L);
    int lua_synth_generate_jump(lua_State* L);
    int lua_synth_generate_powerup(lua_State* L);
    int lua_synth_generate_hurt(lua_State* L);
    int lua_synth_generate_shoot(lua_State* L);
    int lua_synth_generate_click(lua_State* L);
    int lua_synth_generate_pickup(lua_State* L);
    int lua_synth_generate_blip(lua_State* L);
    
    // Sweep sounds
    int lua_synth_generate_sweep_up(lua_State* L);
    int lua_synth_generate_sweep_down(lua_State* L);
    
    // Random/procedural sounds
    int lua_synth_generate_random_beep(lua_State* L);
    
    // Custom synthesis
    int lua_synth_generate_oscillator(lua_State* L);
    
    // Batch generation
    int lua_synth_generate_sound_pack(lua_State* L);
    
    // Utility
    int lua_synth_note_to_frequency(lua_State* L);
    int lua_synth_frequency_to_note(lua_State* L);
    int lua_synth_get_last_generation_time(lua_State* L);
    int lua_synth_get_generated_count(lua_State* L);
    
    // Constants
    int lua_audio_get_wave_constants(lua_State* L);
}

// Registration function to add all audio bindings to Lua
extern "C" void register_audio_lua_bindings(lua_State* L);

// Helper functions for Lua parameter validation
namespace AudioLuaHelpers {
    // Parameter validation
    bool checkAudioInitialized(lua_State* L);
    bool validateMusicId(lua_State* L, int arg);
    bool validateSoundId(lua_State* L, int arg);
    bool validateVolume(lua_State* L, int arg, float& volume);
    bool validatePitch(lua_State* L, int arg, float& pitch);
    bool validatePan(lua_State* L, int arg, float& pan);
    bool validateFrequency(lua_State* L, int arg, float& frequency);
    bool validateWaveform(lua_State* L, int arg, int& waveform);
    bool validateMIDIChannel(lua_State* L, int arg, uint8_t& channel);
    bool validateMIDINote(lua_State* L, int arg, uint8_t& note);
    bool validateMIDIVelocity(lua_State* L, int arg, uint8_t& velocity);
    bool validateMIDIController(lua_State* L, int arg, uint8_t& controller);
    bool validateMIDIProgram(lua_State* L, int arg, uint8_t& program);
    
    // Error handling
    int luaError(lua_State* L, const char* message);
    int luaErrorf(lua_State* L, const char* format, ...);
    
    // Type checking
    bool isValidFilename(const char* filename);
    const char* getFilename(lua_State* L, int arg);
}

// Lua audio constants
namespace AudioLuaConstants {
    // Waveform types
    constexpr int WAVE_SINE = 0;
    constexpr int WAVE_SQUARE = 1;
    constexpr int WAVE_SAWTOOTH = 2;
    constexpr int WAVE_TRIANGLE = 3;
    constexpr int WAVE_NOISE = 4;
    
    // MIDI constants
    constexpr int MIDI_CHANNEL_MIN = 0;
    constexpr int MIDI_CHANNEL_MAX = 15;
    constexpr int MIDI_NOTE_MIN = 0;
    constexpr int MIDI_NOTE_MAX = 127;
    constexpr int MIDI_VELOCITY_MIN = 0;
    constexpr int MIDI_VELOCITY_MAX = 127;
    constexpr int MIDI_CONTROLLER_MIN = 0;
    constexpr int MIDI_CONTROLLER_MAX = 127;
    constexpr int MIDI_PROGRAM_MIN = 0;
    constexpr int MIDI_PROGRAM_MAX = 127;
    
    // Audio limits
    constexpr float VOLUME_MIN = 0.0f;
    constexpr float VOLUME_MAX = 1.0f;
    constexpr float PITCH_MIN = 0.1f;
    constexpr float PITCH_MAX = 4.0f;
    constexpr float PAN_MIN = -1.0f;
    constexpr float PAN_MAX = 1.0f;
    constexpr float FREQUENCY_MIN = 20.0f;
    constexpr float FREQUENCY_MAX = 20000.0f;
}