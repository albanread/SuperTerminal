//
//  LuaRuntimeCompat.cpp
//  SuperTerminal - Lua Runtime Compatibility Shims
//
//  Provides compatibility stubs for old LuaRuntime.cpp functions
//  to allow gradual migration to GCD-based Lua runtime.
//  Copyright © 2025 SuperTerminal. All rights reserved.
//

#include "LuaRuntimeGCD.h"
#include <string>
#include <atomic>
#include <iostream>

// Global variable for compatibility (some code directly accesses this)
std::atomic<bool> g_lua_should_interrupt{false};

// ============================================================================
// MARK: - Compatibility Stubs
// ============================================================================

extern "C" {

// Old function: Check if script is running
bool is_script_running(void) {
    return lua_gcd_is_script_running();
}

// Old function: Get Lua error
const char* lua_get_error(void) {
    return lua_gcd_get_last_error();
}

// Old function: Get Lua state (no longer used with GCD)
void* lua_get_state(void) {
    // GCD runtime doesn't expose Lua state directly
    std::cerr << "WARNING: lua_get_state() called - not supported in GCD runtime" << std::endl;
    return nullptr;
}

// Old function: Cleanup finished executions
void cleanup_finished_executions(void) {
    // GCD runtime handles cleanup automatically
    // This is a no-op
}

// Old function: Lua cleanup
void lua_cleanup(void) {
    std::cout << "lua_cleanup() called - using lua_gcd_shutdown()" << std::endl;
    lua_gcd_shutdown();
}

// Old function: Get current script content
const char* lua_get_current_script_content(void) {
    // GCD runtime doesn't expose script content
    std::cerr << "WARNING: lua_get_current_script_content() called - not supported in GCD runtime" << std::endl;
    return "";
}

// Old function: Get current script filename
const char* lua_get_current_script_filename(void) {
    const char* name = lua_gcd_get_current_script_name();
    return name ? name : "";
}

// Old function: Execute Lua sandboxed
bool exec_lua_sandboxed(const char* lua_code) {
    std::cout << "exec_lua_sandboxed() compatibility shim - using lua_gcd_exec()" << std::endl;
    return lua_gcd_exec(lua_code, "compat_script");
}

// Old function: Execute Lua file sandboxed
bool exec_lua_file_sandboxed(const char* filename) {
    std::cout << "exec_lua_file_sandboxed() compatibility shim - using lua_gcd_exec_file()" << std::endl;
    extern bool lua_gcd_exec_file(const char* filename);
    return lua_gcd_exec_file(filename);
}

// Old function: Stop Lua script bulletproof
void stop_lua_script_bulletproof(void) {
    std::cout << "stop_lua_script_bulletproof() compatibility shim - using lua_gcd_stop_script()" << std::endl;
    lua_gcd_stop_script();
}

// Old function: Global interrupt flag (now redirects to GCD stop)
bool get_lua_should_interrupt(void) {
    return !lua_gcd_is_script_running();
}

void set_lua_should_interrupt(bool value) {
    if (value) {
        std::cout << "set_lua_should_interrupt(true) - stopping script via GCD" << std::endl;
        lua_gcd_stop_script();
    }
    g_lua_should_interrupt = value;
}

// Old variable accessor (for backward compatibility)
std::atomic<bool>& get_g_lua_should_interrupt_ref(void) {
    return g_lua_should_interrupt;
}

// Old function: Lua init
void lua_init(void) {
    std::cout << "lua_init() compatibility shim - using lua_gcd_initialize()" << std::endl;
    lua_gcd_initialize();
}

// Old function: Lua interrupt
void lua_interrupt(void) {
    std::cout << "lua_interrupt() compatibility shim - stopping script" << std::endl;
    lua_gcd_stop_script();
    g_lua_should_interrupt = true;
}

// Old function: Check if Lua is executing
bool lua_is_executing(void) {
    return lua_gcd_is_script_running();
}

// Old function: Terminate current script
bool lua_terminate_current_script(void) {
    std::cout << "lua_terminate_current_script() compatibility shim" << std::endl;
    lua_gcd_stop_script();
    return true;
}

// Old function: Reset Lua complete
void reset_lua_complete(void) {
    std::cout << "reset_lua_complete() compatibility shim" << std::endl;
    lua_gcd_stop_script();
    lua_gcd_reset_repl();
}

// Note: register_superterminal_api is now provided by LuaRuntime.cpp

// Old function: Terminate Lua script with cleanup
void terminate_lua_script_with_cleanup(void) {
    std::cout << "terminate_lua_script_with_cleanup() compatibility shim" << std::endl;
    lua_gcd_stop_script();
}

// Old function: Reset Lua
void reset_lua(void) {
    std::cout << "reset_lua() compatibility shim" << std::endl;
    lua_gcd_stop_script();
    lua_gcd_reset_repl();
}

// Old function: Lua fast reset
void lua_fast_reset(void) {
    std::cout << "lua_fast_reset() compatibility shim" << std::endl;
    lua_gcd_stop_script();
}

// Old function: Save Lua state (not supported in GCD runtime)
void lua_save_state(void) {
    std::cerr << "WARNING: lua_save_state() not supported in GCD runtime" << std::endl;
}

// Old function: Restore Lua state (not supported in GCD runtime)
void lua_restore_state(void) {
    std::cerr << "WARNING: lua_restore_state() not supported in GCD runtime" << std::endl;
}

// Old function: Check if Lua has saved state (always false in GCD runtime)
bool lua_has_saved_state(void) {
    return false;
}

// Old function: Clear saved state (no-op in GCD runtime)
void lua_clear_saved_state(void) {
    // No-op: GCD runtime doesn't support state saving
}

} // extern "C"