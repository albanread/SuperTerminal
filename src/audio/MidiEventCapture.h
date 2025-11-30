//
//  MidiEventCapture.h
//  SuperTerminal - MIDI Event Capture System for Testing
//
//  Created by Assistant on 2024-11-17.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <set>
#include <map>
#include <functional>

extern "C" {
#include "lua.h"
}

// Forward declarations
namespace SuperTerminal {
class MidiEngine;
}

// =====================================================
// MIDI Event Types and Structures
// =====================================================

enum class MidiEventType {
    NOTE_ON,
    NOTE_OFF,
    PROGRAM_CHANGE,
    CONTROL_CHANGE,
    PITCH_BEND,
    TEMPO_CHANGE,
    TIME_SIGNATURE,
    KEY_SIGNATURE,
    UNKNOWN
};

struct CapturedMidiEvent {
    MidiEventType type;
    int channel;                    // MIDI channel (0-15)
    int note;                      // MIDI note number (0-127)
    int velocity;                  // Note velocity (0-127)
    double timestamp;              // Time since capture start (seconds)
    double timeSincePrevious;      // Time since previous event (seconds)
    std::string testName;          // Name of the test that captured this event
    size_t eventIndex;             // Index of this event in the capture session
    
    // Additional data for non-note events
    int controllerNumber;          // For CC messages
    int controllerValue;           // For CC messages
    int programNumber;             // For program change
    double tempo;                  // For tempo changes
    
    CapturedMidiEvent() 
        : type(MidiEventType::UNKNOWN), channel(0), note(0), velocity(0)
        , timestamp(0.0), timeSincePrevious(0.0), eventIndex(0)
        , controllerNumber(0), controllerValue(0), programNumber(0), tempo(120.0) {}
};

struct MidiCaptureStats {
    size_t totalEvents;
    size_t totalEventsProcessed;   // Including filtered events
    size_t noteOnCount;
    size_t noteOffCount;
    double firstEventTime;
    double lastEventTime;
    double captureDuration;
    std::string testName;
    bool isCapturing;
    int minVelocity;
    int maxVelocity;
    std::set<int> uniqueChannels;
    
    MidiCaptureStats() 
        : totalEvents(0), totalEventsProcessed(0), noteOnCount(0), noteOffCount(0)
        , firstEventTime(0.0), lastEventTime(0.0), captureDuration(0.0)
        , isCapturing(false), minVelocity(127), maxVelocity(0) {}
};

// =====================================================
// MIDI Event Capture Class
// =====================================================

class MidiEventCapture {
public:
    MidiEventCapture();
    ~MidiEventCapture();
    
    // Initialization
    bool initialize(SuperTerminal::MidiEngine* midiEngine);
    void shutdown();
    
    // Capture control
    bool startCapture(const std::string& testName = "");
    std::vector<CapturedMidiEvent> stopCapture();
    void clearEvents();
    
    // Configuration
    void setMaxEvents(size_t maxEvents);
    void setVelocityThreshold(int threshold);        // Filter events below this velocity
    void setChannelFilter(int channel);              // Filter to specific channel (-1 for all)
    void setNoteRange(int minNote, int maxNote);     // Filter note range
    void setTimingPrecision(double precision);        // Timing quantization (seconds)
    
    // Status and data access
    std::vector<CapturedMidiEvent> getEvents() const;
    size_t getEventCount() const;
    bool isCapturing() const;
    MidiCaptureStats getStats() const;
    
private:
    // MIDI event callback (called by MidiEngine)
    void onMidiEvent(int channel, int note, int velocity, bool noteOn);
    
    // Internal state
    SuperTerminal::MidiEngine* midiEngine;
    mutable std::mutex captureMutex;
    
    // Capture control
    std::atomic<bool> capturing;
    std::chrono::steady_clock::time_point captureStartTime;
    std::string currentTestName;
    
    // Event storage
    std::vector<CapturedMidiEvent> capturedEvents;
    size_t totalEventsProcessed;
    
    // Configuration
    size_t maxEvents;
    int velocityThreshold;
    int channelFilter;              // -1 means no filter
    int noteRangeMin;
    int noteRangeMax;
    double timingPrecision;
};

// =====================================================
// Utility Functions
// =====================================================

class MidiEventAnalyzer {
public:
    // Timing analysis
    static std::vector<double> extractIntervals(const std::vector<CapturedMidiEvent>& events);
    static double calculateAverageInterval(const std::vector<CapturedMidiEvent>& events);
    static bool isTimingConsistent(const std::vector<CapturedMidiEvent>& events, double tolerance = 0.1);
    
    // Note analysis
    static std::vector<int> extractNotes(const std::vector<CapturedMidiEvent>& events, MidiEventType type = MidiEventType::NOTE_ON);
    static std::set<int> getUniqueNotes(const std::vector<CapturedMidiEvent>& events);
    static std::set<int> getUniqueChannels(const std::vector<CapturedMidiEvent>& events);
    
    // Velocity analysis
    static std::vector<int> extractVelocities(const std::vector<CapturedMidiEvent>& events);
    static int getMinVelocity(const std::vector<CapturedMidiEvent>& events);
    static int getMaxVelocity(const std::vector<CapturedMidiEvent>& events);
    static double getAverageVelocity(const std::vector<CapturedMidiEvent>& events);
    
    // Pattern analysis
    static bool validateChord(const std::vector<CapturedMidiEvent>& events, const std::vector<int>& expectedNotes, double timeTolerance = 0.01);
    static bool validateSequence(const std::vector<CapturedMidiEvent>& events, const std::vector<int>& expectedNoteSequence);
    static bool validateTempo(const std::vector<CapturedMidiEvent>& events, double expectedBPM, double tolerance = 5.0);
    
    // Multi-voice analysis
    static std::map<int, std::vector<CapturedMidiEvent>> groupByChannel(const std::vector<CapturedMidiEvent>& events);
    static bool validateChannelSeparation(const std::vector<CapturedMidiEvent>& events, int expectedChannels);
    
    // Ornament detection
    static bool detectTrill(const std::vector<CapturedMidiEvent>& events, int baseNote, int trillNote);
    static bool detectMordent(const std::vector<CapturedMidiEvent>& events, int baseNote);
    static bool detectGraceNotes(const std::vector<CapturedMidiEvent>& events);
    
    // Utility functions
    static std::string eventTypeToString(MidiEventType type);
    static std::string noteNumberToName(int note);
    static double noteNumberToFrequency(int note);
    static int frequencyToNoteNumber(double frequency);
};

// =====================================================
// Lua API Registration
// =====================================================

extern "C" void register_midi_capture_lua_bindings(lua_State* L);

// =====================================================
// Test Validation Helpers
// =====================================================

namespace MidiTestValidation {
    // Common test patterns
    bool validateSingleNote(const std::vector<CapturedMidiEvent>& events, int expectedNote, int expectedVelocity = -1);
    bool validateNoteSequence(const std::vector<CapturedMidiEvent>& events, const std::vector<int>& expectedNotes);
    bool validateChord(const std::vector<CapturedMidiEvent>& events, const std::vector<int>& expectedNotes, double simultaneityTolerance = 0.01);
    bool validateTiming(const std::vector<CapturedMidiEvent>& events, double expectedInterval, double tolerance = 0.05);
    bool validateChannelRouting(const std::vector<CapturedMidiEvent>& events, const std::map<int, std::vector<int>>& channelToNotes);
    
    // ABC-specific validations
    bool validateABCTempo(const std::vector<CapturedMidiEvent>& events, int abcTempo);
    bool validateABCAccidentals(const std::vector<CapturedMidiEvent>& events, const std::string& abcNotation);
    bool validateABCOctaves(const std::vector<CapturedMidiEvent>& events, const std::string& abcNotation);
    bool validateABCDurations(const std::vector<CapturedMidiEvent>& events, const std::string& abcNotation, double tolerance = 0.1);
    bool validateABCMultiVoice(const std::vector<CapturedMidiEvent>& events, int expectedVoices);
    
    // Statistical validation
    struct ValidationResult {
        bool passed;
        std::string message;
        double confidence;  // 0.0 to 1.0
        std::map<std::string, double> metrics;
    };
    
    ValidationResult validateMidiOutput(const std::vector<CapturedMidiEvent>& events, const std::string& testType, const std::map<std::string, std::string>& parameters);
}