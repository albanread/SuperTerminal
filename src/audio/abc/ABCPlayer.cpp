#include "ABCPlayer.h"
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <atomic>
#include <mutex>
#include <queue>

namespace ABCPlayer {

// Thread-safe logging system for background threads
class ThreadSafeLogger {
private:
    struct LogMessage {
        std::string message;
        bool is_error;
        LogMessage(const std::string& msg, bool err = false) : message(msg), is_error(err) {}
    };
    
    static std::queue<LogMessage> log_queue_;
    static std::mutex log_mutex_;
    
public:
    static void log(const std::string& message, bool is_error = false) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        log_queue_.emplace(message, is_error);
    }
    
    static void processAll() {
        std::lock_guard<std::mutex> lock(log_mutex_);
        while (!log_queue_.empty()) {
            const LogMessage& msg = log_queue_.front();
            if (msg.is_error) {
                std::cerr << msg.message << std::endl;
            } else {
                std::cout << msg.message << std::endl;
            }
            log_queue_.pop();
        }
    }
};

// Static member definitions
std::queue<ThreadSafeLogger::LogMessage> ThreadSafeLogger::log_queue_;
std::mutex ThreadSafeLogger::log_mutex_;

Player::Player() 
    : parser_(std::make_unique<ABCParser>()),
      midi_generator_(std::make_unique<MIDIGenerator>()),
      playback_state_(PlaybackState::STOPPED),
      tune_loaded_(false),
      verbose_(false),
      synchronous_mode_(false),
      current_position_(0.0),
      total_duration_(0.0),
      current_tempo_(120),
      master_volume_(1.0f)
#ifdef __APPLE__
      , audio_graph_(0),
      synth_unit_(0),
      output_unit_(0),
      synth_node_(0),
      output_node_(0),
      audio_initialized_(false),
      should_stop_playback_(false)
#endif
{
#ifdef __APPLE__
    initializeAudio();
#endif
}

Player::~Player() {
    stop();
    
#ifdef __APPLE__
    shutdownAudio();
#endif
}

bool Player::loadABC(const std::string& abc_content) {
    clearErrors();
    
    try {
        // Phase 1: Parse ABC content
        current_tune_ = ABCTune(); // Reset tune
        if (!parsePhase()) {
            return false;
        }
        
        if (!parser_->parseABC(abc_content, current_tune_)) {
            addError("Failed to parse ABC content");
            collectErrors();
            return false;
        }
        
        // Phase 2: Generate MIDI
        if (!generatePhase()) {
            return false;
        }
        
        tune_loaded_ = validateTune();
        if (tune_loaded_) {
            calculateTotalDuration();
            resetPlaybackState();
        }
        
        return tune_loaded_;
        
    } catch (const std::exception& e) {
        addError("Exception during ABC loading: " + std::string(e.what()));
        return false;
    }
}

bool Player::loadABCFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        addError("Cannot open ABC file: " + filename);
        return false;
    }
    
    std::string content;
    std::string line;
    while (std::getline(file, line)) {
        content += line + "\n";
    }
    
    return loadABC(content);
}

bool Player::play() {
    if (!tune_loaded_) {
        addError("No tune loaded");
        return false;
    }
    
    if (playback_state_ == PlaybackState::PLAYING) {
        return true; // Already playing
    }
    
    try {
        if (synchronous_mode_) {
            // Play synchronously on current thread
            return playSynchronous();
        } else {
#ifdef __APPLE__
            startAudioPlayback();
#endif        
        }
        
        playback_state_ = PlaybackState::PLAYING;
        
        if (audio_callback_) {
            audio_callback_->onPlaybackStarted();
        }
        
        if (verbose_) {
            std::cout << "Started playback of: " << current_tune_.title << std::endl;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        addError("Exception during playback start: " + std::string(e.what()));
        playback_state_ = PlaybackState::ERROR;
        return false;
    }
}

bool Player::pause() {
    if (playback_state_ == PlaybackState::PLAYING) {
        playback_state_ = PlaybackState::PAUSED;
        
#ifdef __APPLE__
        should_stop_playback_ = true;
        if (playback_thread_.joinable()) {
            playback_thread_.join();
        }
#endif
        
        if (verbose_) {
            std::cout << "Paused playback" << std::endl;
        }
        
        return true;
    }
    
    return false;
}

bool Player::stop() {
    if (playback_state_ == PlaybackState::STOPPED) {
        return true;
    }
    
    playback_state_ = PlaybackState::STOPPED;
    
#ifdef __APPLE__
    stopAudioPlayback();
#endif
    
    current_position_ = 0.0;
    
    if (audio_callback_) {
        audio_callback_->onPlaybackStopped();
    }
    
    if (verbose_) {
        std::cout << "Stopped playback" << std::endl;
    }
    
    return true;
}

bool Player::exportMIDI(const std::string& filename) {
    if (!tune_loaded_) {
        addError("No tune loaded");
        return false;
    }
    
    return midi_generator_->generateMIDIFile(current_tune_, filename);
}

void Player::setTempo(int bpm) {
    if (bpm > 0 && bpm <= 300) {
        current_tempo_ = bpm;
        
        // Update tempo in current tune
        current_tune_.default_tempo.bpm = bpm;
    }
}

void Player::setVolume(float volume) {
    master_volume_ = std::max(0.0f, std::min(1.0f, volume));
}

void Player::seek(double position_seconds) {
    current_position_ = std::max(0.0, std::min(total_duration_, position_seconds));
}

double Player::getCurrentPosition() const {
    return current_position_;
}

double Player::getTotalDuration() const {
    return total_duration_;
}

void Player::muteVoice(int voice_id, bool muted) {
    voice_muted_[voice_id] = muted;
    updateVoiceSettings();
}

void Player::setVoiceVolume(int voice_id, float volume) {
    voice_volumes_[voice_id] = std::max(0.0f, std::min(1.0f, volume));
    updateVoiceSettings();
}

void Player::setVoiceInstrument(int voice_id, int instrument) {
    if (instrument >= 0 && instrument <= 127) {
        voice_instruments_[voice_id] = instrument;
        
        // Update voice context
        auto it = current_tune_.voices.find(voice_id);
        if (it != current_tune_.voices.end()) {
            it->second.instrument = instrument;
        }
        
        updateVoiceSettings();
    }
}

VoiceContext Player::getVoiceInfo(int voice_id) const {
    auto it = current_tune_.voices.find(voice_id);
    if (it != current_tune_.voices.end()) {
        return it->second;
    }
    
    return VoiceContext(voice_id); // Return default if not found
}

void Player::setTicksPerQuarter(int ticks) {
    midi_generator_->setTicksPerQuarter(ticks);
}

void Player::setDefaultVelocity(int velocity) {
    if (velocity >= 0 && velocity <= 127) {
        midi_generator_->setDefaultVelocity(velocity);
    }
}

void Player::setAudioCallback(std::shared_ptr<AudioCallback> callback) {
    audio_callback_ = callback;
}

void Player::clearErrors() {
    errors_.clear();
    warnings_.clear();
    
    if (parser_) {
        parser_->clearErrors();
    }
    
    if (midi_generator_) {
        midi_generator_->clearErrors();
    }
}

bool Player::generateReport(const std::string& filename) {
    if (!tune_loaded_) {
        addError("No tune loaded");
        return false;
    }
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        addError("Cannot create report file: " + filename);
        return false;
    }
    
    try {
        file << "ABC Player Analysis Report" << std::endl;
        file << "===========================" << std::endl << std::endl;
        
        // Tune information
        file << "Tune Information:" << std::endl;
        file << "  Title: " << current_tune_.title << std::endl;
        file << "  Composer: " << current_tune_.composer << std::endl;
        file << "  Tune Number: " << current_tune_.tune_number << std::endl;
        file << "  Key: " << current_tune_.default_key.name << std::endl;
        file << "  Time Signature: " << current_tune_.default_timesig.numerator 
             << "/" << current_tune_.default_timesig.denominator << std::endl;
        file << "  Unit Length: " << current_tune_.default_unit_length.num 
             << "/" << current_tune_.default_unit_length.denom << std::endl;
        file << "  Tempo: " << current_tune_.default_tempo.bpm << " BPM" << std::endl;
        file << "  Duration: " << formatTime(total_duration_) << std::endl;
        file << std::endl;
        
        // Voice information
        file << "Voice Information:" << std::endl;
        for (const auto& voice_pair : current_tune_.voices) {
            const VoiceContext& voice = voice_pair.second;
            file << "  Voice " << voice.voice_number << ":" << std::endl;
            file << "    Name: " << voice.name << std::endl;
            file << "    Instrument: " << voice.instrument << std::endl;
            file << "    Clef: ";
            switch (voice.clef) {
                case ClefType::TREBLE: file << "Treble"; break;
                case ClefType::BASS: file << "Bass"; break;
                case ClefType::ALTO: file << "Alto"; break;
                case ClefType::TENOR: file << "Tenor"; break;
                default: file << "None"; break;
            }
            file << std::endl;
            file << "    Transpose: " << voice.transpose << " semitones" << std::endl;
            file << std::endl;
        }
        
        // Feature analysis
        file << "Feature Analysis:" << std::endl;
        std::map<FeatureType, int> feature_counts;
        for (const Feature& feature : current_tune_.features) {
            feature_counts[feature.type]++;
        }
        
        file << "  Total Features: " << current_tune_.features.size() << std::endl;
        file << "  Notes: " << feature_counts[FeatureType::NOTE] << std::endl;
        file << "  Rests: " << feature_counts[FeatureType::REST] << std::endl;
        file << "  Chords: " << feature_counts[FeatureType::CHORD] << std::endl;
        file << "  Guitar Chords: " << feature_counts[FeatureType::GCHORD] << std::endl;
        file << "  Voice Changes: " << feature_counts[FeatureType::VOICE] << std::endl;
        file << "  Bar Lines: " << feature_counts[FeatureType::SINGLE_BAR] + 
                                   feature_counts[FeatureType::DOUBLE_BAR] + 
                                   feature_counts[FeatureType::BAR_REP] + 
                                   feature_counts[FeatureType::REP_BAR] << std::endl;
        file << std::endl;
        
        // MIDI track information
        file << "MIDI Track Information:" << std::endl;
        for (const MIDITrack& track : midi_tracks_) {
            file << "  Track " << track.track_number << ":" << std::endl;
            file << "    Name: " << track.name << std::endl;
            file << "    Voice: " << track.voice_number << std::endl;
            file << "    Channel: " << track.channel << std::endl;
            file << "    Events: " << track.events.size() << std::endl;
            file << std::endl;
        }
        
        // Error and warning summary
        if (!errors_.empty() || !warnings_.empty()) {
            file << "Issues:" << std::endl;
            for (const std::string& error : errors_) {
                file << "  ERROR: " << error << std::endl;
            }
            for (const std::string& warning : warnings_) {
                file << "  WARNING: " << warning << std::endl;
            }
            file << std::endl;
        }
        
        file << "Report generated successfully." << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        addError("Exception generating report: " + std::string(e.what()));
        return false;
    }
}

#ifdef __APPLE__
bool Player::initializeAudio() {
    if (audio_initialized_) {
        return true;
    }
    
    try {
        OSStatus result = NewAUGraph(&audio_graph_);
        if (result != noErr) {
            addError("Failed to create audio graph");
            return false;
        }
        
        if (!setupAudioGraph()) {
            return false;
        }
        
        result = AUGraphInitialize(audio_graph_);
        if (result != noErr) {
            addError("Failed to initialize audio graph");
            return false;
        }
        
        audio_initialized_ = true;
        return true;
        
    } catch (const std::exception& e) {
        addError("Exception initializing audio: " + std::string(e.what()));
        return false;
    }
}

void Player::shutdownAudio() {
    if (!audio_initialized_) {
        return;
    }
    
    stopAudioPlayback();
    
    if (audio_graph_) {
        AUGraphUninitialize(audio_graph_);
        AUGraphClose(audio_graph_);
        DisposeAUGraph(audio_graph_);
        audio_graph_ = 0;
    }
    
    audio_initialized_ = false;
}

bool Player::setupAudioGraph() {
    OSStatus result;
    
    // Create audio unit descriptions
    AudioComponentDescription synth_desc = {0};
    synth_desc.componentType = kAudioUnitType_MusicDevice;
    synth_desc.componentSubType = kAudioUnitSubType_DLSSynth;
    synth_desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    
    AudioComponentDescription output_desc = {0};
    output_desc.componentType = kAudioUnitType_Output;
    output_desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    output_desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    
    // Add nodes to graph
    result = AUGraphAddNode(audio_graph_, &synth_desc, &synth_node_);
    if (result != noErr) {
        addError("Failed to add synth node to audio graph");
        return false;
    }
    
    result = AUGraphAddNode(audio_graph_, &output_desc, &output_node_);
    if (result != noErr) {
        addError("Failed to add output node to audio graph");
        return false;
    }
    
    // Open the graph
    result = AUGraphOpen(audio_graph_);
    if (result != noErr) {
        addError("Failed to open audio graph");
        return false;
    }
    
    // Get audio unit references
    result = AUGraphNodeInfo(audio_graph_, synth_node_, NULL, &synth_unit_);
    if (result != noErr) {
        addError("Failed to get synth unit reference");
        return false;
    }
    
    result = AUGraphNodeInfo(audio_graph_, output_node_, NULL, &output_unit_);
    if (result != noErr) {
        addError("Failed to get output unit reference");
        return false;
    }
    
    // Connect synth to output
    result = AUGraphConnectNodeInput(audio_graph_, synth_node_, 0, output_node_, 0);
    if (result != noErr) {
        addError("Failed to connect synth to output");
        return false;
    }
    
    return true;
}

void Player::startAudioPlayback() {
    std::cout << "🔧 startAudioPlayback: Entry" << std::endl;
    
    if (!audio_initialized_) {
        addError("Audio not initialized");
        std::cout << "❌ startAudioPlayback: Audio not initialized" << std::endl;
        return;
    }
    
    std::cout << "✅ startAudioPlayback: Audio initialized OK" << std::endl;
    should_stop_playback_ = false;
    
    std::cout << "🔧 startAudioPlayback: Starting AUGraph..." << std::endl;
    OSStatus result = AUGraphStart(audio_graph_);
    if (result != noErr) {
        addError("Failed to start audio graph");
        std::cout << "❌ startAudioPlayback: AUGraphStart failed: " << result << std::endl;
        return;
    }
    
    std::cout << "✅ startAudioPlayback: AUGraph started OK" << std::endl;
    
    // Start playback thread
    std::cout << "🔧 startAudioPlayback: Creating playback thread..." << std::endl;
    playback_thread_ = std::thread(&Player::playbackThreadFunc, this);
    std::cout << "✅ startAudioPlayback: Playback thread created" << std::endl;
}

void Player::stopAudioPlayback() {
    should_stop_playback_ = true;
    
    if (playback_thread_.joinable()) {
        playback_thread_.join();
    }
    
    if (audio_graph_) {
        AUGraphStop(audio_graph_);
    }
}

void Player::setSynchronousMode(bool sync_mode) {
    synchronous_mode_ = sync_mode;
    if (verbose_) {
        std::cout << "🔧 Synchronous mode " << (sync_mode ? "ENABLED" : "DISABLED") << std::endl;
    }
}

bool Player::playSynchronous() {
    if (verbose_) {
        std::cout << "🎵 Playing synchronously on main thread" << std::endl;
    }
    
    try {
#ifdef __APPLE__
        if (!audio_initialized_) {
            addError("Audio not initialized");
            return false;
        }
        
        should_stop_playback_ = false;
        
        // Start AUGraph
        OSStatus result = AUGraphStart(audio_graph_);
        if (result != noErr) {
            addError("Failed to start audio graph");
            return false;
        }
        
        playback_state_ = PlaybackState::PLAYING;
        
        if (audio_callback_) {
            audio_callback_->onPlaybackStarted();
        }
        
        // Schedule MIDI events synchronously
        scheduleMIDIEvents();
        
        // Stop AUGraph
        AUGraphStop(audio_graph_);
        
        playback_state_ = PlaybackState::STOPPED;
        
        if (audio_callback_) {
            audio_callback_->onPlaybackStopped();
        }
        
        return true;
#else
        addError("Synchronous playback only supported on macOS");
        return false;
#endif
    } catch (const std::exception& e) {
        addError("Exception in synchronous playback: " + std::string(e.what()));
        playback_state_ = PlaybackState::ERROR;
        
        if (audio_callback_) {
            audio_callback_->onPlaybackError("Synchronous playback exception: " + std::string(e.what()));
        }
        return false;
    }
}

void Player::playbackThreadFunc() {
    ThreadSafeLogger::log("🧵 playbackThreadFunc: Entry");
    try {
        ThreadSafeLogger::log("🧵 playbackThreadFunc: Calling scheduleMIDIEvents...");
        scheduleMIDIEvents();
        ThreadSafeLogger::log("🧵 playbackThreadFunc: scheduleMIDIEvents completed");
    } catch (const std::exception& e) {
        addError("Exception in playback thread: " + std::string(e.what()));
        ThreadSafeLogger::log("💥 playbackThreadFunc: Exception: " + std::string(e.what()), true);
        playback_state_ = PlaybackState::ERROR;
        
        if (audio_callback_) {
            audio_callback_->onPlaybackError("Playback thread exception: " + std::string(e.what()));
        }
    }
}

void Player::scheduleMIDIEvents() {
    auto start_time = std::chrono::steady_clock::now();
    double playback_start_seconds = current_position_;
    
    // Sort all events by timestamp across all tracks
    std::vector<std::pair<double, const MIDIEvent*>> all_events;
    
    for (const MIDITrack& track : midi_tracks_) {
        for (const MIDIEvent& event : track.events) {
            // Convert event timestamp from beats to seconds
            double event_time_seconds = convertBeatsToSeconds(event.timestamp, current_tune_.default_tempo, current_tune_.default_unit_length);
            if (event_time_seconds >= playback_start_seconds) {
                all_events.push_back(std::make_pair(event_time_seconds, &event));
            }
        }
    }
    
    std::sort(all_events.begin(), all_events.end());
    
    for (const auto& pair : all_events) {
        if (should_stop_playback_) {
            break;
        }
        
        double event_time_seconds = pair.first;
        const MIDIEvent* event = pair.second;
        
        // Calculate when to play this event relative to playback start
        double relative_time_seconds = event_time_seconds - playback_start_seconds;
        
        // Wait until it's time to play the event
        auto target_time = start_time + std::chrono::duration<double>(relative_time_seconds);
        std::this_thread::sleep_until(target_time);
        
        if (should_stop_playback_) {
            break;
        }
        
        // Send the MIDI event
        sendMIDIEvent(*event);
        
        // Update current position
        current_position_ = event_time_seconds;
    }
    
    // Playback completed
    if (!should_stop_playback_) {
        playback_state_ = PlaybackState::STOPPED;
        current_position_ = 0.0;
        
        if (audio_callback_) {
            audio_callback_->onPlaybackStopped();
        }
    }
}

void Player::sendMIDIEvent(const MIDIEvent& event) {
    if (!synth_unit_) {
        return;
    }
    
    OSStatus result = noErr;
    
    switch (event.type) {
        case MIDIEventType::NOTE_ON: {
            result = MusicDeviceMIDIEvent(synth_unit_, 
                                        0x90 | (event.channel & 0x0F),
                                        event.data1 & 0x7F,
                                        event.data2 & 0x7F,
                                        0);
            
            if (audio_callback_) {
                audio_callback_->onNoteOn(event.data1, event.data2, event.channel, event.timestamp);
            }
            break;
        }
        
        case MIDIEventType::NOTE_OFF: {
            result = MusicDeviceMIDIEvent(synth_unit_,
                                        0x80 | (event.channel & 0x0F),
                                        event.data1 & 0x7F,
                                        0,
                                        0);
            
            if (audio_callback_) {
                audio_callback_->onNoteOff(event.data1, event.channel, event.timestamp);
            }
            break;
        }
        
        case MIDIEventType::PROGRAM_CHANGE: {
            result = MusicDeviceMIDIEvent(synth_unit_,
                                        0xC0 | (event.channel & 0x0F),
                                        event.data1 & 0x7F,
                                        0,
                                        0);
            
            if (audio_callback_) {
                audio_callback_->onProgramChange(event.data1, event.channel, event.timestamp);
            }
            break;
        }
        
        case MIDIEventType::CONTROL_CHANGE: {
            result = MusicDeviceMIDIEvent(synth_unit_,
                                        0xB0 | (event.channel & 0x0F),
                                        event.data1 & 0x7F,
                                        event.data2 & 0x7F,
                                        0);
            break;
        }
        
        case MIDIEventType::META_TEMPO: {
            if (!event.meta_data.empty() && event.meta_data.size() >= 3) {
                uint32_t mpq = (event.meta_data[0] << 16) | 
                              (event.meta_data[1] << 8) | 
                              event.meta_data[2];
                int new_tempo = 60000000 / mpq;
                
                if (new_tempo != current_tempo_) {
                    current_tempo_ = new_tempo;
                    
                    if (audio_callback_) {
                        audio_callback_->onTempo(current_tempo_, event.timestamp);
                    }
                }
            }
            break;
        }
        
        default:
            // Ignore other event types for now
            break;
    }
    
    if (result != noErr && verbose_) {
        std::cerr << "Warning: Failed to send MIDI event, result=" << result << std::endl;
    }
}
#endif

bool Player::validateTune() {
    if (current_tune_.features.empty()) {
        addError("No musical content found in ABC");
        return false;
    }
    
    if (current_tune_.voices.empty()) {
        addError("No voices defined in ABC");
        return false;
    }
    
    return true;
}

void Player::calculateTotalDuration() {
    total_duration_ = 0.0;
    
    // Find the latest timestamp among all features
    for (const Feature& feature : current_tune_.features) {
        double feature_end_time = feature.timestamp;
        
        // Add duration for notes and chords
        if (feature.type == FeatureType::NOTE) {
            const Note* note = feature.get<Note>();
            if (note) {
                feature_end_time += note->duration.toDouble();
            }
        } else if (feature.type == FeatureType::CHORD) {
            const Chord* chord = feature.get<Chord>();
            if (chord) {
                feature_end_time += chord->duration.toDouble();
            }
        } else if (feature.type == FeatureType::REST) {
            const Rest* rest = feature.get<Rest>();
            if (rest) {
                feature_end_time += rest->duration.toDouble();
            }
        }
        
        total_duration_ = std::max(total_duration_, 
                                  convertBeatsToSeconds(feature_end_time, current_tune_.default_tempo, current_tune_.default_unit_length));
    }
}

void Player::resetPlaybackState() {
    current_position_ = 0.0;
    playback_state_ = PlaybackState::STOPPED;
}

void Player::updateVoiceSettings() {
    // Apply voice-specific settings like muting, volume, instruments
    // This would be used during playback to modify MIDI events
}

bool Player::parsePhase() {
    if (!parser_) {
        addError("Parser not initialized");
        return false;
    }
    
    return true;
}

bool Player::generatePhase() {
    if (!midi_generator_) {
        addError("MIDI generator not initialized");
        return false;
    }
    
    midi_tracks_.clear();
    if (!midi_generator_->generateMIDI(current_tune_, midi_tracks_)) {
        addError("Failed to generate MIDI tracks");
        collectErrors();
        return false;
    }
    
    return true;
}

void Player::collectErrors() {
    // Collect errors from parser
    if (parser_) {
        const auto& parser_errors = parser_->getErrors();
        const auto& parser_warnings = parser_->getWarnings();
        
        errors_.insert(errors_.end(), parser_errors.begin(), parser_errors.end());
        warnings_.insert(warnings_.end(), parser_warnings.begin(), parser_warnings.end());
    }
    
    // Collect errors from MIDI generator
    if (midi_generator_) {
        const auto& midi_errors = midi_generator_->getErrors();
        
        errors_.insert(errors_.end(), midi_errors.begin(), midi_errors.end());
    }
}

std::string Player::getParserVersion() const {
    return ABCParser::getVersion();
}

void Player::processLogQueue() {
    ThreadSafeLogger::processAll();
}

double Player::convertBeatsToSeconds(double beats, const Tempo& tempo, const Fraction& unit_length) const {
    // Parser timestamps are in WHOLE-NOTE FRACTIONS
    // 0.25 = quarter note, 0.125 = eighth note, 0.5 = half note, etc.
    
    // Convert whole-note fraction to the tempo note's time value
    double tempo_note_value = tempo.note_value.toDouble(); // e.g., 0.25 for Q:1/4=120
    
    // How many tempo notes in this whole-note fraction?
    double tempo_notes = beats / tempo_note_value;
    
    // Seconds per tempo note
    double seconds_per_tempo_note = 60.0 / tempo.bpm;
    
    // Total seconds
    return tempo_notes * seconds_per_tempo_note;
}

// Default audio callback implementation
DefaultAudioCallback::DefaultAudioCallback() : verbose_(false) {
}

DefaultAudioCallback::~DefaultAudioCallback() {
}

void DefaultAudioCallback::onNoteOn(int midi_note, int velocity, int channel, double timestamp) {
    if (verbose_) {
        std::cout << "♪ Note On: " << midi_note << " vel=" << velocity 
                  << " ch=" << channel << " @ " << timestamp << "s" << std::endl;
    }
}

void DefaultAudioCallback::onNoteOff(int midi_note, int channel, double timestamp) {
    if (verbose_) {
        std::cout << "♫ Note Off: " << midi_note << " ch=" << channel 
                  << " @ " << timestamp << "s" << std::endl;
    }
}

void DefaultAudioCallback::onProgramChange(int program, int channel, double timestamp) {
    if (verbose_) {
        std::cout << "🎵 Program Change: " << program << " ch=" << channel 
                  << " @ " << timestamp << "s" << std::endl;
    }
}

void DefaultAudioCallback::onTempo(int bpm, double timestamp) {
    if (verbose_) {
        std::cout << "⏱ Tempo: " << bpm << " BPM @ " << timestamp << "s" << std::endl;
    }
}

void DefaultAudioCallback::onPlaybackStarted() {
    std::cout << "▶ Playback started" << std::endl;
}

void DefaultAudioCallback::onPlaybackStopped() {
    std::cout << "⏸ Playback stopped" << std::endl;
}

void DefaultAudioCallback::onPlaybackError(const std::string& error) {
    std::cerr << "❌ Playback error: " << error << std::endl;
}

} // namespace ABCPlayer