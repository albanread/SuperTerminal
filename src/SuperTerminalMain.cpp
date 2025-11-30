//
//  SuperTerminalMain.cpp
//  SuperTerminal Framework - Main Entry Point with Subsystem Manager
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Main entry point for SuperTerminal that initializes all subsystems
//  through the SubsystemManager instead of requiring Lua to manage them
//

#include "SubsystemManager.h"
#include "GlobalShutdown.h"
#include "SuperTerminal.h"
#include "LuaRuntimeGCD.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <unistd.h>
#include <cstdlib>

// External function declaration
extern "C" void superterminal_run_event_loop();

// Global state for the main application
static std::atomic<bool> g_application_running{false};
static std::atomic<bool> g_user_app_finished{false};
static std::function<void()> g_user_app_function = nullptr;
static std::thread g_user_app_thread;

// Global termination flag (accessible to other modules to prevent operations during shutdown)
static std::atomic<bool> g_app_is_terminating{false};

// Application termination handler
extern "C" void superterminal_application_will_terminate() {
    static std::mutex termination_mutex;
    static bool termination_called = false;
    
    std::lock_guard<std::mutex> lock(termination_mutex);
    if (termination_called) {
        return; // Avoid multiple calls
    }
    termination_called = true;
    g_app_is_terminating = true;  // Signal all subsystems to stop operations
    
    std::cout << "SuperTerminalMain: Application will terminate - cleaning up..." << std::endl;
    
    // Stop ABC music player first to avoid window timer corruption
    std::cout << "SuperTerminalMain: Stopping ABC music player..." << std::endl;
    try {
        extern void abc_client_shutdown();
        extern void music_stop();
        extern void music_clear_queue();
        
        abc_client_shutdown();
        music_stop();
        music_clear_queue();
    } catch (...) {
        std::cerr << "SuperTerminalMain: Exception during audio shutdown, continuing..." << std::endl;
    }
    
    // Force stop any running Lua scripts immediately
    std::cout << "SuperTerminalMain: Stopping any running Lua scripts..." << std::endl;
    lua_gcd_stop_script();
    
    // Shutdown Lua runtime
    std::cout << "SuperTerminalMain: Shutting down Lua runtime..." << std::endl;
    lua_gcd_shutdown();
    
    // Start emphatic shutdown timer (nuclear option after 8 seconds)
    // Create and detach immediately to avoid any destructor issues
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::seconds(8));
        std::cerr << "\n*** EMPHATIC SHUTDOWN: FORCING PROCESS TERMINATION ***" << std::endl;
        std::cerr << "*** macOS will clean up any remaining threads and resources ***" << std::endl;
        std::fflush(stderr);
        _exit(1); // Immediate process termination, no cleanup
    }).detach();
    
    // Stop the user application first if it's running
    g_application_running = false;
    
    // Wait for user app thread to finish (with shorter timeout)
    if (g_user_app_thread.joinable()) {
        std::cout << "SuperTerminalMain: Waiting for user app thread to finish..." << std::endl;
        auto start_time = std::chrono::steady_clock::now();
        bool joined = false;
        
        // Try to join with reduced timeout (1 second instead of 2)
        while (!joined && std::chrono::steady_clock::now() - start_time < std::chrono::seconds(1)) {
            if (g_user_app_finished.load()) {
                g_user_app_thread.join();
                joined = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        if (!joined) {
            std::cerr << "SuperTerminalMain: User app thread didn't finish in time, detaching..." << std::endl;
            g_user_app_thread.detach();
        }
    }
    
    // Clean shutdown of subsystems with shorter timeout (2 seconds instead of 5)
    std::cout << "SuperTerminalMain: Shutting down subsystems..." << std::endl;
    if (!SubsystemManager::getInstance().shutdownAll(2000)) {
        std::cerr << "SuperTerminalMain: Clean shutdown failed during termination, forcing..." << std::endl;
        SubsystemManager::getInstance().forceShutdownAll();
        
        // Give force shutdown 2 seconds, then exit anyway
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cerr << "SuperTerminalMain: Force shutdown complete, exiting process..." << std::endl;
    }
    
    std::cout << "SuperTerminalMain: Termination cleanup complete" << std::endl;
    std::fflush(stdout);
    std::fflush(stderr);
    
    // Don't call exit(0) here - let NSApplication finish its termination sequence naturally
    // Calling exit(0) while AppKit is cleaning up menus causes memory corruption
    // The emphatic shutdown timer (8 seconds) will force exit if something hangs
}

// Signal handler for graceful shutdown
void signal_handler(int signal) {
    std::cout << "SuperTerminalMain: Received signal " << signal << ", initiating shutdown..." << std::endl;
    
    // Call the proper termination handler
    superterminal_application_will_terminate();
}

// User application wrapper that handles exceptions and completion
void user_app_wrapper() {
    try {
        std::cout << "SuperTerminalMain: Starting user application..." << std::endl;
        
        // Register this thread as an active subsystem
        register_active_subsystem();
        
        if (g_user_app_function) {
            g_user_app_function();
        }
        
        std::cout << "SuperTerminalMain: User application completed normally" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "SuperTerminalMain: Exception in user application: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "SuperTerminalMain: Unknown exception in user application" << std::endl;
    }
    
    // Unregister this thread
    unregister_active_subsystem();
    
    // Mark user app as finished
    g_user_app_finished = true;
}

// Main event loop that uses macOS event loop
void main_event_loop() {
    std::cout << "SuperTerminalMain: Starting macOS event loop..." << std::endl;
    
    // Use the proper macOS event loop instead of custom loop
    superterminal_run_event_loop();
    
    std::cout << "SuperTerminalMain: macOS event loop exited..." << std::endl;
    
    // Clean up after event loop exits
    std::cout << "SuperTerminalMain: Cleaning up after event loop exit..." << std::endl;
    
    // Wait for user app thread to finish if it's still running
    if (g_user_app_thread.joinable()) {
        std::cout << "SuperTerminalMain: Waiting for user application thread..." << std::endl;
        
        // Give it a reasonable time to finish
        auto start_wait = std::chrono::steady_clock::now();
        while (g_user_app_thread.joinable() && 
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start_wait).count() < 2000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        if (g_user_app_thread.joinable()) {
            std::cout << "SuperTerminalMain: Detaching user thread (timeout)" << std::endl;
            g_user_app_thread.detach();
        }
    }
    
    // Shutdown all subsystems
    const int SHUTDOWN_TIMEOUT_MS = 5000;
    if (!SubsystemManager::getInstance().shutdownAll(SHUTDOWN_TIMEOUT_MS)) {
        std::cerr << "SuperTerminalMain: Clean shutdown failed, forcing shutdown" << std::endl;
        SubsystemManager::getInstance().forceShutdownAll();
    }
    
    g_application_running = false;
}

// External C API implementation

extern "C" {

void superterminal_run(void (*app_start)(void)) {
    if (!app_start) {
        std::cerr << "SuperTerminalMain: No application function provided" << std::endl;
        return;
    }
    
    std::cout << "SuperTerminalMain: *** STARTING SUPERTERMINAL ***" << std::endl;
    
    // Install signal handlers for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // Register termination handler for proper cleanup
    std::atexit([]() {
        superterminal_application_will_terminate();
    });
    
    g_application_running = true;
    g_user_app_finished = false;
    g_user_app_function = app_start;
    
    try {
        // Initialize all subsystems with default configuration
        SubsystemConfig config = subsystem_manager_create_default_config();
        
        std::cout << "SuperTerminalMain: Initializing subsystems..." << std::endl;
        if (!SubsystemManager::getInstance().initializeAll(config)) {
            std::cerr << "SuperTerminalMain: Subsystem initialization failed" << std::endl;
            return;
        }
        
        std::cout << "SuperTerminalMain: All subsystems initialized successfully" << std::endl;
        
        // Start user application in background thread
        g_user_app_thread = std::thread(user_app_wrapper);
        
        // Run main event loop (this will block until shutdown)
        main_event_loop();
        
    } catch (const std::exception& e) {
        std::cerr << "SuperTerminalMain: Exception in main loop: " << e.what() << std::endl;
        SubsystemManager::getInstance().forceShutdownAll();
    } catch (...) {
        std::cerr << "SuperTerminalMain: Unknown exception in main loop" << std::endl;
        SubsystemManager::getInstance().forceShutdownAll();
    }
    
    std::cout << "SuperTerminalMain: *** SUPERTERMINAL SHUTDOWN COMPLETE ***" << std::endl;
}

void superterminal_exit(int code) {
    std::cout << "SuperTerminalMain: Exit requested with code " << code << std::endl;
    
    // Call proper termination handler
    superterminal_application_will_terminate();
    
    // This shouldn't be reached due to exit() in termination handler,
    // but just in case...
    exit(code);
}

// Check if application is terminating (exposed for other modules)
extern "C" bool superterminal_is_terminating() {
    return g_app_is_terminating.load();
}

// Enhanced initialization with custom configuration
bool superterminal_initialize_with_config(const SubsystemConfig* config) {
    if (!config) {
        return subsystem_manager_initialize_default();
    }
    
    std::cout << "SuperTerminalMain: Initializing with custom configuration" << std::endl;
    return SubsystemManager::getInstance().initializeAll(*config);
}

// Get subsystem manager instance for advanced use
void* superterminal_get_subsystem_manager(void) {
    return &SubsystemManager::getInstance();
}

// Check if SuperTerminal is running and healthy
bool superterminal_is_running(void) {
    return g_application_running.load() && SubsystemManager::getInstance().isSystemHealthy();
}

// Get performance metrics
char* superterminal_get_metrics(void) {
    return subsystem_manager_get_performance_metrics();
}

// Advanced shutdown control
void superterminal_request_shutdown(void) {
    std::cout << "SuperTerminalMain: Shutdown requested via API" << std::endl;
    superterminal_exit(0);
}

void superterminal_force_shutdown(void) {
    std::cout << "SuperTerminalMain: Force shutdown requested via API" << std::endl;
    
    // Start immediate emphatic shutdown (2 second timeout)
    // Create and detach immediately to avoid any destructor issues
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cerr << "\n*** FORCE SHUTDOWN: PROCESS TERMINATION ***" << std::endl;
        std::fflush(stderr);
        _exit(2); // Immediate forced exit
    }).detach();
    
    SubsystemManager::getInstance().forceShutdownAll();
    g_application_running = false;
    
    // Brief delay for force shutdown, then exit
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "SuperTerminalMain: Force shutdown complete, exiting..." << std::endl;
    std::fflush(stdout);
    exit(2);
}

// Register custom shutdown callback for user applications
void superterminal_register_shutdown_callback(void (*callback)(void), int priority) {
    if (callback) {
        SubsystemManager::getInstance().registerShutdownCallback(
            [callback]() { callback(); }, 
            priority
        );
    }
}

} // extern "C"

// Advanced C++ API for direct access to subsystems

namespace SuperTerminal {

class Application {
public:
    /**
     * Initialize SuperTerminal with custom configuration and run application
     * @param config Custom subsystem configuration
     * @param app_function Application entry point
     * @return Exit code (0 for success)
     */
    static int run(const SubsystemConfig& config, std::function<void()> app_function) {
        g_user_app_function = app_function;
        
        if (!SubsystemManager::getInstance().initializeAll(config)) {
            std::cerr << "Application: Failed to initialize SuperTerminal" << std::endl;
            return 1;
        }
        
        // Run the application using the C API
        superterminal_run([]() {
            if (g_user_app_function) {
                g_user_app_function();
            }
        });
        
        return 0;
    }
    
    /**
     * Get access to specific subsystems
     */
    static lua_State* getLuaState() {
        return SubsystemManager::getInstance().getLuaState();
    }
    
    /**
     * Check system health
     */
    static bool isHealthy() {
        return SubsystemManager::getInstance().isSystemHealthy();
    }
    
    /**
     * Register for shutdown notifications
     */
    static void onShutdown(std::function<void()> callback, int priority = 100) {
        SubsystemManager::getInstance().registerShutdownCallback(callback, priority);
    }
    
    /**
     * Request graceful shutdown
     */
    static void shutdown() {
        superterminal_exit(0);
    }
    
    /**
     * Force immediate shutdown
     */
    static void forceShutdown() {
        superterminal_force_shutdown();
    }
};

} // namespace SuperTerminal