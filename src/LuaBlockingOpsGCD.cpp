//
//  LuaBlockingOpsGCD.cpp
//  SuperTerminal - GCD-Aware Lua Blocking Operations
//
//  Cancellable blocking operations for Lua scripts using GCD
//  Copyright © 2025 SuperTerminal. All rights reserved.
//

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <dispatch/dispatch.h>
#include <chrono>
#include <thread>
#include <iostream>

// Forward declare LuaGCD functions
extern "C" {
    bool lua_gcd_is_on_lua_queue(void);
    bool lua_gcd_is_script_running(void);
}

// ============================================================================
// MARK: - Helper Functions
// ============================================================================

namespace LuaBlockingGCD {

// Check if current block is cancelled
static bool is_cancelled() {
    // Check if the script is still running
    // Don't use dispatch_block_testcancel - it requires the actual block pointer
    return !lua_gcd_is_script_running();
}

// Cancellable sleep with polling
static bool cancellable_sleep_ms(uint64_t milliseconds, uint64_t poll_interval_ms = 10) {
    auto start = std::chrono::steady_clock::now();
    
    while (true) {
        // Check for cancellation
        if (is_cancelled()) {
            return false; // Cancelled
        }
        
        // Check if we've waited long enough
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        
        if (elapsed_ms >= milliseconds) {
            return true; // Completed
        }
        
        // Sleep for short interval
        uint64_t remaining_ms = milliseconds - elapsed_ms;
        uint64_t sleep_ms = std::min(remaining_ms, poll_interval_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
}

} // namespace LuaBlockingGCD

// ============================================================================
// MARK: - Lua Bindings
// ============================================================================

extern "C" {

/**
 * wait_frame(count) - Wait for N frames (16.67ms each for 60 FPS)
 * Checks for cancellation every 10ms
 */
int lua_wait_frame_gcd(lua_State* L) {
    int frame_count = luaL_checkinteger(L, 1);
    
    if (frame_count <= 0) {
        return 0;
    }
    
    // 60 FPS = ~16.67ms per frame
    uint64_t total_ms = frame_count * 17; // Round to 17ms
    
    bool completed = LuaBlockingGCD::cancellable_sleep_ms(total_ms);
    
    if (!completed) {
        return luaL_error(L, "wait_frame cancelled");
    }
    
    return 0;
}

/**
 * sleep(seconds) - Sleep for N seconds
 * Checks for cancellation every 10ms
 */
int lua_sleep_gcd(lua_State* L) {
    double seconds = luaL_checknumber(L, 1);
    
    if (seconds <= 0) {
        return 0;
    }
    
    uint64_t milliseconds = static_cast<uint64_t>(seconds * 1000.0);
    
    bool completed = LuaBlockingGCD::cancellable_sleep_ms(milliseconds);
    
    if (!completed) {
        return luaL_error(L, "sleep cancelled");
    }
    
    return 0;
}

/**
 * sleep_ms(milliseconds) - Sleep for N milliseconds
 * Checks for cancellation every 10ms
 */
int lua_sleep_ms_gcd(lua_State* L) {
    int milliseconds = luaL_checkinteger(L, 1);
    
    if (milliseconds <= 0) {
        return 0;
    }
    
    bool completed = LuaBlockingGCD::cancellable_sleep_ms(milliseconds);
    
    if (!completed) {
        return luaL_error(L, "sleep_ms cancelled");
    }
    
    return 0;
}

/**
 * wait_for_condition(function, timeout_ms, poll_interval_ms)
 * Repeatedly calls function until it returns true or timeout
 * Checks for cancellation on each iteration
 */
int lua_wait_for_condition_gcd(lua_State* L) {
    // Arg 1: function to test
    luaL_checktype(L, 1, LUA_TFUNCTION);
    
    // Arg 2: timeout in milliseconds
    int timeout_ms = luaL_checkinteger(L, 2);
    
    // Arg 3: poll interval (optional, default 100ms)
    int poll_interval_ms = luaL_optinteger(L, 3, 100);
    
    if (poll_interval_ms <= 0) {
        poll_interval_ms = 100;
    }
    
    auto start = std::chrono::steady_clock::now();
    
    while (true) {
        // Check for cancellation
        if (LuaBlockingGCD::is_cancelled()) {
            return luaL_error(L, "wait_for_condition cancelled");
        }
        
        // Check timeout
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        
        if (elapsed_ms >= timeout_ms) {
            lua_pushboolean(L, 0); // Timeout
            return 1;
        }
        
        // Call the condition function
        lua_pushvalue(L, 1); // Push function
        int result = lua_pcall(L, 0, 1, 0);
        
        if (result != LUA_OK) {
            // Error in condition function
            return lua_error(L);
        }
        
        // Check if condition is true
        if (lua_toboolean(L, -1)) {
            lua_pop(L, 1); // Pop result
            lua_pushboolean(L, 1); // Success
            return 1;
        }
        
        lua_pop(L, 1); // Pop false result
        
        // Sleep for poll interval
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
    }
}

/**
 * yield() - Cooperative yield point
 * Just checks for cancellation without sleeping
 */
int lua_yield_gcd(lua_State* L) {
    if (LuaBlockingGCD::is_cancelled()) {
        return luaL_error(L, "Script cancelled at yield point");
    }
    
    // Optionally yield thread time slice
    std::this_thread::yield();
    
    return 0;
}

/**
 * is_cancelled() - Check if script cancellation has been requested
 * Returns boolean
 */
int lua_is_cancelled_gcd(lua_State* L) {
    bool cancelled = LuaBlockingGCD::is_cancelled();
    lua_pushboolean(L, cancelled);
    return 1;
}

/**
 * check_cancellation() - Throw error if cancelled
 * Useful for manual cancellation points
 */
int lua_check_cancellation_gcd(lua_State* L) {
    if (LuaBlockingGCD::is_cancelled()) {
        return luaL_error(L, "Script cancelled");
    }
    return 0;
}

/**
 * wait_with_callback(milliseconds, callback, interval_ms)
 * Wait with periodic callback execution
 * Checks for cancellation on each interval
 */
int lua_wait_with_callback_gcd(lua_State* L) {
    int total_ms = luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    int interval_ms = luaL_optinteger(L, 3, 100);
    
    if (interval_ms <= 0) {
        interval_ms = 100;
    }
    
    auto start = std::chrono::steady_clock::now();
    
    while (true) {
        // Check for cancellation
        if (LuaBlockingGCD::is_cancelled()) {
            return luaL_error(L, "wait_with_callback cancelled");
        }
        
        // Check if done
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        
        if (elapsed_ms >= total_ms) {
            break;
        }
        
        // Call callback
        lua_pushvalue(L, 2); // Push callback function
        lua_pushinteger(L, elapsed_ms); // Push elapsed time
        int result = lua_pcall(L, 1, 0, 0);
        
        if (result != LUA_OK) {
            return lua_error(L);
        }
        
        // Sleep for interval
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
    
    return 0;
}

/**
 * Register all GCD-aware blocking operations
 */
void register_lua_blocking_ops_gcd(lua_State* L) {
    // Replace standard blocking operations with GCD-aware versions
    lua_register(L, "wait_frame", lua_wait_frame_gcd);
    lua_register(L, "sleep", lua_sleep_gcd);
    lua_register(L, "sleep_ms", lua_sleep_ms_gcd);
    lua_register(L, "wait_ms", lua_sleep_ms_gcd);  // Alias for sleep_ms (overrides old implementation)
    
    // New GCD-specific functions
    lua_register(L, "wait_for_condition", lua_wait_for_condition_gcd);
    lua_register(L, "yield", lua_yield_gcd);
    lua_register(L, "is_cancelled", lua_is_cancelled_gcd);
    lua_register(L, "check_cancellation", lua_check_cancellation_gcd);
    lua_register(L, "wait_with_callback", lua_wait_with_callback_gcd);
}

} // extern "C"