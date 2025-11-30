//
//  ParticleSystem.h
//  SuperTerminal Framework
//
//  Native Threaded Particle Explosion System
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#ifndef ParticleSystem_h
#define ParticleSystem_h

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <simd/simd.h>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <memory>
#include <map>

// Forward declarations
@class SuperTerminalSprite;

namespace SuperTerminal {

// Particle data structure - optimized for SIMD operations
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
    Particle() : position(simd_make_float2(0,0)), velocity(simd_make_float2(0,0)), acceleration(simd_make_float2(0,0)),
                rotation(0), angularVelocity(0), scale(1.0f), scaleVelocity(0),
                alpha(1.0f), alphaDecay(0.02f), texCoordMin(simd_make_float2(0,0)), texCoordMax(simd_make_float2(1,1)),
                lifetime(0), maxLifetime(3.0f), mass(1.0f), drag(0.98f), bounce(0.3f),
                color(simd_make_float4(1,1,1,1)), glowIntensity(0), active(false), sourceSprite(0) {}
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
                       fadeTime(2.0f), directionalBias(simd_make_float2(0,0)), fragmentSizeMin(0.1f),
                       fragmentSizeMax(0.3f), rotationSpeed(2.0f), enableGlow(false),
                       enableTrails(false), tintColor(simd_make_float4(1,1,1,1)) {}
};

// Thread-safe particle buffer for communication between physics and render threads
class ParticleBuffer {
private:
    std::vector<Particle> particles;
    std::mutex bufferMutex;
    std::atomic<size_t> activeCount;
    
public:
    ParticleBuffer(size_t maxParticles = 2048);
    ~ParticleBuffer();
    
    // Thread-safe operations
    void addParticle(const Particle& particle);
    void updateParticle(size_t index, const Particle& particle);
    void removeParticle(size_t index);
    void clear();
    
    // Batch operations for efficiency
    std::vector<Particle> getActiveParticles();
    void updateActiveParticles(const std::vector<Particle>& updatedParticles);
    
    size_t getActiveCount() const { return activeCount.load(); }
    size_t getMaxCapacity() const { return particles.size(); }
};

// Physics simulation thread
class ParticlePhysicsThread {
private:
    std::thread physicsThread;
    std::atomic<bool> running;
    std::atomic<bool> paused;
    std::shared_ptr<ParticleBuffer> particleBuffer;
    
    // Physics parameters
    simd_float2 gravity;
    float deltaTime;
    float timeAccumulator;
    
    // Performance tracking
    std::atomic<uint64_t> simulationSteps;
    std::atomic<float> averageFrameTime;
    
    // Thread function
    void physicsLoop();
    void updateParticlePhysics(Particle& particle);
    void handleCollisions(Particle& particle);
    
public:
    ParticlePhysicsThread(std::shared_ptr<ParticleBuffer> buffer);
    ~ParticlePhysicsThread();
    
    void start();
    void stop();
    void pause();
    void resume();
    
    // Physics configuration
    void setGravity(simd_float2 newGravity);
    void setTimeStep(float dt);
    
    // Performance monitoring
    uint64_t getSimulationSteps() const { return simulationSteps.load(); }
    float getAverageFrameTime() const { return averageFrameTime.load(); }
    bool isRunning() const { return running.load(); }
};

// Main particle system manager
class ParticleSystem {
private:
    // Metal resources
    id<MTLDevice> metalDevice;
    id<MTLCommandQueue> commandQueue;
    id<MTLRenderPipelineState> particlePipelineState;
    id<MTLBuffer> particleVertexBuffer;      // Base quad geometry
    id<MTLBuffer> particleInstanceBuffer;    // Instance data for particles
    id<MTLBuffer> particleUniformBuffer;
    id<MTLSamplerState> particleSampler;
    
    // Particle management
    std::shared_ptr<ParticleBuffer> particleBuffer;
    std::unique_ptr<ParticlePhysicsThread> physicsThread;
    
    // Sprite texture cache for fragmentation
    std::map<uint16_t, id<MTLTexture>> spriteTextures;
    std::mutex textureCacheMutex;
    
    // Configuration
    bool systemEnabled;
    float globalTimeScale;
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
    
public:
    ParticleSystem(id<MTLDevice> device);
    ~ParticleSystem();
    
    // Initialization
    bool initialize();
    void shutdown();
    
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
    void updateRenderData();
    
    // Performance and debugging
    size_t getActiveParticleCount() const;
    uint64_t getTotalParticlesCreated() const { return totalParticlesCreated; }
    float getPhysicsFrameRate() const;
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



#endif /* ParticleSystem_h */