//
//  ParticleSystemC.h
//  SuperTerminal Framework
//
//  C-Compatible API for Particle System
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#ifndef ParticleSystemC_h
#define ParticleSystemC_h

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// System management
bool particle_system_initialize(void* metalDevice);
void particle_system_shutdown();
void particle_system_pause();
void particle_system_resume();
void particle_system_clear();

// Explosion functions
bool sprite_explode(uint16_t spriteId, uint16_t particleCount);
bool sprite_explode_advanced(uint16_t spriteId, uint16_t particleCount, 
                            float explosionForce, float gravity, float fadeTime);
bool sprite_explode_directional(uint16_t spriteId, uint16_t particleCount,
                               float forceX, float forceY);

// Configuration
void particle_system_set_time_scale(float scale);
void particle_system_set_world_bounds(float width, float height);
void particle_system_set_enabled(bool enabled);

// Rendering
void particle_system_render(void* encoder, float projectionMatrix[16]);

// Performance monitoring
uint32_t particle_system_get_active_count();
uint64_t particle_system_get_total_created();
float particle_system_get_physics_fps();
void particle_system_dump_stats();

// Lua API registration functions
void register_particle_system_lua_api(struct lua_State* L);
void register_particle_system_lua_api_full(struct lua_State* L);
bool register_particle_system_lua_api_when_ready(struct lua_State* L);
void particle_system_lua_mark_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* ParticleSystemC_h */