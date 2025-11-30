#ifndef ABC_TYPES_H
#define ABC_TYPES_H

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <memory>

namespace ABCPlayer {

// Forward declarations
struct Note;
struct Rest;
struct VoiceChange;
struct Chord;
struct BarLine;
struct TimeSignature;
struct KeySignature;
struct Tempo;

// Feature types - based on abc2midi insights
enum class FeatureType {
    // Bar types
    SINGLE_BAR,
    DOUBLE_BAR,
    DOTTED_BAR,
    BAR_REP,
    REP_BAR,
    PLAY_ON_REP,
    REP1,
    REP2,
    BAR1,
    REP_BAR2,
    DOUBLE_REP,
    THICK_THIN,
    THIN_THICK,
    
    // Musical elements
    NOTE,
    REST,
    CHORD,
    TUPLE,
    TIE,
    SLUR_ON,
    SLUR_OFF,
    
    // Structure
    VOICE,
    PART,
    TEMPO,
    TIME,
    KEY,
    
    // Guitar chords (separate from multi-voice)
    GCHORD,
    GCHORDON,
    GCHORDOFF,
    
    // Text and formatting
    TEXT,
    TITLE,
    COMPOSER,
    
    // Grace notes and ornaments
    GRACEON,
    GRACEOFF,
    
    // Meta
    LINENUM,
    BLANKLINE
};

// Clef types
enum class ClefType {
    TREBLE,
    BASS,
    ALTO,
    TENOR,
    NONE
};

// Track types for MIDI generation
enum class TrackType {
    NOTES,          // Note track
    WORDS,          // Lyric track  
    NOTEWORDS,      // Combined notes+lyrics
    GCHORDS,        // Guitar chord track
    DRUMS           // Drum track
};

// Fraction for representing durations
struct Fraction {
    int num;
    int denom;
    
    Fraction(int n = 1, int d = 1) : num(n), denom(d) {}
    
    double toDouble() const {
        return static_cast<double>(num) / denom;
    }
    
    Fraction operator+(const Fraction& other) const {
        return Fraction(num * other.denom + other.num * denom, 
                       denom * other.denom);
    }
    
    Fraction operator*(const Fraction& other) const {
        return Fraction(num * other.num, denom * other.denom);
    }
    
    Fraction operator/(int divisor) const {
        return Fraction(num, denom * divisor);
    }
    
    void reduce() {
        int gcd_val = gcd(abs(num), abs(denom));
        num /= gcd_val;
        denom /= gcd_val;
        if (denom < 0) {
            num = -num;
            denom = -denom;
        }
    }
    
private:
    int gcd(int a, int b) {
        while (b != 0) {
            int temp = b;
            b = a % b;
            a = temp;
        }
        return a;
    }
};

// Key signature representation
struct KeySignature {
    std::string name;           // "C", "G", "Bb", "F#m", etc.
    int sharps;                 // +7 to -7 (sharps positive, flats negative)
    std::map<char, int> accidentals;  // Note -> accidental modification
    
    KeySignature(const std::string& n = "C") : name(n), sharps(0) {}
};

// Time signature representation  
struct TimeSignature {
    int numerator;
    int denominator;
    bool is_compound;           // 6/8, 9/8, 12/8, etc.
    bool is_cut_time;           // C| (alla breve)
    bool is_common_time;        // C
    
    TimeSignature(int num = 4, int denom = 4) 
        : numerator(num), denominator(denom), is_compound(false),
          is_cut_time(false), is_common_time(false) {}
};

// Voice context - maintains state for each voice
struct VoiceContext {
    int voice_number;
    std::string name;
    std::string short_name;
    ClefType clef;
    KeySignature key;
    TimeSignature timesig;
    Fraction unit_length;       // L: field value
    int transpose;              // Semitone transposition
    int octave_shift;           // Octave transposition
    int instrument;             // MIDI program number (0-127)
    int channel;                // MIDI channel (assigned during generation)
    int velocity;               // Default velocity
    bool muted;
    
    VoiceContext(int num = 1) 
        : voice_number(num), name(""), short_name(""), clef(ClefType::TREBLE),
          unit_length(1, 8), transpose(0), octave_shift(0), instrument(0),
          channel(-1), velocity(80), muted(false) {}
};

// Musical note representation
struct Note {
    char pitch;                 // A-G
    int octave;                 // Octave number
    char accidental;            // '#', 'b', '=', or 0 for natural
    Fraction duration;          // Note duration
    int midi_note;              // Calculated MIDI note number
    int velocity;               // Note velocity
    std::vector<std::string> decorations;  // Ornaments, articulations
    bool tied;                  // Part of a tie
    
    Note() : pitch('C'), octave(4), accidental(0), duration(1, 4), 
             midi_note(60), velocity(80), tied(false) {}
};

// Rest representation
struct Rest {
    Fraction duration;          // Rest duration
    bool multibar;              // Multi-measure rest
    int measures;               // Number of measures (if multibar)
    
    Rest() : duration(1, 4), multibar(false), measures(1) {}
};

// Voice change representation
struct VoiceChange {
    int voice_number;
    std::string voice_name;
    
    VoiceChange(int num = 1, const std::string& name = "") 
        : voice_number(num), voice_name(name) {}
};

// Chord representation (multi-voice chord, not guitar chord)
struct Chord {
    std::vector<Note> notes;    // Notes in the chord
    Fraction duration;          // Chord duration
    
    Chord() : duration(1, 4) {}
};

// Guitar chord representation (chord symbols like "C", "Am7")
struct GuitarChord {
    std::string symbol;         // "C", "Am7", "F#dim", etc.
    int root_note;             // MIDI note number for chord root
    std::string chord_type;    // "major", "minor", "7", "m7", etc.
    Fraction duration;         // Duration of the chord
    
    GuitarChord(const std::string& sym = "") : symbol(sym), root_note(60), chord_type("major"), duration(1, 4) {}
    
    // Explicit copy constructor
    GuitarChord(const GuitarChord& other) 
        : symbol(other.symbol), root_note(other.root_note), 
          chord_type(other.chord_type), duration(other.duration) {}
    
    // Explicit copy assignment
    GuitarChord& operator=(const GuitarChord& other) {
        if (this != &other) {
            symbol = other.symbol;
            root_note = other.root_note;
            chord_type = other.chord_type;
            duration = other.duration;
        }
        return *this;
    }
};

// Bar line representation
struct BarLine {
    FeatureType type;           // SINGLE_BAR, DOUBLE_BAR, etc.
    int repeat_count;           // For repeat bars
    
    BarLine(FeatureType t = FeatureType::SINGLE_BAR) 
        : type(t), repeat_count(1) {}
};

// Tempo representation
struct Tempo {
    Fraction note_value;        // Note value for tempo (1/4 = quarter note)
    int bpm;                    // Beats per minute
    std::string text;           // Optional tempo text
    
    Tempo() : note_value(1, 4), bpm(120) {}
    Tempo(const Fraction& nv, int b) : note_value(nv), bpm(b) {}
};

// Generic feature that can hold any ABC element
struct Feature {
    FeatureType type;
    int voice_id;               // Which voice this belongs to
    double timestamp;           // Time position in beats
    int line_number;            // Source line number (for error reporting)
    
    // Feature data - use variant to hold different types
    std::variant<
        Note,
        Rest, 
        VoiceChange,
        Chord,
        GuitarChord,
        BarLine,
        TimeSignature,
        KeySignature,
        Tempo,
        std::string                // For text, titles, etc.
    > data;
    
    Feature(FeatureType t = FeatureType::NOTE, int voice = 1) 
        : type(t), voice_id(voice), timestamp(0.0), line_number(0) {}
        
    // Template helpers for accessing data
    template<typename T>
    const T* get() const {
        return std::get_if<T>(&data);
    }
    
    template<typename T>
    T* get() {
        return std::get_if<T>(&data);
    }
    
    template<typename T>
    bool holds() const {
        return std::holds_alternative<T>(data);
    }
};

// Track descriptor for MIDI generation
struct TrackDescriptor {
    TrackType type;
    int voice_number;
    int channel;
    std::string name;
    
    TrackDescriptor(TrackType t = TrackType::NOTES, int voice = 1) 
        : type(t), voice_number(voice), channel(-1) {}
};

// Complete parsed ABC tune
struct ABCTune {
    int tune_number;            // X: field
    std::string title;          // T: field
    std::string composer;       // C: field
    std::string origin;         // O: field
    std::string parts;          // P: field
    
    // Global defaults
    KeySignature default_key;
    TimeSignature default_timesig;
    Fraction default_unit_length;
    ClefType default_clef;
    Tempo default_tempo;
    
    // Voice contexts
    std::map<int, VoiceContext> voices;
    
    // Parsed features
    std::vector<Feature> features;
    
    // Track descriptors for MIDI generation
    std::vector<TrackDescriptor> tracks;
    
    ABCTune() : tune_number(1), default_unit_length(1, 8),
                default_clef(ClefType::TREBLE) {}
};

} // namespace ABCPlayer

#endif // ABC_TYPES_H