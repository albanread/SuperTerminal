#ifndef ABC_PLAYER_H
#define ABC_PLAYER_H

#include "ABCTypes.h"
#include "ABCParser.h"
#include "MIDIGenerator.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace ABCPlayer {

// Playback state
enum class PlaybackState {
    STOPPED,
    PLAYING,
    PAUSED,
    ERROR
};

// Audio callback interface
class AudioCallback {
public:
    virtual ~AudioCallback() = default;
    virtual void onNoteOn(int midi_note, int velocity, int channel, double timestamp) = 0;
    virtual void onNoteOff(int midi_note, int channel, double timestamp) = 0;
    virtual void onProgramChange(int program, int channel, double timestamp) = 0;
    virtual void onTempo(int bpm, double timestamp) = 0;
    virtual void onPlaybackStarted() = 0;
    virtual void onPlaybackStopped() = 0;
    virtual void onPlaybackError(const std::string& error) = 0;
};

// Main ABC Player class - coordinates parsing and playback
class Player {
public:
    Player();
    ~Player();
    
    // Core functionality
    bool loadABC(const std::string& abc_content);
    bool loadABCFile(const std::string& filename);
    bool play();
    bool pause();
    bool stop();
    bool exportMIDI(const std::string& filename);
    
    // Playback control
    void setTempo(int bpm);
    void setVolume(float volume);        // 0.0 to 1.0
    void seek(double position_seconds);
    double getCurrentPosition() const;   // in seconds
    double getTotalDuration() const;     // in seconds
    
    // Voice control
    void muteVoice(int voice_id, bool muted);
    void setVoiceVolume(int voice_id, float volume);
    void setVoiceInstrument(int voice_id, int instrument);
    std::vector<int> getVoiceList() const;
    VoiceContext getVoiceInfo(int voice_id) const;
    
    // Configuration
    void setTicksPerQuarter(int ticks);
    void setDefaultVelocity(int velocity);
    void setAudioCallback(std::shared_ptr<AudioCallback> callback);
    void setSynchronousMode(bool sync_mode); // Enable main-thread playback
    
    // Tune information
    const ABCTune& getCurrentTune() const { return current_tune_; }
    std::string getTuneTitle() const;
    std::string getTuneComposer() const;
    int getNumVoices() const;
    
    // State queries
    PlaybackState getPlaybackState() const { return playback_state_; }
    bool isLoaded() const { return tune_loaded_; }
    bool isPlaying() const { return playback_state_ == PlaybackState::PLAYING; }
    
    // Version info
    std::string getParserVersion() const;
    
    // Thread-safe logging
    void processLogQueue();
    
    // Error reporting
    const std::vector<std::string>& getErrors() const { return errors_; }
    const std::vector<std::string>& getWarnings() const { return warnings_; }
    void clearErrors();
    
    // Analysis and debugging
    bool generateReport(const std::string& filename);
    void setVerbose(bool verbose) { verbose_ = verbose; }
    
    // Utility methods
    std::string formatTime(double seconds) const;
    double convertBeatsToSeconds(double beats, const Tempo& tempo, const Fraction& unit_length) const;
    
private:
    // Components
    std::unique_ptr<ABCParser> parser_;
    std::unique_ptr<MIDIGenerator> midi_generator_;
    std::shared_ptr<AudioCallback> audio_callback_;
    
    // Current state
    ABCTune current_tune_;
    std::vector<MIDITrack> midi_tracks_;
    PlaybackState playback_state_;
    bool tune_loaded_;
    bool verbose_;
    bool synchronous_mode_; // If true, playback runs on calling thread
    
    // Playback timing
    double current_position_;        // Current playback position in seconds
    double total_duration_;          // Total duration in seconds
    int current_tempo_;              // Current tempo in BPM
    float master_volume_;            // Master volume (0.0 to 1.0)
    
    // Voice management
    std::map<int, bool> voice_muted_;
    std::map<int, float> voice_volumes_;
    std::map<int, int> voice_instruments_;
    
    // Error collection
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;
    
#ifdef __APPLE__
    // macOS Core Audio components
    AUGraph audio_graph_;
    AudioUnit synth_unit_;
    AudioUnit output_unit_;
    AUNode synth_node_;
    AUNode output_node_;
    bool audio_initialized_;
    
    // Playback thread
    std::thread playback_thread_;
    std::atomic<bool> should_stop_playback_;
    std::mutex playback_mutex_;
    
    // Audio setup
    bool initializeAudio();
    void shutdownAudio();
    bool setupAudioGraph();
    void startAudioPlayback();
    void stopAudioPlayback();
    
    // MIDI playback
    void playbackThreadFunc();
    void scheduleMIDIEvents();
    void sendMIDIEvent(const MIDIEvent& event);
    
    // Synchronous playback (main thread)
    bool playSynchronous();
    
    // Core Audio callbacks
    static OSStatus renderCallback(void* inRefCon,
                                 AudioUnitRenderActionFlags* ioActionFlags,
                                 const AudioTimeStamp* inTimeStamp,
                                 UInt32 inBusNumber,
                                 UInt32 inNumberFrames,
                                 AudioBufferList* ioData);
#endif
    
    // Internal methods
    bool validateTune();
    void calculateTotalDuration();
    void resetPlaybackState();
    void updateVoiceSettings();
    
    // Phase 1: Parsing
    bool parsePhase();
    
    // Phase 2: MIDI Generation
    bool generatePhase();
    
    // Error handling
    void addError(const std::string& message);
    void addWarning(const std::string& message);
    void collectErrors();
    
    // Utility methods
    double beatsToSeconds(double beats, int tempo) const;
    double secondsToBeats(double seconds, int tempo) const;
};

// Default audio callback implementation for simple playback
class DefaultAudioCallback : public AudioCallback {
public:
    DefaultAudioCallback();
    virtual ~DefaultAudioCallback();
    
    void onNoteOn(int midi_note, int velocity, int channel, double timestamp) override;
    void onNoteOff(int midi_note, int channel, double timestamp) override;
    void onProgramChange(int program, int channel, double timestamp) override;
    void onTempo(int bpm, double timestamp) override;
    void onPlaybackStarted() override;
    void onPlaybackStopped() override;
    void onPlaybackError(const std::string& error) override;
    
    void setVerbose(bool verbose) { verbose_ = verbose; }
    
private:
    bool verbose_;
};

// Inline utility implementations
inline void Player::addError(const std::string& message) {
    errors_.push_back(message);
}

inline void Player::addWarning(const std::string& message) {
    warnings_.push_back(message);
}

inline std::string Player::getTuneTitle() const {
    return current_tune_.title;
}

inline std::string Player::getTuneComposer() const {
    return current_tune_.composer;
}

inline int Player::getNumVoices() const {
    return static_cast<int>(current_tune_.voices.size());
}

inline std::vector<int> Player::getVoiceList() const {
    std::vector<int> voices;
    for (const auto& pair : current_tune_.voices) {
        voices.push_back(pair.first);
    }
    return voices;
}

inline double Player::beatsToSeconds(double beats, int tempo) const {
    return beats * 60.0 / tempo;
}

inline double Player::secondsToBeats(double seconds, int tempo) const {
    return seconds * tempo / 60.0;
}

inline std::string Player::formatTime(double seconds) const {
    int minutes = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    int tenths = static_cast<int>((seconds - static_cast<int>(seconds)) * 10);
    return std::to_string(minutes) + ":" + 
           (secs < 10 ? "0" : "") + std::to_string(secs) + "." + 
           std::to_string(tenths);
}

} // namespace ABCPlayer

#endif // ABC_PLAYER_H