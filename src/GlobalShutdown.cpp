//
//  GlobalShutdown.cpp
//  SuperTerminal Framework - Global Emergency Shutdown System
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Implementation of global shutdown coordination system
//

#include "GlobalShutdown.h"
#include <iostream>
#include <mutex>

// Global shutdown state variables
std::atomic<bool> g_emergency_shutdown_requested{false};
std::atomic<std::chrono::steady_clock::time_point> g_shutdown_start_time{std::chrono::steady_clock::time_point{}};
std::atomic<int> g_shutdown_timeout_ms{2000};
std::atomic<int> g_active_subsystem_count{0};
std::atomic<bool> g_subsystems_shutdown_complete{false};

// Mutex for thread-safe operations
static std::mutex g_shutdown_mutex;

void request_emergency_shutdown(int timeout_ms) {
    std::lock_guard<std::mutex> lock(g_shutdown_mutex);
    
    if (g_emergency_shutdown_requested.load()) {
        std::cout << "GlobalShutdown: Emergency shutdown already in progress" << std::endl;
        return;
    }
    
    std::cout << "GlobalShutdown: *** EMERGENCY SHUTDOWN REQUESTED ***" << std::endl;
    std::cout << "GlobalShutdown: Timeout: " << timeout_ms << "ms" << std::endl;
    std::cout << "GlobalShutdown: Active subsystems: " << g_active_subsystem_count.load() << std::endl;
    
    g_shutdown_timeout_ms = timeout_ms;
    g_shutdown_start_time = std::chrono::steady_clock::now();
    g_emergency_shutdown_requested = true;
    g_subsystems_shutdown_complete = false;
    
    std::cout << "GlobalShutdown: All subsystems should check is_emergency_shutdown_requested() and terminate" << std::endl;
}

bool is_emergency_shutdown_requested(void) {
    return g_emergency_shutdown_requested.load();
}

bool is_shutdown_timeout_exceeded(void) {
    if (!g_emergency_shutdown_requested.load()) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto start = g_shutdown_start_time.load();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    
    return elapsed_ms > g_shutdown_timeout_ms.load();
}

void register_active_subsystem(void) {
    int count = g_active_subsystem_count.fetch_add(1) + 1;
    std::cout << "GlobalShutdown: Registered subsystem (total active: " << count << ")" << std::endl;
}

void unregister_active_subsystem(void) {
    int count = g_active_subsystem_count.fetch_sub(1) - 1;
    std::cout << "GlobalShutdown: Unregistered subsystem (remaining active: " << count << ")" << std::endl;
    
    if (count <= 0 && g_emergency_shutdown_requested.load()) {
        g_subsystems_shutdown_complete = true;
        std::cout << "GlobalShutdown: *** ALL SUBSYSTEMS SHUTDOWN COMPLETE ***" << std::endl;
    }
}

int get_active_subsystem_count(void) {
    return g_active_subsystem_count.load();
}

bool are_all_subsystems_shutdown(void) {
    return g_subsystems_shutdown_complete.load() || (g_active_subsystem_count.load() <= 0);
}

void reset_shutdown_system(void) {
    std::lock_guard<std::mutex> lock(g_shutdown_mutex);
    
    std::cout << "GlobalShutdown: Resetting shutdown system" << std::endl;
    
    g_emergency_shutdown_requested = false;
    g_subsystems_shutdown_complete = false;
    g_shutdown_timeout_ms = 2000;
    g_shutdown_start_time = std::chrono::steady_clock::time_point{};
    
    // Note: We don't reset g_active_subsystem_count as subsystems may still be active
    
    std::cout << "GlobalShutdown: Shutdown system reset complete" << std::endl;
}

void force_terminate_all_subsystems(void) {
    std::lock_guard<std::mutex> lock(g_shutdown_mutex);
    
    std::cout << "GlobalShutdown: *** FORCE TERMINATING ALL SUBSYSTEMS ***" << std::endl;
    std::cout << "GlobalShutdown: WARNING - This is an emergency measure, crashes may occur" << std::endl;
    
    // Set flags to indicate force termination
    g_emergency_shutdown_requested = true;
    g_subsystems_shutdown_complete = true;
    g_active_subsystem_count = 0;
    
    // Additional force termination measures could be added here
    // For now, we rely on subsystems checking the emergency flag
    
    std::cout << "GlobalShutdown: Force termination flags set" << std::endl;
}

// Helper function for subsystems to check shutdown status and report
bool should_subsystem_shutdown(const char* subsystem_name) {
    if (is_emergency_shutdown_requested()) {
        std::cout << "GlobalShutdown: " << subsystem_name << " detected shutdown request, terminating..." << std::endl;
        return true;
    }
    return false;
}

// Helper function for subsystems to report clean shutdown
void report_subsystem_shutdown(const char* subsystem_name) {
    std::cout << "GlobalShutdown: " << subsystem_name << " shutdown complete" << std::endl;
    unregister_active_subsystem();
}