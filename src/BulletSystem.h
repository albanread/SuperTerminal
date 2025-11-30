//
//  BulletSystem.h
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#ifndef BULLET_SYSTEM_H
#define BULLET_SYSTEM_H

#include <vector>
#include <queue>
#include <unordered_map>
#include <memory>
#include <Metal/Metal.h>
#include <chrono>

#pragma mark - Forward Declarations
// Forward declarations to avoid header dependencies
class SpriteLayer;

#pragma mark - Bullet Types and Constants

enum BulletType : uint8_t {
    BULLET_NORMAL = 1,      // Standard projectile
    BULLET_TRACER = 2,      // Glowing trail effect  
    BULLET_EXPLOSIVE = 3,   // Explodes on impact
    BULLET_PIERCING = 4,    // Goes through multiple targets
    BULLET_LASER = 5,       // Instant hit, no travel time
    BULLET_ROCKET = 6       // Guided missile with effects
};

enum BulletRenderMode : uint8_t {
    RENDER_SPRITE = 1,      // Individual sprite per bullet (current)
    RENDER_INSTANCED = 2    // Instanced rendering (future optimization)
};

#pragma mark - Bullet Data Structures

// Core bullet physics and gameplay data (render-agnostic)
struct BulletData {
    uint32_t id;                    // Unique bullet identifier
    float x, y;                     // Current world position
    float velocity_x, velocity_y;   // Movement vector (pixels/second)
    float lifetime_remaining;       // Time before auto-destruction (seconds)
    uint16_t owner_sprite_id;       // Sprite that fired this bullet
    uint8_t damage;                 // Damage dealt on hit
    BulletType type;                // Bullet behavior type
    bool active;                    // Is bullet still in play
    bool piercing_used;             // Has piercing bullet hit anything yet
    
    // Physics properties
    float gravity_scale;            // How much gravity affects this bullet (0.0 = none)
    float drag_coefficient;         // Air resistance (0.0 = none, 1.0 = high)
    float max_speed;                // Speed limit for physics calculations
    
    // Visual properties (used by both render modes)
    float rotation;                 // Bullet rotation in radians
    float scale;                    // Size multiplier
    float alpha;                    // Transparency (0.0-1.0)
    uint32_t color;                 // RGBA color override
};

// Sprite-specific rendering data (current implementation)
struct SpriteBulletRender {
    uint16_t sprite_id;             // Associated sprite ID
    const char* texture_path;       // Sprite texture file
    bool sprite_allocated;          // Did we allocate this sprite ID
};

// Instance rendering data (future optimization)
struct InstancedBulletRender {
    uint16_t texture_index;         // Index into texture atlas
    float texture_u, texture_v;     // UV coordinates in atlas
    float width, height;            // Bullet dimensions
};

// Combined bullet with render info
struct Bullet {
    BulletData data;
    BulletRenderMode render_mode;
    
    union {
        SpriteBulletRender sprite;
        InstancedBulletRender instanced;
    } render;
    
    Bullet() : render_mode(RENDER_SPRITE) {
        render.sprite = {};
    }
};

#pragma mark - Collision Results

struct BulletCollision {
    uint32_t bullet_id;             // Which bullet hit
    uint16_t hit_sprite_id;         // What sprite was hit
    float hit_x, hit_y;             // Exact collision point
    uint8_t damage;                 // Damage dealt
    BulletType bullet_type;         // Type of bullet for special effects
    bool bullet_destroyed;          // Was bullet destroyed by hit
};

#pragma mark - Firing Parameters

struct BulletFireParams {
    float start_x, start_y;         // Firing position
    float target_x, target_y;       // Target position (or direction)
    float speed;                    // Initial speed (pixels/second)
    uint8_t damage;                 // Damage value
    BulletType type;                // Bullet behavior
    float lifetime;                 // Max time to live (seconds)
    uint16_t owner_sprite;          // Who fired it
    const char* texture_path;       // Visual texture (for sprites)
    
    // Optional physics parameters
    float gravity_scale = 0.0f;     // Gravity effect
    float drag_coefficient = 0.0f;  // Air resistance
    float spread_angle = 0.0f;      // Random spread in radians
    
    // Visual parameters
    float scale = 1.0f;             // Size multiplier
    float alpha = 1.0f;             // Transparency
    uint32_t color = 0xFFFFFFFF;    // Color tint (RGBA)
};

#pragma mark - Performance Monitoring

struct BulletSystemStats {
    uint32_t active_bullets;        // Currently active bullets
    uint32_t total_fired;           // Total bullets created
    uint32_t total_hits;            // Total successful hits
    uint32_t total_destroyed;       // Total bullets destroyed
    float physics_fps;              // Physics update frequency
    uint32_t collision_checks_per_frame;  // Performance metric
    uint32_t render_calls_per_frame;      // Render performance
};

#pragma mark - Main Bullet System Class

class BulletSystem {
private:
    // Core data
    std::vector<Bullet> bullets;
    std::queue<uint32_t> free_bullet_ids;
    std::unordered_map<uint32_t, size_t> id_to_index;  // Fast bullet lookup
    
    // Rendering integration
    void* sprite_layer;  // SpriteLayer* but using void* to avoid header dependency
    void* metal_device;  // id<MTLDevice> but using void* to avoid header dependency
    void* command_queue; // id<MTLCommandQueue> but using void* to avoid header dependency
    
    // Performance and limits
    uint32_t max_bullets;
    uint32_t next_bullet_id;
    
    // Statistics
    BulletSystemStats stats;
    std::chrono::high_resolution_clock::time_point last_update_time;
    
    // Sprite management (current mode)
    std::queue<uint16_t> available_sprite_ids;
    std::vector<uint16_t> allocated_sprite_ids;
    
    // Future: Instanced rendering resources
    id<MTLBuffer> instance_buffer;
    id<MTLRenderPipelineState> instanced_pipeline;
    id<MTLTexture> bullet_texture_atlas;
    
    // Internal methods
    uint32_t allocate_bullet_id();
    void deallocate_bullet_id(uint32_t id);
    uint16_t allocate_sprite_id();
    void deallocate_sprite_id(uint16_t sprite_id);
    
    void update_bullet_physics(Bullet& bullet, float delta_time);
    void update_bullet_rendering(Bullet& bullet);
    bool is_bullet_off_screen(const Bullet& bullet);
    
public:
    // Initialization
    BulletSystem(uint32_t max_bullets = 512);
    ~BulletSystem();
    
    bool initialize(void* device, void* sprites);
    void shutdown();
    
    // Core bullet operations
    uint32_t fire_bullet(const BulletFireParams& params);
    uint32_t fire_bullet_simple(float start_x, float start_y, 
                                float target_x, float target_y,
                                float speed, uint8_t damage,
                                const char* texture_path = "assets/bullet.png");
    
    bool destroy_bullet(uint32_t bullet_id);
    void clear_all_bullets();
    
    // Update and rendering
    void update(float delta_time);
    void render_sprites(void* encoder);  // Current sprite-based rendering
    void render_instanced(void* encoder); // Future instanced rendering
    
    // Collision detection
    std::vector<BulletCollision> check_sprite_collisions(uint16_t sprite_id);
    std::vector<BulletCollision> check_area_collisions(float x, float y, float radius);
    std::vector<BulletCollision> check_all_collisions();  // Check all active bullets
    
    // Bullet queries
    bool is_bullet_active(uint32_t bullet_id);
    BulletData* get_bullet_data(uint32_t bullet_id);
    std::vector<uint32_t> get_bullets_by_owner(uint16_t owner_sprite);
    
    // System management  
    void set_world_bounds(float width, float height);
    void set_max_bullets(uint32_t max);
    void pause_physics();
    void resume_physics();
    
    // Statistics and debugging
    BulletSystemStats get_stats() const;
    void dump_debug_info();
    uint32_t get_active_bullet_count() const;
    
    // Future: Render mode switching
    void set_render_mode(BulletRenderMode mode);
    bool init_instanced_rendering();
    void cleanup_instanced_rendering();
};

#pragma mark - Global C Interface (for Lua integration)

extern "C" {
    // System management
    bool bullet_system_initialize(void* metal_device, void* sprite_layer);
    void bullet_system_shutdown();
    void bullet_system_update(float delta_time);
    void bullet_system_render();
    
    // Bullet operations
    uint32_t bullet_fire(float start_x, float start_y, float target_x, float target_y, 
                         float speed, uint8_t damage, const char* texture_path);
    uint32_t bullet_fire_advanced(float start_x, float start_y, float target_x, float target_y,
                                  float speed, uint8_t damage, uint8_t bullet_type,
                                  float lifetime, uint16_t owner_sprite, const char* texture_path);
    bool bullet_destroy(uint32_t bullet_id);
    void bullet_clear_all();
    
    // Collision detection
    uint32_t bullet_check_collisions(uint32_t* collision_buffer, uint32_t buffer_size);
    bool bullet_check_sprite_hit(uint16_t sprite_id);
    
    // Queries
    bool bullet_is_active(uint32_t bullet_id);
    uint32_t bullet_get_active_count();
    void bullet_get_stats(BulletSystemStats* stats);
    
    // Configuration
    void bullet_set_world_bounds(float width, float height);
    void bullet_set_max_count(uint32_t max_bullets);
}

#pragma mark - Bullet Type Definitions for Different Weapons

// Predefined bullet configurations for common weapon types
namespace BulletPresets {
    extern const BulletFireParams PISTOL_BULLET;
    extern const BulletFireParams MACHINE_GUN_BULLET;
    extern const BulletFireParams SNIPER_BULLET;
    extern const BulletFireParams ROCKET_LAUNCHER;
    extern const BulletFireParams LASER_BEAM;
    extern const BulletFireParams TRACER_ROUND;
}

#endif // BULLET_SYSTEM_H