//
//  MidiEventCapture.cpp
//  SuperTerminal - MIDI Event Capture System for Testing
//
//  Created by Assistant on 2024-11-17.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "MidiEventCapture.h"
#include "MidiEngine.h"
#include "AudioSystem.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cmath>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

using namespace SuperTerminal;

// Forward declaration for global audio system
extern std::unique_ptr<AudioSystem> g_audioSystem;

// Global MIDI event capture instance
static std::unique_ptr<MidiEventCapture> g_midiCapture = nullptr;

// =====================================================
// MidiEventCapture Implementation
// =====================================================

MidiEventCapture::MidiEventCapture()
    : capturing(false)
    , totalEventsProcessed(0)
    , maxEvents(10000)
    , velocityThreshold(0)
    , channelFilter(-1)
    , noteRangeMin(0)
    , noteRangeMax(127)
    , timingPrecision(0.001)
{
    captureStartTime = std::chrono::steady_clock::now();
}

MidiEventCapture::~MidiEventCapture() {
    stopCapture();
}

bool MidiEventCapture::initialize(MidiEngine* midiEngine) {
    if (!midiEngine) {
        std::cerr << "MidiEventCapture: Cannot initialize without MidiEngine" << std::endl;
        return false;
    }

    this->midiEngine = midiEngine;

    // Set up MIDI event callback
    midiEngine->setMidiEventCallback([this](int channel, int note, int velocity, bool noteOn) {
        this->onMidiEvent(channel, note, velocity, noteOn);
    });

    std::cout << "MidiEventCapture: Initialized with MIDI engine callback" << std::endl;
    return true;
}

void MidiEventCapture::shutdown() {
    stopCapture();

    if (midiEngine) {
        // Clear the callback
        midiEngine->setMidiEventCallback(nullptr);
        midiEngine = nullptr;
    }

    std::cout << "MidiEventCapture: Shutdown complete" << std::endl;
}

bool MidiEventCapture::startCapture(const std::string& testName) {
    std::lock_guard<std::mutex> lock(captureMutex);

    if (capturing) {
        std::cerr << "MidiEventCapture: Already capturing events" << std::endl;
        return false;
    }

    // Reset capture state
    capturedEvents.clear();
    totalEventsProcessed = 0;
    currentTestName = testName.empty() ? "Unnamed Test" : testName;
    captureStartTime = std::chrono::steady_clock::now();
    capturing = true;

    std::cout << "MidiEventCapture: Started capturing for test '" << currentTestName << "'" << std::endl;
    return true;
}

std::vector<CapturedMidiEvent> MidiEventCapture::stopCapture() {
    std::lock_guard<std::mutex> lock(captureMutex);

    if (!capturing) {
        return capturedEvents; // Return existing events if not currently capturing
    }

    capturing = false;
    auto captureEndTime = std::chrono::steady_clock::now();

    auto captureDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
        captureEndTime - captureStartTime).count();

    std::cout << "MidiEventCapture: Stopped capturing after " << captureDuration
              << "ms. Captured " << capturedEvents.size() << " events." << std::endl;

    return capturedEvents;
}

void MidiEventCapture::clearEvents() {
    std::lock_guard<std::mutex> lock(captureMutex);
    capturedEvents.clear();
    totalEventsProcessed = 0;
}

void MidiEventCapture::setMaxEvents(size_t maxEvents) {
    std::lock_guard<std::mutex> lock(captureMutex);
    this->maxEvents = maxEvents;
}

void MidiEventCapture::setVelocityThreshold(int threshold) {
    std::lock_guard<std::mutex> lock(captureMutex);
    velocityThreshold = std::max(0, std::min(127, threshold));
}

void MidiEventCapture::setChannelFilter(int channel) {
    std::lock_guard<std::mutex> lock(captureMutex);
    channelFilter = (channel >= 0 && channel <= 15) ? channel : -1;
}

void MidiEventCapture::setNoteRange(int minNote, int maxNote) {
    std::lock_guard<std::mutex> lock(captureMutex);
    noteRangeMin = std::max(0, std::min(127, minNote));
    noteRangeMax = std::max(noteRangeMin, std::min(127, maxNote));
}

void MidiEventCapture::setTimingPrecision(double precision) {
    std::lock_guard<std::mutex> lock(captureMutex);
    timingPrecision = std::max(0.001, precision);
}

std::vector<CapturedMidiEvent> MidiEventCapture::getEvents() const {
    std::lock_guard<std::mutex> lock(captureMutex);
    return capturedEvents;
}

size_t MidiEventCapture::getEventCount() const {
    std::lock_guard<std::mutex> lock(captureMutex);
    return capturedEvents.size();
}

bool MidiEventCapture::isCapturing() const {
    std::lock_guard<std::mutex> lock(captureMutex);
    return capturing;
}

MidiCaptureStats MidiEventCapture::getStats() const {
    std::lock_guard<std::mutex> lock(captureMutex);

    MidiCaptureStats stats;
    stats.totalEvents = capturedEvents.size();
    stats.totalEventsProcessed = totalEventsProcessed;
    stats.testName = currentTestName;
    stats.isCapturing = capturing;

    if (!capturedEvents.empty()) {
        stats.firstEventTime = capturedEvents.front().timestamp;
        stats.lastEventTime = capturedEvents.back().timestamp;
        stats.captureDuration = stats.lastEventTime - stats.firstEventTime;

        // Count note on/off events
        for (const auto& event : capturedEvents) {
            if (event.type == MidiEventType::NOTE_ON) {
                stats.noteOnCount++;
            } else if (event.type == MidiEventType::NOTE_OFF) {
                stats.noteOffCount++;
            }

            // Track unique channels
            stats.uniqueChannels.insert(event.channel);

            // Track velocity range
            if (event.velocity > 0) {
                stats.minVelocity = std::min(stats.minVelocity, event.velocity);
                stats.maxVelocity = std::max(stats.maxVelocity, event.velocity);
            }
        }
    }

    return stats;
}

void MidiEventCapture::onMidiEvent(int channel, int note, int velocity, bool noteOn) {
    // Quick early return if not capturing
    if (!capturing) return;

    std::lock_guard<std::mutex> lock(captureMutex);

    // Apply filters
    totalEventsProcessed++;

    if (capturedEvents.size() >= maxEvents) {
        return; // Drop events if we've reached the limit
    }

    if (velocity < velocityThreshold) {
        return; // Filter out low velocity events
    }

    if (channelFilter >= 0 && channel != channelFilter) {
        return; // Filter by channel
    }

    if (note < noteRangeMin || note > noteRangeMax) {
        return; // Filter by note range
    }

    // Create captured event
    CapturedMidiEvent capturedEvent;
    capturedEvent.type = noteOn ? MidiEventType::NOTE_ON : MidiEventType::NOTE_OFF;
    capturedEvent.channel = channel;
    capturedEvent.note = note;
    capturedEvent.velocity = velocity;

    // Calculate precise timestamp
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
        now - captureStartTime);
    capturedEvent.timestamp = elapsed.count();

    // Apply timing precision
    capturedEvent.timestamp = std::round(capturedEvent.timestamp / timingPrecision) * timingPrecision;

    // Calculate relative timing
    if (!capturedEvents.empty()) {
        capturedEvent.timeSincePrevious = capturedEvent.timestamp - capturedEvents.back().timestamp;
    } else {
        capturedEvent.timeSincePrevious = 0.0;
    }

    // Add additional metadata
    capturedEvent.testName = currentTestName;
    capturedEvent.eventIndex = capturedEvents.size();

    // Store the event
    capturedEvents.push_back(capturedEvent);
}

// =====================================================
// Lua Binding Functions
// =====================================================

static int lua_midi_capture_initialize(lua_State* L) {
    if (!g_midiCapture) {
        g_midiCapture = std::make_unique<MidiEventCapture>();
    }

    // Get MidiEngine instance from global audio system
    // Declaration moved to top of file to avoid redefinition

    if (!g_audioSystem || !g_audioSystem->isInitialized()) {
        std::cerr << "MidiEventCapture: AudioSystem not initialized" << std::endl;
        lua_pushboolean(L, false);
        return 1;
    }

    SuperTerminal::MidiEngine* midiEngine = g_audioSystem->getMidiEngine();
    if (!midiEngine) {
        std::cerr << "MidiEventCapture: Could not get MidiEngine from AudioSystem" << std::endl;
        lua_pushboolean(L, false);
        return 1;
    }

    bool success = g_midiCapture->initialize(midiEngine);
    lua_pushboolean(L, success);
    return 1;
}

static int lua_midi_capture_shutdown(lua_State* L) {
    if (g_midiCapture) {
        g_midiCapture->shutdown();
        g_midiCapture.reset();
    }
    return 0;
}

static int lua_midi_capture_start(lua_State* L) {
    if (!g_midiCapture) {
        lua_pushboolean(L, false);
        return 1;
    }

    std::string testName = "Default Test";
    if (lua_gettop(L) >= 1 && lua_isstring(L, 1)) {
        testName = lua_tostring(L, 1);
    }

    bool success = g_midiCapture->startCapture(testName);
    lua_pushboolean(L, success);
    return 1;
}

static int lua_midi_capture_stop(lua_State* L) {
    if (!g_midiCapture) {
        lua_newtable(L); // Return empty table
        return 1;
    }

    auto events = g_midiCapture->stopCapture();

    // Convert events to Lua table
    lua_createtable(L, static_cast<int>(events.size()), 0);

    for (size_t i = 0; i < events.size(); ++i) {
        const auto& event = events[i];

        lua_createtable(L, 0, 10); // Event table with 10 fields

        // Event type
        lua_pushstring(L, event.type == MidiEventType::NOTE_ON ? "note_on" : "note_off");
        lua_setfield(L, -2, "type");

        // Basic MIDI data
        lua_pushnumber(L, event.channel);
        lua_setfield(L, -2, "channel");

        lua_pushnumber(L, event.note);
        lua_setfield(L, -2, "note");

        lua_pushnumber(L, event.velocity);
        lua_setfield(L, -2, "velocity");

        // Timing data
        lua_pushnumber(L, event.timestamp);
        lua_setfield(L, -2, "timestamp");

        lua_pushnumber(L, event.timeSincePrevious);
        lua_setfield(L, -2, "time_since_previous");

        // Metadata
        lua_pushstring(L, event.testName.c_str());
        lua_setfield(L, -2, "test_name");

        lua_pushnumber(L, event.eventIndex);
        lua_setfield(L, -2, "event_index");

        // Add note name for convenience
        const char* noteNames[] = {
            "C", "C#", "D", "D#", "E", "F",
            "F#", "G", "G#", "A", "A#", "B"
        };
        int octave = (event.note / 12) - 1;
        int noteClass = event.note % 12;
        std::string noteName = std::string(noteNames[noteClass]) + std::to_string(octave);
        lua_pushstring(L, noteName.c_str());
        lua_setfield(L, -2, "note_name");

        // Add frequency for reference
        double frequency = 440.0 * std::pow(2.0, (static_cast<double>(event.note) - 69.0) / 12.0);
        lua_pushnumber(L, frequency);
        lua_setfield(L, -2, "frequency");

        lua_rawseti(L, -2, static_cast<int>(i + 1)); // 1-based indexing for Lua
    }

    return 1;
}

static int lua_midi_capture_clear(lua_State* L) {
    if (g_midiCapture) {
        g_midiCapture->clearEvents();
    }
    return 0;
}

static int lua_midi_capture_is_capturing(lua_State* L) {
    bool capturing = g_midiCapture ? g_midiCapture->isCapturing() : false;
    lua_pushboolean(L, capturing);
    return 1;
}

static int lua_midi_capture_get_count(lua_State* L) {
    size_t count = g_midiCapture ? g_midiCapture->getEventCount() : 0;
    lua_pushnumber(L, static_cast<lua_Number>(count));
    return 1;
}

static int lua_midi_capture_set_max_events(lua_State* L) {
    if (!g_midiCapture || lua_gettop(L) < 1 || !lua_isnumber(L, 1)) {
        return 0;
    }

    size_t maxEvents = static_cast<size_t>(lua_tonumber(L, 1));
    g_midiCapture->setMaxEvents(maxEvents);
    return 0;
}

static int lua_midi_capture_set_velocity_threshold(lua_State* L) {
    if (!g_midiCapture || lua_gettop(L) < 1 || !lua_isnumber(L, 1)) {
        return 0;
    }

    int threshold = static_cast<int>(lua_tonumber(L, 1));
    g_midiCapture->setVelocityThreshold(threshold);
    return 0;
}

static int lua_midi_capture_set_channel_filter(lua_State* L) {
    if (!g_midiCapture || lua_gettop(L) < 1) {
        return 0;
    }

    int channel = -1;
    if (lua_isnumber(L, 1)) {
        channel = static_cast<int>(lua_tonumber(L, 1));
    } else if (lua_isnil(L, 1)) {
        channel = -1; // No filter
    }

    g_midiCapture->setChannelFilter(channel);
    return 0;
}

static int lua_midi_capture_set_note_range(lua_State* L) {
    if (!g_midiCapture || lua_gettop(L) < 2 ||
        !lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
        return 0;
    }

    int minNote = static_cast<int>(lua_tonumber(L, 1));
    int maxNote = static_cast<int>(lua_tonumber(L, 2));
    g_midiCapture->setNoteRange(minNote, maxNote);
    return 0;
}

static int lua_midi_capture_get_stats(lua_State* L) {
    if (!g_midiCapture) {
        lua_newtable(L);
        return 1;
    }

    auto stats = g_midiCapture->getStats();

    lua_createtable(L, 0, 15); // Stats table

    lua_pushnumber(L, static_cast<lua_Number>(stats.totalEvents));
    lua_setfield(L, -2, "total_events");

    lua_pushnumber(L, static_cast<lua_Number>(stats.totalEventsProcessed));
    lua_setfield(L, -2, "total_events_processed");

    lua_pushnumber(L, static_cast<lua_Number>(stats.noteOnCount));
    lua_setfield(L, -2, "note_on_count");

    lua_pushnumber(L, static_cast<lua_Number>(stats.noteOffCount));
    lua_setfield(L, -2, "note_off_count");

    lua_pushnumber(L, stats.firstEventTime);
    lua_setfield(L, -2, "first_event_time");

    lua_pushnumber(L, stats.lastEventTime);
    lua_setfield(L, -2, "last_event_time");

    lua_pushnumber(L, stats.captureDuration);
    lua_setfield(L, -2, "capture_duration");

    lua_pushstring(L, stats.testName.c_str());
    lua_setfield(L, -2, "test_name");

    lua_pushboolean(L, stats.isCapturing);
    lua_setfield(L, -2, "is_capturing");

    lua_pushnumber(L, stats.minVelocity);
    lua_setfield(L, -2, "min_velocity");

    lua_pushnumber(L, stats.maxVelocity);
    lua_setfield(L, -2, "max_velocity");

    // Unique channels as array
    lua_createtable(L, static_cast<int>(stats.uniqueChannels.size()), 0);
    int channelIndex = 1;
    for (int channel : stats.uniqueChannels) {
        lua_pushnumber(L, channel);
        lua_rawseti(L, -2, channelIndex++);
    }
    lua_setfield(L, -2, "unique_channels");

    return 1;
}

// =====================================================
// Registration Function
// =====================================================

extern "C" void register_midi_capture_lua_bindings(lua_State* L) {
    // Core functions
    lua_register(L, "midi_capture_initialize", lua_midi_capture_initialize);
    lua_register(L, "midi_capture_shutdown", lua_midi_capture_shutdown);
    lua_register(L, "midi_capture_start", lua_midi_capture_start);
    lua_register(L, "midi_capture_stop", lua_midi_capture_stop);
    lua_register(L, "midi_capture_clear", lua_midi_capture_clear);

    // Status functions
    lua_register(L, "midi_capture_is_capturing", lua_midi_capture_is_capturing);
    lua_register(L, "midi_capture_get_count", lua_midi_capture_get_count);
    lua_register(L, "midi_capture_get_stats", lua_midi_capture_get_stats);

    // Configuration functions
    lua_register(L, "midi_capture_set_max_events", lua_midi_capture_set_max_events);
    lua_register(L, "midi_capture_set_velocity_threshold", lua_midi_capture_set_velocity_threshold);
    lua_register(L, "midi_capture_set_channel_filter", lua_midi_capture_set_channel_filter);
    lua_register(L, "midi_capture_set_note_range", lua_midi_capture_set_note_range);

    std::cout << "MidiEventCapture: Lua bindings registered" << std::endl;
}
