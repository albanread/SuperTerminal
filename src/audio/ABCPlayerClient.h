//
//  ABCPlayerClient.h
//  SuperTerminal - ABC Player Client Integration
//
//  Created by Assistant on 2024-11-25.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#ifndef ABC_PLAYER_CLIENT_H
#define ABC_PLAYER_CLIENT_H

#include <string>
#include <vector>
#include <atomic>
#include <sys/types.h>

namespace SuperTerminal {

struct ABCPlayerStatus {
    int queue_size = 0;
    bool is_playing = false;
    bool is_paused = false;
    float volume = 1.0f;
    std::string current_song;
};

class ABCPlayerClient {
public:
    ABCPlayerClient();
    ~ABCPlayerClient();
    
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
    
    // MIDI export
    bool exportMIDI(const std::string& abc_notation, const std::string& midi_filename);
    bool exportMIDIFromFile(const std::string& abc_filename, const std::string& midi_filename);
    
    // Status queries
    ABCPlayerStatus getStatus();
    std::vector<std::string> getQueueList();
    
    // Configuration
    void setAutoStartServer(bool auto_start);
    void setDebugOutput(bool debug);
    
private:
    // Connection management
    bool connectToServer();
    void disconnectFromServer();
    bool startServer();
    bool ensureConnected();
    
    // Server discovery
    std::string findServerExecutable();
    
    // Communication
    bool sendCommand(const std::string& command);
    std::string receiveResponse();
    
    // Logging
    void logInfo(const std::string& message);
    void logError(const std::string& message);
    
    // State
    std::atomic<bool> initialized_;
    std::atomic<bool> connected_;
    int socket_fd_;
    pid_t server_pid_;
    bool auto_start_server_;
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
    const char* abc_client_get_current_song();
    
    void abc_client_set_auto_start_server(bool auto_start);
    void abc_client_set_debug_output(bool debug);
    
    bool abc_client_export_midi(const char* abc_notation, const char* midi_filename);
    bool abc_client_export_midi_from_file(const char* abc_filename, const char* midi_filename);
}

#endif // ABC_PLAYER_CLIENT_H