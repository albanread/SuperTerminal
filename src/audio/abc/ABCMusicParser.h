#ifndef ABC_MUSIC_PARSER_H
#define ABC_MUSIC_PARSER_H

#include "ABCTypes.h"
#include <string>
#include <vector>
#include <functional>

namespace ABCPlayer {

// Forward declarations
class ABCVoiceManager;

class ABCMusicParser {
public:
    ABCMusicParser();
    ~ABCMusicParser();
    
    // Main music parsing interface
    bool parseMusicLine(const std::string& line, ABCTune& tune, ABCVoiceManager& voice_mgr);
    bool parseNoteSequence(const std::string& sequence, ABCTune& tune, ABCVoiceManager& voice_mgr);
    
    // Individual element parsers
    bool parseNote(const char*& p, Note& note, const ABCTune& tune, int voice_id);
    bool parseRest(const char*& p, Rest& rest, const ABCTune& tune, int voice_id);
    bool parseChord(const char*& p, Chord& chord, const ABCTune& tune, int voice_id);
    bool parseGuitarChord(const char*& p, GuitarChord& gchord);
    bool parseBarLine(const char*& p, BarLine& barline);
    bool parseInlineVoiceSwitch(const char*& p, ABCTune& tune, ABCVoiceManager& voice_mgr);
    
    // Duration parsing
    Fraction parseDuration(const char*& p, const Fraction& default_duration);
    
    // Note parsing helpers
    char parseNotePitch(const char*& p);
    char parseAccidental(const char*& p);
    int parseOctave(const char*& p);
    int calculateMidiNote(char pitch, char accidental, int octave, 
                         const KeySignature& key, int transpose);
    
    // Timing management
    double getCurrentTime() const { return current_time_; }
    void setCurrentTime(double time) { current_time_ = time; }
    void advanceTime(double duration) { current_time_ += duration; }
    
    // State management
    void reset();
    void setLineNumber(int line_num) { current_line_ = line_num; }
    
    // Chord state management
    bool isInChord() const { return in_chord_; }
    void setChordState(bool in_chord) { in_chord_ = in_chord; }
    
    // Grace note state
    bool isInGrace() const { return in_grace_; }
    void setGraceState(bool in_grace) { in_grace_ = in_grace; }
    
    // Error reporting callbacks
    void setErrorCallback(std::function<void(const std::string&)> error_cb) { 
        error_callback_ = error_cb; 
    }
    void setWarningCallback(std::function<void(const std::string&)> warning_cb) { 
        warning_callback_ = warning_cb; 
    }
    
private:
    double current_time_;
    int current_line_;
    bool in_chord_;
    bool in_grace_;
    
    // Error reporting
    std::function<void(const std::string&)> error_callback_;
    std::function<void(const std::string&)> warning_callback_;
    
    // Helper methods
    void addError(const std::string& message);
    void addWarning(const std::string& message);
    void skipWhitespace(const char*& p);
    bool isNote(char c);
    bool isAccidental(char c);
    bool isBarLine(char c);
    
    // Feature creation helpers
    void createNoteFeature(const Note& note, ABCTune& tune, int voice_id);
    void createRestFeature(const Rest& rest, ABCTune& tune, int voice_id);
    void createChordFeature(const Chord& chord, ABCTune& tune, int voice_id);
    void createBarFeature(const BarLine& barline, ABCTune& tune, int voice_id);
    void createGuitarChordFeature(const GuitarChord& gchord, ABCTune& tune, int voice_id);
    
    // Chord generation
    void generateChordNotes(const GuitarChord& gchord, ABCTune& tune, int voice_id, double chord_time, const Fraction& melody_duration);
    
    // Chord symbol parsing helpers
    std::vector<int> getChordIntervals(const std::string& chord_type);
    int parseChordRoot(const std::string& chord_symbol);
    std::string parseChordType(const std::string& chord_symbol);
    
    // MIDI note calculation helpers
    static const int note_semitones_[7];  // C D E F G A B semitone offsets
    int getNoteSemitone(char pitch);
    int applyKeySignature(int semitone, char pitch, const KeySignature& key);
};

// Inline helper implementations
inline void ABCMusicParser::skipWhitespace(const char*& p) {
    while (*p && (*p == ' ' || *p == '\t')) {
        ++p;
    }
}

inline bool ABCMusicParser::isNote(char c) {
    return (c >= 'A' && c <= 'G') || (c >= 'a' && c <= 'g');
}

inline bool ABCMusicParser::isAccidental(char c) {
    return c == '#' || c == 'b' || c == '=';
}

inline bool ABCMusicParser::isBarLine(char c) {
    return c == '|' || c == ':' || c == '[' || c == ']';
}

} // namespace ABCPlayer

#endif // ABC_MUSIC_PARSER_H