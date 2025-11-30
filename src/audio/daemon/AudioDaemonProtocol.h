//
//  AudioDaemonProtocol.h
//  SuperTerminal - Audio Daemon IPC Protocol
//
//  Created by Assistant on 2024-11-25.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace SuperTerminal {
namespace AudioDaemon {

// Protocol version for compatibility checking
static const uint32_t PROTOCOL_VERSION = 1;

// Default socket path
static const char* DEFAULT_SOCKET_PATH = "/tmp/superterminal_audio.sock";

// Maximum message size (64KB)
static const size_t MAX_MESSAGE_SIZE = 65536;

// Command types for audio daemon communication
enum class CommandType : uint32_t {
    // Connection management
    CONNECT = 0x1000,
    DISCONNECT = 0x1001,
    PING = 0x1002,
    
    // Playback control
    PLAY_ABC = 0x2000,
    PLAY_MIDI = 0x2001,
    STOP = 0x2002,
    PAUSE = 0x2003,
    RESUME = 0x2004,
    
    // Queue management
    QUEUE_ABC = 0x2100,
    QUEUE_MIDI = 0x2101,
    CLEAR_QUEUE = 0x2102,
    GET_QUEUE_STATUS = 0x2103,
    
    // Playback state
    GET_STATUS = 0x3000,
    GET_POSITION = 0x3001,
    SEEK = 0x3002,
    
    // Audio settings
    SET_VOLUME = 0x4000,
    SET_TEMPO = 0x4001,
    SET_INSTRUMENT = 0x4002,
    
    // Voice management
    SET_VOICE_VOLUME = 0x4100,
    SET_VOICE_INSTRUMENT = 0x4101,
    MUTE_VOICE = 0x4102,
    UNMUTE_VOICE = 0x4103,
    
    // Sound effects (simple beeps, etc.)
    PLAY_BEEP = 0x5000,
    PLAY_TONE = 0x5001,
    
    // Responses
    ACK = 0x8000,
    NACK = 0x8001,
    STATUS_RESPONSE = 0x8002,
    QUEUE_STATUS_RESPONSE = 0x8003,
    POSITION_RESPONSE = 0x8004,
    ERROR = 0x8FFF
};

// Response status codes
enum class ResponseStatus : uint32_t {
    SUCCESS = 0,
    ERROR_INVALID_COMMAND = 1,
    ERROR_INVALID_PARAMETER = 2,
    ERROR_FILE_NOT_FOUND = 3,
    ERROR_PARSE_ERROR = 4,
    ERROR_PLAYBACK_ERROR = 5,
    ERROR_QUEUE_FULL = 6,
    ERROR_NOT_PLAYING = 7,
    ERROR_UNKNOWN = 999
};

// Playback states
enum class PlaybackState : uint32_t {
    STOPPED = 0,
    PLAYING = 1,
    PAUSED = 2,
    LOADING = 3,
    ERROR_STATE = 4
};

// Message header structure (fixed size)
struct MessageHeader {
    uint32_t version;       // Protocol version
    uint32_t command;       // CommandType
    uint32_t sequence;      // Sequence number for request/response matching
    uint32_t dataSize;      // Size of data following header
    uint32_t checksum;      // Simple checksum for data integrity
    uint32_t reserved[3];   // Reserved for future use
    
    MessageHeader() 
        : version(PROTOCOL_VERSION), command(0), sequence(0), 
          dataSize(0), checksum(0) {
        reserved[0] = reserved[1] = reserved[2] = 0;
    }
};

// Command-specific data structures

struct ConnectRequest {
    char clientName[64];    // Client identification
    uint32_t clientVersion; // Client version
    uint32_t flags;         // Connection flags
};

struct PlayABCRequest {
    uint32_t priority;      // Playback priority (0 = immediate, higher = queue)
    uint32_t flags;         // Playback flags (loop, etc.)
    uint32_t dataSize;      // Size of ABC data following
    // ABC notation data follows
};

struct PlayMidiRequest {
    uint32_t priority;      // Playback priority
    uint32_t flags;         // Playback flags
    uint32_t dataSize;      // Size of MIDI data following
    // MIDI data follows
};

struct QueueStatusRequest {
    uint32_t maxEntries;    // Maximum entries to return (0 = all)
};

struct SetVolumeRequest {
    float masterVolume;     // Master volume (0.0 - 1.0)
};

struct SetTempoRequest {
    float tempoMultiplier;  // Tempo multiplier (1.0 = normal, 2.0 = double speed)
};

struct SetVoiceVolumeRequest {
    uint32_t voiceId;       // Voice ID
    float volume;           // Voice volume (0.0 - 1.0)
};

struct SetVoiceInstrumentRequest {
    uint32_t voiceId;       // Voice ID
    uint32_t instrument;    // General MIDI instrument (0-127)
};

struct MuteVoiceRequest {
    uint32_t voiceId;       // Voice ID
    uint32_t muted;         // 1 = muted, 0 = unmuted
};

struct PlayBeepRequest {
    float frequency;        // Frequency in Hz
    float duration;         // Duration in seconds
    float volume;           // Volume (0.0 - 1.0)
};

struct PlayToneRequest {
    float frequency;        // Frequency in Hz
    float duration;         // Duration in seconds
    float volume;           // Volume (0.0 - 1.0)
    uint32_t waveform;      // Waveform type (0=sine, 1=square, 2=sawtooth, 3=triangle)
};

struct SeekRequest {
    double position;        // Position in seconds
};

// Response structures

struct ACKResponse {
    uint32_t status;        // ResponseStatus
    char message[256];      // Optional status message
};

struct StatusResponse {
    uint32_t state;         // PlaybackState
    float currentTime;      // Current playback time in seconds
    float totalTime;        // Total duration in seconds
    float volume;           // Current master volume
    float tempo;            // Current tempo multiplier
    uint32_t activeVoices;  // Number of active voices
    char currentFile[256];  // Currently playing file (if any)
};

struct QueueEntry {
    uint32_t id;            // Queue entry ID
    uint32_t type;          // 0 = ABC, 1 = MIDI
    uint32_t priority;      // Priority level
    float estimatedDuration; // Estimated duration in seconds
    char filename[256];     // Original filename (if any)
};

struct QueueStatusResponse {
    uint32_t totalEntries;  // Total entries in queue
    uint32_t currentIndex;  // Currently playing index
    uint32_t entryCount;    // Number of entries in this response
    // QueueEntry entries follow
};

struct PositionResponse {
    double currentPosition; // Current position in seconds
    double totalDuration;   // Total duration in seconds
};

struct ErrorResponse {
    uint32_t errorCode;     // ResponseStatus error code
    char errorMessage[512]; // Detailed error message
    char context[256];      // Context where error occurred
};

// Utility functions for protocol handling

class ProtocolHelper {
public:
    // Calculate simple checksum for data
    static uint32_t calculateChecksum(const void* data, size_t size);
    
    // Validate message header
    static bool validateHeader(const MessageHeader& header);
    
    // Create standard responses
    static MessageHeader createACK(uint32_t sequence, ResponseStatus status, 
                                  const std::string& message = "");
    static MessageHeader createNACK(uint32_t sequence, ResponseStatus status, 
                                   const std::string& message);
    static MessageHeader createError(uint32_t sequence, ResponseStatus errorCode, 
                                    const std::string& message);
    
    // Serialize/deserialize helpers
    static std::vector<uint8_t> serializeMessage(const MessageHeader& header, 
                                                const void* data = nullptr);
    static bool deserializeMessage(const std::vector<uint8_t>& buffer, 
                                  MessageHeader& header, std::vector<uint8_t>& data);
};

// Socket path helpers
class SocketPathHelper {
public:
    static std::string getDefaultSocketPath();
    static std::string getUserSocketPath(const std::string& username = "");
    static bool createSocketDirectory(const std::string& socketPath);
    static bool cleanupSocket(const std::string& socketPath);
};

} // namespace AudioDaemon
} // namespace SuperTerminal