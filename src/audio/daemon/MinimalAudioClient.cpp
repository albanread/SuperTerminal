//
//  MinimalAudioClient.cpp
//  SuperTerminal - Minimal Audio Client (No Daemon)
//
//  Created by Assistant on 2024-11-25.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "AudioDaemonClient.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <mutex>

namespace SuperTerminal {
namespace AudioDaemon {

// Simple stub implementation that doesn't use a daemon
class StubAudioClient {
public:
    StubAudioClient() : playing_(false), volume_(1.0f) {}
    
    bool playABC(const std::string& abcData) {
        std::cout << "StubAudioClient: Would play ABC data (" << abcData.size() << " bytes)" << std::endl;
        playing_ = true;
        return true;
    }
    
    bool playABCFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "StubAudioClient: Could not open ABC file: " << filename << std::endl;
            return false;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        return playABC(buffer.str());
    }
    
    bool playMIDIFile(const std::string& filename) {
        std::cout << "StubAudioClient: Would play MIDI file: " << filename << std::endl;
        playing_ = true;
        return true;
    }
    
    bool stop() {
        std::cout << "StubAudioClient: Stopping playback" << std::endl;
        playing_ = false;
        return true;
    }
    
    bool beep(float frequency, float duration) {
        std::cout << "StubAudioClient: Would beep at " << frequency << "Hz for " << duration << "s" << std::endl;
        return true;
    }
    
    bool setVolume(float volume) {
        std::cout << "StubAudioClient: Setting volume to " << volume << std::endl;
        volume_ = volume;
        return true;
    }
    
    bool isPlaying() const {
        return playing_;
    }
    
private:
    bool playing_;
    float volume_;
};

// Global stub client
static std::unique_ptr<StubAudioClient> g_stubClient;
static std::mutex g_stubMutex;

// SimpleAudioClient implementation using stub
SimpleAudioClient::SimpleAudioClient() {
    std::lock_guard<std::mutex> lock(g_stubMutex);
    if (!g_stubClient) {
        g_stubClient = std::make_unique<StubAudioClient>();
    }
}

SimpleAudioClient::~SimpleAudioClient() {
    // Keep stub client alive for other instances
}

bool SimpleAudioClient::ensureConnection() {
    return true; // Always "connected" to stub
}

bool SimpleAudioClient::playABC(const std::string& abcData) {
    std::lock_guard<std::mutex> lock(g_stubMutex);
    return g_stubClient ? g_stubClient->playABC(abcData) : false;
}

bool SimpleAudioClient::playABCFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_stubMutex);
    return g_stubClient ? g_stubClient->playABCFile(filename) : false;
}

bool SimpleAudioClient::playMIDIFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_stubMutex);
    return g_stubClient ? g_stubClient->playMIDIFile(filename) : false;
}

bool SimpleAudioClient::stop() {
    std::lock_guard<std::mutex> lock(g_stubMutex);
    return g_stubClient ? g_stubClient->stop() : false;
}

bool SimpleAudioClient::beep(float frequency, float duration) {
    std::lock_guard<std::mutex> lock(g_stubMutex);
    return g_stubClient ? g_stubClient->beep(frequency, duration) : false;
}

bool SimpleAudioClient::setVolume(float volume) {
    std::lock_guard<std::mutex> lock(g_stubMutex);
    return g_stubClient ? g_stubClient->setVolume(volume) : false;
}

bool SimpleAudioClient::isPlaying() {
    std::lock_guard<std::mutex> lock(g_stubMutex);
    return g_stubClient ? g_stubClient->isPlaying() : false;
}

// AudioDaemonClient stub implementation
AudioDaemonClient::AudioDaemonClient(const std::string& clientName, const std::string& socketPath)
    : clientName_(clientName), socketPath_(socketPath), connected_(false) {
}

AudioDaemonClient::~AudioDaemonClient() {
}

bool AudioDaemonClient::connect(uint32_t timeoutMs) {
    std::cout << "AudioDaemonClient: Stub connect for " << clientName_ << std::endl;
    connected_ = true;
    return true;
}

void AudioDaemonClient::disconnect() {
    connected_ = false;
}

bool AudioDaemonClient::ping(uint32_t timeoutMs) {
    return connected_;
}

bool AudioDaemonClient::ensureDaemonRunning(bool autoLaunch) {
    return true; // Stub always "available"
}

bool AudioDaemonClient::playABC(const std::string& abcData, uint32_t flags) {
    std::lock_guard<std::mutex> lock(g_stubMutex);
    return g_stubClient ? g_stubClient->playABC(abcData) : false;
}

bool AudioDaemonClient::playABCFile(const std::string& filename, uint32_t flags) {
    std::lock_guard<std::mutex> lock(g_stubMutex);
    return g_stubClient ? g_stubClient->playABCFile(filename) : false;
}

bool AudioDaemonClient::playMIDIFile(const std::string& filename, uint32_t flags) {
    std::lock_guard<std::mutex> lock(g_stubMutex);
    return g_stubClient ? g_stubClient->playMIDIFile(filename) : false;
}

bool AudioDaemonClient::stop() {
    std::lock_guard<std::mutex> lock(g_stubMutex);
    return g_stubClient ? g_stubClient->stop() : false;
}

bool AudioDaemonClient::playBeep(float frequency, float duration, float volume) {
    std::lock_guard<std::mutex> lock(g_stubMutex);
    return g_stubClient ? g_stubClient->beep(frequency, duration) : false;
}

bool AudioDaemonClient::setVolume(float volume) {
    std::lock_guard<std::mutex> lock(g_stubMutex);
    return g_stubClient ? g_stubClient->setVolume(volume) : false;
}

bool AudioDaemonClient::isPlaying() const {
    std::lock_guard<std::mutex> lock(g_stubMutex);
    return g_stubClient ? g_stubClient->isPlaying() : false;
}

std::string AudioDaemonClient::getLastError() const {
    return "";
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