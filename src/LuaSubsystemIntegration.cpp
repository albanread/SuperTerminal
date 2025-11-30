//
//  LuaSubsystemIntegration.cpp
//  SuperTerminal Framework - Lua Bindings for Subsystem Manager
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Lua API bindings for accessing and monitoring the SubsystemManager
//

#include "SubsystemManager.h"
#include "GlobalShutdown.h"
#include <iostream>
#include <string>
#include <map>
#include <thread>
#include <chrono>
#ifdef __unix__
#include <unistd.h>
#endif

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

using namespace SuperTerminal;

// Helper function to push subsystem state to Lua
static void push_subsystem_state(lua_State* L, SubsystemState state) {
    switch (state) {
        case SUBSYSTEM_STATE_UNINITIALIZED:
            lua_pushstring(L, "uninitialized");
            break;
        case SUBSYSTEM_STATE_INITIALIZING:
            lua_pushstring(L, "initializing");
            break;
        case SUBSYSTEM_STATE_RUNNING:
            lua_pushstring(L, "running");
            break;
        case SUBSYSTEM_STATE_SHUTTING_DOWN:
            lua_pushstring(L, "shutting_down");
            break;
        case SUBSYSTEM_STATE_SHUTDOWN:
            lua_pushstring(L, "shutdown");
            break;
        case SUBSYSTEM_STATE_ERROR:
            lua_pushstring(L, "error");
            break;
        default:
            lua_pushstring(L, "unknown");
            break;
    }
}

// Helper function to parse subsystem type from string
static SubsystemType parse_subsystem_type(const char* type_str) {
    if (!type_str) return SUBSYSTEM_TYPE_COUNT;
    
    std::string type(type_str);
    if (type == "graphics") return SUBSYSTEM_TYPE_GRAPHICS;
    if (type == "audio") return SUBSYSTEM_TYPE_AUDIO;
    if (type == "input") return SUBSYSTEM_TYPE_INPUT;
    if (type == "lua_runtime") return SUBSYSTEM_TYPE_LUA_RUNTIME;
    if (type == "particle_system") return SUBSYSTEM_TYPE_PARTICLE_SYSTEM;
    if (type == "bullet_system") return SUBSYSTEM_TYPE_BULLET_SYSTEM;
    if (type == "sprite_effects") return SUBSYSTEM_TYPE_SPRITE_EFFECTS;
    if (type == "text_editor") return SUBSYSTEM_TYPE_TEXT_EDITOR;
    if (type == "midi") return SUBSYSTEM_TYPE_MIDI;
    if (type == "synth") return SUBSYSTEM_TYPE_SYNTH;
    if (type == "music_player") return SUBSYSTEM_TYPE_MUSIC_PLAYER;
    
    return SUBSYSTEM_TYPE_COUNT; // Invalid type
}

// Lua API Functions

/**
 * subsystem.is_healthy()
 * Returns true if all subsystems are running normally
 */
static int lua_subsystem_is_healthy(lua_State* L) {
    bool healthy = SubsystemManager::getInstance().isSystemHealthy();
    lua_pushboolean(L, healthy ? 1 : 0);
    return 1;
}

/**
 * subsystem.get_state(subsystem_name)
 * Returns the current state of a specific subsystem as a string
 */
static int lua_subsystem_get_state(lua_State* L) {
    const char* subsystem_name = luaL_checkstring(L, 1);
    
    SubsystemType type = parse_subsystem_type(subsystem_name);
    if (type == SUBSYSTEM_TYPE_COUNT) {
        lua_pushnil(L);
        lua_pushstring(L, "Invalid subsystem name");
        return 2;
    }
    
    SubsystemState state = SubsystemManager::getInstance().getSubsystemState(type);
    push_subsystem_state(L, state);
    return 1;
}

/**
 * subsystem.get_all_states()
 * Returns a table with all subsystem names as keys and their states as values
 */
static int lua_subsystem_get_all_states(lua_State* L) {
    lua_newtable(L);
    
    const char* subsystem_names[] = {
        "graphics", "audio", "input", "lua_runtime", "particle_system",
        "bullet_system", "sprite_effects", "text_editor", "midi", "synth", "music_player"
    };
    
    for (int i = 0; i < SUBSYSTEM_TYPE_COUNT; ++i) {
        SubsystemState state = SubsystemManager::getInstance().getSubsystemState(static_cast<SubsystemType>(i));
        
        lua_pushstring(L, subsystem_names[i]);
        push_subsystem_state(L, state);
        lua_settable(L, -3);
    }
    
    return 1;
}

/**
 * subsystem.is_emergency_shutdown()
 * Returns true if emergency shutdown is in progress
 */
static int lua_subsystem_is_emergency_shutdown(lua_State* L) {
    bool emergency = SubsystemManager::getInstance().isEmergencyShutdownInProgress();
    lua_pushboolean(L, emergency ? 1 : 0);
    return 1;
}

/**
 * subsystem.get_performance_metrics()
 * Returns a table with performance metrics for all subsystems
 */
static int lua_subsystem_get_performance_metrics(lua_State* L) {
    auto metrics = SubsystemManager::getInstance().getPerformanceMetrics();
    
    lua_newtable(L);
    
    for (const auto& pair : metrics) {
        lua_pushstring(L, pair.first.c_str());
        lua_pushstring(L, pair.second.c_str());
        lua_settable(L, -3);
    }
    
    return 1;
}

/**
 * subsystem.request_shutdown()
 * Request graceful shutdown of SuperTerminal
 */
static int lua_subsystem_request_shutdown(lua_State* L) {
    std::cout << "LuaSubsystem: Shutdown requested from Lua script" << std::endl;
    
    // Don't call superterminal_exit directly as that might interfere with Lua execution
    // Instead, just request emergency shutdown which will be handled by the main loop
    request_emergency_shutdown(3000);
    
    lua_pushboolean(L, 1);
    return 1;
}

/**
 * subsystem.register_shutdown_callback(callback_function, priority)
 * Register a Lua function to be called during shutdown (DISABLED - causes deadlocks)
 */
static int lua_subsystem_register_shutdown_callback(lua_State* L) {
    // DISABLED: This function causes deadlocks because it tries to call back into Lua
    // during shutdown when the Lua state may be invalid or being destroyed
    std::cout << "LuaSubsystem: Warning - shutdown callbacks from Lua are disabled to prevent deadlocks" << std::endl;
    lua_pushboolean(L, 0);
    return 1;
}

/**
 * subsystem.get_active_count()
 * Returns the number of active subsystems registered with the shutdown system
 */
static int lua_subsystem_get_active_count(lua_State* L) {
    int count = get_active_subsystem_count();
    lua_pushinteger(L, count);
    return 1;
}

/**
 * subsystem.check_should_exit()
 * Returns true if the script should exit (emergency shutdown requested)
 * This is a convenience function for scripts to check in their main loops
 */
static int lua_subsystem_check_should_exit(lua_State* L) {
    bool should_exit = is_emergency_shutdown_requested();
    lua_pushboolean(L, should_exit ? 1 : 0);
    return 1;
}

/**
 * subsystem.get_config()
 * Returns the current subsystem configuration as a table
 */
static int lua_subsystem_get_config(lua_State* L) {
    // This is read-only access to configuration
    // For now, we'll return a basic table with common config values
    lua_newtable(L);
    
    // Add some basic configuration info that scripts might find useful
    lua_pushstring(L, "system_initialized");
    lua_pushboolean(L, SubsystemManager::getInstance().isSystemHealthy() ? 1 : 0);
    lua_settable(L, -3);
    
    lua_pushstring(L, "emergency_shutdown");
    lua_pushboolean(L, SubsystemManager::getInstance().isEmergencyShutdownInProgress() ? 1 : 0);
    lua_settable(L, -3);
    
    return 1;
}

/**
 * subsystem.sleep_safe(milliseconds)
 * Sleep for the specified time while checking for shutdown requests
 * Returns true if sleep completed normally, false if interrupted by shutdown
 */
static int lua_subsystem_sleep_safe(lua_State* L) {
    int milliseconds = static_cast<int>(luaL_checknumber(L, 1));
    
    if (milliseconds <= 0) {
        lua_pushboolean(L, 1);
        return 1;
    }
    
    const int CHECK_INTERVAL = 50; // Check every 50ms
    int remaining = milliseconds;
    
    while (remaining > 0 && !is_emergency_shutdown_requested()) {
        int sleep_time = (remaining < CHECK_INTERVAL) ? remaining : CHECK_INTERVAL;
        
        // Use platform-appropriate sleep
        #ifdef _WIN32
            Sleep(sleep_time);
        #else
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        #endif
        
        remaining -= sleep_time;
    }
    
    // Return true if we completed the full sleep, false if interrupted
    bool completed = (remaining <= 0);
    lua_pushboolean(L, completed ? 1 : 0);
    return 1;
}

/**
 * subsystem.log_state()
 * Print current state of all subsystems to console (for debugging)
 */
static int lua_subsystem_log_state(lua_State* L) {
    std::cout << "\n=== SUBSYSTEM STATE REPORT ===" << std::endl;
    std::cout << "System Healthy: " << (SubsystemManager::getInstance().isSystemHealthy() ? "YES" : "NO") << std::endl;
    std::cout << "Emergency Shutdown: " << (SubsystemManager::getInstance().isEmergencyShutdownInProgress() ? "YES" : "NO") << std::endl;
    std::cout << "Active Subsystems: " << get_active_subsystem_count() << std::endl;
    
    const char* subsystem_names[] = {
        "Graphics", "Audio", "Input", "LuaRuntime", "ParticleSystem",
        "BulletSystem", "SpriteEffects", "TextEditor", "MIDI", "Synth", "MusicPlayer"
    };
    
    for (int i = 0; i < SUBSYSTEM_TYPE_COUNT; ++i) {
        SubsystemState state = SubsystemManager::getInstance().getSubsystemState(static_cast<SubsystemType>(i));
        const char* state_str = "";
        
        switch (state) {
            case SUBSYSTEM_STATE_UNINITIALIZED: state_str = "UNINITIALIZED"; break;
            case SUBSYSTEM_STATE_INITIALIZING: state_str = "INITIALIZING"; break;
            case SUBSYSTEM_STATE_RUNNING: state_str = "RUNNING"; break;
            case SUBSYSTEM_STATE_SHUTTING_DOWN: state_str = "SHUTTING_DOWN"; break;
            case SUBSYSTEM_STATE_SHUTDOWN: state_str = "SHUTDOWN"; break;
            case SUBSYSTEM_STATE_ERROR: state_str = "ERROR"; break;
        }
        
        std::cout << "  " << subsystem_names[i] << ": " << state_str << std::endl;
    }
    
    std::cout << "==============================\n" << std::endl;
    
    lua_pushboolean(L, 1);
    return 1;
}

// Registration function
extern "C" void register_subsystem_lua_api(lua_State* L) {
    // Create subsystem module table
    lua_newtable(L);
    
    // Register functions
    lua_pushcfunction(L, lua_subsystem_is_healthy);
    lua_setfield(L, -2, "is_healthy");
    
    lua_pushcfunction(L, lua_subsystem_get_state);
    lua_setfield(L, -2, "get_state");
    
    lua_pushcfunction(L, lua_subsystem_get_all_states);
    lua_setfield(L, -2, "get_all_states");
    
    lua_pushcfunction(L, lua_subsystem_is_emergency_shutdown);
    lua_setfield(L, -2, "is_emergency_shutdown");
    
    lua_pushcfunction(L, lua_subsystem_get_performance_metrics);
    lua_setfield(L, -2, "get_performance_metrics");
    
    lua_pushcfunction(L, lua_subsystem_request_shutdown);
    lua_setfield(L, -2, "request_shutdown");
    
    lua_pushcfunction(L, lua_subsystem_register_shutdown_callback);
    lua_setfield(L, -2, "register_shutdown_callback");
    
    lua_pushcfunction(L, lua_subsystem_get_active_count);
    lua_setfield(L, -2, "get_active_count");
    
    lua_pushcfunction(L, lua_subsystem_check_should_exit);
    lua_setfield(L, -2, "check_should_exit");
    
    lua_pushcfunction(L, lua_subsystem_get_config);
    lua_setfield(L, -2, "get_config");
    
    lua_pushcfunction(L, lua_subsystem_sleep_safe);
    lua_setfield(L, -2, "sleep_safe");
    
    lua_pushcfunction(L, lua_subsystem_log_state);
    lua_setfield(L, -2, "log_state");
    
    // Set the subsystem table as a global
    lua_setglobal(L, "subsystem");
    
    std::cout << "LuaSubsystem: Subsystem API registered for Lua" << std::endl;
}

// Integration function to be called from LuaRuntime initialization
extern "C" bool lua_subsystem_integration_init(lua_State* L) {
    if (!L) {
        std::cerr << "LuaSubsystem: Cannot initialize - NULL Lua state" << std::endl;
        return false;
    }
    
    try {
        register_subsystem_lua_api(L);
        
        // Add a helper script to make subsystem checking easier
        const char* helper_script = R"(
-- Subsystem helper functions

-- Check if we should continue running (convenience function)
function should_continue()
    return not subsystem.check_should_exit()
end

-- Safe main loop wrapper
function safe_loop(loop_function, check_interval)
    check_interval = check_interval or 100 -- Default 100ms between checks
    
    while should_continue() do
        local start_time = os.clock()
        
        -- Call the user's loop function
        local success, error_msg = pcall(loop_function)
        if not success then
            print("Error in loop function: " .. tostring(error_msg))
            break
        end
        
        -- Sleep for remaining time in the interval
        local elapsed = (os.clock() - start_time) * 1000
        local sleep_time = math.max(0, check_interval - elapsed)
        
        if sleep_time > 0 then
            if not subsystem.sleep_safe(sleep_time) then
                break -- Sleep was interrupted by shutdown
            end
        end
    end
end

-- NOTE: Automatic shutdown callback registration is disabled to prevent deadlocks
-- Scripts should use manual cleanup or rely on framework shutdown handling
)";
        
        int result = luaL_dostring(L, helper_script);
        if (result != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            std::cerr << "LuaSubsystem: Error loading helper script: " << (error ? error : "unknown") << std::endl;
            lua_pop(L, 1);
            return false;
        }
        
        std::cout << "LuaSubsystem: Integration initialized successfully" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "LuaSubsystem: Exception during initialization: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "LuaSubsystem: Unknown exception during initialization" << std::endl;
        return false;
    }
}