//
//  ABCPlayerServiceProtocol.h
//  ABC Player XPC Service Protocol
//
//  Shared protocol between SuperTerminal and ABCPlayerService
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import <Foundation/Foundation.h>

// Error domain
extern NSString * const ABCPlayerErrorDomain;

typedef NS_ENUM(NSInteger, ABCPlayerErrorCode) {
    ABCPlayerErrorInvalidABC = 1,
    ABCPlayerErrorFileNotFound = 2,
    ABCPlayerErrorAudioFailure = 3,
    ABCPlayerErrorQueueFull = 4,
    ABCPlayerErrorNotInitialized = 5
};

// Status dictionary keys
extern NSString * const ABCPlayerStatusKeyQueueSize;
extern NSString * const ABCPlayerStatusKeyIsPlaying;
extern NSString * const ABCPlayerStatusKeyIsPaused;
extern NSString * const ABCPlayerStatusKeyVolume;
extern NSString * const ABCPlayerStatusKeyCurrentSong;

// Protocol that the XPC service implements
@protocol ABCPlayerServiceProtocol

// Playback control
- (void)playABC:(NSString *)abcNotation 
       withName:(NSString *)name 
          reply:(void (^)(BOOL success, NSError * _Nullable error))reply;

- (void)playABCFile:(NSString *)filePath 
              reply:(void (^)(BOOL success, NSError * _Nullable error))reply;

- (void)stopWithReply:(void (^)(void))reply;

- (void)pauseWithReply:(void (^)(BOOL success))reply;

- (void)resumeWithReply:(void (^)(BOOL success))reply;

- (void)clearQueueWithReply:(void (^)(void))reply;

// Configuration
- (void)setVolume:(float)volume 
            reply:(void (^)(void))reply;

// Status queries
- (void)getStatusWithReply:(void (^)(NSDictionary<NSString *, id> * _Nonnull status))reply;

- (void)getQueueListWithReply:(void (^)(NSArray<NSString *> * _Nonnull queue))reply;

// MIDI export
- (void)exportMIDI:(NSString *)abcNotation 
        toFile:(NSString *)midiFilePath 
         reply:(void (^)(BOOL success, NSError * _Nullable error))reply;

- (void)exportMIDIFromFile:(NSString *)abcFilePath 
                toMIDIFile:(NSString *)midiFilePath 
                     reply:(void (^)(BOOL success, NSError * _Nullable error))reply;

// Service management
- (void)pingWithReply:(void (^)(void))reply;

- (void)getVersionWithReply:(void (^)(NSString * _Nonnull version))reply;

@end

// Helper to create XPC interface
static inline NSXPCInterface * _Nonnull ABCPlayerServiceCreateInterface(void) {
    NSXPCInterface *interface = [NSXPCInterface interfaceWithProtocol:@protocol(ABCPlayerServiceProtocol)];
    return interface;
}
