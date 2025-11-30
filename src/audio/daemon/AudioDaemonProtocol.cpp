//
//  AudioDaemonProtocol.cpp
//  SuperTerminal - Audio Daemon Protocol Implementation
//
//  Created by Assistant on 2024-11-25.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "AudioDaemonProtocol.h"
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <iostream>

namespace SuperTerminal {
namespace AudioDaemon {

// Simple checksum calculation (CRC32-like but simpler)
uint32_t ProtocolHelper::calculateChecksum(const void* data, size_t size) {
    if (!data || size == 0) {
        return 0;
    }
    
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t checksum = 0xFFFFFFFF;
    
    for (size_t i = 0; i < size; i++) {
        checksum ^= bytes[i];
        for (int j = 0; j < 8; j++) {
            if (checksum & 1) {
                checksum = (checksum >> 1) ^ 0xEDB88320;
            } else {
                checksum >>= 1;
            }
        }
    }
    
    return ~checksum;
}

// Validate message header
bool ProtocolHelper::validateHeader(const MessageHeader& header) {
    // Check protocol version
    if (header.version != PROTOCOL_VERSION) {
        return false;
    }
    
    // Check data size is reasonable
    if (header.dataSize > MAX_MESSAGE_SIZE) {
        return false;
    }
    
    // Check command type is valid
    uint32_t cmd = header.command;
    if (cmd < 0x1000 || cmd > 0x8FFF) {
        return false;
    }
    
    return true;
}

// Create standard ACK response
MessageHeader ProtocolHelper::createACK(uint32_t sequence, ResponseStatus status, 
                                       const std::string& message) {
    MessageHeader header;
    header.version = PROTOCOL_VERSION;
    header.command = static_cast<uint32_t>(CommandType::ACK);
    header.sequence = sequence;
    
    // Calculate data size
    ACKResponse response;
    response.status = static_cast<uint32_t>(status);
    strncpy(response.message, message.c_str(), sizeof(response.message) - 1);
    response.message[sizeof(response.message) - 1] = '\0';
    
    header.dataSize = sizeof(ACKResponse);
    header.checksum = calculateChecksum(&response, sizeof(response));
    
    return header;
}

// Create standard NACK response
MessageHeader ProtocolHelper::createNACK(uint32_t sequence, ResponseStatus status, 
                                        const std::string& message) {
    MessageHeader header;
    header.version = PROTOCOL_VERSION;
    header.command = static_cast<uint32_t>(CommandType::NACK);
    header.sequence = sequence;
    
    ACKResponse response;
    response.status = static_cast<uint32_t>(status);
    strncpy(response.message, message.c_str(), sizeof(response.message) - 1);
    response.message[sizeof(response.message) - 1] = '\0';
    
    header.dataSize = sizeof(ACKResponse);
    header.checksum = calculateChecksum(&response, sizeof(response));
    
    return header;
}

// Create error response
MessageHeader ProtocolHelper::createError(uint32_t sequence, ResponseStatus errorCode, 
                                         const std::string& message) {
    MessageHeader header;
    header.version = PROTOCOL_VERSION;
    header.command = static_cast<uint32_t>(CommandType::ERROR);
    header.sequence = sequence;
    
    ErrorResponse response;
    response.errorCode = static_cast<uint32_t>(errorCode);
    strncpy(response.errorMessage, message.c_str(), sizeof(response.errorMessage) - 1);
    response.errorMessage[sizeof(response.errorMessage) - 1] = '\0';
    strncpy(response.context, "AudioDaemon", sizeof(response.context) - 1);
    response.context[sizeof(response.context) - 1] = '\0';
    
    header.dataSize = sizeof(ErrorResponse);
    header.checksum = calculateChecksum(&response, sizeof(response));
    
    return header;
}

// Serialize message to byte vector
std::vector<uint8_t> ProtocolHelper::serializeMessage(const MessageHeader& header, 
                                                     const void* data) {
    std::vector<uint8_t> buffer;
    
    // Add header
    buffer.resize(sizeof(MessageHeader));
    memcpy(buffer.data(), &header, sizeof(MessageHeader));
    
    // Add data if present
    if (data && header.dataSize > 0) {
        size_t oldSize = buffer.size();
        buffer.resize(oldSize + header.dataSize);
        memcpy(buffer.data() + oldSize, data, header.dataSize);
    }
    
    return buffer;
}

// Deserialize message from byte vector
bool ProtocolHelper::deserializeMessage(const std::vector<uint8_t>& buffer, 
                                       MessageHeader& header, std::vector<uint8_t>& data) {
    if (buffer.size() < sizeof(MessageHeader)) {
        return false;
    }
    
    // Extract header
    memcpy(&header, buffer.data(), sizeof(MessageHeader));
    
    // Validate header
    if (!validateHeader(header)) {
        return false;
    }
    
    // Check buffer size matches expected data size
    if (buffer.size() != sizeof(MessageHeader) + header.dataSize) {
        return false;
    }
    
    // Extract data if present
    data.clear();
    if (header.dataSize > 0) {
        data.resize(header.dataSize);
        memcpy(data.data(), buffer.data() + sizeof(MessageHeader), header.dataSize);
        
        // Verify checksum
        uint32_t calculatedChecksum = calculateChecksum(data.data(), data.size());
        if (calculatedChecksum != header.checksum) {
            return false;
        }
    }
    
    return true;
}

// Get default socket path
std::string SocketPathHelper::getDefaultSocketPath() {
    return DEFAULT_SOCKET_PATH;
}

// Get user-specific socket path
std::string SocketPathHelper::getUserSocketPath(const std::string& username) {
    std::string user = username;
    
    if (user.empty()) {
        // Get current user
        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            user = pw->pw_name;
        } else {
            user = "unknown";
        }
    }
    
    return "/tmp/superterminal_audio_" + user + ".sock";
}

// Create socket directory if needed
bool SocketPathHelper::createSocketDirectory(const std::string& socketPath) {
    // Extract directory from socket path
    size_t lastSlash = socketPath.find_last_of('/');
    if (lastSlash == std::string::npos) {
        return true; // No directory specified, assume current directory
    }
    
    std::string directory = socketPath.substr(0, lastSlash);
    
    // Check if directory exists
    struct stat st;
    if (stat(directory.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    
    // Try to create directory
    if (mkdir(directory.c_str(), 0755) == 0) {
        return true;
    }
    
    // If mkdir failed, try to create parent directories recursively
    size_t pos = 0;
    while ((pos = directory.find('/', pos + 1)) != std::string::npos) {
        std::string subdir = directory.substr(0, pos);
        if (stat(subdir.c_str(), &st) != 0) {
            if (mkdir(subdir.c_str(), 0755) != 0) {
                return false;
            }
        }
    }
    
    // Try final directory creation
    return mkdir(directory.c_str(), 0755) == 0;
}

// Cleanup socket file
bool SocketPathHelper::cleanupSocket(const std::string& socketPath) {
    // Check if socket file exists
    struct stat st;
    if (stat(socketPath.c_str(), &st) != 0) {
        return true; // Doesn't exist, nothing to clean up
    }
    
    // Remove the socket file
    if (unlink(socketPath.c_str()) != 0) {
        perror("Failed to remove socket file");
        return false;
    }
    
    return true;
}

} // namespace AudioDaemon
} // namespace SuperTerminal