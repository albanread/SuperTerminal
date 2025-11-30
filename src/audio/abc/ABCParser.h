#ifndef ABC_PARSER_NEW_H
#define ABC_PARSER_NEW_H

#include "ABCTypes.h"
#include "ABCVoiceManager.h"
#include "ABCHeaderParser.h"
#include "ABCMusicParser.h"
#include <string>
#include <vector>
#include <memory>

namespace ABCPlayer {

class ABCParser {
public:
    static const char* getVersion() { return "Snafu102"; }
    
private:
public:
    ABCParser();
    ~ABCParser();
    
    // Main parsing interface
    bool parseABC(const std::string& abc_content, ABCTune& tune);
    bool parseABCFile(const std::string& filename, ABCTune& tune);
    
    // Error reporting
    const std::vector<std::string>& getErrors() const { return errors_; }
    const std::vector<std::string>& getWarnings() const { return warnings_; }
    void clearErrors() { errors_.clear(); warnings_.clear(); }
    
    // Debug output
    void setDebugOutput(bool debug) { debug_output_ = debug; }
    bool getDebugOutput() const { return debug_output_; }
    
private:
    // Parsing state
    enum class ParseState {
        HEADER,
        BODY,
        COMPLETE
    };
    
    ParseState state_;
    int current_line_;
    bool debug_output_;
    
    // Component parsers
    std::unique_ptr<ABCVoiceManager> voice_manager_;
    std::unique_ptr<ABCHeaderParser> header_parser_;
    std::unique_ptr<ABCMusicParser> music_parser_;
    
    // Error collection
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;
    
    // Main parsing methods
    void parseLine(const std::string& line, ABCTune& tune);
    void initializeParsers();
    void resetState();
    
    // Error reporting
    void addError(const std::string& message);
    void addWarning(const std::string& message);
    
    // Utility methods
    std::string trim(const std::string& str);
    std::vector<std::string> splitIntoLines(const std::string& content);
    bool shouldIgnoreLine(const std::string& line);
    std::string stripComments(const std::string& line);
};

} // namespace ABCPlayer

#endif // ABC_PARSER_NEW_H