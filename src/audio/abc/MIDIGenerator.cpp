#include "MIDIGenerator.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace ABCPlayer {

// Standard guitar chord definitions (root + intervals in semitones)
const std::map<std::string, std::vector<int>> MIDIGenerator::standard_chords_ = {
    // Major chords
    {"C", {0, 4, 7}}, {"C#", {1, 5, 8}}, {"Db", {1, 5, 8}}, {"D", {2, 6, 9}},
    {"D#", {3, 7, 10}}, {"Eb", {3, 7, 10}}, {"E", {4, 8, 11}}, {"F", {5, 9, 0}},
    {"F#", {6, 10, 1}}, {"Gb", {6, 10, 1}}, {"G", {7, 11, 2}}, {"G#", {8, 0, 3}},
    {"Ab", {8, 0, 3}}, {"A", {9, 1, 4}}, {"A#", {10, 2, 5}}, {"Bb", {10, 2, 5}}, {"B", {11, 3, 6}},
    
    // Minor chords
    {"Cm", {0, 3, 7}}, {"C#m", {1, 4, 8}}, {"Dm", {2, 5, 9}}, {"D#m", {3, 6, 10}},
    {"Em", {4, 7, 11}}, {"Fm", {5, 8, 0}}, {"F#m", {6, 9, 1}}, {"Gm", {7, 10, 2}},
    {"G#m", {8, 11, 3}}, {"Am", {9, 0, 4}}, {"A#m", {10, 1, 5}}, {"Bm", {11, 2, 6}},
    
    // Seventh chords
    {"C7", {0, 4, 7, 10}}, {"Dm7", {2, 5, 9, 0}}, {"Em7", {4, 7, 11, 2}},
    {"F7", {5, 9, 0, 3}}, {"G7", {7, 11, 2, 5}}, {"Am7", {9, 0, 4, 7}}, {"Bm7b5", {11, 2, 5, 8}},
    
    // Other common chords
    {"Csus4", {0, 5, 7}}, {"Dsus4", {2, 7, 9}}, {"Esus4", {4, 9, 11}},
    {"Fsus4", {5, 10, 0}}, {"Gsus4", {7, 0, 2}}, {"Asus4", {9, 2, 4}}, {"Bsus4", {11, 4, 6}}
};

ChannelManager::ChannelManager() : next_available_channel_(0) {
    reset();
}

int ChannelManager::assignChannel(int voice_id, TrackType track_type) {
    // Check if voice already has a channel
    auto it = voice_to_channel_.find(voice_id);
    if (it != voice_to_channel_.end()) {
        return it->second;
    }
    
    // Find next available channel (skip percussion channel 9)
    while (next_available_channel_ < MAX_CHANNELS) {
        if (next_available_channel_ != PERCUSSION_CHANNEL && 
            !channels_in_use_[next_available_channel_]) {
            
            int channel = next_available_channel_;
            channels_in_use_[channel] = true;
            voice_to_channel_[voice_id] = channel;
            next_available_channel_++;
            
            return channel;
        }
        next_available_channel_++;
    }
    
    // If all channels are used, reuse channel 0 (but warn)
    std::cerr << "Warning: All MIDI channels in use, reusing channel 0 for voice " << voice_id << std::endl;
    return 0;
}

void ChannelManager::releaseChannel(int channel) {
    if (channel >= 0 && channel < MAX_CHANNELS) {
        channels_in_use_[channel] = false;
        
        // Remove from voice mapping
        for (auto it = voice_to_channel_.begin(); it != voice_to_channel_.end(); ++it) {
            if (it->second == channel) {
                voice_to_channel_.erase(it);
                break;
            }
        }
    }
}

bool ChannelManager::isChannelFree(int channel) const {
    if (channel < 0 || channel >= MAX_CHANNELS) {
        return false;
    }
    return !channels_in_use_[channel];
}

void ChannelManager::reset() {
    channels_in_use_.assign(MAX_CHANNELS, false);
    voice_to_channel_.clear();
    next_available_channel_ = 0;
}

MIDIGenerator::MIDIGenerator()
    : ticks_per_quarter_(DEFAULT_TICKS_PER_QUARTER), 
      default_tempo_(DEFAULT_TEMPO), 
      default_velocity_(DEFAULT_VELOCITY),
      current_time_(0.0), 
      current_tempo_(DEFAULT_TEMPO) {
}

MIDIGenerator::~MIDIGenerator() {
}

bool MIDIGenerator::generateMIDI(const ABCTune& tune, std::vector<MIDITrack>& tracks) {
    clearErrors();
    
    // Reset state
    channel_manager_.reset();
    voice_contexts_ = tune.voices;
    current_time_ = 0.0;
    current_tempo_ = tune.default_tempo.bpm;
    active_notes_.clear();
    
    try {
        // Create track structure
        createTracks(tune, tracks);
        
        // Assign channels to tracks
        assignChannels(tune, tracks);
        
        // Generate events for each track
        for (auto& track : tracks) {
            generateTrackEvents(tune, track);
        }
        
        // Process tempo track (track 0)
        if (!tracks.empty()) {
            processTempoTrack(tune, tracks[0]);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        addError("Exception during MIDI generation: " + std::string(e.what()));
        return false;
    }
}

bool MIDIGenerator::generateMIDIFile(const ABCTune& tune, const std::string& filename) {
    std::vector<MIDITrack> tracks;
    
    if (!generateMIDI(tune, tracks)) {
        return false;
    }
    
    return writeMIDIFile(tracks, filename);
}

void MIDIGenerator::createTracks(const ABCTune& tune, std::vector<MIDITrack>& tracks) {
    tracks.clear();
    
    // Track 0: Tempo and meta events
    MIDITrack tempo_track(0, TrackType::NOTES);
    tempo_track.name = "Tempo Track";
    tempo_track.channel = -1; // No channel assignment
    tracks.push_back(tempo_track);
    
    // Create tracks for each voice
    int track_num = 1;
    for (const auto& voice_pair : tune.voices) {
        const VoiceContext& voice = voice_pair.second;
        
        MIDITrack voice_track(track_num++, TrackType::NOTES);
        voice_track.voice_number = voice.voice_number;
        voice_track.name = voice.name.empty() ? ("Voice " + std::to_string(voice.voice_number)) : voice.name;
        tracks.push_back(voice_track);
    }
}

void MIDIGenerator::assignChannels(const ABCTune& tune, std::vector<MIDITrack>& tracks) {
    for (auto& track : tracks) {
        if (track.track_number == 0) {
            continue; // Skip tempo track
        }
        
        track.channel = channel_manager_.assignChannel(track.voice_number, track.type);
        
        // Send program change at beginning of track
        auto voice_it = tune.voices.find(track.voice_number);
        if (voice_it != tune.voices.end()) {
            const VoiceContext& voice = voice_it->second;
            addProgramChange(voice.instrument, track.channel, 0.0, track);
        }
    }
}

void MIDIGenerator::generateTrackEvents(const ABCTune& tune, MIDITrack& track) {
    if (track.track_number == 0) {
        return; // Tempo track is handled separately
    }
    
    current_time_ = 0.0;
    
    // Process features for this voice
    for (const Feature& feature : tune.features) {
        // Only process features for this track's voice
        if (feature.voice_id != track.voice_number) {
            continue;
        }
        
        processFeature(feature, track);
    }
    
    // Flush any remaining active notes
    flushActiveNotes(track);
    
    // Add end of track
    addEndOfTrack(current_time_, track);
}

void MIDIGenerator::processTempoTrack(const ABCTune& tune, MIDITrack& track) {
    // Add initial tempo
    addTempo(tune.default_tempo.bpm, 0.0, track);
    
    // Add time signature
    addTimeSignature(tune.default_timesig.numerator, tune.default_timesig.denominator, 0.0, track);
    
    // Add key signature
    addKeySignature(tune.default_key.sharps, true, 0.0, track);
    
    // Add title as text event
    if (!tune.title.empty()) {
        addText(tune.title, 0.0, track);
    }
    
    // Process tempo changes from features
    for (const Feature& feature : tune.features) {
        if (feature.type == FeatureType::TEMPO) {
            const Tempo* tempo = feature.get<Tempo>();
            if (tempo) {
                addTempo(tempo->bpm, feature.timestamp, track);
            }
        } else if (feature.type == FeatureType::TIME) {
            const TimeSignature* timesig = feature.get<TimeSignature>();
            if (timesig) {
                addTimeSignature(timesig->numerator, timesig->denominator, feature.timestamp, track);
            }
        } else if (feature.type == FeatureType::KEY) {
            const KeySignature* key = feature.get<KeySignature>();
            if (key) {
                addKeySignature(key->sharps, true, feature.timestamp, track);
            }
        }
    }
    
    // Add end of track
    addEndOfTrack(current_time_, track);
}

void MIDIGenerator::processFeature(const Feature& feature, MIDITrack& track) {
    double timestamp = feature.timestamp;
    
    switch (feature.type) {
        case FeatureType::NOTE: {
            const Note* note = feature.get<Note>();
            if (note) {
                processNote(*note, timestamp, feature.voice_id, track);
            }
            break;
        }
        
        case FeatureType::REST: {
            const Rest* rest = feature.get<Rest>();
            if (rest) {
                processRest(*rest, timestamp, feature.voice_id, track);
            }
            break;
        }
        
        case FeatureType::CHORD: {
            const Chord* chord = feature.get<Chord>();
            if (chord) {
                processChord(*chord, timestamp, feature.voice_id, track);
            }
            break;
        }
        
        case FeatureType::GCHORD: {
            const GuitarChord* gchord = feature.get<GuitarChord>();
            if (gchord) {
                processGuitarChord(*gchord, timestamp, feature.voice_id, track);
            }
            break;
        }
        
        case FeatureType::VOICE: {
            const VoiceChange* voice_change = feature.get<VoiceChange>();
            if (voice_change) {
                processVoiceChange(*voice_change, timestamp, track);
            }
            break;
        }
        
        case FeatureType::TEMPO: {
            const Tempo* tempo = feature.get<Tempo>();
            if (tempo) {
                processTempo(*tempo, timestamp, track);
            }
            break;
        }
        
        case FeatureType::TIME: {
            const TimeSignature* timesig = feature.get<TimeSignature>();
            if (timesig) {
                processTimeSignature(*timesig, timestamp, track);
            }
            break;
        }
        
        case FeatureType::KEY: {
            const KeySignature* key = feature.get<KeySignature>();
            if (key) {
                processKeySignature(*key, timestamp, track);
            }
            break;
        }
        
        default:
            // Ignore other feature types for now
            break;
    }
    
    // Update current time
    current_time_ = std::max(current_time_, timestamp);
}

void MIDIGenerator::processNote(const Note& note, double timestamp, int voice_id, MIDITrack& track) {
    // Process any note-offs that should happen before this note
    processActiveNotes(timestamp, track);
    
    // Schedule note on
    scheduleNoteOn(note.midi_note, note.velocity, track.channel, timestamp, track);
    
    // Schedule note off
    double note_off_time = timestamp + note.duration.toDouble();
    scheduleNoteOff(note.midi_note, track.channel, note_off_time, track);
}

void MIDIGenerator::processRest(const Rest& rest, double timestamp, int voice_id, MIDITrack& track) {
    // Process any note-offs that should happen during the rest
    double rest_end = timestamp + rest.duration.toDouble();
    processActiveNotes(rest_end, track);
    
    // Update current time
    current_time_ = std::max(current_time_, rest_end);
}

void MIDIGenerator::processChord(const Chord& chord, double timestamp, int voice_id, MIDITrack& track) {
    // Process any note-offs that should happen before this chord
    processActiveNotes(timestamp, track);
    
    // Play all notes in the chord simultaneously
    for (const Note& note : chord.notes) {
        scheduleNoteOn(note.midi_note, note.velocity, track.channel, timestamp, track);
        
        // Schedule note off
        double note_off_time = timestamp + chord.duration.toDouble();
        scheduleNoteOff(note.midi_note, track.channel, note_off_time, track);
    }
}

void MIDIGenerator::processGuitarChord(const GuitarChord& gchord, double timestamp, int voice_id, MIDITrack& track) {
    std::vector<int> chord_notes = parseGuitarChord(gchord.symbol);
    if (!chord_notes.empty()) {
        // Use the chord's duration from the parser (matches the melody note duration)
        double chord_duration = gchord.duration.toDouble();
        
        playGuitarChordNotes(chord_notes, timestamp, chord_duration, track.channel, track);
    }
}

void MIDIGenerator::processVoiceChange(const VoiceChange& voice_change, double timestamp, MIDITrack& track) {
    // Voice changes are handled by track separation, so not much to do here
    // Could potentially send program changes if voice attributes changed
}

void MIDIGenerator::processTempo(const Tempo& tempo, double timestamp, MIDITrack& track) {
    current_tempo_ = tempo.bpm;
    // Tempo changes are handled in the tempo track
}

void MIDIGenerator::processTimeSignature(const TimeSignature& timesig, double timestamp, MIDITrack& track) {
    // Time signature changes are handled in the tempo track
}

void MIDIGenerator::processKeySignature(const KeySignature& key, double timestamp, MIDITrack& track) {
    // Key signature changes are handled in the tempo track
}

void MIDIGenerator::scheduleNoteOn(int midi_note, int velocity, int channel, double timestamp, MIDITrack& track) {
    addNoteOn(midi_note, velocity, channel, timestamp, track);
    
    // Add to active notes for note-off scheduling
    ActiveNote active;
    active.midi_note = midi_note;
    active.channel = channel;
    active.velocity = velocity;
    // end_time will be set when note-off is scheduled
}

void MIDIGenerator::scheduleNoteOff(int midi_note, int channel, double timestamp, MIDITrack& track) {
    ActiveNote active;
    active.midi_note = midi_note;
    active.channel = channel;
    active.end_time = timestamp;
    
    active_notes_.insert(std::make_pair(timestamp, active));
}

void MIDIGenerator::processActiveNotes(double current_time, MIDITrack& track) {
    auto it = active_notes_.begin();
    while (it != active_notes_.end()) {
        if (it->first <= current_time) {
            const ActiveNote& active = it->second;
            addNoteOff(active.midi_note, active.channel, it->first, track);
            it = active_notes_.erase(it);
        } else {
            break; // Notes are sorted by time
        }
    }
}

void MIDIGenerator::flushActiveNotes(MIDITrack& track) {
    for (const auto& pair : active_notes_) {
        const ActiveNote& active = pair.second;
        addNoteOff(active.midi_note, active.channel, pair.first, track);
    }
    active_notes_.clear();
}

std::vector<int> MIDIGenerator::parseGuitarChord(const std::string& chord_symbol) {
    std::vector<int> notes;
    
    // Find chord in standard chord table
    auto it = standard_chords_.find(chord_symbol);
    if (it != standard_chords_.end()) {
        // Convert intervals to MIDI notes (base octave 4)
        int base_note = 60; // Middle C
        for (int interval : it->second) {
            notes.push_back(base_note + interval);
        }
    }
    
    return notes;
}

void MIDIGenerator::playGuitarChordNotes(const std::vector<int>& notes, double timestamp, 
                                        double duration, int channel, MIDITrack& track) {
    for (int midi_note : notes) {
        scheduleNoteOn(midi_note, default_velocity_, channel, timestamp, track);
        scheduleNoteOff(midi_note, channel, timestamp + duration, track);
    }
}

void MIDIGenerator::addNoteOn(int midi_note, int velocity, int channel, double timestamp, MIDITrack& track) {
    MIDIEvent event(MIDIEventType::NOTE_ON, timestamp, channel, midi_note, velocity);
    track.events.push_back(event);
}

void MIDIGenerator::addNoteOff(int midi_note, int channel, double timestamp, MIDITrack& track) {
    MIDIEvent event(MIDIEventType::NOTE_OFF, timestamp, channel, midi_note, 0);
    track.events.push_back(event);
}

void MIDIGenerator::addProgramChange(int program, int channel, double timestamp, MIDITrack& track) {
    MIDIEvent event(MIDIEventType::PROGRAM_CHANGE, timestamp, channel, program, 0);
    track.events.push_back(event);
}

void MIDIGenerator::addControlChange(int controller, int value, int channel, double timestamp, MIDITrack& track) {
    MIDIEvent event(MIDIEventType::CONTROL_CHANGE, timestamp, channel, controller, value);
    track.events.push_back(event);
}

void MIDIGenerator::addTempo(int bpm, double timestamp, MIDITrack& track) {
    MIDIEvent event(MIDIEventType::META_TEMPO, timestamp, 0, 0, 0);
    
    // Calculate microseconds per quarter note
    uint32_t mpq = 60000000 / bpm;
    
    event.meta_data.resize(3);
    event.meta_data[0] = (mpq >> 16) & 0xFF;
    event.meta_data[1] = (mpq >> 8) & 0xFF;
    event.meta_data[2] = mpq & 0xFF;
    
    track.events.push_back(event);
}

void MIDIGenerator::addTimeSignature(int num, int denom, double timestamp, MIDITrack& track) {
    MIDIEvent event(MIDIEventType::META_TIME_SIGNATURE, timestamp, 0, 0, 0);
    
    // Calculate denominator as power of 2
    int denom_power = 0;
    int temp_denom = denom;
    while (temp_denom > 1) {
        temp_denom /= 2;
        denom_power++;
    }
    
    event.meta_data.resize(4);
    event.meta_data[0] = num;
    event.meta_data[1] = denom_power;
    event.meta_data[2] = 24; // MIDI clocks per metronome click
    event.meta_data[3] = 8;  // 32nd notes per quarter note
    
    track.events.push_back(event);
}

void MIDIGenerator::addKeySignature(int sharps, bool major, double timestamp, MIDITrack& track) {
    MIDIEvent event(MIDIEventType::META_KEY_SIGNATURE, timestamp, 0, 0, 0);
    
    event.meta_data.resize(2);
    event.meta_data[0] = sharps;
    event.meta_data[1] = major ? 0 : 1;
    
    track.events.push_back(event);
}

void MIDIGenerator::addText(const std::string& text, double timestamp, MIDITrack& track) {
    MIDIEvent event(MIDIEventType::META_TEXT, timestamp, 0, 0, 0);
    
    event.meta_data.resize(text.length());
    std::copy(text.begin(), text.end(), event.meta_data.begin());
    
    track.events.push_back(event);
}

void MIDIGenerator::addEndOfTrack(double timestamp, MIDITrack& track) {
    MIDIEvent event(MIDIEventType::META_END_OF_TRACK, timestamp, 0, 0, 0);
    track.events.push_back(event);
}

bool MIDIGenerator::writeMIDIFile(const std::vector<MIDITrack>& tracks, const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        addError("Cannot create MIDI file: " + filename);
        return false;
    }
    
    try {
        // Write MIDI file header
        writeFileHeader(file, static_cast<int>(tracks.size()));
        
        // Write each track
        for (const MIDITrack& track : tracks) {
            writeTrack(file, track);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        addError("Error writing MIDI file: " + std::string(e.what()));
        return false;
    }
}

void MIDIGenerator::writeFileHeader(std::ofstream& file, int num_tracks) {
    // MIDI file header chunk
    file.write("MThd", 4);                          // Chunk type
    
    uint32_t header_length = 6;
    file.put((header_length >> 24) & 0xFF);        // Chunk length (big endian)
    file.put((header_length >> 16) & 0xFF);
    file.put((header_length >> 8) & 0xFF);
    file.put(header_length & 0xFF);
    
    uint16_t format = 1;                            // Format 1 (multiple tracks)
    file.put((format >> 8) & 0xFF);
    file.put(format & 0xFF);
    
    uint16_t tracks = static_cast<uint16_t>(num_tracks);
    file.put((tracks >> 8) & 0xFF);                // Number of tracks
    file.put(tracks & 0xFF);
    
    uint16_t division = static_cast<uint16_t>(ticks_per_quarter_);
    file.put((division >> 8) & 0xFF);              // Ticks per quarter note
    file.put(division & 0xFF);
}

void MIDIGenerator::writeTrack(std::ofstream& file, const MIDITrack& track) {
    // First, we need to build the track data to calculate its length
    std::ostringstream track_data;
    
    // Sort events by timestamp
    std::vector<MIDIEvent> sorted_events = track.events;
    std::sort(sorted_events.begin(), sorted_events.end(), 
              [](const MIDIEvent& a, const MIDIEvent& b) {
                  return a.timestamp < b.timestamp;
              });
    
    uint8_t running_status = 0;
    long current_ticks = 0;
    
    for (const MIDIEvent& event : sorted_events) {
        long event_ticks = beatsToTicks(event.timestamp);
        long delta_ticks = event_ticks - current_ticks;
        
        // Write delta time
        writeVariableLength(track_data, static_cast<uint32_t>(delta_ticks));
        
        // Write MIDI event
        writeMIDIEvent(track_data, event, running_status);
        
        current_ticks = event_ticks;
    }
    
    // Write track header
    file.write("MTrk", 4);                          // Chunk type
    
    std::string track_content = track_data.str();
    uint32_t track_length = static_cast<uint32_t>(track_content.length());
    
    file.put((track_length >> 24) & 0xFF);         // Chunk length (big endian)
    file.put((track_length >> 16) & 0xFF);
    file.put((track_length >> 8) & 0xFF);
    file.put(track_length & 0xFF);
    
    // Write track data
    file.write(track_content.c_str(), track_content.length());
}

void MIDIGenerator::writeVariableLength(std::ostream& file, uint32_t value) {
    std::vector<uint8_t> bytes;
    
    bytes.push_back(value & 0x7F);
    value >>= 7;
    
    while (value > 0) {
        bytes.push_back((value & 0x7F) | 0x80);
        value >>= 7;
    }
    
    // Write bytes in reverse order
    for (int i = static_cast<int>(bytes.size()) - 1; i >= 0; --i) {
        file.put(bytes[i]);
    }
}

void MIDIGenerator::writeMIDIEvent(std::ostream& file, const MIDIEvent& event, uint8_t& running_status) {
    switch (event.type) {
        case MIDIEventType::NOTE_ON: {
            uint8_t status = 0x90 | (event.channel & 0x0F);
            if (status != running_status) {
                file.put(status);
                running_status = status;
            }
            file.put(event.data1 & 0x7F);
            file.put(event.data2 & 0x7F);
            break;
        }
        
        case MIDIEventType::NOTE_OFF: {
            uint8_t status = 0x80 | (event.channel & 0x0F);
            if (status != running_status) {
                file.put(status);
                running_status = status;
            }
            file.put(event.data1 & 0x7F);
            file.put(event.data2 & 0x7F);
            break;
        }
        
        case MIDIEventType::PROGRAM_CHANGE: {
            uint8_t status = 0xC0 | (event.channel & 0x0F);
            if (status != running_status) {
                file.put(status);
                running_status = status;
            }
            file.put(event.data1 & 0x7F);
            break;
        }
        
        case MIDIEventType::CONTROL_CHANGE: {
            uint8_t status = 0xB0 | (event.channel & 0x0F);
            if (status != running_status) {
                file.put(status);
                running_status = status;
            }
            file.put(event.data1 & 0x7F);
            file.put(event.data2 & 0x7F);
            break;
        }
        
        case MIDIEventType::META_TEMPO: {
            running_status = 0; // Reset running status for meta events
            file.put(0xFF);
            file.put(0x51);
            file.put(static_cast<uint8_t>(event.meta_data.size()));
            for (uint8_t byte : event.meta_data) {
                file.put(byte);
            }
            break;
        }
        
        case MIDIEventType::META_TIME_SIGNATURE: {
            running_status = 0;
            file.put(0xFF);
            file.put(0x58);
            file.put(static_cast<uint8_t>(event.meta_data.size()));
            for (uint8_t byte : event.meta_data) {
                file.put(byte);
            }
            break;
        }
        
        case MIDIEventType::META_KEY_SIGNATURE: {
            running_status = 0;
            file.put(0xFF);
            file.put(0x59);
            file.put(static_cast<uint8_t>(event.meta_data.size()));
            for (uint8_t byte : event.meta_data) {
                file.put(byte);
            }
            break;
        }
        
        case MIDIEventType::META_TEXT: {
            running_status = 0;
            file.put(0xFF);
            file.put(0x01);
            file.put(static_cast<uint8_t>(event.meta_data.size()));
            for (uint8_t byte : event.meta_data) {
                file.put(byte);
            }
            break;
        }
        
        case MIDIEventType::META_END_OF_TRACK: {
            running_status = 0;
            file.put(0xFF);
            file.put(0x2F);
            file.put(0x00);
            break;
        }
        
        default:
            break;
    }
}

int MIDIGenerator::getMIDINoteFromChord(const std::string& chord_root, int octave) {
    int base_note = 0;
    
    if (chord_root == "C") base_note = 0;
    else if (chord_root == "C#" || chord_root == "Db") base_note = 1;
    else if (chord_root == "D") base_note = 2;
    else if (chord_root == "D#" || chord_root == "Eb") base_note = 3;
    else if (chord_root == "E") base_note = 4;
    else if (chord_root == "F") base_note = 5;
    else if (chord_root == "F#" || chord_root == "Gb") base_note = 6;
    else if (chord_root == "G") base_note = 7;
    else if (chord_root == "G#" || chord_root == "Ab") base_note = 8;
    else if (chord_root == "A") base_note = 9;
    else if (chord_root == "A#" || chord_root == "Bb") base_note = 10;
    else if (chord_root == "B") base_note = 11;
    
    return (octave + 1) * 12 + base_note;
}



} // namespace ABCPlayer