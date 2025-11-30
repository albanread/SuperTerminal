#ifndef ABC_HEADER_PARSER_H
#define ABC_HEADER_PARSER_H

#include "ABCTypes.h"
#include <string>
#include <vector>
#include <regex>
#include <functional>

namespace ABCPlayer {

class ABCHeaderParser {
public:
    ABCHeaderParser();
    ~ABCHeaderParser();
    
    // Main header parsing interface
    bool parseHeaderLine(const std::string& line, ABCTune& tune);
    bool isHeaderField(const std::string& line) const;
    
    // Individual field parsers
    bool parseXField(const std::string& content, ABCTune& tune);  // Index number
    bool parseTField(const std::string& content, ABCTune& tune);  // Title
    bool parseCField(const std::string& content, ABCTune& tune);  // Composer
    bool parseOField(const std::string& content, ABCTune& tune);  // Origin
    bool parsePField(const std::string& content, ABCTune& tune);  // Parts
    bool parseKField(const std::string& content, ABCTune& tune);  // Key signature
    bool parseLField(const std::string& content, ABCTune& tune);  // Unit length
    bool parseMField(const std::string& content, ABCTune& tune);  // Time signature
    bool parseQField(const std::string& content, ABCTune& tune);  // Tempo
    bool parseVField(const std::string& content, ABCTune& tune);  // Voice definition
    
    // Key signature parsing
    KeySignature parseKeySignature(const std::string& key_str);
    TimeSignature parseTimeSignature(const std::string& time_str);
    Tempo parseTempo(const std::string& tempo_str);
    
    // Duration parsing
    Fraction parseNoteDuration(const std::string& duration_str);
    
    // State management
    void reset();
    bool isEndOfHeader() const { return end_of_header_; }
    
    // Error reporting callbacks
    void setErrorCallback(std::function<void(const std::string&)> error_cb) { 
        error_callback_ = error_cb; 
    }
    void setWarningCallback(std::function<void(const std::string&)> warning_cb) { 
        warning_callback_ = warning_cb; 
    }
    
private:
    bool end_of_header_;
    
    // Error reporting
    std::function<void(const std::string&)> error_callback_;
    std::function<void(const std::string&)> warning_callback_;
    
    // Helper methods
    void addError(const std::string& message);
    void addWarning(const std::string& message);
    std::string trim(const std::string& str);
    std::string toLowerCase(const std::string& str);
    
    // Parsing helpers
    bool parseKeyWithMode(const std::string& key_spec, std::string& key, std::string& mode);
    Fraction parseFraction(const std::string& fraction_str);
    bool parseVoiceAttributes(const std::string& attr_str, VoiceContext& voice);
    
    // Regular expressions for parsing
    static const std::regex header_field_regex_;
    static const std::regex fraction_regex_;
    static const std::regex tempo_regex_;
    
    // Standard key signatures
    static const std::map<std::string, KeySignature> standard_keys_;
};

} // namespace ABCPlayer

#endif // ABC_HEADER_PARSER_H