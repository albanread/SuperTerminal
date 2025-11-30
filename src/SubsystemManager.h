//
//  SubsystemManager.h
//  SuperTerminal Framework - Centralized Subsystem Management
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Centralized management of all SuperTerminal subsystems including
//  initialization, shutdown coordination, and lifecycle management
//

#ifndef SUBSYSTEM_MANAGER_H
#define SUBSYSTEM_MANAGER_H

#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <functional>
#include <map>
#include <chrono>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations of subsystem classes
namespace SuperTerminal {
    // Audio classes are accessed via C API to avoid Objective-C header conflicts
    class AssetsManager;
}

// Forward declarations of C subsystems
struct lua_State;
struct ParticleSystem;
struct BulletSystem;
struct SpriteEffectSystem;

/**
 * Subsystem lifecycle states
 */
typedef enum {
    SUBSYSTEM_STATE_UNINITIALIZED = 0,
    SUBSYSTEM_STATE_INITIALIZING = 1,
    SUBSYSTEM_STATE_RUNNING = 2,
    SUBSYSTEM_STATE_RESETTING = 3,
    SUBSYSTEM_STATE_SHUTTING_DOWN = 4,
    SUBSYSTEM_STATE_SHUTDOWN = 5,
    SUBSYSTEM_STATE_ERROR = 6,
    SUBSYSTEM_STATE_WARNING = 7
} SubsystemState;

/**
 * Subsystem types for identification and dependency management
 */
typedef enum {
    SUBSYSTEM_TYPE_GRAPHICS = 0,
    SUBSYSTEM_TYPE_AUDIO = 1,
    SUBSYSTEM_TYPE_INPUT = 2,
    SUBSYSTEM_TYPE_LUA_RUNTIME = 3,
    SUBSYSTEM_TYPE_PARTICLE_SYSTEM = 4,
    SUBSYSTEM_TYPE_BULLET_SYSTEM = 5,
    SUBSYSTEM_TYPE_SPRITE_EFFECTS = 6,
    SUBSYSTEM_TYPE_TEXT_EDITOR = 7,
    SUBSYSTEM_TYPE_MIDI = 8,
    SUBSYSTEM_TYPE_SYNTH = 9,
    SUBSYSTEM_TYPE_MUSIC_PLAYER = 10,
    SUBSYSTEM_TYPE_ASSETS = 11,
    SUBSYSTEM_TYPE_COUNT
} SubsystemType;

/**
 * Subsystem initialization configuration
 */
typedef struct {
    // Graphics configuration
    int window_width;
    int window_height;
    bool enable_metal_rendering;
    bool enable_skia_graphics;
    
    // Audio configuration
    int audio_sample_rate;
    int audio_buffer_size;
    int audio_channels;
    bool enable_midi;
    bool enable_synthesis;
    
    // Lua configuration
    bool enable_lua_jit;
    int lua_memory_limit_mb;
    int lua_execution_timeout_ms;
    
    // Performance configuration
    int target_fps;
    bool enable_vsync;
    
    // Debug configuration
    bool enable_debug_logging;
    bool enable_performance_metrics;
} SubsystemConfig;

/**
 * Subsystem manager class - handles initialization and shutdown of all SuperTerminal subsystems
 */
#ifdef __cplusplus

class SubsystemManager {
public:
    /**
     * Get the global subsystem manager instance (singleton)
     */
    static SubsystemManager& getInstance();
    
    /**
     * Initialize all subsystems with the given configuration
     * @param config Subsystem configuration
     * @return true if all subsystems initialized successfully
     */
    bool initializeAll(const SubsystemConfig& config);
    
    /**
     * Reset all subsystems - stop activities and clear memory but keep initialized
     * @param timeout_ms Maximum time to wait for reset completion
     * @return true if all subsystems reset cleanly
     */
    bool resetAll(int timeout_ms = 3000);
    
    /**
     * Shutdown all subsystems gracefully
     * @param timeout_ms Maximum time to wait for clean shutdown
     * @return true if all subsystems shut down cleanly
     */
    bool shutdownAll(int timeout_ms = 5000);
    
    /**
     * Force shutdown all subsystems immediately (emergency only)
     */
    void forceShutdownAll();
    
    /**
     * Get the current state of a specific subsystem
     * @param type Subsystem type
     * @return Current state of the subsystem
     */
    SubsystemState getSubsystemState(SubsystemType type) const;
    
    /**
     * Get the current overall system state
     * @return true if all subsystems are running normally
     */
    bool isSystemHealthy() const;
    
    /**
     * Get performance metrics for all subsystems
     * @return Map of subsystem names to performance data
     */
    std::map<std::string, std::string> getPerformanceMetrics() const;
    
    /**
     * Register a custom subsystem shutdown callback
     * @param callback Function to call during shutdown
     * @param priority Priority order (lower numbers shutdown first)
     */
    void registerShutdownCallback(std::function<void()> callback, int priority = 100);
    
    /**
     * Check if emergency shutdown is in progress
     * @return true if emergency shutdown was requested
     */
    bool isEmergencyShutdownInProgress() const;
    
    // Subsystem-specific accessors (may return nullptr if not initialized)
    lua_State* getLuaState() const;
    SuperTerminal::AssetsManager* getAssetsManager() const;
    
private:
    // Private constructor for singleton
    SubsystemManager();
    ~SubsystemManager();
    
    // Disable copy constructor and assignment
    SubsystemManager(const SubsystemManager&) = delete;
    SubsystemManager& operator=(const SubsystemManager&) = delete;
    
    // Individual subsystem initialization methods
    bool initializeGraphicsSubsystem(const SubsystemConfig& config);
    bool initializeAudioSubsystem(const SubsystemConfig& config);
    bool initializeInputSubsystem(const SubsystemConfig& config);
    bool initializeLuaRuntimeSubsystem(const SubsystemConfig& config);
    bool initializeParticleSubsystem(const SubsystemConfig& config);
    bool initializeBulletSubsystem(const SubsystemConfig& config);
    bool initializeSpriteEffectsSubsystem(const SubsystemConfig& config);
    bool initializeTextEditorSubsystem(const SubsystemConfig& config);
    bool initializeAssetsSubsystem(const SubsystemConfig& config);
    
    // Individual subsystem reset methods
    bool resetGraphicsSubsystem();
    bool resetAudioSubsystem();
    bool resetInputSubsystem();
    bool resetLuaRuntimeSubsystem();
    bool resetParticleSubsystem();
    bool resetBulletSubsystem();
    bool resetSpriteEffectsSubsystem();
    bool resetTextEditorSubsystem();
    bool resetAssetsSubsystem();
    
    // Individual subsystem shutdown methods
    void shutdownGraphicsSubsystem();
    void shutdownAudioSubsystem();
    void shutdownInputSubsystem();
    void shutdownLuaRuntimeSubsystem();
    void shutdownParticleSubsystem();
    void shutdownBulletSubsystem();
    void shutdownSpriteEffectsSubsystem();
    void shutdownTextEditorSubsystem();
    void shutdownAssetsSubsystem();
    
    // Helper methods
    void logSubsystemState(SubsystemType type, SubsystemState state, const std::string& message = "");
    void setSubsystemState(SubsystemType type, SubsystemState state);
    bool waitForSubsystemsToShutdown(int timeout_ms);
    void updatePerformanceMetrics();
    
    // Member variables
    mutable std::mutex m_mutex;
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_shutting_down{false};
    std::atomic<bool> m_emergency_shutdown{false};
    
    // Subsystem states
    std::array<std::atomic<SubsystemState>, SUBSYSTEM_TYPE_COUNT> m_subsystem_states;
    
    // Subsystem instances
    lua_State* m_lua_state;
    void* m_metal_device;
    void* m_window_handle;
    SuperTerminal::AssetsManager* m_assets_manager;
    
    // Configuration
    SubsystemConfig m_config;
    
    // Shutdown callbacks
    std::vector<std::pair<std::function<void()>, int>> m_shutdown_callbacks;
    
    // Performance tracking
    std::chrono::steady_clock::time_point m_initialization_start;
    std::map<std::string, std::string> m_performance_metrics;
};

#endif // __cplusplus

// C API for subsystem management
extern "C" {

/**
 * Initialize all SuperTerminal subsystems with default configuration
 * @return true if initialization successful
 */
bool subsystem_manager_initialize_default(void);

/**
 * Initialize all SuperTerminal subsystems with custom configuration
 * @param config Pointer to configuration structure
 * @return true if initialization successful
 */
bool subsystem_manager_initialize(const SubsystemConfig* config);

/**
 * Reset all SuperTerminal subsystems - stop activities and clear memory
 * @param timeout_ms Maximum time to wait for reset completion
 * @return true if reset completed successfully
 */
bool subsystem_manager_reset(int timeout_ms);

/**
 * Shutdown all SuperTerminal subsystems
 * @param timeout_ms Maximum time to wait for clean shutdown
 * @return true if shutdown completed successfully
 */
bool subsystem_manager_shutdown(int timeout_ms);

/**
 * Force emergency shutdown of all subsystems
 */
void subsystem_manager_force_shutdown(void);

/**
 * Check if the subsystem manager is initialized
 * @return true if initialized and running
 */
bool subsystem_manager_is_initialized(void);

/**
 * Check if emergency shutdown is in progress
 * @return true if emergency shutdown was requested
 */
bool subsystem_manager_is_emergency_shutdown(void);

/**
 * Get the current state of a specific subsystem
 * @param type Subsystem type
 * @return Current state of the subsystem
 */
SubsystemState subsystem_manager_get_state(SubsystemType type);

/**
 * Check if all subsystems are healthy
 * @return true if all subsystems are running normally
 */
bool subsystem_manager_is_healthy(void);

/**
 * Create a default subsystem configuration
 * @return Default configuration structure
 */
SubsystemConfig subsystem_manager_create_default_config(void);

/**
 * Get the number of active subsystems
 * @return Number of active subsystems registered with shutdown system
 */
int subsystem_manager_get_active_count(void);

/**
 * Get performance metrics as a formatted string
 * @return C string containing performance data (caller must free)
 */
char* subsystem_manager_get_performance_metrics(void);

}

#ifdef __cplusplus
}
#endif

#endif // SUBSYSTEM_MANAGER_H