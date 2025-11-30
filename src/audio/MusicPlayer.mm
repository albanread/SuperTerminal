//
//  MusicPlayer.mm
//  SuperTerminal - High-Level Music Player with ABC Notation
//
//  Created by Assistant on 2024-11-17.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "MusicPlayer.h"
#include "ABCPlayerClient.h"
#include "MidiEngine.h"
#include "AudioSystem.h"
#include "SuperTerminal.h"
#include "../GlobalShutdown.h"
#include <iostream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cmath>
#include <set>

// SuperTerminal C API declarations for background thread safety
extern "C" {
    void audio_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
    void audio_midi_note_off(uint8_t channel, uint8_t note);
    void audio_midi_program_change(uint8_t channel, uint8_t program);
    void audio_midi_control_change(uint8_t channel, uint8_t controller, uint8_t value);

    // Synth function declarations
    uint32_t synth_create_additive(float fundamental, const float* harmonics, int numHarmonics, float duration);
    uint32_t synth_create_fm(float carrierFreq, float modulatorFreq, float modIndex, float duration);
    uint32_t synth_create_granular(float baseFreq, float grainSize, float overlap, float duration);
    uint32_t synth_create_physical_string(float frequency, float damping, float brightness, float duration);
    uint32_t synth_create_physical_bar(float frequency, float damping, float brightness, float duration);
    uint32_t synth_create_physical_tube(float frequency, float airPressure, float brightness, float duration);
    uint32_t synth_create_physical_drum(float frequency, float damping, float excitation, float duration);

    // Audio playback function
    void audio_play_sound(uint32_t sound_id, float volume, float pitch, float pan);
}

// Include synth support
#include "SynthInstrumentMap.h"
#include "SynthEngine.h"
using namespace SuperTerminal;

// Utility function for MIDI note to frequency conversion
float midiNoteToFrequency(int midiNote) {
    return 440.0f * std::pow(2.0f, (midiNote - 69) / 12.0f);
}

// Key signature definitions
struct KeySignatureInfo {
    std::string name;
    std::vector<char> sharps;  // Note letters that should be sharp
    std::vector<char> flats;   // Note letters that should be flat
};

// Common key signatures
static const std::unordered_map<std::string, KeySignatureInfo> KEY_SIGNATURES = {
    {"C", {"C", {}, {}}},                           // C major - no accidentals
    {"G", {"G", {'F'}, {}}},                        // G major - F#
    {"D", {"D", {'F', 'C'}, {}}},                   // D major - F#, C#
    {"A", {"A", {'F', 'C', 'G'}, {}}},              // A major - F#, C#, G#
    {"E", {"E", {'F', 'C', 'G', 'D'}, {}}},         // E major - F#, C#, G#, D#
    {"B", {"B", {'F', 'C', 'G', 'D', 'A'}, {}}},    // B major - F#, C#, G#, D#, A#
    {"F#", {"F#", {'F', 'C', 'G', 'D', 'A', 'E'}, {}}}, // F# major - all sharps

    {"F", {"F", {}, {'B'}}},                        // F major - Bb
    {"Bb", {"Bb", {}, {'B', 'E'}}},                 // Bb major - Bb, Eb
    {"Eb", {"Eb", {}, {'B', 'E', 'A'}}},            // Eb major - Bb, Eb, Ab
    {"Ab", {"Ab", {}, {'B', 'E', 'A', 'D'}}},       // Ab major - Bb, Eb, Ab, Db
    {"Db", {"Db", {}, {'B', 'E', 'A', 'D', 'G'}}}, // Db major - Bb, Eb, Ab, Db, Gb
    {"Gb", {"Gb", {}, {'B', 'E', 'A', 'D', 'G', 'C'}}}, // Gb major - all flats

    // Minor keys (relative minors)
    {"Am", {"Am", {}, {}}},                         // A minor - no accidentals
    {"Em", {"Em", {'F'}, {}}},                      // E minor - F#
    {"Bm", {"Bm", {'F', 'C'}, {}}},                 // B minor - F#, C#
    {"F#m", {"F#m", {'F', 'C', 'G'}, {}}},          // F# minor - F#, C#, G#
    {"C#m", {"C#m", {'F', 'C', 'G', 'D'}, {}}},     // C# minor - F#, C#, G#, D#
    {"G#m", {"G#m", {'F', 'C', 'G', 'D', 'A'}, {}}}, // G# minor - F#, C#, G#, D#, A#

    {"Dm", {"Dm", {}, {'B'}}},                      // D minor - Bb
    {"Gm", {"Gm", {}, {'B', 'E'}}},                 // G minor - Bb, Eb
    {"Cm", {"Cm", {}, {'B', 'E', 'A'}}},            // C minor - Bb, Eb, Ab
    {"Fm", {"Fm", {}, {'B', 'E', 'A', 'D'}}},       // F minor - Bb, Eb, Ab, Db
    {"Bbm", {"Bbm", {}, {'B', 'E', 'A', 'D', 'G'}}}, // Bb minor - Bb, Eb, Ab, Db, Gb
};

// Helper function to apply key signature to a note
static int applyKeySignature(char noteLetter, const std::string& keySignature) {
    auto it = KEY_SIGNATURES.find(keySignature);
    if (it == KEY_SIGNATURES.end()) {
        return 0; // No change for unknown keys
    }

    const KeySignatureInfo& keyInfo = it->second;

    // Check if note should be sharp
    for (char sharpNote : keyInfo.sharps) {
        if (std::toupper(noteLetter) == sharpNote) {
            return 1; // Sharp (+1 semitone)
        }
    }

    // Check if note should be flat
    for (char flatNote : keyInfo.flats) {
        if (std::toupper(noteLetter) == flatNote) {
            return -1; // Flat (-1 semitone)
        }
    }

    return 0; // Natural
}

// Helper function to get clef transposition
static int getClefTransposition(const std::string& clef) {
    if (clef == "bass") {
        return -24; // Bass clef notes sound 2 octaves lower
    } else if (clef == "treble-8") {
        return -12; // Treble clef with 8 below sounds 1 octave lower
    } else if (clef == "treble+8") {
        return 12; // Treble clef with 8 above sounds 1 octave higher
    } else if (clef == "alto" || clef == "c") {
        return -12; // Alto clef sounds 1 octave lower than treble
    } else if (clef == "tenor") {
        return -12; // Tenor clef sounds 1 octave lower than treble
    }
    return 0; // Treble clef (default) - no transposition
}

// Utility function for unique sound IDs
uint64_t os_clock() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Detailed note structure already defined in header file

// Helper function to extract tempo from ABC notation
int extractABCTempo(const std::string& abcNotation) {
    // Safety check: limit search to first 2000 characters to prevent hangs
    size_t searchLimit = std::min(abcNotation.length(), (size_t)2000);
    std::string searchText = abcNotation.substr(0, searchLimit);

    // Look for Q: tempo field in ABC notation
    size_t pos = searchText.find("Q:");
    if (pos == std::string::npos) {
        return 120; // Default tempo
    }

    // Find the end of the line containing Q:
    size_t lineStart = pos;
    size_t lineEnd = searchText.find('\n', pos);
    if (lineEnd == std::string::npos) {
        lineEnd = searchText.length();
    }

    // Safety check: prevent extremely long lines
    if (lineEnd - lineStart > 100) {
        return 120; // Default if line too long
    }

    std::string tempoLine = searchText.substr(lineStart, lineEnd - lineStart);

    // Parse tempo value - handle Q:120 or Q:1/4=120 formats
    if (tempoLine.length() <= 2) {
        return 120; // Safety check for short lines
    }

    std::string tempoStr = tempoLine.substr(2); // Skip "Q:"

    // Remove any whitespace at start
    while (!tempoStr.empty() && std::isspace(tempoStr[0])) {
        tempoStr = tempoStr.substr(1);
    }

    if (tempoStr.empty()) {
        return 120;
    }

    size_t equalPos = tempoStr.find('=');

    if (equalPos != std::string::npos) {
        // Q:1/4=120 format
        std::string bpmStr = tempoStr.substr(equalPos + 1);
        try {
            int tempo = std::stoi(bpmStr);
            // Safety check: reasonable tempo range
            return (tempo >= 30 && tempo <= 300) ? tempo : 120;
        } catch (...) {
            return 120;
        }
    } else {
        // Q:120 format
        try {
            int tempo = std::stoi(tempoStr);
            // Safety check: reasonable tempo range
            return (tempo >= 30 && tempo <= 300) ? tempo : 120;
        } catch (...) {
            return 120;
        }
    }
}

// Helper function to generate ABC rest notation with specified duration in milliseconds
std::string generateABCRest(int milliseconds, int tempoBPM = 120) {
    if (milliseconds <= 0) return "";

    // Convert milliseconds to beats based on tempo
    // At 120 BPM: 1 beat (quarter note) = 500ms
    double beatsPerMs = (double)tempoBPM / (60.0 * 1000.0);
    double restDurationBeats = milliseconds * beatsPerMs;

    // ABC notation uses L:1/8 (eighth note) as default, so we need to convert beats to eighth notes
    // 1 beat (quarter note) = 2 eighth notes
    double eighthNotes = restDurationBeats * 2.0;

    // Debug logging removed for cleaner output

    // Convert to ABC notation duration (based on eighth notes with L:1/8)
    if (eighthNotes >= 8.0) {
        // Multiple whole note rests (8 eighth notes = 1 whole note)
        int wholeRests = (int)(eighthNotes / 8.0);
        double remainder = eighthNotes - (wholeRests * 8.0);
        std::string result = "";
        for (int i = 0; i < wholeRests; i++) {
            result += "z8 ";
        }
        if (remainder > 0.1) { // Add fractional rest if significant
            int remainingEighths = (int)(remainder + 0.5); // Round to nearest eighth
            if (remainingEighths > 0) {
                result += "z" + std::to_string(remainingEighths);
            }
        }
        return result;
    } else if (eighthNotes >= 1.0) {
        // Eighth note based rests
        int eighthRests = (int)(eighthNotes + 0.5); // Round to nearest eighth
        return "z" + std::to_string(eighthRests);
    } else {
        // Very short rest - use minimum eighth note
        return "z";
    }
}

// Helper function to append ABC rest to existing ABC notation
std::string appendABCRest(const std::string& abcNotation, int restMilliseconds, int tempoBPM = 120) {
    if (restMilliseconds <= 0) return abcNotation;

    // Extract the actual tempo from ABC notation, override parameter if found
    int actualTempo = extractABCTempo(abcNotation);
    if (actualTempo != 120) {
        tempoBPM = actualTempo;
    }

    std::string restNotation = generateABCRest(restMilliseconds, tempoBPM);
    if (restNotation.empty()) return abcNotation;

    // Simply append the rest to the end of the ABC notation
    // Add a space before the rest for readability
    std::string result = abcNotation + " " + restNotation;

    // Debug logging removed for cleaner output

    return result;
}

namespace SuperTerminal {

// Note name to MIDI number mapping (C4 = 60)
static const int NOTE_NAMES[] = {0, 2, 4, 5, 7, 9, 11}; // C D E F G A B
static const char* NOTE_LETTERS = "CDEFGAB";

// ABCParser Implementation
ABCParser::ABCParser() : lastError("") {
}

ABCParser::~ABCParser() {
}

int ABCParser::noteNameToMIDI(const std::string& noteName, int octave) {
    if (noteName.empty()) return 60; // Default to C4

    char note = std::toupper(noteName[0]);
    int noteIndex = -1;

    // Find note index
    for (int i = 0; i < 7; i++) {
        if (NOTE_LETTERS[i] == note) {
            noteIndex = i;
            break;
        }
    }

    if (noteIndex == -1) return 60;

    int midiNote = (octave + 1) * 12 + NOTE_NAMES[noteIndex];

    // Apply accidentals
    if (noteName.find('^') != std::string::npos) midiNote++; // Sharp
    if (noteName.find('_') != std::string::npos) midiNote--; // Flat

    return std::clamp(midiNote, 0, 127);
}

std::string ABCParser::midiToNoteName(int midiNote) {
    const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = (midiNote / 12) - 1;
    int noteIndex = midiNote % 12;
    return std::string(notes[noteIndex]) + std::to_string(octave);
}

double ABCParser::durationStringToDuration(const std::string& durStr, double defaultDur) {
    if (durStr.empty()) return defaultDur;

    if (durStr[0] == '/') {
        // Fractional duration like /2, /4
        int divisor = std::stoi(durStr.substr(1));
        return defaultDur / divisor;
    } else if (std::isdigit(durStr[0])) {
        // Multiplicative duration like 2, 4
        int multiplier = std::stoi(durStr);
        return defaultDur * multiplier;
    }

    return defaultDur;
}

// Convert ABC note length notation (L: field) to beats
double ABCParser::noteLengthToBeats(const std::string& lengthStr) {
    if (lengthStr.empty()) return 1.0; // Default L:1/4 = quarter note = 1 beat

    // Parse fraction format like "1/4", "1/8", "1/1"
    size_t slashPos = lengthStr.find('/');
    if (slashPos != std::string::npos) {
        try {
            int numerator = std::stoi(lengthStr.substr(0, slashPos));
            int denominator = std::stoi(lengthStr.substr(slashPos + 1));

            // Convert to beats where quarter note (1/4) = 1 beat
            // L:1/1 (whole note) = 4 beats
            // L:1/2 (half note) = 2 beats
            // L:1/4 (quarter note) = 1 beat
            // L:1/8 (eighth note) = 0.5 beats
            double beats = (double)numerator * 4.0 / (double)denominator;

            // Debug logging for L: field parsing
            char debug[256];
            snprintf(debug, sizeof(debug), "ABCParser: L:%s parsed as %.3f beats (was using 0.25)",
                     lengthStr.c_str(), beats);
            console(debug);

            return beats;
        } catch (...) {
            return 1.0; // Default fallback to quarter note
        }
    }

    // Handle single number like "4" (should be rare for L: field)
    try {
        int num = std::stoi(lengthStr);
        return (double)num; // Interpret as beats directly
    } catch (...) {
        return 1.0; // Default fallback to quarter note
    }
}

std::vector<ABCToken> ABCParser::tokenize(const std::string& abc) {
    std::vector<ABCToken> tokens;
    std::string current = "";

    for (size_t i = 0; i < abc.length(); i++) {
        char c = abc[i];

        if (std::isspace(c)) {
            if (!current.empty()) {
                tokens.emplace_back(identifyToken(current), current, i);
                current = "";
            }
            continue;
        }

        // Handle multi-character tokens that need lookahead
        if (c == '|') {
            if (!current.empty()) {
                tokens.emplace_back(identifyToken(current), current, i);
                current = "";
            }
            // Look ahead for repeat markers
            std::string barToken = "|";
            if (i + 1 < abc.length()) {
                if (abc[i + 1] == ':') {
                    barToken = "|:";
                    i++; // Skip the ':'
                    tokens.emplace_back(ABCTokenType::REPEAT_START, barToken, i - 1);
                } else if (abc[i + 1] == ']') {
                    barToken = "|]";
                    i++; // Skip the ']'
                    tokens.emplace_back(ABCTokenType::BAR, barToken, i - 1);
                } else if (abc[i + 1] == '|') {
                    barToken = "||";
                    i++; // Skip the second '|'
                    tokens.emplace_back(ABCTokenType::BAR, barToken, i - 1);
                } else {
                    tokens.emplace_back(ABCTokenType::BAR, barToken, i);
                }
            } else {
                tokens.emplace_back(ABCTokenType::BAR, barToken, i);
            }
            continue;
        }

        // Handle repeat end and double repeat
        if (c == ':') {
            if (!current.empty()) {
                tokens.emplace_back(identifyToken(current), current, i);
                current = "";
            }
            if (i + 1 < abc.length() && abc[i + 1] == '|') {
                tokens.emplace_back(ABCTokenType::REPEAT_END, ":|", i);
                i++; // Skip the '|'
            } else if (i + 1 < abc.length() && abc[i + 1] == ':') {
                tokens.emplace_back(ABCTokenType::REPEAT_END, "::", i);
                i++; // Skip the second ':'
            }
            continue;
        }

        // Handle chord brackets
        if (c == '[') {
            if (!current.empty()) {
                tokens.emplace_back(identifyToken(current), current, i);
                current = "";
            }
            // Check for endings like [1, [2, etc.
            if (i + 1 < abc.length() && std::isdigit(abc[i + 1])) {
                std::string ending = "[";
                ending += abc[i + 1];
                i++; // Skip the digit
                if (abc[i + 1] == '1') {
                    tokens.emplace_back(ABCTokenType::FIRST_ENDING, ending, i - 1);
                } else if (abc[i + 1] == '2') {
                    tokens.emplace_back(ABCTokenType::SECOND_ENDING, ending, i - 1);
                } else {
                    tokens.emplace_back(ABCTokenType::CHORD_START, "[", i - 1);
                    current = std::string(1, abc[i]); // Start new token with the digit
                }
            } else {
                // Handle chord notation - tokenize individual notes inside brackets
                tokens.emplace_back(ABCTokenType::CHORD_START, "[", i);

                // Find the closing bracket and extract chord contents
                size_t chordEnd = i + 1;
                while (chordEnd < abc.length() && abc[chordEnd] != ']') {
                    chordEnd++;
                }

                if (chordEnd < abc.length()) {
                    // Extract chord contents between brackets
                    std::string chordContents = abc.substr(i + 1, chordEnd - i - 1);

                    // Tokenize individual notes within the chord
                    for (size_t notePos = 0; notePos < chordContents.length(); notePos++) {
                        char noteChar = chordContents[notePos];

                        // Skip non-note characters within chords
                        if (!((noteChar >= 'A' && noteChar <= 'G') ||
                              (noteChar >= 'a' && noteChar <= 'g') ||
                              noteChar == '^' || noteChar == '_' || noteChar == '=' ||
                              noteChar == ',' || noteChar == '\'')) {
                            continue;
                        }

                        // Build individual note token (including accidentals and octave marks)
                        std::string noteToken = "";

                        // Handle accidentals
                        if (noteChar == '^' || noteChar == '_' || noteChar == '=') {
                            noteToken += noteChar;
                            notePos++;
                            if (notePos < chordContents.length()) {
                                noteToken += chordContents[notePos];
                            }
                        } else {
                            noteToken += noteChar;
                        }

                        // Handle octave marks after the note
                        while (notePos + 1 < chordContents.length() &&
                               (chordContents[notePos + 1] == '\'' || chordContents[notePos + 1] == ',')) {
                            notePos++;
                            noteToken += chordContents[notePos];
                        }

                        if (!noteToken.empty()) {
                            tokens.emplace_back(ABCTokenType::NOTE, noteToken, i + notePos);
                        }
                    }

                    // Add the closing bracket token
                    tokens.emplace_back(ABCTokenType::CHORD_END, "]", chordEnd);

                    // Skip ahead past the entire chord
                    i = chordEnd;
                }
            }
            continue;
        }

        if (c == ']') {
            if (!current.empty()) {
                tokens.emplace_back(identifyToken(current), current, i);
                current = "";
            }
            // Note: CHORD_END is now handled in the '[' section above for proper chord parsing
            // This handles any stray ']' characters that aren't part of chords
            tokens.emplace_back(ABCTokenType::CHORD_END, "]", i);
            continue;
        }

        // Handle grace notes
        if (c == '{') {
            if (!current.empty()) {
                tokens.emplace_back(identifyToken(current), current, i);
                current = "";
            }
            tokens.emplace_back(ABCTokenType::GRACE_START, "{", i);
            continue;
        }

        if (c == '}') {
            if (!current.empty()) {
                tokens.emplace_back(identifyToken(current), current, i);
                current = "";
            }
            tokens.emplace_back(ABCTokenType::GRACE_END, "}", i);
            continue;
        }

        // Handle decorations and other single characters
        if (c == '~' || c == 'H' || c == 'L' || c == 'M' || c == 'O' ||
            c == 'P' || c == 'S' || c == 'T' || c == 'u' || c == 'v' || c == '.') {
            if (!current.empty()) {
                tokens.emplace_back(identifyToken(current), current, i);
                current = "";
            }
            tokens.emplace_back(ABCTokenType::DECORATION, std::string(1, c), i);
            continue;
        }

        // Handle ties
        if (c == '-') {
            if (!current.empty()) {
                tokens.emplace_back(identifyToken(current), current, i);
                current = "";
            }
            tokens.emplace_back(ABCTokenType::TIE, "-", i);
            continue;
        }

        // Handle slurs - need to be careful not to confuse with tuplets
        if (c == '(') {
            if (!current.empty()) {
                tokens.emplace_back(identifyToken(current), current, i);
                current = "";
            }
            // Look ahead for tuplet numbers
            if (i + 1 < abc.length() && std::isdigit(abc[i + 1])) {
                std::string tuplet = "(";
                tuplet += abc[i + 1];
                i++; // Skip the digit
                tokens.emplace_back(ABCTokenType::TUPLET, tuplet, i - 1);
            } else {
                tokens.emplace_back(ABCTokenType::SLUR_START, "(", i);
            }
            continue;
        }

        if (c == ')') {
            if (!current.empty()) {
                tokens.emplace_back(identifyToken(current), current, i);
                current = "";
            }
            tokens.emplace_back(ABCTokenType::SLUR_END, ")", i);
            continue;
        }

        // Handle broken rhythm
        if (c == '<' || c == '>') {
            if (!current.empty()) {
                tokens.emplace_back(identifyToken(current), current, i);
                current = "";
            }
            std::string brokenRhythm = std::string(1, c);
            // Check for double/triple broken rhythm
            while (i + 1 < abc.length() && abc[i + 1] == c) {
                brokenRhythm += c;
                i++;
            }
            tokens.emplace_back(ABCTokenType::BROKEN_RHYTHM, brokenRhythm, i);
            continue;
        }

        // Handle extended decorations !symbol!
        if (c == '!') {
            if (!current.empty()) {
                tokens.emplace_back(identifyToken(current), current, i);
                current = "";
            }
            std::string decoration = "!";
            i++; // Skip opening !
            while (i < abc.length() && abc[i] != '!') {
                decoration += abc[i];
                i++;
            }
            if (i < abc.length()) {
                decoration += '!'; // Add closing !
                tokens.emplace_back(ABCTokenType::DECORATION_EXTENDED, decoration, i);
            }
            continue;
        }

        // Handle chord symbols in quotes
        if (c == '"') {
            if (!current.empty()) {
                tokens.emplace_back(identifyToken(current), current, i);
                current = "";
            }
            std::string chordSymbol = "\"";
            i++; // Skip opening quote
            while (i < abc.length() && abc[i] != '"') {
                chordSymbol += abc[i];
                i++;
            }
            if (i < abc.length()) {
                chordSymbol += '"'; // Add closing quote
                tokens.emplace_back(ABCTokenType::CHORD_SYMBOL, chordSymbol, i);
            }
            continue;
        }

        // Handle space as token separator
        if (std::isspace(c)) {
            if (!current.empty()) {
                // Check if this is a note with attached duration (like G3/2)
                if (current.length() > 1) {
                    char first = current[0];
                    if ((first >= 'A' && first <= 'G') || (first >= 'a' && first <= 'g')) {
                        // Find where duration starts
                        size_t durationStart = 1;
                        while (durationStart < current.length() &&
                               current[durationStart] != '/' &&
                               !std::isdigit(current[durationStart])) {
                            durationStart++;
                        }

                        if (durationStart < current.length()) {
                            // Split note and duration
                            std::string note = current.substr(0, durationStart);
                            std::string duration = current.substr(durationStart);
                            tokens.emplace_back(ABCTokenType::NOTE, note, i);
                            tokens.emplace_back(ABCTokenType::DURATION, duration, i);
                        } else {
                            // Just a note
                            tokens.emplace_back(identifyToken(current), current, i);
                        }
                    } else {
                        tokens.emplace_back(identifyToken(current), current, i);
                    }
                } else {
                    tokens.emplace_back(identifyToken(current), current, i);
                }
                current = "";
            }
            continue;
        }

        current += c;
    }

    if (!current.empty()) {
        // Check if this is a note with attached duration
        if (current.length() > 1) {
            char first = current[0];
            if ((first >= 'A' && first <= 'G') || (first >= 'a' && first <= 'g')) {
                // Find where the duration starts
                size_t durationStart = 1;
                while (durationStart < current.length() &&
                       !std::isdigit(current[durationStart]) &&
                       current[durationStart] != '/') {
                    durationStart++;
                }

                if (durationStart < current.length()) {
                    // Split note and duration
                    std::string note = current.substr(0, durationStart);
                    std::string duration = current.substr(durationStart);
                    tokens.emplace_back(ABCTokenType::NOTE, note);
                    tokens.emplace_back(ABCTokenType::DURATION, duration);
                } else {
                    tokens.emplace_back(identifyToken(current), current);
                }
            } else {
                tokens.emplace_back(identifyToken(current), current);
            }
        } else {
            tokens.emplace_back(identifyToken(current), current);
        }
    }

    // DEBUG: Log all tokens produced
    char debug[512];
    snprintf(debug, sizeof(debug), "ABCParser: tokenize produced %zu tokens from input: '%.100s'",
             tokens.size(), abc.c_str());
    console(debug);

    for (size_t i = 0; i < tokens.size() && i < 10; i++) {
        snprintf(debug, sizeof(debug), "Token %zu: type=%d value='%s'",
                 i, (int)tokens[i].type, tokens[i].value.c_str());
        console(debug);
    }

    return tokens;
}

ABCTokenType ABCParser::identifyToken(const std::string& token) {
    if (token.empty()) return ABCTokenType::UNKNOWN;

    char first = token[0];

    // ABC header fields
    if (token.find("T:") == 0) return ABCTokenType::TITLE;
    if (token.find("C:") == 0) return ABCTokenType::COMPOSER;
    if (token.find("O:") == 0) return ABCTokenType::ORIGIN;
    if (token.find("R:") == 0) return ABCTokenType::RHYTHM;
    if (token.find("Q:") == 0) return ABCTokenType::TEMPO;
    if (token.find("K:") == 0) return ABCTokenType::KEY;
    if (token.find("M:") == 0) return ABCTokenType::TIME_SIG;
    if (token.find("L:") == 0) return ABCTokenType::LENGTH;
    if (token.find("P:") == 0) return ABCTokenType::PARTS;
    if (token.find("w:") == 0) return ABCTokenType::WORDS_INLINE;
    if (token.find("W:") == 0) return ABCTokenType::WORDS_BLOCK;
    if (token.find("N:") == 0) return ABCTokenType::NOTES;

    // Bar lines and repeats
    if (token == "|" || token == "||" || token == "|]" || token == "[|") return ABCTokenType::BAR;
    if (token == "|:" || token == "::") return ABCTokenType::REPEAT_START;
    if (token == ":|") return ABCTokenType::REPEAT_END;

    // Endings
    if (token.find("[1") == 0) return ABCTokenType::FIRST_ENDING;
    if (token.find("[2") == 0) return ABCTokenType::SECOND_ENDING;

    // Chord delimiters
    if (first == '[') return ABCTokenType::CHORD_START;
    if (first == ']') return ABCTokenType::CHORD_END;

    // Grace notes
    if (first == '{') return ABCTokenType::GRACE_START;
    if (first == '}') return ABCTokenType::GRACE_END;

    // Rests
    if (first == 'z' || first == 'x') return ABCTokenType::REST;
    if (first == 'Z' || first == 'X') return ABCTokenType::MULTI_MEASURE_REST;

    // Accidentals with notes (e.g., ^C, _E, =F) - these should be treated as NOTE tokens
    if (first == '^' || first == '_' || first == '=') {
        // Check if this is an accidental followed by a note letter
        size_t notePos = 0;

        // Handle double accidentals (^^, __)
        if (token.length() > 1 && token[1] == first) {
            notePos = 2; // Skip both accidental characters
        } else {
            notePos = 1; // Skip single accidental character
        }

        // Check if we have a valid note letter after the accidental(s)
        if (notePos < token.length()) {
            char noteLetter = token[notePos];
            if ((noteLetter >= 'A' && noteLetter <= 'G') || (noteLetter >= 'a' && noteLetter <= 'g')) {
                return ABCTokenType::NOTE; // This is an accidental + note, treat as NOTE
            }
        }

        // If no note letter follows, treat as standalone accidental modifier
        if (first == '^') {
            if (token.length() > 1 && token[1] == '^') return ABCTokenType::ACCIDENTAL_MODIFIER;
            return ABCTokenType::SHARP;
        }
        if (first == '_') {
            if (token.length() > 1 && token[1] == '_') return ABCTokenType::ACCIDENTAL_MODIFIER;
            return ABCTokenType::FLAT;
        }
        if (first == '=') return ABCTokenType::NATURAL;
    }

    // Octave modifiers
    if (first == '\'') return ABCTokenType::OCTAVE_UP;
    if (first == ',') return ABCTokenType::OCTAVE_DOWN;

    // Extended decorations - !symbol! syntax
    if (first == '!' && token.length() > 2 && token.back() == '!') {
        return ABCTokenType::DECORATION_EXTENDED;
    }

    // Shorthand decorations
    if (first == '~' || first == 'H' || first == 'L' || first == 'M' ||
        first == 'O' || first == 'P' || first == 'S' || first == 'T' ||
        first == 'u' || first == 'v' || first == '.') {
        return ABCTokenType::DECORATION;
    }

    // Ties and slurs
    if (first == '-') return ABCTokenType::TIE;
    if (first == '(') return ABCTokenType::SLUR_START;
    if (first == ')') return ABCTokenType::SLUR_END;

    // Advanced tuplets with (p:q:r syntax
    if (token.find("(") == 0 && token.length() > 1 && std::isdigit(token[1])) {
        // Check for advanced syntax with colons
        if (token.find(':') != std::string::npos) {
            return ABCTokenType::TUPLET_ADVANCED;
        }
        return ABCTokenType::TUPLET;
    }

    // Navigation symbols (D.S., D.C., Fine)
    if (token == "D.S." || token == "D.C." || token == "Fine" || token == "Coda") {
        return ABCTokenType::NAVIGATION;
    }

    // Broken rhythm
    if (first == '<' || first == '>') return ABCTokenType::BROKEN_RHYTHM;

    // Chord symbols
    if (first == '"') return ABCTokenType::CHORD_SYMBOL;

    // Annotations
    if (first == '^' || first == '_' || first == '<' || first == '>' || first == '@') {
        // Only if followed by quote or text
        if (token.length() > 1) return ABCTokenType::ANNOTATION;
    }

    // Notes (A-G, a-g) - check for valid note names
    if ((first >= 'A' && first <= 'G') || (first >= 'a' && first <= 'g')) {
        return ABCTokenType::NOTE;
    }

    // Duration markers
    if (std::isdigit(first) || first == '/') {
        return ABCTokenType::DURATION;
    }

    return ABCTokenType::UNKNOWN;
}

bool ABCParser::parse(const std::string& abcNotation, ST_MusicSequence& sequence) {
    lastError = "";

    // Debug logging removed for cleaner output

    if (abcNotation.empty()) {
        lastError = "Empty ABC notation";
        return false;
    }

    // Preprocess ABC to handle line continuations and clean up
    std::string processedABC = preprocessABC(abcNotation);

    // Split into lines for better info field handling
    std::vector<std::string> lines = splitIntoLines(processedABC);

    // Check for multi-voice ABC by looking for V: fields or [V:] markers
    bool hasMultiVoice = false;
    for (const std::string& line : lines) {
        if (line.find("V:") == 0 || line.find("[V:") != std::string::npos) {
            hasMultiVoice = true;
            break;
        }
    }

    if (hasMultiVoice) {
        console("ABCParser::parse: Detected multi-voice ABC, using multi-voice parser");
        return parseMultiVoiceABC(lines, sequence);
    }

    // Single-voice parsing (original logic)
    // Debug logging removed for cleaner output

    // Initialize default voice for single-voice music
    currentVoiceId = "default";
    currentVoice = VoiceInfo("default", 0, 0);

    // Parse information fields first
    if (!parseInfoFields(lines, sequence)) return false;

    // Tokenize the musical content (non-info field lines)
    std::vector<ABCToken> tokens = tokenize(processedABC);

    if (tokens.empty()) {
        // If no tokens but we have info fields, that's okay (header-only tune)
        console("ABCParser::parse: No music tokens found, but info fields parsed");
        return true;
    }

    // Parse header and body
    if (!parseHeader(tokens, sequence)) return false;
    if (!parseBody(tokens, sequence)) return false;

    // Debug logging removed for cleaner output
    return true;
}

bool ABCParser::parseHeader(const std::vector<ABCToken>& tokens, ST_MusicSequence& sequence) {
    for (const auto& token : tokens) {
        switch (token.type) {
            case ABCTokenType::TEMPO:
                if (token.value.length() > 2) {
                    std::string tempoStr = token.value.substr(2);
                    // Handle various tempo formats: Q:120, Q:1/4=120, Q:"Andante"
                    size_t equalPos = tempoStr.find('=');
                    if (equalPos != std::string::npos) {
                        std::string bpmStr = tempoStr.substr(equalPos + 1);
                        try {
                            sequence.tempoBPM = std::stoi(bpmStr);
                        } catch (...) {
                            sequence.tempoBPM = 120; // Default
                        }
                    } else {
                        try {
                            sequence.tempoBPM = std::stoi(tempoStr);
                        } catch (...) {
                            sequence.tempoBPM = 120; // Default
                        }
                    }
                }
                break;

            case ABCTokenType::KEY:
                if (token.value.length() > 2) {
                    sequence.key = token.value.substr(2);
                    // Trim whitespace
                    sequence.key.erase(0, sequence.key.find_first_not_of(" \t"));
                    sequence.key.erase(sequence.key.find_last_not_of(" \t") + 1);
                }
                break;

            case ABCTokenType::TIME_SIG:
                if (token.value.length() > 2) {
                    std::string timeSig = token.value.substr(2);
                    timeSig.erase(0, timeSig.find_first_not_of(" \t"));

                    // Handle special cases
                    if (timeSig == "C") {
                        sequence.timeSignatureNum = 4;
                        sequence.timeSignatureDen = 4;
                        calculateCompoundTimeProperties(sequence);
                    } else if (timeSig == "C|") {
                        sequence.timeSignatureNum = 2;
                        sequence.timeSignatureDen = 2;
                        calculateCompoundTimeProperties(sequence);
                    } else {
                        size_t slashPos = timeSig.find('/');
                        if (slashPos != std::string::npos) {
                            try {
                                sequence.timeSignatureNum = std::stoi(timeSig.substr(0, slashPos));
                                sequence.timeSignatureDen = std::stoi(timeSig.substr(slashPos + 1));

                                // Calculate compound time properties
                                calculateCompoundTimeProperties(sequence);
                            } catch (...) {
                                // Keep defaults
                            }
                        }
                    }
                }
                break;

            case ABCTokenType::LENGTH:
                if (token.value.length() > 2) {
                    std::string lengthStr = token.value.substr(2);
                    lengthStr.erase(0, lengthStr.find_first_not_of(" \t"));
                    double duration = noteLengthToBeats(lengthStr);
                    sequence.defaultNoteDuration = duration;
                    sequence.unitNoteDuration = duration;  // Store original for broken rhythm reset

                    // If this is compound time, the duration calculation is already correct
                    // The compound time properties are calculated when M: is parsed
                }
                break;

            case ABCTokenType::TITLE:
                if (token.value.length() > 2) {
                    sequence.name = token.value.substr(2);
                    sequence.name.erase(0, sequence.name.find_first_not_of(" \t"));
                    sequence.name.erase(sequence.name.find_last_not_of(" \t") + 1);
                }
                break;

            case ABCTokenType::COMPOSER:
                if (token.value.length() > 2) {
                    sequence.composer = token.value.substr(2);
                    sequence.composer.erase(0, sequence.composer.find_first_not_of(" \t"));
                    sequence.composer.erase(sequence.composer.find_last_not_of(" \t") + 1);
                }
                break;

            case ABCTokenType::ORIGIN:
                if (token.value.length() > 2) {
                    sequence.origin = token.value.substr(2);
                    sequence.origin.erase(0, sequence.origin.find_first_not_of(" \t"));
                    sequence.origin.erase(sequence.origin.find_last_not_of(" \t") + 1);
                }
                break;

            case ABCTokenType::RHYTHM:
                if (token.value.length() > 2) {
                    sequence.rhythm = token.value.substr(2);
                    sequence.rhythm.erase(0, sequence.rhythm.find_first_not_of(" \t"));
                    sequence.rhythm.erase(sequence.rhythm.find_last_not_of(" \t") + 1);
                }
                break;

            case ABCTokenType::PARTS:
                if (token.value.length() > 2) {
                    sequence.parts = token.value.substr(2);
                    sequence.parts.erase(0, sequence.parts.find_first_not_of(" \t"));
                    sequence.parts.erase(sequence.parts.find_last_not_of(" \t") + 1);
                }
                break;

            case ABCTokenType::REPEAT_START:
            case ABCTokenType::REPEAT_END:
            case ABCTokenType::FIRST_ENDING:
            case ABCTokenType::SECOND_ENDING:
                sequence.hasRepeats = true;
                break;

            default:
                break;
        }
    }

    return true;
}

bool ABCParser::parseBody(const std::vector<ABCToken>& tokens, ST_MusicSequence& sequence) {
    double currentTime = 0.0;
    int currentOctave = 4;
    bool inChord = false;
    std::vector<MusicalNote> chordNotes;
    std::set<size_t> processedTokens; // Track processed tokens to prevent duplicates

    // DEBUG: Log parseBody entry
    char debug[256];
    snprintf(debug, sizeof(debug), "ABCParser: parseBody called with %zu tokens", tokens.size());
    console(debug);

    for (size_t i = 0; i < tokens.size(); i++) {
        const ABCToken& token = tokens[i];

        // Skip if already processed (prevent duplicates)
        if (processedTokens.find(i) != processedTokens.end()) {
            continue;
        }

        switch (token.type) {
            case ABCTokenType::NOTE: {
                // Mark token as processed
                processedTokens.insert(i);

                // DEBUG: Always log note processing
                char noteDebug[256];
                snprintf(noteDebug, sizeof(noteDebug), "ABCParser: Found NOTE token %zu: '%s'", i, token.value.c_str());
                console(noteDebug);

                // Parse note with all details (including embedded duration modifiers)
                DetailedNote detailedNote = parseDetailedNote(token.value, currentOctave, sequence.key,
                                                            currentTime, sequence.defaultNoteDuration);

                // Debug: Show final parsed duration
                char durationDebug[256];
                snprintf(durationDebug, sizeof(durationDebug), "ABCParser: Processing token %zu: '%s' -> MIDI %d at time %.3f",
                         i, token.value.c_str(), detailedNote.midiNote, currentTime);
                console(durationDebug);

                // Convert to MusicalNote with correct start time
                MusicalNote note(detailedNote.midiNote, detailedNote.velocity,
                               currentTime, detailedNote.duration, detailedNote.channel);

                if (inChord) {
                    chordNotes.push_back(note);
                } else {
                    sequence.notes.push_back(note);
                    currentTime += detailedNote.duration;

                    // DEBUG: Log note addition
                    char addDebug[256];
                    snprintf(addDebug, sizeof(addDebug), "ABCParser: Added note to sequence. Total notes: %zu", sequence.notes.size());
                    console(addDebug);
                }

                // Reset default note duration to original after applying broken rhythm
                if (sequence.defaultNoteDuration != sequence.unitNoteDuration) {
                    sequence.defaultNoteDuration = sequence.unitNoteDuration;
                }
                break;
            }

            case ABCTokenType::REST: {
                double restDuration = sequence.defaultNoteDuration;

                // Check for duration modifier
                if (i + 1 < tokens.size() && tokens[i + 1].type == ABCTokenType::DURATION) {
                    restDuration = parseDurationString(tokens[i + 1].value, sequence.defaultNoteDuration);
                    i++; // Skip duration token
                }

                currentTime += restDuration; // Just advance time for rests
                break;
            }

            case ABCTokenType::CHORD_START:
                inChord = true;
                chordNotes.clear();
                break;

            case ABCTokenType::DECORATION_EXTENDED: {
                // Handle extended decorations like !trill!, !fermata!, etc.
                DecorationInfo decoration;
                if (parseExtendedDecorations(token.value, decoration)) {
                    if (debugOutput) {
                        char debug[256];
                        snprintf(debug, sizeof(debug), "ABCParser: Extended decoration '%s' -> type:%s, variant:%s, intensity:%.2f",
                                token.value.c_str(), decoration.type.c_str(), decoration.variant.c_str(), decoration.intensity);
                        console(debug);
                    }

                    // Apply decoration to the next note
                    if (i + 1 < tokens.size() && tokens[i + 1].type == ABCTokenType::NOTE) {
                        // Store decoration to apply to next note
                        // For now, we'll handle this in the note parsing
                    }
                }
                break;
            }

            case ABCTokenType::TUPLET_ADVANCED: {
                // Handle advanced tuplet syntax (p:q:r
                TupletInfo tupletInfo;
                if (parseAdvancedTuplet(token.value, tupletInfo)) {
                    if (debugOutput) {
                        char debug[256];
                        snprintf(debug, sizeof(debug), "ABCParser: Advanced tuplet '%s' -> %d notes in time of %d, affecting %d",
                                token.value.c_str(), tupletInfo.notes, tupletInfo.inTimeOf, tupletInfo.affectNext);
                        console(debug);
                    }

                    // Apply tuplet timing to following notes
                    double tupletFactor = (double)tupletInfo.inTimeOf / tupletInfo.notes;
                    sequence.defaultNoteDuration *= tupletFactor;
                }
                break;
            }

            case ABCTokenType::CHORD_SYMBOL: {
                // Handle chord symbols like "G", "Am", "D7" etc.
                // Generate backing chord notes with proper duration
                std::string chordName = token.value;

                // Remove quotes from chord symbol
                if (chordName.front() == '"' && chordName.back() == '"') {
                    chordName = chordName.substr(1, chordName.length() - 2);
                }

                // Look ahead to find the duration of the next note(s) this chord accompanies
                double chordDuration = sequence.defaultNoteDuration;
                double nextNotesTime = 0.0;

                // Scan forward to calculate total duration this chord should cover
                for (size_t j = i + 1; j < tokens.size(); j++) {
                    if (tokens[j].type == ABCTokenType::NOTE) {
                        DetailedNote nextNote = parseDetailedNote(tokens[j].value, currentOctave, sequence.key, 0.0, sequence.defaultNoteDuration);
                        nextNotesTime += nextNote.duration;
                    } else if (tokens[j].type == ABCTokenType::CHORD_SYMBOL || tokens[j].type == ABCTokenType::BAR) {
                        break; // Stop at next chord or bar line
                    } else if (tokens[j].type == ABCTokenType::REST) {
                        nextNotesTime += sequence.defaultNoteDuration;
                    }
                }

                if (nextNotesTime > 0) {
                    chordDuration = nextNotesTime;
                }

                // Generate chord notes
                std::vector<int> chordNotes = parseChordSymbol(chordName);

                if (debugOutput) {
                    char debug[256];
                    snprintf(debug, sizeof(debug), "ABCParser: Chord '%s' -> duration %.3f beats, %zu notes",
                             chordName.c_str(), chordDuration, chordNotes.size());
                    console(debug);
                }

                // Add chord notes on a dedicated harmony channel (channel 14)
                // Use channel 14 to avoid conflict with percussion (usually channel 9/15)
                int harmonyChannel = 14;
                int harmonyInstrument = 1; // Acoustic Grand Piano for chord backing

                for (int midiNote : chordNotes) {
                    if (midiNote > 0) {
                        MusicalNote chordNote(midiNote, 50, currentTime, chordDuration, harmonyChannel);  // Soft velocity for backing
                        sequence.notes.push_back(chordNote);
                    }
                }

                // Set up harmony channel with appropriate instrument if not already done
                if (sequence.voices.find("Harmony") == sequence.voices.end()) {
                    VoiceInfo harmonyVoice;
                    harmonyVoice.id = "Harmony";
                    harmonyVoice.name = "Chord Backing";
                    harmonyVoice.shortName = "Chrd";
                    harmonyVoice.instrument = harmonyInstrument;
                    harmonyVoice.channel = harmonyChannel;
                    harmonyVoice.clef = "bass";
                    sequence.voices[harmonyVoice.id] = harmonyVoice;

                    if (debugOutput) {
                        console("ABCParser: Created harmony voice for chord backing");
                    }
                }
                break;
            }

            case ABCTokenType::BROKEN_RHYTHM: {
                // Handle broken rhythm like d>c (dotted eighth + sixteenth) or A<B (sixteenth + dotted eighth)
                if (i > 0 && i + 1 < tokens.size()) {
                    // Get the previous and next notes
                    if (tokens[i-1].type == ABCTokenType::NOTE && tokens[i+1].type == ABCTokenType::NOTE) {
                        // Modify the duration of the previous note (already parsed)
                        if (!sequence.notes.empty()) {
                            MusicalNote& prevNote = sequence.notes.back();

                            if (token.value == ">") {
                                // First note gets 1.5x duration, next note gets 0.5x duration
                                prevNote.duration = prevNote.duration * 1.5;
                                // Store the modified duration for the next note
                                sequence.defaultNoteDuration = sequence.defaultNoteDuration * 0.5;
                            } else if (token.value == "<") {
                                // First note gets 0.5x duration, next note gets 1.5x duration
                                prevNote.duration = prevNote.duration * 0.5;
                                sequence.defaultNoteDuration = sequence.defaultNoteDuration * 1.5;
                            }

                            if (debugOutput) {
                                char debug[256];
                                snprintf(debug, sizeof(debug), "ABCParser: Broken rhythm '%s' applied - prev note duration: %.3f",
                                         token.value.c_str(), prevNote.duration);
                                console(debug);
                            }
                        }
                    }
                }
                break;
            }

            case ABCTokenType::CHORD_END: {
                inChord = false;
                // Add all chord notes at the same time using different channels for simultaneity
                int baseChannel = currentVoice.channel;
                for (size_t i = 0; i < chordNotes.size(); ++i) {
                    auto& chordNote = chordNotes[i];
                    chordNote.startTime = currentTime;

                    // Assign different channels to each chord note for simultaneous playback
                    // Use channels 0-15, wrapping around if more than 16 notes in chord
                    // Avoid channel 9 (percussion) unless it's the only option
                    int chordChannel = (baseChannel + i) % 16;
                    if (chordChannel == 9 && chordNotes.size() > 1 && i < 15) {
                        chordChannel = (chordChannel + 1) % 16; // Skip percussion channel if possible
                    }
                    chordNote.channel = chordChannel;

                    // Debug output for chord channel assignment
                    if (debugOutput && i < 5) { // Limit debug output for large chords
                        char debug[256];
                        snprintf(debug, sizeof(debug), "ABCParser: Chord note %zu -> MIDI %d on channel %d (base=%d)",
                                i, chordNote.midiNote, chordChannel, baseChannel);
                        console(debug);
                    }

                    sequence.notes.push_back(chordNote);
                }
                if (!chordNotes.empty()) {
                    currentTime += chordNotes[0].duration;

                    // Log chord summary
                    if (debugOutput) {
                        char debug[256];
                        snprintf(debug, sizeof(debug), "ABCParser: Chord complete - %zu notes spread across channels %d-%d",
                                chordNotes.size(), baseChannel, (baseChannel + (int)chordNotes.size() - 1) % 16);
                        console(debug);
                    }
                }
                chordNotes.clear();
                break;
            }

            case ABCTokenType::OCTAVE_UP:
                currentOctave++;
                break;

            case ABCTokenType::OCTAVE_DOWN:
                currentOctave--;
                break;

            case ABCTokenType::BAR:
                // Bar lines don't affect timing in simple parsing
                // Reset default note duration after broken rhythm
                sequence.defaultNoteDuration = sequence.unitNoteDuration;
                break;

            default:
                break;
        }
    }

    return true;
}

// Comprehensive note parsing function
DetailedNote ABCParser::parseDetailedNote(const std::string& noteStr, int currentOctave, const std::string& key,
                                        double startTime, double defaultDuration) {
    if (noteStr.empty()) {
        return DetailedNote(60, 100, startTime, defaultDuration, "C4");
    }

    // DEBUG: Add detailed logging for note conversion
    char parseDebug[256];
    snprintf(parseDebug, sizeof(parseDebug), "ABCParser DEBUG: parseDetailedNote('%s', octave=%d, startTime=%.3f)",
             noteStr.c_str(), currentOctave, startTime);
    console(parseDebug);

    DetailedNote note;
    note.startTime = startTime;
    note.duration = defaultDuration;
    note.velocity = 100;
    note.channel = currentVoice.channel;  // Use current voice's channel instead of hardcoded 0
    note.isRest = false;

    // Extract duration modifier from note string (e.g., C4, C3/2, C/2, C/4)
    std::string cleanNote = noteStr;
    std::string durationStr = "";

    // Find trailing duration pattern using simple string parsing
    // Look for digits and/or slashes at the end of the note string
    size_t durationStart = std::string::npos;

    // Scan from end backwards to find where duration starts
    for (int i = (int)noteStr.length() - 1; i >= 0; i--) {
        char c = noteStr[i];
        if (std::isdigit(c) || c == '/') {
            durationStart = i;
        } else {
            // Found non-duration character, stop scanning
            break;
        }
    }

    if (durationStart != std::string::npos) {
        durationStr = noteStr.substr(durationStart);
        cleanNote = noteStr.substr(0, durationStart);

        // Debug logging
        if (debugOutput) {
            char debug[256];
            snprintf(debug, sizeof(debug), "ABCParser: Note '%s' -> base='%s' duration='%s'",
                     noteStr.c_str(), cleanNote.c_str(), durationStr.c_str());
            console(debug);
        }

        // Apply duration using the enhanced parser
        if (!durationStr.empty()) {
            note.duration = parseDurationString(durationStr, defaultDuration);
        }
    } else {
        // No duration modifier found, use default
        if (debugOutput) {
            char debug[256];
            snprintf(debug, sizeof(debug), "ABCParser: Note '%s' -> no duration modifier, using default %.3f beats",
                     noteStr.c_str(), defaultDuration);
            console(debug);
        }
    }

    // Parse accidentals
    if (noteStr.find('^') != std::string::npos) {
        note.hasAccidental = true;
        note.accidental = "#";
        cleanNote.erase(std::remove(cleanNote.begin(), cleanNote.end(), '^'), cleanNote.end());
    } else if (noteStr.find('_') != std::string::npos) {
        note.hasAccidental = true;
        note.accidental = "b";
        cleanNote.erase(std::remove(cleanNote.begin(), cleanNote.end(), '_'), cleanNote.end());
    } else if (noteStr.find('=') != std::string::npos) {
        note.hasAccidental = true;
        note.accidental = "♮";
        cleanNote.erase(std::remove(cleanNote.begin(), cleanNote.end(), '='), cleanNote.end());
    }

    // Determine octave
    note.octave = currentOctave;
    if (std::islower(cleanNote[0])) {
        note.octave++; // Lowercase = higher octave in ABC
    }

    // Count octave modifiers
    for (char c : cleanNote) {
        if (c == '\'') note.octave++;
        if (c == ',') note.octave--;
    }

    // Extract base note
    char noteLetter = std::toupper(cleanNote[0]);

    // DEBUG: Log octave and note calculation
    snprintf(parseDebug, sizeof(parseDebug), "  cleanNote='%s', noteLetter='%c', finalOctave=%d",
             cleanNote.c_str(), noteLetter, note.octave);
    console(parseDebug);

    // Convert to MIDI
    int semitone = 0;
    std::string baseNoteName;
    switch (noteLetter) {
        case 'C': semitone = 0; baseNoteName = "C"; break;
        case 'D': semitone = 2; baseNoteName = "D"; break;
        case 'E': semitone = 4; baseNoteName = "E"; break;
        case 'F': semitone = 5; baseNoteName = "F"; break;
        case 'G': semitone = 7; baseNoteName = "G"; break;
        case 'A': semitone = 9; baseNoteName = "A"; break;
        case 'B': semitone = 11; baseNoteName = "B"; break;
        default: semitone = 0; baseNoteName = "C"; break;
    }

    // Apply accidental
    if (note.accidental == "#") semitone++;
    else if (note.accidental == "b") semitone--;

    note.midiNote = (note.octave + 1) * 12 + semitone;

    // Apply voice transposition
    note.midiNote += currentVoice.transpose;

    // Apply clef transposition
    note.midiNote += getClefTransposition(currentVoice.clef);

    note.midiNote = std::max(0, std::min(127, note.midiNote));

    // Build note name
    note.noteName = baseNoteName + note.accidental + std::to_string(note.octave);

    // DEBUG: Log final conversion result
    snprintf(parseDebug, sizeof(parseDebug), "  RESULT: '%s' -> MIDI %d (%s), startTime=%.3f, duration=%.3f",
             noteStr.c_str(), note.midiNote, note.noteName.c_str(), note.startTime, note.duration);
    console(parseDebug);

    return note;
}

double ABCParser::parseDurationString(const std::string& durStr, double defaultDur) {
    if (durStr.empty()) return defaultDur;

    try {
        if (durStr[0] == '/') {
            // Handle multiple slashes: / = /2, // = /4, /// = /8, etc.
            if (durStr.length() > 1 && durStr[1] != '/') {
                // Single slash followed by number like /2, /4
                int divisor = std::stoi(durStr.substr(1));
                double result = defaultDur / divisor;

                if (debugOutput) {
                    char debug[256];
                    snprintf(debug, sizeof(debug), "ABCParser: Duration '%s' -> %.3f beats (%.3f / %d)",
                             durStr.c_str(), result, defaultDur, divisor);
                    console(debug);
                }

                return result;
            } else {
                // Multiple slashes: / = /2, // = /4, /// = /8, etc.
                int slashCount = 0;
                for (char c : durStr) {
                    if (c == '/') slashCount++;
                    else break;
                }
                int divisor = 1 << slashCount; // 2^slashCount (2, 4, 8, 16, ...)
                double result = defaultDur / divisor;

                if (debugOutput) {
                    char debug[256];
                    snprintf(debug, sizeof(debug), "ABCParser: Duration '%s' (%d slashes) -> %.3f beats (%.3f / %d)",
                             durStr.c_str(), slashCount, result, defaultDur, divisor);
                    console(debug);
                }

                return result;
            }
        } else if (std::isdigit(durStr[0])) {
            // Check for complex fractions like 3/2 (dotted notes)
            size_t slashPos = durStr.find('/');
            if (slashPos != std::string::npos) {
                int numerator = std::stoi(durStr.substr(0, slashPos));
                int denominator = std::stoi(durStr.substr(slashPos + 1));
                double result = defaultDur * (double)numerator / (double)denominator;

                if (debugOutput) {
                    char debug[256];
                    snprintf(debug, sizeof(debug), "ABCParser: Duration '%s' -> %.3f beats (%.3f * %d / %d)",
                             durStr.c_str(), result, defaultDur, numerator, denominator);
                    console(debug);
                }

                return result;
            } else {
                // Simple multiplicative duration like 2, 4
                int multiplier = std::stoi(durStr);
                double result = defaultDur * multiplier;

                if (debugOutput) {
                    char debug[256];
                    snprintf(debug, sizeof(debug), "ABCParser: Duration '%s' -> %.3f beats (%.3f * %d)",
                             durStr.c_str(), result, defaultDur, multiplier);
                    console(debug);
                }

                return result;
            }
        }
    } catch (const std::exception& e) {
        if (debugOutput) {
            char debug[256];
            snprintf(debug, sizeof(debug), "ABCParser: Duration parsing error for '%s', using default %.3f",
                     durStr.c_str(), defaultDur);
            console(debug);
        }
    }

    return defaultDur;
}

// Simple note conversion function for thread safety (kept for compatibility)
int ABCParser::convertNoteToMidi(const std::string& noteStr) {
    DetailedNote note = parseDetailedNote(noteStr, 4, "C", 0.0, 0.25);
    return note.midiNote;
}

int ABCParser::parseNote(const std::string& noteStr, int currentOctave, const std::string& key) {
    if (noteStr.empty()) return 60;

    int octave = currentOctave;
    std::string cleanNote = noteStr;

    // Handle octave modifiers
    if (std::islower(noteStr[0])) {
        octave++; // Lowercase = higher octave
    }

    // Count octave modifiers
    for (char c : noteStr) {
        if (c == '\'') octave++;
        if (c == ',') octave--;
    }

    // Extract just the note letter
    char noteLetter = std::toupper(noteStr[0]);
    cleanNote = std::string(1, noteLetter);

    // Handle explicit accidentals first (they override key signature)
    bool hasExplicitAccidental = false;
    if (noteStr.find('^') != std::string::npos) {
        cleanNote = "^" + cleanNote;
        hasExplicitAccidental = true;
    }
    if (noteStr.find('_') != std::string::npos) {
        cleanNote = "_" + cleanNote;
        hasExplicitAccidental = true;
    }
    if (noteStr.find('=') != std::string::npos) {
        cleanNote = "=" + cleanNote;
        hasExplicitAccidental = true;
    }

    // Apply key signature if no explicit accidental
    if (!hasExplicitAccidental && !key.empty()) {
        int keyAlteration = ::applyKeySignature(noteLetter, key);
        if (keyAlteration > 0) {
            cleanNote = "^" + cleanNote; // Sharp
        } else if (keyAlteration < 0) {
            cleanNote = "_" + cleanNote; // Flat
        }
    }

    int midiNote = noteNameToMIDI(cleanNote, octave);
    return std::clamp(midiNote, 0, 127);
}

double ABCParser::parseDuration(const std::string& durStr, double defaultDuration) {
    return durationStringToDuration(durStr, defaultDuration);
}

// ABCMusicPlayer Implementation
ST_MusicPlayer::ST_MusicPlayer() : initialized(false), midiEngine(nullptr), synthEngine(nullptr) {
}

ST_MusicPlayer::~ST_MusicPlayer() {
    shutdown();
}

bool ST_MusicPlayer::initialize(SuperTerminal::MidiEngine* engine, SynthEngine* synthEng) {
    if (initialized) return true;

    if (!engine) {
        return false;
    }

    midiEngine = engine;
    synthEngine = synthEng;  // Store synth engine (can be nullptr)

    // Initialize timing state
    isPlaying = false;
    isPaused = false;
    currentPlaybackTime = 0.0;
    nextNoteIndex = 0;
    activeNotes.clear();
    lastUpdateTime = std::chrono::steady_clock::now();

    // Start background music thread
    startMusicThread();

    // Register as active subsystem for shutdown coordination
    register_active_subsystem();

    initialized = true;
    return true;
}

void ST_MusicPlayer::shutdown() {
    if (!initialized) return;

    // Stop background thread first
    stopMusicThread();

    // MusicPlayer shutting down

    // Stop playback
    stopCurrentSequence();

    initialized = false;
    // MusicPlayer shutdown complete
}

bool ST_MusicPlayer::playMusic(const std::string& abcNotation, const std::string& name,
                          int tempoBPM, int instrument) {
    if (!initialized) {
        return false;
    }

    clearQueue();
    return queueMusic(abcNotation, name, tempoBPM, instrument, false);
}

uint32_t ST_MusicPlayer::queueMusic(const std::string& abcNotation, const std::string& name,
                                int tempoBPM, int instrument, bool loop) {
    // Call the version with rest parameter, using 0 for no automatic rest
    return queueMusic(abcNotation, name, tempoBPM, instrument, loop, 0);
}

uint32_t ST_MusicPlayer::queueMusic(const std::string& abcNotation, const std::string& name,
                                int tempoBPM, int instrument, bool loop, int waitAfterMs) {
    if (!initialized) {
        return 0;
    }

    // Generate unique slot ID
    uint32_t slotId = nextSlotId.fetch_add(1);

    // Automatically append ABC rest instead of using wait logic
    std::string abcWithRest = appendABCRest(abcNotation, waitAfterMs, tempoBPM);

    // Create music data and slot without wait time (rest is in ABC notation now)
    MusicData musicData(abcWithRest, name, tempoBPM, instrument, 0, loop);

    // Protect queue access - app thread adding while render thread may be reading
    {
        std::lock_guard<std::mutex> lock(queueMutex);

        // Add to slots map
        musicSlots.emplace(slotId, MusicSlot(slotId, musicData));

        // Add ID to playback queue
        playbackQueue.push(slotId);
    }

    return slotId;
}

bool ST_MusicPlayer::queueMusicLegacy(const std::string& abcNotation, const std::string& name,
                                  int tempoBPM, int instrument, bool loop) {
    uint32_t slotId = queueMusic(abcNotation, name, tempoBPM, instrument, loop);
    return slotId > 0;
}

void ST_MusicPlayer::stopMusic() {
    if (!initialized) return;

    clearQueue();
    stopCurrentSequence();

    {
        // Reset music state with proper lock ordering
        {
            std::lock_guard<std::mutex> musicLock(musicStateMutex);
            currentPlaybackTime = 0.0;
            nextNoteIndex = 0;
            activeNotes.clear();
            lastUpdateTime = std::chrono::steady_clock::now();
        }

        isPlaying = false;
        isPaused = false;
    }

    allNotesOff();
    logInfo("MusicPlayer: Music stopped");
}

void ST_MusicPlayer::pauseMusic() {
    if (!initialized || !isPlaying) return;

    {
        if (isPlaying && !isPaused) {
            isPaused = true;

            // Stop all currently playing notes with proper mutex ordering
            {
                std::lock_guard<std::mutex> musicLock(musicStateMutex);
                for (const auto& note : activeNotes) {
                    sendNoteOff(note.channel, note.midiNote);
                }
                activeNotes.clear();
            }

            logInfo("MusicPlayer: Music paused");
        }
    }
}

void ST_MusicPlayer::resumeMusic() {
    if (!initialized || !isPaused) return;

    {
        if (isPlaying && isPaused) {
            {
                std::lock_guard<std::mutex> musicLock(musicStateMutex);
                lastUpdateTime = std::chrono::steady_clock::now(); // Reset timing for smooth resume
            }

            isPaused = false;
            logInfo("MusicPlayer: Music resumed");
        }
    }
}

void ST_MusicPlayer::clearQueue() {
    if (!initialized) return;

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        musicSlots.clear();
        while (!playbackQueue.empty()) {
            playbackQueue.pop();
        }
    }

    logInfo("MusicPlayer: Music queue cleared");
}

int ST_MusicPlayer::getQueueSize() const {
    if (!initialized) return 0;

    std::lock_guard<std::mutex> lock(queueMutex);
    return static_cast<int>(playbackQueue.size());
}

std::string ST_MusicPlayer::getCurrentSongName() const {
    if (!initialized) return "";

    std::lock_guard<std::mutex> lock(stateMutex);
    return currentSongName;
}

void ST_MusicPlayer::update() {
    // Check for emergency shutdown first
    if (is_emergency_shutdown_requested()) {
        std::cout << "MusicPlayer: Emergency shutdown detected, signaling shutdown..." << std::endl;
        // Signal all threads to stop, but don't join from wrong thread
        musicThreadRunning = false;
        isPlaying = false;
        unregister_active_subsystem();
        return;
    }

    // Lightweight frame update - just check if we need to start next song
    if (!initialized) {
        return;
    }

    // Check if we need to start next song (non-blocking check)
    bool queueNotEmpty = false;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        queueNotEmpty = !playbackQueue.empty();
    }

    if (!isPlaying.load() && queueNotEmpty) {
        // Start next song immediately (pauses are now handled by ABC rests in notation)
        startNextSong();
    }
}

void ST_MusicPlayer::updateMusicTiming() {
    // Check for emergency shutdown before acquiring locks
    if (is_emergency_shutdown_requested()) {
        std::cout << "MusicPlayer: Emergency shutdown detected in timing update, signaling thread exit..." << std::endl;
        // Don't call shutdown() from within the thread - just signal exit
        musicThreadRunning = false;
        unregister_active_subsystem();
        return;
    }

    std::lock_guard<std::mutex> musicLock(musicStateMutex);

    if (!currentSequence || currentSequence->notes.empty()) {
        // No music to play - check for next song in queue
        bool hasQueuedSongs = false;
        {
            std::lock_guard<std::mutex> queueLock(queueMutex);
            hasQueuedSongs = !playbackQueue.empty();
        }

        if (hasQueuedSongs) {
            // Need to release lock before calling startNextSong to avoid deadlock
            return; // Will be picked up by next update cycle
        } else {
            isPlaying = false;
        }
        return;
    }

    // Calculate elapsed time since last update
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdateTime);
    lastUpdateTime = now;

    // Convert elapsed time to beats
    // Calculate beats per millisecond accounting for compound time
    double effectiveTempo = currentSequence->tempoBPM * masterTempo.load();
    double beatsPerMs;

    if (currentSequence->isCompoundTime) {
        // In compound time, tempo refers to dotted quarter note beats
        // e.g., in 6/8 time, there are 2 dotted quarter beats per measure
        beatsPerMs = (effectiveTempo * currentSequence->compoundBeatsPerMeasure) /
                     (currentSequence->timeSignatureNum * 60.0 * 1000.0);
    } else {
        // In simple time, use standard calculation
        beatsPerMs = effectiveTempo / (60.0 * 1000.0);
    }

    double elapsedBeats = elapsed.count() * beatsPerMs;
    currentPlaybackTime += elapsedBeats;

    // Debug logging removed for cleaner output

    // Stop notes that have reached their end time
    for (auto it = activeNotes.begin(); it != activeNotes.end();) {
        if (currentPlaybackTime >= it->endTime) {
            sendNoteOff(it->channel, it->midiNote);
            it = activeNotes.erase(it);
        } else {
            ++it;
        }
    }

    // Start all notes that should begin at or before current time
    while (nextNoteIndex < currentSequence->notes.size()) {
        const MusicalNote& note = currentSequence->notes[nextNoteIndex];

        if (note.startTime > currentPlaybackTime) {
            // Future note - stop processing for this update
            break;
        }

        // Play the note
        int adjustedVelocity = static_cast<int>(note.velocity * masterVolume.load());
        sendNoteOn(note.channel, note.midiNote, adjustedVelocity, &note);

        // Add to active notes with calculated end time
        double endTime = note.startTime + note.duration;
        activeNotes.emplace_back(note.midiNote, note.channel, endTime);

        nextNoteIndex++;
    }

    // Check if sequence is finished
    if (nextNoteIndex >= currentSequence->notes.size() && activeNotes.empty()) {
        // Always set isPlaying = false when sequence finishes
        // This allows update() to detect completion and start next queued track
        isPlaying = false;
    }
}

void ST_MusicPlayer::setMasterVolume(float volume) {
    masterVolume = std::clamp(volume, 0.0f, 1.0f);
}

void ST_MusicPlayer::setMasterTempo(float multiplier) {
    masterTempo = std::clamp(multiplier, 0.1f, 4.0f);
}

void ST_MusicPlayer::setDebugParserOutput(bool enabled) {
    debugParserOutput = enabled;
    parser.setDebugOutput(enabled);
}

double ST_MusicPlayer::getCurrentPosition() const {
    if (!currentSequence || !isPlaying) return 0.0;

    // Calculate position based on current playback time
    double position = currentPlaybackTime;

    if (currentSequence->isCompoundTime) {
        // In compound time, convert compound beats to seconds
        double compoundBeatsPerSecond = currentSequence->tempoBPM / 60.0;
        double noteBeatsPerCompoundBeat = (double)currentSequence->timeSignatureNum / currentSequence->compoundBeatsPerMeasure;
        return position / (compoundBeatsPerSecond * noteBeatsPerCompoundBeat);
    } else {
        return position * (60.0 / currentSequence->tempoBPM); // Convert beats to seconds
    }
}

double ST_MusicPlayer::getCurrentDuration() const {
    if (!currentSequence) return 0.0;

    // Calculate total duration of all notes
    double totalDuration = 0.0;
    for (const auto& note : currentSequence->notes) {
        totalDuration += note.duration;
    }

    return totalDuration * (60.0 / currentSequence->tempoBPM); // Convert beats to seconds
}

bool ST_MusicPlayer::startNextSong() {
    // Wait logic removed - pauses now handled by ABC rests in notation

    uint32_t nextSlotId = 0;
    MusicData nextMusic("", "", 120, 0, 0, false);

    console("STEP 3: Getting item from queue (with mutex protection)...");

    // Protect queue access - render thread reading while app thread may be adding
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (playbackQueue.empty()) {
            return false;
        }

        nextSlotId = playbackQueue.front();
        playbackQueue.pop();

        // Find the slot data
        auto slotIt = musicSlots.find(nextSlotId);
        if (slotIt == musicSlots.end()) {
            return false;
        }

        nextMusic = slotIt->second.data;
    }

    // Parse ABC notation
    auto sequence = std::make_unique<ST_MusicSequence>();

    if (!parser.parse(nextMusic.notation, *sequence)) {
        // FALLBACK: If parser fails, create a simple test sequence manually
        sequence->notes.clear();

        // Create a simple C major scale manually
        int notes[] = {60, 62, 64, 65, 67, 69, 71, 72}; // C D E F G A B C
        for (int i = 0; i < 8; i++) {
            MusicalNote note;
            note.midiNote = notes[i];
            note.velocity = 100;
            note.startTime = i * 0.5; // Half beat apart
            note.duration = 0.4;      // Slightly shorter than gap
            note.channel = 0;
            sequence->notes.push_back(note);
        }

        sequence->tempoBPM = 120;
        // Debug logging removed for cleaner output
    }
    // Debug logging removed for cleaner output

    // Set up instruments for multi-voice music
    if (sequence->isMultiVoice && !sequence->voices.empty()) {
        console("🎛️ ST_MusicPlayer::startNextSong: Setting up MULTI-VOICE instruments...");
        console("📊 CHANNEL ASSIGNMENT SUMMARY:");

        for (const auto& voicePair : sequence->voices) {
            const VoiceInfo& voice = voicePair.second;
            sendProgramChange(voice.channel, voice.instrument);

            char voiceDebug[256];
            snprintf(voiceDebug, sizeof(voiceDebug),
                    "🎹 PROGRAM CHANGE: Voice '%s' → Channel %d → Instrument %d (%s)",
                    voice.id.c_str(), voice.channel, voice.instrument,
                    voice.name.c_str());
            console(voiceDebug);
        }

        // Set up additional channels for chord support with the same instrument as the main voice
        // This ensures chord notes sound consistent
        int mainInstrument = sequence->voices.empty() ? nextMusic.instrument : sequence->voices.begin()->second.instrument;
        for (int ch = 0; ch < 16; ch++) {
            if (ch != 9) { // Skip percussion channel
                sendProgramChange(ch, mainInstrument);
            }
        }
        console("🎵 Multi-channel chord support: All channels initialized with consistent instrument");

        char debug[256];
        snprintf(debug, sizeof(debug), "✅ Multi-voice setup complete: %zu voices on separate channels",
                sequence->voices.size());
        console(debug);
    } else {
        // Single voice - set up instrument on all channels for chord support
        for (int ch = 0; ch < 16; ch++) {
            if (ch != 9) { // Skip percussion channel
                sendProgramChange(ch, nextMusic.instrument);
            }
        }
        console("🎵 Single-voice chord support: All channels initialized with same instrument");
    }

    // Override tempo if specified
    if (nextMusic.tempoBPM > 0) {
        sequence->tempoBPM = nextMusic.tempoBPM;
    }

    // Update music state with proper mutex
    {
        std::lock_guard<std::mutex> musicLock(musicStateMutex);

        // Stop all currently playing notes before starting new song
        for (const auto& note : activeNotes) {
            sendNoteOff(note.channel, note.midiNote);
        }
        activeNotes.clear();

        currentSequence = std::move(sequence);
        currentSongName = nextMusic.name;

        // Sort notes by start time for efficient time-based playback
        sortSequenceNotesByStartTime();

        // Reset playback state
        currentPlaybackTime = 0.0;
        nextNoteIndex = 0;
        lastUpdateTime = std::chrono::steady_clock::now();

        // Wait logic removed - pauses now handled by ABC rests in notation

    }

    // Start playback (atomic operations)
    isPlaying = true;
    isPaused = false;

    return true;
}

void ST_MusicPlayer::stopCurrentSequence() {
    // Stop all currently playing notes with proper mutex ordering
    {
        std::lock_guard<std::mutex> musicLock(musicStateMutex);
        for (const auto& note : activeNotes) {
            sendNoteOff(note.channel, note.midiNote);
        }
        activeNotes.clear();
        currentPlaybackTime = 0.0;
        nextNoteIndex = 0;
    }

    allNotesOff();

    isPlaying = false;
    isPaused = false;
}

std::chrono::milliseconds ST_MusicPlayer::beatsToMilliseconds(double beats, int tempoBPM) const {
    double beatsPerSecond;

    if (currentSequence && currentSequence->isCompoundTime) {
        // In compound time, adjust the beat calculation
        // The tempo marking refers to the compound beat (dotted quarter)
        double compoundBeatsPerSecond = tempoBPM / 60.0;
        // Convert from note beats to compound beats
        double noteBeatsPerCompoundBeat = (double)currentSequence->timeSignatureNum / currentSequence->compoundBeatsPerMeasure;
        beatsPerSecond = compoundBeatsPerSecond * noteBeatsPerCompoundBeat;
    } else {
        beatsPerSecond = tempoBPM / 60.0;
    }

    double seconds = beats / beatsPerSecond;
    return std::chrono::milliseconds(static_cast<long long>(seconds * 1000));
}

void ST_MusicPlayer::sendNoteOn(int channel, int note, int velocity, const MusicalNote* noteInfo) {
    if (debugMidiOutput) {
        char debug[128];
        snprintf(debug, sizeof(debug), "*** SENDING NOTE ON: ch=%d, note=%d, vel=%d ***", channel, note, velocity);
        console(debug);
    }

    // Process ornaments if noteInfo is available
    if (noteInfo && !noteInfo->decorations.empty()) {
        std::vector<MusicalNote> expandedNotes;
        processOrnaments(*noteInfo, expandedNotes);

        // Play the expanded ornament notes
        for (const auto& ornamentNote : expandedNotes) {
            int adjustedVel = static_cast<int>(ornamentNote.velocity * masterVolume.load());

            // Get program for this channel
            int program = (channelPrograms.find(ornamentNote.channel) != channelPrograms.end()) ?
                         channelPrograms[ornamentNote.channel] : 0;

            if (SynthInstrumentMap::isSynthInstrument(program)) {
                playSynthNote(ornamentNote.channel, ornamentNote.midiNote, adjustedVel, program, &ornamentNote);
            } else {
                if (midiEngine) {
                    midiEngine->playNote(static_cast<uint8_t>(ornamentNote.channel),
                                       static_cast<uint8_t>(ornamentNote.midiNote),
                                       static_cast<uint8_t>(adjustedVel));
                }
            }

            // Add timing delay for ornament sequence if needed
            if (ornamentNote.startTime > noteInfo->startTime) {
                // For real-time ornaments, we would schedule these with delays
                // For now, play them immediately in sequence
            }
        }
        return;
    }

    // Get program for this channel
    int program = (channelPrograms.find(channel) != channelPrograms.end()) ? channelPrograms[channel] : 0;

    if (SynthInstrumentMap::isSynthInstrument(program)) {
        // Route to synthesized instrument
        playSynthNote(channel, note, velocity, program, noteInfo);
    } else {
        // Route to MIDI engine
        if (midiEngine) {
            if (debugMidiOutput) {
                console("MusicPlayer: Calling midiEngine->playNote()...");
            }
            midiEngine->playNote(static_cast<uint8_t>(channel), static_cast<uint8_t>(note), static_cast<uint8_t>(velocity));
            if (debugMidiOutput) {
                console("MusicPlayer: midiEngine->playNote() CALLED");
            }
        } else {
            console("*** ERROR: midiEngine is NULL! ***");
        }
    }
}

void ST_MusicPlayer::sendNoteOff(int channel, int note) {
    if (debugMidiOutput) {
        char debug[128];
        snprintf(debug, sizeof(debug), "*** SENDING NOTE OFF: ch=%d, note=%d ***", channel, note);
        console(debug);
    }

    // Get program for this channel
    int program = (channelPrograms.find(channel) != channelPrograms.end()) ? channelPrograms[channel] : 0;

    if (SynthInstrumentMap::isSynthInstrument(program)) {
        // Route to synthesized instrument
        stopSynthNote(channel, note, program);
    } else {
        // Route to MIDI engine
        if (midiEngine) {
            if (debugMidiOutput) {
                console("MusicPlayer: Calling midiEngine->stopNote()...");
            }
            midiEngine->stopNote(static_cast<uint8_t>(channel), static_cast<uint8_t>(note));
            if (debugMidiOutput) {
                console("MusicPlayer: midiEngine->stopNote() CALLED");
            }
        } else {
            console("*** ERROR: midiEngine is NULL! ***");
        }
    }
}

void ST_MusicPlayer::sendProgramChange(int channel, int program) {
    // Store program for this channel
    channelPrograms[channel] = program;

    // Route to appropriate engine
    if (SynthInstrumentMap::isSynthInstrument(program)) {
        // Synthesized instrument - store mapping for later note routing
        char debug[256];
        snprintf(debug, sizeof(debug), "MusicPlayer: Channel %d assigned to synth instrument %d", channel, program);
        console(debug);
    } else {
        // Regular MIDI instrument
        if (midiEngine) {
            midiEngine->sendProgramChange(static_cast<uint8_t>(channel), static_cast<uint8_t>(program));
        }
    }
}

void ST_MusicPlayer::playSynthNote(int channel, int note, int velocity, int program, const MusicalNote* noteInfo) {
    if (!synthEngine) {
        console("*** ERROR: synthEngine is NULL! ***");
        return;
    }

    const SynthParams* params = SynthInstrumentMap::getInstance().getSynthParams(program);
    if (!params) {
        char debug[256];
        snprintf(debug, sizeof(debug), "*** ERROR: No synth params for instrument %d ***", program);
        console(debug);
        return;
    }

    float frequency = midiNoteToFrequency(note);
    float volume = velocity / 127.0f;

    // Calculate proper note duration from the actual ABC note
    float noteDuration = 0.5f; // Default fallback

    if (noteInfo) {
        // Use the actual parsed note duration from ABC notation
        float secondsPerBeat = currentSequence ? (60.0f / currentSequence->tempoBPM) : 0.5f;
        noteDuration = noteInfo->duration * secondsPerBeat;
    } else if (currentSequence) {
        // Fallback to default note duration
        float secondsPerBeat = 60.0f / currentSequence->tempoBPM;
        noteDuration = currentSequence->defaultNoteDuration * secondsPerBeat;
    }

    // Apply envelope timing - extend duration for sustained instruments
    if (params->type == SynthType::PHYSICAL_BAR || params->type == SynthType::PHYSICAL_STRING) {
        noteDuration *= 1.5f; // Bells and strings ring a bit longer
    } else if (params->type == SynthType::GRANULAR) {
        noteDuration *= 1.2f; // Granular pads sustain slightly longer
    }

    // Minimum duration to avoid clicks
    noteDuration = std::max(0.1f, noteDuration);
    // Maximum duration to avoid overlapping issues
    noteDuration = std::min(8.0f, noteDuration);

    char debug[256];
    if (noteInfo) {
        snprintf(debug, sizeof(debug), "MusicPlayer: SYNTH DEBUG - noteInfo->duration=%.3f beats, tempo=%d BPM, secondsPerBeat=%.3f, calculated=%.3fs",
                noteInfo->duration, currentSequence ? currentSequence->tempoBPM : 0,
                currentSequence ? (60.0f / currentSequence->tempoBPM) : 0.5f, noteDuration);
        console(debug);
    }
    snprintf(debug, sizeof(debug), "MusicPlayer: Playing synth note - freq=%.1f, vol=%.2f, dur=%.2fs, type=%s",
            frequency, volume, noteDuration, synthTypeToString(params->type).c_str());
    console(debug);

    // Create and play synthesized sound with proper duration

    uint32_t soundId = 0;
    switch (params->type) {
        case SynthType::ADDITIVE:
            soundId = synth_create_additive(frequency,
                                          params->harmonics.data(),
                                          static_cast<int>(params->harmonics.size()),
                                          noteDuration);
            break;
        case SynthType::FM:
            soundId = synth_create_fm(frequency * params->carrierRatio,
                                    frequency * params->modulatorRatio,
                                    params->modIndex, noteDuration);
            break;
        case SynthType::PHYSICAL_BAR:
            soundId = synth_create_physical_bar(frequency,
                                              params->damping, params->brightness, noteDuration);
            break;
        case SynthType::PHYSICAL_STRING:
            soundId = synth_create_physical_string(frequency,
                                                 params->damping, params->brightness, noteDuration);
            break;
        case SynthType::PHYSICAL_TUBE:
            soundId = synth_create_physical_tube(frequency,
                                               params->airPressure, params->brightness, noteDuration);
            break;
        case SynthType::PHYSICAL_DRUM:
            soundId = synth_create_physical_drum(frequency,
                                               params->damping, params->excitation, noteDuration);
            break;
        case SynthType::GRANULAR:
            soundId = synth_create_granular(frequency,
                                          params->grainSize, params->overlap, noteDuration);
            break;
    }

    // Play the synthesized sound immediately
    if (soundId > 0) {
        audio_play_sound(soundId, volume, 1.0f, 0.0f); // volume, pitch=1.0, pan=center
        char debug2[256];
        snprintf(debug2, sizeof(debug2), "MusicPlayer: Synth sound created (ID %d) and playing (%.2fs)", soundId, noteDuration);
        console(debug2);
    } else {
        console("MusicPlayer: Failed to create synthesized sound");
    }
}

void ST_MusicPlayer::stopSynthNote(int channel, int note, int program) {
    // For now, synthesized notes play their full duration
    // TODO: Implement note-off for real-time synthesis
    char debug[256];
    snprintf(debug, sizeof(debug), "MusicPlayer: Synth note off - ch=%d, note=%d (duration-based)", channel, note);
    console(debug);
}

void ST_MusicPlayer::startMusicThread() {
    musicThreadRunning = true;
    musicThread = std::thread(&ST_MusicPlayer::musicTimingThreadLoop, this);
}

void ST_MusicPlayer::stopMusicThread() {
    musicThreadRunning = false;
    if (musicThread.joinable()) {
        musicThread.join();
    }
}

void ST_MusicPlayer::musicTimingThreadLoop() {
    console("MusicPlayer: Background music thread started");

    while (musicThreadRunning.load()) {
        if (isPlaying.load() && !isPaused.load()) {
            updateMusicTiming();
        }

        // Sleep for 10ms (100Hz update rate - much more precise than 60fps)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    console("MusicPlayer: Background music thread stopped");
}

void ST_MusicPlayer::sortSequenceNotesByStartTime() {
    // This function is called within musicStateMutex, so no additional locking needed
    if (!currentSequence || currentSequence->notes.empty()) {
        return;
    }

    // Sort notes by start time to enable efficient time-based playback
    std::sort(currentSequence->notes.begin(), currentSequence->notes.end(),
              [](const MusicalNote& a, const MusicalNote& b) {
                  return a.startTime < b.startTime;
              });

    char debug[256];
    snprintf(debug, sizeof(debug), "MusicPlayer: Sorted %zu notes by start time for polyphonic playback",
             currentSequence->notes.size());
    console(debug);
}

void ST_MusicPlayer::allNotesOff() {
    // Use SuperTerminal C API for thread safety - turn off all notes on all channels
    for (int channel = 0; channel < 16; channel++) {
        for (int note = 0; note < 128; note++) {
            audio_midi_note_off(static_cast<uint8_t>(channel), static_cast<uint8_t>(note));
        }
    }
}

void ST_MusicPlayer::logInfo(const std::string& message) {
    // Use console for debugging in SuperTerminal context
    // std::cout << message << std::endl;
}

void ST_MusicPlayer::logError(const std::string& message) {
    // Use console for debugging in SuperTerminal context
    // std::cerr << "ERROR: " << message << std::endl;
}

// Enhanced ornament processing methods
void ST_MusicPlayer::processOrnaments(const MusicalNote& note, std::vector<MusicalNote>& expandedNotes) {
    bool hasOrnaments = false;

    for (const std::string& decoration : note.decorations) {
        if (decoration == "trill" || decoration == "~") {
            DecorationInfo decorInfo("trill", "standard");
            processTrill(note, decorInfo, expandedNotes);
            hasOrnaments = true;
        } else if (decoration == "uppermordent" || decoration == "M") {
            DecorationInfo decorInfo("mordent", "upper");
            processMordent(note, decorInfo, expandedNotes);
            hasOrnaments = true;
        } else if (decoration == "lowermordent" || decoration == "L") {
            DecorationInfo decorInfo("mordent", "lower");
            processMordent(note, decorInfo, expandedNotes);
            hasOrnaments = true;
        } else if (decoration == "turn" || decoration == "T") {
            DecorationInfo decorInfo("turn", "standard");
            processTurn(note, decorInfo, expandedNotes);
            hasOrnaments = true;
        }
    }

    // If no ornaments found, add the original note
    if (!hasOrnaments) {
        expandedNotes.push_back(note);
    }
}

void ST_MusicPlayer::processTrill(const MusicalNote& note, const DecorationInfo& decoration, std::vector<MusicalNote>& notes) {
    // Generate a rapid alternation between the note and its upper neighbor
    int upperNote = note.midiNote + 1; // Semitone above
    double trillDuration = note.duration / 8.0; // Divide into 8 quick notes

    for (int i = 0; i < 8; i++) {
        MusicalNote trillNote = note;
        trillNote.midiNote = (i % 2 == 0) ? note.midiNote : upperNote;
        trillNote.startTime = note.startTime + (i * trillDuration);
        trillNote.duration = trillDuration;
        trillNote.velocity = note.velocity * 0.9; // Slightly softer for ornament
        notes.push_back(trillNote);
    }

    if (debugMidiOutput) {
        console("MusicPlayer: Generated trill ornament with 8 alternating notes");
    }
}

void ST_MusicPlayer::processMordent(const MusicalNote& note, const DecorationInfo& decoration, std::vector<MusicalNote>& notes) {
    // Generate a quick neighbor tone and back
    int neighborNote = note.midiNote + (decoration.variant == "upper" ? 1 : -1);
    double mordentDuration = note.duration / 3.0;

    // First note: main note (short)
    MusicalNote mainNote1 = note;
    mainNote1.duration = mordentDuration;
    notes.push_back(mainNote1);

    // Second note: neighbor (very short)
    MusicalNote neighborNoteObj = note;
    neighborNoteObj.midiNote = neighborNote;
    neighborNoteObj.startTime = note.startTime + mordentDuration;
    neighborNoteObj.duration = mordentDuration * 0.5;
    neighborNoteObj.velocity = note.velocity * 0.8;
    notes.push_back(neighborNoteObj);

    // Third note: main note (remaining duration)
    MusicalNote mainNote2 = note;
    mainNote2.startTime = note.startTime + (mordentDuration * 1.5);
    mainNote2.duration = note.duration - (mordentDuration * 1.5);
    notes.push_back(mainNote2);

    if (debugMidiOutput) {
        console("MusicPlayer: Generated mordent ornament");
    }
}

void ST_MusicPlayer::processTurn(const MusicalNote& note, const DecorationInfo& decoration, std::vector<MusicalNote>& notes) {
    // Generate upper neighbor, main note, lower neighbor, main note
    int upperNote = note.midiNote + 1;
    int lowerNote = note.midiNote - 1;
    double turnDuration = note.duration / 4.0;

    // Upper neighbor
    MusicalNote upperNoteObj = note;
    upperNoteObj.midiNote = upperNote;
    upperNoteObj.duration = turnDuration;
    upperNoteObj.velocity = note.velocity * 0.8;
    notes.push_back(upperNoteObj);

    // Main note
    MusicalNote mainNote1 = note;
    mainNote1.startTime = note.startTime + turnDuration;
    mainNote1.duration = turnDuration;
    notes.push_back(mainNote1);

    // Lower neighbor
    MusicalNote lowerNoteObj = note;
    lowerNoteObj.midiNote = lowerNote;
    lowerNoteObj.startTime = note.startTime + (turnDuration * 2);
    lowerNoteObj.duration = turnDuration;
    lowerNoteObj.velocity = note.velocity * 0.8;
    notes.push_back(lowerNoteObj);

    // Final main note
    MusicalNote mainNote2 = note;
    mainNote2.startTime = note.startTime + (turnDuration * 3);
    mainNote2.duration = turnDuration;
    notes.push_back(mainNote2);

    if (debugMidiOutput) {
        console("MusicPlayer: Generated turn ornament");
    }
}

void ST_MusicPlayer::processDynamics(MusicalNote& note, const DecorationInfo& decoration) {
    if (decoration.type == "dynamic" && decoration.affectsVelocity) {
        // Apply dynamic intensity to velocity
        int newVelocity = static_cast<int>(note.velocity * decoration.intensity);
        note.velocity = std::max(1, std::min(127, newVelocity));

        if (debugMidiOutput) {
            char debug[256];
            snprintf(debug, sizeof(debug), "MusicPlayer: Applied dynamic %s, velocity: %d -> %d",
                    decoration.variant.c_str(), note.velocity, newVelocity);
            console(debug);
        }
    } else if (decoration.type == "accent" && decoration.affectsVelocity) {
        // Apply accent
        int newVelocity = static_cast<int>(note.velocity * decoration.intensity);
        note.velocity = std::max(1, std::min(127, newVelocity));
    } else if (decoration.type == "staccato" && decoration.affectsDuration) {
        // Shorten note duration
        note.duration *= decoration.intensity;
    } else if (decoration.type == "tenuto" && decoration.affectsDuration) {
        // Ensure full duration (no change needed as default is full)
    } else if (decoration.type == "fermata" && decoration.affectsDuration) {
        // Extend duration
        note.duration *= decoration.intensity;
    }
}

// Slot management methods
bool ST_MusicPlayer::removeMusicSlot(uint32_t slotId) {
    if (!initialized) return false;

    std::lock_guard<std::mutex> lock(queueMutex);

    // Remove from slots map
    auto it = musicSlots.find(slotId);
    if (it == musicSlots.end()) {
        return false; // Slot not found
    }

    musicSlots.erase(it);

    // Note: We don't remove from playbackQueue here since it's a queue
    // The slot will be skipped when its turn comes up in startNextSong()

    char debug[128];
    snprintf(debug, sizeof(debug), "MusicPlayer: Removed slot ID %u", slotId);
    console(debug);
    return true;
}

bool ST_MusicPlayer::hasMusicSlot(uint32_t slotId) const {
    if (!initialized) return false;

    std::lock_guard<std::mutex> lock(queueMutex);
    return musicSlots.find(slotId) != musicSlots.end();
}

MusicData ST_MusicPlayer::getMusicSlotData(uint32_t slotId) const {
    if (!initialized) return MusicData("", "", 120, 0, 0, false);

    std::lock_guard<std::mutex> lock(queueMutex);
    auto it = musicSlots.find(slotId);
    if (it != musicSlots.end()) {
        return it->second.data;
    }
    return MusicData("", "", 120, 0, 0, false); // Empty data if not found
}

std::vector<uint32_t> ST_MusicPlayer::getQueuedSlotIds() const {
    if (!initialized) return {};

    std::lock_guard<std::mutex> lock(queueMutex);
    std::vector<uint32_t> result;

    // Make a copy of the queue to iterate through it
    std::queue<uint32_t> queueCopy = playbackQueue;
    while (!queueCopy.empty()) {
        uint32_t slotId = queueCopy.front();
        queueCopy.pop();

        // Only include slots that still exist
        if (musicSlots.find(slotId) != musicSlots.end()) {
            result.push_back(slotId);
        }
    }

    return result;
}

// Enhanced test songs with new ABC features
std::string ST_MusicPlayer::getTestSong(const std::string& songName) {
    if (songName == "ornaments") {
        return R"(T:Enhanced Ornament Showcase
Q:120
M:4/4
L:1/8
K:D
!trill!A2 !turn!B2 | !uppermordent!c2 !fermata!d4 |
!staccato!e2 !staccato!f2 | !accent!g2 !tenuto!a2 |
!crescendo(!f4 !crescendo)!d4 |)";
    }

    if (songName == "jazz") {
        return R"(T:Jazz Progression Demo
Q:120
M:4/4
L:1/2
K:C
"Cmaj7"C2 "A7"A,2 |
"Dm7"D2 "G13"G,2 |
"Em7b5"E2 "A7alt"A,2 |
"Dm9"D2 "G7sus4"G,2 |)";
    }

    if (songName == "tuplets") {
        return R"(T:Advanced Tuplet Demo
Q:120
M:4/4
L:1/8
K:G
(3:2:3 GAB | (5:4:5 cdefg |
(3abc (3def | (7:4:7 GABCDEF |
G4 !fermata!G4 |)";
    }

    if (songName == "multivoice") {
        return R"(T:Multi-Voice Enhanced Demo
M:4/4
L:1/8
K:C
V:S name="Soprano" clef=treble
V:A name="Alto" clef=treble
V:T name="Tenor" clef=treble-8
V:B name="Bass" clef=bass
[V:S] !trill!e4 !turn!d4 | c4 !fermata!e4 |
[V:A] c4 B4 | A4 !fermata!c4 |
[V:T] G4 G4 | F4 !fermata!G4 |
[V:B] C4 G,4 | F,4 !fermata!C4 |)";
    }

    if (songName == "showcase") {
        return R"(T:Complete ABC v2.1 Showcase
Q:120
M:4/4
L:1/8
K:D
V:Solo name="Solo Line" clef=treble
V:Chords name="Chord Backing" clef=bass transpose=-12
[V:Solo] !f!A2 !trill!B2 | !crescendo(!c2 d2 e2 !crescendo)!f2 |
"Dmaj7"!turn!g4 | (5:4:5 !staccato!f !staccato!e !staccato!d !staccato!c !staccato!B |
!fermata!A4 z4 |
[V:Chords] "Dmaj7"D,2 A,,2 | "G/B"G,2 "A7"A,,2 |
"Dmaj7"D,4 | "Em7"E,2 "A7sus4"A,,2 |
!fermata!D,4 z4 |)";
    }

    // Default classic test songs
    if (songName == "scale") {
        return R"(T:C Major Scale
Q:120
M:4/4
L:1/4
K:C
C D E F | G A B c |)";
    }

    if (songName == "arpeggio") {
        return R"(T:C Major Arpeggio
Q:120
M:4/4
L:1/8
K:C
C E G c | e c G E | C2 z2 z4 |)";
    }

    if (songName == "twinkle") {
        return R"(T:Twinkle, Twinkle, Little Star
Q:120
M:4/4
L:1/4
K:C
C C G G | A A G2 | F F E E | D D C2 |)";
    }

    // Return default scale if unknown song
    return R"(T:C Major Scale
Q:120
M:4/4
L:1/4
K:C
C D E F | G A B c |)";
}

// Enhanced preprocessing and parsing utility methods
std::string ABCParser::preprocessABC(const std::string& abc) {
    std::string result;
    result.reserve(abc.length());

    for (size_t i = 0; i < abc.length(); ++i) {
        char c = abc[i];

        // Handle line continuations (backslash at end of line)
        if (c == '\\' && i + 1 < abc.length() &&
           (abc[i + 1] == '\n' || abc[i + 1] == '\r')) {
            // Skip backslash and following newline
            if (abc[i + 1] == '\r' && i + 2 < abc.length() && abc[i + 2] == '\n') {
                i += 2; // Skip \r\n
            } else {
                i += 1; // Skip \n
            }
            continue;
        }

        result += c;
    }

    return result;
}

std::vector<std::string> ABCParser::splitIntoLines(const std::string& abc) {
    std::vector<std::string> lines;
    std::string currentLine;

    for (char c : abc) {
        if (c == '\n' || c == '\r') {
            if (!currentLine.empty()) {
                lines.push_back(currentLine);
                currentLine.clear();
            }
        } else {
            currentLine += c;
        }
    }

    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }

    return lines;
}

bool ABCParser::parseInfoFields(const std::vector<std::string>& lines, ST_MusicSequence& sequence) {
    for (const std::string& line : lines) {
        if (shouldIgnoreLine(line)) continue;

        std::string cleanLine = stripComments(line);
        if (cleanLine.empty()) continue;

        if (isInfoField(cleanLine)) {
            std::string value = extractInfoFieldValue(cleanLine);
            char field = cleanLine[0];

            switch (field) {
                case 'T':
                    sequence.name = value;
                    break;
                case 'C':
                    sequence.composer = value;
                    break;
                case 'O':
                    sequence.origin = value;
                    break;
                case 'R':
                    sequence.rhythm = value;
                    break;
                case 'M':
                    // Handle meter
                    if (value == "C") {
                        sequence.timeSignatureNum = 4;
                        sequence.timeSignatureDen = 4;
                        calculateCompoundTimeProperties(sequence);
                    } else if (value == "C|") {
                        sequence.timeSignatureNum = 2;
                        sequence.timeSignatureDen = 2;
                        calculateCompoundTimeProperties(sequence);
                    } else {
                        size_t slashPos = value.find('/');
                        if (slashPos != std::string::npos) {
                            try {
                                sequence.timeSignatureNum = std::stoi(value.substr(0, slashPos));
                                sequence.timeSignatureDen = std::stoi(value.substr(slashPos + 1));

                                // Calculate compound time properties
                                calculateCompoundTimeProperties(sequence);
                            } catch (...) {
                                // Keep defaults
                            }
                        }
                    }
                    break;
                case 'L':
                    sequence.defaultNoteDuration = noteLengthToBeats(value);
                    sequence.unitNoteDuration = sequence.defaultNoteDuration;  // Store original
                    // If this is compound time, adjust the default duration
                    if (sequence.isCompoundTime) {
                        // In compound time, adjust duration for the compound beat grouping
                        // This ensures proper timing for compound meters like 6/8
                        sequence.defaultNoteDuration = sequence.defaultNoteDuration;
                    }
                    break;
                case 'Q':
                    // Handle tempo
                    {
                        size_t equalPos = value.find('=');
                        if (equalPos != std::string::npos) {
                            std::string bpmStr = value.substr(equalPos + 1);
                            try {
                                sequence.tempoBPM = std::stoi(bpmStr);
                            } catch (...) {
                                sequence.tempoBPM = 120;
                            }
                        } else {
                            try {
                                sequence.tempoBPM = std::stoi(value);
                            } catch (...) {
                                sequence.tempoBPM = 120;
                            }
                        }
                    }
                    break;
                case 'K':
                    sequence.key = value;
                    break;
                case 'P':
                    sequence.parts = value;
                    break;
                case 'V':
                    // Handle voice field
                    {
                        VoiceInfo voice;
                        if (parseVoiceField(line, voice)) {
                            sequence.voices[voice.id] = voice;
                            sequence.isMultiVoice = true;
                            console(("ABCParser: Registered voice " + voice.id + " with instrument " +
                                   std::to_string(voice.instrument) + " on channel " +
                                   std::to_string(voice.channel)).c_str());
                        }
                    }
                    break;
                case 'w':
                case 'W':
                    sequence.lyrics.push_back(value);
                    break;
                default:
                    // Ignore other info fields for now
                    break;
            }
        }
    }

    return true;
}

// Voice parsing implementation - Enhanced for ABC 2.1 specification
bool ABCParser::parseVoiceField(const std::string& voiceStr, VoiceInfo& voice) {
    // Extract voice field value (everything after V:)
    std::string value = extractInfoFieldValue(voiceStr);

    if (value.empty()) {
        return false;
    }

    // Parse voice ID (first token)
    std::istringstream iss(value);
    std::string token;
    if (!(iss >> token)) {
        return false;
    }

    voice.id = token;
    voice.name = token;  // Default name to ID
    voice.instrument = 0;  // Default to piano
    voice.channel = 0;     // Will be assigned later
    voice.clef = "treble";
    voice.transpose = 0;

    // Initialize ABC 2.1 extended properties
    std::string stemDirection = "auto";
    std::string middlePitch = "";
    int octaveShift = 0;
    int staffLines = 5;
    int clefLine = -1;  // -1 means use default for clef type
    bool hasOctaveMarking = false;
    int octaveMarkingShift = 0;

    // Parse optional parameters with enhanced ABC 2.1 support
    std::string remainingTokens;
    std::string temp;
    while (std::getline(iss, temp)) {
        remainingTokens += temp;
    }

    // Reset stringstream with all remaining content
    iss.clear();
    iss.str(value);
    iss >> token; // Skip voice ID

    while (iss >> token) {
        if (token.find("name=") == 0) {
            voice.name = token.substr(5);
            // Handle quoted names with spaces
            if (!voice.name.empty() && voice.name.front() == '"') {
                if (voice.name.back() != '"') {
                    // Multi-word quoted name - read until closing quote
                    std::string word;
                    while (iss >> word && word.back() != '"') {
                        voice.name += " " + word;
                    }
                    if (!word.empty()) {
                        voice.name += " " + word;
                    }
                }
                // Remove quotes
                if (voice.name.front() == '"' && voice.name.back() == '"') {
                    voice.name = voice.name.substr(1, voice.name.length() - 2);
                }
            }
        } else if (token.find("nm=") == 0) {
            // Abbreviated form of name
            voice.name = token.substr(3);
            if (!voice.name.empty() && voice.name.front() == '"' && voice.name.back() == '"') {
                voice.name = voice.name.substr(1, voice.name.length() - 2);
            }
        } else if (token.find("subname=") == 0 || token.find("snm=") == 0) {
            // subname or snm (abbreviated)
            size_t pos = token.find("=") + 1;
            voice.shortName = token.substr(pos);
            if (!voice.shortName.empty() && voice.shortName.front() == '"') {
                if (voice.shortName.back() != '"') {
                    std::string word;
                    while (iss >> word && word.back() != '"') {
                        voice.shortName += " " + word;
                    }
                    if (!word.empty()) {
                        voice.shortName += " " + word;
                    }
                }
                if (voice.shortName.front() == '"' && voice.shortName.back() == '"') {
                    voice.shortName = voice.shortName.substr(1, voice.shortName.length() - 2);
                }
            }
        } else if (token.find("instrument=") == 0) {
            try {
                voice.instrument = std::stoi(token.substr(11));
                // Allow synthesized instruments 200+ - don't clamp to MIDI range
                voice.instrument = std::clamp(voice.instrument, 0, 999);
            } catch (...) {
                voice.instrument = 0;
            }
        } else if (token.find("clef=") == 0) {
            std::string clefSpec = token.substr(5);
            parseClefSpecification(clefSpec, voice.clef, clefLine, hasOctaveMarking, octaveMarkingShift);
        } else if (token.find("transpose=") == 0 || token.find("t=") == 0) {
            // transpose or t (abbreviated)
            size_t pos = token.find("=") + 1;
            try {
                voice.transpose = std::stoi(token.substr(pos));
            } catch (...) {
                voice.transpose = 0;
            }
        } else if (token.find("stem=") == 0) {
            // Stem direction: up, down, auto
            stemDirection = token.substr(5);
        } else if (token.find("middle=") == 0 || token.find("m=") == 0) {
            // Middle note specification for clef positioning
            size_t pos = token.find("=") + 1;
            middlePitch = token.substr(pos);
            parseMiddlePitchSpecification(middlePitch, clefLine);
        } else if (token.find("octave=") == 0) {
            // Octave shift for transposition
            try {
                octaveShift = std::stoi(token.substr(7));
                voice.transpose += octaveShift * 12; // Convert octaves to semitones
            } catch (...) {
                octaveShift = 0;
            }
        } else if (token.find("stafflines=") == 0) {
            // Number of staff lines
            try {
                staffLines = std::stoi(token.substr(11));
                staffLines = std::clamp(staffLines, 1, 10); // Reasonable range
            } catch (...) {
                staffLines = 5;
            }
        }
    }

    // Apply octave marking transposition if present
    if (hasOctaveMarking) {
        voice.transpose += octaveMarkingShift * 12;
    }

    // Log enhanced voice properties for debugging
    if (debugOutput) {
        char debug[512];
        snprintf(debug, sizeof(debug),
            "ABCParser: Enhanced voice parsed - ID:'%s' Name:'%s' Clef:'%s' Instrument:%d Transpose:%d Stem:%s StaffLines:%d",
            voice.id.c_str(), voice.name.c_str(), voice.clef.c_str(),
            voice.instrument, voice.transpose, stemDirection.c_str(), staffLines);
        console(debug);
    }

    return true;
}

// Helper function to parse complex clef specifications
void ABCParser::parseClefSpecification(const std::string& clefSpec, std::string& clef, int& clefLine, bool& hasOctaveMarking, int& octaveShift) {
    clef = clefSpec;
    hasOctaveMarking = false;
    octaveShift = 0;
    clefLine = -1;

    // Handle octave markings (+8, -8)
    if (clef.find("+8") != std::string::npos) {
        hasOctaveMarking = true;
        octaveShift = 1;
        clef = clef.substr(0, clef.find("+8"));
    } else if (clef.find("-8") != std::string::npos) {
        hasOctaveMarking = true;
        octaveShift = -1;
        clef = clef.substr(0, clef.find("-8"));
    }

    // Handle numbered clef variants
    if (clef == "treble" || clef == "treble1") {
        clef = "treble";
        clefLine = 2;
    } else if (clef == "treble2") {
        clef = "treble";
        clefLine = 1;
    } else if (clef == "treble3") {
        clef = "treble";
        clefLine = 3;
    } else if (clef == "bass" || clef == "bass4") {
        clef = "bass";
        clefLine = 4;
    } else if (clef == "bass3" || clef == "baritone") {
        clef = "bass";
        clefLine = 3;
    } else if (clef == "alto" || clef == "alto3") {
        clef = "alto";
        clefLine = 3;
    } else if (clef == "alto1" || clef == "soprano") {
        clef = "alto";
        clefLine = 1;
    } else if (clef == "alto2" || clef == "mezzosoprano") {
        clef = "alto";
        clefLine = 2;
    } else if (clef == "tenor" || clef == "tenor4") {
        clef = "tenor";
        clefLine = 4;
    } else if (clef == "perc" || clef == "percussion") {
        clef = "percussion";
        clefLine = 3;
    } else if (clef == "none") {
        clef = "none";
        clefLine = 0;
    }

    // Extract explicit line numbers (e.g., "treble2", "bass3")
    std::regex lineRegex("([a-zA-Z]+)([0-9]+)");
    std::smatch match;
    if (std::regex_match(clef, match, lineRegex) && match.size() > 2) {
        std::string baseClef = match[1];
        try {
            clefLine = std::stoi(match[2]);
            clef = baseClef;
        } catch (...) {
            // Keep original clef if number parsing fails
        }
    }
}

// Helper function to parse middle pitch specification
void ABCParser::parseMiddlePitchSpecification(const std::string& middlePitch, int& clefLine) {
    // Convert middle pitch to appropriate clef line
    // This is a simplified implementation - full ABC 2.1 support would be more complex
    if (middlePitch == "B" || middlePitch == "b") {
        clefLine = 3; // Standard treble clef
    } else if (middlePitch == "C" || middlePitch == "c") {
        clefLine = 3; // Alto clef
    } else if (middlePitch == "A," || middlePitch == "a,") {
        clefLine = 4; // Tenor clef
    } else if (middlePitch == "D," || middlePitch == "d," || middlePitch == "d") {
        clefLine = 4; // Bass clef
    }
    // Add more middle pitch mappings as needed
}

// Enhanced decoration parsing for !symbol! syntax
bool ABCParser::parseExtendedDecorations(const std::string& decoration, DecorationInfo& decorationInfo) {
    if (decoration.length() < 3 || decoration[0] != '!' || decoration.back() != '!') {
        return false;
    }

    // Extract decoration name without ! marks
    std::string decorName = decoration.substr(1, decoration.length() - 2);

    // Parse common decorations
    if (decorName == "trill") {
        decorationInfo.type = "trill";
        decorationInfo.variant = "standard";
        decorationInfo.affectsDuration = true;
        return true;
    } else if (decorName == "trill(") {
        decorationInfo.type = "trill";
        decorationInfo.variant = "start_extended";
        decorationInfo.isExtended = true;
        return true;
    } else if (decorName == "trill)") {
        decorationInfo.type = "trill";
        decorationInfo.variant = "end_extended";
        decorationInfo.isExtended = true;
        return true;
    } else if (decorName == "uppermordent" || decorName == "pralltriller") {
        decorationInfo.type = "mordent";
        decorationInfo.variant = "upper";
        decorationInfo.affectsDuration = true;
        return true;
    } else if (decorName == "lowermordent" || decorName == "mordent") {
        decorationInfo.type = "mordent";
        decorationInfo.variant = "lower";
        decorationInfo.affectsDuration = true;
        return true;
    } else if (decorName == "turn") {
        decorationInfo.type = "turn";
        decorationInfo.variant = "standard";
        decorationInfo.affectsDuration = true;
        return true;
    } else if (decorName == "turnx") {
        decorationInfo.type = "turn";
        decorationInfo.variant = "with_line";
        decorationInfo.affectsDuration = true;
        return true;
    } else if (decorName == "invertedturn") {
        decorationInfo.type = "turn";
        decorationInfo.variant = "inverted";
        decorationInfo.affectsDuration = true;
        return true;
    } else if (decorName == "fermata") {
        decorationInfo.type = "fermata";
        decorationInfo.affectsDuration = true;
        decorationInfo.intensity = 1.5; // Extend duration by 50%
        return true;
    } else if (decorName == "staccato") {
        decorationInfo.type = "staccato";
        decorationInfo.affectsDuration = true;
        decorationInfo.intensity = 0.5; // Shorten to 50%
        return true;
    } else if (decorName == "tenuto") {
        decorationInfo.type = "tenuto";
        decorationInfo.affectsDuration = true;
        decorationInfo.intensity = 1.0; // Full duration
        return true;
    } else if (decorName == "accent" || decorName == ">") {
        decorationInfo.type = "accent";
        decorationInfo.affectsVelocity = true;
        decorationInfo.intensity = 1.2; // 20% velocity increase
        return true;
    }

    // Dynamic markings
    else if (decorName == "pppp") {
        decorationInfo.type = "dynamic";
        decorationInfo.variant = "pppp";
        decorationInfo.affectsVelocity = true;
        decorationInfo.intensity = 0.1;
        return true;
    } else if (decorName == "ppp") {
        decorationInfo.type = "dynamic";
        decorationInfo.variant = "ppp";
        decorationInfo.affectsVelocity = true;
        decorationInfo.intensity = 0.2;
        return true;
    } else if (decorName == "pp") {
        decorationInfo.type = "dynamic";
        decorationInfo.variant = "pp";
        decorationInfo.affectsVelocity = true;
        decorationInfo.intensity = 0.3;
        return true;
    } else if (decorName == "p") {
        decorationInfo.type = "dynamic";
        decorationInfo.variant = "p";
        decorationInfo.affectsVelocity = true;
        decorationInfo.intensity = 0.4;
        return true;
    } else if (decorName == "mp") {
        decorationInfo.type = "dynamic";
        decorationInfo.variant = "mp";
        decorationInfo.affectsVelocity = true;
        decorationInfo.intensity = 0.6;
        return true;
    } else if (decorName == "mf") {
        decorationInfo.type = "dynamic";
        decorationInfo.variant = "mf";
        decorationInfo.affectsVelocity = true;
        decorationInfo.intensity = 0.7;
        return true;
    } else if (decorName == "f") {
        decorationInfo.type = "dynamic";
        decorationInfo.variant = "f";
        decorationInfo.affectsVelocity = true;
        decorationInfo.intensity = 0.8;
        return true;
    } else if (decorName == "ff") {
        decorationInfo.type = "dynamic";
        decorationInfo.variant = "ff";
        decorationInfo.affectsVelocity = true;
        decorationInfo.intensity = 0.9;
        return true;
    } else if (decorName == "fff") {
        decorationInfo.type = "dynamic";
        decorationInfo.variant = "fff";
        decorationInfo.affectsVelocity = true;
        decorationInfo.intensity = 0.95;
        return true;
    } else if (decorName == "ffff") {
        decorationInfo.type = "dynamic";
        decorationInfo.variant = "ffff";
        decorationInfo.affectsVelocity = true;
        decorationInfo.intensity = 1.0;
        return true;
    }

    // Crescendo and diminuendo
    else if (decorName == "crescendo(" || decorName == "<(") {
        decorationInfo.type = "crescendo";
        decorationInfo.variant = "start";
        decorationInfo.isExtended = true;
        return true;
    } else if (decorName == "crescendo)" || decorName == "<)") {
        decorationInfo.type = "crescendo";
        decorationInfo.variant = "end";
        decorationInfo.isExtended = true;
        return true;
    } else if (decorName == "diminuendo(" || decorName == ">(") {
        decorationInfo.type = "diminuendo";
        decorationInfo.variant = "start";
        decorationInfo.isExtended = true;
        return true;
    } else if (decorName == "diminuendo)" || decorName == ">)") {
        decorationInfo.type = "diminuendo";
        decorationInfo.variant = "end";
        decorationInfo.isExtended = true;
        return true;
    }

    // Navigation symbols
    else if (decorName == "segno") {
        decorationInfo.type = "navigation";
        decorationInfo.variant = "segno";
        return true;
    } else if (decorName == "coda") {
        decorationInfo.type = "navigation";
        decorationInfo.variant = "coda";
        return true;
    } else if (decorName == "D.S.") {
        decorationInfo.type = "navigation";
        decorationInfo.variant = "dal_segno";
        return true;
    } else if (decorName == "D.C.") {
        decorationInfo.type = "navigation";
        decorationInfo.variant = "da_capo";
        return true;
    } else if (decorName == "fine") {
        decorationInfo.type = "navigation";
        decorationInfo.variant = "fine";
        return true;
    }

    // String techniques
    else if (decorName == "upbow") {
        decorationInfo.type = "bowing";
        decorationInfo.variant = "up";
        return true;
    } else if (decorName == "downbow") {
        decorationInfo.type = "bowing";
        decorationInfo.variant = "down";
        return true;
    } else if (decorName == "open") {
        decorationInfo.type = "string_technique";
        decorationInfo.variant = "open";
        return true;
    } else if (decorName == "thumb") {
        decorationInfo.type = "string_technique";
        decorationInfo.variant = "thumb";
        return true;
    }

    // Fingerings (0-5)
    else if (decorName.length() == 1 && decorName[0] >= '0' && decorName[0] <= '5') {
        decorationInfo.type = "fingering";
        decorationInfo.variant = decorName;
        return true;
    }

    // Unknown decoration - store as-is for future extension
    decorationInfo.type = "unknown";
    decorationInfo.variant = decorName;
    return true;
}

// Enhanced tuplet parsing for (p:q:r syntax
bool ABCParser::parseAdvancedTuplet(const std::string& tupletStr, TupletInfo& tuplet) {
    if (tupletStr.empty() || tupletStr[0] != '(') {
        return false;
    }

    // Remove opening parenthesis
    std::string content = tupletStr.substr(1);

    // Split by colons
    std::vector<int> values;
    std::string current = "";

    for (char c : content) {
        if (c == ':') {
            if (!current.empty()) {
                try {
                    values.push_back(std::stoi(current));
                } catch (...) {
                    return false;
                }
                current = "";
            } else {
                values.push_back(0); // Empty means use default
            }
        } else if (std::isdigit(c)) {
            current += c;
        } else {
            break; // Stop at non-digit, non-colon
        }
    }

    // Add final value
    if (!current.empty()) {
        try {
            values.push_back(std::stoi(current));
        } catch (...) {
            return false;
        }
    }

    if (values.empty()) return false;

    // Parse values according to (p:q:r format
    tuplet.notes = values[0]; // p - number of notes

    if (values.size() > 1 && values[1] > 0) {
        tuplet.inTimeOf = values[1]; // q - time value
    } else {
        // Default q based on p and time signature
        if (tuplet.notes == 2) tuplet.inTimeOf = 3;
        else if (tuplet.notes == 3) tuplet.inTimeOf = 2;
        else if (tuplet.notes == 4) tuplet.inTimeOf = 3;
        else if (tuplet.notes == 6) tuplet.inTimeOf = 4;
        else if (tuplet.notes == 8) tuplet.inTimeOf = 6;
        else tuplet.inTimeOf = 2; // Default fallback
    }

    if (values.size() > 2 && values[2] > 0) {
        tuplet.affectNext = values[2]; // r - notes affected
    } else {
        tuplet.affectNext = tuplet.notes; // Default r = p
    }

    tuplet.isActive = true;

    if (debugOutput) {
        char debug[256];
        snprintf(debug, sizeof(debug), "ABCParser: Advanced tuplet (%d:%d:%d) - %d notes in time of %d affecting %d notes",
                tuplet.notes, tuplet.inTimeOf, tuplet.affectNext,
                tuplet.notes, tuplet.inTimeOf, tuplet.affectNext);
        console(debug);
    }

    return true;
}

// Enhanced voice property parsing
bool ABCParser::parseVoiceProperties(const std::string& voiceStr, VoiceInfo& voice) {
    // Parse V:id properties format: V:T1 name="Tenor" clef=treble transpose=-2

    size_t colonPos = voiceStr.find(':');
    if (colonPos == std::string::npos) return false;

    std::string content = voiceStr.substr(colonPos + 1);

    // Extract voice ID (first word/identifier)
    std::istringstream iss(content);
    if (!(iss >> voice.id)) return false;

    // Parse properties
    std::string token;
    while (iss >> token) {
        size_t equalPos = token.find('=');
        if (equalPos != std::string::npos) {
            std::string key = token.substr(0, equalPos);
            std::string value = token.substr(equalPos + 1);

            // Remove quotes from value if present
            if (value.length() >= 2 && value[0] == '"' && value.back() == '"') {
                value = value.substr(1, value.length() - 2);
            }

            // Process property
            if (key == "name") {
                voice.name = value;
            } else if (key == "snm" || key == "subname") {
                voice.shortName = value;
            } else if (key == "clef") {
                voice.clef = value;
                // Simplified clef parsing for now
                if (value.find("treble") == 0) {
                    voice.clef = "treble";
                } else if (value == "bass") {
                    voice.clef = "bass";
                } else if (value == "perc" || value == "percussion") {
                    voice.clef = "perc";
                    voice.channel = 9; // MIDI channel 10 for percussion
                }
            } else if (key == "transpose") {
                try {
                    voice.transpose = std::stoi(value);
                } catch (...) {}
            } else if (key == "octave") {
                try {
                    voice.octaveShift = std::stoi(value);
                } catch (...) {}
            } else if (key == "stem") {
                voice.stemDirection = value;
            } else if (key == "middle") {
                voice.middle = value;
                // Store middle pitch specification
                voice.middle = value;
            } else if (key == "stafflines") {
                try {
                    voice.staffLines = std::stoi(value);
                } catch (...) {}
            }
        }
    }

    if (debugOutput) {
        char debug[512];
        snprintf(debug, sizeof(debug), "ABCParser: Voice properties - ID:%s, Name:%s, Clef:%s, Transpose:%d, Octave:%d",
                voice.id.c_str(), voice.name.c_str(), voice.clef.c_str(), voice.transpose, voice.octaveShift);
        console(debug);
    }

    return true;
}

// Enhanced clef specification parsing


// Enhanced chord symbol parsing for extended jazz chords
std::vector<int> ABCParser::parseChordSymbol(const std::string& chordName) {
    std::vector<int> notes;

    if (chordName.empty()) return notes;

    // Extract root note (first letter)
    char root = std::toupper(chordName[0]);
    int rootNote = 0;

    // Convert root to MIDI note (C4 = 60)
    switch (root) {
        case 'C': rootNote = 60; break;
        case 'D': rootNote = 62; break;
        case 'E': rootNote = 64; break;
        case 'F': rootNote = 65; break;
        case 'G': rootNote = 67; break;
        case 'A': rootNote = 69; break;
        case 'B': rootNote = 71; break;
        default: return notes; // Invalid root
    }

    // Handle accidentals
    size_t pos = 1;
    if (pos < chordName.length()) {
        if (chordName[pos] == '#') {
            rootNote++;
            pos++;
        } else if (chordName[pos] == 'b') {
            rootNote--;
            pos++;
        }
    }

    // Handle slash chords (bass note)
    int bassNote = rootNote;
    size_t slashPos = chordName.find('/');
    if (slashPos != std::string::npos && slashPos + 1 < chordName.length()) {
        char bassLetter = std::toupper(chordName[slashPos + 1]);
        switch (bassLetter) {
            case 'C': bassNote = 60; break;
            case 'D': bassNote = 62; break;
            case 'E': bassNote = 64; break;
            case 'F': bassNote = 65; break;
            case 'G': bassNote = 67; break;
            case 'A': bassNote = 69; break;
            case 'B': bassNote = 71; break;
        }
        // Handle bass accidentals
        if (slashPos + 2 < chordName.length()) {
            if (chordName[slashPos + 2] == '#') bassNote++;
            else if (chordName[slashPos + 2] == 'b') bassNote--;
        }
    }

    // Get chord quality (before slash if present)
    std::string quality = chordName.substr(pos);
    if (slashPos != std::string::npos) {
        quality = quality.substr(0, slashPos - pos);
    }

    // Transpose to bass register (one octave lower)
    rootNote -= 12;
    bassNote -= 24; // Bass note goes even lower

    // Add bass note first (if different from root)
    if (bassNote != rootNote - 12) {
        notes.push_back(bassNote);
    }

    // Build chord based on quality with enhanced jazz support
    if (quality.empty() || quality == "maj" || quality == "M") {
        // Major triad: root, major third, fifth
        notes.push_back(rootNote);
        notes.push_back(rootNote + 4);
        notes.push_back(rootNote + 7);
    } else if (quality == "m" || quality == "min" || quality == "-") {
        // Minor triad: root, minor third, fifth
        notes.push_back(rootNote);
        notes.push_back(rootNote + 3);
        notes.push_back(rootNote + 7);
    } else if (quality == "7") {
        // Dominant 7th: root, major third, fifth, minor seventh
        notes.push_back(rootNote);
        notes.push_back(rootNote + 4);
        notes.push_back(rootNote + 7);
        notes.push_back(rootNote + 10);
    } else if (quality == "m7" || quality == "-7") {
        // Minor 7th: root, minor third, fifth, minor seventh
        notes.push_back(rootNote);
        notes.push_back(rootNote + 3);
        notes.push_back(rootNote + 7);
        notes.push_back(rootNote + 10);
    } else if (quality == "maj7" || quality == "M7" || quality == "Δ7") {
        // Major 7th: root, major third, fifth, major seventh
        notes.push_back(rootNote);
        notes.push_back(rootNote + 4);
        notes.push_back(rootNote + 7);
        notes.push_back(rootNote + 11);
    } else if (quality == "m7b5" || quality == "ø7" || quality == "-7b5") {
        // Half-diminished 7th: root, minor third, diminished fifth, minor seventh
        notes.push_back(rootNote);
        notes.push_back(rootNote + 3);
        notes.push_back(rootNote + 6);
        notes.push_back(rootNote + 10);
    } else if (quality == "dim7" || quality == "°7") {
        // Fully diminished 7th: root, minor third, diminished fifth, diminished seventh
        notes.push_back(rootNote);
        notes.push_back(rootNote + 3);
        notes.push_back(rootNote + 6);
        notes.push_back(rootNote + 9);
    } else if (quality == "dim" || quality == "°") {
        // Diminished triad: root, minor third, diminished fifth
        notes.push_back(rootNote);
        notes.push_back(rootNote + 3);
        notes.push_back(rootNote + 6);
    } else if (quality == "aug" || quality == "+" || quality == "+5") {
        // Augmented triad: root, major third, augmented fifth
        notes.push_back(rootNote);
        notes.push_back(rootNote + 4);
        notes.push_back(rootNote + 8);
    } else if (quality == "sus4" || quality == "sus") {
        // Suspended 4th: root, fourth, fifth
        notes.push_back(rootNote);
        notes.push_back(rootNote + 5);
        notes.push_back(rootNote + 7);
    } else if (quality == "sus2") {
        // Suspended 2nd: root, second, fifth
        notes.push_back(rootNote);
        notes.push_back(rootNote + 2);
        notes.push_back(rootNote + 7);
    } else if (quality == "9") {
        // Dominant 9th: root, third, fifth, minor seventh, ninth
        notes.push_back(rootNote);
        notes.push_back(rootNote + 4);
        notes.push_back(rootNote + 7);
        notes.push_back(rootNote + 10);
        notes.push_back(rootNote + 14);
    } else if (quality == "m9" || quality == "-9") {
        // Minor 9th: root, minor third, fifth, minor seventh, ninth
        notes.push_back(rootNote);
        notes.push_back(rootNote + 3);
        notes.push_back(rootNote + 7);
        notes.push_back(rootNote + 10);
        notes.push_back(rootNote + 14);
    } else if (quality == "maj9" || quality == "M9" || quality == "Δ9") {
        // Major 9th: root, third, fifth, major seventh, ninth
        notes.push_back(rootNote);
        notes.push_back(rootNote + 4);
        notes.push_back(rootNote + 7);
        notes.push_back(rootNote + 11);
        notes.push_back(rootNote + 14);
    } else if (quality == "11") {
        // Dominant 11th: root, third, fifth, minor seventh, ninth, eleventh
        notes.push_back(rootNote);
        notes.push_back(rootNote + 4);
        notes.push_back(rootNote + 7);
        notes.push_back(rootNote + 10);
        notes.push_back(rootNote + 14);
        notes.push_back(rootNote + 17);
    } else if (quality == "13") {
        // Dominant 13th: root, third, fifth, minor seventh, ninth, thirteenth
        notes.push_back(rootNote);
        notes.push_back(rootNote + 4);
        notes.push_back(rootNote + 7);
        notes.push_back(rootNote + 10);
        notes.push_back(rootNote + 14);
        notes.push_back(rootNote + 21);
    } else if (quality == "6") {
        // Added 6th: root, third, fifth, sixth
        notes.push_back(rootNote);
        notes.push_back(rootNote + 4);
        notes.push_back(rootNote + 7);
        notes.push_back(rootNote + 9);
    } else if (quality == "m6" || quality == "-6") {
        // Minor 6th: root, minor third, fifth, sixth
        notes.push_back(rootNote);
        notes.push_back(rootNote + 3);
        notes.push_back(rootNote + 7);
        notes.push_back(rootNote + 9);
    } else if (quality == "add9") {
        // Add 9th: root, third, fifth, ninth (no seventh)
        notes.push_back(rootNote);
        notes.push_back(rootNote + 4);
        notes.push_back(rootNote + 7);
        notes.push_back(rootNote + 14);
    } else {
        // Default to major triad for unrecognized qualities
        notes.push_back(rootNote);
        notes.push_back(rootNote + 4);
        notes.push_back(rootNote + 7);

        if (debugOutput) {
            char debug[256];
            snprintf(debug, sizeof(debug), "ABCParser: Unknown chord quality '%s' in '%s', defaulting to major triad",
                    quality.c_str(), chordName.c_str());
            console(debug);
        }
    }

    if (debugOutput) {
        char debug[512];
        std::string noteList = "";
        for (size_t i = 0; i < notes.size(); i++) {
            if (i > 0) noteList += ", ";
            noteList += std::to_string(notes[i]);
        }
        snprintf(debug, sizeof(debug), "ABCParser: Chord '%s' -> MIDI notes: %s",
                chordName.c_str(), noteList.c_str());
        console(debug);
    }

    return notes;
}

// Multi-voice ABC parsing implementation
bool ABCParser::parseMultiVoiceABC(const std::vector<std::string>& lines, ST_MusicSequence& sequence) {
    console("ABCParser::parseMultiVoiceABC: Starting multi-voice parsing");

    // First pass: parse header info fields and voice definitions
    if (!parseInfoFields(lines, sequence)) {
        return false;
    }

    // Assign MIDI channels to voices
    console("🔧 ASSIGNING MIDI CHANNELS TO VOICES:");
    int channelCounter = 0;
    for (auto& voicePair : sequence.voices) {
        voicePair.second.channel = channelCounter;
        channelCounter = (channelCounter + 1) % 16;  // Wrap at 16 channels

        char debug[256];
        snprintf(debug, sizeof(debug), "📍 VOICE ASSIGNMENT: '%s' → Channel %d, Instrument %d",
                voicePair.second.id.c_str(), voicePair.second.channel, voicePair.second.instrument);
        console(debug);
    }
    console("✅ Voice channel assignment complete");

    // Second pass: parse music for each voice
    std::string currentVoiceInParsing = "";
    std::string musicBody = "";

    for (const std::string& line : lines) {
        if (shouldIgnoreLine(line)) continue;

        std::string cleanLine = stripComments(line);
        if (cleanLine.empty()) continue;

        // Check for voice selection [V:voiceId]
        if (cleanLine.find("[V:") == 0) {
            // Parse previous voice's music if any
            if (!currentVoiceInParsing.empty() && !musicBody.empty()) {
                if (!parseVoiceMusic(musicBody, currentVoiceInParsing, sequence)) {
                    return false;
                }
                musicBody.clear();
            }

            // Extract new voice ID
            size_t endBracket = cleanLine.find(']');
            if (endBracket != std::string::npos) {
                currentVoiceInParsing = cleanLine.substr(3, endBracket - 3);
                console(("ABCParser: Switching to voice " + currentVoiceInParsing).c_str());
            }
        } else if (!isInfoField(cleanLine) && !currentVoiceInParsing.empty()) {
            // Accumulate music for current voice
            musicBody += cleanLine + " ";
        }
    }

    // Parse final voice's music
    if (!currentVoiceInParsing.empty() && !musicBody.empty()) {
        if (!parseVoiceMusic(musicBody, currentVoiceInParsing, sequence)) {
            return false;
        }
    }

    // Debug logging removed for cleaner output
    return true;
}

// Parse music for a specific voice
bool ABCParser::parseVoiceMusic(const std::string& musicBody, const std::string& voiceId, ST_MusicSequence& sequence) {
    // Set current voice context
    auto voiceIt = sequence.voices.find(voiceId);
    if (voiceIt == sequence.voices.end()) {
        char error[256];
        snprintf(error, sizeof(error), "ABCParser: Voice '%s' not defined", voiceId.c_str());
        console(error);
        return false;
    }

    currentVoiceId = voiceId;
    currentVoice = voiceIt->second;

    char debug[256];
    snprintf(debug, sizeof(debug), "🎼 PARSING VOICE MUSIC: '%s' → Channel %d, Instrument %d",
             voiceId.c_str(), currentVoice.channel, currentVoice.instrument);
    console(debug);

    // Tokenize and parse this voice's music
    std::vector<ABCToken> tokens = tokenize(musicBody);
    if (tokens.empty()) {
        console("ABCParser: No tokens found for voice");
        return true;  // Empty voice is okay
    }

    // Parse the voice's music body
    size_t notesBefore = sequence.notes.size();
    if (!parseBody(tokens, sequence)) {
        return false;
    }

    size_t notesAdded = sequence.notes.size() - notesBefore;
    snprintf(debug, sizeof(debug), "✅ VOICE PARSING COMPLETE: Added %zu notes for voice '%s' on channel %d",
             notesAdded, voiceId.c_str(), currentVoice.channel);
    console(debug);

    return true;
}

bool ABCParser::isInfoField(const std::string& line) {
    return line.length() >= 2 && line[1] == ':' &&
           ((line[0] >= 'A' && line[0] <= 'Z') || (line[0] >= 'a' && line[0] <= 'z'));
}

std::string ABCParser::extractInfoFieldValue(const std::string& line) {
    if (line.length() <= 2) return "";

    std::string value = line.substr(2);
    // Trim leading and trailing whitespace
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);
    return value;
}

bool ABCParser::shouldIgnoreLine(const std::string& line) {
    if (line.empty()) return true;

    // Ignore comment lines
    std::string trimmed = line;
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    if (trimmed.empty() || trimmed[0] == '%') return true;

    // Ignore stylesheet directives (lines starting with %%)
    if (trimmed.length() >= 2 && trimmed[0] == '%' && trimmed[1] == '%') return true;

    return false;
}

std::string ABCParser::stripComments(const std::string& line) {
    size_t commentPos = line.find('%');
    if (commentPos != std::string::npos) {
        return line.substr(0, commentPos);
    }
    return line;
}

// Compound time signature support functions
bool ABCParser::isCompoundMeter(int num, int den) {
    // Compound meters have numerators divisible by 3 (and > 3) with denominators of 4, 8, or 16
    // Common compound meters: 6/8, 9/8, 12/8, 6/4, 9/4, 12/4, 6/16, 9/16, 12/16
    return (num > 3 && num % 3 == 0) && (den == 4 || den == 8 || den == 16);
}

void ABCParser::calculateCompoundTimeProperties(ST_MusicSequence& sequence) {
    sequence.isCompoundTime = isCompoundMeter(sequence.timeSignatureNum, sequence.timeSignatureDen);

    if (sequence.isCompoundTime) {
        // In compound time, beats are grouped by 3
        sequence.compoundBeatsPerMeasure = sequence.timeSignatureNum / 3;
        sequence.subdivisionPerBeat = 3;

        // Set default note duration based on ABC standard for compound time
        // According to ABC standard: if meter < 0.75, default is 1/16; if >= 0.75, default is 1/8
        double meterValue = (double)sequence.timeSignatureNum / sequence.timeSignatureDen;
        if (meterValue < 0.75) {
            sequence.defaultNoteDuration = 0.25;  // 1/16 note in quarter-note beats
        } else {
            sequence.defaultNoteDuration = 0.5;   // 1/8 note in quarter-note beats
        }
        sequence.unitNoteDuration = sequence.defaultNoteDuration;

        if (debugOutput) {
            char debug[256];
            snprintf(debug, sizeof(debug),
                    "ABCParser: Compound time %d/%d detected - %d beats per measure, %d subdivisions per beat",
                    sequence.timeSignatureNum, sequence.timeSignatureDen,
                    sequence.compoundBeatsPerMeasure, sequence.subdivisionPerBeat);
            console(debug);
        }
    } else {
        // Simple time
        sequence.compoundBeatsPerMeasure = sequence.timeSignatureNum;
        sequence.subdivisionPerBeat = 2;

        if (debugOutput) {
            char debug[256];
            snprintf(debug, sizeof(debug),
                    "ABCParser: Simple time %d/%d - %d beats per measure",
                    sequence.timeSignatureNum, sequence.timeSignatureDen, sequence.timeSignatureNum);
            console(debug);
        }
    }
}

double ABCParser::getEffectiveBeatDuration(const ST_MusicSequence& sequence) {
    if (sequence.isCompoundTime) {
        // In compound time, the beat is a dotted note (1.5 times the written value)
        double baseDuration = 1.0 / (sequence.timeSignatureDen / 4.0);
        return baseDuration * 1.5; // Dotted quarter for 6/8, 9/8, 12/8
    } else {
        // In simple time, the beat is the denominator value
        return 1.0 / (sequence.timeSignatureDen / 4.0);
    }
}

bool ABCParser::isBarLine(const std::string& token) {
    return token == "|" || token == "||" || token == "|:" || token == ":|" ||
           token == "::" || token == "[|" || token == "|]";
}

bool ABCParser::isRepeatMarker(const std::string& token) {
    return token == "|:" || token == ":|" || token == "::" ||
           token.find("[1") == 0 || token.find("[2") == 0;
}



} // namespace SuperTerminal

// C API functions are now implemented in AudioSystem.mm
// This avoids duplicate/conflicting implementations

extern "C" {

// Lightweight frame update function (called from main render loop)
void music_update_frame() {
    // Check for emergency shutdown before processing
    if (is_emergency_shutdown_requested()) {
        // Don't call shutdown from frame update - just skip processing
        return;
    }

    // Now just a lightweight check - heavy work moved to background thread
    if (g_audioSystem && g_audioSystem->getMusicPlayer()) {
        g_audioSystem->getMusicPlayer()->update();
    }
}

// Shutdown music system
void music_shutdown() {
    if (g_audioSystem && g_audioSystem->getMusicPlayer()) {
        g_audioSystem->getMusicPlayer()->shutdown();
        unregister_active_subsystem();
    }
}

// Debug control functions
void set_music_debug_output(bool enabled) {
    if (g_audioSystem && g_audioSystem->getMusicPlayer()) {
        g_audioSystem->getMusicPlayer()->setDebugMidiOutput(enabled);
    }
}

bool get_music_debug_output() {
    if (g_audioSystem && g_audioSystem->getMusicPlayer()) {
        return g_audioSystem->getMusicPlayer()->getDebugMidiOutput();
    }
    return false;
}

// Parser debug control functions
void set_music_parser_debug_output(bool enabled) {
    if (g_audioSystem && g_audioSystem->getMusicPlayer()) {
        g_audioSystem->getMusicPlayer()->setDebugParserOutput(enabled);
    }
}

bool get_music_parser_debug_output() {
    if (g_audioSystem && g_audioSystem->getMusicPlayer()) {
        return g_audioSystem->getMusicPlayer()->getDebugParserOutput();
    }
    return false;
}

// ABC Player Client Integration Functions
bool abc_music_initialize() {
    return abc_client_initialize();
}

void abc_music_shutdown() {
    abc_client_shutdown();
}

bool abc_music_is_initialized() {
    return abc_client_is_initialized();
}

bool abc_play_music(const char* abc_notation, const char* name) {
    return abc_client_play_abc(abc_notation, name);
}

bool abc_play_music_file(const char* filename) {
    return abc_client_play_abc_file(filename);
}

bool abc_stop_music() {
    return abc_client_stop();
}

bool abc_pause_music() {
    return abc_client_pause();
}

bool abc_resume_music() {
    return abc_client_resume();
}

bool abc_clear_music_queue() {
    return abc_client_clear_queue();
}

bool abc_set_music_volume(float volume) {
    return abc_client_set_volume(volume);
}

bool abc_is_music_playing() {
    return abc_client_is_playing();
}

bool abc_is_music_paused() {
    return abc_client_is_paused();
}

int abc_get_music_queue_size() {
    return abc_client_get_queue_size();
}

float abc_get_music_volume() {
    return abc_client_get_volume();
}

void abc_set_auto_start_server(bool auto_start) {
    abc_client_set_auto_start_server(auto_start);
}

void abc_set_debug_output(bool debug) {
    abc_client_set_debug_output(debug);
}

} // extern "C"
