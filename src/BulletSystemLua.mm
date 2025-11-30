//
//  BulletSystemLua.cpp
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "BulletSystemLua.h"
#include "BulletSystem.h"
#include <lua.hpp>
#include <iostream>

#pragma mark - Bullet Type Constants

// Bullet type constants for Lua
#define BULLET_NORMAL_LUA     1
#define BULLET_TRACER_LUA     2
#define BULLET_EXPLOSIVE_LUA  3
#define BULLET_PIERCING_LUA   4
#define BULLET_LASER_LUA      5
#define BULLET_ROCKET_LUA     6

#pragma mark - Lua API Functions

/**
 * Fire a simple bullet
 * Usage: bullet_id = bullet_fire(start_x, start_y, target_x, target_y, speed, damage, texture_path)
 */
static int l_bullet_fire(lua_State* L) {
    // Get parameters
    float start_x = (float)luaL_checknumber(L, 1);
    float start_y = (float)luaL_checknumber(L, 2);
    float target_x = (float)luaL_checknumber(L, 3);
    float target_y = (float)luaL_checknumber(L, 4);
    float speed = (float)luaL_checknumber(L, 5);
    uint8_t damage = (uint8_t)luaL_checkinteger(L, 6);
    
    // Optional texture path (default to basic bullet)
    const char* texture_path = "assets/bullet.png";
    if (lua_gettop(L) >= 7 && !lua_isnil(L, 7)) {
        texture_path = luaL_checkstring(L, 7);
    }
    
    // Fire bullet
    uint32_t bullet_id = bullet_fire(start_x, start_y, target_x, target_y, speed, damage, texture_path);
    
    lua_pushinteger(L, bullet_id);
    return 1;
}

/**
 * Fire an advanced bullet with all options
 * Usage: bullet_id = bullet_fire_advanced(start_x, start_y, target_x, target_y, options)
 * options = { speed=600, damage=25, type=BULLET_NORMAL, lifetime=3.0, owner_sprite=0, texture_path="..." }
 */
static int l_bullet_fire_advanced(lua_State* L) {
    // Get position parameters
    float start_x = (float)luaL_checknumber(L, 1);
    float start_y = (float)luaL_checknumber(L, 2);
    float target_x = (float)luaL_checknumber(L, 3);
    float target_y = (float)luaL_checknumber(L, 4);
    
    // Get options table
    luaL_checktype(L, 5, LUA_TTABLE);
    
    // Extract options with defaults
    lua_getfield(L, 5, "speed");
    float speed = lua_isnil(L, -1) ? 600.0f : (float)luaL_checknumber(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, 5, "damage");
    uint8_t damage = lua_isnil(L, -1) ? 25 : (uint8_t)luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, 5, "type");
    uint8_t bullet_type = lua_isnil(L, -1) ? BULLET_NORMAL : (uint8_t)luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, 5, "lifetime");
    float lifetime = lua_isnil(L, -1) ? 3.0f : (float)luaL_checknumber(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, 5, "owner_sprite");
    uint16_t owner_sprite = lua_isnil(L, -1) ? 0 : (uint16_t)luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, 5, "texture_path");
    const char* texture_path = lua_isnil(L, -1) ? "assets/bullet.png" : luaL_checkstring(L, -1);
    lua_pop(L, 1);
    
    // Fire advanced bullet
    uint32_t bullet_id = bullet_fire_advanced(start_x, start_y, target_x, target_y,
                                              speed, damage, bullet_type,
                                              lifetime, owner_sprite, texture_path);
    
    lua_pushinteger(L, bullet_id);
    return 1;
}

/**
 * Fire a machine gun burst
 * Usage: bullet_ids = bullet_fire_burst(gun_x, gun_y, target_x, target_y, burst_size, options)
 */
static int l_bullet_fire_burst(lua_State* L) {
    float gun_x = (float)luaL_checknumber(L, 1);
    float gun_y = (float)luaL_checknumber(L, 2);
    float target_x = (float)luaL_checknumber(L, 3);
    float target_y = (float)luaL_checknumber(L, 4);
    int burst_size = luaL_checkinteger(L, 5);
    
    // Optional parameters table
    float speed = 800.0f;
    uint8_t damage = 15;
    float spread = 0.1f;
    const char* texture_path = "assets/bullet_tracer.png";
    
    if (lua_gettop(L) >= 6 && lua_istable(L, 6)) {
        lua_getfield(L, 6, "speed");
        if (!lua_isnil(L, -1)) speed = (float)luaL_checknumber(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 6, "damage");
        if (!lua_isnil(L, -1)) damage = (uint8_t)luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 6, "spread");
        if (!lua_isnil(L, -1)) spread = (float)luaL_checknumber(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 6, "texture_path");
        if (!lua_isnil(L, -1)) texture_path = luaL_checkstring(L, -1);
        lua_pop(L, 1);
    }
    
    // Create table for bullet IDs
    lua_newtable(L);
    
    // Fire burst
    for (int i = 0; i < burst_size; i++) {
        float spread_angle = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * spread;
        float dx = target_x - gun_x;
        float dy = target_y - gun_y;
        float distance = sqrtf(dx * dx + dy * dy);
        
        if (distance > 0.001f) {
            dx /= distance;
            dy /= distance;
            
            // Apply spread
            float cos_spread = cosf(spread_angle);
            float sin_spread = sinf(spread_angle);
            float new_dx = dx * cos_spread - dy * sin_spread;
            float new_dy = dx * sin_spread + dy * cos_spread;
            
            // Calculate target with spread
            float spread_target_x = gun_x + new_dx * distance;
            float spread_target_y = gun_y + new_dy * distance;
            
            uint32_t bullet_id = bullet_fire_advanced(gun_x, gun_y, spread_target_x, spread_target_y,
                                                      speed, damage, BULLET_TRACER,
                                                      2.0f, 0, texture_path);
            
            // Add to results table
            lua_pushinteger(L, bullet_id);
            lua_rawseti(L, -2, i + 1);
        }
    }
    
    return 1; // Return table of bullet IDs
}

/**
 * Destroy a bullet
 * Usage: success = bullet_destroy(bullet_id)
 */
static int l_bullet_destroy(lua_State* L) {
    uint32_t bullet_id = (uint32_t)luaL_checkinteger(L, 1);
    bool success = bullet_destroy(bullet_id);
    lua_pushboolean(L, success);
    return 1;
}

/**
 * Check if a bullet is active
 * Usage: active = bullet_is_active(bullet_id)
 */
static int l_bullet_is_active(lua_State* L) {
    uint32_t bullet_id = (uint32_t)luaL_checkinteger(L, 1);
    bool active = bullet_is_active(bullet_id);
    lua_pushboolean(L, active);
    return 1;
}

/**
 * Clear all bullets
 * Usage: bullet_clear_all()
 */
static int l_bullet_clear_all(lua_State* L) {
    bullet_clear_all();
    return 0;
}

/**
 * Check if a sprite was hit by any bullet
 * Usage: hit = bullet_check_sprite_hit(sprite_id)
 */
static int l_bullet_check_sprite_hit(lua_State* L) {
    uint16_t sprite_id = (uint16_t)luaL_checkinteger(L, 1);
    bool hit = bullet_check_sprite_hit(sprite_id);
    lua_pushboolean(L, hit);
    return 1;
}

/**
 * Get active bullet count
 * Usage: count = bullet_get_active_count()
 */
static int l_bullet_get_active_count(lua_State* L) {
    uint32_t count = bullet_get_active_count();
    lua_pushinteger(L, count);
    return 1;
}

/**
 * Get bullet system statistics
 * Usage: stats = bullet_get_stats()
 * Returns: { active_bullets, total_fired, total_hits, total_destroyed, physics_fps }
 */
static int l_bullet_get_stats(lua_State* L) {
    BulletSystemStats stats;
    bullet_get_stats(&stats);
    
    lua_newtable(L);
    
    lua_pushinteger(L, stats.active_bullets);
    lua_setfield(L, -2, "active_bullets");
    
    lua_pushinteger(L, stats.total_fired);
    lua_setfield(L, -2, "total_fired");
    
    lua_pushinteger(L, stats.total_hits);
    lua_setfield(L, -2, "total_hits");
    
    lua_pushinteger(L, stats.total_destroyed);
    lua_setfield(L, -2, "total_destroyed");
    
    lua_pushnumber(L, stats.physics_fps);
    lua_setfield(L, -2, "physics_fps");
    
    lua_pushinteger(L, stats.collision_checks_per_frame);
    lua_setfield(L, -2, "collision_checks_per_frame");
    
    lua_pushinteger(L, stats.render_calls_per_frame);
    lua_setfield(L, -2, "render_calls_per_frame");
    
    return 1;
}

/**
 * Set world bounds for bullet cleanup
 * Usage: bullet_set_world_bounds(width, height)
 */
static int l_bullet_set_world_bounds(lua_State* L) {
    float width = (float)luaL_checknumber(L, 1);
    float height = (float)luaL_checknumber(L, 2);
    bullet_set_world_bounds(width, height);
    return 0;
}

/**
 * Set maximum bullet count
 * Usage: bullet_set_max_count(max_bullets)
 */
static int l_bullet_set_max_count(lua_State* L) {
    uint32_t max_bullets = (uint32_t)luaL_checkinteger(L, 1);
    bullet_set_max_count(max_bullets);
    return 0;
}

#pragma mark - Bullet System Table Functions

/**
 * bullet_system.pause() - Pause bullet physics
 */
static int l_bullet_system_pause(lua_State* L) {
    // TODO: Implement pause functionality
    std::cout << "BulletSystemLua: Pause requested (not yet implemented)" << std::endl;
    return 0;
}

/**
 * bullet_system.resume() - Resume bullet physics  
 */
static int l_bullet_system_resume(lua_State* L) {
    // TODO: Implement resume functionality
    std::cout << "BulletSystemLua: Resume requested (not yet implemented)" << std::endl;
    return 0;
}

/**
 * bullet_system.clear() - Clear all bullets
 */
static int l_bullet_system_clear(lua_State* L) {
    bullet_clear_all();
    return 0;
}

/**
 * bullet_system.get_active_count() - Get active bullet count
 */
static int l_bullet_system_get_active_count(lua_State* L) {
    uint32_t count = bullet_get_active_count();
    lua_pushinteger(L, count);
    return 1;
}

/**
 * bullet_system.get_stats() - Get system statistics
 */
static int l_bullet_system_get_stats(lua_State* L) {
    return l_bullet_get_stats(L);
}

/**
 * bullet_system.info() - Print debug information
 */
static int l_bullet_system_info(lua_State* L) {
    BulletSystemStats stats;
    bullet_get_stats(&stats);
    
    std::cout << "\n=== BULLET SYSTEM INFO ===" << std::endl;
    std::cout << "Active bullets: " << stats.active_bullets << std::endl;
    std::cout << "Total fired: " << stats.total_fired << std::endl;
    std::cout << "Total hits: " << stats.total_hits << std::endl;
    std::cout << "Total destroyed: " << stats.total_destroyed << std::endl;
    std::cout << "Physics FPS: " << stats.physics_fps << std::endl;
    std::cout << "Collision checks/frame: " << stats.collision_checks_per_frame << std::endl;
    std::cout << "Render calls/frame: " << stats.render_calls_per_frame << std::endl;
    std::cout << "===========================" << std::endl;
    
    return 0;
}

#pragma mark - Registration Functions

void register_bullet_system_lua_api(lua_State* L) {
    std::cout << "BulletSystemLua: Registering bullet system API..." << std::endl;
    
    // Create bullet_system table
    lua_newtable(L);
    
    // System control functions
    lua_pushcfunction(L, l_bullet_system_pause);
    lua_setfield(L, -2, "pause");
    
    lua_pushcfunction(L, l_bullet_system_resume);
    lua_setfield(L, -2, "resume");
    
    lua_pushcfunction(L, l_bullet_system_clear);
    lua_setfield(L, -2, "clear");
    
    // Information functions
    lua_pushcfunction(L, l_bullet_system_get_active_count);
    lua_setfield(L, -2, "get_active_count");
    
    lua_pushcfunction(L, l_bullet_system_get_stats);
    lua_setfield(L, -2, "get_stats");
    
    lua_pushcfunction(L, l_bullet_system_info);
    lua_setfield(L, -2, "info");
    
    // Set the bullet_system table as global
    lua_setglobal(L, "bullet_system");
    
    // Register bullet functions as global functions
    lua_pushcfunction(L, l_bullet_fire);
    lua_setglobal(L, "bullet_fire");
    
    lua_pushcfunction(L, l_bullet_fire_advanced);
    lua_setglobal(L, "bullet_fire_advanced");
    
    lua_pushcfunction(L, l_bullet_fire_burst);
    lua_setglobal(L, "bullet_fire_burst");
    
    lua_pushcfunction(L, l_bullet_destroy);
    lua_setglobal(L, "bullet_destroy");
    
    lua_pushcfunction(L, l_bullet_is_active);
    lua_setglobal(L, "bullet_is_active");
    
    lua_pushcfunction(L, l_bullet_clear_all);
    lua_setglobal(L, "bullet_clear_all");
    
    lua_pushcfunction(L, l_bullet_check_sprite_hit);
    lua_setglobal(L, "bullet_check_sprite_hit");
    
    lua_pushcfunction(L, l_bullet_get_active_count);
    lua_setglobal(L, "bullet_get_active_count");
    
    lua_pushcfunction(L, l_bullet_get_stats);
    lua_setglobal(L, "bullet_get_stats");
    
    lua_pushcfunction(L, l_bullet_set_world_bounds);
    lua_setglobal(L, "bullet_set_world_bounds");
    
    lua_pushcfunction(L, l_bullet_set_max_count);
    lua_setglobal(L, "bullet_set_max_count");
    
    // Register bullet type constants as globals
    lua_pushinteger(L, BULLET_NORMAL_LUA);
    lua_setglobal(L, "BULLET_NORMAL");
    
    lua_pushinteger(L, BULLET_TRACER_LUA);
    lua_setglobal(L, "BULLET_TRACER");
    
    lua_pushinteger(L, BULLET_EXPLOSIVE_LUA);
    lua_setglobal(L, "BULLET_EXPLOSIVE");
    
    lua_pushinteger(L, BULLET_PIERCING_LUA);
    lua_setglobal(L, "BULLET_PIERCING");
    
    lua_pushinteger(L, BULLET_LASER_LUA);
    lua_setglobal(L, "BULLET_LASER");
    
    lua_pushinteger(L, BULLET_ROCKET_LUA);
    lua_setglobal(L, "BULLET_ROCKET");
    
    std::cout << "BulletSystemLua: Registered bullet system API functions and constants" << std::endl;
}

bool initialize_bullet_system_from_lua(void* metal_device, void* sprite_layer) {
    std::cout << "BulletSystemLua: Initializing bullet system..." << std::endl;
    
    bool success = bullet_system_initialize(metal_device, sprite_layer);
    
    if (success) {
        std::cout << "BulletSystemLua: Bullet system initialized successfully" << std::endl;
    } else {
        std::cout << "BulletSystemLua: ERROR - Failed to initialize bullet system" << std::endl;
    }
    
    return success;
}

void shutdown_bullet_system_from_lua() {
    std::cout << "BulletSystemLua: Shutting down bullet system..." << std::endl;
    bullet_system_shutdown();
}