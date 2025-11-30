//
//  LuaRuntimeGCD.h
//  SuperTerminal - GCD-Based Lua Runtime
//
//  Modern, reliable Lua execution using Grand Central Dispatch
//  Copyright © 2025 SuperTerminal. All rights reserved.
//

#ifndef LUA_RUNTIME_GCD_H
#define LUA_RUNTIME_GCD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// MARK: - Initialization & Shutdown
// ============================================================================

/**
 * Initialize the GCD-based Lua runtime.
 * Creates the serial dispatch queue for script execution.
 * 
 * @return true if initialization succeeded, false otherwise
 */
bool lua_gcd_initialize(void);

/**
 * Shutdown the Lua runtime and cleanup resources.
 * Cancels any running scripts and releases the dispatch queue.
 */
void lua_gcd_shutdown(void);

/**
 * Check if the Lua runtime is initialized.
 * 
 * @return true if initialized, false otherwise
 */
bool lua_gcd_is_initialized(void);

// ============================================================================
// MARK: - Script Execution
// ============================================================================

/**
 * Execute a Lua script asynchronously on the Lua execution queue.
 * The script runs in its own isolated Lua state.
 * 
 * @param lua_code The Lua script source code (null-terminated string)
 * @param script_name Optional name for the script (for debugging/display)
 * @return true if script was queued successfully, false on error
 */
bool lua_gcd_exec(const char* lua_code, const char* script_name);

/**
 * Execute a Lua script from a file.
 * 
 * @param filename Path to the Lua script file
 * @return true if script was queued successfully, false on error
 */
bool lua_gcd_exec_file(const char* filename);

/**
 * Execute a single line of Lua code in the REPL (Read-Eval-Print Loop).
 * Uses a persistent Lua state that is maintained between calls.
 * 
 * @param lua_code Single line or expression to evaluate
 * @return true if execution succeeded, false on error
 */
bool lua_gcd_exec_repl(const char* lua_code);

// ============================================================================
// MARK: - Script Control
// ============================================================================

/**
 * Stop the currently running Lua script.
 * Sends a cancellation request to the executing dispatch block.
 * The script will stop at the next cancellation check point.
 * 
 * @return true if stop was requested, false if no script is running
 */
bool lua_gcd_stop_script(void);

/**
 * Check if a Lua script is currently executing.
 * 
 * @return true if a script is running, false otherwise
 */
bool lua_gcd_is_script_running(void);

/**
 * Get the name of the currently running script.
 * 
 * @return Script name, or NULL if no script is running
 */
const char* lua_gcd_get_current_script_name(void);

/**
 * Get the elapsed execution time of the current script in milliseconds.
 * 
 * @return Elapsed time in milliseconds, or 0 if no script is running
 */
uint64_t lua_gcd_get_script_elapsed_time_ms(void);

// ============================================================================
// MARK: - Error Handling
// ============================================================================

/**
 * Get the last error message from Lua execution.
 * 
 * @return Error message string, or empty string if no error
 */
const char* lua_gcd_get_last_error(void);

/**
 * Clear the last error message.
 */
void lua_gcd_clear_error(void);

// ============================================================================
// MARK: - Configuration
// ============================================================================

/**
 * Set the Lua hook frequency (instructions between cancellation checks).
 * Lower values = more responsive cancellation, but slightly slower execution.
 * 
 * @param instruction_count Number of Lua instructions between checks (default: 1000)
 */
void lua_gcd_set_hook_frequency(int instruction_count);

/**
 * Enable or disable verbose debug output.
 * 
 * @param enabled true to enable debug output, false to disable
 */
void lua_gcd_set_debug_output(bool enabled);

/**
 * Set the script timeout in seconds.
 * Scripts that run longer than this will be automatically cancelled.
 * Set to 0 to disable timeout (default).
 * 
 * @param timeout_seconds Timeout in seconds, or 0 for no timeout
 */
void lua_gcd_set_script_timeout(uint32_t timeout_seconds);

// ============================================================================
// MARK: - Statistics & Monitoring
// ============================================================================

/**
 * Get the total number of scripts executed since initialization.
 * 
 * @return Total script count
 */
uint64_t lua_gcd_get_total_scripts_executed(void);

/**
 * Get the number of scripts that completed successfully.
 * 
 * @return Successful script count
 */
uint64_t lua_gcd_get_successful_scripts_count(void);

/**
 * Get the number of scripts that failed with an error.
 * 
 * @return Failed script count
 */
uint64_t lua_gcd_get_failed_scripts_count(void);

/**
 * Get the number of scripts that were cancelled.
 * 
 * @return Cancelled script count
 */
uint64_t lua_gcd_get_cancelled_scripts_count(void);

/**
 * Reset all statistics counters to zero.
 */
void lua_gcd_reset_statistics(void);

// ============================================================================
// MARK: - REPL Management
// ============================================================================

/**
 * Reset the REPL Lua state.
 * Clears all variables and returns to a clean state.
 */
void lua_gcd_reset_repl(void);

/**
 * Shutdown the REPL Lua state.
 * Releases resources used by the persistent REPL.
 */
void lua_gcd_shutdown_repl(void);

// ============================================================================
// MARK: - Internal / Advanced
// ============================================================================

/**
 * Check if the current thread is executing on the Lua queue.
 * Useful for assertions and debugging.
 * 
 * @return true if current thread is on Lua execution queue
 */
bool lua_gcd_is_on_lua_queue(void);

/**
 * Execute a block synchronously on the Lua queue.
 * WARNING: This will deadlock if called from the Lua queue itself!
 * 
 * @param block The block to execute
 */
void lua_gcd_sync(void (^block)(void));

/**
 * Execute a block asynchronously on the Lua queue.
 * 
 * @param block The block to execute
 */
void lua_gcd_async(void (^block)(void));

#ifdef __cplusplus
}
#endif

#endif // LUA_RUNTIME_GCD_H
