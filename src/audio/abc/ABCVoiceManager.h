#ifndef ABC_VOICE_MANAGER_H
#define ABC_VOICE_MANAGER_H

#include "ABCTypes.h"
#include <string>
#include <map>
#include <vector>

namespace ABCPlayer {

class ABCVoiceManager {
public:
    ABCVoiceManager();
    ~ABCVoiceManager();
    
    // Voice definition and management
    bool defineVoice(const std::string& voice_spec, ABCTune& tune);
    int switchToVoice(const std::string& voice_identifier, ABCTune& tune);
    int getCurrentVoice() const { return current_voice_; }
    void setCurrentVoice(int voice_id) { current_voice_ = voice_id; }
    
    // Register a voice that was created externally (e.g., by header parser)
    void registerExternalVoice(int voice_id, const std::string& identifier);
    
    // Voice attribute parsing (this fixes the quoted string issue)
    bool parseVoiceAttributes(const std::string& attr_str, VoiceContext& voice);
    
    // Voice lookup and creation
    int findVoiceByIdentifier(const std::string& identifier, const ABCTune& tune);
    int getOrCreateVoice(const std::string& identifier, ABCTune& tune);
    
    // Voice time management
    void saveCurrentTime(double time);
    double restoreVoiceTime(int voice_id);
    
    // State management
    void reset();
    void initializeVoiceFromDefaults(VoiceContext& voice, const ABCTune& tune);
    
    // Error reporting callback
    void setErrorCallback(std::function<void(const std::string&)> error_cb) { 
        error_callback_ = error_cb; 
    }
    void setWarningCallback(std::function<void(const std::string&)> warning_cb) { 
        warning_callback_ = warning_cb; 
    }
    
private:
    int current_voice_;
    int next_voice_id_;
    std::map<int, double> voice_times_;
    std::map<std::string, int> voice_name_to_id_;
    
    // Error reporting
    std::function<void(const std::string&)> error_callback_;
    std::function<void(const std::string&)> warning_callback_;
    
    // Helper methods
    void addError(const std::string& message);
    void addWarning(const std::string& message);
    std::string trim(const std::string& str);
    std::string toLowerCase(const std::string& str);
    
    // Attribute parsing helpers
    ClefType parseClef(const std::string& clef_str);
    int parseInstrument(const std::string& instrument_str);
    
    // Standard clef and instrument mappings
    static const std::map<std::string, ClefType> clef_types_;
    static const std::map<std::string, int> standard_instruments_;
};

} // namespace ABCPlayer

#endif // ABC_VOICE_MANAGER_H