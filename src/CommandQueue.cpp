//
//  CommandQueue.cpp
//  SuperTerminal Framework - Thread-Safe Command Queue Implementation
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Implementation of the command queue system for thread-safe API calls.
//

#include "CommandQueue.h"
#include <iostream>
#include <thread>

namespace SuperTerminal {

// Global command queue instance
CommandQueue g_command_queue;

// Track the main thread ID
static std::thread::id g_main_thread_id;
static bool g_initialized = false;

void command_queue_init() {
    if (g_initialized) {
        std::cout << "CommandQueue: Already initialized" << std::endl;
        return;
    }
    
    g_main_thread_id = std::this_thread::get_id();
    g_initialized = true;
    
    std::cout << "CommandQueue: Initialized on main thread" << std::endl;
}

void command_queue_shutdown() {
    if (!g_initialized) {
        return;
    }
    
    std::cout << "CommandQueue: Shutting down..." << std::endl;
    g_command_queue.shutdown();
    g_initialized = false;
    std::cout << "CommandQueue: Shutdown complete" << std::endl;
}

void command_queue_process() {
    if (!g_initialized) {
        return;
    }
    
    // Verify we're on the main thread
    if (std::this_thread::get_id() != g_main_thread_id) {
        std::cerr << "CommandQueue: ERROR - processCommands called from non-main thread!" << std::endl;
        return;
    }
    
    g_command_queue.processCommands();
}

bool command_queue_process_single() {
    if (!g_initialized) {
        return false;
    }
    
    // Verify we're on the main thread
    if (std::this_thread::get_id() != g_main_thread_id) {
        std::cerr << "CommandQueue: ERROR - processSingleCommand called from non-main thread!" << std::endl;
        return false;
    }
    
    return g_command_queue.processSingleCommand();
}

bool is_main_thread() {
    return g_initialized && (std::this_thread::get_id() == g_main_thread_id);
}

// Implementation of CommandQueue::isMainThread()
bool CommandQueue::isMainThread() const {
    return g_initialized && (std::this_thread::get_id() == g_main_thread_id);
}

// Template implementations are now in the header file

} // namespace SuperTerminal

// C wrapper functions for use from other modules
extern "C" {
    bool command_queue_process_single() {
        return SuperTerminal::command_queue_process_single();
    }
    
    void command_queue_process() {
        SuperTerminal::command_queue_process();
    }
    
    void command_queue_init() {
        SuperTerminal::command_queue_init();
    }
    
    void command_queue_shutdown() {
        SuperTerminal::command_queue_shutdown();
    }
}