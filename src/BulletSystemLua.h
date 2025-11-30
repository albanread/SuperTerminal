//
//  BulletSystemLua.h
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#ifndef BULLET_SYSTEM_LUA_H
#define BULLET_SYSTEM_LUA_H

#ifdef __cplusplus
extern "C" {
#endif

struct lua_State;

/**
 * Register bullet system API functions with Lua runtime
 * This makes all bullet functions and constants available to Lua scripts
 * 
 * @param L Lua state to register functions with
 */
void register_bullet_system_lua_api(struct lua_State* L);

/**
 * Initialize the bullet system from Lua
 * This should be called during SuperTerminal initialization
 * 
 * @param metal_device Metal device for rendering (id<MTLDevice>)
 * @param sprite_layer Pointer to SpriteLayer instance
 * @return true if initialization successful, false otherwise
 */
bool initialize_bullet_system_from_lua(void* metal_device, void* sprite_layer);

/**
 * Shutdown the bullet system from Lua
 * This should be called during SuperTerminal cleanup
 */
void shutdown_bullet_system_from_lua(void);

#ifdef __cplusplus
}
#endif

#endif // BULLET_SYSTEM_LUA_H