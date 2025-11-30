//
//  AudioDaemon.h
//  SuperTerminal - Audio Daemon Server
//
//  Created by Assistant on 2024-11-25.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#pragma once

#include "AudioDaemonProtocol.h"
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <functional>
#include <sys/socket.h>
#include <sys/un.h>

// Forward declarations
namespace SuperTerminal {
    class MidiEngine;
    class SynthEngine;
    class CoreAudioEngine;
}

namespace SuperTerminal {
namespace AudioDaemon {

// Client connection information
struct ClientConnection {
    int socket;
    std::string name;
    uint32_t version;
    std::thread clientThread;
    std::atomic<bool> active;
    uint32_t lastSequence;
    
    ClientConnection(int sock, const std::string& clientName, uint32_t clientVer)
        : socket(sock), name(clientName), version(clientVer), active(true), lastSequence(0) {}
};

// Queue entry for playback
struct PlaybackQueueEntry {
    uint32_t id;
    CommandType type;
    std::vector<uint8_t> data;
    uint32_t priority;
    uint32_t flags;
    std::string originalFilename;
    double estimatedDuration;
    
    PlaybackQueueEntry(uint32_t entryId, CommandType cmdType, 
                      const std::vector<uint8_t>& audioData, uint32_t prio = 0)
        : id(entryId), type(cmdType), data(audioData), priority(prio), 
          flags(0), estimatedDuration(0.0) {}
};

// Audio daemon server class
class AudioDaemon {
public:
    // Constructor/Destructor
    AudioDaemon(const std::string& socketPath = DEFAULT_SOCKET_PATH);
    ~AudioDaemon();
    
    // Server lifecycle
    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }
    
    // Configuration
    void setMaxClients(size_t maxClients) { maxClients_ = maxClients; }
    void setQueueSize(size_t maxQueueSize) { maxQueueSize_ = maxQueueSize; }
    void setSocketPath(const std::string& path) { socketPath_ = path; }
    
    // Status
    size_t getClientCount() const;
    size_t getQueueSize() const;
    PlaybackState getCurrentState() const { return currentState_.load(); }
    
private:
    // Core server functionality
    bool initializeSocket();
    void cleanupSocket();
    void serverLoop();
    void acceptClients();
    
    // Client management
    void handleClient(std::shared_ptr<ClientConnection> client);
    void processClientMessage(std::shared_ptr<ClientConnection> client, 
                             const MessageHeader& header, 
                             const std::vector<uint8_t>& data);
    void disconnectClient(std::shared_ptr<ClientConnection> client);
    void broadcastToClients(const MessageHeader& header, const void* data = nullptr);
    
    // Command handlers
    void handleConnect(std::shared_ptr<ClientConnection> client, 
                      uint32_t sequence, const std::vector<uint8_t>& data);
    void handleDisconnect(std::shared_ptr<ClientConnection> client, uint32_t sequence);
    void handlePing(std::shared_ptr<ClientConnection> client, uint32_t sequence);
    
    void handlePlayABC(std::shared_ptr<ClientConnection> client, 
                      uint32_t sequence, const std::vector<uint8_t>& data);
    void handlePlayMIDI(std::shared_ptr<ClientConnection> client, 
                       uint32_t sequence, const std::vector<uint8_t>& data);
    void handleStop(std::shared_ptr<ClientConnection> client, uint32_t sequence);
    void handlePause(std::shared_ptr<ClientConnection> client, uint32_t sequence);
    void handleResume(std::shared_ptr<ClientConnection> client, uint32_t sequence);
    
    void handleQueueABC(std::shared_ptr<ClientConnection> client, 
                       uint32_t sequence, const std::vector<uint8_t>& data);
    void handleQueueMIDI(std::shared_ptr<ClientConnection> client, 
                        uint32_t sequence, const std::vector<uint8_t>& data);
    void handleClearQueue(std::shared_ptr<ClientConnection> client, uint32_t sequence);
    void handleGetQueueStatus(std::shared_ptr<ClientConnection> client, uint32_t sequence);
    
    void handleGetStatus(std::shared_ptr<ClientConnection> client, uint32_t sequence);
    void handleGetPosition(std::shared_ptr<ClientConnection> client, uint32_t sequence);
    void handleSeek(std::shared_ptr<ClientConnection> client, 
                   uint32_t sequence, const std::vector<uint8_t>& data);
    
    void handleSetVolume(std::shared_ptr<ClientConnection> client, 
                        uint32_t sequence, const std::vector<uint8_t>& data);
    void handleSetTempo(std::shared_ptr<ClientConnection> client, 
                       uint32_t sequence, const std::vector<uint8_t>& data);
    void handleSetVoiceVolume(std::shared_ptr<ClientConnection> client, 
                             uint32_t sequence, const std::vector<uint8_t>& data);
    void handleSetVoiceInstrument(std::shared_ptr<ClientConnection> client, 
                                 uint32_t sequence, const std::vector<uint8_t>& data);
    void handleMuteVoice(std::shared_ptr<ClientConnection> client, 
                        uint32_t sequence, const std::vector<uint8_t>& data);
    
    void handlePlayBeep(std::shared_ptr<ClientConnection> client, 
                       uint32_t sequence, const std::vector<uint8_t>& data);
    void handlePlayTone(std::shared_ptr<ClientConnection> client, 
                       uint32_t sequence, const std::vector<uint8_t>& data);
    
    // Playback management
    void initializeAudioEngines();
    void shutdownAudioEngines();
    void playbackLoop();
    void processQueue();
    bool playABCData(const std::vector<uint8_t>& abcData, uint32_t flags = 0);
    bool playMIDIData(const std::vector<uint8_t>& midiData, uint32_t flags = 0);
    void stopCurrentPlayback();
    void pauseCurrentPlayback();
    void resumeCurrentPlayback();
    
    // Queue management
    uint32_t addToQueue(CommandType type, const std::vector<uint8_t>& data, 
                       uint32_t priority = 0, uint32_t flags = 0);
    void clearPlaybackQueue();
    std::vector<QueueEntry> getQueueEntries(size_t maxEntries = 0) const;
    
    // Response helpers
    void sendACK(std::shared_ptr<ClientConnection> client, uint32_t sequence, 
                ResponseStatus status, const std::string& message = "");
    void sendNACK(std::shared_ptr<ClientConnection> client, uint32_t sequence, 
                 ResponseStatus status, const std::string& message);
    void sendError(std::shared_ptr<ClientConnection> client, uint32_t sequence, 
                  ResponseStatus errorCode, const std::string& message);
    void sendStatusResponse(std::shared_ptr<ClientConnection> client, uint32_t sequence);
    void sendQueueStatusResponse(std::shared_ptr<ClientConnection> client, 
                                uint32_t sequence, size_t maxEntries = 0);
    void sendPositionResponse(std::shared_ptr<ClientConnection> client, uint32_t sequence);
    
    bool sendMessage(std::shared_ptr<ClientConnection> client, 
                    const MessageHeader& header, const void* data = nullptr);
    
    // Utility methods
    double estimateABCDuration(const std::string& abcData) const;
    double estimateMIDIDuration(const std::vector<uint8_t>& midiData) const;
    std::string getCurrentlyPlayingFile() const;
    
private:
    // Configuration
    std::string socketPath_;
    size_t maxClients_;
    size_t maxQueueSize_;
    
    // Server state
    std::atomic<bool> running_;
    std::atomic<bool> initialized_;
    int serverSocket_;
    std::thread serverThread_;
    std::thread playbackThread_;
    
    // Client management
    mutable std::mutex clientsMutex_;
    std::vector<std::shared_ptr<ClientConnection>> clients_;
    
    // Playback state
    std::atomic<PlaybackState> currentState_;
    mutable std::mutex queueMutex_;
    std::queue<PlaybackQueueEntry> playbackQueue_;
    std::condition_variable queueCondition_;
    uint32_t nextQueueId_;
    std::atomic<uint32_t> currentQueueId_;
    
    // Audio settings
    std::atomic<float> masterVolume_;
    std::atomic<float> tempoMultiplier_;
    mutable std::mutex voiceSettingsMutex_;
    std::unordered_map<uint32_t, float> voiceVolumes_;
    std::unordered_map<uint32_t, uint32_t> voiceInstruments_;
    std::unordered_map<uint32_t, bool> voiceMuted_;
    
    // Audio engines
    std::unique_ptr<SuperTerminal::CoreAudioEngine> audioEngine_;
    std::unique_ptr<SuperTerminal::MidiEngine> midiEngine_;
    std::unique_ptr<SuperTerminal::SynthEngine> synthEngine_;
    
    // Playback tracking
    mutable std::mutex playbackMutex_;
    std::atomic<double> currentPosition_;
    std::atomic<double> totalDuration_;
    std::string currentlyPlayingFile_;
    
    // Command handler map
    std::unordered_map<CommandType, std::function<void(std::shared_ptr<ClientConnection>, 
                                                      uint32_t, const std::vector<uint8_t>&)>> commandHandlers_;
    
    // Statistics
    std::atomic<size_t> totalClientsServed_;
    std::atomic<size_t> totalSongsPlayed_;
    std::atomic<size_t> totalErrors_;
};

// Standalone daemon launcher
class AudioDaemonLauncher {
public:
    // Launch daemon as separate process
    static bool launchDaemon(const std::string& socketPath = DEFAULT_SOCKET_PATH,
                            bool detach = true);
    
    // Check if daemon is running
    static bool isDaemonRunning(const std::string& socketPath = DEFAULT_SOCKET_PATH);
    
    // Stop running daemon
    static bool stopDaemon(const std::string& socketPath = DEFAULT_SOCKET_PATH);
    
    // Get daemon process ID
    static int getDaemonPID(const std::string& socketPath = DEFAULT_SOCKET_PATH);
    
private:
    static bool pingDaemon(const std::string& socketPath);
};

} // namespace AudioDaemon
} // namespace SuperTerminal