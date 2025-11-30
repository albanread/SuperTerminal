//
//  ConsoleLogger.mm
//  SuperTerminal Framework - NSLog Console Output
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Objective-C++ helper for console output via NSLog on the UI thread.
//

#import <Foundation/Foundation.h>
#include "ConsoleLogger.h"

void console_nslog(const char* message) {
    if (!message) return;

    // Convert C string to NSString and use NSLog
    NSString *nsMessage = [NSString stringWithUTF8String:message];
    NSLog(@"[SuperTerminal Console] %@", nsMessage);
}
