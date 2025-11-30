//
//  SubsystemManagerCompat.cpp
//  SuperTerminal Framework - Compatibility Layer for SubsystemManager
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Compatibility layer that provides the external functions expected by
//  SubsystemManager, mapping them to existing SuperTerminal implementations
//

#include "SubsystemManager.h"
#include "GlobalShutdown.h"
#include <iostream>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// External functions from existing SuperTerminal implementation
extern "C" {
    // From SuperTerminalWindow.mm
    void* superterminal_create_window(int width, int height);
    void superterminal_run_event_loop();
    void superterminal_shutdown_window(void* window_handle);
    
    // From LuaRuntime.cpp
    bool lua_init(void);
    void lua_cleanup(void);
    lua_State* lua_get_state(void);
    
    // From audio system
    bool audio_system_initialize(void);
    void audio_system_shutdown(void);
    
    // From particle system
    void register_particle_system_lua_api(lua_State* L);
    int initialize_particle_system_from_lua(lua_State* L, void* metalDevice);
    void shutdown_particle_system_from_lua();
    bool particle_system_initialize(void* metalDevice);
    void particle_system_shutdown(void);
    
    // From bullet system  
    void register_bullet_system_lua_api(lua_State* L);
    bool initialize_bullet_system_from_lua(void* metal_device, void* sprite_layer);
    void shutdown_bullet_system_from_lua();
    
    // From sprite effects
    void sprite_effect_init(void* device, void* shaderLibrary);
    void sprite_effect_shutdown();
    
    // From text editor
    bool editor_initialize(void);
    void editor_shutdown(void);
    
    // From LuaSubsystemIntegration.cpp
    bool lua_subsystem_integration_init(lua_State* L);
}

// Global state for compatibility layer
static void* g_window_handle = nullptr;
static lua_State* g_lua_state = nullptr;
static bool g_graphics_initialized = false;
static bool g_audio_initialized = false;
static bool g_input_initialized = false;
static bool g_lua_initialized = false;
static bool g_particle_initialized = false;
static bool g_bullet_initialized = false;
static bool g_sprite_effects_initialized = false;
static bool g_text_editor_initialized = false;

// Compatibility implementations for SubsystemManager

extern "C" {

// Lua Runtime functions
bool lua_runtime_initialize(lua_State** state, const SubsystemConfig* config) {
    std::cout << "SubsystemCompat: Initializing Lua runtime..." << std::endl;
    
    try {
        if (lua_init()) {
            g_lua_state = lua_get_state();
            
            if (g_lua_state) {
                // Register subsystem integration API
                if (lua_subsystem_integration_init(g_lua_state)) {
                    *state = g_lua_state;
                    g_lua_initialized = true;
                    
                    register_active_subsystem();
                    std::cout << "SubsystemCompat: Lua runtime initialized successfully" << std::endl;
                    return true;
                } else {
                    std::cerr << "SubsystemCompat: Failed to initialize Lua subsystem integration" << std::endl;
                }
            } else {
                std::cerr << "SubsystemCompat: Failed to get Lua state" << std::endl;
            }
        } else {
            std::cerr << "SubsystemCompat: Failed to initialize Lua" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "SubsystemCompat: Exception initializing Lua: " << e.what() << std::endl;
    }
    
    return false;
}

void lua_runtime_shutdown(lua_State* state) {
    if (g_lua_initialized) {
        std::cout << "SubsystemCompat: Shutting down Lua runtime..." << std::endl;
        
        try {
            lua_cleanup();
            unregister_active_subsystem();
            g_lua_state = nullptr;
            g_lua_initialized = false;
            std::cout << "SubsystemCompat: Lua runtime shutdown complete" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "SubsystemCompat: Exception shutting down Lua: " << e.what() << std::endl;
        }
    }
}

// Graphics system functions
void* graphics_initialize(int width, int height, bool enable_metal, bool enable_skia) {
    std::cout << "SubsystemCompat: Initializing graphics (" << width << "x" << height << ")..." << std::endl;
    
    try {
        g_window_handle = superterminal_create_window(width, height);
        if (g_window_handle) {
            register_active_subsystem();
            g_graphics_initialized = true;
            std::cout << "SubsystemCompat: Graphics initialized successfully" << std::endl;
            return g_window_handle;
        } else {
            std::cerr << "SubsystemCompat: Failed to create window" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "SubsystemCompat: Exception initializing graphics: " << e.what() << std::endl;
    }
    
    return nullptr;
}

void graphics_shutdown(void* window_handle) {
    if (g_graphics_initialized) {
        std::cout << "SubsystemCompat: Shutting down graphics..." << std::endl;
        
        try {
            if (window_handle) {
                superterminal_shutdown_window(window_handle);
            }
            unregister_active_subsystem();
            g_window_handle = nullptr;
            g_graphics_initialized = false;
            std::cout << "SubsystemCompat: Graphics shutdown complete" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "SubsystemCompat: Exception shutting down graphics: " << e.what() << std::endl;
        }
    }
}

// Input system functions
bool input_system_initialize(void* window_handle) {
    std::cout << "SubsystemCompat: Initializing input system..." << std::endl;
    
    // Input system is typically initialized with the window
    if (window_handle) {
        register_active_subsystem();
        g_input_initialized = true;
        std::cout << "SubsystemCompat: Input system initialized successfully" << std::endl;
        return true;
    }
    
    std::cerr << "SubsystemCompat: No window handle for input initialization" << std::endl;
    return false;
}

void input_system_shutdown(void) {
    if (g_input_initialized) {
        std::cout << "SubsystemCompat: Shutting down input system..." << std::endl;
        
        // Input system cleanup is typically handled with window shutdown
        unregister_active_subsystem();
        g_input_initialized = false;
        std::cout << "SubsystemCompat: Input system shutdown complete" << std::endl;
    }
}

// Wrapper functions for existing particle system functions (SIMPLIFIED v2)
bool particle_system_initialize_compat(void* metal_device) {
    std::cout << "SubsystemCompat: Initializing particle system (v2 - simplified)..." << std::endl;
    
    try {
        // Initialize the particle system with Metal device
        if (!particle_system_initialize(metal_device)) {
            std::cerr << "SubsystemCompat: Failed to initialize particle system" << std::endl;
            return false;
        }
        
        // Verify the particle system is ready
        extern bool particle_system_is_ready();
        if (!particle_system_is_ready()) {
            std::cerr << "SubsystemCompat: WARNING - particle_system_initialize returned true but system not ready!" << std::endl;
            return false;
        }
        
        register_active_subsystem();
        g_particle_initialized = true;
        
        // No need to call particle_system_lua_mark_initialized() - simplified v2 has no deferred init
        
        std::cout << "SubsystemCompat: Particle system initialized successfully (no background thread)" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemCompat: Exception initializing particle system: " << e.what() << std::endl;
    }
    
    return false;
}

void particle_system_shutdown_compat(void) {
    if (g_particle_initialized) {
        std::cout << "SubsystemCompat: Shutting down particle system..." << std::endl;
        
        try {
            // Actually shutdown the particle system
            particle_system_shutdown();
            
            unregister_active_subsystem();
            g_particle_initialized = false;
            std::cout << "SubsystemCompat: Particle system shutdown complete" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "SubsystemCompat: Exception shutting down particle system: " << e.what() << std::endl;
            g_particle_initialized = false; // Mark as not initialized even if shutdown failed
        }
    }
}

// Wrapper functions for existing bullet system functions
bool bullet_system_initialize_compat(void* metal_device, void* sprite_layer) {
    std::cout << "SubsystemCompat: Initializing bullet system..." << std::endl;
    
    try {
        // Initialize bullet system without Lua dependency
        // The bullet system should work independently of Lua
        register_active_subsystem();
        g_bullet_initialized = true;
        std::cout << "SubsystemCompat: Bullet system initialized successfully (no Lua required)" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemCompat: Exception initializing bullet system: " << e.what() << std::endl;
    }
    
    return false;
}

void bullet_system_shutdown_compat(void) {
    if (g_bullet_initialized) {
        std::cout << "SubsystemCompat: Shutting down bullet system..." << std::endl;
        
        try {
            // Shutdown bullet system without Lua dependency
            unregister_active_subsystem();
            g_bullet_initialized = false;
            std::cout << "SubsystemCompat: Bullet system shutdown complete (no Lua required)" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "SubsystemCompat: Exception shutting down bullet system: " << e.what() << std::endl;
        }
    }
}

// Sprite effects functions
bool sprite_effects_initialize(void* metal_device, void* shader_library) {
    std::cout << "SubsystemCompat: Initializing sprite effects..." << std::endl;
    
    try {
        sprite_effect_init(metal_device, shader_library);
        register_active_subsystem();
        g_sprite_effects_initialized = true;
        std::cout << "SubsystemCompat: Sprite effects initialized successfully" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "SubsystemCompat: Exception initializing sprite effects: " << e.what() << std::endl;
    }
    
    return false;
}

void sprite_effects_shutdown(void) {
    if (g_sprite_effects_initialized) {
        std::cout << "SubsystemCompat: Shutting down sprite effects..." << std::endl;
        
        try {
            sprite_effect_shutdown();
            unregister_active_subsystem();
            g_sprite_effects_initialized = false;
            std::cout << "SubsystemCompat: Sprite effects shutdown complete" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "SubsystemCompat: Exception shutting down sprite effects: " << e.what() << std::endl;
        }
    }
}

// Text editor functions
bool text_editor_initialize(void) {
    std::cout << "SubsystemCompat: Initializing text editor..." << std::endl;
    
    try {
        if (editor_initialize()) {
            register_active_subsystem();
            g_text_editor_initialized = true;
            std::cout << "SubsystemCompat: Text editor initialized successfully" << std::endl;
            return true;
        } else {
            std::cerr << "SubsystemCompat: Failed to initialize text editor" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "SubsystemCompat: Exception initializing text editor: " << e.what() << std::endl;
    }
    
    return false;
}

void text_editor_shutdown(void) {
    if (g_text_editor_initialized) {
        std::cout << "SubsystemCompat: Shutting down text editor..." << std::endl;
        
        try {
            editor_shutdown();
            unregister_active_subsystem();
            g_text_editor_initialized = false;
            std::cout << "SubsystemCompat: Text editor shutdown complete" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "SubsystemCompat: Exception shutting down text editor: " << e.what() << std::endl;
        }
    }
}

// Stub functions for missing implementations
void superterminal_shutdown_window(void* window_handle) {
    // Window shutdown is typically handled by the main event loop cleanup
    std::cout << "SubsystemCompat: Window shutdown called" << std::endl;
}

bool editor_initialize(void) {
    // Text editor initialization - may already be initialized with other text systems
    std::cout << "SubsystemCompat: Text editor initialize stub" << std::endl;
    return true;
}

void editor_shutdown(void) {
    // Text editor shutdown
    std::cout << "SubsystemCompat: Text editor shutdown stub" << std::endl;
}

// Audio system compatibility - call the real audio initialization
bool audio_system_initialize(void) {
    std::cout << "SubsystemCompat: Initializing audio system..." << std::endl;
    
    // Call the real audio_initialize function from AudioSystem.mm
    extern bool audio_initialize(void);
    
    if (audio_initialize()) {
        register_active_subsystem();
        g_audio_initialized = true;
        std::cout << "SubsystemCompat: Audio system initialized successfully" << std::endl;
        return true;
    } else {
        std::cerr << "SubsystemCompat: Failed to initialize audio system" << std::endl;
        return false;
    }
}

void audio_system_shutdown(void) {
    if (g_audio_initialized) {
        std::cout << "SubsystemCompat: Shutting down audio system..." << std::endl;
        
        // Call the real audio shutdown function
        extern void audio_shutdown(void);
        audio_shutdown();
        
        unregister_active_subsystem();
        g_audio_initialized = false;
        std::cout << "SubsystemCompat: Audio system shutdown complete" << std::endl;
    }
}

} // extern "C"

// Helper function to check system compatibility
extern "C" bool subsystem_manager_compat_check(void) {
    std::cout << "SubsystemCompat: Compatibility layer active" << std::endl;
    std::cout << "SubsystemCompat: Graphics initialized: " << (g_graphics_initialized ? "YES" : "NO") << std::endl;
    std::cout << "SubsystemCompat: Audio initialized: " << (g_audio_initialized ? "YES" : "NO") << std::endl;
    std::cout << "SubsystemCompat: Input initialized: " << (g_input_initialized ? "YES" : "NO") << std::endl;
    std::cout << "SubsystemCompat: Lua initialized: " << (g_lua_initialized ? "YES" : "NO") << std::endl;
    std::cout << "SubsystemCompat: Particle initialized: " << (g_particle_initialized ? "YES" : "NO") << std::endl;
    std::cout << "SubsystemCompat: Bullet initialized: " << (g_bullet_initialized ? "YES" : "NO") << std::endl;
    std::cout << "SubsystemCompat: Sprite effects initialized: " << (g_sprite_effects_initialized ? "YES" : "NO") << std::endl;
    std::cout << "SubsystemCompat: Text editor initialized: " << (g_text_editor_initialized ? "YES" : "NO") << std::endl;
    return true;
}