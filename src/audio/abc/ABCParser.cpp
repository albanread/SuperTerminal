#include "ABCParser.h"
#include "expand_abc_repeats.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

namespace ABCPlayer {

ABCParser::ABCParser() 
    : state_(ParseState::HEADER)
    , current_line_(0)
    , debug_output_(false) {
    initializeParsers();
}

ABCParser::~ABCParser() = default;

bool ABCParser::parseABC(const std::string& abc_content, ABCTune& tune) {
    clearErrors();
    
    // First, expand repeats at the ABC text level
    std::string expanded_content = expandABCRepeats(abc_content);
    
    // Reset all state
    resetState();
    
    // Split content into lines and parse
    std::vector<std::string> lines = splitIntoLines(expanded_content);
    
    for (const std::string& line : lines) {
        current_line_++;
        
        if (shouldIgnoreLine(line)) {
            continue;
        }
        
        std::string clean_line = stripComments(line);
        clean_line = trim(clean_line);
        
        if (clean_line.empty()) {
            continue;
        }
        
        try {
            parseLine(clean_line, tune);
        } catch (const std::exception& e) {
            addError("Exception on line " + std::to_string(current_line_) + ": " + e.what());
        }
    }
    
    // Finalize tune if no errors
    if (errors_.empty()) {
        // Ensure we have at least one voice
        if (tune.voices.empty()) {
            VoiceContext default_voice;
            default_voice.voice_number = 1;
            default_voice.name = "1";
            voice_manager_->initializeVoiceFromDefaults(default_voice, tune);
            tune.voices[1] = default_voice;
        }
        
        state_ = ParseState::COMPLETE;
    }
    
    return errors_.empty();
}

bool ABCParser::parseABCFile(const std::string& filename, ABCTune& tune) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        addError("Cannot open file: " + filename);
        return false;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    
    return parseABC(content, tune);
}

void ABCParser::parseLine(const std::string& line, ABCTune& tune) {
    if (debug_output_) {
        std::cout << "Line " << current_line_ << ": " << line << std::endl;
    }
    
    // Save current voice time before any voice changes
    int prev_voice = voice_manager_->getCurrentVoice();
    
    // Check if this is a [V:...] voice switch line (only if it's standalone)
    // If there's content after [V:], let the music parser handle the inline switch
    if (line.length() >= 4 && line[0] == '[' && line[1] == 'V' && line[2] == ':') {
        size_t end_bracket = line.find(']');
        if (end_bracket != std::string::npos) {
            // Check if there's any non-whitespace content after the closing bracket
            bool has_content_after = false;
            for (size_t i = end_bracket + 1; i < line.length(); ++i) {
                if (line[i] != ' ' && line[i] != '\t') {
                    has_content_after = true;
                    break;
                }
            }
            
            // Only handle here if it's a standalone [V:] line (no notes after it)
            if (!has_content_after) {
                std::string voice_id = line.substr(3, end_bracket - 3);
                voice_id = trim(voice_id);
                
                // Convert numbered voices to voice_N format
                try {
                    int voice_num = std::stoi(voice_id);
                    voice_id = "voice_" + voice_id;
                } catch (...) {
                    // Not a number, keep as is
                }
                
                if (!voice_id.empty()) {
                    // Save current time for previous voice
                    voice_manager_->saveCurrentTime(music_parser_->getCurrentTime());
                    
                    int new_voice = voice_manager_->switchToVoice(voice_id, tune);
                    
                    // Restore time for new voice (will be 0.0 if first time seeing this voice)
                    double restored_time = voice_manager_->restoreVoiceTime(new_voice);
                    music_parser_->setCurrentTime(restored_time);
                }
                return;
            }
            // If has_content_after, fall through to let music parser handle the line
        }
    }
    
    // Check if this is a V: field (voice switch/definition)
    // V: can define voices after K: field but before music starts
    // Once music starts, V: switches to existing voice
    if (line.length() >= 2 && line[0] == 'V' && line[1] == ':') {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string voice_spec = line.substr(colon_pos + 1);
            voice_spec = trim(voice_spec);
            
            // Extract just the voice identifier (first word)
            std::istringstream stream(voice_spec);
            std::string voice_id;
            stream >> voice_id;
            
            if (!voice_id.empty()) {
                // Check if line has attributes (instrument=, name=, etc)
                // If it has attributes, it's a definition, otherwise it's a switch
                bool has_attributes = (voice_spec.find('=') != std::string::npos);
                
                if (has_attributes) {
                    // This is a voice definition (can be in header or just after K:)
                    bool success = header_parser_->parseHeaderLine(line, tune);
                    if (success) {
                        // Convert numbered voice_id to voice_N format for lookup
                        // (header parser converts "1" to "voice_1" internally)
                        std::string lookup_id = voice_id;
                        try {
                            int voice_num = std::stoi(voice_id);
                            lookup_id = "voice_" + voice_id;
                        } catch (...) {
                            // Not a number, keep as is
                        }
                        
                        // Set current voice to the one just created
                        int found_voice_id = voice_manager_->findVoiceByIdentifier(lookup_id, tune);
                        if (found_voice_id != 0) {
                            // Register the voice with voice manager so it can be found later
                            voice_manager_->registerExternalVoice(found_voice_id, lookup_id);
                            voice_manager_->setCurrentVoice(found_voice_id);
                        }
                    }
                } else {
                    // This is a voice switch (no attributes)
                    // Save current time for previous voice
                    voice_manager_->saveCurrentTime(music_parser_->getCurrentTime());
                    
                    int new_voice = voice_manager_->switchToVoice(voice_id, tune);
                    
                    // Restore time for new voice (will be 0.0 if first time seeing this voice)
                    double restored_time = voice_manager_->restoreVoiceTime(new_voice);
                    music_parser_->setCurrentTime(restored_time);
                }
            }
        }
        return;
    }
    
    // Check if this is another header field (but not V:)
    if (state_ == ParseState::HEADER && header_parser_->isHeaderField(line)) {
        bool success = header_parser_->parseHeaderLine(line, tune);
        
        // Check if header parsing is complete (K: field marks end)
        if (success && header_parser_->isEndOfHeader()) {
            state_ = ParseState::BODY;
            // Ensure we have a default voice if none defined
            if (tune.voices.empty()) {
                voice_manager_->switchToVoice("1", tune);
            }
        }
        
        return;
    }
    
    // If we're still in header state but encounter non-header content,
    // transition to body parsing
    if (state_ == ParseState::HEADER) {
        state_ = ParseState::BODY;
        // Ensure we have a default voice
        if (tune.voices.empty()) {
            voice_manager_->switchToVoice("1", tune);
        }
    }
    
    // Parse as music content
    if (state_ == ParseState::BODY) {
        // Set current line in music parser for error reporting
        music_parser_->setLineNumber(current_line_);
        
        bool success = music_parser_->parseMusicLine(line, tune, *voice_manager_);
        
        if (!success && debug_output_) {
            std::cout << "Warning: Failed to parse music line: " << line << std::endl;
        }
    }
}

void ABCParser::initializeParsers() {
    // Create component parsers
    voice_manager_ = std::make_unique<ABCVoiceManager>();
    header_parser_ = std::make_unique<ABCHeaderParser>();
    music_parser_ = std::make_unique<ABCMusicParser>();
    
    // Set up error callback chains
    auto error_cb = [this](const std::string& msg) { this->addError(msg); };
    auto warning_cb = [this](const std::string& msg) { this->addWarning(msg); };
    
    voice_manager_->setErrorCallback(error_cb);
    voice_manager_->setWarningCallback(warning_cb);
    
    header_parser_->setErrorCallback(error_cb);
    header_parser_->setWarningCallback(warning_cb);
    
    music_parser_->setErrorCallback(error_cb);
    music_parser_->setWarningCallback(warning_cb);
}

void ABCParser::resetState() {
    state_ = ParseState::HEADER;
    current_line_ = 0;
    
    // Reset component parsers
    voice_manager_->reset();
    header_parser_->reset();
    music_parser_->reset();
}

void ABCParser::addError(const std::string& message) {
    std::string formatted = "Line " + std::to_string(current_line_) + ": " + message;
    errors_.push_back(formatted);
    
    if (debug_output_) {
        std::cout << "ERROR: " << formatted << std::endl;
    }
}

void ABCParser::addWarning(const std::string& message) {
    std::string formatted = "Line " + std::to_string(current_line_) + ": " + message;
    warnings_.push_back(formatted);
    
    if (debug_output_) {
        std::cout << "WARNING: " << formatted << std::endl;
    }
}

std::string ABCParser::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> ABCParser::splitIntoLines(const std::string& content) {
    std::vector<std::string> lines;
    std::istringstream stream(content);
    std::string line;
    
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    
    return lines;
}

bool ABCParser::shouldIgnoreLine(const std::string& line) {
    std::string trimmed = trim(line);
    
    // Ignore empty lines
    if (trimmed.empty()) {
        return true;
    }
    
    // Ignore comment lines
    if (trimmed[0] == '%') {
        return true;
    }
    
    return false;
}

std::string ABCParser::stripComments(const std::string& line) {
    size_t comment_pos = line.find('%');
    if (comment_pos != std::string::npos) {
        return line.substr(0, comment_pos);
    }
    return line;
}

} // namespace ABCPlayer