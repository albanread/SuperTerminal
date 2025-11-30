#include "ABCHeaderParser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace ABCPlayer {

// Static member definitions
const std::regex ABCHeaderParser::header_field_regex_(R"(^([A-Za-z]):\s*(.*)$)");
const std::regex ABCHeaderParser::fraction_regex_(R"((\d+)/(\d+))");
const std::regex ABCHeaderParser::tempo_regex_(R"((?:(\d+)/(\d+)=)?(\d+))");

const std::map<std::string, KeySignature> ABCHeaderParser::standard_keys_ = {
    {"C", KeySignature("C")},
    {"G", KeySignature("G")}, {"D", KeySignature("D")}, {"A", KeySignature("A")}, 
    {"E", KeySignature("E")}, {"B", KeySignature("B")}, {"F#", KeySignature("F#")}, 
    {"C#", KeySignature("C#")},
    {"F", KeySignature("F")}, {"Bb", KeySignature("Bb")}, {"Eb", KeySignature("Eb")}, 
    {"Ab", KeySignature("Ab")}, {"Db", KeySignature("Db")}, {"Gb", KeySignature("Gb")}, 
    {"Cb", KeySignature("Cb")},
    {"Am", KeySignature("Am")}, {"Em", KeySignature("Em")}, {"Bm", KeySignature("Bm")}, 
    {"F#m", KeySignature("F#m")}, {"C#m", KeySignature("C#m")}, {"G#m", KeySignature("G#m")}, 
    {"D#m", KeySignature("D#m")}, {"Dm", KeySignature("Dm")}, {"Gm", KeySignature("Gm")}, 
    {"Cm", KeySignature("Cm")}, {"Fm", KeySignature("Fm")}, {"Bbm", KeySignature("Bbm")}, 
    {"Ebm", KeySignature("Ebm")}, {"Abm", KeySignature("Abm")}
};

ABCHeaderParser::ABCHeaderParser() : end_of_header_(false) {
}

ABCHeaderParser::~ABCHeaderParser() = default;

bool ABCHeaderParser::parseHeaderLine(const std::string& line, ABCTune& tune) {
    std::smatch matches;
    if (!std::regex_match(line, matches, header_field_regex_)) {
        addError("Invalid header field format: " + line);
        return false;
    }
    
    char field_type = matches[1].str()[0];
    std::string content = matches[2].str();
    content = trim(content);
    
    bool success = false;
    
    switch (field_type) {
        case 'X':
            success = parseXField(content, tune);
            break;
        case 'T':
            success = parseTField(content, tune);
            break;
        case 'C':
            success = parseCField(content, tune);
            break;
        case 'O':
            success = parseOField(content, tune);
            break;
        case 'P':
            success = parsePField(content, tune);
            break;
        case 'K':
            success = parseKField(content, tune);
            end_of_header_ = true; // K: field marks end of header
            break;
        case 'L':
            success = parseLField(content, tune);
            break;
        case 'M':
            success = parseMField(content, tune);
            break;
        case 'Q':
            success = parseQField(content, tune);
            break;
        case 'V':
            success = parseVField(content, tune);
            break;
        default:
            addWarning("Ignoring unsupported header field: " + std::string(1, field_type));
            success = true; // Not an error, just unsupported
            break;
    }
    
    return success;
}

bool ABCHeaderParser::isHeaderField(const std::string& line) const {
    return std::regex_match(line, header_field_regex_);
}

bool ABCHeaderParser::parseXField(const std::string& content, ABCTune& tune) {
    try {
        tune.tune_number = std::stoi(content);
        return true;
    } catch (const std::exception&) {
        addError("Invalid tune number: " + content);
        return false;
    }
}

bool ABCHeaderParser::parseTField(const std::string& content, ABCTune& tune) {
    tune.title = content;
    return true;
}

bool ABCHeaderParser::parseCField(const std::string& content, ABCTune& tune) {
    tune.composer = content;
    return true;
}

bool ABCHeaderParser::parseOField(const std::string& content, ABCTune& tune) {
    tune.origin = content;
    return true;
}

bool ABCHeaderParser::parsePField(const std::string& content, ABCTune& tune) {
    tune.parts = content;
    return true;
}

bool ABCHeaderParser::parseKField(const std::string& content, ABCTune& tune) {
    tune.default_key = parseKeySignature(content);
    return true;
}

bool ABCHeaderParser::parseLField(const std::string& content, ABCTune& tune) {
    std::smatch matches;
    if (std::regex_match(content, matches, fraction_regex_)) {
        int num = std::stoi(matches[1].str());
        int denom = std::stoi(matches[2].str());
        tune.default_unit_length = Fraction(num, denom);
        return true;
    } else {
        addError("Invalid unit length format: " + content);
        return false;
    }
}

bool ABCHeaderParser::parseMField(const std::string& content, ABCTune& tune) {
    tune.default_timesig = parseTimeSignature(content);
    return true;
}

bool ABCHeaderParser::parseQField(const std::string& content, ABCTune& tune) {
    tune.default_tempo = parseTempo(content);
    return true;
}

bool ABCHeaderParser::parseVField(const std::string& content, ABCTune& tune) {
    // Parse voice specification like "V:1" or "V:melody name="Lead" instrument=73"
    std::istringstream stream(content);
    std::string voice_identifier;
    stream >> voice_identifier;
    
    int voice_id;
    std::string voice_name = voice_identifier;
    
    // Try to parse as number first
    try {
        voice_id = std::stoi(voice_identifier);
        voice_name = "voice_" + voice_identifier; // Use voice_N format for numbered voices
    } catch (const std::exception&) {
        // Not a number, treat as name and assign sequential ID
        voice_id = tune.voices.size() + 1;
        voice_name = voice_identifier;
    }
    
    // Create or get voice context
    if (tune.voices.find(voice_id) == tune.voices.end()) {
        VoiceContext voice;
        voice.voice_number = voice_id;
        voice.name = voice_name; // Store identifier for [V:name] lookup
        voice.key = tune.default_key;
        voice.timesig = tune.default_timesig;
        voice.unit_length = tune.default_unit_length;
        voice.clef = ClefType::TREBLE;
        voice.transpose = 0;
        voice.octave_shift = 0;
        voice.instrument = 0;
        voice.channel = -1;
        voice.velocity = 80;
        voice.muted = false;
        tune.voices[voice_id] = voice;
    } else {
        // Update existing voice identifier
        if (!voice_name.empty()) {
            tune.voices[voice_id].name = voice_name;
        }
    }
    
    // Parse additional attributes
    std::string remaining;
    std::getline(stream, remaining);
    if (!remaining.empty()) {
        parseVoiceAttributes(remaining, tune.voices[voice_id]);
    }
    
    return true;
}

KeySignature ABCHeaderParser::parseKeySignature(const std::string& key_str) {
    std::string trimmed = trim(key_str);
    std::string key_name;
    std::string mode = "major";
    
    // Split key and mode
    size_t space_pos = trimmed.find(' ');
    if (space_pos != std::string::npos) {
        key_name = trimmed.substr(0, space_pos);
        mode = toLowerCase(trimmed.substr(space_pos + 1));
    } else {
        key_name = trimmed;
    }
    
    // Look up standard key
    auto it = standard_keys_.find(key_name);
    if (it != standard_keys_.end()) {
        KeySignature key = it->second;
        // Mode handling would go here if needed
        return key;
    }
    
    // Default to C major if unknown
    addWarning("Unknown key signature: " + key_str + ", defaulting to C major");
    return KeySignature("C");
}

TimeSignature ABCHeaderParser::parseTimeSignature(const std::string& time_str) {
    std::string trimmed = trim(time_str);
    
    if (trimmed == "C") {
        return TimeSignature(4, 4); // Common time
    } else if (trimmed == "C|") {
        return TimeSignature(2, 2); // Cut time
    }
    
    std::smatch matches;
    if (std::regex_match(trimmed, matches, fraction_regex_)) {
        int num = std::stoi(matches[1].str());
        int denom = std::stoi(matches[2].str());
        return TimeSignature(num, denom);
    }
    
    addWarning("Invalid time signature: " + time_str + ", defaulting to 4/4");
    return TimeSignature(4, 4);
}

Tempo ABCHeaderParser::parseTempo(const std::string& tempo_str) {
    std::string trimmed = trim(tempo_str);
    
    std::smatch matches;
    if (std::regex_match(trimmed, matches, tempo_regex_)) {
        Fraction note_value(1, 4); // Default to quarter note
        
        // Check if note value is specified
        if (matches[1].matched && matches[2].matched) {
            int num = std::stoi(matches[1].str());
            int denom = std::stoi(matches[2].str());
            note_value = Fraction(num, denom);
        }
        
        int bpm = std::stoi(matches[3].str());
        
        Tempo tempo;
        tempo.note_value = note_value;
        tempo.bpm = bpm;
        return tempo;
    }
    
    // Try parsing as just a number
    try {
        int bpm = std::stoi(trimmed);
        Tempo tempo;
        tempo.note_value = Fraction(1, 4);
        tempo.bpm = bpm;
        return tempo;
    } catch (const std::exception&) {
        addError("Invalid tempo: " + tempo_str);
        Tempo tempo;
        tempo.note_value = Fraction(1, 4);
        tempo.bpm = 120;
        return tempo;
    }
}

Fraction ABCHeaderParser::parseNoteDuration(const std::string& duration_str) {
    std::string trimmed = trim(duration_str);
    
    std::smatch matches;
    if (std::regex_match(trimmed, matches, fraction_regex_)) {
        int num = std::stoi(matches[1].str());
        int denom = std::stoi(matches[2].str());
        return Fraction(num, denom);
    }
    
    // Try parsing as just a number
    try {
        int num = std::stoi(trimmed);
        return Fraction(num, 1);
    } catch (const std::exception&) {
        addError("Invalid duration: " + duration_str);
        return Fraction(1, 4); // Default to quarter note
    }
}

void ABCHeaderParser::reset() {
    end_of_header_ = false;
}

bool ABCHeaderParser::parseKeyWithMode(const std::string& key_spec, std::string& key, std::string& mode) {
    size_t space_pos = key_spec.find(' ');
    if (space_pos != std::string::npos) {
        key = trim(key_spec.substr(0, space_pos));
        mode = toLowerCase(trim(key_spec.substr(space_pos + 1)));
        return true;
    } else {
        key = trim(key_spec);
        mode = "major";
        return true;
    }
}

Fraction ABCHeaderParser::parseFraction(const std::string& fraction_str) {
    std::smatch matches;
    if (std::regex_match(fraction_str, matches, fraction_regex_)) {
        int num = std::stoi(matches[1].str());
        int denom = std::stoi(matches[2].str());
        return Fraction(num, denom);
    }
    
    addError("Invalid fraction: " + fraction_str);
    return Fraction(1, 1);
}

void ABCHeaderParser::addError(const std::string& message) {
    if (error_callback_) {
        error_callback_(message);
    }
}

void ABCHeaderParser::addWarning(const std::string& message) {
    if (warning_callback_) {
        warning_callback_(message);
    }
}

std::string ABCHeaderParser::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string ABCHeaderParser::toLowerCase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), 
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

bool ABCHeaderParser::parseVoiceAttributes(const std::string& attr_str, VoiceContext& voice) {
    size_t pos = 0;
    
    while (pos < attr_str.length()) {
        // Skip whitespace
        while (pos < attr_str.length() && std::isspace(attr_str[pos])) {
            pos++;
        }
        if (pos >= attr_str.length()) break;
        
        // Find the key=value pair
        size_t eq_pos = attr_str.find('=', pos);
        if (eq_pos == std::string::npos) break;
        
        std::string key = attr_str.substr(pos, eq_pos - pos);
        key = trim(key);
        pos = eq_pos + 1;
        
        // Skip whitespace after =
        while (pos < attr_str.length() && std::isspace(attr_str[pos])) {
            pos++;
        }
        if (pos >= attr_str.length()) break;
        
        std::string value;
        
        // Handle quoted values properly - THIS IS THE KEY FIX
        if (attr_str[pos] == '"') {
            pos++; // Skip opening quote
            size_t end_quote = attr_str.find('"', pos);
            if (end_quote != std::string::npos) {
                value = attr_str.substr(pos, end_quote - pos);
                pos = end_quote + 1;
            } else {
                // Unterminated quote, take rest of string
                value = attr_str.substr(pos);
                pos = attr_str.length();
                addWarning("Unterminated quote in voice attribute");
            }
        } else {
            // Unquoted value - take until space or end
            size_t end_pos = pos;
            while (end_pos < attr_str.length() && !std::isspace(attr_str[end_pos])) {
                end_pos++;
            }
            value = attr_str.substr(pos, end_pos - pos);
            pos = end_pos;
        }
        
        // Process the attribute
        if (key == "name") {
            voice.short_name = value; // Display name
        } else if (key == "sname" || key == "short") {
            voice.short_name = value;
        } else if (key == "instrument") {
            try {
                voice.instrument = std::stoi(value);
            } catch (const std::exception&) {
                addWarning("Invalid instrument value: " + value);
            }
        } else if (key == "clef") {
            std::string lower = toLowerCase(value);
            if (lower == "treble") voice.clef = ClefType::TREBLE;
            else if (lower == "bass") voice.clef = ClefType::BASS;
            else if (lower == "alto") voice.clef = ClefType::ALTO;
            else if (lower == "tenor") voice.clef = ClefType::TENOR;
            else addWarning("Unknown clef: " + value);
        } else if (key == "transpose") {
            try {
                voice.transpose = std::stoi(value);
            } catch (const std::exception&) {
                addWarning("Invalid transpose value: " + value);
            }
        } else if (key == "octave") {
            try {
                voice.octave_shift = std::stoi(value);
            } catch (const std::exception&) {
                addWarning("Invalid octave value: " + value);
            }
        } else {
            addWarning("Unknown voice attribute: " + key);
        }
    }
    
    return true;
}

} // namespace ABCPlayer