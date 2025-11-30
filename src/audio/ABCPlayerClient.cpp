//
//  ABCPlayerClient.cpp
//  SuperTerminal - ABC Player Client Integration
//
//  Created by Assistant on 2024-11-25.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "ABCPlayerClient.h"
#include "../SuperTerminal.framework/Headers/SuperTerminal.h"
#include "../GlobalShutdown.h"
#include "abc/ABCParser.h"
#include "abc/MIDIGenerator.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>
#include <vector>
#include <fstream>

namespace SuperTerminal {

static const char* DEFAULT_SOCKET_PATH = "/tmp/superterminal_audio.sock";
static const char* SERVER_EXECUTABLE_NAME = "abc_socket_player_queue";
static const int CONNECTION_TIMEOUT_MS = 5000;
static const int SERVER_START_WAIT_MS = 2000;

ABCPlayerClient::ABCPlayerClient() 
    : initialized_(false)
    , connected_(false)
    , socket_fd_(-1)
    , server_pid_(-1)
    , auto_start_server_(true)
    , debug_output_(false) {
}

ABCPlayerClient::~ABCPlayerClient() {
    shutdown();
}

bool ABCPlayerClient::initialize() {
    if (initialized_) {
        return true;
    }
    
    logInfo("Initializing ABC Player Client");
    
    // Try to connect to existing server first
    if (connectToServer()) {
        initialized_ = true;
        logInfo("ABC Player Client initialized (connected to existing server)");
        return true;
    }
    
    // If auto-start is enabled, try to start server
    if (auto_start_server_) {
        if (startServer()) {
            // Wait for server to start
            std::this_thread::sleep_for(std::chrono::milliseconds(SERVER_START_WAIT_MS));
            
            // Try to connect again
            if (connectToServer()) {
                initialized_ = true;
                logInfo("ABC Player Client initialized (started new server)");
                return true;
            } else {
                logError("Failed to connect to newly started server");
                return false;
            }
        } else {
            logError("Failed to start ABC player server");
            return false;
        }
    }
    
    logError("ABC Player Client initialization failed");
    return false;
}

void ABCPlayerClient::shutdown() {
    if (!initialized_) {
        return;
    }
    
    logInfo("Shutting down ABC Player Client");
    
    // Disconnect from server
    disconnectFromServer();
    
    initialized_ = false;
}

bool ABCPlayerClient::isInitialized() const {
    return initialized_;
}

bool ABCPlayerClient::playABC(const std::string& abc_notation, const std::string& name) {
    if (!ensureConnected()) {
        return false;
    }
    
    // Encode newlines for socket transmission
    std::string encoded_abc = abc_notation;
    size_t pos = 0;
    while ((pos = encoded_abc.find('\n', pos)) != std::string::npos) {
        encoded_abc.replace(pos, 1, "\\n");
        pos += 2;
    }
    
    std::string command = "QUEUE_ABC " + encoded_abc;
    return sendCommand(command);
}

bool ABCPlayerClient::playABCFile(const std::string& filename) {
    if (!ensureConnected()) {
        return false;
    }
    
    std::string command = "QUEUE_FILE " + filename;
    return sendCommand(command);
}

bool ABCPlayerClient::stop() {
    if (!ensureConnected()) {
        return false;
    }
    
    return sendCommand("STOP");
}

bool ABCPlayerClient::pause() {
    if (!ensureConnected()) {
        return false;
    }
    
    return sendCommand("PAUSE");
}

bool ABCPlayerClient::resume() {
    if (!ensureConnected()) {
        return false;
    }
    
    return sendCommand("RESUME");
}

bool ABCPlayerClient::clearQueue() {
    if (!ensureConnected()) {
        return false;
    }
    
    return sendCommand("CLEAR");
}

bool ABCPlayerClient::setVolume(float volume) {
    if (!ensureConnected()) {
        return false;
    }
    
    std::ostringstream oss;
    oss << "VOLUME " << volume;
    return sendCommand(oss.str());
}

ABCPlayerStatus ABCPlayerClient::getStatus() {
    ABCPlayerStatus status = {};
    
    if (!ensureConnected()) {
        return status;
    }
    
    if (!sendCommand("STATUS")) {
        return status;
    }
    
    std::string response = receiveResponse();
    if (response.empty()) {
        return status;
    }
    
    // Parse status response
    // Expected format: "Queue size: X\nPlaying: Yes/No\nPaused: Yes/No\nVolume: X.X"
    std::istringstream iss(response);
    std::string line;
    
    while (std::getline(iss, line)) {
        if (line.find("Queue size:") == 0) {
            std::string size_str = line.substr(12); // Skip "Queue size: "
            status.queue_size = std::stoi(size_str);
        } else if (line.find("Playing:") == 0) {
            std::string playing_str = line.substr(9); // Skip "Playing: "
            status.is_playing = (playing_str == "Yes");
        } else if (line.find("Paused:") == 0) {
            std::string paused_str = line.substr(8); // Skip "Paused: "
            status.is_paused = (paused_str == "Yes");
        } else if (line.find("Volume:") == 0) {
            std::string volume_str = line.substr(8); // Skip "Volume: "
            status.volume = std::stof(volume_str);
        }
    }
    
    return status;
}

std::vector<std::string> ABCPlayerClient::getQueueList() {
    std::vector<std::string> queue_list;
    
    if (!ensureConnected()) {
        return queue_list;
    }
    
    if (!sendCommand("LIST")) {
        return queue_list;
    }
    
    std::string response = receiveResponse();
    if (response.empty()) {
        return queue_list;
    }
    
    // Parse queue list response
    std::istringstream iss(response);
    std::string line;
    
    while (std::getline(iss, line)) {
        if (line.find(". FILE:") != std::string::npos || 
            line.find(". ABC:") != std::string::npos) {
            // Extract filename/description after the colon
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos && colon_pos + 2 < line.length()) {
                std::string item = line.substr(colon_pos + 2);
                queue_list.push_back(item);
            }
        }
    }
    
    return queue_list;
}

void ABCPlayerClient::setAutoStartServer(bool auto_start) {
    auto_start_server_ = auto_start;
}

void ABCPlayerClient::setDebugOutput(bool debug) {
    debug_output_ = debug;
}

bool ABCPlayerClient::exportMIDI(const std::string& abc_notation, const std::string& midi_filename) {
    if (abc_notation.empty()) {
        logError("ABC notation is empty");
        return false;
    }
    
    if (midi_filename.empty()) {
        logError("MIDI filename is empty");
        return false;
    }
    
    logInfo("Exporting ABC to MIDI: " + midi_filename);
    
    try {
        // Parse the ABC notation
        ABCPlayer::ABCParser parser;
        ABCPlayer::ABCTune tune;
        
        if (!parser.parseABC(abc_notation, tune)) {
            logError("Failed to parse ABC notation");
            const auto& errors = parser.getErrors();
            for (const auto& error : errors) {
                logError("  " + error);
            }
            return false;
        }
        
        // Generate MIDI file
        ABCPlayer::MIDIGenerator generator;
        if (!generator.generateMIDIFile(tune, midi_filename)) {
            logError("Failed to generate MIDI file");
            const auto& errors = generator.getErrors();
            for (const auto& error : errors) {
                logError("  " + error);
            }
            return false;
        }
        
        logInfo("Successfully exported MIDI file: " + midi_filename);
        return true;
        
    } catch (const std::exception& e) {
        logError("Exception during MIDI export: " + std::string(e.what()));
        return false;
    }
}

bool ABCPlayerClient::exportMIDIFromFile(const std::string& abc_filename, const std::string& midi_filename) {
    if (abc_filename.empty()) {
        logError("ABC filename is empty");
        return false;
    }
    
    if (midi_filename.empty()) {
        logError("MIDI filename is empty");
        return false;
    }
    
    logInfo("Exporting ABC file to MIDI: " + abc_filename + " -> " + midi_filename);
    
    // Read ABC file
    std::ifstream file(abc_filename);
    if (!file.is_open()) {
        logError("Cannot open ABC file: " + abc_filename);
        return false;
    }
    
    std::string abc_content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    file.close();
    
    if (abc_content.empty()) {
        logError("ABC file is empty: " + abc_filename);
        return false;
    }
    
    // Export using the content
    return exportMIDI(abc_content, midi_filename);
}

bool ABCPlayerClient::connectToServer() {
    if (connected_) {
        return true;
    }
    
    // Create socket
    socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        logError("Failed to create socket: " + std::string(strerror(errno)));
        return false;
    }
    
    // Set up server address
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, DEFAULT_SOCKET_PATH, sizeof(server_addr.sun_path) - 1);
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = CONNECTION_TIMEOUT_MS / 1000;
    timeout.tv_usec = (CONNECTION_TIMEOUT_MS % 1000) * 1000;
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    // Connect to server
    if (connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    // Read welcome message
    std::string welcome = receiveResponse();
    if (debug_output_) {
        logInfo("Server welcome: " + welcome);
    }
    if (welcome.find("ABC Socket Player Ready") == std::string::npos) {
        if (debug_output_) {
            logError("Unexpected welcome message from server: " + welcome);
        }
        disconnectFromServer();
        return false;
    }
    
    connected_ = true;
    if (debug_output_) {
        logInfo("Connected to ABC Player Server");
    }
    
    return true;
}

void ABCPlayerClient::disconnectFromServer() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    connected_ = false;
}

std::string ABCPlayerClient::findServerExecutable() {
    std::vector<std::string> search_paths = {
        // Build directory (primary location - our fixed server)
        std::string("./build/") + SERVER_EXECUTABLE_NAME,
        
        // Current directory
        std::string("./") + SERVER_EXECUTABLE_NAME,
        
        // Parent directory build (if running from subdirectory)
        std::string("../build/") + SERVER_EXECUTABLE_NAME,
        
        // Original locations for development (fallback)
        std::string("./new_abc_player/") + SERVER_EXECUTABLE_NAME,
        
        // Parent directory (if running from subdirectory)
        std::string("../") + SERVER_EXECUTABLE_NAME,
        
        // Just the name (in PATH)
        SERVER_EXECUTABLE_NAME,
    };
    
    for (const std::string& path : search_paths) {
        if (access(path.c_str(), X_OK) == 0) {
            if (debug_output_) {
                logInfo("Found server executable at: " + path);
            }
            return path;
        }
    }
    
    return ""; // Not found
}

bool ABCPlayerClient::startServer() {
    if (debug_output_) {
        logInfo("Starting ABC Player Server");
    }
    
    // Find the server executable
    std::string server_path = findServerExecutable();
    if (server_path.empty()) {
        logError("ABC Player Server executable not found. Searched for: " + std::string(SERVER_EXECUTABLE_NAME));
        return false;
    }
    
    // Fork process to start server
    pid_t pid = fork();
    
    if (pid < 0) {
        logError("Failed to fork process for server: " + std::string(strerror(errno)));
        return false;
    } else if (pid == 0) {
        // Child process - start the server
        
        // Redirect stdout and stderr to /dev/null to run silently
        if (!debug_output_) {
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);
        }
        
        // Execute the server
        execl(server_path.c_str(), server_path.c_str(), "-v", nullptr);
        
        // If execl fails, exit
        _exit(1);
    } else {
        // Parent process - store server PID
        server_pid_ = pid;
        
        if (debug_output_) {
            logInfo("Started server process with PID: " + std::to_string(pid) + " using: " + server_path);
        }
        
        return true;
    }
}

bool ABCPlayerClient::ensureConnected() {
    if (!initialized_) {
        logError("ABC Player Client not initialized");
        return false;
    }
    
    if (connected_) {
        return true;
    }
    
    // Try to reconnect
    if (connectToServer()) {
        return true;
    }
    
    // If auto-start is enabled and we have no server, try starting one
    if (auto_start_server_) {
        if (startServer()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(SERVER_START_WAIT_MS));
            return connectToServer();
        }
    }
    
    return false;
}

bool ABCPlayerClient::sendCommand(const std::string& command) {
    if (socket_fd_ < 0) {
        return false;
    }
    
    std::string full_command = command + "\n";
    ssize_t bytes_sent = send(socket_fd_, full_command.c_str(), full_command.length(), 0);
    
    if (bytes_sent < 0) {
        logError("Failed to send command: " + std::string(strerror(errno)));
        disconnectFromServer();
        return false;
    }
    
    if (debug_output_) {
        logInfo("Sent command: " + command);
    }
    
    return true;
}

std::string ABCPlayerClient::receiveResponse() {
    if (socket_fd_ < 0) {
        return "";
    }
    
    std::string response;
    char buffer[1024];
    
    while (true) {
        ssize_t bytes_received = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout
                break;
            }
            logError("Failed to receive response: " + std::string(strerror(errno)));
            disconnectFromServer();
            return "";
        } else if (bytes_received == 0) {
            // Connection closed
            disconnectFromServer();
            break;
        }
        
        buffer[bytes_received] = '\0';
        response += buffer;
        
        // Check if we have a complete response (ends with newline or contains "OK:" or "ERROR:")
        if (response.find('\n') != std::string::npos ||
            response.find("OK:") != std::string::npos ||
            response.find("ERROR:") != std::string::npos) {
            break;
        }
    }
    
    if (debug_output_ && !response.empty()) {
        logInfo("Received response: " + response);
    }
    
    return response;
}

void ABCPlayerClient::logInfo(const std::string& message) {
    if (debug_output_) {
        std::cout << "[ABC Client] " << message << std::endl;
    }
}

void ABCPlayerClient::logError(const std::string& message) {
    std::cerr << "[ABC Client ERROR] " << message << std::endl;
}

// Global instance
static ABCPlayerClient g_abc_client;

} // namespace SuperTerminal

// C API Implementation
extern "C" {

bool abc_client_initialize() {
    return SuperTerminal::g_abc_client.initialize();
}

void abc_client_shutdown() {
    SuperTerminal::g_abc_client.shutdown();
}

bool abc_client_is_initialized() {
    return SuperTerminal::g_abc_client.isInitialized();
}

bool abc_client_play_abc(const char* abc_notation, const char* name) {
    if (!abc_notation) return false;
    
    std::string notation(abc_notation);
    std::string song_name = name ? std::string(name) : "";
    
    return SuperTerminal::g_abc_client.playABC(notation, song_name);
}

bool abc_client_play_abc_file(const char* filename) {
    if (!filename) return false;
    
    return SuperTerminal::g_abc_client.playABCFile(std::string(filename));
}

bool abc_client_stop() {
    return SuperTerminal::g_abc_client.stop();
}

bool abc_client_pause() {
    return SuperTerminal::g_abc_client.pause();
}

bool abc_client_resume() {
    return SuperTerminal::g_abc_client.resume();
}

bool abc_client_clear_queue() {
    return SuperTerminal::g_abc_client.clearQueue();
}

bool abc_client_set_volume(float volume) {
    return SuperTerminal::g_abc_client.setVolume(volume);
}

bool abc_client_is_playing() {
    SuperTerminal::ABCPlayerStatus status = SuperTerminal::g_abc_client.getStatus();
    return status.is_playing;
}

bool abc_client_is_paused() {
    SuperTerminal::ABCPlayerStatus status = SuperTerminal::g_abc_client.getStatus();
    return status.is_paused;
}

int abc_client_get_queue_size() {
    SuperTerminal::ABCPlayerStatus status = SuperTerminal::g_abc_client.getStatus();
    return status.queue_size;
}

float abc_client_get_volume() {
    SuperTerminal::ABCPlayerStatus status = SuperTerminal::g_abc_client.getStatus();
    return status.volume;
}

const char* abc_client_get_current_song() {
    SuperTerminal::ABCPlayerStatus status = SuperTerminal::g_abc_client.getStatus();
    static std::string current_song_cache;
    current_song_cache = status.current_song;
    return current_song_cache.c_str();
}

void abc_client_set_auto_start_server(bool auto_start) {
    SuperTerminal::g_abc_client.setAutoStartServer(auto_start);
}

void abc_client_set_debug_output(bool debug) {
    SuperTerminal::g_abc_client.setDebugOutput(debug);
}

bool abc_client_export_midi(const char* abc_notation, const char* midi_filename) {
    if (!abc_notation || !midi_filename) {
        return false;
    }
    
    return SuperTerminal::g_abc_client.exportMIDI(
        std::string(abc_notation),
        std::string(midi_filename)
    );
}

bool abc_client_export_midi_from_file(const char* abc_filename, const char* midi_filename) {
    if (!abc_filename || !midi_filename) {
        return false;
    }
    
    return SuperTerminal::g_abc_client.exportMIDIFromFile(
        std::string(abc_filename),
        std::string(midi_filename)
    );
}

} // extern "C"