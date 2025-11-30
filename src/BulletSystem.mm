//
//  BulletSystem.mm
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "BulletSystem.h"
#include <iostream>
#include <cmath>
#include <algorithm>

// Forward declare functions from SuperTerminal C API
extern "C" {
    bool sprite_load(uint16_t id, const char* filename);
    void sprite_show(uint16_t id, float x, float y);
    void sprite_hide(uint16_t id);
    void sprite_move(uint16_t id, float x, float y);
    void sprite_scale(uint16_t id, float scale);
    void sprite_alpha(uint16_t id, float alpha);
    void sprite_rotate(uint16_t id, float angle);
    bool sprite_check_collision(uint16_t id1, uint16_t id2);
}



#pragma mark - Global Instance
static BulletSystem* g_bullet_system = nullptr;

#pragma mark - Bullet Presets
namespace BulletPresets {
    const BulletFireParams PISTOL_BULLET = {
        .speed = 600.0f,
        .damage = 25,
        .type = BULLET_NORMAL,
        .lifetime = 2.0f,
        .gravity_scale = 0.1f,
        .drag_coefficient = 0.02f,
        .texture_path = "assets/bullet_pistol.png"
    };

    const BulletFireParams MACHINE_GUN_BULLET = {
        .speed = 800.0f,
        .damage = 15,
        .type = BULLET_TRACER,
        .lifetime = 1.5f,
        .gravity_scale = 0.05f,
        .spread_angle = 0.1f,
        .texture_path = "assets/bullet_tracer.png"
    };

    const BulletFireParams SNIPER_BULLET = {
        .speed = 1200.0f,
        .damage = 80,
        .type = BULLET_PIERCING,
        .lifetime = 3.0f,
        .gravity_scale = 0.2f,
        .texture_path = "assets/bullet_sniper.png"
    };

    const BulletFireParams ROCKET_LAUNCHER = {
        .speed = 400.0f,
        .damage = 100,
        .type = BULLET_EXPLOSIVE,
        .lifetime = 5.0f,
        .scale = 2.0f,
        .texture_path = "assets/rocket.png"
    };

    const BulletFireParams LASER_BEAM = {
        .speed = 2000.0f,
        .damage = 40,
        .type = BULLET_LASER,
        .lifetime = 0.1f,
        .alpha = 0.8f,
        .texture_path = "assets/laser_beam.png"
    };

    const BulletFireParams TRACER_ROUND = {
        .speed = 700.0f,
        .damage = 20,
        .type = BULLET_TRACER,
        .lifetime = 2.5f,
        .gravity_scale = 0.08f,
        .texture_path = "assets/bullet_tracer.png"
    };
}

#pragma mark - BulletSystem Implementation

BulletSystem::BulletSystem(uint32_t max_bullets) :
    max_bullets(max_bullets),
    next_bullet_id(1),
    sprite_layer(nullptr),
    metal_device(nullptr),
    command_queue(nullptr),
    instance_buffer(nullptr),
    instanced_pipeline(nullptr),
    bullet_texture_atlas(nullptr)
{
    bullets.reserve(max_bullets);

    // Initialize sprite ID pool (reserve range for bullets)
    for (uint16_t i = 200; i < 255; i++) {  // Use sprite IDs 200-254 for bullets
        available_sprite_ids.push(i);
    }

    // Initialize stats
    stats = {};
    last_update_time = std::chrono::high_resolution_clock::now();

    std::cout << "BulletSystem: Initialized with capacity for " << max_bullets << " bullets" << std::endl;
}

BulletSystem::~BulletSystem() {
    shutdown();
}

bool BulletSystem::initialize(void* device, void* sprites) {
    std::cout << "BulletSystem: Initializing..." << std::endl;

    if (!device || !sprites) {
        std::cout << "BulletSystem: ERROR - Invalid device or sprite layer" << std::endl;
        return false;
    }

    metal_device = device;
    sprite_layer = sprites;

    id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
    command_queue = (__bridge void*)[metalDevice newCommandQueue];

    if (!command_queue) {
        std::cout << "BulletSystem: ERROR - Failed to create command queue" << std::endl;
        return false;
    }

    std::cout << "BulletSystem: Initialization complete" << std::endl;
    return true;
}

void BulletSystem::shutdown() {
    std::cout << "BulletSystem: Shutting down..." << std::endl;

    // Clear all bullets and free sprite IDs
    clear_all_bullets();

    // Release Metal resources
    if (instance_buffer) {
        [instance_buffer release];
        instance_buffer = nullptr;
    }

    if (instanced_pipeline) {
        [instanced_pipeline release];
        instanced_pipeline = nullptr;
    }

    if (bullet_texture_atlas) {
        [bullet_texture_atlas release];
        bullet_texture_atlas = nullptr;
    }

    if (command_queue) {
        // Cast back to release - no need for __bridge_transfer without ARC
        [(id<MTLCommandQueue>)(__bridge id<MTLCommandQueue>)command_queue release];
        command_queue = nullptr;
    }

    metal_device = nullptr;
    sprite_layer = nullptr;

    std::cout << "BulletSystem: Shutdown complete" << std::endl;
}

uint32_t BulletSystem::allocate_bullet_id() {
    if (!free_bullet_ids.empty()) {
        uint32_t id = free_bullet_ids.front();
        free_bullet_ids.pop();
        return id;
    }
    return next_bullet_id++;
}

void BulletSystem::deallocate_bullet_id(uint32_t id) {
    free_bullet_ids.push(id);
}

uint16_t BulletSystem::allocate_sprite_id() {
    if (available_sprite_ids.empty()) {
        std::cout << "BulletSystem: WARNING - No available sprite IDs for bullets" << std::endl;
        return 0;  // Invalid sprite ID
    }

    uint16_t sprite_id = available_sprite_ids.front();
    available_sprite_ids.pop();
    allocated_sprite_ids.push_back(sprite_id);
    return sprite_id;
}

void BulletSystem::deallocate_sprite_id(uint16_t sprite_id) {
    if (sprite_id == 0) return;

    // Remove from allocated list
    auto it = std::find(allocated_sprite_ids.begin(), allocated_sprite_ids.end(), sprite_id);
    if (it != allocated_sprite_ids.end()) {
        allocated_sprite_ids.erase(it);
    }

    // Hide sprite and return to available pool
    sprite_hide(sprite_id);
    available_sprite_ids.push(sprite_id);
}

uint32_t BulletSystem::fire_bullet_simple(float start_x, float start_y,
                                          float target_x, float target_y,
                                          float speed, uint8_t damage,
                                          const char* texture_path) {
    BulletFireParams params = {};
    params.start_x = start_x;
    params.start_y = start_y;
    params.target_x = target_x;
    params.target_y = target_y;
    params.speed = speed;
    params.damage = damage;
    params.type = BULLET_NORMAL;
    params.lifetime = 3.0f;
    params.owner_sprite = 0;
    params.texture_path = texture_path;

    return fire_bullet(params);
}

uint32_t BulletSystem::fire_bullet(const BulletFireParams& params) {
    if (bullets.size() >= max_bullets) {
        std::cout << "BulletSystem: WARNING - Maximum bullet limit reached (" << max_bullets << ")" << std::endl;
        return 0;  // Invalid bullet ID
    }

    // Calculate direction vector
    float dx = params.target_x - params.start_x;
    float dy = params.target_y - params.start_y;
    float distance = std::sqrt(dx * dx + dy * dy);

    if (distance < 0.001f) {  // Avoid division by zero
        dx = 1.0f;
        dy = 0.0f;
        distance = 1.0f;
    }

    // Normalize direction
    dx /= distance;
    dy /= distance;

    // Apply spread if specified
    if (params.spread_angle > 0.0f) {
        float spread = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * params.spread_angle;
        float cos_spread = std::cos(spread);
        float sin_spread = std::sin(spread);

        float new_dx = dx * cos_spread - dy * sin_spread;
        float new_dy = dx * sin_spread + dy * cos_spread;
        dx = new_dx;
        dy = new_dy;
    }

    // Create bullet
    Bullet bullet;
    bullet.data.id = allocate_bullet_id();
    bullet.data.x = params.start_x;
    bullet.data.y = params.start_y;
    bullet.data.velocity_x = dx * params.speed;
    bullet.data.velocity_y = dy * params.speed;
    bullet.data.lifetime_remaining = params.lifetime;
    bullet.data.owner_sprite_id = params.owner_sprite;
    bullet.data.damage = params.damage;
    bullet.data.type = params.type;
    bullet.data.active = true;
    bullet.data.piercing_used = false;
    bullet.data.gravity_scale = params.gravity_scale;
    bullet.data.drag_coefficient = params.drag_coefficient;
    bullet.data.max_speed = params.speed * 2.0f;  // Allow some acceleration
    bullet.data.rotation = std::atan2(dy, dx);  // Point bullet in movement direction
    bullet.data.scale = params.scale;
    bullet.data.alpha = params.alpha;
    bullet.data.color = params.color;

    // Set up sprite rendering
    bullet.render_mode = RENDER_SPRITE;
    bullet.render.sprite.sprite_id = allocate_sprite_id();
    bullet.render.sprite.texture_path = params.texture_path;
    bullet.render.sprite.sprite_allocated = true;

    if (bullet.render.sprite.sprite_id == 0) {
        deallocate_bullet_id(bullet.data.id);
        return 0;  // Failed to allocate sprite
    }

    // Load and configure sprite
    if (!sprite_load(bullet.render.sprite.sprite_id, params.texture_path)) {
        std::cout << "BulletSystem: WARNING - Failed to load bullet texture: " << params.texture_path << std::endl;
        // Try fallback texture
        if (!sprite_load(bullet.render.sprite.sprite_id, "assets/bullet.png")) {
            deallocate_sprite_id(bullet.render.sprite.sprite_id);
            deallocate_bullet_id(bullet.data.id);
            return 0;
        }
    }

    // Position and show sprite
    sprite_show(bullet.render.sprite.sprite_id, bullet.data.x, bullet.data.y);
    sprite_scale(bullet.render.sprite.sprite_id, bullet.data.scale);
    sprite_alpha(bullet.render.sprite.sprite_id, bullet.data.alpha);
    sprite_rotate(bullet.render.sprite.sprite_id, bullet.data.rotation);

    // Add to bullet list and index
    size_t index = bullets.size();
    bullets.push_back(bullet);
    id_to_index[bullet.data.id] = index;

    // Update stats
    stats.total_fired++;
    stats.active_bullets++;

    std::cout << "BulletSystem: Fired bullet " << bullet.data.id << " from ("
              << params.start_x << ", " << params.start_y << ") to ("
              << params.target_x << ", " << params.target_y << ")" << std::endl;

    return bullet.data.id;
}

void BulletSystem::update_bullet_physics(Bullet& bullet, float delta_time) {
    if (!bullet.data.active) return;

    // Apply gravity
    if (bullet.data.gravity_scale > 0.0f) {
        bullet.data.velocity_y += 980.0f * bullet.data.gravity_scale * delta_time;  // 980 px/s² gravity
    }

    // Apply drag
    if (bullet.data.drag_coefficient > 0.0f) {
        float speed = std::sqrt(bullet.data.velocity_x * bullet.data.velocity_x +
                               bullet.data.velocity_y * bullet.data.velocity_y);

        if (speed > 0.001f) {
            float drag_force = bullet.data.drag_coefficient * speed * delta_time;
            float drag_ratio = std::max(0.0f, 1.0f - drag_force / speed);

            bullet.data.velocity_x *= drag_ratio;
            bullet.data.velocity_y *= drag_ratio;
        }
    }

    // Limit max speed
    float speed = std::sqrt(bullet.data.velocity_x * bullet.data.velocity_x +
                           bullet.data.velocity_y * bullet.data.velocity_y);
    if (speed > bullet.data.max_speed) {
        float scale = bullet.data.max_speed / speed;
        bullet.data.velocity_x *= scale;
        bullet.data.velocity_y *= scale;
    }

    // Update position
    bullet.data.x += bullet.data.velocity_x * delta_time;
    bullet.data.y += bullet.data.velocity_y * delta_time;

    // Update rotation to match velocity direction
    if (bullet.data.type == BULLET_TRACER || bullet.data.type == BULLET_ROCKET) {
        bullet.data.rotation = std::atan2(bullet.data.velocity_y, bullet.data.velocity_x);
    }

    // Update lifetime
    bullet.data.lifetime_remaining -= delta_time;
}

void BulletSystem::update_bullet_rendering(Bullet& bullet) {
    if (!bullet.data.active) return;

    if (bullet.render_mode == RENDER_SPRITE) {
        // Update sprite position and properties
        sprite_move(bullet.render.sprite.sprite_id, bullet.data.x, bullet.data.y);
        sprite_rotate(bullet.render.sprite.sprite_id, bullet.data.rotation);
        sprite_scale(bullet.render.sprite.sprite_id, bullet.data.scale);
        sprite_alpha(bullet.render.sprite.sprite_id, bullet.data.alpha);
    }
    // Future: RENDER_INSTANCED would update instance buffer here
}

bool BulletSystem::is_bullet_off_screen(const Bullet& bullet) {
    // TODO: Use actual world bounds instead of hardcoded values
    const float SCREEN_WIDTH = 1024.0f;
    const float SCREEN_HEIGHT = 768.0f;
    const float MARGIN = 100.0f;  // Allow bullets slightly off-screen

    return (bullet.data.x < -MARGIN || bullet.data.x > SCREEN_WIDTH + MARGIN ||
            bullet.data.y < -MARGIN || bullet.data.y > SCREEN_HEIGHT + MARGIN);
}

void BulletSystem::update(float delta_time) {
    auto current_time = std::chrono::high_resolution_clock::now();
    auto frame_time = std::chrono::duration<float>(current_time - last_update_time);

    // Calculate physics FPS
    if (frame_time.count() > 0.0f) {
        stats.physics_fps = 1.0f / frame_time.count();
    }
    last_update_time = current_time;

    // Update all active bullets
    stats.active_bullets = 0;

    for (size_t i = 0; i < bullets.size(); ) {
        Bullet& bullet = bullets[i];

        if (!bullet.data.active) {
            // Remove inactive bullet
            destroy_bullet(bullet.data.id);
            continue;  // Don't increment i, as we removed an element
        }

        // Update physics
        update_bullet_physics(bullet, delta_time);

        // Check if bullet should be destroyed
        bool should_destroy = false;

        if (bullet.data.lifetime_remaining <= 0.0f) {
            should_destroy = true;
        } else if (is_bullet_off_screen(bullet)) {
            should_destroy = true;
        }

        if (should_destroy) {
            destroy_bullet(bullet.data.id);
            continue;  // Don't increment i
        }

        // Update rendering
        update_bullet_rendering(bullet);

        stats.active_bullets++;
        i++;
    }
}

bool BulletSystem::destroy_bullet(uint32_t bullet_id) {
    auto it = id_to_index.find(bullet_id);
    if (it == id_to_index.end()) {
        return false;  // Bullet not found
    }

    size_t index = it->second;
    if (index >= bullets.size()) {
        id_to_index.erase(it);
        return false;  // Invalid index
    }

    Bullet& bullet = bullets[index];

    // Clean up sprite resources
    if (bullet.render_mode == RENDER_SPRITE && bullet.render.sprite.sprite_allocated) {
        deallocate_sprite_id(bullet.render.sprite.sprite_id);
    }

    // Mark as inactive
    bullet.data.active = false;

    // Remove from bullets vector (swap with last element for O(1) removal)
    if (index < bullets.size() - 1) {
        std::swap(bullets[index], bullets.back());
        // Update index mapping for swapped element
        id_to_index[bullets[index].data.id] = index;
    }
    bullets.pop_back();

    // Clean up ID mapping
    id_to_index.erase(it);
    deallocate_bullet_id(bullet_id);

    // Update stats
    stats.total_destroyed++;

    return true;
}

void BulletSystem::clear_all_bullets() {
    std::cout << "BulletSystem: Clearing all bullets..." << std::endl;

    for (auto& bullet : bullets) {
        if (bullet.render_mode == RENDER_SPRITE && bullet.render.sprite.sprite_allocated) {
            deallocate_sprite_id(bullet.render.sprite.sprite_id);
        }
        deallocate_bullet_id(bullet.data.id);
    }

    bullets.clear();
    id_to_index.clear();

    // Clear ID queues
    std::queue<uint32_t> empty_bullet_queue;
    free_bullet_ids.swap(empty_bullet_queue);

    stats.active_bullets = 0;
    stats.total_destroyed += bullets.size();
}

std::vector<BulletCollision> BulletSystem::check_sprite_collisions(uint16_t sprite_id) {
    std::vector<BulletCollision> collisions;

    for (auto& bullet : bullets) {
        if (!bullet.data.active || bullet.data.owner_sprite_id == sprite_id) {
            continue;  // Skip inactive bullets or bullets from same owner
        }

        if (bullet.render_mode == RENDER_SPRITE) {
            // Use sprite collision detection
            if (sprite_check_collision(bullet.render.sprite.sprite_id, sprite_id)) {
                BulletCollision collision;
                collision.bullet_id = bullet.data.id;
                collision.hit_sprite_id = sprite_id;
                collision.hit_x = bullet.data.x;
                collision.hit_y = bullet.data.y;
                collision.damage = bullet.data.damage;
                collision.bullet_type = bullet.data.type;
                collision.bullet_destroyed = (bullet.data.type != BULLET_PIERCING || bullet.data.piercing_used);

                collisions.push_back(collision);

                // Handle piercing bullets
                if (bullet.data.type == BULLET_PIERCING && !bullet.data.piercing_used) {
                    bullet.data.piercing_used = true;  // Can pierce one more target
                } else {
                    bullet.data.active = false;  // Mark for destruction
                }

                stats.total_hits++;
            }
        }
        // Future: Add instanced collision detection here
    }

    stats.collision_checks_per_frame += bullets.size();
    return collisions;
}

std::vector<BulletCollision> BulletSystem::check_all_collisions() {
    std::vector<BulletCollision> all_collisions;

    // This is a simplified version - in a real game you'd check against
    // a list of target sprites rather than hardcoded IDs
    for (uint16_t sprite_id = 1; sprite_id < 200; sprite_id++) {
        // Check if sprite exists and is visible
        // (In real implementation, you'd have a sprite manager to query)

        auto collisions = check_sprite_collisions(sprite_id);
        all_collisions.insert(all_collisions.end(), collisions.begin(), collisions.end());
    }

    return all_collisions;
}

bool BulletSystem::is_bullet_active(uint32_t bullet_id) {
    auto it = id_to_index.find(bullet_id);
    if (it == id_to_index.end()) {
        return false;
    }

    size_t index = it->second;
    return (index < bullets.size() && bullets[index].data.active);
}

BulletData* BulletSystem::get_bullet_data(uint32_t bullet_id) {
    auto it = id_to_index.find(bullet_id);
    if (it == id_to_index.end()) {
        return nullptr;
    }

    size_t index = it->second;
    if (index >= bullets.size()) {
        return nullptr;
    }

    return &bullets[index].data;
}

BulletSystemStats BulletSystem::get_stats() const {
    return stats;
}

void BulletSystem::dump_debug_info() {
    std::cout << "\n=== BULLET SYSTEM DEBUG INFO ===" << std::endl;
    std::cout << "Active bullets: " << stats.active_bullets << std::endl;
    std::cout << "Total fired: " << stats.total_fired << std::endl;
    std::cout << "Total hits: " << stats.total_hits << std::endl;
    std::cout << "Total destroyed: " << stats.total_destroyed << std::endl;
    std::cout << "Physics FPS: " << stats.physics_fps << std::endl;
    std::cout << "Available sprite IDs: " << available_sprite_ids.size() << std::endl;
    std::cout << "Allocated sprite IDs: " << allocated_sprite_ids.size() << std::endl;
    std::cout << "==================================\n" << std::endl;
}

uint32_t BulletSystem::get_active_bullet_count() const {
    return stats.active_bullets;
}

void BulletSystem::set_world_bounds(float width, float height) {
    // TODO: Store world bounds and use for off-screen detection
    std::cout << "BulletSystem: Set world bounds to " << width << "x" << height << std::endl;
}

void BulletSystem::set_max_bullets(uint32_t max) {
    if (max > 0) {
        max_bullets = max;
        std::cout << "BulletSystem: Set max bullets to " << max_bullets << std::endl;
    }
}

void BulletSystem::render_sprites(void* encoder) {
    // Sprite rendering is handled automatically by the sprite system
    // This method is here for future instanced rendering integration
    stats.render_calls_per_frame = 1;  // One call per frame for sprite mode
}

void BulletSystem::render_instanced(void* encoder) {
    // TODO: Implement instanced rendering for high bullet counts
    // This will be used when switching to RENDER_INSTANCED mode
}

#pragma mark - C Interface Implementation

bool bullet_system_initialize(void* metal_device, void* sprite_layer) {
    if (g_bullet_system) {
        std::cout << "BulletSystem: WARNING - Already initialized" << std::endl;
        return true;
    }

    g_bullet_system = new BulletSystem();
    return g_bullet_system->initialize(metal_device, sprite_layer);
}

void bullet_system_shutdown() {
    if (g_bullet_system) {
        delete g_bullet_system;
        g_bullet_system = nullptr;
    }
}

void bullet_system_update(float delta_time) {
    if (g_bullet_system) {
        g_bullet_system->update(delta_time);
    }
}

void bullet_system_render() {
    if (g_bullet_system) {
        g_bullet_system->render_sprites(nullptr);
    }
}

uint32_t bullet_fire(float start_x, float start_y, float target_x, float target_y,
                     float speed, uint8_t damage, const char* texture_path) {
    if (!g_bullet_system) return 0;

    return g_bullet_system->fire_bullet_simple(start_x, start_y, target_x, target_y,
                                              speed, damage, texture_path);
}

uint32_t bullet_fire_advanced(float start_x, float start_y, float target_x, float target_y,
                              float speed, uint8_t damage, uint8_t bullet_type,
                              float lifetime, uint16_t owner_sprite, const char* texture_path) {
    if (!g_bullet_system) return 0;

    BulletFireParams params = {};
    params.start_x = start_x;
    params.start_y = start_y;
    params.target_x = target_x;
    params.target_y = target_y;
    params.speed = speed;
    params.damage = damage;
    params.type = static_cast<BulletType>(bullet_type);
    params.lifetime = lifetime;
    params.owner_sprite = owner_sprite;
    params.texture_path = texture_path;

    return g_bullet_system->fire_bullet(params);
}

bool bullet_destroy(uint32_t bullet_id) {
    if (!g_bullet_system) return false;
    return g_bullet_system->destroy_bullet(bullet_id);
}

void bullet_clear_all() {
    if (g_bullet_system) {
        g_bullet_system->clear_all_bullets();
    }
}

bool bullet_is_active(uint32_t bullet_id) {
    if (!g_bullet_system) return false;
    return g_bullet_system->is_bullet_active(bullet_id);
}

uint32_t bullet_get_active_count() {
    if (!g_bullet_system) return 0;
    return g_bullet_system->get_active_bullet_count();
}

void bullet_get_stats(BulletSystemStats* stats) {
    if (!g_bullet_system || !stats) return;
    *stats = g_bullet_system->get_stats();
}

uint32_t bullet_check_collisions(uint32_t* collision_buffer, uint32_t buffer_size) {
    if (!g_bullet_system || !collision_buffer) return 0;

    auto collisions = g_bullet_system->check_all_collisions();
    uint32_t count = std::min(static_cast<uint32_t>(collisions.size()), buffer_size);

    for (uint32_t i = 0; i < count; i++) {
        collision_buffer[i] = collisions[i].bullet_id;  // Simplified - just return bullet IDs
    }

    return count;
}

bool bullet_check_sprite_hit(uint16_t sprite_id) {
    if (!g_bullet_system) return false;

    auto collisions = g_bullet_system->check_sprite_collisions(sprite_id);
    return !collisions.empty();
}

void bullet_set_world_bounds(float width, float height) {
    if (g_bullet_system) {
        g_bullet_system->set_world_bounds(width, height);
    }
}

void bullet_set_max_count(uint32_t max_bullets) {
    if (g_bullet_system) {
        g_bullet_system->set_max_bullets(max_bullets);
    }
}
