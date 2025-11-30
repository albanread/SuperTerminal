//
//  ABCPlayerXPCClient.mm
//  SuperTerminal - ABC Player XPC Client Implementation
//
//  XPC-based client for ABC Player service
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "ABCPlayerXPCClient.h"
#import "xpc/ABCPlayerServiceProtocol.h"
#include "abc/ABCParser.h"

namespace SuperTerminal {

// Private implementation using Objective-C++ for XPC
class ABCPlayerXPCClient::Impl {
public:
    NSXPCConnection *connection;
    dispatch_queue_t queue;
    std::atomic<bool> connected;
    bool debug_output;

    Impl() : connection(nil), connected(false), debug_output(false) {
        queue = dispatch_queue_create("com.superterminal.ABCPlayerClient", DISPATCH_QUEUE_SERIAL);
    }

    ~Impl() {
        disconnect();
        if (queue) {
            // queue will be released by ARC
        }
    }

    bool connect() {
        if (connected) {
            return true;
        }

        @autoreleasepool {
            if (debug_output) {
                NSLog(@"[Client] Connecting to ABCPlayer XPC service");
            }

            // Create connection to XPC service
            connection = [[NSXPCConnection alloc] initWithServiceName:@"com.superterminal.ABCPlayer"];

            if (!connection) {
                if (debug_output) {
                    NSLog(@"[Client] Failed to create XPC connection");
                }
                return false;
            }

            // Set up interface
            connection.remoteObjectInterface = ABCPlayerServiceCreateInterface();

            // Handle connection lifecycle
            // Note: Not using __weak since we're in manual reference counting
            Impl* selfPtr = this;

            connection.interruptionHandler = ^{
                if (selfPtr && selfPtr->debug_output) {
                    NSLog(@"[Client] XPC connection interrupted");
                }
                selfPtr->connected = false;
                // Will automatically reconnect on next call
            };

            connection.invalidationHandler = ^{
                if (selfPtr && selfPtr->debug_output) {
                    NSLog(@"[Client] XPC connection invalidated");
                }
                selfPtr->connected = false;
                selfPtr->connection = nil;
            };

            [connection resume];
            connected = true;

            if (debug_output) {
                NSLog(@"[Client] Connected to ABCPlayer XPC service");
            }

            return true;
        }
    }

    void disconnect() {
        @autoreleasepool {
            if (connection) {
                if (debug_output) {
                    NSLog(@"[Client] Disconnecting from XPC service");
                }
                [connection invalidate];
                connection = nil;
            }
            connected = false;
        }
    }

    bool exportMIDI(const std::string& abc_notation, const std::string& midi_filename) {
        if (!connect()) {
            return false;
        }

        @autoreleasepool {
            NSString *abcString = [NSString stringWithUTF8String:abc_notation.c_str()];
            NSString *midiPath = [NSString stringWithUTF8String:midi_filename.c_str()];

            if (!abcString || !midiPath) {
                if (debug_output) {
                    NSLog(@"[Client] Invalid parameters for exportMIDI");
                }
                return false;
            }

            __block BOOL success = NO;
            dispatch_semaphore_t sema = dispatch_semaphore_create(0);

            id<ABCPlayerServiceProtocol> service = [connection synchronousRemoteObjectProxyWithErrorHandler:^(NSError *error) {
                if (debug_output) {
                    NSLog(@"[Client] Error getting remote proxy for exportMIDI: %@", error);
                }
                dispatch_semaphore_signal(sema);
            }];

            [service exportMIDI:abcString toFile:midiPath reply:^(BOOL result, NSError *error) {
                success = result;
                if (!result && debug_output) {
                    NSLog(@"[Client] exportMIDI failed: %@", error);
                }
                dispatch_semaphore_signal(sema);
            }];

            dispatch_semaphore_wait(sema, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC));
            return success;
        }
    }

    bool exportMIDIFromFile(const std::string& abc_filename, const std::string& midi_filename) {
        if (!connect()) {
            return false;
        }

        @autoreleasepool {
            NSString *abcPath = [NSString stringWithUTF8String:abc_filename.c_str()];
            NSString *midiPath = [NSString stringWithUTF8String:midi_filename.c_str()];

            if (!abcPath || !midiPath) {
                if (debug_output) {
                    NSLog(@"[Client] Invalid parameters for exportMIDIFromFile");
                }
                return false;
            }

            __block BOOL success = NO;
            dispatch_semaphore_t sema = dispatch_semaphore_create(0);

            id<ABCPlayerServiceProtocol> service = [connection synchronousRemoteObjectProxyWithErrorHandler:^(NSError *error) {
                if (debug_output) {
                    NSLog(@"[Client] Error getting remote proxy for exportMIDIFromFile: %@", error);
                }
                dispatch_semaphore_signal(sema);
            }];

            [service exportMIDIFromFile:abcPath toMIDIFile:midiPath reply:^(BOOL result, NSError *error) {
                success = result;
                if (!result && debug_output) {
                    NSLog(@"[Client] exportMIDIFromFile failed: %@", error);
                }
                dispatch_semaphore_signal(sema);
            }];

            dispatch_semaphore_wait(sema, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC));
            return success;
        }
    }

    id<ABCPlayerServiceProtocol> getService(NSError **errorOut) {
        if (!connected) {
            if (!connect()) {
                if (errorOut) {
                    *errorOut = [NSError errorWithDomain:ABCPlayerErrorDomain
                                                   code:ABCPlayerErrorNotInitialized
                                               userInfo:@{NSLocalizedDescriptionKey: @"Not connected to service"}];
                }
                return nil;
            }
        }

        // Use regular remote object proxy for async calls
        id<ABCPlayerServiceProtocol> proxy = [connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
            if (debug_output) {
                NSLog(@"[Client] Remote proxy error: %@", error);
            }
            if (errorOut) {
                *errorOut = error;
            }
        }];

        return proxy;
    }

    bool pingWithTimeout(int timeout_ms) {
        if (!connected) {
            if (!connect()) {
                return false;
            }
        }

        @autoreleasepool {
            __block NSError *error = nil;

            // Use synchronous proxy with timeout for health check
            id<ABCPlayerServiceProtocol> service = [connection synchronousRemoteObjectProxyWithErrorHandler:^(NSError *proxyError) {
                if (debug_output) {
                    NSLog(@"[Client] Synchronous proxy error: %@", proxyError);
                }
                error = proxyError;
            }];

            if (!service || error) {
                if (debug_output) {
                    NSLog(@"[Client] Failed to get synchronous proxy");
                }
                return false;
            }

            // Set up timeout using dispatch
            __block BOOL ping_succeeded = NO;
            __block BOOL ping_completed = NO;
            dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

            [service pingWithReply:^{
                ping_succeeded = YES;
                ping_completed = YES;
                dispatch_semaphore_signal(semaphore);
            }];

            // Wait for reply with timeout
            dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_ms * NSEC_PER_MSEC);
            long result = dispatch_semaphore_wait(semaphore, timeout);

            if (result != 0) {
                // Timeout occurred
                if (debug_output) {
                    NSLog(@"[Client] Ping timeout after %d ms", timeout_ms);
                }
                return false;
            }

            return ping_succeeded;
        }
    }
};

#pragma mark - ABCPlayerXPCClient Implementation

ABCPlayerXPCClient::ABCPlayerXPCClient()
    : impl_(nullptr)
    , initialized_(false)
    , debug_output_(false) {
}

ABCPlayerXPCClient::~ABCPlayerXPCClient() {
    shutdown();
}

bool ABCPlayerXPCClient::initialize() {
    if (initialized_) {
        return true;
    }

    @autoreleasepool {
        impl_ = new Impl();
        if (!impl_) {
            return false;
        }

        impl_->debug_output = debug_output_;

        // Try to connect
        if (!impl_->connect()) {
            delete impl_;
            impl_ = nullptr;
            return false;
        }

        // Test connection with ping and timeout to verify service starts
        if (debug_output_) {
            NSLog(@"[Client] Pinging XPC service with 3 second timeout...");
        }

        if (!impl_->pingWithTimeout(3000)) {
            if (debug_output_) {
                NSLog(@"[Client] XPC service failed to respond within timeout");
            }
            delete impl_;
            impl_ = nullptr;
            return false;
        }

        initialized_ = true;

        if (debug_output_) {
            NSLog(@"[Client] ABCPlayerXPCClient initialized successfully");
        }

        return true;
    }
}

void ABCPlayerXPCClient::shutdown() {
    if (!initialized_) {
        return;
    }

    @autoreleasepool {
        if (debug_output_) {
            NSLog(@"[Client] Shutting down ABCPlayerXPCClient");
        }

        if (impl_) {
            delete impl_;
            impl_ = nullptr;
        }

        initialized_ = false;
    }
}

bool ABCPlayerXPCClient::isInitialized() const {
    return initialized_;
}

bool ABCPlayerXPCClient::playABC(const std::string& abc_notation, const std::string& name) {
    if (!initialized_ || !impl_) {
        if (debug_output_) {
            NSLog(@"[Client] playABC called but not initialized");
        }
        return false;
    }

    @autoreleasepool {
        if (debug_output_) {
            NSLog(@"[Client] playABC called with %zu bytes of ABC notation, name: %s",
                  abc_notation.length(), name.c_str());
        }

        NSString *abc = [NSString stringWithUTF8String:abc_notation.c_str()];
        NSString *nm = [NSString stringWithUTF8String:name.empty() ? "Unnamed" : name.c_str()];

        NSError *error = nil;
        id<ABCPlayerServiceProtocol> service = impl_->getService(&error);
        if (!service) {
            if (debug_output_) {
                NSLog(@"[Client] Failed to get service proxy: %@", error);
            }
            return false;
        }

        if (debug_output_) {
            NSLog(@"[Client] Sending playABC to service (async)");
        }

        // Send the command asynchronously - don't wait for reply
        [service playABC:abc withName:nm reply:^(BOOL result, NSError *replyError) {
            if (debug_output_) {
                NSLog(@"[Client] playABC reply received: result=%d, error=%@", result, replyError);
            }
        }];

        // Return true immediately - command was sent successfully
        return true;
    }
}

bool ABCPlayerXPCClient::playABCFile(const std::string& filename) {
    if (!initialized_ || !impl_) {
        return false;
    }

    @autoreleasepool {
        NSString *path = [NSString stringWithUTF8String:filename.c_str()];

        NSError *error = nil;
        id<ABCPlayerServiceProtocol> service = impl_->getService(&error);
        if (!service) {
            return false;
        }

        __block BOOL success = NO;
        [service playABCFile:path reply:^(BOOL result, NSError *replyError) {
            success = result;
            if (replyError && debug_output_) {
                NSLog(@"[Client] playABCFile error: %@", replyError);
            }
        }];

        return success;
    }
}

bool ABCPlayerXPCClient::stop() {
    if (!initialized_ || !impl_) {
        return false;
    }

    @autoreleasepool {
        NSError *error = nil;
        id<ABCPlayerServiceProtocol> service = impl_->getService(&error);
        if (!service) {
            return false;
        }

        __block BOOL completed = NO;
        dispatch_semaphore_t sema = dispatch_semaphore_create(0);

        [service stopWithReply:^{
            completed = YES;
            dispatch_semaphore_signal(sema);
        }];

        // Wait for completion with timeout
        dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 1 * NSEC_PER_SEC);
        dispatch_semaphore_wait(sema, timeout);

        return completed;
    }
}

bool ABCPlayerXPCClient::pause() {
    if (!initialized_ || !impl_) {
        return false;
    }

    @autoreleasepool {
        NSError *error = nil;
        id<ABCPlayerServiceProtocol> service = impl_->getService(&error);
        if (!service) {
            return false;
        }

        __block BOOL success = NO;
        [service pauseWithReply:^(BOOL result) {
            success = result;
        }];

        return success;
    }
}

bool ABCPlayerXPCClient::resume() {
    if (!initialized_ || !impl_) {
        return false;
    }

    @autoreleasepool {
        NSError *error = nil;
        id<ABCPlayerServiceProtocol> service = impl_->getService(&error);
        if (!service) {
            return false;
        }

        __block BOOL success = NO;
        [service resumeWithReply:^(BOOL result) {
            success = result;
        }];

        return success;
    }
}

bool ABCPlayerXPCClient::clearQueue() {
    if (!initialized_ || !impl_) {
        return false;
    }

    @autoreleasepool {
        NSError *error = nil;
        id<ABCPlayerServiceProtocol> service = impl_->getService(&error);
        if (!service) {
            return false;
        }

        [service clearQueueWithReply:^{}];
        return true;
    }
}

bool ABCPlayerXPCClient::setVolume(float volume) {
    if (!initialized_ || !impl_) {
        return false;
    }

    @autoreleasepool {
        NSError *error = nil;
        id<ABCPlayerServiceProtocol> service = impl_->getService(&error);
        if (!service) {
            return false;
        }

        [service setVolume:volume reply:^{}];
        return true;
    }
}

ABCPlayerStatus ABCPlayerXPCClient::getStatus() {
    ABCPlayerStatus status = {};

    if (!initialized_ || !impl_) {
        return status;
    }

    @autoreleasepool {
        NSError *error = nil;
        id<ABCPlayerServiceProtocol> service = impl_->getService(&error);
        if (!service) {
            return status;
        }

        __block NSDictionary *dict = nil;
        [service getStatusWithReply:^(NSDictionary<NSString *,id> *result) {
            dict = result;
        }];

        if (dict) {
            status.queue_size = [dict[ABCPlayerStatusKeyQueueSize] intValue];
            status.is_playing = [dict[ABCPlayerStatusKeyIsPlaying] boolValue];
            status.is_paused = [dict[ABCPlayerStatusKeyIsPaused] boolValue];
            status.volume = [dict[ABCPlayerStatusKeyVolume] floatValue];

            NSString *song = dict[ABCPlayerStatusKeyCurrentSong];
            if (song) {
                status.current_song = [song UTF8String];
            }
        }

        return status;
    }
}

std::vector<std::string> ABCPlayerXPCClient::getQueueList() {
    std::vector<std::string> queue_list;

    if (!initialized_ || !impl_) {
        return queue_list;
    }

    @autoreleasepool {
        NSError *error = nil;
        id<ABCPlayerServiceProtocol> service = impl_->getService(&error);
        if (!service) {
            return queue_list;
        }

        __block NSArray *array = nil;
        [service getQueueListWithReply:^(NSArray<NSString *> *result) {
            array = result;
        }];

        if (array) {
            for (NSString *item in array) {
                queue_list.push_back([item UTF8String]);
            }
        }

        return queue_list;
    }
}

bool ABCPlayerXPCClient::ping() {
    if (!initialized_ || !impl_) {
        return false;
    }

    @autoreleasepool {
        NSError *error = nil;
        id<ABCPlayerServiceProtocol> service = impl_->getService(&error);
        if (!service) {
            return false;
        }

        __block BOOL success = NO;
        [service pingWithReply:^{
            success = YES;
        }];

        return success;
    }
}

bool ABCPlayerXPCClient::pingWithTimeout(int timeout_ms) {
    if (!initialized_ || !impl_) {
        return false;
    }
    return impl_->pingWithTimeout(timeout_ms);
}

std::string ABCPlayerXPCClient::getVersion() {
    // Get version directly from the local ABC parser - no need for XPC
    return ABCPlayer::ABCParser::getVersion();
}

void ABCPlayerXPCClient::setDebugOutput(bool debug) {
    debug_output_ = debug;
    if (impl_) {
        impl_->debug_output = debug;
    }
}

bool ABCPlayerXPCClient::exportMIDI(const std::string& abc_notation, const std::string& midi_filename) {
    if (!impl_) {
        return false;
    }
    return impl_->exportMIDI(abc_notation, midi_filename);
}

bool ABCPlayerXPCClient::exportMIDIFromFile(const std::string& abc_filename, const std::string& midi_filename) {
    if (!impl_) {
        return false;
    }
    return impl_->exportMIDIFromFile(abc_filename, midi_filename);
}

} // namespace SuperTerminal

#pragma mark - C API Implementation

extern "C" {

// Thread Safety Note:
// Audio operations use XPC (inter-process communication) which provides:
// 1. Process isolation (audio service runs in separate process)
// 2. Kernel-level thread safety (Mach ports handle synchronization)
// 3. Blocking behavior on caller's thread (Lua thread blocks until response)
// However, g_abc_client access must be protected with a mutex to prevent
// race conditions during initialization/shutdown from multiple threads.

static SuperTerminal::ABCPlayerXPCClient* g_abc_client = nullptr;
static std::mutex g_abc_client_mutex;

bool abc_client_initialize() {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client) {
        g_abc_client = new SuperTerminal::ABCPlayerXPCClient();
    }
    return g_abc_client->initialize();
}

void abc_client_shutdown() {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (g_abc_client) {
        g_abc_client->shutdown();
        delete g_abc_client;
        g_abc_client = nullptr;
    }
}

bool abc_client_is_initialized() {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);
    return g_abc_client && g_abc_client->isInitialized();
}

bool abc_client_play_abc(const char* abc_notation, const char* name) {
    NSLog(@"[C API] abc_client_play_abc called: abc_notation=%p, name=%s", abc_notation, name ? name : "NULL");

    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client) {
        NSLog(@"[C API] abc_client_play_abc: g_abc_client is NULL!");
        return false;
    }

    NSLog(@"[C API] abc_client_play_abc: calling playABC on client");
    bool result = g_abc_client->playABC(abc_notation, name ? name : "");
    NSLog(@"[C API] abc_client_play_abc: playABC returned %d", result);
    return result;
}

bool abc_client_play_abc_file(const char* filename) {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client) {
        return false;
    }
    return g_abc_client->playABCFile(filename);
}

bool abc_client_stop() {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client) {
        return false;
    }
    return g_abc_client->stop();
}

bool abc_client_pause() {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client) {
        return false;
    }
    return g_abc_client->pause();
}

bool abc_client_resume() {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client) {
        return false;
    }
    return g_abc_client->resume();
}

bool abc_client_clear_queue() {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client) {
        return false;
    }
    return g_abc_client->clearQueue();
}

bool abc_client_set_volume(float volume) {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client) {
        return false;
    }
    return g_abc_client->setVolume(volume);
}

bool abc_client_is_playing() {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client) {
        return false;
    }
    auto status = g_abc_client->getStatus();
    return status.is_playing;
}

bool abc_client_is_paused() {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client) {
        return false;
    }
    auto status = g_abc_client->getStatus();
    return status.is_paused;
}

int abc_client_get_queue_size() {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client) {
        return 0;
    }
    auto status = g_abc_client->getStatus();
    return status.queue_size;
}

float abc_client_get_volume() {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client) {
        return 0.0f;
    }
    auto status = g_abc_client->getStatus();
    return status.volume;
}

const char* abc_client_get_current_song() {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client) {
        return "";
    }
    auto status = g_abc_client->getStatus();
    static std::string current_song_cache;
    current_song_cache = status.current_song;
    return current_song_cache.c_str();
}

void abc_client_set_debug_output(bool debug) {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (g_abc_client) {
        g_abc_client->setDebugOutput(debug);
    }
}

void abc_client_set_auto_start_server(bool auto_start) {
    // XPC services are automatically managed by launchd
    // This function exists for compatibility with old Unix socket API
    // but is a no-op for XPC
    (void)auto_start;  // Suppress unused parameter warning
}

bool abc_client_ping_with_timeout(int timeout_ms) {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client) {
        return false;
    }
    return g_abc_client->pingWithTimeout(timeout_ms);
}

const char* abc_client_get_version() {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client) {
        return "unknown";
    }
    static std::string version_cache;
    version_cache = g_abc_client->getVersion();
    return version_cache.c_str();
}

bool abc_client_export_midi(const char* abc_notation, const char* midi_filename) {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client || !abc_notation || !midi_filename) {
        return false;
    }
    return g_abc_client->exportMIDI(abc_notation, midi_filename);
}

bool abc_client_export_midi_from_file(const char* abc_filename, const char* midi_filename) {
    std::lock_guard<std::mutex> lock(g_abc_client_mutex);

    if (!g_abc_client || !abc_filename || !midi_filename) {
        return false;
    }
    return g_abc_client->exportMIDIFromFile(abc_filename, midi_filename);
}

} // extern "C"
