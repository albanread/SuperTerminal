//
//  DirectScriptLoader.cpp
//  SuperTerminal Framework - Direct Script Loading System
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Ultra-minimal script loader that bypasses ALL shutdown coordination systems
//  to eliminate deadlocks when loading multiple scripts.
//

#include <iostream>
#include <mutex>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Minimal Lua includes
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

// Direct access to SuperTerminal API registration
extern "C" {
    void register_superterminal_api(lua_State* L);
}

// Ultra-minimal state
static lua_State* g_direct_lua = nullptr;
static std::mutex g_direct_mutex;
static std::atomic<bool> g_direct_executing{false};

class DirectScriptLoader {
public:
    // Initialize fresh Lua state
    static bool init() {
        std::lock_guard<std::mutex> lock(g_direct_mutex);
        
        // Force clean slate - destroy any existing state
        if (g_direct_lua) {
            lua_close(g_direct_lua);
            g_direct_lua = nullptr;
        }
        
        g_direct_executing = false;
        
        // Create completely fresh Lua state
        g_direct_lua = luaL_newstate();
        if (!g_direct_lua) {
            std::cout << "DirectScriptLoader: Failed to create Lua state" << std::endl;
            return false;
        }
        
        // Load standard libraries
        luaL_openlibs(g_direct_lua);
        
        // Register SuperTerminal API
        register_superterminal_api(g_direct_lua);
        
        std::cout << "DirectScriptLoader: Fresh Lua state initialized" << std::endl;
        return true;
    }
    
    // Execute script content directly - no interruption, no coordination
    static bool execute(const char* script_content) {
        if (!script_content) return false;
        
        std::cout << "DirectScriptLoader: Executing script directly..." << std::endl;
        
        // Always reinitialize for clean slate
        if (!init()) {
            std::cout << "DirectScriptLoader: Failed to initialize" << std::endl;
            return false;
        }
        
        std::lock_guard<std::mutex> lock(g_direct_mutex);
        
        if (!g_direct_lua) {
            std::cout << "DirectScriptLoader: No Lua state available" << std::endl;
            return false;
        }
        
        g_direct_executing = true;
        
        // Execute directly with no hooks, no interruption
        int result = luaL_dostring(g_direct_lua, script_content);
        
        g_direct_executing = false;
        
        if (result != LUA_OK) {
            if (lua_isstring(g_direct_lua, -1)) {
                std::cout << "DirectScriptLoader: Error: " << lua_tostring(g_direct_lua, -1) << std::endl;
            } else {
                std::cout << "DirectScriptLoader: Unknown execution error" << std::endl;
            }
            lua_pop(g_direct_lua, 1); // Remove error message
            return false;
        }
        
        std::cout << "DirectScriptLoader: Script executed successfully" << std::endl;
        return true;
    }
    
    // Load and execute file - no coordination
    static bool load_and_execute(const char* filename) {
        if (!filename) return false;
        
        std::cout << "DirectScriptLoader: Loading file: " << filename << std::endl;
        
        // Read file manually
        FILE* file = fopen(filename, "rb");
        if (!file) {
            std::cout << "DirectScriptLoader: Cannot open file: " << filename << std::endl;
            return false;
        }
        
        // Get file size
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        if (size <= 0) {
            std::cout << "DirectScriptLoader: File is empty: " << filename << std::endl;
            fclose(file);
            return false;
        }
        
        // Read content
        char* content = (char*)malloc(size + 1);
        if (!content) {
            std::cout << "DirectScriptLoader: Memory allocation failed" << std::endl;
            fclose(file);
            return false;
        }
        
        size_t read_size = fread(content, 1, size, file);
        fclose(file);
        
        if (read_size != (size_t)size) {
            std::cout << "DirectScriptLoader: Failed to read file completely" << std::endl;
            free(content);
            return false;
        }
        
        content[size] = '\0';
        
        std::cout << "DirectScriptLoader: Loaded " << size << " bytes" << std::endl;
        
        // Execute the content
        bool success = execute(content);
        
        free(content);
        return success;
    }
    
    // Check if executing (simple check)
    static bool is_executing() {
        return g_direct_executing.load();
    }
    
    // Force stop - just reinitialize
    static void force_stop() {
        std::cout << "DirectScriptLoader: Force stop - reinitializing" << std::endl;
        init();
    }
    
    // Cleanup
    static void cleanup() {
        std::lock_guard<std::mutex> lock(g_direct_mutex);
        
        if (g_direct_lua) {
            lua_close(g_direct_lua);
            g_direct_lua = nullptr;
        }
        
        g_direct_executing = false;
        std::cout << "DirectScriptLoader: Cleanup complete" << std::endl;
    }
};

// C API functions
extern "C" {
    
    bool direct_execute_script(const char* script_content) {
        return DirectScriptLoader::execute(script_content);
    }
    
    bool direct_load_script_file(const char* filename) {
        return DirectScriptLoader::load_and_execute(filename);
    }
    
    bool direct_is_executing() {
        return DirectScriptLoader::is_executing();
    }
    
    void direct_force_stop() {
        DirectScriptLoader::force_stop();
    }
    
    void direct_cleanup() {
        DirectScriptLoader::cleanup();
    }
    
    bool direct_init() {
        return DirectScriptLoader::init();
    }
}