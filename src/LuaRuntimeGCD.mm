//
//  LuaRuntimeGCD.mm
//  SuperTerminal - GCD-Based Lua Runtime Implementation
//
//  Modern, reliable Lua execution using Grand Central Dispatch
//  Copyright © 2025 SuperTerminal. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
#include "LuaRuntimeGCD.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <string>
#include <atomic>
#include <chrono>
#include <mutex>
#include <iostream>
#include <fstream>
#include <sstream>

// Forward declarations for SuperTerminal API registration
extern "C" void register_superterminal_api(lua_State* L);
extern "C" void register_particle_system_lua_api(lua_State* L);

// Forward declarations for audio bindings
namespace {
    extern "C" void register_audio_lua_bindings(lua_State* L);
}

// Forward declaration for assets bindings
extern "C" void register_assets_lua_bindings(lua_State* L);

// Forward declaration for GCD-aware blocking operations
extern "C" void register_lua_blocking_ops_gcd(lua_State* L);

// ============================================================================
// MARK: - Global State
// ============================================================================

namespace LuaGCD {

// Dispatch queue for Lua execution (serial queue)
static dispatch_queue_t g_lua_queue = nullptr;

// Current executing block (for cancellation)
static dispatch_block_t g_current_block = nullptr;
static std::mutex g_block_mutex;

// Current script info
static std::string g_current_script_name;
static std::chrono::steady_clock::time_point g_script_start_time;
static std::atomic<bool> g_script_running{false};

// Script generation tracking (for abandoning old scripts)
static std::atomic<uint64_t> g_script_generation{0};
static std::atomic<uint64_t> g_abandoned_generation{0};

// REPL persistent state
static lua_State* g_repl_lua = nullptr;
static std::mutex g_repl_mutex;

// Error tracking
static std::string g_last_error;
static std::mutex g_error_mutex;

// Configuration
static std::atomic<int> g_hook_frequency{1000};
static std::atomic<bool> g_debug_output{true};
static std::atomic<uint32_t> g_script_timeout_seconds{0};

// Statistics
static std::atomic<uint64_t> g_total_scripts{0};
static std::atomic<uint64_t> g_successful_scripts{0};
static std::atomic<uint64_t> g_failed_scripts{0};
static std::atomic<uint64_t> g_cancelled_scripts{0};

// Initialization flag
static std::atomic<bool> g_initialized{false};

// Queue label for identification
static const char* QUEUE_LABEL = "com.superterminal.lua.execution";

// ============================================================================
// MARK: - Helper Functions
// ============================================================================

static void log_debug(const char* format, ...) {
    if (!g_debug_output.load()) return;

    va_list args;
    va_start(args, format);
    printf("[LuaGCD] ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

static void set_error(const std::string& error) {
    std::lock_guard<std::mutex> lock(g_error_mutex);
    g_last_error = error;
    log_debug("Error: %s", error.c_str());
}

static std::string get_error() {
    std::lock_guard<std::mutex> lock(g_error_mutex);
    return g_last_error;
}

static void clear_error() {
    std::lock_guard<std::mutex> lock(g_error_mutex);
    g_last_error.clear();
}

// ============================================================================
// MARK: - Lua Hook for Cancellation
// ============================================================================

static void lua_cancellation_hook(lua_State* L, lua_Debug* ar) {
    // Simply check the atomic flag instead of using dispatch_block_testcancel
    // The dispatch queue will handle block cancellation
    if (!g_script_running.load()) {
        log_debug("Cancellation detected in Lua hook");
        luaL_error(L, "Script cancelled by user");
    }

    // Check timeout
    uint32_t timeout = g_script_timeout_seconds.load();
    if (timeout > 0) {
        auto elapsed = std::chrono::steady_clock::now() - g_script_start_time;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        if (seconds >= timeout) {
            log_debug("Script timeout after %lld seconds", seconds);
            luaL_error(L, "Script timeout after %u seconds", timeout);
        }
    }
}

// ============================================================================
// MARK: - Lua Panic Handler
// ============================================================================

static int lua_panic_handler(lua_State* L) {
    const char* error = lua_tostring(L, -1);
    std::string error_msg = error ? error : "Unknown panic error";

    log_debug("LUA PANIC: %s", error_msg.c_str());
    set_error("PANIC: " + error_msg);

    // Don't use longjmp - just return and let GCD handle cleanup
    return 0;
}

// ============================================================================
// MARK: - Script Execution
// ============================================================================

// RAII guard to ensure g_script_running is always reset
struct ScriptRunningGuard {
    uint64_t my_generation;

    ScriptRunningGuard() {
        fprintf(stderr, "[GCD] ScriptRunningGuard: Setting g_script_running = true\n");
        fflush(stderr);
        g_script_running = true;
        g_script_start_time = std::chrono::steady_clock::now();
        my_generation = g_script_generation.load();
        fprintf(stderr, "[GCD] ScriptRunningGuard: generation = %llu\n", my_generation);
        fflush(stderr);
    }
    ~ScriptRunningGuard() {
        fprintf(stderr, "[GCD] ScriptRunningGuard: Destructor - Setting g_script_running = false\n");
        fflush(stderr);
        g_script_running = false;
        std::lock_guard<std::mutex> lock(g_block_mutex);
        g_current_script_name.clear();
        g_current_block = nullptr;
        fprintf(stderr, "[GCD] ScriptRunningGuard: Destructor complete\n");
        fflush(stderr);
    }

    bool is_abandoned() const {
        return my_generation <= g_abandoned_generation.load();
    }
};

static void execute_lua_script(const std::string& script_code, const std::string& script_name) {
    fprintf(stderr, "[GCD] execute_lua_script() START for: %s\n", script_name.c_str());
    fflush(stderr);
    log_debug("Executing script: %s (%zu bytes)", script_name.c_str(), script_code.length());

    // Use RAII guard to ensure g_script_running is always reset
    ScriptRunningGuard guard;

    fprintf(stderr, "[GCD] ScriptRunningGuard created, g_script_running should be true now\n");
    fflush(stderr);

    {
        std::lock_guard<std::mutex> lock(g_block_mutex);
        g_current_script_name = script_name;
    }

    // Create new Lua state for this script
    lua_State* L = luaL_newstate();
    if (!L) {
        set_error("Failed to create Lua state");
        g_failed_scripts++;
        return;
    }

    // Set panic handler
    lua_atpanic(L, lua_panic_handler);

    // Open standard libraries
    luaL_openlibs(L);

    // Register SuperTerminal APIs
    try {
        log_debug("Registering SuperTerminal API...");
        register_superterminal_api(L);

        log_debug("Registering particle system API...");
        register_particle_system_lua_api(L);

        log_debug("Registering audio bindings...");
        register_audio_lua_bindings(L);

        log_debug("Registering assets bindings...");
        register_assets_lua_bindings(L);

        log_debug("Registering GCD-aware blocking operations...");
        register_lua_blocking_ops_gcd(L);

    } catch (const std::exception& e) {
        log_debug("Warning: Failed to register some APIs: %s", e.what());
    }

    // Set cancellation hook
    int hook_freq = g_hook_frequency.load();
    lua_sethook(L, lua_cancellation_hook, LUA_MASKCOUNT, hook_freq);
    log_debug("Set Lua hook with frequency: %d", hook_freq);

    // Execute the script
    log_debug("Running luaL_dostring...");
    int result = luaL_dostring(L, script_code.c_str());

    // Check if script was cancelled by checking if it's still running
    bool was_cancelled = !g_script_running.load();

    if (result != LUA_OK) {
        const char* error = lua_tostring(L, -1);
        std::string error_msg = error ? error : "Unknown error";

        if (was_cancelled || error_msg.find("cancelled") != std::string::npos) {
            log_debug("Script cancelled: %s", script_name.c_str());
            set_error("Script cancelled");
            g_cancelled_scripts++;
        } else {
            log_debug("Script error: %s", error_msg.c_str());
            set_error(error_msg);
            g_failed_scripts++;
        }
    } else {
        log_debug("Script completed successfully: %s", script_name.c_str());
        clear_error();
        g_successful_scripts++;
    }

    // Cleanup Lua state
    lua_close(L);

    // Log execution time
    auto elapsed = std::chrono::steady_clock::now() - g_script_start_time;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    log_debug("Script finished after %lld ms", ms);

    fprintf(stderr, "[GCD] execute_lua_script() END - guard destructor will run next\n");
    fflush(stderr);

    // g_script_running will be reset automatically by ScriptRunningGuard destructor
}

} // namespace LuaGCD

// ============================================================================
// MARK: - C API Implementation
// ============================================================================

extern "C" {

bool lua_gcd_initialize(void) {
    using namespace LuaGCD;

    if (g_initialized.load()) {
        log_debug("Already initialized");
        return true;
    }

    log_debug("Initializing Lua GCD runtime...");

    // Create serial dispatch queue
    g_lua_queue = dispatch_queue_create(
        QUEUE_LABEL,
        DISPATCH_QUEUE_SERIAL
    );

    if (!g_lua_queue) {
        set_error("Failed to create dispatch queue");
        return false;
    }

    // Set target queue to high priority global queue
    dispatch_set_target_queue(g_lua_queue,
        dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));

    g_initialized = true;
    log_debug("Lua GCD runtime initialized successfully");

    return true;
}

void lua_gcd_shutdown(void) {
    using namespace LuaGCD;

    if (!g_initialized.load()) {
        return;
    }

    log_debug("Shutting down Lua GCD runtime...");

    // Stop any running script
    lua_gcd_stop_script();

    // Shutdown REPL
    lua_gcd_shutdown_repl();

    // Wait for queue to drain
    dispatch_sync(g_lua_queue, ^{
        log_debug("Queue drained");
    });

    // Release queue
    if (g_lua_queue) {
        // Note: dispatch_release not needed on modern macOS with ARC
        g_lua_queue = nullptr;
    }

    g_initialized = false;
    log_debug("Lua GCD runtime shutdown complete");
}

bool lua_gcd_is_initialized(void) {
    return LuaGCD::g_initialized.load();
}

bool lua_gcd_exec(const char* lua_code, const char* script_name) {
    using namespace LuaGCD;

    fprintf(stderr, "\n========================================\n");
    fprintf(stderr, "lua_gcd_exec() CALLED\n");
    fprintf(stderr, "Script name: %s\n", script_name ? script_name : "(null)");
    fprintf(stderr, "Code pointer: %p\n", (void*)lua_code);
    fprintf(stderr, "========================================\n");
    fflush(stderr);

    if (!g_initialized.load()) {
        fprintf(stderr, "WARNING: Lua runtime not initialized - auto-initializing...\n");
        fflush(stderr);
        if (!lua_gcd_initialize()) {
            fprintf(stderr, "ERROR: Failed to auto-initialize Lua runtime\n");
            fflush(stderr);
            set_error("Failed to auto-initialize Lua runtime");
            return false;
        }
        fprintf(stderr, "SUCCESS: Lua runtime auto-initialized\n");
        fflush(stderr);
    }

    if (!lua_code) {
        fprintf(stderr, "ERROR: No script code provided\n");
        fflush(stderr);
        set_error("No script code provided");
        return false;
    }

    fprintf(stderr, "Code length: %zu bytes\n", strlen(lua_code));
    fprintf(stderr, "Code preview: %.100s\n", lua_code);
    fflush(stderr);

    // Check if script is already running
    if (g_script_running.load()) {
        fprintf(stderr, "ERROR: Another script is already running\n");
        fflush(stderr);
        set_error("Another script is already running");
        return false;
    }

    std::string code(lua_code);
    std::string name(script_name ? script_name : "script");

    // Increment generation counter - this makes any previous scripts "old"
    uint64_t new_generation = g_script_generation.fetch_add(1) + 1;
    fprintf(stderr, "[GCD] Starting new script generation: %llu\n", new_generation);
    fflush(stderr);

    // Create cancellable block
    dispatch_block_t block = dispatch_block_create(
        DISPATCH_BLOCK_INHERIT_QOS_CLASS,
        ^{
            execute_lua_script(code, name);
        }
    );

    // Store block for cancellation
    {
        std::lock_guard<std::mutex> lock(g_block_mutex);
        g_current_block = block;
    }

    // Increment counter
    g_total_scripts++;

    // Dispatch async
    dispatch_async(g_lua_queue, block);

    fprintf(stderr, "Script queued successfully: %s\n", name.c_str());
    fprintf(stderr, "========================================\n\n");
    fflush(stderr);

    log_debug("Script queued: %s", name.c_str());
    return true;
}

bool lua_gcd_exec_file(const char* filename) {
    using namespace LuaGCD;

    if (!filename) {
        set_error("No filename provided");
        return false;
    }

    // Read file
    std::ifstream file(filename);
    if (!file.is_open()) {
        set_error(std::string("Could not open file: ") + filename);
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    if (content.empty()) {
        set_error(std::string("Empty file: ") + filename);
        return false;
    }

    return lua_gcd_exec(content.c_str(), filename);
}

bool lua_gcd_exec_repl(const char* lua_code) {
    using namespace LuaGCD;

    if (!g_initialized.load()) {
        fprintf(stderr, "WARNING: Lua runtime not initialized - auto-initializing for REPL...\n");
        fflush(stderr);
        if (!lua_gcd_initialize()) {
            fprintf(stderr, "ERROR: Failed to auto-initialize Lua runtime\n");
            fflush(stderr);
            set_error("Failed to auto-initialize Lua runtime");
            return false;
        }
        fprintf(stderr, "SUCCESS: Lua runtime auto-initialized for REPL\n");
        fflush(stderr);
    }

    if (!lua_code) {
        set_error("No code provided");
        return false;
    }

    std::lock_guard<std::mutex> lock(g_repl_mutex);

    // Initialize REPL Lua state if needed
    if (!g_repl_lua) {
        log_debug("Initializing REPL Lua state");
        g_repl_lua = luaL_newstate();
        if (!g_repl_lua) {
            set_error("Failed to create REPL Lua state");
            return false;
        }

        lua_atpanic(g_repl_lua, lua_panic_handler);
        luaL_openlibs(g_repl_lua);

        try {
            register_superterminal_api(g_repl_lua);
            register_particle_system_lua_api(g_repl_lua);
            register_audio_lua_bindings(g_repl_lua);
            register_assets_lua_bindings(g_repl_lua);
            register_lua_blocking_ops_gcd(g_repl_lua);
        } catch (...) {
            log_debug("Warning: Failed to register some REPL APIs");
        }
    }

    // Execute code
    int result = luaL_dostring(g_repl_lua, lua_code);

    if (result != LUA_OK) {
        const char* error = lua_tostring(g_repl_lua, -1);
        std::string error_msg = error ? error : "Unknown error";
        set_error(error_msg);
        lua_pop(g_repl_lua, 1); // Pop error
        return false;
    }

    clear_error();
    return true;
}

bool lua_gcd_stop_script(void) {
    using namespace LuaGCD;

    fprintf(stderr, "[GCD] lua_gcd_stop_script() called\n");
    fflush(stderr);

    // Check if script is actually running
    if (!g_script_running.load()) {
        fprintf(stderr, "[GCD] No script is running (g_script_running = false)\n");
        fflush(stderr);
        return false;
    }

    uint64_t current_gen = g_script_generation.load();
    fprintf(stderr, "[GCD] Abandoning script generation: %llu\n", current_gen);
    fflush(stderr);

    // Mark this generation as abandoned - script will continue in background but we don't care
    g_abandoned_generation.store(current_gen);

    // Clear the running flag so new scripts can start immediately
    g_script_running = false;

    fprintf(stderr, "[GCD] Script abandoned - new scripts can run immediately\n");
    fflush(stderr);

    return true;
}

bool lua_gcd_is_script_running(void) {
    // A script is "running" if the flag is set AND it hasn't been abandoned
    bool flag_running = LuaGCD::g_script_running.load();
    if (!flag_running) {
        return false;
    }

    // Check if current generation is abandoned
    uint64_t current_gen = LuaGCD::g_script_generation.load();
    uint64_t abandoned_gen = LuaGCD::g_abandoned_generation.load();

    return current_gen > abandoned_gen;
}

const char* lua_gcd_get_current_script_name(void) {
    using namespace LuaGCD;

    std::lock_guard<std::mutex> lock(g_block_mutex);

    if (g_current_script_name.empty()) {
        return nullptr;
    }

    return g_current_script_name.c_str();
}

uint64_t lua_gcd_get_script_elapsed_time_ms(void) {
    using namespace LuaGCD;

    if (!g_script_running.load()) {
        return 0;
    }

    auto elapsed = std::chrono::steady_clock::now() - g_script_start_time;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

const char* lua_gcd_get_last_error(void) {
    using namespace LuaGCD;

    std::lock_guard<std::mutex> lock(g_error_mutex);
    return g_last_error.c_str();
}

void lua_gcd_clear_error(void) {
    using namespace LuaGCD;
    clear_error();
}

void lua_gcd_set_hook_frequency(int instruction_count) {
    LuaGCD::g_hook_frequency = instruction_count;
    LuaGCD::log_debug("Hook frequency set to: %d", instruction_count);
}

void lua_gcd_set_debug_output(bool enabled) {
    LuaGCD::g_debug_output = enabled;
    LuaGCD::log_debug("Debug output %s", enabled ? "enabled" : "disabled");
}

void lua_gcd_set_script_timeout(uint32_t timeout_seconds) {
    LuaGCD::g_script_timeout_seconds = timeout_seconds;
    LuaGCD::log_debug("Script timeout set to: %u seconds", timeout_seconds);
}

uint64_t lua_gcd_get_total_scripts_executed(void) {
    return LuaGCD::g_total_scripts.load();
}

uint64_t lua_gcd_get_successful_scripts_count(void) {
    return LuaGCD::g_successful_scripts.load();
}

uint64_t lua_gcd_get_failed_scripts_count(void) {
    return LuaGCD::g_failed_scripts.load();
}

uint64_t lua_gcd_get_cancelled_scripts_count(void) {
    return LuaGCD::g_cancelled_scripts.load();
}

void lua_gcd_reset_statistics(void) {
    using namespace LuaGCD;

    g_total_scripts = 0;
    g_successful_scripts = 0;
    g_failed_scripts = 0;
    g_cancelled_scripts = 0;

    log_debug("Statistics reset");
}

void lua_gcd_reset_repl(void) {
    using namespace LuaGCD;

    std::lock_guard<std::mutex> lock(g_repl_mutex);

    if (g_repl_lua) {
        lua_close(g_repl_lua);
        g_repl_lua = nullptr;
        log_debug("REPL state reset");
    }
}

void lua_gcd_shutdown_repl(void) {
    lua_gcd_reset_repl();
}

bool lua_gcd_is_on_lua_queue(void) {
    const char* current_label = dispatch_queue_get_label(DISPATCH_CURRENT_QUEUE_LABEL);
    return (current_label && strcmp(current_label, LuaGCD::QUEUE_LABEL) == 0);
}

void lua_gcd_sync(void (^block)(void)) {
    using namespace LuaGCD;

    if (!g_initialized.load() || !g_lua_queue) {
        LuaGCD::log_debug("Cannot sync: runtime not initialized");
        return;
    }

    dispatch_sync(g_lua_queue, block);
}

void lua_gcd_async(void (^block)(void)) {
    using namespace LuaGCD;

    if (!g_initialized.load() || !g_lua_queue) {
        LuaGCD::log_debug("Cannot async: runtime not initialized");
        return;
    }

    dispatch_async(g_lua_queue, block);
}

} // extern "C"
