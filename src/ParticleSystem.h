//
//  ParticleSystem.h
//  SuperTerminal Framework
//
//  Simplified Native Particle Explosion System (v2)
//  No background threads - physics runs in main loop
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#ifndef ParticleSystem_h
#define ParticleSystem_h

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <simd/simd.h>
#include <vector>
#include <map>

// Forward declarations
@class SuperTerminalSprite;

namespace SuperTerminal {

// Particle data structure - optimized for SIMD and GPU instancing
struct Particle {
    // Position and physics
    simd_float2 position;
    simd_float2 velocity;
    simd_float2 acceleration;
    
    // Rotation
    float rotation;
    float angularVelocity;
    
    // Visual properties
    float scale;
    float scaleVelocity;
    float alpha;
    float alphaDecay;
    
    // Texture coordinates (fragment of original sprite)
    simd_float2 texCoordMin;
    simd_float2 texCoordMax;
    
    // Lifecycle
    float lifetime;
    float maxLifetime;
    
    // Physics properties
    float mass;
    float drag;
    float bounce;
    
    // Visual effects
    simd_float4 color;
    float glowIntensity;
    
    // State
    bool active;
    uint16_t sourceSprite;
    
    // Constructor
    Particle() : position(simd_make_float2(0,0)), velocity(simd_make_float2(0,0)), 
                acceleration(simd_make_float2(0,0)), rotation(0), angularVelocity(0), 
                scale(1.0f), scaleVelocity(0), alpha(1.0f), alphaDecay(0.02f), 
                texCoordMin(simd_make_float2(0,0)), texCoordMax(simd_make_float2(1,1)),
                lifetime(0), maxLifetime(3.0f), mass(1.0f), drag(0.98f), bounce(0.3f),
                color(simd_make_float4(1,1,1,1)), glowIntensity(0), active(false), 
                sourceSprite(0) {}
};

// Explosion configuration
struct ExplosionConfig {
    uint16_t particleCount;
    float explosionForce;
    float gravityStrength;
    float fadeTime;
    simd_float2 directionalBias;
    float fragmentSizeMin;
    float fragmentSizeMax;
    float rotationSpeed;
    bool enableGlow;
    bool enableTrails;
    simd_float4 tintColor;
    
    ExplosionConfig() : particleCount(32), explosionForce(200.0f), gravityStrength(100.0f),
                       fadeTime(2.0f), directionalBias(simd_make_float2(0,0)), 
                       fragmentSizeMin(0.1f), fragmentSizeMax(0.3f), rotationSpeed(2.0f), 
                       enableGlow(false), enableTrails(false), 
                       tintColor(simd_make_float4(1,1,1,1)) {}
};

// Main particle system manager - SIMPLIFIED (no threads, no mutexes)
class ParticleSystem {
private:
    // Metal resources for instanced rendering
    id<MTLDevice> metalDevice;
    id<MTLCommandQueue> commandQueue;
    id<MTLRenderPipelineState> particlePipelineState;
    id<MTLBuffer> particleVertexBuffer;      // Base quad geometry
    id<MTLBuffer> particleInstanceBuffer;    // Instance data for particles
    id<MTLBuffer> particleUniformBuffer;
    id<MTLSamplerState> particleSampler;
    
    // Particle storage - NO MUTEX, single-threaded access
    std::vector<Particle> particles;
    size_t maxParticles;
    
    // Sprite texture cache for fragmentation
    std::map<uint16_t, id<MTLTexture>> spriteTextures;
    
    // Configuration
    bool systemEnabled;
    float globalTimeScale;
    simd_float2 gravity;
    simd_float2 worldBounds;
    
    // Performance monitoring
    uint64_t totalParticlesCreated;
    uint64_t activeExplosions;
    
    // Internal methods
    bool initializeMetalResources();
    void createParticlesFromSprite(uint16_t spriteId, const ExplosionConfig& config);
    std::vector<simd_float2> generateFragmentTexCoords(uint16_t particleCount, 
                                                       float minSize, float maxSize);
    void cacheSprite(uint16_t spriteId, SuperTerminalSprite* sprite);
    
    // Physics methods (called from update, not background thread)
    void updateParticlePhysics(Particle& particle, float deltaTime);
    void handleCollisions(Particle& particle);
    
public:
    ParticleSystem(id<MTLDevice> device, size_t maxParticles = 8192);
    ~ParticleSystem();
    
    // Initialization
    bool initialize();
    void shutdown();
    
    // Main update - CALLED FROM MAIN LOOP (not background thread)
    void update(float deltaTime);
    
    // Explosion API
    bool explodeSprite(uint16_t spriteId, SuperTerminalSprite* sprite);
    bool explodeSpriteAdvanced(uint16_t spriteId, SuperTerminalSprite* sprite, 
                              const ExplosionConfig& config);
    bool explodeSpriteDirectional(uint16_t spriteId, SuperTerminalSprite* sprite,
                                 float forceX, float forceY, uint16_t particleCount);
    
    // System control
    void pause();
    void resume();
    void clear();
    void setTimeScale(float scale);
    void setWorldBounds(float width, float height);
    
    // Rendering integration
    void render(id<MTLRenderCommandEncoder> encoder, simd_float4x4 projectionMatrix);
    
    // Performance and debugging
    size_t getActiveParticleCount() const;
    uint64_t getTotalParticlesCreated() const { return totalParticlesCreated; }
    void dumpSystemStats();
    
    // Configuration
    bool isEnabled() const { return systemEnabled; }
    void setEnabled(bool enabled);
};

// Vertex structure for particle rendering (base quad geometry)
struct ParticleVertex {
    simd_float2 position;
    simd_float2 texCoord;
    simd_float4 color;
    float scale;
    float rotation;
};

// Instance data structure for particle instanced rendering
struct ParticleInstanceData {
    simd_float2 position;
    simd_float2 velocity;
    simd_float2 texCoordMin;
    simd_float2 texCoordMax;
    simd_float4 color;
    float scale;
    float rotation;
    float alpha;
    float lifetime;
    float glowIntensity;
};

// Uniform buffer for particle shader
struct ParticleUniforms {
    simd_float4x4 projectionMatrix;
    simd_float2 screenSize;
    float time;
    float globalScale;
};

} // namespace SuperTerminal

// C API - defined in same file as implementation
extern "C" {
    // System management
    bool particle_system_initialize(void* metalDevice);
    void particle_system_shutdown();
    void particle_system_update(float deltaTime);
    void particle_system_render(void* encoder, simd_float4x4 projectionMatrix);
    
    // Explosion functions
    bool sprite_explode(uint16_t spriteId, uint16_t particleCount);
    bool sprite_explode_advanced(uint16_t spriteId, uint16_t particleCount, 
                                float explosionForce, float gravity, float fadeTime);
    bool sprite_explode_directional(uint16_t spriteId, uint16_t particleCount,
                                   float forceX, float forceY);
    
    // System control
    void particle_system_pause();
    void particle_system_resume();
    void particle_system_clear();
    void particle_system_set_time_scale(float scale);
    void particle_system_set_world_bounds(float width, float height);
    void particle_system_set_enabled(bool enabled);
    
    // Queries
    uint32_t particle_system_get_active_count();
    uint64_t particle_system_get_total_created();
    void particle_system_dump_stats();
    
    // Readiness check
    bool particle_system_is_ready();
}

#endif /* ParticleSystem_h */