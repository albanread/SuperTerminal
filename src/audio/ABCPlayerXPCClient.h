//
//  ABCPlayerXPCClient.h
//  SuperTerminal - ABC Player XPC Client
//
//  XPC-based client for ABC Player service
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#ifndef ABC_PLAYER_XPC_CLIENT_H
#define ABC_PLAYER_XPC_CLIENT_H

#include <string>
#include <vector>
#include <atomic>

namespace SuperTerminal {

struct ABCPlayerStatus {
    int queue_size = 0;
    bool is_playing = false;
    bool is_paused = false;
    float volume = 1.0f;
    std::string current_song;
};

class ABCPlayerXPCClient {
public:
    ABCPlayerXPCClient();
    ~ABCPlayerXPCClient();
    
    // Initialization and cleanup
    bool initialize();
    void shutdown();
    bool isInitialized() const;
    
    // Music playback control
    bool playABC(const std::string& abc_notation, const std::string& name = "");
    bool playABCFile(const std::string& filename);
    bool stop();
    bool pause();
    bool resume();
    bool clearQueue();
    
    // Volume control
    bool setVolume(float volume);
    
    // Status queries
    ABCPlayerStatus getStatus();
    std::vector<std::string> getQueueList();
    
    // Service management
    bool ping();
    bool pingWithTimeout(int timeout_ms);
    std::string getVersion();
    
    // Configuration
    void setDebugOutput(bool debug);
    
    // MIDI export
    bool exportMIDI(const std::string& abc_notation, const std::string& midi_filename);
    bool exportMIDIFromFile(const std::string& abc_filename, const std::string& midi_filename);
    
private:
    class Impl;
    Impl* impl_;
    
    std::atomic<bool> initialized_;
    bool debug_output_;
};

} // namespace SuperTerminal

// C API for integration with existing SuperTerminal code
extern "C" {
    bool abc_client_initialize();
    void abc_client_shutdown();
    bool abc_client_is_initialized();
    
    bool abc_client_play_abc(const char* abc_notation, const char* name);
    bool abc_client_play_abc_file(const char* filename);
    bool abc_client_stop();
    bool abc_client_pause();
    bool abc_client_resume();
    bool abc_client_clear_queue();
    
    bool abc_client_set_volume(float volume);
    bool abc_client_is_playing();
    bool abc_client_is_paused();
    int abc_client_get_queue_size();
    float abc_client_get_volume();
    
    void abc_client_set_debug_output(bool debug);
    void abc_client_set_auto_start_server(bool auto_start);
    bool abc_client_ping_with_timeout(int timeout_ms);
    const char* abc_client_get_version();
    
    bool abc_client_export_midi(const char* abc_notation, const char* midi_filename);
    bool abc_client_export_midi_from_file(const char* abc_filename, const char* midi_filename);
}

#endif // ABC_PLAYER_XPC_CLIENT_H