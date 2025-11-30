//
//  ParticleSystemLua.mm
//  SuperTerminal Framework
//
//  Simplified Lua API Bindings for Particle System (v2)
//  Direct bindings - no stubs, no deferred initialization
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include <lua.hpp>
#include <iostream>

// Forward declarations of C API functions (defined in ParticleSystem.mm)
extern "C" {
    bool sprite_explode(uint16_t spriteId, uint16_t particleCount);
    bool sprite_explode_advanced(uint16_t spriteId, uint16_t particleCount,
                                float explosionForce, float gravity, float fadeTime);
    bool sprite_explode_directional(uint16_t spriteId, uint16_t particleCount,
                                   float forceX, float forceY);
    void particle_system_pause();
    void particle_system_resume();
    void particle_system_clear();
    void particle_system_set_time_scale(float scale);
    void particle_system_set_world_bounds(float width, float height);
    void particle_system_set_enabled(bool enabled);
    uint32_t particle_system_get_active_count();
    uint64_t particle_system_get_total_created();
    void particle_system_dump_stats();
    bool particle_system_is_ready();
}

#pragma mark - Lua Binding Functions (Direct Calls)

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
    int sprite_id = (int)luaL_checkinteger(L, 1);
    int particle_count = 32; // Default

    if (lua_gettop(L) >= 2) {
        particle_count = (int)luaL_checkinteger(L, 2);
    }

    // Validate parameters
    if (sprite_id < 1 || sprite_id > 1024) {
        return luaL_error(L, "sprite_explode: sprite_id must be between 1 and 1024");
    }

    if (particle_count < 1 || particle_count > 500) {
        return luaL_error(L, "sprite_explode: particle_count must be between 1 and 500");
    }

    // Direct call to C API (no stub, no flag check, no CommandQueue)
    bool result = sprite_explode((uint16_t)sprite_id, (uint16_t)particle_count);
    lua_pushboolean(L, result ? 1 : 0);

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
        return luaL_error(L, "sprite_explode_advanced: sprite_id must be between 1 and 1024");
    }

    if (particle_count < 1 || particle_count > 500) {
        return luaL_error(L, "sprite_explode_advanced: particle_count must be between 1 and 500");
    }

    if (explosion_force < 0.0f || explosion_force > 2000.0f) {
        return luaL_error(L, "sprite_explode_advanced: explosion_force must be between 0 and 2000");
    }

    if (fade_time < 0.1f || fade_time > 20.0f) {
        return luaL_error(L, "sprite_explode_advanced: fade_time must be between 0.1 and 20.0");
    }

    // Direct call
    bool result = sprite_explode_advanced((uint16_t)sprite_id, (uint16_t)particle_count,
                                         explosion_force, gravity, fade_time);
    lua_pushboolean(L, result ? 1 : 0);

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
    int sprite_id = (int)luaL_checkinteger(L, 1);
    int particle_count = (int)luaL_checkinteger(L, 2);
    float force_x = (float)luaL_checknumber(L, 3);
    float force_y = (float)luaL_checknumber(L, 4);

    // Validate parameters
    if (sprite_id < 1 || sprite_id > 1024) {
        return luaL_error(L, "sprite_explode_directional: sprite_id must be between 1 and 1024");
    }

    if (particle_count < 1 || particle_count > 500) {
        return luaL_error(L, "sprite_explode_directional: particle_count must be between 1 and 500");
    }

    // Direct call
    bool result = sprite_explode_directional((uint16_t)sprite_id, (uint16_t)particle_count,
                                            force_x, force_y);
    lua_pushboolean(L, result ? 1 : 0);

    return 1;
}

/**
 * sprite_explode_mode(sprite_id, explosion_mode)
 *
 * Simple sprite explosion using predefined modes.
 *
 * @param sprite_id: Integer - ID of the sprite to explode
 * @param explosion_mode: Integer - Explosion mode (1-6)
 * @return Boolean - true if explosion was triggered successfully
 */
static int l_sprite_explode_mode(lua_State* L) {
    int sprite_id = (int)luaL_checkinteger(L, 1);
    int explosion_mode = (int)luaL_checkinteger(L, 2);

    // Validate sprite ID
    if (sprite_id < 1 || sprite_id > 1024) {
        return luaL_error(L, "sprite_explode_mode: sprite_id must be between 1 and 1024");
    }

    // Validate explosion mode
    if (explosion_mode < 1 || explosion_mode > 6) {
        return luaL_error(L, "sprite_explode_mode: explosion_mode must be between 1 and 6");
    }

    bool success = false;

    // Apply the appropriate explosion mode
    switch (explosion_mode) {
        case 1: // BASIC_EXPLOSION
            success = sprite_explode_advanced((uint16_t)sprite_id, 48, 200.0f, 100.0f, 2.0f);
            break;
        case 2: // MASSIVE_BLAST
            success = sprite_explode_advanced((uint16_t)sprite_id, 128, 350.0f, 80.0f, 3.0f);
            break;
        case 3: // GENTLE_DISPERSAL
            success = sprite_explode_advanced((uint16_t)sprite_id, 64, 120.0f, 40.0f, 4.0f);
            break;
        case 4: // RIGHTWARD_BLAST
            success = sprite_explode_directional((uint16_t)sprite_id, 80, 180.0f, -30.0f);
            break;
        case 5: // UPWARD_ERUPTION
            success = sprite_explode_directional((uint16_t)sprite_id, 96, 0.0f, -250.0f);
            break;
        case 6: // RAPID_BURST
            success = sprite_explode_advanced((uint16_t)sprite_id, 32, 400.0f, 200.0f, 1.0f);
            break;
        default:
            return luaL_error(L, "sprite_explode_mode: invalid explosion_mode");
    }

    lua_pushboolean(L, success ? 1 : 0);
    return 1;
}

/**
 * particle_clear()
 *
 * Clear all active particles from the system.
 */
static int l_particle_system_clear(lua_State* L) {
    particle_system_clear();
    return 0;
}

/**
 * particle_set_time_scale(scale)
 *
 * Set the time scale for particle physics simulation.
 *
 * @param scale: Number - Time scale multiplier (0.1 to 5.0)
 */
static int l_particle_system_set_time_scale(lua_State* L) {
    float scale = (float)luaL_checknumber(L, 1);

    if (scale < 0.1f) scale = 0.1f;
    if (scale > 5.0f) scale = 5.0f;

    particle_system_set_time_scale(scale);
    return 0;
}

/**
 * particle_set_world_bounds(width, height)
 *
 * Set the world bounds for particle culling.
 *
 * @param width: Number - World width in pixels
 * @param height: Number - World height in pixels
 */
static int l_particle_system_set_world_bounds(lua_State* L) {
    float width = (float)luaL_checknumber(L, 1);
    float height = (float)luaL_checknumber(L, 2);

    particle_system_set_world_bounds(width, height);
    return 0;
}

/**
 * particle_set_enabled(enabled)
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
 * particle_get_active_count()
 *
 * Get the number of active particles.
 *
 * @return Integer - Number of active particles
 */
static int l_particle_system_get_active_count(lua_State* L) {
    uint32_t count = particle_system_get_active_count();
    lua_pushinteger(L, count);
    return 1;
}

/**
 * particle_get_total_created()
 *
 * Get the total number of particles created since initialization.
 *
 * @return Integer - Total particles created
 */
static int l_particle_system_get_total_created(lua_State* L) {
    uint64_t total = particle_system_get_total_created();
    lua_pushinteger(L, (lua_Integer)total);
    return 1;
}

/**
 * particle_dump_stats()
 *
 * Dump particle system statistics to console.
 */
static int l_particle_system_dump_stats(lua_State* L) {
    particle_system_dump_stats();
    return 0;
}

/**
 * particle_info()
 *
 * Get particle system information as a table.
 *
 * @return Table - Contains active_count, total_created
 */
static int l_particle_system_info(lua_State* L) {
    lua_newtable(L);

    lua_pushinteger(L, (lua_Integer)particle_system_get_active_count());
    lua_setfield(L, -2, "active_count");

    lua_pushinteger(L, (lua_Integer)particle_system_get_total_created());
    lua_setfield(L, -2, "total_created");

    lua_pushboolean(L, particle_system_is_ready());
    lua_setfield(L, -2, "ready");

    return 1;
}

/**
 * particle_pause()
 *
 * Pause particle physics simulation.
 */
static int l_particle_system_pause(lua_State* L) {
    particle_system_pause();
    return 0;
}

/**
 * particle_resume()
 *
 * Resume particle physics simulation.
 */
static int l_particle_system_resume(lua_State* L) {
    particle_system_resume();
    return 0;
}

#pragma mark - Registration

/**
 * Register all particle system Lua API functions.
 * Called once during Lua runtime initialization.
 * No deferred registration - all functions registered immediately.
 */
extern "C" void register_particle_system_lua_api(lua_State* L) {
    std::cout << "ParticleSystemLua: Registering particle system Lua API (direct bindings)" << std::endl;

    // Explosion functions
    lua_register(L, "sprite_explode", l_sprite_explode);
    lua_register(L, "sprite_explode_advanced", l_sprite_explode_advanced);
    lua_register(L, "sprite_explode_directional", l_sprite_explode_directional);
    lua_register(L, "sprite_explode_mode", l_sprite_explode_mode);

    // System control
    lua_register(L, "particle_clear", l_particle_system_clear);
    lua_register(L, "particle_set_time_scale", l_particle_system_set_time_scale);
    lua_register(L, "particle_set_world_bounds", l_particle_system_set_world_bounds);
    lua_register(L, "particle_set_enabled", l_particle_system_set_enabled);
    lua_register(L, "particle_pause", l_particle_system_pause);
    lua_register(L, "particle_resume", l_particle_system_resume);

    // Queries
    lua_register(L, "particle_get_active_count", l_particle_system_get_active_count);
    lua_register(L, "particle_get_total_created", l_particle_system_get_total_created);
    lua_register(L, "particle_dump_stats", l_particle_system_dump_stats);
    lua_register(L, "particle_info", l_particle_system_info);

    // Export explosion mode constants
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

    std::cout << "ParticleSystemLua: Registration complete ("
              << "14 functions, 6 constants)" << std::endl;

    // Create particle_system table for backward compatibility (e.g., breakout.lua)
    lua_newtable(L);

    // Add methods to particle_system table
    lua_pushcfunction(L, l_particle_system_clear);
    lua_setfield(L, -2, "clear");

    lua_pushcfunction(L, l_particle_system_set_time_scale);
    lua_setfield(L, -2, "set_time_scale");

    lua_pushcfunction(L, l_particle_system_set_world_bounds);
    lua_setfield(L, -2, "set_world_bounds");

    lua_pushcfunction(L, l_particle_system_set_enabled);
    lua_setfield(L, -2, "set_enabled");

    lua_pushcfunction(L, l_particle_system_pause);
    lua_setfield(L, -2, "pause");

    lua_pushcfunction(L, l_particle_system_resume);
    lua_setfield(L, -2, "resume");

    lua_pushcfunction(L, l_particle_system_get_active_count);
    lua_setfield(L, -2, "get_active_count");

    lua_pushcfunction(L, l_particle_system_get_total_created);
    lua_setfield(L, -2, "get_total_created");

    lua_pushcfunction(L, l_particle_system_dump_stats);
    lua_setfield(L, -2, "dump_stats");

    lua_pushcfunction(L, l_particle_system_info);
    lua_setfield(L, -2, "info");

    // Set as global particle_system table
    lua_setglobal(L, "particle_system");

    std::cout << "ParticleSystemLua: Created particle_system table for backward compatibility" << std::endl;
}

/**
 * Compatibility function for LuaRuntime - always succeeds (no deferred init in v2)
 */
extern "C" bool register_particle_system_lua_api_when_ready(lua_State* L) {
    // In v2, there's no deferred initialization - system is ready immediately after init
    // This function exists for backward compatibility with LuaRuntime
    // No-op: API is already registered by register_particle_system_lua_api()
    return true;
}

/**
 * Compatibility function for LuaRuntime - does nothing (C API handles shutdown)
 */
extern "C" void shutdown_particle_system_from_lua() {
    // In v2, Lua doesn't manage particle system lifecycle
    // C API particle_system_shutdown() is called by SubsystemManager
    // This function exists for backward compatibility with LuaRuntime
    // No-op: shutdown is handled by SubsystemManager
}

/**
 * Compatibility function for LuaRuntime - deprecated (use C API directly)
 */
extern "C" int initialize_particle_system_from_lua(lua_State* L, void* metalDevice) {
    // In v2, initialization is handled by SubsystemManager via C API
    // This function exists for backward compatibility
    // Just check if system is ready
    return particle_system_is_ready() ? 1 : 0;
}
