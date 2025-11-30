//
//  ParticleSystemLua.cpp
//  SuperTerminal Framework
//
//  Lua API Bindings for Native Particle Explosion System
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "ParticleSystemC.h"
#include "CommandQueue.h"
#include <lua.hpp>
#include <iostream>

extern "C" {

// Forward declarations of C API functions
bool particle_system_initialize(void* metalDevice);
void particle_system_shutdown();
void particle_system_pause();
void particle_system_resume();
void particle_system_clear();
bool sprite_explode(uint16_t spriteId, uint16_t particleCount);
bool sprite_explode_advanced(uint16_t spriteId, uint16_t particleCount, 
                            float explosionForce, float gravity, float fadeTime);
bool sprite_explode_directional(uint16_t spriteId, uint16_t particleCount,
                               float forceX, float forceY);
void particle_system_set_time_scale(float scale);
void particle_system_set_world_bounds(float width, float height);
void particle_system_set_enabled(bool enabled);
uint32_t particle_system_get_active_count();
uint64_t particle_system_get_total_created();
float particle_system_get_physics_fps();
void particle_system_dump_stats();

// Explosion mode constants
#define BASIC_EXPLOSION 1
#define MASSIVE_BLAST 2
#define GENTLE_DISPERSAL 3
#define RIGHTWARD_BLAST 4
#define UPWARD_ERUPTION 5
#define RAPID_BURST 6

// Global flag to track particle system initialization state
static bool g_particle_system_initialized = false;

#pragma mark - Lua API Functions

/**
 * sprite_explode(sprite_id, particle_count)
 * 
 * Basic sprite explosion with default settings.
 * 
 * @param sprite_id: Integer - ID of the sprite to explode
 * @param particle_count: Integer - Number of particles to create (default: 32)
 * @return Boolean - true if explosion was triggered successfully
 */
static int l_sprite_explode(lua_State* L) {
    // Get parameters
    int sprite_id = (int)luaL_checkinteger(L, 1);
    int particle_count = 32; // Default
    
    if (lua_gettop(L) >= 2) {
        particle_count = (int)luaL_checkinteger(L, 2);
    }
    
    // Validate parameters
    if (sprite_id < 1 || sprite_id > 1024) {
        lua_pushboolean(L, 0);
        luaL_error(L, "sprite_explode: sprite_id must be between 1 and 1024");
        return 1;
    }
    
    if (particle_count < 1 || particle_count > 500) {
        lua_pushboolean(L, 0);
        luaL_error(L, "sprite_explode: particle_count must be between 1 and 500");
        return 1;
    }
    
    // Direct call - particle system handles its own thread safety
    sprite_explode((uint16_t)sprite_id, (uint16_t)particle_count);
    lua_pushboolean(L, 1); // Always return success for non-blocking operation
    
    return 1;
}

/**
 * sprite_explode_advanced(sprite_id, particle_count, explosion_force, gravity, fade_time)
 * 
 * Advanced sprite explosion with customizable parameters.
 * 
 * @param sprite_id: Integer - ID of the sprite to explode
 * @param particle_count: Integer - Number of particles to create
 * @param explosion_force: Number - Initial explosion velocity (default: 200.0)
 * @param gravity: Number - Gravity strength (default: 100.0)
 * @param fade_time: Number - Time in seconds for particles to fade (default: 2.0)
 * @return Boolean - true if explosion was triggered successfully
 */
static int l_sprite_explode_advanced(lua_State* L) {
    // Get required parameters
    int sprite_id = (int)luaL_checkinteger(L, 1);
    int particle_count = (int)luaL_checkinteger(L, 2);
    
    // Get optional parameters with defaults
    float explosion_force = 200.0f;
    float gravity = 100.0f;
    float fade_time = 2.0f;
    
    if (lua_gettop(L) >= 3) explosion_force = (float)luaL_checknumber(L, 3);
    if (lua_gettop(L) >= 4) gravity = (float)luaL_checknumber(L, 4);
    if (lua_gettop(L) >= 5) fade_time = (float)luaL_checknumber(L, 5);
    
    // Validate parameters
    if (sprite_id < 1 || sprite_id > 1024) {
        lua_pushboolean(L, 0);
        luaL_error(L, "sprite_explode_advanced: sprite_id must be between 1 and 1024");
        return 1;
    }
    
    if (particle_count < 1 || particle_count > 500) {
        lua_pushboolean(L, 0);
        luaL_error(L, "sprite_explode_advanced: particle_count must be between 1 and 500");
        return 1;
    }
    
    if (explosion_force < 0.0f || explosion_force > 2000.0f) {
        lua_pushboolean(L, 0);
        luaL_error(L, "sprite_explode_advanced: explosion_force must be between 0 and 2000");
        return 1;
    }
    
    if (fade_time < 0.1f || fade_time > 20.0f) {
        lua_pushboolean(L, 0);
        luaL_error(L, "sprite_explode_advanced: fade_time must be between 0.1 and 20.0");
        return 1;
    }
    
    // Direct call - particle system handles its own thread safety
    sprite_explode_advanced((uint16_t)sprite_id, (uint16_t)particle_count,
                           explosion_force, gravity, fade_time);
    lua_pushboolean(L, 1); // Always return success for non-blocking operation
    
    return 1;
}

/**
 * sprite_explode_directional(sprite_id, particle_count, force_x, force_y)
 * 
 * Directional sprite explosion - particles fly in a specific direction.
 * 
 * @param sprite_id: Integer - ID of the sprite to explode
 * @param particle_count: Integer - Number of particles to create
 * @param force_x: Number - Horizontal force component
 * @param force_y: Number - Vertical force component
 * @return Boolean - true if explosion was triggered successfully
 */
static int l_sprite_explode_directional(lua_State* L) {
    // Get parameters
    int sprite_id = (int)luaL_checkinteger(L, 1);
    int particle_count = (int)luaL_checkinteger(L, 2);
    float force_x = (float)luaL_checknumber(L, 3);
    float force_y = (float)luaL_checknumber(L, 4);
    
    // Validate parameters
    if (sprite_id < 1 || sprite_id > 1024) {
        lua_pushboolean(L, 0);
        luaL_error(L, "sprite_explode_directional: sprite_id must be between 1 and 1024");
        return 1;
    }
    
    if (particle_count < 1 || particle_count > 500) {
        lua_pushboolean(L, 0);
        luaL_error(L, "sprite_explode_directional: particle_count must be between 1 and 500");
        return 1;
    }
    
    // Direct call - particle system handles its own thread safety
    sprite_explode_directional((uint16_t)sprite_id, (uint16_t)particle_count,
                              force_x, force_y);
    lua_pushboolean(L, 1); // Always return success for non-blocking operation
    
    return 1;
}

/**
 * sprite_explode_mode(sprite_id, explosion_mode)
 * 
 * Simple sprite explosion using predefined modes.
 * 
 * @param sprite_id: Integer - ID of the sprite to explode
 * @param explosion_mode: Integer - Explosion mode (BASIC_EXPLOSION, MASSIVE_BLAST, etc.)
 * @return Boolean - true if explosion was triggered successfully
 */
static int l_sprite_explode_mode(lua_State* L) {
    // Get parameters
    int sprite_id = (int)luaL_checkinteger(L, 1);
    int explosion_mode = (int)luaL_checkinteger(L, 2);
    
    // Validate sprite ID
    if (sprite_id < 1 || sprite_id > 1024) {
        lua_pushboolean(L, 0);
        luaL_error(L, "sprite_explode_mode: sprite_id must be between 1 and 1024");
        return 1;
    }
    
    // Validate explosion mode
    if (explosion_mode < 1 || explosion_mode > 6) {
        lua_pushboolean(L, 0);
        luaL_error(L, "sprite_explode_mode: explosion_mode must be between 1 and 6");
        return 1;
    }
    
    bool success = false;
    
    // Apply the appropriate explosion mode
    switch (explosion_mode) {
        case BASIC_EXPLOSION:
            success = sprite_explode_advanced((uint16_t)sprite_id, 48, 200.0f, 100.0f, 2.0f);
            break;
        case MASSIVE_BLAST:
            success = sprite_explode_advanced((uint16_t)sprite_id, 128, 350.0f, 80.0f, 3.0f);
            break;
        case GENTLE_DISPERSAL:
            success = sprite_explode_advanced((uint16_t)sprite_id, 64, 120.0f, 40.0f, 4.0f);
            break;
        case RIGHTWARD_BLAST:
            success = sprite_explode_directional((uint16_t)sprite_id, 80, 180.0f, -30.0f);
            break;
        case UPWARD_ERUPTION:
            success = sprite_explode_directional((uint16_t)sprite_id, 96, 0.0f, -250.0f);
            break;
        case RAPID_BURST:
            success = sprite_explode_advanced((uint16_t)sprite_id, 32, 400.0f, 200.0f, 1.0f);
            break;
        default:
            lua_pushboolean(L, 0);
            luaL_error(L, "sprite_explode_mode: invalid explosion_mode");
            return 1;
    }
    
    lua_pushboolean(L, success ? 1 : 0);
    return 1;
}

// Stub functions for deferred API (when particle system not ready)
// These now call the real functions if system is initialized
// Direct calls - no CommandQueue to avoid deadlock during init
static int l_particle_system_clear_stub(lua_State* L) {
    if (g_particle_system_initialized) {
        particle_system_clear();
    }
    return 0;
}

static int l_particle_system_set_time_scale_stub(lua_State* L) {
    if (g_particle_system_initialized) {
        float scale = (float)luaL_checknumber(L, 1);
        if (scale < 0.1f) scale = 0.1f;
        if (scale > 5.0f) scale = 5.0f;
        particle_system_set_time_scale(scale);
    }
    return 0;
}

static int l_particle_system_set_world_bounds_stub(lua_State* L) {
    if (g_particle_system_initialized) {
        float width = (float)luaL_checknumber(L, 1);
        float height = (float)luaL_checknumber(L, 2);
        particle_system_set_world_bounds(width, height);
    }
    return 0;
}

static int l_particle_system_set_enabled_stub(lua_State* L) {
    if (g_particle_system_initialized) {
        bool enabled = lua_toboolean(L, 1);
        particle_system_set_enabled(enabled);
    }
    return 0;
}

static int l_particle_system_get_active_count_stub(lua_State* L) {
    if (g_particle_system_initialized) {
        uint32_t count = particle_system_get_active_count();
        lua_pushinteger(L, count);
    } else {
        lua_pushinteger(L, 0);
    }
    return 1;
}

static int l_particle_system_get_total_created_stub(lua_State* L) {
    if (g_particle_system_initialized) {
        uint64_t total = particle_system_get_total_created();
        lua_pushinteger(L, (lua_Integer)total);
    } else {
        lua_pushinteger(L, 0);
    }
    return 1;
}

static int l_particle_system_get_physics_fps_stub(lua_State* L) {
    if (g_particle_system_initialized) {
        float fps = particle_system_get_physics_fps();
        lua_pushnumber(L, (lua_Number)fps);
    } else {
        lua_pushnumber(L, 0.0);
    }
    return 1;
}

static int l_particle_system_dump_stats_stub(lua_State* L) {
    if (g_particle_system_initialized) {
        particle_system_dump_stats();
    }
    return 0;
}

static int l_particle_system_info_stub(lua_State* L) {
    lua_newtable(L);
    if (g_particle_system_initialized) {
        lua_pushinteger(L, (lua_Integer)particle_system_get_active_count());
        lua_setfield(L, -2, "active_count");
        lua_pushinteger(L, (lua_Integer)particle_system_get_total_created());
        lua_setfield(L, -2, "total_created");
        lua_pushnumber(L, (lua_Number)particle_system_get_physics_fps());
        lua_setfield(L, -2, "physics_fps");
    }
    return 1;
}

static int l_particle_system_pause_stub(lua_State* L) {
    if (g_particle_system_initialized) {
        particle_system_pause();
    }
    return 0;
}

static int l_particle_system_resume_stub(lua_State* L) {
    if (g_particle_system_initialized) {
        particle_system_resume();
    }
    return 0;
}

static int l_sprite_explode_stub(lua_State* L) {
    if (g_particle_system_initialized) {
        int sprite_id = (int)luaL_checkinteger(L, 1);
        int particle_count = (int)luaL_checkinteger(L, 2);
        sprite_explode((uint16_t)sprite_id, (uint16_t)particle_count);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int l_sprite_explode_advanced_stub(lua_State* L) {
    if (g_particle_system_initialized) {
        int sprite_id = (int)luaL_checkinteger(L, 1);
        int particle_count = (int)luaL_checkinteger(L, 2);
        float explosion_force = (float)luaL_checknumber(L, 3);
        float gravity = (float)luaL_checknumber(L, 4);
        float fade_time = (float)luaL_checknumber(L, 5);
        sprite_explode_advanced((uint16_t)sprite_id, (uint16_t)particle_count,
                               explosion_force, gravity, fade_time);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int l_sprite_explode_directional_stub(lua_State* L) {
    if (g_particle_system_initialized) {
        int sprite_id = (int)luaL_checkinteger(L, 1);
        int particle_count = (int)luaL_checkinteger(L, 2);
        float force_x = (float)luaL_checknumber(L, 3);
        float force_y = (float)luaL_checknumber(L, 4);
        sprite_explode_directional((uint16_t)sprite_id, (uint16_t)particle_count,
                                  force_x, force_y);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int l_sprite_explode_mode_stub(lua_State* L) {
    if (g_particle_system_initialized) {
        int sprite_id = (int)luaL_checkinteger(L, 1);
        int mode = (int)luaL_checkinteger(L, 2);
        // Call the appropriate explosion function based on mode
        switch(mode) {
            case 1: sprite_explode((uint16_t)sprite_id, 50); break;
            case 2: sprite_explode((uint16_t)sprite_id, 200); break;
            case 3: sprite_explode((uint16_t)sprite_id, 30); break;
            case 4: sprite_explode_directional((uint16_t)sprite_id, 100, 300.0f, 0.0f); break;
            case 5: sprite_explode_directional((uint16_t)sprite_id, 100, 0.0f, -300.0f); break;
            case 6: sprite_explode((uint16_t)sprite_id, 150); break;
            default: sprite_explode((uint16_t)sprite_id, 50); break;
        }
    }
    lua_pushboolean(L, 1);
    return 1;
}

/**
 * particle_system_pause()
 * 
 * Pause the particle physics simulation.
 */
static int l_particle_system_pause(lua_State* L) {
    particle_system_pause();
    return 0;
}

/**
 * particle_system_resume()
 * 
 * Resume the particle physics simulation.
 */
static int l_particle_system_resume(lua_State* L) {
    particle_system_resume();
    return 0;
}

/**
 * particle_system_clear()
 * 
 * Clear all active particles from the system.
 */
static int l_particle_system_clear(lua_State* L) {
    particle_system_clear();
    return 0;
}

/**
 * particle_system_set_time_scale(scale)
 * 
 * Set the time scale for particle physics (1.0 = normal speed).
 * 
 * @param scale: Number - Time scale multiplier (0.1 to 5.0)
 */
static int l_particle_system_set_time_scale(lua_State* L) {
    float scale = (float)luaL_checknumber(L, 1);
    
    // Clamp to reasonable range
    if (scale < 0.1f) scale = 0.1f;
    if (scale > 5.0f) scale = 5.0f;
    
    particle_system_set_time_scale(scale);
    return 0;
}

/**
 * particle_system_set_world_bounds(width, height)
 * 
 * Set the world bounds for particle physics.
 * 
 * @param width: Number - World width
 * @param height: Number - World height
 */
static int l_particle_system_set_world_bounds(lua_State* L) {
    float width = (float)luaL_checknumber(L, 1);
    float height = (float)luaL_checknumber(L, 2);
    
    particle_system_set_world_bounds(width, height);
    return 0;
}

/**
 * particle_system_set_enabled(enabled)
 * 
 * Enable or disable the particle system.
 * 
 * @param enabled: Boolean - true to enable, false to disable
 */
static int l_particle_system_set_enabled(lua_State* L) {
    bool enabled = lua_toboolean(L, 1);
    particle_system_set_enabled(enabled);
    return 0;
}

/**
 * particle_system_get_active_count()
 * 
 * Get the number of currently active particles.
 * 
 * @return Integer - Number of active particles
 */
static int l_particle_system_get_active_count(lua_State* L) {
    // Use direct call instead of executeCommand to avoid deadlock during initialization
    uint32_t count = particle_system_get_active_count();
    lua_pushinteger(L, count);
    return 1;
}

/**
 * particle_system_get_total_created()
 * 
 * Get the total number of particles created since system start.
 * 
 * @return Integer - Total particles created
 */
static int l_particle_system_get_total_created(lua_State* L) {
    uint64_t total = particle_system_get_total_created();
    lua_pushinteger(L, (lua_Integer)total);
    return 1;
}

/**
 * particle_system_get_physics_fps()
 * 
 * Get the current physics simulation frame rate.
 * 
 * @return Number - Physics FPS
 */
static int l_particle_system_get_physics_fps(lua_State* L) {
    float fps = particle_system_get_physics_fps();
    lua_pushnumber(L, (lua_Number)fps);
    return 1;
}

/**
 * particle_system_dump_stats()
 * 
 * Print detailed particle system statistics to console.
 */
static int l_particle_system_dump_stats(lua_State* L) {
    particle_system_dump_stats();
    return 0;
}

/**
 * particle_system_info()
 * 
 * Get particle system information as a table.
 * 
 * @return Table - System info with keys: active_count, total_created, physics_fps, enabled
 */
static int l_particle_system_info(lua_State* L) {
    lua_newtable(L);
    
    // Active count
    lua_pushinteger(L, (lua_Integer)particle_system_get_active_count());
    lua_setfield(L, -2, "active_count");
    
    // Total created
    lua_pushinteger(L, (lua_Integer)particle_system_get_total_created());
    lua_setfield(L, -2, "total_created");
    
    // Physics FPS
    lua_pushnumber(L, (lua_Number)particle_system_get_physics_fps());
    lua_setfield(L, -2, "physics_fps");
    
    // System enabled (we'll assume it's enabled if we can get stats)
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "enabled");
    
    return 1;
}

#pragma mark - Registration

/**
 * Register all particle system functions with Lua
 */
extern "C" void register_particle_system_lua_api(lua_State* L) {
    if (!L) {
        std::cout << "ParticleSystemLua: ERROR - NULL Lua state passed to register_particle_system_lua_api" << std::endl;
        return;
    }
    
    // Don't register if system is already initialized - full API will be registered instead
    if (g_particle_system_initialized) {
        std::cout << "ParticleSystemLua: Particle system already initialized, skipping deferred API" << std::endl;
        return;
    }
    
    std::cout << "ParticleSystemLua: Starting deferred API registration..." << std::endl;
    
    // Create particle_system table with stub functions initially
    lua_newtable(L);
    
    // Register safe stub methods that won't hang
    lua_pushcfunction(L, l_particle_system_clear_stub);
    lua_setfield(L, -2, "clear");
    
    lua_pushcfunction(L, l_particle_system_set_time_scale_stub);
    lua_setfield(L, -2, "set_time_scale");
    
    lua_pushcfunction(L, l_particle_system_set_world_bounds_stub);
    lua_setfield(L, -2, "set_world_bounds");
    
    lua_pushcfunction(L, l_particle_system_set_enabled_stub);
    lua_setfield(L, -2, "set_enabled");
    
    lua_pushcfunction(L, l_particle_system_get_active_count_stub);
    lua_setfield(L, -2, "get_active_count");
    
    lua_pushcfunction(L, l_particle_system_get_total_created_stub);
    lua_setfield(L, -2, "get_total_created");
    
    lua_pushcfunction(L, l_particle_system_get_physics_fps_stub);
    lua_setfield(L, -2, "get_physics_fps");
    
    lua_pushcfunction(L, l_particle_system_dump_stats_stub);
    lua_setfield(L, -2, "dump_stats");
    
    lua_pushcfunction(L, l_particle_system_info_stub);
    lua_setfield(L, -2, "info");
    
    lua_pushcfunction(L, l_particle_system_pause_stub);
    lua_setfield(L, -2, "pause");
    
    lua_pushcfunction(L, l_particle_system_resume_stub);
    lua_setfield(L, -2, "resume");
    
    // Set the particle_system global
    lua_setglobal(L, "particle_system");
    
    // Register safe explosion function stubs
    lua_pushcfunction(L, l_sprite_explode_stub);
    lua_setglobal(L, "sprite_explode");
    
    lua_pushcfunction(L, l_sprite_explode_advanced_stub);
    lua_setglobal(L, "sprite_explode_advanced");
    
    lua_pushcfunction(L, l_sprite_explode_directional_stub);
    lua_setglobal(L, "sprite_explode_directional");
    
    lua_pushcfunction(L, l_sprite_explode_mode_stub);
    lua_setglobal(L, "sprite_explode_mode");
    
    // Register explosion mode constants
    lua_pushinteger(L, 1);
    lua_setglobal(L, "BASIC_EXPLOSION");
    
    lua_pushinteger(L, 2);
    lua_setglobal(L, "MASSIVE_BLAST");
    
    lua_pushinteger(L, 3);
    lua_setglobal(L, "GENTLE_DISPERSAL");
    
    lua_pushinteger(L, 4);
    lua_setglobal(L, "RIGHTWARD_BLAST");
    
    lua_pushinteger(L, 5);
    lua_setglobal(L, "UPWARD_ERUPTION");
    
    lua_pushinteger(L, 6);
    lua_setglobal(L, "RAPID_BURST");
    
    std::cout << "ParticleSystemLua: Registered deferred particle system API successfully" << std::endl;
}

/**
 * register_particle_system_lua_api_full(lua_State* L)
 * 
 * Register the full particle system API when the system is ready.
 * This should be called after the particle system is initialized.
 */
extern "C" void register_particle_system_lua_api_full(lua_State* L) {
    if (!L) {
        std::cout << "ParticleSystemLua: ERROR - NULL Lua state passed to register_particle_system_lua_api_full" << std::endl;
        return;
    }
    
    if (!g_particle_system_initialized) {
        std::cout << "ParticleSystemLua: WARNING - Attempting to register full API but system not marked initialized" << std::endl;
        return;
    }
    
    std::cout << "ParticleSystemLua: Registering full API - particle system is ready" << std::endl;
    
    // Create particle_system table with real functions
    lua_newtable(L);
    
    // Register actual particle_system table methods
    lua_pushcfunction(L, l_particle_system_clear);
    lua_setfield(L, -2, "clear");
    
    lua_pushcfunction(L, l_particle_system_set_time_scale);
    lua_setfield(L, -2, "set_time_scale");
    
    lua_pushcfunction(L, l_particle_system_set_world_bounds);
    lua_setfield(L, -2, "set_world_bounds");
    
    lua_pushcfunction(L, l_particle_system_set_enabled);
    lua_setfield(L, -2, "set_enabled");
    
    lua_pushcfunction(L, l_particle_system_get_active_count);
    lua_setfield(L, -2, "get_active_count");
    
    lua_pushcfunction(L, l_particle_system_get_total_created);
    lua_setfield(L, -2, "get_total_created");
    
    lua_pushcfunction(L, l_particle_system_get_physics_fps);
    lua_setfield(L, -2, "get_physics_fps");
    
    lua_pushcfunction(L, l_particle_system_dump_stats);
    lua_setfield(L, -2, "dump_stats");
    
    lua_pushcfunction(L, l_particle_system_info);
    lua_setfield(L, -2, "info");
    
    lua_pushcfunction(L, l_particle_system_pause);
    lua_setfield(L, -2, "pause");
    
    lua_pushcfunction(L, l_particle_system_resume);
    lua_setfield(L, -2, "resume");
    
    // Set the particle_system global
    lua_setglobal(L, "particle_system");
    
    // Register real explosion functions
    lua_pushcfunction(L, l_sprite_explode);
    lua_setglobal(L, "sprite_explode");
    
    lua_pushcfunction(L, l_sprite_explode_advanced);
    lua_setglobal(L, "sprite_explode_advanced");
    
    lua_pushcfunction(L, l_sprite_explode_directional);
    lua_setglobal(L, "sprite_explode_directional");
    
    lua_pushcfunction(L, l_sprite_explode_mode);
    lua_setglobal(L, "sprite_explode_mode");
    
    std::cout << "ParticleSystemLua: Full particle system API registered successfully" << std::endl;
}

/**
 * Set particle system initialization flag (called from particle system)
 */
extern "C" void particle_system_lua_mark_initialized() {
    g_particle_system_initialized = true;
    std::cout << "ParticleSystemLua: Particle system marked as initialized" << std::endl;
}

/**
 * register_particle_system_lua_api_when_ready(lua_State* L)
 * 
 * Check if particle system is ready and register full API if it is.
 * This should be called after Lua runtime is initialized.
 */
extern "C" bool register_particle_system_lua_api_when_ready(lua_State* L) {
    if (!L) {
        std::cout << "ParticleSystemLua: ERROR - NULL Lua state passed to register_particle_system_lua_api_when_ready" << std::endl;
        return false;
    }
    
    // Don't register full API - the stub functions already call the real functions
    // when initialized, and registering the full API causes hangs
    if (g_particle_system_initialized) {
        std::cout << "ParticleSystemLua: Particle system is ready, stub API will forward to real functions" << std::endl;
        return false; // Keep using stubs - they work fine
    } else {
        std::cout << "ParticleSystemLua: Particle system not ready yet, keeping deferred API" << std::endl;
        return false;
    }
}

/**
 * Initialize particle system from Lua (called automatically when system starts)
 */
int initialize_particle_system_from_lua(lua_State* L, void* metalDevice) {
    bool success = particle_system_initialize(metalDevice);
    
    if (success) {
        std::cout << "ParticleSystemLua: Native particle system initialized successfully" << std::endl;
        
        // Set default world bounds (can be overridden by scripts)
        particle_system_set_world_bounds(1024.0f, 768.0f);
        
        // Register API functions
        register_particle_system_lua_api(L);
    } else {
        std::cout << "ParticleSystemLua: Failed to initialize native particle system" << std::endl;
    }
    
    return success ? 0 : 1;
}

/**
 * Shutdown particle system from Lua (called when system shuts down)
 */
void shutdown_particle_system_from_lua() {
    particle_system_shutdown();
    std::cout << "ParticleSystemLua: Native particle system shut down" << std::endl;
}

} // extern "C"

#pragma mark - Usage Examples in Comments

/*
Example Lua usage:

-- Basic explosion
sprite_load(1, "assets/enemy.png")
sprite_show(1, 400, 300)
sprite_explode(1, 50)  -- Explode with 50 particles

-- Advanced explosion
sprite_explode_advanced(1, 100, 300.0, 150.0, 3.0)  -- 100 particles, force 300, gravity 150, fade 3 seconds

-- Directional explosion (hit from the left)
sprite_explode_directional(1, 75, 200.0, 50.0)  -- 75 particles flying right and up

-- System control
particle_system.pause()         -- Pause physics
particle_system.resume()        -- Resume physics
particle_system.clear()         -- Clear all particles
particle_system.set_time_scale(0.5)  -- Slow motion
particle_system.set_enabled(false)   -- Disable system

-- Get information
local info = particle_system.info()
console("Active particles: " .. info.active_count)
console("Physics FPS: " .. info.physics_fps)

-- Performance monitoring
particle_system.dump_stats()    -- Print detailed stats to console
*/