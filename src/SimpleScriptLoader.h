//
//  SimpleScriptLoader.h
//  SuperTerminal Framework - Simple Script Loading System
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  This is a simple, reliable script loader that bypasses the problematic
//  emergency shutdown system to allow loading multiple scripts without deadlocks.
//

#ifndef SIMPLE_SCRIPT_LOADER_H
#define SIMPLE_SCRIPT_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Simple script execution function - executes Lua script content directly
 * This function handles stopping any currently running script and starting the new one
 * @param script_content Null-terminated Lua script content
 * @return true if script executed successfully, false otherwise
 */
bool simple_execute_script(const char* script_content);

/**
 * Simple script file loading function - loads and executes a Lua script from file
 * This function handles stopping any currently running script and loading the new one
 * @param filename Path to Lua script file
 * @return true if script loaded and executed successfully, false otherwise
 */
bool simple_load_script_file(const char* filename);

/**
 * Check if script loading is currently in progress
 * @return true if a script loading operation is active
 */
bool simple_script_loading_in_progress(void);

/**
 * Force stop current script (emergency function)
 * This function immediately stops any running script and cleans up the Lua state
 */
void simple_force_stop_script(void);

/**
 * Read file content into a string (utility function)
 * @param filename Path to file to read
 * @param content Pointer to receive allocated content string (caller must free)
 * @param length Pointer to receive content length
 * @return true if file was read successfully, false otherwise
 */
bool read_file_to_string(const char* filename, char** content, size_t* length);

#ifdef __cplusplus
}
#endif

#endif // SIMPLE_SCRIPT_LOADER_H