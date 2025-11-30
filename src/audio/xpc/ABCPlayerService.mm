//
//  ABCPlayerService.mm
//  ABC Player XPC Service - Full Implementation
//
//  XPC service that provides ABC music notation playback with queue management
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "ABCPlayerServiceProtocol.h"
#import "ABCPlayer.h"

#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <condition_variable>

// Song queue entry
struct QueuedSong {
    std::string abc_content;
    std::string name;

    QueuedSong(const std::string& content, const std::string& n)
        : abc_content(content), name(n) {}
};

// Service implementation with C++ integration
@interface ABCPlayerService : NSObject <ABCPlayerServiceProtocol> {
@private
    // Player and queue management
    std::unique_ptr<ABCPlayer::Player> player_;
    std::queue<QueuedSong> song_queue_;
    std::mutex queue_mutex_;
    std::mutex player_mutex_;

    // Playback state
    std::atomic<bool> is_playing_;
    std::atomic<bool> is_paused_;
    std::atomic<float> volume_;
    std::string current_song_name_;

    // Worker thread for playback
    std::thread playback_thread_;
    std::atomic<bool> should_stop_thread_;
    std::condition_variable queue_cv_;

    // Configuration
    BOOL debug_output_;
}

- (instancetype)init;
- (void)dealloc;

// Internal methods
- (void)playbackLoop;
- (BOOL)playNextInQueue;
- (void)ensurePlayerInitialized;

@end

@implementation ABCPlayerService

- (instancetype)init {
    self = [super init];
    if (self) {
        // Initialize state
        is_playing_ = false;
        is_paused_ = false;
        volume_ = 1.0f;
        should_stop_thread_ = false;
        debug_output_ = YES;

        // Create ABC player
        player_ = std::make_unique<ABCPlayer::Player>();
        player_->setVerbose(false);
        player_->setVolume(volume_);
        player_->setSynchronousMode(true);  // Run playback on worker thread

        // Start playback worker thread
        playback_thread_ = std::thread([self]() {
            [self playbackLoop];
        });

        if (debug_output_) {
            NSLog(@"[ABCPlayerService] Initialized successfully");
        }
    }
    return self;
}

- (void)dealloc {
    if (debug_output_) {
        NSLog(@"[ABCPlayerService] Shutting down...");
    }

    // Stop playback thread
    should_stop_thread_ = true;
    queue_cv_.notify_all();

    if (playback_thread_.joinable()) {
        playback_thread_.join();
    }

    // Stop player
    if (player_) {
        std::lock_guard<std::mutex> lock(player_mutex_);
        player_->stop();
    }
}

- (void)ensurePlayerInitialized {
    std::lock_guard<std::mutex> lock(player_mutex_);
    if (!player_) {
        player_ = std::make_unique<ABCPlayer::Player>();
        player_->setVerbose(debug_output_);
        player_->setVolume(volume_);
        player_->setSynchronousMode(true);
    }
}

- (void)playbackLoop {
    @autoreleasepool {
        while (!should_stop_thread_) {
            // Wait for songs in queue
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [self] {
                return !song_queue_.empty() || should_stop_thread_;
            });

            if (should_stop_thread_) {
                break;
            }

            // Get next song
            if (!song_queue_.empty()) {
                QueuedSong song = song_queue_.front();
                song_queue_.pop();
                lock.unlock();

                // Play the song
                current_song_name_ = song.name;

                if (debug_output_) {
                    NSLog(@"[ABCPlayerService] Playing: %s", song.name.c_str());
                }

                std::lock_guard<std::mutex> player_lock(player_mutex_);

                // Load and play ABC
                if (player_->loadABC(song.abc_content)) {
                    is_playing_ = true;
                    is_paused_ = false;

                    if (player_->play()) {
                        // Wait for playback to complete
                        // The player runs in synchronous mode on this thread
                        while (player_->isPlaying() && !should_stop_thread_) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                    } else {
                        NSLog(@"[ABCPlayerService] Failed to start playback: %s", song.name.c_str());

                        // Log errors
                        const auto& errors = player_->getErrors();
                        for (const auto& error : errors) {
                            NSLog(@"[ABCPlayerService] Error: %s", error.c_str());
                        }
                    }

                    is_playing_ = false;
                } else {
                    NSLog(@"[ABCPlayerService] Failed to load ABC: %s", song.name.c_str());

                    const auto& errors = player_->getErrors();
                    for (const auto& error : errors) {
                        NSLog(@"[ABCPlayerService] Parse error: %s", error.c_str());
                    }
                }

                current_song_name_ = "";
            }
        }
    }
}

- (BOOL)playNextInQueue {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    if (song_queue_.empty()) {
        is_playing_ = false;
        return NO;
    }

    // Notify playback thread
    queue_cv_.notify_one();
    return YES;
}

#pragma mark - ABCPlayerServiceProtocol Implementation

- (void)playABC:(NSString *)abcNotation
       withName:(NSString *)name
          reply:(void (^)(BOOL success, NSError * _Nullable error))reply {
    @autoreleasepool {
        if (debug_output_) {
            NSLog(@"[ABCPlayerService] playABC called: %@", name);
        }

        if (!abcNotation || [abcNotation length] == 0) {
            NSError *error = [NSError errorWithDomain:ABCPlayerErrorDomain
                                                code:ABCPlayerErrorInvalidABC
                                            userInfo:@{NSLocalizedDescriptionKey: @"ABC notation is empty"}];
            reply(NO, error);
            return;
        }

        // Convert to C++ string
        std::string abc_content([abcNotation UTF8String]);
        std::string song_name = name ? [name UTF8String] : "Untitled";

        // Stop current playback
        {
            std::lock_guard<std::mutex> lock(player_mutex_);
            if (player_ && is_playing_) {
                player_->stop();
            }
            is_playing_ = false;
            is_paused_ = false;
        }

        // Clear queue and add new song
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            while (!song_queue_.empty()) {
                song_queue_.pop();
            }
            song_queue_.push(QueuedSong(abc_content, song_name));
        }

        // Start playback
        [self playNextInQueue];

        reply(YES, nil);
    }
}

- (void)playABCFile:(NSString *)filePath
              reply:(void (^)(BOOL success, NSError * _Nullable error))reply {
    @autoreleasepool {
        if (debug_output_) {
            NSLog(@"[ABCPlayerService] playABCFile called: %@", filePath);
        }

        if (!filePath || [filePath length] == 0) {
            NSError *error = [NSError errorWithDomain:ABCPlayerErrorDomain
                                                code:ABCPlayerErrorFileNotFound
                                            userInfo:@{NSLocalizedDescriptionKey: @"File path is empty"}];
            reply(NO, error);
            return;
        }

        // Read file
        NSError *readError = nil;
        NSString *content = [NSString stringWithContentsOfFile:filePath
                                                      encoding:NSUTF8StringEncoding
                                                         error:&readError];

        if (readError || !content) {
            NSError *error = [NSError errorWithDomain:ABCPlayerErrorDomain
                                                code:ABCPlayerErrorFileNotFound
                                            userInfo:@{NSLocalizedDescriptionKey:
                                                      [NSString stringWithFormat:@"Failed to read file: %@",
                                                       readError.localizedDescription]}];
            reply(NO, error);
            return;
        }

        // Extract filename for display
        NSString *fileName = [[filePath lastPathComponent] stringByDeletingPathExtension];

        // Use playABC to handle the actual playback
        [self playABC:content withName:fileName reply:reply];
    }
}

- (void)stopWithReply:(void (^)(void))reply {
    @autoreleasepool {
        if (debug_output_) {
            NSLog(@"[ABCPlayerService] stop called");
        }

        // Stop player
        {
            std::lock_guard<std::mutex> lock(player_mutex_);
            if (player_) {
                player_->stop();
            }
            is_playing_ = false;
            is_paused_ = false;
        }

        // Clear queue
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            while (!song_queue_.empty()) {
                song_queue_.pop();
            }
        }

        current_song_name_ = "";

        reply();
    }
}

- (void)pauseWithReply:(void (^)(BOOL))reply {
    @autoreleasepool {
        if (debug_output_) {
            NSLog(@"[ABCPlayerService] pause called");
        }

        BOOL success = NO;

        {
            std::lock_guard<std::mutex> lock(player_mutex_);
            if (player_ && is_playing_ && !is_paused_) {
                if (player_->pause()) {
                    is_paused_ = true;
                    success = YES;
                }
            }
        }

        reply(success);
    }
}

- (void)resumeWithReply:(void (^)(BOOL))reply {
    @autoreleasepool {
        if (debug_output_) {
            NSLog(@"[ABCPlayerService] resume called");
        }

        BOOL success = NO;

        {
            std::lock_guard<std::mutex> lock(player_mutex_);
            if (player_ && is_paused_) {
                if (player_->play()) {
                    is_paused_ = false;
                    success = YES;
                }
            }
        }

        reply(success);
    }
}

- (void)clearQueueWithReply:(void (^)(void))reply {
    @autoreleasepool {
        if (debug_output_) {
            NSLog(@"[ABCPlayerService] clearQueue called");
        }

        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!song_queue_.empty()) {
            song_queue_.pop();
        }

        reply();
    }
}

- (void)setVolume:(float)volume reply:(void (^)(void))reply {
    @autoreleasepool {
        if (debug_output_) {
            NSLog(@"[ABCPlayerService] setVolume called: %.2f", volume);
        }

        // Clamp volume to valid range
        volume = std::max(0.0f, std::min(1.0f, volume));
        volume_ = volume;

        {
            std::lock_guard<std::mutex> lock(player_mutex_);
            if (player_) {
                player_->setVolume(volume);
            }
        }

        reply();
    }
}

- (void)getStatusWithReply:(void (^)(NSDictionary<NSString *,id> * _Nonnull))reply {
    @autoreleasepool {
        int queue_size = 0;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            queue_size = static_cast<int>(song_queue_.size());
        }

        NSString *current_song = [NSString stringWithUTF8String:current_song_name_.c_str()];
        if (!current_song) {
            current_song = @"";
        }

        NSDictionary *status = @{
            ABCPlayerStatusKeyQueueSize: @(queue_size),
            ABCPlayerStatusKeyIsPlaying: @(is_playing_.load()),
            ABCPlayerStatusKeyIsPaused: @(is_paused_.load()),
            ABCPlayerStatusKeyVolume: @(volume_.load()),
            ABCPlayerStatusKeyCurrentSong: current_song
        };

        if (debug_output_) {
            NSLog(@"[ABCPlayerService] getStatus: playing=%d, paused=%d, queue=%d",
                  is_playing_.load(), is_paused_.load(), queue_size);
        }

        reply(status);
    }
}

- (void)getQueueListWithReply:(void (^)(NSArray<NSString *> * _Nonnull))reply {
    @autoreleasepool {
        NSMutableArray<NSString *> *queue_list = [NSMutableArray array];

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);

            // Copy queue to temporary to iterate
            std::queue<QueuedSong> temp_queue = song_queue_;

            while (!temp_queue.empty()) {
                const QueuedSong& song = temp_queue.front();
                NSString *name = [NSString stringWithUTF8String:song.name.c_str()];
                if (name) {
                    [queue_list addObject:name];
                }
                temp_queue.pop();
            }
        }

        if (debug_output_) {
            NSLog(@"[ABCPlayerService] getQueueList: %lu items", (unsigned long)[queue_list count]);
        }

        reply(queue_list);
    }
}

- (void)pingWithReply:(void (^)(void))reply {
    if (debug_output_) {
        NSLog(@"[ABCPlayerService] ping called");
    }
    reply();
}

- (void)getVersionWithReply:(void (^)(NSString * _Nonnull version))reply {
    if (debug_output_) {
        NSLog(@"[ABCPlayerService] getVersion called");
    }

    // Get version from the ABC parser
    std::string version_str;
    {
        std::lock_guard<std::mutex> lock(player_mutex_);
        if (player_) {
            version_str = player_->getParserVersion();
        } else {
            version_str = ABCPlayer::ABCParser::getVersion();
        }
    }

    NSString *versionString = [NSString stringWithUTF8String:version_str.c_str()];

    if (debug_output_) {
        NSLog(@"[ABCPlayerService] Returning version: %@", versionString);
    }

    reply(versionString);
}

- (void)exportMIDI:(NSString *)abcNotation
            toFile:(NSString *)midiFilePath
             reply:(void (^)(BOOL success, NSError * _Nullable error))reply {
    @autoreleasepool {
        if (!abcNotation || !midiFilePath) {
            NSError *error = [NSError errorWithDomain:ABCPlayerErrorDomain
                                               code:ABCPlayerErrorInvalidABC
                                           userInfo:@{NSLocalizedDescriptionKey: @"Invalid parameters"}];
            reply(NO, error);
            return;
        }

        if (debug_output_) {
            NSLog(@"[ABCPlayerService] exportMIDI to: %@", midiFilePath);
        }

        std::string abc_content = [abcNotation UTF8String];
        std::string midi_path = [midiFilePath UTF8String];

        // Ensure player is initialized
        [self ensurePlayerInitialized];

        std::lock_guard<std::mutex> lock(player_mutex_);

        // Load the ABC notation
        if (!player_->loadABC(abc_content)) {
            NSString *errorMsg = @"Failed to parse ABC notation";
            const auto& errors = player_->getErrors();
            if (!errors.empty()) {
                NSMutableString *details = [NSMutableString string];
                for (const auto& err : errors) {
                    [details appendFormat:@"%s\n", err.c_str()];
                }
                errorMsg = [NSString stringWithFormat:@"Failed to parse ABC notation:\n%@", details];
            }

            NSError *error = [NSError errorWithDomain:ABCPlayerErrorDomain
                                               code:ABCPlayerErrorInvalidABC
                                           userInfo:@{NSLocalizedDescriptionKey: errorMsg}];
            reply(NO, error);
            return;
        }

        // Export to MIDI file
        if (!player_->exportMIDI(midi_path)) {
            NSString *errorMsg = @"Failed to export MIDI file";
            const auto& errors = player_->getErrors();
            if (!errors.empty()) {
                NSMutableString *details = [NSMutableString string];
                for (const auto& err : errors) {
                    [details appendFormat:@"%s\n", err.c_str()];
                }
                errorMsg = [NSString stringWithFormat:@"Failed to export MIDI:\n%@", details];
            }

            NSError *error = [NSError errorWithDomain:ABCPlayerErrorDomain
                                               code:ABCPlayerErrorAudioFailure
                                           userInfo:@{NSLocalizedDescriptionKey: errorMsg}];
            reply(NO, error);
            return;
        }

        if (debug_output_) {
            NSLog(@"[ABCPlayerService] Successfully exported MIDI to: %@", midiFilePath);
        }

        reply(YES, nil);
    }
}

- (void)exportMIDIFromFile:(NSString *)abcFilePath
                toMIDIFile:(NSString *)midiFilePath
                     reply:(void (^)(BOOL success, NSError * _Nullable error))reply {
    @autoreleasepool {
        if (!abcFilePath || !midiFilePath) {
            NSError *error = [NSError errorWithDomain:ABCPlayerErrorDomain
                                               code:ABCPlayerErrorInvalidABC
                                           userInfo:@{NSLocalizedDescriptionKey: @"Invalid parameters"}];
            reply(NO, error);
            return;
        }

        if (debug_output_) {
            NSLog(@"[ABCPlayerService] exportMIDIFromFile: %@ -> %@", abcFilePath, midiFilePath);
        }

        // Read ABC file
        NSError *readError = nil;
        NSString *abcContent = [NSString stringWithContentsOfFile:abcFilePath
                                                         encoding:NSUTF8StringEncoding
                                                            error:&readError];

        if (!abcContent) {
            NSString *errorMsg = [NSString stringWithFormat:@"Failed to read ABC file: %@",
                                 readError ? readError.localizedDescription : @"Unknown error"];
            NSError *error = [NSError errorWithDomain:ABCPlayerErrorDomain
                                               code:ABCPlayerErrorFileNotFound
                                           userInfo:@{NSLocalizedDescriptionKey: errorMsg}];
            reply(NO, error);
            return;
        }

        // Use the exportMIDI method with the content
        [self exportMIDI:abcContent toFile:midiFilePath reply:reply];
    }
}

@end

#pragma mark - XPC Service Delegate

@interface ServiceDelegate : NSObject <NSXPCListenerDelegate>
@end

@implementation ServiceDelegate

- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)newConnection {
    NSLog(@"[ABCPlayerService] New connection from client");

    // Set up the connection
    newConnection.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(ABCPlayerServiceProtocol)];

    // Create service instance for this connection
    ABCPlayerService *exportedObject = [[ABCPlayerService alloc] init];
    newConnection.exportedObject = exportedObject;

    // Handle connection lifecycle
    newConnection.invalidationHandler = ^{
        NSLog(@"[ABCPlayerService] Connection invalidated");
    };

    newConnection.interruptionHandler = ^{
        NSLog(@"[ABCPlayerService] Connection interrupted");
    };

    [newConnection resume];

    return YES;
}

@end

#pragma mark - Main Entry Point

int main(int argc, const char *argv[])
{
    @autoreleasepool {
        NSLog(@"[ABCPlayerService] Starting XPC service...");

        // Create service delegate
        ServiceDelegate *delegate = [[ServiceDelegate alloc] init];

        // Set up XPC listener
        NSXPCListener *listener = [NSXPCListener serviceListener];
        listener.delegate = delegate;

        // Start listening
        [listener resume];

        NSLog(@"[ABCPlayerService] Ready to accept connections");

        // Run the service
        [[NSRunLoop currentRunLoop] run];
    }

    return 0;
}
