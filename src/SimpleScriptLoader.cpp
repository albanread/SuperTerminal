//
//  SimpleScriptLoader.cpp
//  SuperTerminal Framework - Simple Script Loading System
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  This is a simple, reliable script loader that bypasses the problematic
//  emergency shutdown system to allow loading multiple scripts without deadlocks.
//

#include <iostream>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>

// Forward declarations for Lua system
extern "C" {
    bool lua_init();
    void lua_cleanup();
    bool lua_is_executing();
    void lua_interrupt();
    
    // Simple file operations
    bool read_file_to_string(const char* filename, char** content, size_t* length);
}

// Simple script loader state
static std::mutex g_script_mutex;
static std::atomic<bool> g_script_loading{false};
static std::atomic<bool> g_force_stop_current{false};

// Use existing Lua functions instead of accessing internal variables
extern "C" {
    bool lua_is_executing();
    void lua_interrupt();
}

class SimpleScriptLoader {
private:
    static constexpr int MAX_INTERRUPT_WAIT_MS = 1000;
    static constexpr int INTERRUPT_CHECK_INTERVAL_MS = 50;
    
public:
    // Simple, reliable script execution
    static bool execute_script_content(const char* script_content) {
        std::lock_guard<std::mutex> lock(g_script_mutex);
        
        std::cout << "SimpleScriptLoader: Executing script content..." << std::endl;
        
        // Stop any currently running script first (simple approach)
        if (!stop_current_script_simple()) {
            std::cout << "SimpleScriptLoader: WARNING - Could not cleanly stop current script, proceeding anyway" << std::endl;
        }
        
        // Initialize Lua if needed
        if (!ensure_lua_ready()) {
            std::cout << "SimpleScriptLoader: ERROR - Could not initialize Lua" << std::endl;
            return false;
        }
        
        // Execute the new script
        std::cout << "SimpleScriptLoader: Script execution disabled during redesign" << std::endl;
        bool success = false;
        
        if (success) {
            std::cout << "SimpleScriptLoader: Script executed successfully" << std::endl;
        } else {
            std::cout << "SimpleScriptLoader: Script execution temporarily disabled" << std::endl;
        }
        
        return success;
    }
    
    // Load and execute script from file
    static bool load_and_execute_script(const char* filename) {
        std::cout << "SimpleScriptLoader: Loading script from: " << filename << std::endl;
        
        // Read file content
        char* content = nullptr;
        size_t length = 0;
        
        if (!read_file_to_string(filename, &content, &length)) {
            std::cout << "SimpleScriptLoader: ERROR - Could not read file: " << filename << std::endl;
            return false;
        }
        
        if (!content || length == 0) {
            std::cout << "SimpleScriptLoader: ERROR - File is empty: " << filename << std::endl;
            if (content) free(content);
            return false;
        }
        
        std::cout << "SimpleScriptLoader: Loaded " << length << " bytes from file" << std::endl;
        
        // Execute the content
        bool success = execute_script_content(content);
        
        // Clean up
        free(content);
        
        return success;
    }
    
private:
    // Simple script stopping - just interrupt and wait briefly
    static bool stop_current_script_simple() {
        if (!lua_is_executing()) {
            // No script running
            return true;
        }
        
        std::cout << "SimpleScriptLoader: Stopping current script (simple method)..." << std::endl;
        
        // Set interrupt flag
        lua_interrupt();
        g_force_stop_current = true;
        
        // Wait for script to stop (with timeout)
        auto start_time = std::chrono::steady_clock::now();
        while (lua_is_executing()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
                
            if (elapsed > MAX_INTERRUPT_WAIT_MS) {
                std::cout << "SimpleScriptLoader: Timeout waiting for script to stop (" 
                          << elapsed << "ms), forcing cleanup..." << std::endl;
                
                // Force cleanup - reinitialize Lua state to ensure clean slate
                g_force_stop_current = false;
                
                lua_cleanup();
                lua_init();
                
                return false;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(INTERRUPT_CHECK_INTERVAL_MS));
        }
        
        // Reset flags
        g_force_stop_current = false;
        
        std::cout << "SimpleScriptLoader: Current script stopped successfully" << std::endl;
        return true;
    }
    
    // Ensure Lua is ready for execution
    static bool ensure_lua_ready() {
        // Simple approach - just try to initialize
        // If it fails, try cleanup and reinitialize
        if (!lua_init()) {
            std::cout << "SimpleScriptLoader: First init failed, trying cleanup and reinit..." << std::endl;
            lua_cleanup();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            if (!lua_init()) {
                std::cout << "SimpleScriptLoader: Second init failed, giving up" << std::endl;
                return false;
            }
        }
        
        return true;
    }
};

// C API functions for simple script loading
extern "C" {
    
    // Simple script execution function
    bool simple_execute_script(const char* script_content) {
        if (!script_content) {
            return false;
        }
        
        return SimpleScriptLoader::execute_script_content(script_content);
    }
    
    // Simple script file loading function
    bool simple_load_script_file(const char* filename) {
        if (!filename) {
            return false;
        }
        
        return SimpleScriptLoader::load_and_execute_script(filename);
    }
    
    // Check if script loading is in progress
    bool simple_script_loading_in_progress() {
        return g_script_loading.load();
    }
    
    // Force stop current script (emergency function)
    void simple_force_stop_script() {
        std::cout << "SimpleScriptLoader: Emergency stop requested" << std::endl;
        g_force_stop_current = true;
        lua_interrupt();
        
        // Give it a moment to stop gracefully
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Force cleanup if needed
        if (lua_is_executing()) {
            std::cout << "SimpleScriptLoader: Force terminating script" << std::endl;
            lua_cleanup();
            lua_init();
        }
        
        g_force_stop_current = false;
    }
}

// Implementation of read_file_to_string (simple version)
bool read_file_to_string(const char* filename, char** content, size_t* length) {
    if (!filename || !content || !length) {
        return false;
    }
    
    FILE* file = fopen(filename, "rb");
    if (!file) {
        return false;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(file);
        return false;
    }
    
    // Allocate buffer
    *content = (char*)malloc(file_size + 1);
    if (!*content) {
        fclose(file);
        return false;
    }
    
    // Read file
    size_t bytes_read = fread(*content, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != (size_t)file_size) {
        free(*content);
        *content = nullptr;
        return false;
    }
    
    // Null terminate
    (*content)[file_size] = '\0';
    *length = file_size;
    
    return true;
}