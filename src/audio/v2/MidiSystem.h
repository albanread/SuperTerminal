//
//  MidiSystem.h
//  SuperTerminal Framework - Audio Graph v2.0
//
//  Professional MIDI system interface supporting real MIDI features
//  Platform-agnostic design with proper abstraction layer
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#pragma once

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <atomic>
#include <mutex>
#include <unordered_map>

// Forward declarations
class AudioBuffer;
class MidiSequence;
class MidiTrack;
class MidiEvent;

// MIDI event types
enum class MidiEventType : uint8_t {
    NOTE_OFF = 0x80,
    NOTE_ON = 0x90,
    POLYPHONIC_PRESSURE = 0xA0,
    CONTROL_CHANGE = 0xB0,
    PROGRAM_CHANGE = 0xC0,
    CHANNEL_PRESSURE = 0xD0,
    PITCH_BEND = 0xE0,
    SYSTEM_EXCLUSIVE = 0xF0,
    TIME_CODE = 0xF1,
    SONG_POSITION = 0xF2,
    SONG_SELECT = 0xF3,
    TUNE_REQUEST = 0xF6,
    TIMING_CLOCK = 0xF8,
    START = 0xFA,
    CONTINUE = 0xFB,
    STOP = 0xFC,
    ACTIVE_SENSING = 0xFE,
    RESET = 0xFF
};

// Standard MIDI Control Change numbers
namespace MidiCC {
    constexpr uint8_t BANK_SELECT = 0;
    constexpr uint8_t MODULATION = 1;
    constexpr uint8_t BREATH_CONTROLLER = 2;
    constexpr uint8_t FOOT_CONTROLLER = 4;
    constexpr uint8_t PORTAMENTO_TIME = 5;
    constexpr uint8_t DATA_ENTRY_MSB = 6;
    constexpr uint8_t VOLUME = 7;
    constexpr uint8_t BALANCE = 8;
    constexpr uint8_t PAN = 10;
    constexpr uint8_t EXPRESSION = 11;
    constexpr uint8_t EFFECT_CONTROL_1 = 12;
    constexpr uint8_t EFFECT_CONTROL_2 = 13;
    constexpr uint8_t GENERAL_PURPOSE_1 = 16;
    constexpr uint8_t GENERAL_PURPOSE_2 = 17;
    constexpr uint8_t GENERAL_PURPOSE_3 = 18;
    constexpr uint8_t GENERAL_PURPOSE_4 = 19;
    constexpr uint8_t BANK_SELECT_LSB = 32;
    constexpr uint8_t MODULATION_LSB = 33;
    constexpr uint8_t SUSTAIN_PEDAL = 64;
    constexpr uint8_t PORTAMENTO = 65;
    constexpr uint8_t SOSTENUTO = 66;
    constexpr uint8_t SOFT_PEDAL = 67;
    constexpr uint8_t LEGATO = 68;
    constexpr uint8_t HOLD_2 = 69;
    constexpr uint8_t SOUND_VARIATION = 70;
    constexpr uint8_t FILTER_RESONANCE = 71;
    constexpr uint8_t RELEASE_TIME = 72;
    constexpr uint8_t ATTACK_TIME = 73;
    constexpr uint8_t FILTER_CUTOFF = 74;
    constexpr uint8_t DECAY_TIME = 75;
    constexpr uint8_t VIBRATO_RATE = 76;
    constexpr uint8_t VIBRATO_DEPTH = 77;
    constexpr uint8_t VIBRATO_DELAY = 78;
    constexpr uint8_t PORTAMENTO_CONTROL = 84;
    constexpr uint8_t REVERB_DEPTH = 91;
    constexpr uint8_t TREMOLO_DEPTH = 92;
    constexpr uint8_t CHORUS_DEPTH = 93;
    constexpr uint8_t DETUNE_DEPTH = 94;
    constexpr uint8_t PHASER_DEPTH = 95;
    constexpr uint8_t DATA_INCREMENT = 96;
    constexpr uint8_t DATA_DECREMENT = 97;
    constexpr uint8_t NRPN_LSB = 98;
    constexpr uint8_t NRPN_MSB = 99;
    constexpr uint8_t RPN_LSB = 100;
    constexpr uint8_t RPN_MSB = 101;
    constexpr uint8_t ALL_SOUND_OFF = 120;
    constexpr uint8_t RESET_ALL_CONTROLLERS = 121;
    constexpr uint8_t LOCAL_CONTROL = 122;
    constexpr uint8_t ALL_NOTES_OFF = 123;
    constexpr uint8_t OMNI_MODE_OFF = 124;
    constexpr uint8_t OMNI_MODE_ON = 125;
    constexpr uint8_t MONO_MODE_ON = 126;
    constexpr uint8_t POLY_MODE_ON = 127;
}

// General MIDI drum map (channel 10)
namespace MidiDrums {
    constexpr uint8_t ACOUSTIC_BASS_DRUM = 35;
    constexpr uint8_t BASS_DRUM_1 = 36;
    constexpr uint8_t SIDE_STICK = 37;
    constexpr uint8_t ACOUSTIC_SNARE = 38;
    constexpr uint8_t HAND_CLAP = 39;
    constexpr uint8_t ELECTRIC_SNARE = 40;
    constexpr uint8_t LOW_FLOOR_TOM = 41;
    constexpr uint8_t CLOSED_HI_HAT = 42;
    constexpr uint8_t HIGH_FLOOR_TOM = 43;
    constexpr uint8_t PEDAL_HI_HAT = 44;
    constexpr uint8_t LOW_TOM = 45;
    constexpr uint8_t OPEN_HI_HAT = 46;
    constexpr uint8_t LOW_MID_TOM = 47;
    constexpr uint8_t HI_MID_TOM = 48;
    constexpr uint8_t CRASH_CYMBAL_1 = 49;
    constexpr uint8_t HIGH_TOM = 50;
    constexpr uint8_t RIDE_CYMBAL_1 = 51;
    constexpr uint8_t CHINESE_CYMBAL = 52;
    constexpr uint8_t RIDE_BELL = 53;
    constexpr uint8_t TAMBOURINE = 54;
    constexpr uint8_t SPLASH_CYMBAL = 55;
    constexpr uint8_t COWBELL = 56;
    constexpr uint8_t CRASH_CYMBAL_2 = 57;
    constexpr uint8_t VIBRASLAP = 58;
    constexpr uint8_t RIDE_CYMBAL_2 = 59;
    constexpr uint8_t HI_BONGO = 60;
    constexpr uint8_t LOW_BONGO = 61;
    constexpr uint8_t MUTE_HI_CONGA = 62;
    constexpr uint8_t OPEN_HI_CONGA = 63;
    constexpr uint8_t LOW_CONGA = 64;
}

// Time signature representation
struct TimeSignature {
    uint8_t numerator = 4;
    uint8_t denominator = 4;  // Actually stored as power of 2 (4 = 2^2)
    uint8_t clocksPerBeat = 24;
    uint8_t thirtySecondNotesPerBeat = 8;
    
    TimeSignature() = default;
    TimeSignature(uint8_t num, uint8_t den) : numerator(num), denominator(den) {}
    
    float getBeatsPerMeasure() const { return static_cast<float>(numerator); }
    float getBeatDuration() const { return 4.0f / denominator; }
};

// Key signature representation
struct KeySignature {
    int8_t sharpsFlats = 0;  // Negative for flats, positive for sharps
    bool isMinor = false;
    
    KeySignature() = default;
    KeySignature(int8_t sf, bool minor = false) : sharpsFlats(sf), isMinor(minor) {}
    
    std::string toString() const;
};

// Tempo change event
struct TempoChange {
    double timeInBeats;
    double microsecondsPerQuarterNote;
    double bpm;
    
    TempoChange(double time, double bpmValue) 
        : timeInBeats(time), bpm(bpmValue) {
        microsecondsPerQuarterNote = 60000000.0 / bpm;
    }
};

// MIDI device information
struct MidiDevice {
    uint32_t id;
    std::string name;
    std::string manufacturer;
    bool isInput;
    bool isOutput;
    bool isConnected;
    
    MidiDevice(uint32_t deviceId, const std::string& deviceName, bool input, bool output)
        : id(deviceId), name(deviceName), isInput(input), isOutput(output), isConnected(false) {}
};

// Quantization modes
enum class QuantizeMode {
    NONE,
    QUARTER_NOTE,
    EIGHTH_NOTE,
    SIXTEENTH_NOTE,
    THIRTY_SECOND_NOTE,
    QUARTER_TRIPLET,
    EIGHTH_TRIPLET,
    SIXTEENTH_TRIPLET
};

// MIDI event structure
class MidiEvent {
public:
    MidiEventType type;
    uint8_t channel;
    double timeInBeats;
    double timeInSeconds;
    std::vector<uint8_t> data;
    
    MidiEvent(MidiEventType eventType, uint8_t ch, double time)
        : type(eventType), channel(ch), timeInBeats(time), timeInSeconds(0.0) {}
    
    // Factory methods for common events
    static MidiEvent noteOn(uint8_t channel, uint8_t note, uint8_t velocity, double time);
    static MidiEvent noteOff(uint8_t channel, uint8_t note, uint8_t velocity, double time);
    static MidiEvent controlChange(uint8_t channel, uint8_t controller, uint8_t value, double time);
    static MidiEvent programChange(uint8_t channel, uint8_t program, double time);
    static MidiEvent pitchBend(uint8_t channel, uint16_t value, double time);
    static MidiEvent channelPressure(uint8_t channel, uint8_t pressure, double time);
    static MidiEvent polyPressure(uint8_t channel, uint8_t note, uint8_t pressure, double time);
    static MidiEvent systemExclusive(const std::vector<uint8_t>& sysexData, double time);
    static MidiEvent tempoChange(double bpm, double time);
    static MidiEvent timeSignatureChange(const TimeSignature& timeSig, double time);
    static MidiEvent keySignatureChange(const KeySignature& keySig, double time);
    
    // Getters for specific event data
    uint8_t getNote() const;
    uint8_t getVelocity() const;
    uint8_t getController() const;
    uint8_t getControllerValue() const;
    uint8_t getProgram() const;
    uint16_t getPitchBendValue() const;
    uint8_t getPressure() const;
    double getTempoBPM() const;
    TimeSignature getTimeSignature() const;
    KeySignature getKeySignature() const;
    
    // Utility
    bool isNoteEvent() const;
    bool isControllerEvent() const;
    bool isMetaEvent() const;
    std::string toString() const;
};

// MIDI sequence playback state
enum class PlaybackState {
    STOPPED,
    PLAYING,
    PAUSED,
    RECORDING
};

// Platform-agnostic MIDI system interface
class MidiSystem {
public:
    virtual ~MidiSystem() = default;
    
    // === INITIALIZATION ===
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool isInitialized() const = 0;
    
    // === REAL-TIME MIDI OUTPUT ===
    virtual void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) = 0;
    virtual void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity = 64) = 0;
    virtual void sendControlChange(uint8_t channel, uint8_t controller, uint8_t value) = 0;
    virtual void sendProgramChange(uint8_t channel, uint8_t program) = 0;
    virtual void sendPitchBend(uint8_t channel, uint16_t value) = 0;
    virtual void sendChannelPressure(uint8_t channel, uint8_t pressure) = 0;
    virtual void sendPolyPressure(uint8_t channel, uint8_t note, uint8_t pressure) = 0;
    virtual void sendSystemExclusive(const std::vector<uint8_t>& data) = 0;
    
    // Convenience methods
    virtual void sendAllNotesOff(uint8_t channel) = 0;
    virtual void sendAllSoundOff(uint8_t channel) = 0;
    virtual void sendResetAllControllers(uint8_t channel) = 0;
    virtual void sendPanic() = 0;  // All notes off on all channels
    
    // === SEQUENCE MANAGEMENT ===
    virtual uint32_t createSequence(const std::string& name = "Sequence") = 0;
    virtual bool deleteSequence(uint32_t sequenceId) = 0;
    virtual MidiSequence* getSequence(uint32_t sequenceId) = 0;
    virtual std::vector<uint32_t> getAllSequenceIds() const = 0;
    
    // === MIDI FILE I/O ===
    virtual bool loadMidiFile(uint32_t sequenceId, const std::string& filePath) = 0;
    virtual bool saveMidiFile(uint32_t sequenceId, const std::string& filePath) = 0;
    virtual bool importMidiData(uint32_t sequenceId, const std::vector<uint8_t>& midiData) = 0;
    virtual std::vector<uint8_t> exportMidiData(uint32_t sequenceId) = 0;
    
    // === SEQUENCE PLAYBACK ===
    virtual bool playSequence(uint32_t sequenceId, bool loop = false) = 0;
    virtual void stopSequence(uint32_t sequenceId) = 0;
    virtual void pauseSequence(uint32_t sequenceId) = 0;
    virtual void resumeSequence(uint32_t sequenceId) = 0;
    virtual PlaybackState getSequenceState(uint32_t sequenceId) const = 0;
    
    // === PLAYBACK CONTROL ===
    virtual void setSequencePosition(uint32_t sequenceId, double timeInBeats) = 0;
    virtual double getSequencePosition(uint32_t sequenceId) const = 0;
    virtual double getSequenceLength(uint32_t sequenceId) const = 0;
    virtual void setSequenceVolume(uint32_t sequenceId, float volume) = 0;
    virtual float getSequenceVolume(uint32_t sequenceId) const = 0;
    virtual void setSequenceTranspose(uint32_t sequenceId, int semitones) = 0;
    virtual int getSequenceTranspose(uint32_t sequenceId) const = 0;
    
    // === TIMING AND TEMPO ===
    virtual void setMasterTempo(double bpm) = 0;
    virtual double getMasterTempo() const = 0;
    virtual void setTempoMultiplier(float multiplier) = 0;
    virtual float getTempoMultiplier() const = 0;
    virtual void setTimeSignature(const TimeSignature& timeSig) = 0;
    virtual TimeSignature getTimeSignature() const = 0;
    virtual double getCurrentTime() const = 0;  // In beats
    virtual double getCurrentTimeInSeconds() const = 0;
    
    // === ADVANCED TIMING ===
    virtual void scheduleEvent(const MidiEvent& event, double timeInBeats) = 0;
    virtual void scheduleEventAtTime(const MidiEvent& event, double timeInSeconds) = 0;
    virtual void clearScheduledEvents() = 0;
    virtual size_t getScheduledEventCount() const = 0;
    
    // === GROOVE AND FEEL ===
    virtual void setSwing(float amount) = 0;  // 0.0 = straight, 1.0 = max swing
    virtual float getSwing() const = 0;
    virtual void setGrooveTemplate(const std::vector<float>& timingOffsets) = 0;
    virtual void setQuantization(QuantizeMode mode) = 0;
    virtual QuantizeMode getQuantization() const = 0;
    virtual void setHumanization(float timing, float velocity) = 0;
    
    // === RECORDING ===
    virtual bool startRecording(uint32_t sequenceId, uint32_t trackId) = 0;
    virtual void stopRecording() = 0;
    virtual bool isRecording() const = 0;
    virtual void setRecordingQuantization(QuantizeMode mode) = 0;
    virtual void setMetronomeEnabled(bool enabled) = 0;
    virtual bool isMetronomeEnabled() const = 0;
    virtual void setCountInBars(int bars) = 0;
    
    // === DEVICE MANAGEMENT ===
    virtual std::vector<MidiDevice> getInputDevices() = 0;
    virtual std::vector<MidiDevice> getOutputDevices() = 0;
    virtual bool connectInputDevice(uint32_t deviceId) = 0;
    virtual bool connectOutputDevice(uint32_t deviceId) = 0;
    virtual void disconnectInputDevice(uint32_t deviceId) = 0;
    virtual void disconnectOutputDevice(uint32_t deviceId) = 0;
    virtual std::vector<uint32_t> getConnectedInputDevices() const = 0;
    virtual std::vector<uint32_t> getConnectedOutputDevices() const = 0;
    
    // === SYNCHRONIZATION ===
    virtual void setMidiClockSource(bool internal) = 0;  // true = internal, false = external
    virtual bool isMidiClockInternal() const = 0;
    virtual void sendMidiClock(bool enabled) = 0;
    virtual void sendStartMessage() = 0;
    virtual void sendStopMessage() = 0;
    virtual void sendContinueMessage() = 0;
    virtual void setSongPosition(uint16_t position) = 0;
    
    // === MONITORING AND STATUS ===
    virtual size_t getActiveNoteCount() const = 0;
    virtual size_t getActiveNoteCount(uint8_t channel) const = 0;
    virtual std::vector<uint8_t> getActiveNotes(uint8_t channel) const = 0;
    virtual float getCpuUsage() const = 0;
    virtual size_t getMemoryUsage() const = 0;
    virtual double getLatency() const = 0;  // In milliseconds
    
    // === CALLBACKS ===
    using MidiInputCallback = std::function<void(const MidiEvent&)>;
    using TempoChangeCallback = std::function<void(double newBpm)>;
    using PlaybackCallback = std::function<void(uint32_t sequenceId, PlaybackState state)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    
    virtual void setMidiInputCallback(MidiInputCallback callback) = 0;
    virtual void setTempoChangeCallback(TempoChangeCallback callback) = 0;
    virtual void setPlaybackCallback(PlaybackCallback callback) = 0;
    virtual void setErrorCallback(ErrorCallback callback) = 0;
    
    // === UTILITY FUNCTIONS ===
    static std::string noteNumberToName(uint8_t noteNumber);
    static uint8_t noteNameToNumber(const std::string& noteName);
    static double beatsToSeconds(double beats, double bpm);
    static double secondsToBeats(double seconds, double bpm);
    static uint8_t velocityFromFloat(float velocity);  // 0.0-1.0 -> 0-127
    static float velocityToFloat(uint8_t velocity);    // 0-127 -> 0.0-1.0
    static std::vector<uint8_t> createSysExMessage(uint8_t manufacturerId, const std::vector<uint8_t>& data);
    
    // === CHORD AND SCALE UTILITIES ===
    static std::vector<uint8_t> getChordNotes(const std::string& chordType, uint8_t rootNote);
    static std::vector<uint8_t> getScaleNotes(const std::string& scaleType, uint8_t rootNote);
    static std::vector<uint8_t> harmonizeNote(uint8_t note, const std::string& scaleType, uint8_t rootNote);
    
protected:
    // Platform-specific implementations should override these
    virtual bool initializePlatformMidi() = 0;
    virtual void shutdownPlatformMidi() = 0;
    virtual void processMidiEvents() = 0;
    virtual void updateTiming() = 0;
    
    // Common state that implementations can use
    std::atomic<bool> initialized{false};
    std::atomic<double> masterTempo{120.0};
    std::atomic<float> tempoMultiplier{1.0f};
    std::atomic<float> swingAmount{0.0f};
    TimeSignature currentTimeSignature;
    std::atomic<bool> recording{false};
    std::atomic<bool> metronomeEnabled{false};
    QuantizeMode quantizeMode = QuantizeMode::NONE;
    
    mutable std::mutex stateMutex;
    
    // Callbacks
    MidiInputCallback midiInputCallback;
    TempoChangeCallback tempoChangeCallback;
    PlaybackCallback playbackCallback;
    ErrorCallback errorCallback;
};

// Factory function for creating platform-specific MIDI systems
std::unique_ptr<MidiSystem> createMidiSystem();

// Utility class for MIDI theory and music calculations
class MidiTheory {
public:
    // Chord definitions (intervals from root)
    static const std::vector<uint8_t> MAJOR_TRIAD;        // {0, 4, 7}
    static const std::vector<uint8_t> MINOR_TRIAD;        // {0, 3, 7}
    static const std::vector<uint8_t> DIMINISHED_TRIAD;   // {0, 3, 6}
    static const std::vector<uint8_t> AUGMENTED_TRIAD;    // {0, 4, 8}
    static const std::vector<uint8_t> MAJOR_SEVENTH;      // {0, 4, 7, 11}
    static const std::vector<uint8_t> MINOR_SEVENTH;      // {0, 3, 7, 10}
    static const std::vector<uint8_t> DOMINANT_SEVENTH;   // {0, 4, 7, 10}
    static const std::vector<uint8_t> HALF_DIMINISHED;    // {0, 3, 6, 10}
    static const std::vector<uint8_t> DIMINISHED_SEVENTH; // {0, 3, 6, 9}
    
    // Scale definitions (intervals from root)
    static const std::vector<uint8_t> MAJOR_SCALE;         // {0, 2, 4, 5, 7, 9, 11}
    static const std::vector<uint8_t> NATURAL_MINOR_SCALE; // {0, 2, 3, 5, 7, 8, 10}
    static const std::vector<uint8_t> HARMONIC_MINOR_SCALE;// {0, 2, 3, 5, 7, 8, 11}
    static const std::vector<uint8_t> MELODIC_MINOR_SCALE; // {0, 2, 3, 5, 7, 9, 11}
    static const std::vector<uint8_t> PENTATONIC_MAJOR;    // {0, 2, 4, 7, 9}
    static const std::vector<uint8_t> PENTATONIC_MINOR;    // {0, 3, 5, 7, 10}
    static const std::vector<uint8_t> BLUES_SCALE;         // {0, 3, 5, 6, 7, 10}
    static const std::vector<uint8_t> CHROMATIC_SCALE;     // {0, 1, 2, ..., 11}
    static const std::vector<uint8_t> DORIAN_MODE;         // {0, 2, 3, 5, 7, 9, 10}
    static const std::vector<uint8_t> PHRYGIAN_MODE;       // {0, 1, 3, 5, 7, 8, 10}
    static const std::vector<uint8_t> LYDIAN_MODE;         // {0, 2, 4, 6, 7, 9, 11}
    static const std::vector<uint8_t> MIXOLYDIAN_MODE;     // {0, 2, 4, 5, 7, 9, 10}
    
    // Utility functions
    static std::vector<uint8_t> getChordIntervals(const std::string& chordName);
    static std::vector<uint8_t> getScaleIntervals(const std::string& scaleName);
    static std::vector<uint8_t> transposeNotes(const std::vector<uint8_t>& notes, int semitones);
    static std::string getChordName(const std::vector<uint8_t>& notes, uint8_t root);
    static std::string getScaleName(const std::vector<uint8_t>& notes, uint8_t root);
    static bool isInScale(uint8_t note, const std::vector<uint8_t>& scale, uint8_t root);
    static uint8_t getNearestScaleNote(uint8_t note, const std::vector<uint8_t>& scale, uint8_t root);
    static std::vector<uint8_t> quantizeToScale(const std::vector<uint8_t>& notes, const std::vector<uint8_t>& scale, uint8_t root);
};