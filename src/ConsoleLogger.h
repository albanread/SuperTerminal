//
//  ConsoleLogger.h
//  SuperTerminal Framework - NSLog Console Output
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Objective-C++ helper for console output via NSLog on the UI thread.
//

#ifndef CONSOLE_LOGGER_H
#define CONSOLE_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Output a message to the console using NSLog.
 * 
 * This function should be called from the UI/main thread.
 * Use the command queue to safely call this from Lua threads.
 * 
 * @param message The message to log
 */
void console_nslog(const char* message);

#ifdef __cplusplus
}
#endif

#endif // CONSOLE_LOGGER_H