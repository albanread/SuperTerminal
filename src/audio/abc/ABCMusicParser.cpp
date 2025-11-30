#include "ABCMusicParser.h"
#include "ABCVoiceManager.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace ABCPlayer {

// Static member definitions
const int ABCMusicParser::note_semitones_[7] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G

ABCMusicParser::ABCMusicParser() 
    : current_time_(0.0)
    , current_line_(0)
    , in_chord_(false)
    , in_grace_(false) {
}

ABCMusicParser::~ABCMusicParser() = default;

bool ABCMusicParser::parseMusicLine(const std::string& line, ABCTune& tune, ABCVoiceManager& voice_mgr) {
    // Check for inline header fields first
    if (line.length() >= 2 && line[1] == ':') {
        // This might be an inline header field like K:, M:, etc.
        // For now, we'll ignore these in the music parser
        return true;
    }
    
    return parseNoteSequence(line, tune, voice_mgr);
}

bool ABCMusicParser::parseNoteSequence(const std::string& sequence, ABCTune& tune, ABCVoiceManager& voice_mgr) {
    const char* p = sequence.c_str();
    
    while (*p) {
        skipWhitespace(p);
        if (!*p) break;
        
        // Check for inline voice switch [V:name]
        if (*p == '[' && *(p+1) == 'V' && *(p+2) == ':') {
            if (parseInlineVoiceSwitch(p, tune, voice_mgr)) {
                continue;
            }
        }
        
        // Check for chord symbol "chord_name" - these should be tied to following note
        if (*p == '"') {
            GuitarChord gchord;
            const char* chord_start = p;
            if (parseGuitarChord(p, gchord)) {
                // Look ahead to find the note this chord is tied to
                const char* next_pos = p;
                skipWhitespace(next_pos);
                
                // Parse the following note to get its duration
                if (isNote(*next_pos)) {
                    Note melody_note;
                    const char* note_pos = next_pos;
                    
                    // Parse the note that follows the chord symbol
                    if (parseNote(note_pos, melody_note, tune, voice_mgr.getCurrentVoice())) {
                        // Get the note's duration
                        Fraction note_duration = parseDuration(note_pos, melody_note.duration);
                        
                        // Set the chord duration to match the melody note
                        gchord.duration = note_duration;
                        
                        // Generate chord backing with same duration as the melody note
                        generateChordNotes(gchord, tune, voice_mgr.getCurrentVoice(), current_time_, note_duration);
                    }
                } else {
                    // No following note, use default chord duration
                    Fraction default_duration(1, 2);
                    if (tune.voices.find(voice_mgr.getCurrentVoice()) != tune.voices.end()) {
                        default_duration = tune.voices[voice_mgr.getCurrentVoice()].unit_length;
                    }
                    gchord.duration = default_duration;
                    generateChordNotes(gchord, tune, voice_mgr.getCurrentVoice(), current_time_, default_duration);
                }
                continue;
            }
        }
        
        // Check for bar line
        if (isBarLine(*p)) {
            BarLine barline;
            if (parseBarLine(p, barline)) {
                createBarFeature(barline, tune, voice_mgr.getCurrentVoice());
                continue;
            }
        }
        
        // Check for inline voice switch [V:identifier] before treating as chord
        if (*p == '[' && p[1] == 'V' && p[2] == ':' && !in_chord_) {
            // Parse inline voice switch
            const char* voice_start = p + 3;
            const char* voice_end = voice_start;
            while (*voice_end && *voice_end != ']') voice_end++;
            
            if (*voice_end == ']') {
                // Skip leading whitespace
                while (voice_start < voice_end && (*voice_start == ' ' || *voice_start == '\t')) {
                    voice_start++;
                }
                // Skip trailing whitespace
                while (voice_end > voice_start && (*(voice_end - 1) == ' ' || *(voice_end - 1) == '\t')) {
                    voice_end--;
                }
                
                std::string voice_id(voice_start, voice_end - voice_start);
                
                // Convert numbered voices to voice_N format
                try {
                    int voice_num = std::stoi(voice_id);
                    voice_id = "voice_" + voice_id;
                } catch (...) {
                    // Not a number, keep as is
                }
                
                if (!voice_id.empty()) {
                    // Save current time for previous voice
                    voice_mgr.saveCurrentTime(current_time_);
                    
                    // Switch to new voice
                    int new_voice = voice_mgr.switchToVoice(voice_id, tune);
                    
                    // Restore time for new voice (will be 0.0 if first time)
                    current_time_ = voice_mgr.restoreVoiceTime(new_voice);
                }
                
                // Move past the [V:...] construct
                p = voice_end + 1;
                
                // Skip any whitespace after [V:...]
                while (*p && (*p == ' ' || *p == '\t')) p++;
                
                continue;
            }
        }
        
        // Check for chord (multiple notes in brackets)
        if (*p == '[' && !in_chord_) {
            Chord chord;
            if (parseChord(p, chord, tune, voice_mgr.getCurrentVoice())) {
                createChordFeature(chord, tune, voice_mgr.getCurrentVoice());
                advanceTime(chord.duration.toDouble());
            } else {
                addError("Failed to parse chord");
                // Skip malformed chord
                while (*p && *p != ']') p++;
                if (*p == ']') p++;
            }
            continue;
        }
        
        // Check for rest
        if (*p == 'z' || *p == 'Z') {
            Rest rest;
            if (parseRest(p, rest, tune, voice_mgr.getCurrentVoice())) {
                createRestFeature(rest, tune, voice_mgr.getCurrentVoice());
                advanceTime(rest.duration.toDouble());
                continue;
            }
        }
        
        // Check for note
        if (isNote(*p)) {
            Note note;
            int current_voice = voice_mgr.getCurrentVoice();
            
            // Parse note without duration
            note.accidental = parseAccidental(p);
            note.pitch = parseNotePitch(p);
            note.octave = parseOctave(p);
            
            // Set default duration
            if (tune.voices.find(current_voice) != tune.voices.end()) {
                note.duration = tune.voices[current_voice].unit_length;
            } else {
                note.duration = Fraction(1, 8); // Default
            }
            
            // Parse duration modifier if present
            if (std::isdigit(*p)) {
                int multiplier = 0;
                while (std::isdigit(*p)) {
                    multiplier = multiplier * 10 + (*p - '0');
                    p++;
                }
                note.duration = note.duration * multiplier;
            } else if (*p == '/') {
                p++; // Skip '/'
                int divisor = 2; // Default
                if (std::isdigit(*p)) {
                    divisor = 0;
                    while (std::isdigit(*p)) {
                        divisor = divisor * 10 + (*p - '0');
                        p++;
                    }
                }
                note.duration = note.duration / divisor;
            }
            
            // Calculate MIDI note
            if (tune.voices.find(current_voice) != tune.voices.end()) {
                const VoiceContext& voice = tune.voices[current_voice];
                note.midi_note = calculateMidiNote(note.pitch, note.accidental, note.octave, voice.key, voice.transpose);
                note.velocity = voice.velocity;
            } else {
                note.midi_note = 60; // Default middle C
                note.velocity = 80;
            }
            
            createNoteFeature(note, tune, current_voice);
            advanceTime(note.duration.toDouble());
            continue;
        }
        
        // If we get here, we have an unrecognized character
        addWarning("Ignoring unknown character: " + std::string(1, *p));
        p++;
    }
    
    return true;
}

bool ABCMusicParser::parseNote(const char*& p, Note& note, const ABCTune& tune, int voice_id) {
    if (!isNote(*p)) {
        return false;
    }
    
    // Parse accidental
    note.accidental = parseAccidental(p);
    
    // Parse note pitch
    note.pitch = parseNotePitch(p);
    if (note.pitch == 0) {
        return false;
    }
    
    // Parse octave
    note.octave = parseOctave(p);
    
    // Ensure voice exists
    if (tune.voices.find(voice_id) == tune.voices.end()) {
        addError("Voice context not found for note");
        return false;
    }
    
    const VoiceContext& voice = tune.voices.at(voice_id);
    
    // Duration is now parsed in parseNoteSequence
    note.duration = voice.unit_length;
    
    // Calculate MIDI note number
    note.midi_note = calculateMidiNote(note.pitch, note.accidental, note.octave,
                                      voice.key, voice.transpose);
    
    // Set velocity from voice
    note.velocity = voice.velocity;
    
    return true;
}

bool ABCMusicParser::parseRest(const char*& p, Rest& rest, const ABCTune& tune, int voice_id) {
    if (*p != 'z' && *p != 'Z') {
        return false;
    }
    
    p++; // Skip rest character
    
    // Ensure voice exists
    if (tune.voices.find(voice_id) == tune.voices.end()) {
        addError("Voice context not found for rest");
        return false;
    }
    
    const VoiceContext& voice = tune.voices.at(voice_id);
    
    // Parse duration
    rest.duration = parseDuration(p, voice.unit_length);
    
    return true;
}

bool ABCMusicParser::parseChord(const char*& p, Chord& chord, const ABCTune& tune, int voice_id) {
    if (*p != '[') {
        return false;
    }
    
    p++; // Skip opening bracket
    chord.notes.clear();
    
    while (*p && *p != ']') {
        skipWhitespace(p);
        if (*p == ']') break;
        
        Note note;
        if (parseNote(p, note, tune, voice_id)) {
            chord.notes.push_back(note);
        } else {
            // Skip unknown characters in chord
            if (*p && *p != ']') p++;
        }
        skipWhitespace(p);
    }
    
    if (*p == ']') {
        p++; // Skip closing bracket
        
        // Parse chord duration
        if (tune.voices.find(voice_id) != tune.voices.end()) {
            const VoiceContext& voice = tune.voices.at(voice_id);
            chord.duration = parseDuration(p, voice.unit_length);
        } else {
            chord.duration = Fraction(1, 4); // Default duration
        }
        
        return !chord.notes.empty();
    }
    
    return false;
}

bool ABCMusicParser::parseGuitarChord(const char*& p, GuitarChord& gchord) {
    if (*p != '"') {
        return false;
    }
    
    p++; // Skip opening quote
    std::string chord_name;
    
    while (*p && *p != '"') {
        chord_name += *p;
        p++;
    }
    
    if (*p == '"') {
        p++; // Skip closing quote
        gchord.symbol = chord_name;
        gchord.root_note = parseChordRoot(chord_name);
        gchord.chord_type = parseChordType(chord_name);
        return true;
    }
    
    return false;
}

bool ABCMusicParser::parseBarLine(const char*& p, BarLine& barline) {
    if (!isBarLine(*p)) {
        return false;
    }
    
    std::string bar_str;
    while (*p && isBarLine(*p)) {
        bar_str += *p;
        p++;
    }
    
    // Determine bar line type
    if (bar_str == "|") {
        barline.type = FeatureType::BAR1;
    } else if (bar_str == "||") {
        barline.type = FeatureType::DOUBLE_BAR;
    } else if (bar_str == "|:") {
        barline.type = FeatureType::REP_BAR;
    } else if (bar_str == ":|") {
        barline.type = FeatureType::BAR_REP;
    } else if (bar_str == ":|:") {
        barline.type = FeatureType::DOUBLE_REP;
    } else {
        barline.type = FeatureType::BAR1; // Default
    }
    
    return true;
}

bool ABCMusicParser::parseInlineVoiceSwitch(const char*& p, ABCTune& tune, ABCVoiceManager& voice_mgr) {
    if (*p != '[' || *(p+1) != 'V' || *(p+2) != ':') {
        return false;
    }
    
    p += 3; // Skip '[V:'
    
    // Extract voice identifier
    std::string voice_identifier;
    while (*p && *p != ']' && !std::isspace(*p)) {
        voice_identifier += *p;
        p++;
    }
    
    // Convert numbered voices to voice_N format
    try {
        int voice_num = std::stoi(voice_identifier);
        voice_identifier = "voice_" + voice_identifier;
    } catch (...) {
        // Not a number, keep as is
    }
    
    // Skip to closing ]
    while (*p && *p != ']') {
        p++;
    }
    
    if (*p == ']') {
        p++; // Consume the closing ]
    } else {
        addError("Unclosed voice switch bracket");
        return false;
    }
    
    if (voice_identifier.empty()) {
        addError("Empty voice identifier");
        return false;
    }
    
    // Save current time for the previous voice
    voice_mgr.saveCurrentTime(current_time_);
    
    // Switch to the voice
    int voice_id = voice_mgr.switchToVoice(voice_identifier, tune);
    
    if (voice_id == 0) {
        addError("Failed to switch to voice: " + voice_identifier);
        return false;
    }
    
    // Restore the time position for this voice
    current_time_ = voice_mgr.restoreVoiceTime(voice_id);
    
    // Create voice change feature
    VoiceChange voice_change;
    voice_change.voice_number = voice_id;
    voice_change.voice_name = voice_identifier;
    
    Feature voice_feature(FeatureType::VOICE, voice_id);
    voice_feature.line_number = current_line_;
    voice_feature.timestamp = current_time_;
    voice_feature.data = voice_change;
    tune.features.push_back(voice_feature);
    
    return true;
}

Fraction ABCMusicParser::parseDuration(const char*& p, const Fraction& default_duration) {
    Fraction duration = default_duration;
    
    // Check for explicit duration
    if (std::isdigit(*p)) {
        int numerator = 0;
        while (std::isdigit(*p)) {
            numerator = numerator * 10 + (*p - '0');
            p++;
        }
        
        if (*p == '/') {
            p++; // Skip '/'
            int denominator = 2; // Default
            if (std::isdigit(*p)) {
                denominator = 0;
                while (std::isdigit(*p)) {
                    denominator = denominator * 10 + (*p - '0');
                    p++;
                }
            }
            duration = Fraction(numerator, denominator) * default_duration;
        } else {
            duration = Fraction(numerator, 1) * default_duration;
        }
    } else if (*p == '/') {
        p++; // Skip '/'
        int denominator = 2; // Default
        if (std::isdigit(*p)) {
            denominator = 0;
            while (std::isdigit(*p)) {
                denominator = denominator * 10 + (*p - '0');
                p++;
            }
        }
        duration = Fraction(default_duration.num, default_duration.denom * denominator);
    }
    
    // Handle dotted notes
    while (*p == '.') {
        duration = duration * Fraction(3, 2);
        p++;
    }
    
    return duration;
}

char ABCMusicParser::parseNotePitch(const char*& p) {
    if (!isNote(*p)) {
        return 0;
    }
    
    char pitch = *p;
    p++;
    return pitch;
}

char ABCMusicParser::parseAccidental(const char*& p) {
    if (isAccidental(*p)) {
        char acc = *p;
        p++;
        return acc;
    }
    return 0; // No accidental
}

int ABCMusicParser::parseOctave(const char*& p) {
    int octave = 0;
    
    while (*p == '\'') {
        octave++;
        p++;
    }
    
    while (*p == ',') {
        octave--;
        p++;
    }
    
    return octave;
}

int ABCMusicParser::calculateMidiNote(char pitch, char accidental, int octave, 
                                     const KeySignature& key, int transpose) {
    // Convert pitch to semitone (A=9, B=11, C=0, D=2, E=4, F=5, G=7)
    char normalized_pitch = std::toupper(pitch);
    if (normalized_pitch < 'A' || normalized_pitch > 'G') {
        return 60; // Default to middle C
    }
    
    int semitone = note_semitones_[normalized_pitch - 'A'];
    
    // Apply accidental
    if (accidental == '#') {
        semitone++;
    } else if (accidental == 'b') {
        semitone--;
    }
    // Natural (=) cancels key signature accidental - would need key signature lookup
    
    // Calculate base octave
    int base_octave = 4; // Middle octave
    if (std::islower(pitch)) {
        base_octave++; // Lowercase notes are one octave higher
    }
    
    // Apply octave modifiers
    base_octave += octave;
    
    // Calculate MIDI note (C4 = 60)
    int midi_note = base_octave * 12 + semitone;
    
    // Apply transpose
    midi_note += transpose;
    
    // Clamp to valid MIDI range
    midi_note = std::max(0, std::min(127, midi_note));
    
    return midi_note;
}

void ABCMusicParser::reset() {
    current_time_ = 0.0;
    current_line_ = 0;
    in_chord_ = false;
    in_grace_ = false;
}

void ABCMusicParser::createNoteFeature(const Note& note, ABCTune& tune, int voice_id) {
    Feature note_feature(FeatureType::NOTE, voice_id);
    note_feature.line_number = current_line_;
    note_feature.timestamp = current_time_;
    note_feature.data = note;
    tune.features.push_back(note_feature);
}

void ABCMusicParser::createRestFeature(const Rest& rest, ABCTune& tune, int voice_id) {
    Feature rest_feature(FeatureType::REST, voice_id);
    rest_feature.line_number = current_line_;
    rest_feature.timestamp = current_time_;
    rest_feature.data = rest;
    tune.features.push_back(rest_feature);
}

void ABCMusicParser::createChordFeature(const Chord& chord, ABCTune& tune, int voice_id) {
    Feature chord_feature(FeatureType::CHORD, voice_id);
    chord_feature.line_number = current_line_;
    chord_feature.timestamp = current_time_;
    chord_feature.data = chord;
    tune.features.push_back(chord_feature);
}

void ABCMusicParser::createBarFeature(const BarLine& barline, ABCTune& tune, int voice_id) {
    Feature bar_feature(barline.type, voice_id);
    bar_feature.line_number = current_line_;
    bar_feature.timestamp = current_time_;
    bar_feature.data = barline;
    tune.features.push_back(bar_feature);
}

void ABCMusicParser::createGuitarChordFeature(const GuitarChord& gchord, ABCTune& tune, int voice_id) {
    Feature gchord_feature(FeatureType::GCHORD, voice_id);
    gchord_feature.line_number = current_line_;
    gchord_feature.timestamp = current_time_;
    gchord_feature.data = gchord;
    tune.features.push_back(gchord_feature);
}

int ABCMusicParser::getNoteSemitone(char pitch) {
    char normalized = std::toupper(pitch);
    if (normalized < 'A' || normalized > 'G') {
        return 0; // Default to C
    }
    return note_semitones_[normalized - 'A'];
}

int ABCMusicParser::applyKeySignature(int semitone, char pitch, const KeySignature& key) {
    // This would apply key signature accidentals
    // For now, just return the semitone unchanged
    return semitone;
}

void ABCMusicParser::addError(const std::string& message) {
    if (error_callback_) {
        error_callback_(message);
    }
}

void ABCMusicParser::addWarning(const std::string& message) {
    if (warning_callback_) {
        warning_callback_(message);
    }
}

void ABCMusicParser::generateChordNotes(const GuitarChord& gchord, ABCTune& tune, int voice_id, double chord_time, const Fraction& melody_duration) {
    // Get chord intervals based on chord type
    std::vector<int> intervals = getChordIntervals(gchord.chord_type);
    
    // Create a chord voice (use voice_id + 100 to avoid conflicts with regular voices)
    int chord_voice = voice_id + 100;
    
    // Ensure chord voice exists in tune
    if (tune.voices.find(chord_voice) == tune.voices.end()) {
        VoiceContext chord_voice_ctx(chord_voice);
        chord_voice_ctx.name = "Chord_" + std::to_string(voice_id);
        chord_voice_ctx.instrument = 48; // Default: String ensemble for chords (can be overridden by V:101 instrument=)
        chord_voice_ctx.velocity = 50; // Softer than melody
        if (tune.voices.find(voice_id) != tune.voices.end()) {
            chord_voice_ctx.key = tune.voices[voice_id].key;
            chord_voice_ctx.timesig = tune.voices[voice_id].timesig;
            chord_voice_ctx.unit_length = tune.voices[voice_id].unit_length;
        }
        tune.voices[chord_voice] = chord_voice_ctx;
    } else {
        // Voice already exists (e.g., from V:101 instrument=52), use its settings
        // Don't override the instrument if it was explicitly set
    }
    
    // Use melody duration for chord duration (tied to melody timing)
    Fraction chord_duration = melody_duration;
    
    // Generate notes for each chord tone
    for (int interval : intervals) {
        Note chord_note;
        chord_note.midi_note = gchord.root_note + interval;
        chord_note.velocity = 50; // Softer than melody
        chord_note.duration = chord_duration;
        chord_note.pitch = 0; // Will be calculated from MIDI note
        chord_note.accidental = 0;
        chord_note.octave = 0;
        
        // Ensure MIDI note is in valid range (bass register for chords)
        while (chord_note.midi_note < 36) chord_note.midi_note += 12; // Minimum low C
        while (chord_note.midi_note > 72) chord_note.midi_note -= 12; // Keep in bass/mid range
        
        // Create feature with current chord time
        Feature chord_feature(FeatureType::NOTE, chord_voice);
        chord_feature.line_number = current_line_;
        chord_feature.timestamp = chord_time;
        chord_feature.data = chord_note;
        tune.features.push_back(chord_feature);
    }
}

std::vector<int> ABCMusicParser::getChordIntervals(const std::string& chord_type) {
    // Return semitone intervals from root for different chord types
    if (chord_type == "major" || chord_type == "") {
        return {0, 4, 7}; // Root, major third, perfect fifth
    } else if (chord_type == "minor" || chord_type == "m") {
        return {0, 3, 7}; // Root, minor third, perfect fifth
    } else if (chord_type == "7" || chord_type == "dom7") {
        return {0, 4, 7, 10}; // Dominant 7th
    } else if (chord_type == "maj7" || chord_type == "M7") {
        return {0, 4, 7, 11}; // Major 7th
    } else if (chord_type == "m7") {
        return {0, 3, 7, 10}; // Minor 7th
    } else if (chord_type == "dim" || chord_type == "o") {
        return {0, 3, 6}; // Diminished
    } else if (chord_type == "aug" || chord_type == "+") {
        return {0, 4, 8}; // Augmented
    }
    
    // Default to major triad
    return {0, 4, 7};
}

int ABCMusicParser::parseChordRoot(const std::string& chord_symbol) {
    if (chord_symbol.empty()) return 60; // Default to C
    
    char root = std::toupper(chord_symbol[0]);
    int midi_note = 60; // Start at middle C
    
    // Convert note name to MIDI number (C=60, D=62, E=64, F=65, G=67, A=69, B=71)
    switch (root) {
        case 'C': midi_note = 60; break;
        case 'D': midi_note = 62; break;
        case 'E': midi_note = 64; break;
        case 'F': midi_note = 65; break;
        case 'G': midi_note = 67; break;
        case 'A': midi_note = 69; break;
        case 'B': midi_note = 71; break;
        default: midi_note = 60; break;
    }
    
    // Check for accidentals
    if (chord_symbol.length() > 1) {
        if (chord_symbol[1] == '#') {
            midi_note++;
        } else if (chord_symbol[1] == 'b') {
            midi_note--;
        }
    }
    
    // Transpose to bass octave (one octave below middle C)
    return midi_note - 12;
}

std::string ABCMusicParser::parseChordType(const std::string& chord_symbol) {
    // Find chord type after root note and accidental
    size_t type_start = 1;
    if (chord_symbol.length() > 1 && (chord_symbol[1] == '#' || chord_symbol[1] == 'b')) {
        type_start = 2;
    }
    
    if (type_start >= chord_symbol.length()) {
        return "major"; // No type specified = major
    }
    
    std::string type = chord_symbol.substr(type_start);
    
    // Normalize common chord type variations
    if (type == "m") return "minor";
    if (type == "min") return "minor";
    if (type == "7") return "dom7";
    if (type == "maj7") return "maj7";
    if (type == "M7") return "maj7";
    if (type == "m7") return "m7";
    if (type == "dim") return "dim";
    if (type == "o") return "dim";
    if (type == "aug") return "aug";
    if (type == "+") return "aug";
    
    return type.empty() ? "major" : type;
}

} // namespace ABCPlayer