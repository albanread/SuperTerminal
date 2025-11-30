//
//  GlobalShutdown.h
//  SuperTerminal Framework - Global Emergency Shutdown System
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Global shutdown coordination system for clean termination of all subsystems
//

#ifndef GLOBAL_SHUTDOWN_H
#define GLOBAL_SHUTDOWN_H

#include <atomic>
#include <chrono>

#ifdef __cplusplus
extern "C" {
#endif

// Global emergency shutdown flag - checked by all subsystems every frame
extern std::atomic<bool> g_emergency_shutdown_requested;

// Shutdown timeout tracking
extern std::atomic<std::chrono::steady_clock::time_point> g_shutdown_start_time;
extern std::atomic<int> g_shutdown_timeout_ms;

// Subsystem registration for shutdown monitoring
extern std::atomic<int> g_active_subsystem_count;
extern std::atomic<bool> g_subsystems_shutdown_complete;

/**
 * Request emergency shutdown of all subsystems
 * @param timeout_ms Maximum time to wait for clean shutdown before force kill
 */
void request_emergency_shutdown(int timeout_ms = 2000);

/**
 * Check if emergency shutdown is requested - CALL THIS EVERY FRAME IN ALL SUBSYSTEMS
 * @return true if subsystem should terminate immediately
 */
bool is_emergency_shutdown_requested(void);

/**
 * Check if shutdown timeout has been exceeded
 * @return true if we should force kill everything now
 */
bool is_shutdown_timeout_exceeded(void);

/**
 * Register a subsystem as active (increment counter)
 */
void register_active_subsystem(void);

/**
 * Unregister a subsystem (decrement counter) - call when subsystem shuts down cleanly
 */
void unregister_active_subsystem(void);

/**
 * Get number of active subsystems still running
 */
int get_active_subsystem_count(void);

/**
 * Check if all subsystems have shut down cleanly
 */
bool are_all_subsystems_shutdown(void);

/**
 * Reset the shutdown system after successful termination
 */
void reset_shutdown_system(void);

/**
 * Force terminate all subsystems immediately (last resort)
 */
void force_terminate_all_subsystems(void);

#ifdef __cplusplus
}
#endif

#endif // GLOBAL_SHUTDOWN_H