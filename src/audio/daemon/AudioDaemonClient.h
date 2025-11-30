//
//  AudioDaemonClient.h
//  SuperTerminal - Audio Daemon Client Interface
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
#include <functional>
#include <future>
#include <chrono>
#include <sys/socket.h>
#include <sys/un.h>

namespace SuperTerminal {
namespace AudioDaemon {

// Forward declarations
struct StatusResponse;
struct QueueStatusResponse;
struct PositionResponse;

// Callback function types for asynchronous notifications
using StatusCallback = std::function<void(const StatusResponse&)>;
using ErrorCallback = std::function<void(ResponseStatus, const std::string&)>;
using PlaybackCallback = std::function<void(PlaybackState, const std::string&)>;

// Result wrapper for async operations
template<typename T>
class AsyncResult {
public:
    AsyncResult() : ready_(false) {}
    
    bool isReady() const { return ready_.load(); }
    
    bool waitFor(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, timeout, [this] { return ready_.load(); });
    }
    
    T get() {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return ready_.load(); });
        return result_;
    }
    
    void set(const T& value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_ = value;
            ready_ = true;
        }
        condition_.notify_all();
    }
    
    void setError(const std::string& error) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            error_ = error;
            ready_ = true;
        }
        condition_.notify_all();
    }
    
    bool hasError() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !error_.empty();
    }
    
    std::string getError() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return error_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<bool> ready_;
    T result_;
    std::string error_;
};

// Audio daemon client class
class AudioDaemonClient {
public:
    // Constructor/Destructor
    AudioDaemonClient(const std::string& clientName = "SuperTerminal",
                     const std::string& socketPath = DEFAULT_SOCKET_PATH);
    ~AudioDaemonClient();
    
    // Connection management
    bool connect(uint32_t timeoutMs = 5000);
    void disconnect();
    bool isConnected() const { return connected_.load(); }
    bool ping(uint32_t timeoutMs = 1000);
    
    // Ensure daemon is running (auto-launch if needed)
    bool ensureDaemonRunning(bool autoLaunch = true);
    
    // Synchronous playback control
    bool playABC(const std::string& abcData, uint32_t flags = 0);
    bool playABCFile(const std::string& filename, uint32_t flags = 0);
    bool playMIDI(const std::vector<uint8_t>& midiData, uint32_t flags = 0);
    bool playMIDIFile(const std::string& filename, uint32_t flags = 0);
    bool stop();
    bool pause();
    bool resume();
    
    // Queue management
    bool queueABC(const std::string& abcData, uint32_t priority = 0, uint32_t flags = 0);
    bool queueABCFile(const std::string& filename, uint32_t priority = 0, uint32_t flags = 0);
    bool queueMIDI(const std::vector<uint8_t>& midiData, uint32_t priority = 0, uint32_t flags = 0);
    bool queueMIDIFile(const std::string& filename, uint32_t priority = 0, uint32_t flags = 0);
    bool clearQueue();
    
    // Audio settings
    bool setVolume(float volume);
    bool setTempo(float tempoMultiplier);
    bool setVoiceVolume(uint32_t voiceId, float volume);
    bool setVoiceInstrument(uint32_t voiceId, uint32_t instrument);
    bool muteVoice(uint32_t voiceId, bool muted = true);
    bool unmuteVoice(uint32_t voiceId) { return muteVoice(voiceId, false); }
    
    // Simple sound effects
    bool playBeep(float frequency = 800.0f, float duration = 0.2f, float volume = 0.5f);
    bool playTone(float frequency, float duration, float volume = 0.5f, uint32_t waveform = 0);
    
    // Playback position
    bool seek(double position);
    
    // Status queries (synchronous)
    StatusResponse getStatus();
    std::vector<QueueEntry> getQueueStatus(size_t maxEntries = 0);
    std::pair<double, double> getPosition(); // returns {current, total}
    
    // Asynchronous API
    std::shared_ptr<AsyncResult<bool>> playABCAsync(const std::string& abcData, uint32_t flags = 0);
    std::shared_ptr<AsyncResult<bool>> playMIDIAsync(const std::vector<uint8_t>& midiData, uint32_t flags = 0);
    std::shared_ptr<AsyncResult<StatusResponse>> getStatusAsync();
    std::shared_ptr<AsyncResult<std::vector<QueueEntry>>> getQueueStatusAsync(size_t maxEntries = 0);
    
    // Callback registration for notifications
    void setStatusCallback(StatusCallback callback) { statusCallback_ = callback; }
    void setErrorCallback(ErrorCallback callback) { errorCallback_ = callback; }
    void setPlaybackCallback(PlaybackCallback callback) { playbackCallback_ = callback; }
    
    // Configuration
    void setTimeout(uint32_t timeoutMs) { defaultTimeout_ = timeoutMs; }
    void setRetryCount(uint32_t retries) { maxRetries_ = retries; }
    
    // Utility methods
    bool isPlaying() const;
    bool isPaused() const;
    bool isStopped() const;
    PlaybackState getCurrentState() const { return lastKnownState_.load(); }
    
    // Error handling
    std::string getLastError() const;
    ResponseStatus getLastErrorCode() const { return lastErrorCode_.load(); }
    
private:
    // Connection management
    bool initializeSocket();
    void cleanupSocket();
    void messageLoop();
    
    // Message handling
    bool sendMessage(const MessageHeader& header, const void* data = nullptr);
    bool receiveMessage(MessageHeader& header, std::vector<uint8_t>& data, uint32_t timeoutMs = 0);
    void processIncomingMessage(const MessageHeader& header, const std::vector<uint8_t>& data);
    
    // Request/response handling
    uint32_t getNextSequence() { return ++sequenceCounter_; }
    bool sendRequest(CommandType command, const void* data = nullptr, size_t dataSize = 0);
    bool sendRequestAndWaitForResponse(CommandType command, uint32_t& sequence,
                                      const void* requestData, size_t requestSize,
                                      MessageHeader& responseHeader,
                                      std::vector<uint8_t>& responseData,
                                      uint32_t timeoutMs);
    
    // Response handlers
    void handleACK(uint32_t sequence, const std::vector<uint8_t>& data);
    void handleNACK(uint32_t sequence, const std::vector<uint8_t>& data);
    void handleStatusResponse(uint32_t sequence, const std::vector<uint8_t>& data);
    void handleQueueStatusResponse(uint32_t sequence, const std::vector<uint8_t>& data);
    void handlePositionResponse(uint32_t sequence, const std::vector<uint8_t>& data);
    void handleError(uint32_t sequence, const std::vector<uint8_t>& data);
    
    // Async operation management
    void registerAsyncOperation(uint32_t sequence, std::function<void(bool, const std::string&)> callback);
    void completeAsyncOperation(uint32_t sequence, bool success, const std::string& error = "");
    void timeoutAsyncOperations();
    
    // File utilities
    std::string readTextFile(const std::string& filename);
    std::vector<uint8_t> readBinaryFile(const std::string& filename);
    
    // Error handling
    void setLastError(ResponseStatus code, const std::string& message);
    void clearLastError();
    
    // Auto-reconnection
    bool shouldAutoReconnect() const;
    void attemptReconnection();
    
private:
    // Configuration
    std::string clientName_;
    std::string socketPath_;
    uint32_t defaultTimeout_;
    uint32_t maxRetries_;
    bool autoReconnect_;
    
    // Connection state
    std::atomic<bool> connected_;
    std::atomic<bool> connecting_;
    int socket_;
    std::thread messageThread_;
    std::atomic<bool> shouldStop_;
    
    // Protocol handling
    std::atomic<uint32_t> sequenceCounter_;
    mutable std::mutex pendingRequestsMutex_;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> pendingRequests_;
    std::unordered_map<uint32_t, std::function<void(bool, const std::string&)>> asyncCallbacks_;
    
    // State tracking
    std::atomic<PlaybackState> lastKnownState_;
    std::atomic<ResponseStatus> lastErrorCode_;
    mutable std::mutex lastErrorMutex_;
    std::string lastErrorMessage_;
    
    // Callbacks
    StatusCallback statusCallback_;
    ErrorCallback errorCallback_;
    PlaybackCallback playbackCallback_;
    
    // Synchronization for blocking operations
    mutable std::mutex responseMutex_;
    std::condition_variable responseCondition_;
    std::unordered_map<uint32_t, std::pair<MessageHeader, std::vector<uint8_t>>> pendingResponses_;
    
    // Statistics
    std::atomic<size_t> totalRequestsSent_;
    std::atomic<size_t> totalResponsesReceived_;
    std::atomic<size_t> totalErrors_;
    std::atomic<size_t> reconnectionAttempts_;
};

// Convenience wrapper for simple use cases
class SimpleAudioClient {
public:
    SimpleAudioClient();
    ~SimpleAudioClient();
    
    // Simple playback methods that handle connection automatically
    bool playABC(const std::string& abcData);
    bool playABCFile(const std::string& filename);
    bool playMIDIFile(const std::string& filename);
    bool stop();
    bool beep(float frequency = 800.0f, float duration = 0.2f);
    
    // Volume control
    bool setVolume(float volume);
    
    // Status
    bool isPlaying();
    
private:
    std::unique_ptr<AudioDaemonClient> client_;
    bool ensureConnection();
};

// Global convenience functions for C-style API compatibility
extern "C" {
    // Simple C API for Lua bindings
    int audio_play_abc(const char* abcData);
    int audio_play_abc_file(const char* filename);
    int audio_play_midi_file(const char* filename);
    int audio_stop();
    int audio_beep_daemon(float frequency, float duration);
    int audio_set_volume(float volume);
    int audio_is_playing();
}

} // namespace AudioDaemon
} // namespace SuperTerminal