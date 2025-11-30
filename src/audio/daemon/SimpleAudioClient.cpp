//
//  SimpleAudioClient.cpp
//  SuperTerminal - Simple Audio Client Implementation
//
//  Created by Assistant on 2024-11-25.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "AudioDaemonClient.h"
#include "AudioDaemonProtocol.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <chrono>
#include <thread>

// Helper function declaration
bool isDaemonRunning(const std::string& socketPath);

namespace SuperTerminal {
namespace AudioDaemon {

// SimpleAudioClient Implementation
SimpleAudioClient::SimpleAudioClient() 
    : client_(std::make_unique<AudioDaemonClient>("SimpleClient")) {
}

SimpleAudioClient::~SimpleAudioClient() {
    if (client_) {
        client_->disconnect();
    }
}

bool SimpleAudioClient::ensureConnection() {
    if (client_->isConnected()) {
        return true;
    }
    
    // Try to ensure daemon is running
    if (!client_->ensureDaemonRunning(true)) {
        return false;
    }
    
    // Connect to daemon
    return client_->connect(3000); // 3 second timeout
}

bool SimpleAudioClient::playABC(const std::string& abcData) {
    if (!ensureConnection()) {
        return false;
    }
    return client_->playABC(abcData);
}

bool SimpleAudioClient::playABCFile(const std::string& filename) {
    if (!ensureConnection()) {
        return false;
    }
    return client_->playABCFile(filename);
}

bool SimpleAudioClient::playMIDIFile(const std::string& filename) {
    if (!ensureConnection()) {
        return false;
    }
    return client_->playMIDIFile(filename);
}

bool SimpleAudioClient::stop() {
    if (!ensureConnection()) {
        return false;
    }
    return client_->stop();
}

bool SimpleAudioClient::beep(float frequency, float duration) {
    if (!ensureConnection()) {
        return false;
    }
    return client_->playBeep(frequency, duration);
}

bool SimpleAudioClient::setVolume(float volume) {
    if (!ensureConnection()) {
        return false;
    }
    return client_->setVolume(volume);
}

bool SimpleAudioClient::isPlaying() {
    if (!ensureConnection()) {
        return false;
    }
    return client_->isPlaying();
}

// AudioDaemonClient basic implementation
AudioDaemonClient::AudioDaemonClient(const std::string& clientName, const std::string& socketPath)
    : clientName_(clientName)
    , socketPath_(socketPath)
    , defaultTimeout_(5000)
    , maxRetries_(3)
    , autoReconnect_(true)
    , connected_(false)
    , connecting_(false)
    , socket_(-1)
    , shouldStop_(false)
    , sequenceCounter_(0)
    , lastKnownState_(PlaybackState::STOPPED)
    , lastErrorCode_(ResponseStatus::SUCCESS)
    , totalRequestsSent_(0)
    , totalResponsesReceived_(0)
    , totalErrors_(0)
    , reconnectionAttempts_(0) {
}

AudioDaemonClient::~AudioDaemonClient() {
    disconnect();
}

bool AudioDaemonClient::connect(uint32_t timeoutMs) {
    if (connected_.load()) {
        return true;
    }
    
    if (connecting_.load()) {
        // Wait for connection attempt to complete
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (connecting_.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return connected_.load();
    }
    
    connecting_ = true;
    
    // Initialize socket
    if (!initializeSocket()) {
        connecting_ = false;
        return false;
    }
    
    // Create socket address
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);
    
    // Connect to daemon
    if (::connect(socket_, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        setLastError(ResponseStatus::ERROR_UNKNOWN, 
                    "Failed to connect to daemon: " + std::string(strerror(errno)));
        cleanupSocket();
        connecting_ = false;
        return false;
    }
    
    // Send connect request
    ConnectRequest request;
    memset(&request, 0, sizeof(request));
    strncpy(request.clientName, clientName_.c_str(), sizeof(request.clientName) - 1);
    request.clientVersion = PROTOCOL_VERSION;
    request.flags = 0;
    
    uint32_t sequence;
    MessageHeader responseHeader;
    std::vector<uint8_t> responseData;
    if (!sendRequestAndWaitForResponse(CommandType::CONNECT, sequence, 
                                      &request, sizeof(request),
                                      responseHeader, responseData, timeoutMs)) {
        setLastError(ResponseStatus::ERROR_UNKNOWN, "Failed to send connect request");
        cleanupSocket();
        connecting_ = false;
        return false;
    }
    
    // Check response
    if (responseHeader.command == static_cast<uint32_t>(CommandType::ACK)) {
        connected_ = true;
        shouldStop_ = false;
        
        // Start message handling thread
        messageThread_ = std::thread(&AudioDaemonClient::messageLoop, this);
        
        clearLastError();
        connecting_ = false;
        return true;
    } else {
        setLastError(ResponseStatus::ERROR_UNKNOWN, "Connection rejected by daemon");
        cleanupSocket();
        connecting_ = false;
        return false;
    }
}

void AudioDaemonClient::disconnect() {
    if (!connected_.load()) {
        return;
    }
    
    shouldStop_ = true;
    connected_ = false;
    
    // Send disconnect if still connected
    if (socket_ >= 0) {
        sendRequest(CommandType::DISCONNECT);
    }
    
    // Wait for message thread to finish
    if (messageThread_.joinable()) {
        messageThread_.join();
    }
    
    cleanupSocket();
}

bool AudioDaemonClient::ping(uint32_t timeoutMs) {
    if (!connected_.load()) {
        return false;
    }
    
    uint32_t sequence;
    MessageHeader responseHeader;
    std::vector<uint8_t> responseData;
    
    return sendRequestAndWaitForResponse(CommandType::PING, sequence, 
                                        nullptr, 0, responseHeader, responseData, timeoutMs);
}

bool AudioDaemonClient::ensureDaemonRunning(bool autoLaunch) {
    // Check if daemon is already running
    if (isDaemonRunning(socketPath_)) {
        return true;
    }
    
    if (!autoLaunch) {
        return false;
    }
    
    // Launch daemon - for now just return true
    // TODO: Implement actual daemon launching
    return true;
}

bool AudioDaemonClient::playABC(const std::string& abcData, uint32_t flags) {
    if (!connected_.load()) {
        setLastError(ResponseStatus::ERROR_UNKNOWN, "Not connected to daemon");
        return false;
    }
    
    PlayABCRequest request;
    request.priority = 0; // Immediate playback
    request.flags = flags;
    request.dataSize = abcData.size();
    
    // Create combined data (request + ABC data)
    std::vector<uint8_t> requestData(sizeof(request) + abcData.size());
    memcpy(requestData.data(), &request, sizeof(request));
    memcpy(requestData.data() + sizeof(request), abcData.data(), abcData.size());
    
    uint32_t sequence;
    MessageHeader responseHeader;
    std::vector<uint8_t> responseData;
    
    return sendRequestAndWaitForResponse(CommandType::PLAY_ABC, sequence,
                                        requestData.data(), requestData.size(),
                                        responseHeader, responseData, defaultTimeout_);
}

bool AudioDaemonClient::playABCFile(const std::string& filename, uint32_t flags) {
    std::string abcData = readTextFile(filename);
    if (abcData.empty()) {
        setLastError(ResponseStatus::ERROR_FILE_NOT_FOUND, "Could not read ABC file: " + filename);
        return false;
    }
    return playABC(abcData, flags);
}

bool AudioDaemonClient::stop() {
    if (!connected_.load()) {
        return false;
    }
    
    uint32_t sequence;
    MessageHeader responseHeader;
    std::vector<uint8_t> responseData;
    
    return sendRequestAndWaitForResponse(CommandType::STOP, sequence,
                                        nullptr, 0, responseHeader, responseData, defaultTimeout_);
}

bool AudioDaemonClient::playBeep(float frequency, float duration, float volume) {
    if (!connected_.load()) {
        return false;
    }
    
    PlayBeepRequest request;
    request.frequency = frequency;
    request.duration = duration;
    request.volume = volume;
    
    uint32_t sequence;
    MessageHeader responseHeader;
    std::vector<uint8_t> responseData;
    
    return sendRequestAndWaitForResponse(CommandType::PLAY_BEEP, sequence,
                                        &request, sizeof(request),
                                        responseHeader, responseData, defaultTimeout_);
}

bool AudioDaemonClient::setVolume(float volume) {
    if (!connected_.load()) {
        return false;
    }
    
    SetVolumeRequest request;
    request.masterVolume = volume;
    
    uint32_t sequence;
    MessageHeader responseHeader;
    std::vector<uint8_t> responseData;
    
    return sendRequestAndWaitForResponse(CommandType::SET_VOLUME, sequence,
                                        &request, sizeof(request),
                                        responseHeader, responseData, defaultTimeout_);
}

bool AudioDaemonClient::isPlaying() const {
    PlaybackState state = lastKnownState_.load();
    return state == PlaybackState::PLAYING;
}

std::string AudioDaemonClient::getLastError() const {
    std::lock_guard<std::mutex> lock(lastErrorMutex_);
    return lastErrorMessage_;
}

// Private methods
bool AudioDaemonClient::initializeSocket() {
    socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_ < 0) {
        setLastError(ResponseStatus::ERROR_UNKNOWN, "Failed to create socket");
        return false;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = defaultTimeout_ / 1000;
    timeout.tv_usec = (defaultTimeout_ % 1000) * 1000;
    
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    return true;
}

void AudioDaemonClient::cleanupSocket() {
    if (socket_ >= 0) {
        close(socket_);
        socket_ = -1;
    }
}

void AudioDaemonClient::messageLoop() {
    while (!shouldStop_.load() && connected_.load()) {
        MessageHeader header;
        std::vector<uint8_t> data;
        
        if (receiveMessage(header, data, 1000)) { // 1 second timeout
            processIncomingMessage(header, data);
        }
    }
}

bool AudioDaemonClient::sendMessage(const MessageHeader& header, const void* data) {
    if (socket_ < 0) {
        return false;
    }
    
    std::vector<uint8_t> buffer = ProtocolHelper::serializeMessage(header, data);
    
    ssize_t sent = send(socket_, buffer.data(), buffer.size(), 0);
    if (sent != static_cast<ssize_t>(buffer.size())) {
        return false;
    }
    
    totalRequestsSent_++;
    return true;
}

bool AudioDaemonClient::receiveMessage(MessageHeader& header, std::vector<uint8_t>& data, uint32_t timeoutMs) {
    if (socket_ < 0) {
        return false;
    }
    
    // Receive header first
    ssize_t received = recv(socket_, &header, sizeof(header), 0);
    if (received != sizeof(header)) {
        return false;
    }
    
    // Validate header
    if (!ProtocolHelper::validateHeader(header)) {
        return false;
    }
    
    // Receive data if present
    data.clear();
    if (header.dataSize > 0) {
        data.resize(header.dataSize);
        received = recv(socket_, data.data(), header.dataSize, 0);
        if (received != static_cast<ssize_t>(header.dataSize)) {
            return false;
        }
        
        // Verify checksum
        uint32_t checksum = ProtocolHelper::calculateChecksum(data.data(), data.size());
        if (checksum != header.checksum) {
            return false;
        }
    }
    
    totalResponsesReceived_++;
    return true;
}

bool AudioDaemonClient::sendRequest(CommandType command, const void* data, size_t dataSize) {
    MessageHeader header;
    header.version = PROTOCOL_VERSION;
    header.command = static_cast<uint32_t>(command);
    header.sequence = getNextSequence();
    header.dataSize = dataSize;
    header.checksum = data ? ProtocolHelper::calculateChecksum(data, dataSize) : 0;
    
    return sendMessage(header, data);
}

bool AudioDaemonClient::sendRequestAndWaitForResponse(CommandType command, uint32_t& sequence,
                                                     const void* requestData, size_t requestSize,
                                                     MessageHeader& responseHeader, 
                                                     std::vector<uint8_t>& responseData,
                                                     uint32_t timeoutMs) {
    sequence = getNextSequence();
    
    MessageHeader requestHeader;
    requestHeader.version = PROTOCOL_VERSION;
    requestHeader.command = static_cast<uint32_t>(command);
    requestHeader.sequence = sequence;
    requestHeader.dataSize = requestSize;
    requestHeader.checksum = requestData ? ProtocolHelper::calculateChecksum(requestData, requestSize) : 0;
    
    // Send request
    if (!sendMessage(requestHeader, requestData)) {
        return false;
    }
    
    // Wait for response
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(responseMutex_);
            auto it = pendingResponses_.find(sequence);
            if (it != pendingResponses_.end()) {
                responseHeader = it->second.first;
                responseData = it->second.second;
                pendingResponses_.erase(it);
                return true;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return false; // Timeout
}

void AudioDaemonClient::processIncomingMessage(const MessageHeader& header, const std::vector<uint8_t>& data) {
    // Store response for waiting requests
    {
        std::lock_guard<std::mutex> lock(responseMutex_);
        pendingResponses_[header.sequence] = std::make_pair(header, data);
    }
    
    // Handle specific message types
    CommandType command = static_cast<CommandType>(header.command);
    switch (command) {
        case CommandType::ACK:
            handleACK(header.sequence, data);
            break;
        case CommandType::NACK:
            handleNACK(header.sequence, data);
            break;
        case CommandType::ERROR:
            handleError(header.sequence, data);
            break;
        default:
            break;
    }
}

void AudioDaemonClient::handleACK(uint32_t sequence, const std::vector<uint8_t>& data) {
    if (data.size() >= sizeof(ACKResponse)) {
        const ACKResponse* response = reinterpret_cast<const ACKResponse*>(data.data());
        if (response->status == static_cast<uint32_t>(ResponseStatus::SUCCESS)) {
            clearLastError();
        }
    }
}

void AudioDaemonClient::handleNACK(uint32_t sequence, const std::vector<uint8_t>& data) {
    if (data.size() >= sizeof(ACKResponse)) {
        const ACKResponse* response = reinterpret_cast<const ACKResponse*>(data.data());
        ResponseStatus status = static_cast<ResponseStatus>(response->status);
        setLastError(status, response->message);
    }
    totalErrors_++;
}

void AudioDaemonClient::handleError(uint32_t sequence, const std::vector<uint8_t>& data) {
    if (data.size() >= sizeof(ErrorResponse)) {
        const ErrorResponse* response = reinterpret_cast<const ErrorResponse*>(data.data());
        ResponseStatus status = static_cast<ResponseStatus>(response->errorCode);
        setLastError(status, response->errorMessage);
    }
    totalErrors_++;
}

std::string AudioDaemonClient::readTextFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return "";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void AudioDaemonClient::setLastError(ResponseStatus code, const std::string& message) {
    std::lock_guard<std::mutex> lock(lastErrorMutex_);
    lastErrorCode_ = code;
    lastErrorMessage_ = message;
}

void AudioDaemonClient::clearLastError() {
    std::lock_guard<std::mutex> lock(lastErrorMutex_);
    lastErrorCode_ = ResponseStatus::SUCCESS;
    lastErrorMessage_.clear();
}

// Helper function for daemon status checking
bool isDaemonRunning(const std::string& socketPath) {
    // Try to connect to the socket to see if daemon is running
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);
    
    bool running = (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    close(sock);
    
    return running;
}

// C API functions
extern "C" {
    static SimpleAudioClient* g_simpleClient = nullptr;
    
    static SimpleAudioClient* getClient() {
        if (!g_simpleClient) {
            g_simpleClient = new SimpleAudioClient();
        }
        return g_simpleClient;
    }
    
    int audio_play_abc(const char* abcData) {
        return getClient()->playABC(abcData ? abcData : "") ? 1 : 0;
    }
    
    int audio_play_abc_file(const char* filename) {
        return getClient()->playABCFile(filename ? filename : "") ? 1 : 0;
    }
    
    int audio_play_midi_file(const char* filename) {
        return getClient()->playMIDIFile(filename ? filename : "") ? 1 : 0;
    }
    
    int audio_stop() {
        return getClient()->stop() ? 1 : 0;
    }
    
    int audio_beep_daemon(float frequency, float duration) {
        return getClient()->beep(frequency, duration) ? 1 : 0;
    }
    
    int audio_set_volume(float volume) {
        return getClient()->setVolume(volume) ? 1 : 0;
    }
    
    int audio_is_playing() {
        return getClient()->isPlaying() ? 1 : 0;
    }
}

} // namespace AudioDaemon
} // namespace SuperTerminal