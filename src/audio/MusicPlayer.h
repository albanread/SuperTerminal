//
//  MusicPlayer.h
//  SuperTerminal - High-Level Music Player with ABC Notation
//
//  Created by Assistant on 2024-11-17.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "ABCPlayerClient.h"

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <chrono>
#include "SynthInstrumentMap.h"

// Forward declarations
class SynthEngine;
namespace SuperTerminal {
    class MidiEngine;
}

namespace SuperTerminal {

// Enhanced ABC Token types for comprehensive parsing
enum class ABCTokenType {
    NOTE,                   // A, B, C, D, E, F, G with optional accidentals
    REST,                   // z, x (invisible rest)
    BAR,                    // |, ||, |:, :|, ::, [|, |]
    CHORD_START,            // [
    CHORD_END,              // ]
    TEMPO,                  // Q:120
    KEY,                    // K:C
    TIME_SIG,               // M:4/4
    LENGTH,                 // L:1/4
    TITLE,                  // T:
    OCTAVE_UP,              // '
    OCTAVE_DOWN,            // ,
    SHARP,                  // ^
    FLAT,                   // _
    NATURAL,                // =
    DURATION,               // /2, 2, 4, etc.
    GRACE_START,            // {
    GRACE_END,              // }
    TUPLET,                 // (2, (3, etc.
    TUPLET_ADVANCED,        // Enhanced: (p:q:r syntax
    TIE,                    // -
    SLUR_START,             // (
    SLUR_END,               // )
    DECORATION,             // ~, H, L, M, O, P, S, T, u, v, .
    DECORATION_EXTENDED,    // Enhanced: !trill!, !fermata!, etc.
    REPEAT_START,           // |:
    REPEAT_END,             // :|
    FIRST_ENDING,           // [1
    SECOND_ENDING,          // [2
    PARTS,                  // P:
    COMPOSER,               // C:
    ORIGIN,                 // O:
    RHYTHM,                 // R:
    NOTES,                  // N:
    WORDS_INLINE,           // w:
    WORDS_BLOCK,            // W:
    BROKEN_RHYTHM,          // <, >, <<, >>
    CHORD_SYMBOL,           // "C", "Am7", etc.
    ANNOTATION,             // "^text", "_text", "<text", ">text"
    BEAM_BREAK,             // space within beamed notes
    MULTI_MEASURE_REST,     // Z, Z4
    ACCIDENTAL_MODIFIER,    // ^^, __
    VOICE,                  // V:
    VOICE_PROPERTY,         // Enhanced: V: with clef=, name=, etc.
    SYMBOL_LINE,            // Enhanced: s: symbol lines
    NAVIGATION,             // Enhanced: D.S., D.C., Fine
    UNKNOWN
};

// Enhanced ABC token with properties support
struct ABCToken {
    ABCTokenType type;
    std::string value;
    int position;
    std::unordered_map<std::string, std::string> properties; // For voice properties
    
    ABCToken(ABCTokenType t, const std::string& v, int pos = 0) 
        : type(t), value(v), position(pos) {}
};

// Enhanced Musical Note with ornament support
struct MusicalNote {
    int midiNote;               // 0-127 MIDI note number
    int velocity;               // 0-127 velocity (enhanced by dynamics)
    double startTime;           // Start time in beats
    double duration;            // Duration in beats
    int channel;                // MIDI channel (0-15)
    int voice;                  // Voice number (for multi-voice music)
    int instrument;             // General MIDI instrument (0-127)
    std::vector<std::string> decorations; // Applied ornaments
    bool isGraceNote;           // True for grace notes
    
    MusicalNote(int note = 60, int vel = 100, double start = 0.0, double dur = 1.0, 
                int ch = 0, int v = 0, int inst = 0)
        : midiNote(note), velocity(vel), startTime(start), duration(dur), 
          channel(ch), voice(v), instrument(inst), isGraceNote(false) {}
};

// Enhanced Voice Information with full ABC 2.1 properties
struct VoiceInfo {
    std::string id;             // Voice ID (V:T1)
    std::string name;           // Full name ("Tenor I")
    std::string shortName;      // Short name ("T.I")
    int instrument;             // General MIDI instrument
    int channel;                // MIDI channel
    std::string clef;           // treble, bass, alto, tenor, perc
    int transpose;              // Semitones transposition
    int octaveShift;            // Octave shift for display/MIDI
    std::string stemDirection;  // up, down, auto
    std::string middle;         // Middle pitch for clef positioning
    int staffLines;             // Number of staff lines (for percussion)
    bool muted;                 // Voice muted state
    float volume;               // Voice volume multiplier
    
    VoiceInfo(const std::string& voiceId = "", int inst = 0, int ch = 0)
        : id(voiceId), name(""), shortName(""), instrument(inst), channel(ch), 
          clef("treble"), transpose(0), octaveShift(0), stemDirection("auto"), 
          middle(""), staffLines(5), muted(false), volume(1.0f) {}
};

// Enhanced Tuplet Information for (p:q:r syntax
struct TupletInfo {
    int notes;              // p - number of notes in tuplet
    int inTimeOf;          // q - time value they occupy (default based on meter)
    int affectNext;        // r - how many notes are affected (default = p)
    double startTime;      // When tuplet starts
    double totalDuration; // Total duration of tuplet
    bool isActive;        // Currently processing tuplet
    
    TupletInfo(int p = 3, int q = 0, int r = 0) 
        : notes(p), inTimeOf(q), affectNext(r > 0 ? r : p),
          startTime(0.0), totalDuration(0.0), isActive(false) {}
};

// Decoration/Ornament Information for enhanced expression
struct DecorationInfo {
    std::string type;       // trill, mordent, turn, fermata, crescendo, etc.
    std::string variant;    // upper, lower, inverted, extended, etc.
    bool isExtended;        // true for !trill(! ... !trill)! syntax
    double intensity;       // for dynamics: pppp=0.1 to ffff=1.0
    bool affectsVelocity;   // true if decoration should modify velocity
    bool affectsDuration;   // true if decoration should modify timing
    
    DecorationInfo(const std::string& t = "", const std::string& v = "")
        : type(t), variant(v), isExtended(false), intensity(1.0),
          affectsVelocity(false), affectsDuration(false) {}
};

// Enhanced Music Sequence with full ABC 2.1 support
struct ST_MusicSequence {
    std::vector<MusicalNote> notes;
    std::unordered_map<std::string, VoiceInfo> voices;  // Voice ID -> Voice Info
    int tempoBPM;
    int timeSignatureNum;
    int timeSignatureDen;
    std::string key;
    std::string keyMode;           // major, minor, dorian, mixolydian, etc.
    double defaultNoteDuration;
    double unitNoteDuration;       // Original unit duration for broken rhythm reset
    std::string name;
    std::string composer;
    std::string origin;
    std::string rhythm;
    std::string parts;
    std::string partsSequence;     // Enhanced: P:AABACA execution order
    std::vector<std::string> lyrics;
    std::unordered_map<std::string, std::vector<std::string>> symbolLines; // s: lines
    bool hasRepeats;
    bool isMultiVoice;
    int globalTranspose;           // Global transposition in semitones
    
    // Compound time signature support
    bool isCompoundTime;           // true for 6/8, 9/8, 12/8, etc.
    int compoundBeatsPerMeasure;   // e.g., 2 for 6/8, 3 for 9/8, 4 for 12/8
    int subdivisionPerBeat;        // e.g., 3 for compound time (dotted quarter note beats)
    
    ST_MusicSequence() 
        : tempoBPM(120), timeSignatureNum(4), timeSignatureDen(4), 
          key("C"), keyMode("major"), defaultNoteDuration(0.25), 
          unitNoteDuration(0.25), name("Untitled"), composer(""), origin(""), 
          rhythm(""), parts(""), partsSequence(""), hasRepeats(false), 
          isMultiVoice(false), globalTranspose(0),
          isCompoundTime(false), compoundBeatsPerMeasure(4), subdivisionPerBeat(2) {}
};

// Music data for a slot
struct MusicData {
    std::string notation;
    std::string name;
    int tempoBPM;
    int instrument;     // GM instrument (0-127)
    int channel;        // MIDI channel (0-15)
    bool loop;
    
    MusicData(const std::string& abc = "", const std::string& title = "", 
              int tempo = 120, int instr = 0, int ch = 0, bool shouldLoop = false)
        : notation(abc), name(title), tempoBPM(tempo), instrument(instr), 
          channel(ch), loop(shouldLoop) {}
};

// Music slot with unique ID
struct MusicSlot {
    uint32_t id;
    MusicData data;
    std::chrono::steady_clock::time_point createdAt;
    
    MusicSlot(uint32_t slotId, const MusicData& musicData)
        : id(slotId), data(musicData), createdAt(std::chrono::steady_clock::now()) {}
};

// Legacy typedef for backward compatibility
typedef MusicData QueuedMusic;

// Active note for polyphonic playback tracking
struct ActiveNote {
    int midiNote;
    int channel;
    double endTime;  // When this note should stop (in beats)
    
    ActiveNote(int note = 60, int ch = 0, double end = 0.0)
        : midiNote(note), channel(ch), endTime(end) {}
};

// Enhanced detailed note structure for comprehensive parsing
struct DetailedNote {
    int midiNote;
    int velocity;
    double startTime;
    double duration;
    int channel;
    std::string noteName;
    bool isRest;
    bool hasAccidental;
    std::string accidental;
    int octave;
    std::vector<DecorationInfo> decorations;  // Enhanced decorations
    bool isGraceNote;
    int voiceId;
    TupletInfo* tupletInfo;                   // Pointer to active tuplet
    
    DetailedNote(int midi = 60, int vel = 100, double start = 0.0, double dur = 0.25,
                 const std::string& name = "C4", bool rest = false, int ch = 0)
        : midiNote(midi), velocity(vel), startTime(start), duration(dur),
          channel(ch), noteName(name), isRest(rest), hasAccidental(false), 
          accidental(""), octave(4), isGraceNote(false), voiceId(0), 
          tupletInfo(nullptr) {}
};

// Enhanced ABC Notation Parser with ABC 2.1 compliance
class ABCParser {
public:
    ABCParser();
    ~ABCParser();
    
    // Main parsing function
    bool parse(const std::string& abcNotation, ST_MusicSequence& sequence);
    
    // Error handling
    std::string getLastError() const { return lastError; }
    
    // Debug control
    void setDebugOutput(bool enabled) { debugOutput = enabled; }
    bool getDebugOutput() const { return debugOutput; }
    
    // Enhanced utility functions
    int noteNameToMIDI(const std::string& noteName, int octave = 4);
    std::string midiToNoteName(int midiNote);
    double durationStringToDuration(const std::string& durStr, double defaultDur);
    double noteLengthToBeats(const std::string& lengthStr);
    
    // Parsing utilities
    bool isInfoField(const std::string& line);
    std::string extractInfoFieldValue(const std::string& line);
    bool shouldIgnoreLine(const std::string& line);
    std::string stripComments(const std::string& line);
    bool isBarLine(const std::string& token);
    bool isRepeatMarker(const std::string& token);
    bool isVoiceField(const std::string& line);
    
private:
    std::string lastError;
    std::atomic<bool> debugOutput{false};
    
    // Enhanced tokenization
    std::vector<ABCToken> tokenize(const std::string& abc);
    ABCTokenType identifyToken(const std::string& token);
    
    // Thread-safe note conversion
    int convertNoteToMidi(const std::string& noteStr);
    
    // Enhanced parsing methods
    bool parseInfoFields(const std::vector<std::string>& lines, ST_MusicSequence& sequence);
    std::vector<std::string> splitIntoLines(const std::string& abc);
    std::string preprocessABC(const std::string& abc);
    bool isValidNoteName(char c);
    bool isAccidental(char c);
    bool isOctaveModifier(char c);
    
    // Enhanced feature parsing
    bool parseExtendedDecorations(const std::string& decoration, DecorationInfo& decorationInfo);
    bool parseAdvancedTuplet(const std::string& tupletStr, TupletInfo& tuplet);
    bool parseVoiceProperties(const std::string& voiceStr, VoiceInfo& voice);
    bool parseKeyWithMode(const std::string& keyStr, std::string& key, std::string& mode, int& transpose);
    
    // Grace notes and ornaments
    bool parseGraceNotes(const std::vector<ABCToken>& tokens, size_t& index, std::vector<DetailedNote>& graceNotes);
    bool parseTuplet(const std::vector<ABCToken>& tokens, size_t& index, TupletInfo& tuplet);
    bool parseDecoration(const std::string& decoration, DecorationInfo& decorationInfo);
    std::vector<int> parseChordSymbol(const std::string& chordName);
    
    // Structure parsing
    bool parseRepeats(const std::vector<ABCToken>& tokens, ST_MusicSequence& sequence);
    bool parseParts(const std::string& partsStr, ST_MusicSequence& sequence);
    
    // Enhanced multi-voice parsing
    bool parseVoiceField(const std::string& line, VoiceInfo& voice);
    bool parseMultiVoiceABC(const std::vector<std::string>& lines, ST_MusicSequence& sequence);
    bool parseVoiceMusic(const std::string& musicStr, const std::string& voiceId, ST_MusicSequence& sequence);
    std::string getCurrentVoiceId() const { return currentVoiceId; }
    
    // Clef and transposition parsing
    void parseClefSpecification(const std::string& clefSpec, std::string& clef, int& clefLine, bool& hasOctaveMarking, int& octaveShift);
    void parseMiddlePitchSpecification(const std::string& middlePitch, int& clefLine);
    
    // Current voice tracking
    std::string currentVoiceId;
    VoiceInfo currentVoice;
    
    // Enhanced note parsing
    DetailedNote parseDetailedNote(const std::string& noteStr, int currentOctave, 
                                  const std::string& key, double startTime, 
                                  double defaultDuration);
    double parseDurationString(const std::string& durStr, double defaultDur);
    
    // Parsing stages
    bool parseHeader(const std::vector<ABCToken>& tokens, ST_MusicSequence& sequence);
    bool parseBody(const std::vector<ABCToken>& tokens, ST_MusicSequence& sequence);
    
    // Note parsing helpers
    int parseNote(const std::string& noteStr, int currentOctave, const std::string& key);
    double parseDuration(const std::string& durStr, double defaultDuration);
    int applyKeySignature(int midiNote, const std::string& key);
    
    // Enhanced key signature support
    struct KeySignature {
        std::string name;
        std::vector<int> sharps;  // MIDI note numbers that should be sharped
        std::vector<int> flats;   // MIDI note numbers that should be flatted
    };
    KeySignature getKeySignature(const std::string& key);
    
    // Compound time signature support
    bool isCompoundMeter(int num, int den);
    void calculateCompoundTimeProperties(ST_MusicSequence& sequence);
    double getEffectiveBeatDuration(const ST_MusicSequence& sequence);
};

// Enhanced Music Player with comprehensive ABC 2.1 support
class ST_MusicPlayer {
public:
    ST_MusicPlayer();
    ~ST_MusicPlayer();
    
    // Initialization
    bool initialize(SuperTerminal::MidiEngine* midiEngine, SynthEngine* synthEngine = nullptr);
    void shutdown();
    bool isInitialized() const { return initialized; }
    
    // High-level music control (thread-safe)
    bool playMusic(const std::string& abcNotation, const std::string& name = "", 
                   int tempoBPM = 120, int instrument = 0);
    
    // Enhanced slot-based API
    uint32_t queueMusic(const std::string& abcNotation, const std::string& name = "", 
                        int tempoBPM = 120, int instrument = 0, bool loop = false);
    uint32_t queueMusic(const std::string& abcNotation, const std::string& name, 
                        int tempoBPM, int instrument, bool loop, int waitAfterMs);
    uint32_t queueMusic(const MusicData& data, int waitAfterMs = 500);
    
    // Legacy API (returns true if ID > 0)
    bool queueMusicLegacy(const std::string& abcNotation, const std::string& name = "", 
                          int tempoBPM = 120, int instrument = 0, bool loop = false);
    void stopMusic();
    void pauseMusic();
    void resumeMusic();
    void clearQueue();
    
    // Frame-based update (call from main thread)
    void update(); // Call this each frame to handle music timing
    
    // Playback state
    bool isMusicPlaying() const { return isPlaying; }
    bool isMusicPaused() const { return isPaused; }
    int getQueueSize() const;
    std::string getCurrentSongName() const;
    double getCurrentPosition() const; // In seconds
    double getCurrentDuration() const; // In seconds
    
    // Global settings
    void setMasterVolume(float volume);       // 0.0 - 1.0
    float getMasterVolume() const { return masterVolume; }
    void setMasterTempo(float multiplier);    // 0.5 - 2.0
    float getMasterTempo() const { return masterTempo; }
    
    // Slot management
    bool removeMusicSlot(uint32_t slotId);
    bool hasMusicSlot(uint32_t slotId) const;
    MusicData getMusicSlotData(uint32_t slotId) const;
    std::vector<uint32_t> getQueuedSlotIds() const;
    
    // Queue management
    bool skipToNext();
    bool skipToPrevious();
    std::vector<std::string> getQueuedSongNames() const;
    
    // Enhanced debug control
    void setDebugMidiOutput(bool enabled) { debugMidiOutput = enabled; }
    bool getDebugMidiOutput() const { return debugMidiOutput; }
    void setDebugParserOutput(bool enabled);
    bool getDebugParserOutput() const { return debugParserOutput; }
    
    // Enhanced test songs with ABC 2.1 features
    static std::string getTestSong(const std::string& songName);
    
private:
    std::atomic<bool> initialized{false};
    SuperTerminal::MidiEngine* midiEngine;
    SynthEngine* synthEngine;
    ABCParser parser;
    
    // Enhanced channel management
    std::unordered_map<int, int> channelPrograms;  // channel -> program number
    std::unordered_map<std::string, const MusicalNote*> currentNotes;  // note tracking
    
    // Background music thread timing
    std::atomic<bool> isPlaying{false};
    std::atomic<bool> isPaused{false};
    std::atomic<bool> musicThreadRunning{false};
    std::thread musicThread;
    
    // Enhanced polyphonic playback tracking
    std::vector<ActiveNote> activeNotes;
    double currentPlaybackTime{0.0};  // Current time in beats
    size_t nextNoteIndex{0};          // Next unplayed note in sorted sequence
    std::chrono::steady_clock::time_point lastUpdateTime;

    // Enhanced music slot system
    std::unordered_map<uint32_t, MusicSlot> musicSlots;  // ID -> MusicSlot
    std::queue<uint32_t> playbackQueue;                  // Queue of slot IDs
    std::atomic<uint32_t> nextSlotId{1};                 // ID generator
    mutable std::mutex queueMutex;
    std::condition_variable queueCondition;
    
    // Separate mutex for music state to avoid deadlocks
    mutable std::mutex musicStateMutex;
    
    // Current playback state
    std::unique_ptr<ST_MusicSequence> currentSequence;
    std::atomic<float> masterVolume{1.0f};
    std::atomic<float> masterTempo{1.0f};
    std::string currentSongName;
    mutable std::mutex stateMutex;
    
    // Enhanced debug control
    std::atomic<bool> debugMidiOutput{false};
    std::atomic<bool> debugParserOutput{false};
    
    // Background thread functions
    bool startNextSong();
    void updateMusicTiming(); // Called by background thread
    void stopCurrentSequence();
    void sortSequenceNotesByStartTime();
    void musicTimingThreadLoop(); // Main background thread function
    void startMusicThread();
    void stopMusicThread();
    
    // Enhanced timing helpers
    std::chrono::milliseconds beatsToMilliseconds(double beats, int tempoBPM) const;
    double millisecondsToBeats(std::chrono::milliseconds ms, int tempoBPM) const;
    
    // Enhanced MIDI helpers with ornament support
    void sendNoteOn(int channel, int note, int velocity, const MusicalNote* noteInfo = nullptr);
    void sendNoteOff(int channel, int note);
    void sendProgramChange(int channel, int program);
    void allNotesOff();
    
    // Enhanced synth routing with ornament processing
    void playSynthNote(int channel, int note, int velocity, int program, const MusicalNote* noteInfo = nullptr);
    void stopSynthNote(int channel, int note, int program);
    
    // Ornament processing helpers
    void processOrnaments(const MusicalNote& note, std::vector<MusicalNote>& expandedNotes);
    void processTrill(const MusicalNote& note, const DecorationInfo& decoration, std::vector<MusicalNote>& notes);
    void processMordent(const MusicalNote& note, const DecorationInfo& decoration, std::vector<MusicalNote>& notes);
    void processTurn(const MusicalNote& note, const DecorationInfo& decoration, std::vector<MusicalNote>& notes);
    void processDynamics(MusicalNote& note, const DecorationInfo& decoration);
    
    // Utility
    void logInfo(const std::string& message);
    void logError(const std::string& message);
};

} // namespace SuperTerminal

// Enhanced C API for comprehensive ABC 2.1 support
extern "C" {
    // Initialize music player (called by SuperTerminal automatically)
    bool music_initialize();
    void music_shutdown();
    
    // High-level music functions
    bool play_music(const char* abc_notation, const char* name, int tempo_bpm, int instrument);
    
    // Enhanced slot-based API
    uint32_t queue_music_slot(const char* abc_notation, const char* name, int tempo_bpm, int instrument, bool loop);
    uint32_t queue_music_slot_with_wait(const char* abc_notation, const char* name, int tempo_bpm, int instrument, bool loop, int wait_after_ms);
    
    // Legacy API (wrapper around slot-based API)
    bool queue_music(const char* abc_notation, const char* name, int tempo_bpm, int instrument, bool loop);
    void stop_music();
    void pause_music();
    void resume_music();
    void clear_music_queue();
    
    // Playback state
    bool is_music_playing();
    bool is_music_paused();
    int get_music_queue_size();
    const char* get_current_song_name();
    double get_music_position(); // seconds
    double get_music_duration(); // seconds
    
    // Control
    void set_music_volume(float volume);
    float get_music_volume();
    void set_music_tempo(float multiplier);
    float get_music_tempo();
    
    // Queue navigation
    bool skip_to_next_song();
    bool skip_to_previous_song();
    
    // Enhanced slot management functions
    bool remove_music_slot(uint32_t slot_id);
    bool has_music_slot(uint32_t slot_id);
    int get_music_slot_count();
    
    // Convenience functions with default parameters
    bool play_music_simple(const char* abc_notation);
    bool queue_music_simple(const char* abc_notation);
    uint32_t queue_music_slot_simple(const char* abc_notation);
    
    // Enhanced debug control
    void set_music_debug_output(bool enabled);
    bool get_music_debug_output();
    void set_music_parser_debug_output(bool enabled);
    bool get_music_parser_debug_output();
    
    // Enhanced test songs with ABC 2.1 features
    const char* get_test_song(const char* song_name); // "scale", "arpeggio", "ornaments", "multivoice", etc.

    // ABC Player Client Integration (New Multi-Voice Player)
    bool abc_music_initialize();
    void abc_music_shutdown();
    bool abc_music_is_initialized();
    
    bool abc_play_music(const char* abc_notation, const char* name = nullptr);
    bool abc_play_music_file(const char* filename);
    bool abc_stop_music();
    bool abc_pause_music();
    bool abc_resume_music();
    bool abc_clear_music_queue();
    
    bool abc_set_music_volume(float volume);
    bool abc_is_music_playing();
    bool abc_is_music_paused();
    int abc_get_music_queue_size();
    float abc_get_music_volume();
    
    void abc_set_auto_start_server(bool auto_start);
    void abc_set_debug_output(bool debug);
}