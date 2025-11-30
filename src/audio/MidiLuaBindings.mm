//
//  MidiLuaBindings.cpp
//  SuperTerminal - Lua MIDI Tracker API
//
//  Created by Assistant on 2024-11-17.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "MidiEngine.h"
#include "AudioSystem.h"
#include <lua.hpp>
#include <iostream>
#include <string>
#include <vector>

using namespace SuperTerminal;

// External reference to global audio system
extern std::unique_ptr<AudioSystem> g_audioSystem;

// Helper function to get MIDI engine
static SuperTerminal::MidiEngine* getMidiEngine(lua_State* L) {
    if (!g_audioSystem || !g_audioSystem->isInitialized()) {
        luaL_error(L, "Audio system not initialized. Call audio_initialize() first.");
        return nullptr;
    }
    
    SuperTerminal::MidiEngine* midiEngine = g_audioSystem->getMidiEngine();
    if (!midiEngine || !midiEngine->isInitialized()) {
        luaL_error(L, "MIDI engine not initialized. Call audio_initialize() first.");
        return nullptr;
    }
    return midiEngine;
}

// Helper to check if MIDI is initialized
static bool isMidiInitialized() {
    return g_audioSystem && g_audioSystem->isInitialized() && 
           g_audioSystem->getMidiEngine() && g_audioSystem->getMidiEngine()->isInitialized();
}

// Lua API Functions

// System Management
static int lua_midi_initialize(lua_State* L) {
    // MIDI is initialized automatically with the audio system
    lua_pushboolean(L, isMidiInitialized() ? 1 : 0);
    return 1;
}

static int lua_midi_shutdown(lua_State* L) {
    // MIDI shutdown is handled automatically with audio system
    std::cout << "MidiLuaBindings: MIDI shutdown requested from Lua" << std::endl;
    return 0;
}

static int lua_midi_is_initialized(lua_State* L) {
    lua_pushboolean(L, isMidiInitialized() ? 1 : 0);
    return 1;
}

// Sequence Management
static int lua_midi_create_sequence(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    const char* name = luaL_optstring(L, 1, "Sequence");
    double tempo = luaL_optnumber(L, 2, 120.0);
    
    int sequenceId = engine->createSequence(name, tempo);
    lua_pushinteger(L, sequenceId);
    return 1;
}

static int lua_midi_delete_sequence(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    bool success = engine->deleteSequence(sequenceId);
    lua_pushboolean(L, success ? 1 : 0);
    return 1;
}

// Track Management
static int lua_midi_add_track(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    const char* name = luaL_optstring(L, 2, "Track");
    int channel = luaL_optinteger(L, 3, 1);
    
    MidiSequence* sequence = engine->getSequence(sequenceId);
    if (!sequence) {
        luaL_error(L, "Invalid sequence ID: %d", sequenceId);
        return 0;
    }
    
    int trackIndex = sequence->addTrack(name, channel);
    lua_pushinteger(L, trackIndex);
    return 1;
}

static int lua_midi_remove_track(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    int trackIndex = luaL_checkinteger(L, 2);
    
    MidiSequence* sequence = engine->getSequence(sequenceId);
    if (!sequence) {
        luaL_error(L, "Invalid sequence ID: %d", sequenceId);
        return 0;
    }
    
    sequence->removeTrack(trackIndex);
    return 0;
}

// Note Events
static int lua_midi_add_note(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    int trackIndex = luaL_checkinteger(L, 2);
    int note = luaL_checkinteger(L, 3);
    int velocity = luaL_optinteger(L, 4, 100);
    double startTime = luaL_optnumber(L, 5, 0.0);
    double duration = luaL_optnumber(L, 6, 1.0);
    
    MidiSequence* sequence = engine->getSequence(sequenceId);
    if (!sequence) {
        luaL_error(L, "Invalid sequence ID: %d", sequenceId);
        return 0;
    }
    
    MidiTrack* track = sequence->getTrack(trackIndex);
    if (!track) {
        luaL_error(L, "Invalid track index: %d", trackIndex);
        return 0;
    }
    
    track->addNote(note, velocity, startTime, duration);
    return 0;
}

static int lua_midi_add_note_by_name(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    int trackIndex = luaL_checkinteger(L, 2);
    const char* noteName = luaL_checkstring(L, 3);
    int velocity = luaL_optinteger(L, 4, 100);
    double startTime = luaL_optnumber(L, 5, 0.0);
    double duration = luaL_optnumber(L, 6, 1.0);
    
    int note = MidiEngine::noteNameToNumber(noteName);
    
    MidiSequence* sequence = engine->getSequence(sequenceId);
    if (!sequence) {
        luaL_error(L, "Invalid sequence ID: %d", sequenceId);
        return 0;
    }
    
    MidiTrack* track = sequence->getTrack(trackIndex);
    if (!track) {
        luaL_error(L, "Invalid track index: %d", trackIndex);
        return 0;
    }
    
    track->addNote(note, velocity, startTime, duration);
    return 0;
}

// Chord and Pattern Functions
static int lua_midi_add_chord(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    int trackIndex = luaL_checkinteger(L, 2);
    
    // Get notes table
    luaL_checktype(L, 3, LUA_TTABLE);
    std::vector<int> notes;
    lua_pushnil(L);
    while (lua_next(L, 3) != 0) {
        notes.push_back(luaL_checkinteger(L, -1));
        lua_pop(L, 1);
    }
    
    int velocity = luaL_optinteger(L, 4, 100);
    double startTime = luaL_optnumber(L, 5, 0.0);
    double duration = luaL_optnumber(L, 6, 1.0);
    
    MidiSequence* sequence = engine->getSequence(sequenceId);
    if (!sequence) {
        luaL_error(L, "Invalid sequence ID: %d", sequenceId);
        return 0;
    }
    
    sequence->addChord(trackIndex, notes, velocity, startTime, duration);
    return 0;
}

static int lua_midi_add_chord_by_name(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    int trackIndex = luaL_checkinteger(L, 2);
    const char* chordName = luaL_checkstring(L, 3);
    int rootNote = luaL_checkinteger(L, 4);
    int velocity = luaL_optinteger(L, 5, 100);
    double startTime = luaL_optnumber(L, 6, 0.0);
    double duration = luaL_optnumber(L, 7, 1.0);
    
    std::vector<int> chord = engine->parseChord(chordName, rootNote);
    
    MidiSequence* sequence = engine->getSequence(sequenceId);
    if (!sequence) {
        luaL_error(L, "Invalid sequence ID: %d", sequenceId);
        return 0;
    }
    
    sequence->addChord(trackIndex, chord, velocity, startTime, duration);
    return 0;
}

static int lua_midi_add_arpeggio(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    int trackIndex = luaL_checkinteger(L, 2);
    
    // Get notes table
    luaL_checktype(L, 3, LUA_TTABLE);
    std::vector<int> notes;
    lua_pushnil(L);
    while (lua_next(L, 3) != 0) {
        notes.push_back(luaL_checkinteger(L, -1));
        lua_pop(L, 1);
    }
    
    int velocity = luaL_optinteger(L, 4, 100);
    double startTime = luaL_optnumber(L, 5, 0.0);
    double stepDuration = luaL_optnumber(L, 6, 0.25);
    
    MidiSequence* sequence = engine->getSequence(sequenceId);
    if (!sequence) {
        luaL_error(L, "Invalid sequence ID: %d", sequenceId);
        return 0;
    }
    
    sequence->addArpeggio(trackIndex, notes, velocity, startTime, stepDuration);
    return 0;
}

static int lua_midi_add_drum_pattern(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    int trackIndex = luaL_checkinteger(L, 2);
    
    // Get drums table
    luaL_checktype(L, 3, LUA_TTABLE);
    std::vector<int> drums;
    lua_pushnil(L);
    while (lua_next(L, 3) != 0) {
        drums.push_back(luaL_checkinteger(L, -1));
        lua_pop(L, 1);
    }
    
    // Get pattern table
    luaL_checktype(L, 4, LUA_TTABLE);
    std::vector<bool> pattern;
    lua_pushnil(L);
    while (lua_next(L, 4) != 0) {
        pattern.push_back(lua_toboolean(L, -1));
        lua_pop(L, 1);
    }
    
    double startTime = luaL_optnumber(L, 5, 0.0);
    double stepDuration = luaL_optnumber(L, 6, 0.25);
    
    MidiSequence* sequence = engine->getSequence(sequenceId);
    if (!sequence) {
        luaL_error(L, "Invalid sequence ID: %d", sequenceId);
        return 0;
    }
    
    sequence->addDrumPattern(trackIndex, drums, pattern, startTime, stepDuration);
    return 0;
}

// Control Changes and Program Changes
static int lua_midi_add_program_change(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    int trackIndex = luaL_checkinteger(L, 2);
    int program = luaL_checkinteger(L, 3);
    double time = luaL_optnumber(L, 4, 0.0);
    
    MidiSequence* sequence = engine->getSequence(sequenceId);
    if (!sequence) {
        luaL_error(L, "Invalid sequence ID: %d", sequenceId);
        return 0;
    }
    
    MidiTrack* track = sequence->getTrack(trackIndex);
    if (!track) {
        luaL_error(L, "Invalid track index: %d", trackIndex);
        return 0;
    }
    
    track->addProgramChange(program, time);
    return 0;
}

static int lua_midi_add_control_change(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    int trackIndex = luaL_checkinteger(L, 2);
    int controller = luaL_checkinteger(L, 3);
    int value = luaL_checkinteger(L, 4);
    double time = luaL_optnumber(L, 5, 0.0);
    
    MidiSequence* sequence = engine->getSequence(sequenceId);
    if (!sequence) {
        luaL_error(L, "Invalid sequence ID: %d", sequenceId);
        return 0;
    }
    
    MidiTrack* track = sequence->getTrack(trackIndex);
    if (!track) {
        luaL_error(L, "Invalid track index: %d", trackIndex);
        return 0;
    }
    
    track->addControlChange(controller, value, time);
    return 0;
}

// Sequence Properties
static int lua_midi_set_tempo(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    double tempo = luaL_checknumber(L, 2);
    
    MidiSequence* sequence = engine->getSequence(sequenceId);
    if (!sequence) {
        luaL_error(L, "Invalid sequence ID: %d", sequenceId);
        return 0;
    }
    
    sequence->setTempo(tempo);
    return 0;
}

static int lua_midi_set_time_signature(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    int numerator = luaL_checkinteger(L, 2);
    int denominator = luaL_checkinteger(L, 3);
    
    MidiSequence* sequence = engine->getSequence(sequenceId);
    if (!sequence) {
        luaL_error(L, "Invalid sequence ID: %d", sequenceId);
        return 0;
    }
    
    sequence->setTimeSignature(numerator, denominator);
    return 0;
}

// Playback Control
static int lua_midi_play_sequence(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    float volume = luaL_optnumber(L, 2, 1.0);
    bool loop = lua_toboolean(L, 3);
    
    bool success = engine->playSequence(sequenceId, volume, loop);
    lua_pushboolean(L, success ? 1 : 0);
    return 1;
}

static int lua_midi_stop_sequence(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    engine->stopSequence(sequenceId);
    return 0;
}

static int lua_midi_pause_sequence(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    engine->pauseSequence(sequenceId);
    return 0;
}

static int lua_midi_resume_sequence(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    engine->resumeSequence(sequenceId);
    return 0;
}

static int lua_midi_set_sequence_volume(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int sequenceId = luaL_checkinteger(L, 1);
    float volume = luaL_checknumber(L, 2);
    
    engine->setSequenceVolume(sequenceId, volume);
    return 0;
}

// Real-time MIDI
static int lua_midi_play_note(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int channel = luaL_checkinteger(L, 1);
    int note = luaL_checkinteger(L, 2);
    int velocity = luaL_optinteger(L, 3, 100);
    double duration = luaL_optnumber(L, 4, 0.0);
    
    engine->playNote(channel, note, velocity, duration);
    return 0;
}

static int lua_midi_play_note_by_name(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int channel = luaL_checkinteger(L, 1);
    const char* noteName = luaL_checkstring(L, 2);
    int velocity = luaL_optinteger(L, 3, 100);
    double duration = luaL_optnumber(L, 4, 0.0);
    
    int note = MidiEngine::noteNameToNumber(noteName);
    engine->playNote(channel, note, velocity, duration);
    return 0;
}

static int lua_midi_stop_note(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int channel = luaL_checkinteger(L, 1);
    int note = luaL_checkinteger(L, 2);
    
    engine->stopNote(channel, note);
    return 0;
}

static int lua_midi_send_control_change(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int channel = luaL_checkinteger(L, 1);
    int controller = luaL_checkinteger(L, 2);
    int value = luaL_checkinteger(L, 3);
    
    engine->sendControlChange(channel, controller, value);
    return 0;
}

static int lua_midi_send_program_change(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int channel = luaL_checkinteger(L, 1);
    int program = luaL_checkinteger(L, 2);
    
    engine->sendProgramChange(channel, program);
    return 0;
}

static int lua_midi_all_notes_off(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    int channel = luaL_optinteger(L, 1, -1); // -1 for all channels
    engine->allNotesOff(channel);
    return 0;
}

// Global Controls
static int lua_midi_set_master_volume(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    float volume = luaL_checknumber(L, 1);
    engine->setMasterVolume(volume);
    return 0;
}

static int lua_midi_get_master_volume(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    lua_pushnumber(L, engine->getMasterVolume());
    return 1;
}

static int lua_midi_set_master_tempo(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    float tempoMultiplier = luaL_checknumber(L, 1);
    engine->setMasterTempo(tempoMultiplier);
    return 0;
}

// Utility Functions
static int lua_midi_note_name_to_number(lua_State* L) {
    const char* noteName = luaL_checkstring(L, 1);
    int noteNumber = MidiEngine::noteNameToNumber(noteName);
    lua_pushinteger(L, noteNumber);
    return 1;
}

static int lua_midi_note_number_to_name(lua_State* L) {
    int noteNumber = luaL_checkinteger(L, 1);
    std::string noteName = MidiEngine::noteNumberToName(noteNumber);
    lua_pushstring(L, noteName.c_str());
    return 1;
}

static int lua_midi_beats_to_seconds(lua_State* L) {
    double beats = luaL_checknumber(L, 1);
    double tempo = luaL_checknumber(L, 2);
    double seconds = MidiEngine::beatsToSeconds(beats, tempo);
    lua_pushnumber(L, seconds);
    return 1;
}

static int lua_midi_seconds_to_beats(lua_State* L) {
    double seconds = luaL_checknumber(L, 1);
    double tempo = luaL_checknumber(L, 2);
    double beats = MidiEngine::secondsToBeats(seconds, tempo);
    lua_pushnumber(L, beats);
    return 1;
}

// Helper Functions for Tracker-Style Composition
static int lua_midi_get_chord_notes(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    const char* chordName = luaL_checkstring(L, 1);
    int rootNote = luaL_checkinteger(L, 2);
    
    std::vector<int> chord = engine->parseChord(chordName, rootNote);
    
    lua_createtable(L, chord.size(), 0);
    for (size_t i = 0; i < chord.size(); ++i) {
        lua_pushinteger(L, chord[i]);
        lua_rawseti(L, -2, i + 1);
    }
    
    return 1;
}

static int lua_midi_get_scale_notes(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    const char* scaleName = luaL_checkstring(L, 1);
    int rootNote = luaL_checkinteger(L, 2);
    
    std::vector<int> scale = engine->parseScale(scaleName, rootNote);
    
    lua_createtable(L, scale.size(), 0);
    for (size_t i = 0; i < scale.size(); ++i) {
        lua_pushinteger(L, scale[i]);
        lua_rawseti(L, -2, i + 1);
    }
    
    return 1;
}

// Status and Info
static int lua_midi_get_active_note_count(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) {
        lua_pushinteger(L, 0);
        return 1;
    }
    
    lua_pushinteger(L, engine->getActiveNoteCount());
    return 1;
}

static int lua_midi_get_loaded_sequence_count(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) {
        lua_pushinteger(L, 0);
        return 1;
    }
    
    lua_pushinteger(L, engine->getLoadedSequenceCount());
    return 1;
}

static int lua_midi_wait_for_playback(lua_State* L) {
    MidiEngine* engine = getMidiEngine(L);
    if (!engine) return 0;
    
    engine->waitForPlaybackComplete();
    return 0;
}

// Registration Function
void registerMidiLuaBindings(lua_State* L, SuperTerminal::MidiEngine* midiEngine) {
    // midiEngine parameter is ignored - we get it dynamically from g_audioSystem
    
    // System functions
    lua_register(L, "midi_initialize", lua_midi_initialize);
    lua_register(L, "midi_shutdown", lua_midi_shutdown);
    lua_register(L, "midi_is_initialized", lua_midi_is_initialized);
    
    // Sequence management
    lua_register(L, "midi_create_sequence", lua_midi_create_sequence);
    lua_register(L, "midi_delete_sequence", lua_midi_delete_sequence);
    
    // Track management
    lua_register(L, "midi_add_track", lua_midi_add_track);
    lua_register(L, "midi_remove_track", lua_midi_remove_track);
    
    // Note events
    lua_register(L, "midi_add_note", lua_midi_add_note);
    lua_register(L, "midi_add_note_by_name", lua_midi_add_note_by_name);
    
    // Chord and pattern functions
    lua_register(L, "midi_add_chord", lua_midi_add_chord);
    lua_register(L, "midi_add_chord_by_name", lua_midi_add_chord_by_name);
    lua_register(L, "midi_add_arpeggio", lua_midi_add_arpeggio);
    lua_register(L, "midi_add_drum_pattern", lua_midi_add_drum_pattern);
    
    // Control and program changes
    lua_register(L, "midi_add_program_change", lua_midi_add_program_change);
    lua_register(L, "midi_add_control_change", lua_midi_add_control_change);
    
    // Sequence properties
    lua_register(L, "midi_set_tempo", lua_midi_set_tempo);
    lua_register(L, "midi_set_time_signature", lua_midi_set_time_signature);
    
    // Playback control
    lua_register(L, "midi_play_sequence", lua_midi_play_sequence);
    lua_register(L, "midi_stop_sequence", lua_midi_stop_sequence);
    lua_register(L, "midi_pause_sequence", lua_midi_pause_sequence);
    lua_register(L, "midi_resume_sequence", lua_midi_resume_sequence);
    lua_register(L, "midi_set_sequence_volume", lua_midi_set_sequence_volume);
    
    // Real-time MIDI
    lua_register(L, "midi_play_note", lua_midi_play_note);
    lua_register(L, "midi_play_note_by_name", lua_midi_play_note_by_name);
    lua_register(L, "midi_stop_note", lua_midi_stop_note);
    lua_register(L, "midi_send_control_change", lua_midi_send_control_change);
    lua_register(L, "midi_send_program_change", lua_midi_send_program_change);
    lua_register(L, "midi_all_notes_off", lua_midi_all_notes_off);
    
    // Global controls
    lua_register(L, "midi_set_master_volume", lua_midi_set_master_volume);
    lua_register(L, "midi_get_master_volume", lua_midi_get_master_volume);
    lua_register(L, "midi_set_master_tempo", lua_midi_set_master_tempo);
    
    // Utility functions
    lua_register(L, "midi_note_name_to_number", lua_midi_note_name_to_number);
    lua_register(L, "midi_note_number_to_name", lua_midi_note_number_to_name);
    lua_register(L, "midi_beats_to_seconds", lua_midi_beats_to_seconds);
    lua_register(L, "midi_seconds_to_beats", lua_midi_seconds_to_beats);
    
    // Helper functions
    lua_register(L, "midi_get_chord_notes", lua_midi_get_chord_notes);
    lua_register(L, "midi_get_scale_notes", lua_midi_get_scale_notes);
    
    // Status functions
    lua_register(L, "midi_get_active_note_count", lua_midi_get_active_note_count);
    lua_register(L, "midi_get_loaded_sequence_count", lua_midi_get_loaded_sequence_count);
    lua_register(L, "midi_wait_for_playback", lua_midi_wait_for_playback);
    
    // MIDI Constants
    lua_pushinteger(L, 36); lua_setglobal(L, "MIDI_KICK_DRUM");
    lua_pushinteger(L, 38); lua_setglobal(L, "MIDI_SNARE_DRUM");
    lua_pushinteger(L, 42); lua_setglobal(L, "MIDI_CLOSED_HIHAT");
    lua_pushinteger(L, 46); lua_setglobal(L, "MIDI_OPEN_HIHAT");
    lua_pushinteger(L, 49); lua_setglobal(L, "MIDI_CRASH_CYMBAL");
    lua_pushinteger(L, 51); lua_setglobal(L, "MIDI_RIDE_CYMBAL");
    
    // Standard MIDI note numbers
    lua_pushinteger(L, 60); lua_setglobal(L, "MIDI_MIDDLE_C");
    lua_pushinteger(L, 69); lua_setglobal(L, "MIDI_A440");
    
    // Controller numbers
    lua_pushinteger(L, 1);   lua_setglobal(L, "MIDI_CC_MODULATION");
    lua_pushinteger(L, 7);   lua_setglobal(L, "MIDI_CC_VOLUME");
    lua_pushinteger(L, 10);  lua_setglobal(L, "MIDI_CC_PAN");
    lua_pushinteger(L, 11);  lua_setglobal(L, "MIDI_CC_EXPRESSION");
    lua_pushinteger(L, 64);  lua_setglobal(L, "MIDI_CC_SUSTAIN_PEDAL");
    lua_pushinteger(L, 91);  lua_setglobal(L, "MIDI_CC_REVERB");
    lua_pushinteger(L, 93);  lua_setglobal(L, "MIDI_CC_CHORUS");
    lua_pushinteger(L, 123); lua_setglobal(L, "MIDI_CC_ALL_NOTES_OFF");
    
    std::cout << "MidiLuaBindings: Registered all MIDI functions and constants" << std::endl;
}