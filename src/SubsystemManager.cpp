//
//  SubsystemManager.cpp
//  SuperTerminal Framework - Centralized Subsystem Management
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Implementation of centralized subsystem management for SuperTerminal
//

#include "SubsystemManager.h"
#include "GlobalShutdown.h"
#include "audio/ABCPlayerXPCClient.h"
#include "assets/AssetsManager.h"
#include <iostream>

// External C function declaration
extern "C" void console(const char* message);
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstring>

// Forward declarations instead of includes to avoid Objective-C conflicts
namespace SuperTerminal {
    class AudioSystem;
    class MusicPlayer;
    class SynthEngine;
    class MidiEngine;
    
    // Asset dialog interface (C++ only, no Objective-C)
    class AssetDialogsInterface {
    public:
        static void Initialize(AssetsManager* manager);
    };
}

extern "C" {
#include <luajit.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// External C function declarations for subsystems
extern "C" {
// Lua GCD Runtime functions
bool lua_gcd_initialize(void);
void lua_gcd_shutdown(void);
bool lua_gcd_stop_script(void);
void lua_gcd_set_debug_output(bool enabled);
void lua_gcd_set_script_timeout(uint32_t timeout_seconds);
void lua_gcd_set_hook_frequency(int instruction_count);
    
// Graphics system functions
void* graphics_initialize(int width, int height, bool enable_metal, bool enable_skia);
void graphics_shutdown(void* window_handle);
void* superterminal_get_metal_device(void);
    
    // Input system functions
    bool input_system_initialize(void* window_handle);
    void input_system_shutdown(void);
    
    // Audio system functions
    bool audio_system_initialize(void);
    void audio_system_shutdown(void);
    
    // Synth engine functions
    bool synth_initialize(void);
    void synth_shutdown(void);
    
    // Particle system functions (compatibility wrappers)
    bool particle_system_initialize_compat(void* metal_device);
    void particle_system_shutdown_compat(void);
    
    // Bullet system functions (compatibility wrappers)
    bool bullet_system_initialize_compat(void* metal_device, void* sprite_layer);
    void bullet_system_shutdown_compat(void);
    
    // Sprite effects functions
    bool sprite_effects_initialize(void* metal_device, void* shader_library);
    void sprite_effects_shutdown(void);
    
    // Text editor functions
    bool text_editor_initialize(void);
    void text_editor_shutdown(void);
    
    // ABC Player XPC Client functions
    bool abc_client_initialize(void);
    void abc_client_shutdown(void);
    bool abc_client_ping_with_timeout(int timeout_ms);
    void abc_client_set_debug_output(bool debug);
    
    // Reset/clear functions for subsystems
    void overlay_graphics_layer_clear(void);
    void particle_system_clear(void);
    void bullet_clear_all(void);
    void music_stop(void);
    void music_clear_queue(void);
}

using namespace SuperTerminal;

// Static member initialization
SubsystemManager* g_instance = nullptr;
std::mutex g_instance_mutex;

// Subsystem name mapping for logging
static const char* getSubsystemName(SubsystemType type) {
    switch (type) {
        case SUBSYSTEM_TYPE_GRAPHICS: return "Graphics";
        case SUBSYSTEM_TYPE_AUDIO: return "Audio";
        case SUBSYSTEM_TYPE_INPUT: return "Input";
        case SUBSYSTEM_TYPE_LUA_RUNTIME: return "LuaRuntime";
        case SUBSYSTEM_TYPE_PARTICLE_SYSTEM: return "ParticleSystem";
        case SUBSYSTEM_TYPE_BULLET_SYSTEM: return "BulletSystem";
        case SUBSYSTEM_TYPE_SPRITE_EFFECTS: return "SpriteEffects";
        case SUBSYSTEM_TYPE_TEXT_EDITOR: return "TextEditor";
        case SUBSYSTEM_TYPE_MIDI: return "MIDI";
        case SUBSYSTEM_TYPE_SYNTH: return "Synth";
        case SUBSYSTEM_TYPE_MUSIC_PLAYER: return "MusicPlayer";
        case SUBSYSTEM_TYPE_ASSETS: return "Assets";
        default: return "Unknown";
    }
}

// SubsystemManager Implementation

SubsystemManager& SubsystemManager::getInstance() {
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    if (!g_instance) {
        g_instance = new SubsystemManager();
    }
    return *g_instance;
}

SubsystemManager::SubsystemManager()
    : m_lua_state(nullptr)
    , m_metal_device(nullptr)
    , m_window_handle(nullptr)
    , m_assets_manager(nullptr)
{
    // Initialize all subsystem states to uninitialized
    for (int i = 0; i < SUBSYSTEM_TYPE_COUNT; ++i) {
        m_subsystem_states[i] = SUBSYSTEM_STATE_UNINITIALIZED;
    }
    
    std::cout << "SubsystemManager: Instance created" << std::endl;
}

SubsystemManager::~SubsystemManager() {
    if (m_initialized.load()) {
        shutdownAll(2000);
    }
    std::cout << "SubsystemManager: Instance destroyed" << std::endl;
}

bool SubsystemManager::initializeAll(const SubsystemConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_initialized.load()) {
        std::cout << "SubsystemManager: Already initialized" << std::endl;
        return true;
    }
    
    if (m_shutting_down.load()) {
        std::cerr << "SubsystemManager: Cannot initialize during shutdown" << std::endl;
        return false;
    }
    
    std::cout << "SubsystemManager: *** INITIALIZING ALL SUBSYSTEMS ***" << std::endl;
    m_initialization_start = std::chrono::steady_clock::now();
    m_config = config;
    
    // Initialize subsystems in dependency order (foundation → dependent systems)
    bool success = true;
    
    // 1. Assets manager (FIRST - everything depends on being able to load assets)
    if (success) {
        success = initializeAssetsSubsystem(config);
    }
    
    // 2. Graphics (foundation - provides Metal device, window handle)
    if (success) {
        success = initializeGraphicsSubsystem(config);
    }
    
    // 3. Input (depends on graphics window handle)
    if (success) {
        success = initializeInputSubsystem(config);
    }
    
    // 4. Audio (independent, but may be triggered by game systems, may load assets)
    if (success) {
        success = initializeAudioSubsystem(config);
    }
    
    // 5. Lua Runtime (SKIPPED - causes hanging, let applications handle Lua init)
    std::cout << "SubsystemManager: Skipping Lua initialization to avoid hanging" << std::endl;
    setSubsystemState(SUBSYSTEM_TYPE_LUA_RUNTIME, SUBSYSTEM_STATE_UNINITIALIZED);
    
    // 6. Visual effects foundation (sprite effects used by particles/bullets)
    if (success) {
        success = initializeSpriteEffectsSubsystem(config);
    }
    
    // 7. Game systems that use visual effects
    if (success) {
        success = initializeParticleSubsystem(config);
    }
    
    if (success) {
        success = initializeBulletSubsystem(config);
    }
    
    // 8. Text editor (high-level UI, depends on input and graphics)
    if (success) {
        success = initializeTextEditorSubsystem(config);
    }
    
    if (success) {
        m_initialized = true;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_initialization_start).count();
        
        std::cout << "SubsystemManager: *** ALL SUBSYSTEMS INITIALIZED SUCCESSFULLY ***" << std::endl;
        std::cout << "SubsystemManager: Total initialization time: " << elapsed << "ms" << std::endl;
        
        // Update performance metrics
        updatePerformanceMetrics();
    } else {
        std::cerr << "SubsystemManager: *** INITIALIZATION FAILED ***" << std::endl;
        // Shutdown any subsystems that were successfully initialized
        shutdownAll(2000);
    }
    
    return success;
}

bool SubsystemManager::shutdownAll(int timeout_ms) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized.load() || m_shutting_down.load()) {
        std::cout << "SubsystemManager: Already shut down or shutting down" << std::endl;
        return true;
    }
    
    std::cout << "SubsystemManager: *** SHUTTING DOWN ALL SUBSYSTEMS ***" << std::endl;
    m_shutting_down = true;
    
    // Request emergency shutdown through the global system
    request_emergency_shutdown(timeout_ms);
    
    auto shutdown_start = std::chrono::steady_clock::now();
    
    // Execute custom shutdown callbacks first (in priority order)
    std::sort(m_shutdown_callbacks.begin(), m_shutdown_callbacks.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    
    for (const auto& callback_pair : m_shutdown_callbacks) {
        try {
            callback_pair.first();
        } catch (const std::exception& e) {
            std::cerr << "SubsystemManager: Exception in shutdown callback: " << e.what() << std::endl;
        }
    }
    
    // Shutdown subsystems in reverse dependency order
    // Higher-level systems that depend on others shut down first
    std::cout << "SubsystemManager: Shutting down Text Editor (before closing window)" << std::endl;
    shutdownTextEditorSubsystem();
    
    std::cout << "SubsystemManager: Shutting down Bullet System (can trigger particles)" << std::endl;
    shutdownBulletSubsystem();
    
    std::cout << "SubsystemManager: Shutting down Particle System (uses sprite effects)" << std::endl;
    shutdownParticleSubsystem();
    
    std::cout << "SubsystemManager: Shutting down Sprite Effects (visual foundation)" << std::endl;
    shutdownSpriteEffectsSubsystem();
    
    std::cout << "SubsystemManager: Shutting down Lua Runtime (after dependent systems)" << std::endl;
    shutdownLuaRuntimeSubsystem();
    
    std::cout << "SubsystemManager: Shutting down Audio System (triggered by game systems)" << std::endl;
    shutdownAudioSubsystem();
    
    std::cout << "SubsystemManager: Shutting down Input System (drives UI/game interaction)" << std::endl;
    shutdownInputSubsystem();
    
    std::cout << "SubsystemManager: Shutting down Graphics System and closing window (foundation layer)" << std::endl;
    shutdownGraphicsSubsystem();
    
    std::cout << "SubsystemManager: Shutting down Assets Manager LAST (everything depends on it)" << std::endl;
    shutdownAssetsSubsystem();
    
    // Wait for clean shutdown
    bool clean_shutdown = waitForSubsystemsToShutdown(timeout_ms);
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - shutdown_start).count();
    
    m_initialized = false;
    m_shutting_down = false;
    
    if (clean_shutdown) {
        std::cout << "SubsystemManager: *** ALL SUBSYSTEMS SHUTDOWN CLEANLY ***" << std::endl;
        std::cout << "SubsystemManager: Total shutdown time: " << elapsed << "ms" << std::endl;
    } else {
        std::cerr << "SubsystemManager: *** FORCED SHUTDOWN AFTER TIMEOUT ***" << std::endl;
        std::cerr << "SubsystemManager: Shutdown time: " << elapsed << "ms (timeout: " << timeout_ms << "ms)" << std::endl;
    }
    
    // Reset the global shutdown system
    reset_shutdown_system();
    
    return clean_shutdown;
}

bool SubsystemManager::resetAll(int timeout_ms) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized.load()) {
        std::cout << "SubsystemManager: Cannot reset - not initialized" << std::endl;
        return false;
    }
    
    if (m_shutting_down.load()) {
        std::cout << "SubsystemManager: Cannot reset during shutdown" << std::endl;
        return false;
    }
    
    std::cout << "SubsystemManager: *** RESETTING ALL SUBSYSTEMS ***" << std::endl;
    
    // Add console output for visibility in SuperTerminal
    console("SubsystemManager: *** RESETTING ALL SUBSYSTEMS ***");
    
    auto reset_start = std::chrono::steady_clock::now();
    
    // Reset subsystems with timeout protection
    bool success = true;
    auto timeout_point = reset_start + std::chrono::milliseconds(timeout_ms);
    
    try {
        // Reset in dependency order (light operations first)
        
        // Quick resets first (no external calls)
        if (std::chrono::steady_clock::now() < timeout_point) {
            console("SubsystemManager: Resetting text editor, input, and Lua runtime...");
            success &= resetTextEditorSubsystem();
            success &= resetInputSubsystem();
            success &= resetLuaRuntimeSubsystem();
        }
        
        // Graphics operations (may involve GPU calls)
        if (success && std::chrono::steady_clock::now() < timeout_point) {
            std::cout << "SubsystemManager: Clearing graphics..." << std::endl;
            console("SubsystemManager: Clearing graphics and sprite effects...");
            success &= resetGraphicsSubsystem();
            success &= resetSpriteEffectsSubsystem();
        }
        
        // Game object systems
        if (success && std::chrono::steady_clock::now() < timeout_point) {
            std::cout << "SubsystemManager: Clearing game objects..." << std::endl;
            console("SubsystemManager: Clearing particles and bullets...");
            success &= resetParticleSubsystem();
            success &= resetBulletSubsystem();
        }
        
        // Audio last (may have longer cleanup)
        if (success && std::chrono::steady_clock::now() < timeout_point) {
            std::cout << "SubsystemManager: Stopping audio..." << std::endl;
            console("SubsystemManager: Stopping all audio and music...");
            success &= resetAudioSubsystem();
        }
        
        // Check for timeout
        if (std::chrono::steady_clock::now() >= timeout_point) {
            std::cerr << "SubsystemManager: Reset timeout exceeded!" << std::endl;
            success = false;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Exception during reset: " << e.what() << std::endl;
        success = false;
    } catch (...) {
        std::cerr << "SubsystemManager: Unknown exception during reset" << std::endl;
        success = false;
    }
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - reset_start).count();
    
    if (success) {
        std::cout << "SubsystemManager: *** ALL SUBSYSTEMS RESET SUCCESSFULLY ***" << std::endl;
        std::cout << "SubsystemManager: Total reset time: " << elapsed << "ms" << std::endl;
        console("✅ ALL SUBSYSTEMS RESET SUCCESSFULLY");
        char time_msg[100];
        snprintf(time_msg, sizeof(time_msg), "Reset completed in %lldms", elapsed);
        console(time_msg);
    } else {
        std::cerr << "SubsystemManager: *** RESET FAILED OR INCOMPLETE ***" << std::endl;
        std::cerr << "SubsystemManager: Reset time: " << elapsed << "ms (timeout: " << timeout_ms << "ms)" << std::endl;
        console("❌ SUBSYSTEM RESET FAILED OR INCOMPLETE");
        char error_msg[150];
        snprintf(error_msg, sizeof(error_msg), "Reset failed after %lldms (timeout: %dms)", elapsed, timeout_ms);
        console(error_msg);
    }
    
    return success;
}

void SubsystemManager::forceShutdownAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::cout << "SubsystemManager: *** FORCE SHUTDOWN REQUESTED ***" << std::endl;
    m_emergency_shutdown = true;
    
    // Trigger global emergency shutdown
    force_terminate_all_subsystems();
    
    // Force set all states to shutdown
    for (int i = 0; i < SUBSYSTEM_TYPE_COUNT; ++i) {
        m_subsystem_states[i] = SUBSYSTEM_STATE_SHUTDOWN;
    }
    
    m_initialized = false;
    m_shutting_down = false;
    
    std::cout << "SubsystemManager: *** FORCE SHUTDOWN COMPLETE ***" << std::endl;
}

SubsystemState SubsystemManager::getSubsystemState(SubsystemType type) const {
    if (type >= 0 && type < SUBSYSTEM_TYPE_COUNT) {
        return m_subsystem_states[type].load();
    }
    return SUBSYSTEM_STATE_ERROR;
}

bool SubsystemManager::isSystemHealthy() const {
    if (!m_initialized.load() || m_shutting_down.load() || m_emergency_shutdown.load()) {
        return false;
    }
    
    // Check if all subsystems are in running state
    for (int i = 0; i < SUBSYSTEM_TYPE_COUNT; ++i) {
        SubsystemState state = m_subsystem_states[i].load();
        if (state != SUBSYSTEM_STATE_RUNNING && state != SUBSYSTEM_STATE_UNINITIALIZED) {
            return false;
        }
    }
    
    return true;
}

std::map<std::string, std::string> SubsystemManager::getPerformanceMetrics() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_performance_metrics;
}

void SubsystemManager::registerShutdownCallback(std::function<void()> callback, int priority) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_shutdown_callbacks.emplace_back(callback, priority);
}

bool SubsystemManager::isEmergencyShutdownInProgress() const {
    return m_emergency_shutdown.load() || is_emergency_shutdown_requested();
}



lua_State* SubsystemManager::getLuaState() const {
    return m_lua_state;
}

SuperTerminal::AssetsManager* SubsystemManager::getAssetsManager() const {
    return m_assets_manager;
}

// Individual subsystem initialization methods

bool SubsystemManager::initializeGraphicsSubsystem(const SubsystemConfig& config) {
    logSubsystemState(SUBSYSTEM_TYPE_GRAPHICS, SUBSYSTEM_STATE_INITIALIZING, "Starting graphics initialization");
    
    try {
        m_window_handle = graphics_initialize(config.window_width, config.window_height, 
                                            config.enable_metal_rendering, config.enable_skia_graphics);
        
        if (!m_window_handle) {
            setSubsystemState(SUBSYSTEM_TYPE_GRAPHICS, SUBSYSTEM_STATE_ERROR);
            return false;
        }
        
        // Get the Metal device from the framework for use by other subsystems
        m_metal_device = superterminal_get_metal_device();
        if (!m_metal_device) {
            std::cerr << "SubsystemManager: Warning - Metal device not available after graphics init" << std::endl;
        } else {
            std::cout << "SubsystemManager: Metal device acquired successfully" << std::endl;
        }
        
        setSubsystemState(SUBSYSTEM_TYPE_GRAPHICS, SUBSYSTEM_STATE_RUNNING);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Graphics initialization failed: " << e.what() << std::endl;
        setSubsystemState(SUBSYSTEM_TYPE_GRAPHICS, SUBSYSTEM_STATE_ERROR);
        return false;
    }
}

bool SubsystemManager::initializeAudioSubsystem(const SubsystemConfig& config) {
    logSubsystemState(SUBSYSTEM_TYPE_AUDIO, SUBSYSTEM_STATE_INITIALIZING, "Starting audio initialization");
    
    try {
        // Use C API for audio initialization to avoid Objective-C header conflicts
        if (audio_system_initialize()) {
            setSubsystemState(SUBSYSTEM_TYPE_AUDIO, SUBSYSTEM_STATE_RUNNING);
            
            // Initialize music player subsystems if enabled
            if (config.enable_midi) {
                setSubsystemState(SUBSYSTEM_TYPE_MIDI, SUBSYSTEM_STATE_RUNNING);
            }
            if (config.enable_synthesis) {
                // Actually initialize the synth engine
                if (synth_initialize()) {
                    setSubsystemState(SUBSYSTEM_TYPE_SYNTH, SUBSYSTEM_STATE_RUNNING);
                    std::cout << "SubsystemManager: Synth engine initialized successfully" << std::endl;
                } else {
                    setSubsystemState(SUBSYSTEM_TYPE_SYNTH, SUBSYSTEM_STATE_ERROR);
                    std::cerr << "SubsystemManager: Failed to initialize synth engine" << std::endl;
                }
            }
            if (config.enable_midi || config.enable_synthesis) {
                setSubsystemState(SUBSYSTEM_TYPE_MUSIC_PLAYER, SUBSYSTEM_STATE_RUNNING);
                
                // Initialize ABC Player XPC Client with health check
                std::cerr << "SubsystemManager: Starting ABC Player XPC service..." << std::endl;
                
                // Enable debug output for troubleshooting
                if (config.enable_debug_logging) {
                    abc_client_set_debug_output(true);
                }
                
                if (abc_client_initialize()) {
                    // Ping the XPC service with a short timeout to verify it's responsive
                    std::cerr << "SubsystemManager: Pinging ABC Player XPC service (2 second timeout)..." << std::endl;
                    
                    if (abc_client_ping_with_timeout(2000)) {
                        logSubsystemState(SUBSYSTEM_TYPE_MUSIC_PLAYER, SUBSYSTEM_STATE_RUNNING, "ABC Player XPC service started and responding");
                        std::cerr << "SubsystemManager: ABC Player XPC service is healthy" << std::endl;
                        
                        // Play the cool retro startup boot sound! 🎵
                        std::cerr << "SubsystemManager: Playing retro startup boot sound... 🎵💾" << std::endl;
                        
                        // Try to load from assets directory
                        const char* startup_sound_paths[] = {
                            "assets/tunes /startup_boot.abc",
                            "../assets/tunes /startup_boot.abc",
                            "../../assets/tunes /startup_boot.abc",
                            nullptr
                        };
                        
                        bool played = false;
                        for (int i = 0; startup_sound_paths[i] != nullptr && !played; i++) {
                            if (abc_client_play_abc_file(startup_sound_paths[i])) {
                                std::cerr << "SubsystemManager: ✓ Startup boot sound loaded from: " << startup_sound_paths[i] << std::endl;
                                played = true;
                            }
                        }
                        
                        if (!played) {
                            // Fallback to inline startup sound if file not found
                            std::cerr << "SubsystemManager: Startup sound file not found, using fallback tune" << std::endl;
                            const char* fallback_tune = 
                                "X:1\n"
                                "T:SuperTerminal Boot\n"
                                "M:4/4\n"
                                "L:1/16\n"
                                "K:C\n"
                                "Q:1/4=200\n"
                                "V:1\n"
                                "C4 E4 G4 c4 | e4 g4 c'4 e'4 | [c'e'g']8 z8 |]\n";
                            abc_client_play_abc(fallback_tune, "SuperTerminal Boot (Fallback)");
                        }
                    } else {
                        // Service didn't respond - mark as dead but continue startup
                        setSubsystemState(SUBSYSTEM_TYPE_MUSIC_PLAYER, SUBSYSTEM_STATE_WARNING);
                        std::cerr << "ERROR: SubsystemManager: ABC Player XPC service failed to respond within timeout" << std::endl;
                        std::cerr << "ERROR: ABC music playback will not be available" << std::endl;
                        std::cerr << "NOTE: Continuing startup with ABC Player marked as dead" << std::endl;
                    }
                } else {
                    // Service failed to initialize - mark as dead but continue startup
                    setSubsystemState(SUBSYSTEM_TYPE_MUSIC_PLAYER, SUBSYSTEM_STATE_WARNING);
                    std::cerr << "ERROR: SubsystemManager: ABC Player XPC Client initialization failed" << std::endl;
                    std::cerr << "ERROR: ABC music playback will not be available" << std::endl;
                    std::cerr << "NOTE: Continuing startup with ABC Player marked as dead" << std::endl;
                }
            }
            
            return true;
        } else {
            setSubsystemState(SUBSYSTEM_TYPE_AUDIO, SUBSYSTEM_STATE_ERROR);
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Audio initialization failed: " << e.what() << std::endl;
        setSubsystemState(SUBSYSTEM_TYPE_AUDIO, SUBSYSTEM_STATE_ERROR);
        return false;
    }
}

bool SubsystemManager::initializeInputSubsystem(const SubsystemConfig& config) {
    logSubsystemState(SUBSYSTEM_TYPE_INPUT, SUBSYSTEM_STATE_INITIALIZING, "Starting input initialization");
    
    try {
        if (!input_system_initialize(m_window_handle)) {
            setSubsystemState(SUBSYSTEM_TYPE_INPUT, SUBSYSTEM_STATE_ERROR);
            return false;
        }
        
        setSubsystemState(SUBSYSTEM_TYPE_INPUT, SUBSYSTEM_STATE_RUNNING);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Input initialization failed: " << e.what() << std::endl;
        setSubsystemState(SUBSYSTEM_TYPE_INPUT, SUBSYSTEM_STATE_ERROR);
        return false;
    }
}

bool SubsystemManager::initializeLuaRuntimeSubsystem(const SubsystemConfig& config) {
    logSubsystemState(SUBSYSTEM_TYPE_LUA_RUNTIME, SUBSYSTEM_STATE_INITIALIZING, "Starting Lua GCD runtime initialization");
    
    try {
        if (!lua_gcd_initialize()) {
            std::cerr << "SubsystemManager: Failed to initialize Lua GCD runtime" << std::endl;
            setSubsystemState(SUBSYSTEM_TYPE_LUA_RUNTIME, SUBSYSTEM_STATE_ERROR);
            return false;
        }
        
        // Configure Lua runtime
        lua_gcd_set_debug_output(config.enable_debug_logging);
        lua_gcd_set_script_timeout(300); // 5 minute timeout
        lua_gcd_set_hook_frequency(1000); // Check cancellation every 1000 instructions
        
        std::cout << "SubsystemManager: Lua GCD runtime initialized successfully" << std::endl;
        setSubsystemState(SUBSYSTEM_TYPE_LUA_RUNTIME, SUBSYSTEM_STATE_RUNNING);
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Exception during Lua initialization: " << e.what() << std::endl;
        setSubsystemState(SUBSYSTEM_TYPE_LUA_RUNTIME, SUBSYSTEM_STATE_ERROR);
        return false;
    }
}

bool SubsystemManager::initializeParticleSubsystem(const SubsystemConfig& config) {
    logSubsystemState(SUBSYSTEM_TYPE_PARTICLE_SYSTEM, SUBSYSTEM_STATE_INITIALIZING, "Starting particle system initialization");
    
    try {
        if (!particle_system_initialize_compat(m_metal_device)) {
            setSubsystemState(SUBSYSTEM_TYPE_PARTICLE_SYSTEM, SUBSYSTEM_STATE_ERROR);
            return false;
        }
        setSubsystemState(SUBSYSTEM_TYPE_PARTICLE_SYSTEM, SUBSYSTEM_STATE_RUNNING);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Particle system initialization failed: " << e.what() << std::endl;
        setSubsystemState(SUBSYSTEM_TYPE_PARTICLE_SYSTEM, SUBSYSTEM_STATE_ERROR);
        return false;
    }
}

bool SubsystemManager::initializeBulletSubsystem(const SubsystemConfig& config) {
    logSubsystemState(SUBSYSTEM_TYPE_BULLET_SYSTEM, SUBSYSTEM_STATE_INITIALIZING, "Starting bullet system initialization");
    
    try {
        if (!bullet_system_initialize_compat(m_metal_device, nullptr)) {
            setSubsystemState(SUBSYSTEM_TYPE_BULLET_SYSTEM, SUBSYSTEM_STATE_ERROR);
            return false;
        }
        
        setSubsystemState(SUBSYSTEM_TYPE_BULLET_SYSTEM, SUBSYSTEM_STATE_RUNNING);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Bullet system initialization failed: " << e.what() << std::endl;
        setSubsystemState(SUBSYSTEM_TYPE_BULLET_SYSTEM, SUBSYSTEM_STATE_ERROR);
        return false;
    }
}

bool SubsystemManager::initializeSpriteEffectsSubsystem(const SubsystemConfig& config) {
    logSubsystemState(SUBSYSTEM_TYPE_SPRITE_EFFECTS, SUBSYSTEM_STATE_INITIALIZING, "Starting sprite effects initialization");
    
    try {
        if (!sprite_effects_initialize(m_metal_device, nullptr)) {
            setSubsystemState(SUBSYSTEM_TYPE_SPRITE_EFFECTS, SUBSYSTEM_STATE_ERROR);
            return false;
        }
        
        setSubsystemState(SUBSYSTEM_TYPE_SPRITE_EFFECTS, SUBSYSTEM_STATE_RUNNING);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Sprite effects initialization failed: " << e.what() << std::endl;
        setSubsystemState(SUBSYSTEM_TYPE_SPRITE_EFFECTS, SUBSYSTEM_STATE_ERROR);
        return false;
    }
}

bool SubsystemManager::initializeTextEditorSubsystem(const SubsystemConfig& config) {
    logSubsystemState(SUBSYSTEM_TYPE_TEXT_EDITOR, SUBSYSTEM_STATE_INITIALIZING, "Starting text editor initialization");
    
    try {
        if (!text_editor_initialize()) {
            setSubsystemState(SUBSYSTEM_TYPE_TEXT_EDITOR, SUBSYSTEM_STATE_ERROR);
            return false;
        }
        
        setSubsystemState(SUBSYSTEM_TYPE_TEXT_EDITOR, SUBSYSTEM_STATE_RUNNING);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Text editor initialization failed: " << e.what() << std::endl;
        setSubsystemState(SUBSYSTEM_TYPE_TEXT_EDITOR, SUBSYSTEM_STATE_ERROR);
        return false;
    }
}

// Individual subsystem shutdown methods

void SubsystemManager::shutdownGraphicsSubsystem() {
    if (getSubsystemState(SUBSYSTEM_TYPE_GRAPHICS) == SUBSYSTEM_STATE_RUNNING) {
        logSubsystemState(SUBSYSTEM_TYPE_GRAPHICS, SUBSYSTEM_STATE_SHUTTING_DOWN, "Shutting down graphics");
        
        try {
            graphics_shutdown(m_window_handle);
            m_window_handle = nullptr;
            setSubsystemState(SUBSYSTEM_TYPE_GRAPHICS, SUBSYSTEM_STATE_SHUTDOWN);
        } catch (const std::exception& e) {
            std::cerr << "SubsystemManager: Graphics shutdown error: " << e.what() << std::endl;
            setSubsystemState(SUBSYSTEM_TYPE_GRAPHICS, SUBSYSTEM_STATE_ERROR);
        }
    }
}

void SubsystemManager::shutdownLuaRuntimeSubsystem() {
    if (getSubsystemState(SUBSYSTEM_TYPE_LUA_RUNTIME) == SUBSYSTEM_STATE_RUNNING) {
        logSubsystemState(SUBSYSTEM_TYPE_LUA_RUNTIME, SUBSYSTEM_STATE_SHUTTING_DOWN, "Shutting down Lua GCD runtime");
        
        // Stop any running scripts
        lua_gcd_stop_script();
        
        // Shutdown Lua runtime
        lua_gcd_shutdown();
        
        setSubsystemState(SUBSYSTEM_TYPE_LUA_RUNTIME, SUBSYSTEM_STATE_SHUTDOWN);
        std::cout << "SubsystemManager: Lua GCD runtime shutdown complete" << std::endl;
    }
}

void SubsystemManager::shutdownAudioSubsystem() {
    if (getSubsystemState(SUBSYSTEM_TYPE_MUSIC_PLAYER) == SUBSYSTEM_STATE_RUNNING) {
        logSubsystemState(SUBSYSTEM_TYPE_MUSIC_PLAYER, SUBSYSTEM_STATE_SHUTTING_DOWN, "Shutting down music player");
        
        // Shutdown ABC Player Client
        abc_client_shutdown();
        
        setSubsystemState(SUBSYSTEM_TYPE_MUSIC_PLAYER, SUBSYSTEM_STATE_SHUTDOWN);
    }
    
    if (getSubsystemState(SUBSYSTEM_TYPE_SYNTH) == SUBSYSTEM_STATE_RUNNING) {
        logSubsystemState(SUBSYSTEM_TYPE_SYNTH, SUBSYSTEM_STATE_SHUTTING_DOWN, "Shutting down synth");
        setSubsystemState(SUBSYSTEM_TYPE_SYNTH, SUBSYSTEM_STATE_SHUTDOWN);
    }
    
    if (getSubsystemState(SUBSYSTEM_TYPE_MIDI) == SUBSYSTEM_STATE_RUNNING) {
        logSubsystemState(SUBSYSTEM_TYPE_MIDI, SUBSYSTEM_STATE_SHUTTING_DOWN, "Shutting down MIDI");
        setSubsystemState(SUBSYSTEM_TYPE_MIDI, SUBSYSTEM_STATE_SHUTDOWN);
    }
    
    if (getSubsystemState(SUBSYSTEM_TYPE_AUDIO) == SUBSYSTEM_STATE_RUNNING) {
        logSubsystemState(SUBSYSTEM_TYPE_AUDIO, SUBSYSTEM_STATE_SHUTTING_DOWN, "Shutting down audio system");
        audio_system_shutdown();
        setSubsystemState(SUBSYSTEM_TYPE_AUDIO, SUBSYSTEM_STATE_SHUTDOWN);
    }
}

void SubsystemManager::shutdownInputSubsystem() {
    if (getSubsystemState(SUBSYSTEM_TYPE_INPUT) == SUBSYSTEM_STATE_RUNNING) {
        logSubsystemState(SUBSYSTEM_TYPE_INPUT, SUBSYSTEM_STATE_SHUTTING_DOWN, "Shutting down input system");
        input_system_shutdown();
        setSubsystemState(SUBSYSTEM_TYPE_INPUT, SUBSYSTEM_STATE_SHUTDOWN);
    }
}



void SubsystemManager::shutdownParticleSubsystem() {
    if (getSubsystemState(SUBSYSTEM_TYPE_PARTICLE_SYSTEM) == SUBSYSTEM_STATE_RUNNING) {
        logSubsystemState(SUBSYSTEM_TYPE_PARTICLE_SYSTEM, SUBSYSTEM_STATE_SHUTTING_DOWN, "Shutting down particle system");
        particle_system_shutdown_compat();
        setSubsystemState(SUBSYSTEM_TYPE_PARTICLE_SYSTEM, SUBSYSTEM_STATE_SHUTDOWN);
    }
}

void SubsystemManager::shutdownBulletSubsystem() {
    if (getSubsystemState(SUBSYSTEM_TYPE_BULLET_SYSTEM) == SUBSYSTEM_STATE_RUNNING) {
        logSubsystemState(SUBSYSTEM_TYPE_BULLET_SYSTEM, SUBSYSTEM_STATE_SHUTTING_DOWN, "Shutting down bullet system");
        bullet_system_shutdown_compat();
        setSubsystemState(SUBSYSTEM_TYPE_BULLET_SYSTEM, SUBSYSTEM_STATE_SHUTDOWN);
    }
}

void SubsystemManager::shutdownSpriteEffectsSubsystem() {
    if (getSubsystemState(SUBSYSTEM_TYPE_SPRITE_EFFECTS) == SUBSYSTEM_STATE_RUNNING) {
        logSubsystemState(SUBSYSTEM_TYPE_SPRITE_EFFECTS, SUBSYSTEM_STATE_SHUTTING_DOWN, "Shutting down sprite effects");
        sprite_effects_shutdown();
        setSubsystemState(SUBSYSTEM_TYPE_SPRITE_EFFECTS, SUBSYSTEM_STATE_SHUTDOWN);
    }
}

void SubsystemManager::shutdownTextEditorSubsystem() {
    if (getSubsystemState(SUBSYSTEM_TYPE_TEXT_EDITOR) == SUBSYSTEM_STATE_RUNNING) {
        logSubsystemState(SUBSYSTEM_TYPE_TEXT_EDITOR, SUBSYSTEM_STATE_SHUTTING_DOWN, "Shutting down text editor");
        text_editor_shutdown();
        setSubsystemState(SUBSYSTEM_TYPE_TEXT_EDITOR, SUBSYSTEM_STATE_SHUTDOWN);
    }
}

bool SubsystemManager::initializeAssetsSubsystem(const SubsystemConfig& config) {
    logSubsystemState(SUBSYSTEM_TYPE_ASSETS, SUBSYSTEM_STATE_INITIALIZING, "Starting assets manager initialization");
    
    try {
        std::cout << "SubsystemManager: [DEBUG] Creating AssetsManager instance..." << std::endl;
        m_assets_manager = new SuperTerminal::AssetsManager();
        std::cout << "SubsystemManager: [DEBUG] AssetsManager instance created" << std::endl;
        
        // Ensure user assets directory exists
        std::string userAssetsDir = SuperTerminal::AssetsManager::getUserAssetsPath();
        std::cout << "SubsystemManager: [DEBUG] User assets dir: '" << userAssetsDir << "'" << std::endl;
        
        if (!userAssetsDir.empty()) {
            // Create directory recursively
            std::string cmd = "mkdir -p \"" + userAssetsDir + "\"";
            std::cout << "SubsystemManager: [DEBUG] Running command: " << cmd << std::endl;
            int result = system(cmd.c_str());
            std::cout << "SubsystemManager: [DEBUG] mkdir result: " << result << std::endl;
        } else {
            std::cout << "SubsystemManager: [DEBUG] WARNING: User assets dir is empty!" << std::endl;
        }
        
        // Try multiple database locations in order of preference
        std::vector<std::string> dbPaths = {
            userAssetsDir + "/assets.db",  // User directory
            SuperTerminal::AssetsManager::getBundleResourcesPath() + "/assets.db",  // Bundle resources
            "./assets.db"  // Current directory
        };
        
        std::cout << "SubsystemManager: [DEBUG] Will try " << dbPaths.size() << " database paths:" << std::endl;
        for (size_t i = 0; i < dbPaths.size(); i++) {
            std::cout << "SubsystemManager: [DEBUG]   " << (i+1) << ". " << dbPaths[i] << std::endl;
        }
        
        bool initialized = false;
        for (size_t i = 0; i < dbPaths.size(); i++) {
            const auto& dbPath = dbPaths[i];
            std::cout << "SubsystemManager: [DEBUG] Trying path " << (i+1) << ": " << dbPath << std::endl;
            
            if (m_assets_manager->initialize(dbPath)) {
                std::cout << "SubsystemManager: Assets database opened: " << dbPath << std::endl;
                initialized = true;
                break;
            } else {
                std::cout << "SubsystemManager: [DEBUG] Failed to open: " << m_assets_manager->getLastError() << std::endl;
            }
        }
        
        if (!initialized) {
            // Create new database in user directory
            std::string userDbPath = userAssetsDir + "/assets.db";
            std::cout << "SubsystemManager: [DEBUG] All paths failed, creating new database: " << userDbPath << std::endl;
            
            if (m_assets_manager->initialize(userDbPath)) {
                std::cout << "SubsystemManager: New assets database created successfully" << std::endl;
                initialized = true;
            } else {
                std::cout << "SubsystemManager: [DEBUG] Failed to create new database: " << m_assets_manager->getLastError() << std::endl;
            }
        }
        
        if (!initialized) {
            std::cerr << "SubsystemManager: [ERROR] Failed to initialize assets database after all attempts" << std::endl;
            std::cerr << "SubsystemManager: [ERROR] Last error: " << m_assets_manager->getLastError() << std::endl;
            delete m_assets_manager;
            m_assets_manager = nullptr;
            setSubsystemState(SUBSYSTEM_TYPE_ASSETS, SUBSYSTEM_STATE_ERROR);
            return false;
        }
        
        // Note: Dialog system initialization is handled in menu system (Objective-C++)
        // We just provide the AssetsManager instance via getAssetsManager()
        
        setSubsystemState(SUBSYSTEM_TYPE_ASSETS, SUBSYSTEM_STATE_RUNNING);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Assets initialization failed: " << e.what() << std::endl;
        if (m_assets_manager) {
            delete m_assets_manager;
            m_assets_manager = nullptr;
        }
        setSubsystemState(SUBSYSTEM_TYPE_ASSETS, SUBSYSTEM_STATE_ERROR);
        return false;
    }
}

void SubsystemManager::shutdownAssetsSubsystem() {
    if (getSubsystemState(SUBSYSTEM_TYPE_ASSETS) == SUBSYSTEM_STATE_RUNNING) {
        logSubsystemState(SUBSYSTEM_TYPE_ASSETS, SUBSYSTEM_STATE_SHUTTING_DOWN, "Shutting down assets manager");
        
        if (m_assets_manager) {
            m_assets_manager->shutdown();
            delete m_assets_manager;
            m_assets_manager = nullptr;
        }
        
        setSubsystemState(SUBSYSTEM_TYPE_ASSETS, SUBSYSTEM_STATE_SHUTDOWN);
    }
}

bool SubsystemManager::resetAssetsSubsystem() {
    if (m_assets_manager) {
        try {
            // Clear asset cache to free memory
            m_assets_manager->clearCache();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "SubsystemManager: Assets reset error: " << e.what() << std::endl;
            return false;
        }
    }
    return true;
}

bool SubsystemManager::resetGraphicsSubsystem() {
    try {
        // Clear overlay graphics layer (fast operation)
        overlay_graphics_layer_clear();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Graphics reset error: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "SubsystemManager: Unknown error in graphics reset" << std::endl;
        return false;
    }
}

bool SubsystemManager::resetAudioSubsystem() {
    try {
        // Stop all music and clear queues (may take time)
        music_stop();
        music_clear_queue();
        
        // Stop all sounds - implement when available
        // audio_stop_all_sounds();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Audio reset error: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "SubsystemManager: Unknown error in audio reset" << std::endl;
        return false;
    }
}

bool SubsystemManager::resetInputSubsystem() {
    // Input system typically doesn't need reset - just clear any pending events
    return true;
}

bool SubsystemManager::resetLuaRuntimeSubsystem() {
    // Lua runtime reset is handled externally - no action needed
    return true;
}

bool SubsystemManager::resetParticleSubsystem() {
    try {
        particle_system_clear();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Particle system reset error: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "SubsystemManager: Unknown error in particle reset" << std::endl;
        return false;
    }
}

bool SubsystemManager::resetBulletSubsystem() {
    try {
        bullet_clear_all();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Bullet system reset error: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "SubsystemManager: Unknown error in bullet reset" << std::endl;
        return false;
    }
}

bool SubsystemManager::resetSpriteEffectsSubsystem() {
    try {
        // Clear sprite effects - implement when sprite clear API is available
        // sprite_effects_clear_all();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Sprite effects reset error: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "SubsystemManager: Unknown error in sprite effects reset" << std::endl;
        return false;
    }
}

bool SubsystemManager::resetTextEditorSubsystem() {
    try {
        // Text editor reset - clear any temporary state if needed
        // editor_clear_temporary_state();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemManager: Text editor reset error: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "SubsystemManager: Unknown error in text editor reset" << std::endl;
        return false;
    }
}

// Helper methods

void SubsystemManager::logSubsystemState(SubsystemType type, SubsystemState state, const std::string& message) {
    const char* subsystem_name = getSubsystemName(type);
    const char* state_name = "";
    
    switch (state) {
        case SUBSYSTEM_STATE_UNINITIALIZED: state_name = "UNINITIALIZED"; break;
        case SUBSYSTEM_STATE_INITIALIZING: state_name = "INITIALIZING"; break;
        case SUBSYSTEM_STATE_RUNNING: state_name = "RUNNING"; break;
        case SUBSYSTEM_STATE_RESETTING: state_name = "RESETTING"; break;
        case SUBSYSTEM_STATE_SHUTTING_DOWN: state_name = "SHUTTING_DOWN"; break;
        case SUBSYSTEM_STATE_SHUTDOWN: state_name = "SHUTDOWN"; break;
        case SUBSYSTEM_STATE_ERROR: state_name = "ERROR"; break;
        case SUBSYSTEM_STATE_WARNING: state_name = "WARNING"; break;
    }
    
    if (!message.empty()) {
        std::cout << "SubsystemManager: [" << subsystem_name << "] " << state_name << " - " << message << std::endl;
    } else {
        std::cout << "SubsystemManager: [" << subsystem_name << "] " << state_name << std::endl;
    }
}

void SubsystemManager::setSubsystemState(SubsystemType type, SubsystemState state) {
    if (type >= 0 && type < SUBSYSTEM_TYPE_COUNT) {
        m_subsystem_states[type] = state;
    }
}

bool SubsystemManager::waitForSubsystemsToShutdown(int timeout_ms) {
    auto start_time = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(timeout_ms);
    
    while (std::chrono::steady_clock::now() - start_time < timeout) {
        bool all_shutdown = true;
        
        for (int i = 0; i < SUBSYSTEM_TYPE_COUNT; ++i) {
            SubsystemState state = m_subsystem_states[i].load();
            if (state != SUBSYSTEM_STATE_SHUTDOWN && state != SUBSYSTEM_STATE_UNINITIALIZED) {
                all_shutdown = false;
                break;
            }
        }
        
        if (all_shutdown || are_all_subsystems_shutdown()) {
            return true;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    return false;
}

void SubsystemManager::updatePerformanceMetrics() {
    auto now = std::chrono::steady_clock::now();
    
    if (m_initialization_start != std::chrono::steady_clock::time_point{}) {
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - m_initialization_start).count();
        m_performance_metrics["uptime_seconds"] = std::to_string(uptime);
    }
    
    m_performance_metrics["active_subsystem_count"] = std::to_string(get_active_subsystem_count());
    m_performance_metrics["system_healthy"] = isSystemHealthy() ? "true" : "false";
    m_performance_metrics["emergency_shutdown"] = isEmergencyShutdownInProgress() ? "true" : "false";
    
    // Add individual subsystem states
    for (int i = 0; i < SUBSYSTEM_TYPE_COUNT; ++i) {
        std::string key = std::string("subsystem_") + getSubsystemName(static_cast<SubsystemType>(i));
        m_performance_metrics[key] = std::to_string(static_cast<int>(m_subsystem_states[i].load()));
    }
}

// C API Implementation

extern "C" {

bool subsystem_manager_initialize_default(void) {
    SubsystemConfig config = subsystem_manager_create_default_config();
    return SubsystemManager::getInstance().initializeAll(config);
}

bool subsystem_manager_initialize(const SubsystemConfig* config) {
    if (!config) {
        return subsystem_manager_initialize_default();
    }
    return SubsystemManager::getInstance().initializeAll(*config);
}

extern "C" SuperTerminal::AssetsManager* subsystem_manager_get_assets_manager(void) {
    return SubsystemManager::getInstance().getAssetsManager();
}

bool subsystem_manager_shutdown(int timeout_ms) {
    return SubsystemManager::getInstance().shutdownAll(timeout_ms);
}

void subsystem_manager_force_shutdown(void) {
    SubsystemManager::getInstance().forceShutdownAll();
}

bool subsystem_manager_is_initialized(void) {
    return SubsystemManager::getInstance().isSystemHealthy();
}

bool subsystem_manager_is_emergency_shutdown(void) {
    return SubsystemManager::getInstance().isEmergencyShutdownInProgress();
}

SubsystemState subsystem_manager_get_state(SubsystemType type) {
    return SubsystemManager::getInstance().getSubsystemState(type);
}

bool subsystem_manager_is_healthy(void) {
    return SubsystemManager::getInstance().isSystemHealthy();
}

SubsystemConfig subsystem_manager_create_default_config(void) {
    SubsystemConfig config = {};
    
    // Graphics defaults
    config.window_width = 1024;
    config.window_height = 768;
    config.enable_metal_rendering = true;
    config.enable_skia_graphics = false; // Optional, requires Skia build
    
    // Audio defaults
    config.audio_sample_rate = 44100;
    config.audio_buffer_size = 512;
    config.audio_channels = 2;
    config.enable_midi = true;
    config.enable_synthesis = true;
    
    // Lua defaults
    config.enable_lua_jit = true;
    config.lua_memory_limit_mb = 64;
    config.lua_execution_timeout_ms = 5000;
    
    // Performance defaults
    config.target_fps = 60;
    config.enable_vsync = true;
    
    // Debug defaults
    config.enable_debug_logging = true;
    config.enable_performance_metrics = true;
    
    return config;
}

int subsystem_manager_get_active_count(void) {
    return get_active_subsystem_count();
}

bool subsystem_manager_reset(int timeout_ms) {
    return SubsystemManager::getInstance().resetAll(timeout_ms);
}

char* subsystem_manager_get_performance_metrics(void) {
    auto metrics = SubsystemManager::getInstance().getPerformanceMetrics();
    
    // Build formatted string
    std::string result = "SubsystemManager Performance Metrics:\n";
    for (const auto& pair : metrics) {
        result += "  " + pair.first + ": " + pair.second + "\n";
    }
    
    // Allocate C string
    char* c_result = static_cast<char*>(malloc(result.length() + 1));
    if (c_result) {
        strcpy(c_result, result.c_str());
    }
    
    return c_result;
}

} // extern "C"