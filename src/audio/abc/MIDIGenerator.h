#ifndef MIDI_GENERATOR_H
#define MIDI_GENERATOR_H

#include "ABCTypes.h"
#include <vector>
#include <map>
#include <queue>
#include <memory>
#include <functional>

namespace ABCPlayer {

// MIDI event types
enum class MIDIEventType {
    NOTE_ON,
    NOTE_OFF,
    PROGRAM_CHANGE,
    CONTROL_CHANGE,
    PITCH_BEND,
    META_TEMPO,
    META_TIME_SIGNATURE,
    META_KEY_SIGNATURE,
    META_TEXT,
    META_END_OF_TRACK
};

// MIDI event structure
struct MIDIEvent {
    MIDIEventType type;
    double timestamp;           // Time in beats
    int channel;               // MIDI channel (0-15)
    int data1;                 // First data byte
    int data2;                 // Second data byte
    std::vector<uint8_t> meta_data;  // For meta events
    
    MIDIEvent(MIDIEventType t = MIDIEventType::NOTE_ON, double time = 0.0, 
              int ch = 0, int d1 = 0, int d2 = 0)
        : type(t), timestamp(time), channel(ch), data1(d1), data2(d2) {}
};

// MIDI track - sequence of events for one track
struct MIDITrack {
    int track_number;
    TrackType type;
    int voice_number;
    int channel;
    std::string name;
    std::vector<MIDIEvent> events;
    
    MIDITrack(int num = 0, TrackType t = TrackType::NOTES) 
        : track_number(num), type(t), voice_number(1), channel(0) {}
};

// Channel assignment manager
class ChannelManager {
public:
    ChannelManager();
    
    int assignChannel(int voice_id, TrackType track_type);
    void releaseChannel(int channel);
    bool isChannelFree(int channel) const;
    void reset();
    
    // Channel 10 (index 9) is reserved for percussion
    static const int PERCUSSION_CHANNEL = 9;
    static const int MAX_CHANNELS = 16;
    
private:
    std::vector<bool> channels_in_use_;
    std::map<int, int> voice_to_channel_;
    int next_available_channel_;
};

// MIDI file generator
class MIDIGenerator {
public:
    MIDIGenerator();
    ~MIDIGenerator();
    
    // Main generation interface
    bool generateMIDI(const ABCTune& tune, std::vector<MIDITrack>& tracks);
    bool generateMIDIFile(const ABCTune& tune, const std::string& filename);
    
    // Configuration
    void setTicksPerQuarter(int ticks) { ticks_per_quarter_ = ticks; }
    void setDefaultTempo(int bpm) { default_tempo_ = bpm; }
    void setDefaultVelocity(int velocity) { default_velocity_ = velocity; }
    
    // Error reporting
    const std::vector<std::string>& getErrors() const { return errors_; }
    void clearErrors() { errors_.clear(); }
    
private:
    // Configuration
    int ticks_per_quarter_;
    int default_tempo_;
    int default_velocity_;
    
    // State during generation
    ChannelManager channel_manager_;
    std::map<int, VoiceContext> voice_contexts_;
    double current_time_;
    int current_tempo_;
    
    // Active notes tracking (for note-off generation)
    struct ActiveNote {
        int midi_note;
        int channel;
        double end_time;
        int velocity;
    };
    std::multimap<double, ActiveNote> active_notes_;
    
    // Error collection
    std::vector<std::string> errors_;
    
    // Main generation methods
    void createTracks(const ABCTune& tune, std::vector<MIDITrack>& tracks);
    void assignChannels(const ABCTune& tune, std::vector<MIDITrack>& tracks);
    void generateTrackEvents(const ABCTune& tune, MIDITrack& track);
    void processTempoTrack(const ABCTune& tune, MIDITrack& track);
    
    // Feature processing
    void processFeature(const Feature& feature, MIDITrack& track);
    void processNote(const Note& note, double timestamp, int voice_id, MIDITrack& track);
    void processRest(const Rest& rest, double timestamp, int voice_id, MIDITrack& track);
    void processChord(const Chord& chord, double timestamp, int voice_id, MIDITrack& track);
    void processGuitarChord(const GuitarChord& gchord, double timestamp, int voice_id, MIDITrack& track);
    void processVoiceChange(const VoiceChange& voice_change, double timestamp, MIDITrack& track);
    void processTempo(const Tempo& tempo, double timestamp, MIDITrack& track);
    void processTimeSignature(const TimeSignature& timesig, double timestamp, MIDITrack& track);
    void processKeySignature(const KeySignature& key, double timestamp, MIDITrack& track);
    
    // Note management
    void scheduleNoteOn(int midi_note, int velocity, int channel, double timestamp, MIDITrack& track);
    void scheduleNoteOff(int midi_note, int channel, double timestamp, MIDITrack& track);
    void processActiveNotes(double current_time, MIDITrack& track);
    void flushActiveNotes(MIDITrack& track);
    
    // Guitar chord processing
    std::vector<int> parseGuitarChord(const std::string& chord_symbol);
    void playGuitarChordNotes(const std::vector<int>& notes, double timestamp, 
                             double duration, int channel, MIDITrack& track);
    
    // MIDI event creation
    void addNoteOn(int midi_note, int velocity, int channel, double timestamp, MIDITrack& track);
    void addNoteOff(int midi_note, int channel, double timestamp, MIDITrack& track);
    void addProgramChange(int program, int channel, double timestamp, MIDITrack& track);
    void addControlChange(int controller, int value, int channel, double timestamp, MIDITrack& track);
    void addTempo(int bpm, double timestamp, MIDITrack& track);
    void addTimeSignature(int num, int denom, double timestamp, MIDITrack& track);
    void addKeySignature(int sharps, bool major, double timestamp, MIDITrack& track);
    void addText(const std::string& text, double timestamp, MIDITrack& track);
    void addEndOfTrack(double timestamp, MIDITrack& track);
    
    // Time conversion
    long beatsToTicks(double beats) const;
    double ticksToBeats(long ticks) const;
    
public:
    
    // MIDI file writing
    bool writeMIDIFile(const std::vector<MIDITrack>& tracks, const std::string& filename);
    void writeFileHeader(std::ofstream& file, int num_tracks);
    void writeTrack(std::ofstream& file, const MIDITrack& track);
    void writeVariableLength(std::ostream& file, uint32_t value);
    void writeMIDIEvent(std::ostream& file, const MIDIEvent& event, uint8_t& running_status);
    
    // Utility methods
    void addError(const std::string& message);
    int getMIDINoteFromChord(const std::string& chord_root, int octave = 4);
    
    // Standard MIDI values
    static const int DEFAULT_TICKS_PER_QUARTER = 480;
    static const int DEFAULT_TEMPO = 120;
    static const int DEFAULT_VELOCITY = 80;
    
    // Guitar chord definitions
    static const std::map<std::string, std::vector<int>> standard_chords_;
};

// Inline implementations
inline long MIDIGenerator::beatsToTicks(double beats) const {
    // Parser timestamps are in whole-note fractions (0.25 = quarter note, 0.125 = eighth note)
    // MIDI uses ticks per quarter note, so: whole-note-fraction * 4 * ticks_per_quarter
    return static_cast<long>(beats * 4.0 * ticks_per_quarter_);
}

inline double MIDIGenerator::ticksToBeats(long ticks) const {
    // Convert MIDI ticks back to whole-note fractions
    return static_cast<double>(ticks) / (4.0 * ticks_per_quarter_);
}

inline void MIDIGenerator::addError(const std::string& message) {
    errors_.push_back(message);
}

} // namespace ABCPlayer

#endif // MIDI_GENERATOR_H