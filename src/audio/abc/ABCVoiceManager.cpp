#include "ABCVoiceManager.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace ABCPlayer {

// Static member definitions
const std::map<std::string, ClefType> ABCVoiceManager::clef_types_ = {
    {"treble", ClefType::TREBLE},
    {"bass", ClefType::BASS},
    {"alto", ClefType::ALTO},
    {"tenor", ClefType::TENOR}
};

const std::map<std::string, int> ABCVoiceManager::standard_instruments_ = {
    {"piano", 0}, {"acoustic_grand_piano", 0}, {"bright_acoustic_piano", 1},
    {"electric_grand_piano", 2}, {"honky_tonk_piano", 3}, {"electric_piano_1", 4},
    {"electric_piano_2", 5}, {"harpsichord", 6}, {"clavinet", 7},
    {"celesta", 8}, {"glockenspiel", 9}, {"music_box", 10}, {"vibraphone", 11},
    {"marimba", 12}, {"xylophone", 13}, {"tubular_bells", 14}, {"dulcimer", 15},
    {"drawbar_organ", 16}, {"percussive_organ", 17}, {"rock_organ", 18},
    {"church_organ", 19}, {"reed_organ", 20}, {"accordion", 21}, {"harmonica", 22},
    {"tango_accordion", 23}, {"acoustic_guitar_nylon", 24}, {"acoustic_guitar_steel", 25},
    {"electric_guitar_jazz", 26}, {"electric_guitar_clean", 27}, {"electric_guitar_muted", 28},
    {"overdriven_guitar", 29}, {"distortion_guitar", 30}, {"guitar_harmonics", 31},
    {"acoustic_bass", 32}, {"electric_bass_finger", 33}, {"electric_bass_pick", 34},
    {"fretless_bass", 35}, {"slap_bass_1", 36}, {"slap_bass_2", 37}, {"synth_bass_1", 38},
    {"synth_bass_2", 39}, {"violin", 40}, {"viola", 41}, {"cello", 42}, {"contrabass", 43},
    {"tremolo_strings", 44}, {"pizzicato_strings", 45}, {"orchestral_harp", 46},
    {"timpani", 47}, {"string_ensemble_1", 48}, {"string_ensemble_2", 49},
    {"synth_strings_1", 50}, {"synth_strings_2", 51}, {"choir_aahs", 52}, {"voice_oohs", 53},
    {"synth_choir", 54}, {"orchestra_hit", 55}, {"trumpet", 56}, {"trombone", 57},
    {"tuba", 58}, {"muted_trumpet", 59}, {"french_horn", 60}, {"brass_section", 61},
    {"synth_brass_1", 62}, {"synth_brass_2", 63}, {"soprano_sax", 64}, {"alto_sax", 65},
    {"tenor_sax", 66}, {"baritone_sax", 67}, {"oboe", 68}, {"english_horn", 69},
    {"bassoon", 70}, {"clarinet", 71}, {"piccolo", 72}, {"flute", 73}, {"recorder", 74},
    {"pan_flute", 75}, {"blown_bottle", 76}, {"shakuhachi", 77}, {"whistle", 78},
    {"ocarina", 79}, {"lead_1_square", 80}, {"lead_2_sawtooth", 81}, {"lead_3_calliope", 82},
    {"lead_4_chiff", 83}, {"lead_5_charang", 84}, {"lead_6_voice", 85}, {"lead_7_fifths", 86},
    {"lead_8_bass_lead", 87}
};

ABCVoiceManager::ABCVoiceManager() 
    : current_voice_(1), next_voice_id_(1) {
}

ABCVoiceManager::~ABCVoiceManager() = default;

bool ABCVoiceManager::defineVoice(const std::string& voice_spec, ABCTune& tune) {
    // Parse voice specification like "V:1" or "V:melody name="Lead" instrument=73"
    std::istringstream stream(voice_spec);
    std::string voice_identifier;
    stream >> voice_identifier;
    
    int voice_id;
    std::string voice_name = voice_identifier;
    
    // Try to parse as number first
    try {
        voice_id = std::stoi(voice_identifier);
        voice_name = ""; // Clear name for numbered voices
    } catch (const std::exception&) {
        // Not a number, treat as name and assign sequential ID
        voice_id = next_voice_id_++;
        voice_name = voice_identifier;
    }
    
    // Save current time for previous voice
    if (current_voice_ != 0) {
        voice_times_[current_voice_] = 0.0; // Will be set by music parser
    }
    
    current_voice_ = voice_id;
    
    // Create or get voice context
    if (tune.voices.find(voice_id) == tune.voices.end()) {
        VoiceContext voice;
        voice.voice_number = voice_id;
        voice.name = voice_name; // Store identifier for [V:name] lookup
        initializeVoiceFromDefaults(voice, tune);
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
    
    // Update name mapping
    if (!voice_name.empty()) {
        voice_name_to_id_[voice_name] = voice_id;
    }
    
    return true;
}

int ABCVoiceManager::switchToVoice(const std::string& voice_identifier, ABCTune& tune) {
    int voice_id = findVoiceByIdentifier(voice_identifier, tune);
    
    if (voice_id == 0) {
        // Voice not found, create it
        voice_id = getOrCreateVoice(voice_identifier, tune);
    }
    
    // Save current time for previous voice (will be set by music parser)
    if (current_voice_ != 0) {
        // Time will be managed by music parser
    }
    
    current_voice_ = voice_id;
    return voice_id;
}

bool ABCVoiceManager::parseVoiceAttributes(const std::string& attr_str, VoiceContext& voice) {
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
            // Keep the identifier in voice.name for lookup
        } else if (key == "sname" || key == "short") {
            voice.short_name = value;
        } else if (key == "instrument") {
            voice.instrument = parseInstrument(value);
        } else if (key == "clef") {
            voice.clef = parseClef(value);
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

int ABCVoiceManager::findVoiceByIdentifier(const std::string& identifier, const ABCTune& tune) {
    // Try to parse as number first
    try {
        int voice_id = std::stoi(identifier);
        if (tune.voices.find(voice_id) != tune.voices.end()) {
            return voice_id;
        }
    } catch (...) {
        // Not a number, continue with name lookup
    }
    
    // Find existing voice by name or identifier
    for (const auto& pair : tune.voices) {
        const VoiceContext& voice = pair.second;
        if (voice.name == identifier || voice.short_name == identifier) {
            return pair.first;
        }
    }
    
    // Check name mapping
    auto it = voice_name_to_id_.find(identifier);
    if (it != voice_name_to_id_.end()) {
        return it->second;
    }
    
    return 0; // Voice not found
}

int ABCVoiceManager::getOrCreateVoice(const std::string& identifier, ABCTune& tune) {
    // Check if voice already exists
    int existing_id = findVoiceByIdentifier(identifier, tune);
    if (existing_id != 0) {
        return existing_id;
    }
    
    // Create new voice
    int voice_id = next_voice_id_++;
    VoiceContext voice;
    voice.voice_number = voice_id;
    voice.name = identifier;
    initializeVoiceFromDefaults(voice, tune);
    tune.voices[voice_id] = voice;
    
    // Update mapping
    voice_name_to_id_[identifier] = voice_id;
    
    return voice_id;
}

void ABCVoiceManager::registerExternalVoice(int voice_id, const std::string& identifier) {
    // Update the name-to-id mapping for a voice that was created externally
    // (e.g., by the header parser)
    voice_name_to_id_[identifier] = voice_id;
    
    // Update next_voice_id if needed to avoid collisions
    if (voice_id >= next_voice_id_) {
        next_voice_id_ = voice_id + 1;
    }
}

void ABCVoiceManager::saveCurrentTime(double time) {
    if (current_voice_ != 0) {
        voice_times_[current_voice_] = time;
    }
}

double ABCVoiceManager::restoreVoiceTime(int voice_id) {
    auto it = voice_times_.find(voice_id);
    if (it != voice_times_.end()) {
        return it->second;
    }
    return 0.0; // Start at beginning for new voice
}

void ABCVoiceManager::reset() {
    current_voice_ = 1;
    next_voice_id_ = 1;
    voice_times_.clear();
    voice_name_to_id_.clear();
}

void ABCVoiceManager::initializeVoiceFromDefaults(VoiceContext& voice, const ABCTune& tune) {
    voice.key = tune.default_key;
    voice.timesig = tune.default_timesig;
    voice.unit_length = tune.default_unit_length;
    voice.clef = ClefType::TREBLE;
    voice.transpose = 0;
    voice.octave_shift = 0;
    voice.instrument = 0;
    voice.channel = -1; // Will be assigned by MIDI generator
    voice.velocity = 80; // Default velocity
}

ClefType ABCVoiceManager::parseClef(const std::string& clef_str) {
    std::string lower = toLowerCase(clef_str);
    
    auto it = clef_types_.find(lower);
    if (it != clef_types_.end()) {
        return it->second;
    }
    
    addWarning("Unknown clef: " + clef_str + ", defaulting to treble");
    return ClefType::TREBLE;
}

int ABCVoiceManager::parseInstrument(const std::string& instrument_str) {
    // Try to parse as number first
    try {
        int instrument = std::stoi(instrument_str);
        return std::max(0, std::min(127, instrument)); // Clamp to MIDI range
    } catch (const std::exception&) {
        // Try to find by name
        std::string lower = toLowerCase(instrument_str);
        auto it = standard_instruments_.find(lower);
        if (it != standard_instruments_.end()) {
            return it->second;
        }
    }
    
    addWarning("Unknown instrument: " + instrument_str + ", defaulting to piano");
    return 0; // Piano
}

void ABCVoiceManager::addError(const std::string& message) {
    if (error_callback_) {
        error_callback_(message);
    }
}

void ABCVoiceManager::addWarning(const std::string& message) {
    if (warning_callback_) {
        warning_callback_(message);
    }
}

std::string ABCVoiceManager::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string ABCVoiceManager::toLowerCase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), 
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

} // namespace ABCPlayer