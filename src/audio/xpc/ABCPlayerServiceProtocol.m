//
//  ABCPlayerServiceProtocol.m
//  ABC Player XPC Service Protocol Implementation
//
//  Shared constants for ABCPlayerService protocol
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import "ABCPlayerServiceProtocol.h"

// Error domain
NSString * const ABCPlayerErrorDomain = @"com.superterminal.ABCPlayer.error";

// Status dictionary keys
NSString * const ABCPlayerStatusKeyQueueSize = @"queueSize";
NSString * const ABCPlayerStatusKeyIsPlaying = @"isPlaying";
NSString * const ABCPlayerStatusKeyIsPaused = @"isPaused";
NSString * const ABCPlayerStatusKeyVolume = @"volume";
NSString * const ABCPlayerStatusKeyCurrentSong = @"currentSong";
