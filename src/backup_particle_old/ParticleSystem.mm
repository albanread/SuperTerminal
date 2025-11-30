//
//  ParticleSystem.mm
//  SuperTerminal Framework
//
//  Native Threaded Particle Explosion System Implementation
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import "ParticleSystem.h"
#import <Foundation/Foundation.h>
#import <random>
#import <chrono>
#import <algorithm>
#include "GlobalShutdown.h"

// Debug logging control
#define PARTICLE_DEBUG_LOGGING 0
#if PARTICLE_DEBUG_LOGGING
    #define PARTICLE_LOG(...) NSLog(__VA_ARGS__)
#else
    #define PARTICLE_LOG(...) do {} while(0)
#endif

// SuperTerminalSprite interface definition for implementation file
@interface SuperTerminalSprite : NSObject
@property (nonatomic, assign) uint16_t spriteId;
@property (nonatomic, strong) id<MTLTexture> texture;
@property (nonatomic, assign) float x, y;
@property (nonatomic, assign) float scale;
@property (nonatomic, assign) float rotation;
@property (nonatomic, assign) float alpha;
@property (nonatomic, assign) BOOL visible;
@property (nonatomic, assign) BOOL loaded;
@end

// External sprite interface
extern "C" {
    extern SuperTerminalSprite* sprite_layer_get_sprite(uint16_t id);
    extern void* superterminal_get_metal_device(void);
    extern void sprite_layer_init(void* device);

    // Helper functions to access sprite properties (avoids direct property access in C++)
    float sprite_get_x(SuperTerminalSprite* sprite);
    float sprite_get_y(SuperTerminalSprite* sprite);
    float sprite_get_scale(SuperTerminalSprite* sprite);
    float sprite_get_alpha(SuperTerminalSprite* sprite);
    bool sprite_is_loaded(SuperTerminalSprite* sprite);
    void* sprite_get_texture(SuperTerminalSprite* sprite); // id<MTLTexture>
}

// Helper function implementations
float sprite_get_x(SuperTerminalSprite* sprite) {
    return sprite ? sprite.x : 0.0f;
}

float sprite_get_y(SuperTerminalSprite* sprite) {
    return sprite ? sprite.y : 0.0f;
}

float sprite_get_scale(SuperTerminalSprite* sprite) {
    return sprite ? sprite.scale : 1.0f;
}

float sprite_get_alpha(SuperTerminalSprite* sprite) {
    return sprite ? sprite.alpha : 1.0f;
}

bool sprite_is_loaded(SuperTerminalSprite* sprite) {
    return sprite ? sprite.loaded : false;
}

void* sprite_get_texture(SuperTerminalSprite* sprite) {
    return sprite ? (__bridge void*)sprite.texture : nullptr;
}

namespace SuperTerminal {

#pragma mark - ParticleBuffer Implementation

ParticleBuffer::ParticleBuffer(size_t maxParticles) : activeCount(0) {
    particles.resize(maxParticles);
    NSLog(@"ParticleBuffer: Initialized with capacity for %zu particles", maxParticles);
}

ParticleBuffer::~ParticleBuffer() {
    clear();
    NSLog(@"ParticleBuffer: Destroyed");
}

void ParticleBuffer::addParticle(const Particle& particle) {
    // Check for emergency shutdown before acquiring lock
    if (is_emergency_shutdown_requested()) {
        return;
    }

    try {
        std::lock_guard<std::mutex> lock(bufferMutex);

        // Find first inactive slot
        for (size_t i = 0; i < particles.size(); ++i) {
            if (!particles[i].active) {
                particles[i] = particle;
                particles[i].active = true;
                activeCount++;
                return;
            }
        }
    } catch (const std::exception& e) {
        NSLog(@"ParticleBuffer: Exception in addParticle: %s", e.what());
    }
}

void ParticleBuffer::updateParticle(size_t index, const Particle& particle) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    if (index < particles.size()) {
        particles[index] = particle;
    }
}

void ParticleBuffer::removeParticle(size_t index) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    if (index < particles.size() && particles[index].active) {
        particles[index].active = false;
        activeCount--;
    }
}

void ParticleBuffer::clear() {
    std::lock_guard<std::mutex> lock(bufferMutex);
    for (auto& particle : particles) {
        particle.active = false;
    }
    activeCount = 0;
}

std::vector<Particle> ParticleBuffer::getActiveParticles() {
    // Check for emergency shutdown before acquiring lock
    if (is_emergency_shutdown_requested()) {
        return std::vector<Particle>();
    }

    try {
        std::lock_guard<std::mutex> lock(bufferMutex);
        std::vector<Particle> activeParticles;
        activeParticles.reserve(activeCount.load());

        for (const auto& particle : particles) {
            if (particle.active) {
                activeParticles.push_back(particle);
            }
        }
        return activeParticles;
    } catch (const std::exception& e) {
        NSLog(@"ParticleBuffer: Exception in getActiveParticles: %s", e.what());
        return std::vector<Particle>();
    }
}

void ParticleBuffer::updateActiveParticles(const std::vector<Particle>& updatedParticles) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    size_t updatedIndex = 0;

    for (size_t i = 0; i < particles.size() && updatedIndex < updatedParticles.size(); ++i) {
        if (particles[i].active) {
            particles[i] = updatedParticles[updatedIndex];
            if (!particles[i].active) {
                activeCount--;
            }
            updatedIndex++;
        }
    }
}

#pragma mark - ParticlePhysicsThread Implementation

ParticlePhysicsThread::ParticlePhysicsThread(std::shared_ptr<ParticleBuffer> buffer)
    : running(false), paused(false), particleBuffer(buffer),
      gravity(simd_make_float2(0.0f, -98.0f)), deltaTime(1.0f/120.0f), timeAccumulator(0.0f),
      simulationSteps(0), averageFrameTime(0.0f) {
}

ParticlePhysicsThread::~ParticlePhysicsThread() {
    stop();
}

void ParticlePhysicsThread::start() {
    if (running.load()) return;

    running = true;
    paused = false;
    physicsThread = std::thread(&ParticlePhysicsThread::physicsLoop, this);
    NSLog(@"ParticlePhysicsThread: Started physics simulation thread");
}

void ParticlePhysicsThread::stop() {
    if (!running.load()) return;

    NSLog(@"ParticlePhysicsThread: Requesting physics thread stop...");
    running = false;

    if (physicsThread.joinable()) {
        try {
            // Simple blocking join - the loop checks running flag every 500us
            physicsThread.join();
            NSLog(@"ParticlePhysicsThread: Physics thread joined successfully");
        } catch (const std::exception& e) {
            NSLog(@"ParticlePhysicsThread: Exception during thread join: %s", e.what());
        }
    }
    NSLog(@"ParticlePhysicsThread: Stop sequence complete");
}

void ParticlePhysicsThread::pause() {
    paused = true;
}

void ParticlePhysicsThread::resume() {
    paused = false;
}

void ParticlePhysicsThread::setGravity(simd_float2 newGravity) {
    gravity = newGravity;
}

void ParticlePhysicsThread::setTimeStep(float dt) {
    deltaTime = dt;
}

void ParticlePhysicsThread::physicsLoop() {
    auto lastTime = std::chrono::high_resolution_clock::now();
    float frameTimeAccumulator = 0.0f;
    int frameCount = 0;

    while (running.load()) {
        // Check for emergency shutdown at the start of each loop iteration
        if (is_emergency_shutdown_requested()) {
            NSLog(@"PhysicsThread: Emergency shutdown detected, terminating physics thread");
            break;
        }

        auto currentTime = std::chrono::high_resolution_clock::now();
        auto deltaTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime - lastTime);
        float frameTime = deltaTimeNs.count() / 1e9f;
        lastTime = currentTime;

        if (!paused.load()) {
            timeAccumulator += frameTime;

            // Fixed timestep physics simulation
            while (timeAccumulator >= deltaTime) {
                // Get active particles from buffer
                auto activeParticles = particleBuffer->getActiveParticles();

                // Update physics for each particle
                size_t particlesBeforeUpdate = activeParticles.size();
                size_t activeAfterUpdate = 0;
                for (auto& particle : activeParticles) {
                    updateParticlePhysics(particle);
                    handleCollisions(particle);
                    if (particle.active) activeAfterUpdate++;
                }

                // Write updated particles back to buffer
                particleBuffer->updateActiveParticles(activeParticles);

                if (particlesBeforeUpdate > 0) {
                    NSLog(@"PhysicsThread: Updated %zu particles, %zu still active",
                          particlesBeforeUpdate, activeAfterUpdate);
                }

                timeAccumulator -= deltaTime;
                simulationSteps++;
            }
        }

        // Calculate average frame time
        frameTimeAccumulator += frameTime;
        frameCount++;
        if (frameCount >= 60) {
            averageFrameTime = frameTimeAccumulator / frameCount;
            frameTimeAccumulator = 0.0f;
            frameCount = 0;
        }

        // Sleep briefly to prevent 100% CPU usage
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

void ParticlePhysicsThread::updateParticlePhysics(Particle& particle) {
    if (!particle.active) return;

    // Update lifetime
    particle.lifetime += deltaTime;
    if (particle.lifetime >= particle.maxLifetime) {
        particle.active = false;
        return;
    }

    // Apply gravity
    particle.acceleration = gravity;

    // Update velocity (Verlet integration)
    particle.velocity += particle.acceleration * deltaTime;
    particle.velocity *= particle.drag; // Apply drag

    // Update position
    particle.position += particle.velocity * deltaTime;

    // Update rotation
    particle.rotation += particle.angularVelocity * deltaTime;

    // Update scale
    particle.scale += particle.scaleVelocity * deltaTime;
    if (particle.scale < 0.01f) {
        particle.active = false;
        return;
    }

    // Update alpha (fade out)
    float lifeRatio = particle.lifetime / particle.maxLifetime;
    particle.alpha = 1.0f - (lifeRatio * lifeRatio); // Quadratic fade
    if (particle.alpha <= 0.0f) {
        particle.active = false;
        return;
    }

    // Update color alpha
    particle.color.w = particle.alpha;
}

void ParticlePhysicsThread::handleCollisions(Particle& particle) {
    // Simple ground collision (y = 0)
    if (particle.position.y <= 0.0f && particle.velocity.y < 0.0f) {
        particle.position.y = 0.0f;
        particle.velocity.y = -particle.velocity.y * particle.bounce;
        particle.velocity.x *= 0.8f; // Friction

        // Add some random bounce variation
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<float> dis(-10.0f, 10.0f);
        particle.velocity.x += dis(gen);
    }

    // Remove particles that go too far off screen
    if (particle.position.x < -200.0f || particle.position.x > 1200.0f ||
        particle.position.y < -200.0f || particle.position.y > 1000.0f) {
        particle.active = false;
    }
}

#pragma mark - ParticleSystem Implementation

ParticleSystem::ParticleSystem(id<MTLDevice> device)
    : metalDevice(device), commandQueue(nil), particlePipelineState(nil),
      particleVertexBuffer(nil), particleInstanceBuffer(nil), particleUniformBuffer(nil), particleSampler(nil),
      systemEnabled(true), globalTimeScale(1.0f), worldBounds(simd_make_float2(1024.0f, 768.0f)),
      totalParticlesCreated(0), activeExplosions(0) {

    PARTICLE_LOG(@"ParticleSystem: Constructor called with device: %@", device);
    particleBuffer = std::make_shared<ParticleBuffer>(8192);
    PARTICLE_LOG(@"ParticleSystem: Particle buffer created");
    physicsThread = std::make_unique<ParticlePhysicsThread>(particleBuffer);
    PARTICLE_LOG(@"ParticleSystem: Physics thread created");
}

ParticleSystem::~ParticleSystem() {
    shutdown();
}

bool ParticleSystem::initialize() {
    PARTICLE_LOG(@"ParticleSystem: Initializing native particle system...");

    // Initialize sprite layer first (needed for sprite explosions)
    PARTICLE_LOG(@"ParticleSystem: Initializing sprite layer...");
    sprite_layer_init((__bridge void*)metalDevice);

    PARTICLE_LOG(@"ParticleSystem: About to initialize Metal resources");
    if (!initializeMetalResources()) {
        PARTICLE_LOG(@"ParticleSystem: Failed to initialize Metal resources");
        return false;
    }
    PARTICLE_LOG(@"ParticleSystem: Metal resources initialized successfully");

    // Start physics thread (only if we have valid resources)
    if (particlePipelineState) {
        PARTICLE_LOG(@"ParticleSystem: Starting physics thread");
        physicsThread->start();
        PARTICLE_LOG(@"ParticleSystem: Physics thread started");
    } else {
        PARTICLE_LOG(@"ParticleSystem: Particle rendering disabled, physics thread not started");
    }

    PARTICLE_LOG(@"ParticleSystem: Initialization complete");
    return true;
}

void ParticleSystem::shutdown() {
    PARTICLE_LOG(@"ParticleSystem: Shutting down particle system...");

    // Stop physics thread with timeout protection
    if (physicsThread) {
        try {
            physicsThread->stop();
        } catch (const std::exception& e) {
            NSLog(@"ParticleSystem: Exception during physics thread stop: %s", e.what());
        }
        physicsThread.reset(); // Release the thread object
    }

    // Clear all particles (with mutex protection)
    if (particleBuffer) {
        try {
            particleBuffer->clear();
        } catch (const std::exception& e) {
            NSLog(@"ParticleSystem: Exception during buffer clear: %s", e.what());
        }
    }

    // Release Metal resources
    particlePipelineState = nil;
    particleVertexBuffer = nil;
    particleInstanceBuffer = nil;
    particleUniformBuffer = nil;
    particleSampler = nil;
    commandQueue = nil;

    // Clear texture cache with timeout protection
    try {
        std::lock_guard<std::mutex> lock(textureCacheMutex);
        spriteTextures.clear();
    } catch (const std::exception& e) {
        NSLog(@"ParticleSystem: Exception during texture cache clear: %s", e.what());
        // Force clear without lock if needed
        spriteTextures.clear();
    }

    PARTICLE_LOG(@"ParticleSystem: Shutdown complete");
}

bool ParticleSystem::initializeMetalResources() {
    PARTICLE_LOG(@"ParticleSystem: Creating command queue");
    // Create command queue
    commandQueue = [metalDevice newCommandQueue];
    if (!commandQueue) {
        PARTICLE_LOG(@"ParticleSystem: Failed to create command queue");
        return false;
    }
    PARTICLE_LOG(@"ParticleSystem: Command queue created successfully");

    // Create vertex buffer for base quad geometry (instanced rendering)
    particleVertexBuffer = [metalDevice newBufferWithLength:sizeof(ParticleVertex) * 6
                                                     options:MTLResourceStorageModeShared];
    if (!particleVertexBuffer) {
        PARTICLE_LOG(@"ParticleSystem: Failed to create vertex buffer");
        return false;
    }

    // Initialize base quad geometry (single quad that will be instanced)
    ParticleVertex* quadVertices = (ParticleVertex*)[particleVertexBuffer contents];

    // Triangle 1
    quadVertices[0] = {{-1.0f, -1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f};
    quadVertices[1] = {{ 1.0f, -1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f};
    quadVertices[2] = {{-1.0f,  1.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f};

    // Triangle 2
    quadVertices[3] = {{ 1.0f, -1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f};
    quadVertices[4] = {{ 1.0f,  1.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f};
    quadVertices[5] = {{-1.0f,  1.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f};

    // Create instance buffer for particle data
    particleInstanceBuffer = [metalDevice newBufferWithLength:sizeof(ParticleInstanceData) * 8192
                                                      options:MTLResourceStorageModeShared];
    if (!particleInstanceBuffer) {
        PARTICLE_LOG(@"ParticleSystem: Failed to create instance buffer");
        return false;
    }

    // Create uniform buffer
    particleUniformBuffer = [metalDevice newBufferWithLength:sizeof(ParticleUniforms)
                                                      options:MTLResourceStorageModeShared];
    if (!particleUniformBuffer) {
        PARTICLE_LOG(@"ParticleSystem: Failed to create uniform buffer");
        return false;
    }

    // Create sampler state
    MTLSamplerDescriptor *samplerDesc = [[MTLSamplerDescriptor alloc] init];
    samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
    samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
    samplerDesc.mipFilter = MTLSamplerMipFilterNotMipmapped;
    samplerDesc.maxAnisotropy = 1;
    samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDesc.rAddressMode = MTLSamplerAddressModeClampToEdge;
    particleSampler = [metalDevice newSamplerStateWithDescriptor:samplerDesc];

    // Create particle render pipeline state
    NSBundle *frameworkBundle = [NSBundle bundleWithIdentifier:@"com.superterminal.SuperTerminal"];
    if (!frameworkBundle) {
        frameworkBundle = [NSBundle mainBundle];
    }

    NSURL *shaderURL = [frameworkBundle URLForResource:@"Particle" withExtension:@"metallib"];

    // Try build directory if not found in bundle
    if (!shaderURL) {
        NSString *currentDir = [[NSFileManager defaultManager] currentDirectoryPath];
        NSArray *searchPaths = @[
            [currentDir stringByAppendingPathComponent:@"Particle.metallib"],           // Current directory
            [currentDir stringByAppendingPathComponent:@"build/Particle.metallib"],     // Relative build
            [[currentDir stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"build/Particle.metallib"], // Parent/build
            [currentDir stringByAppendingPathComponent:@"../build/Particle.metallib"]   // Relative parent/build
        ];

        NSString *foundPath = nil;
        for (NSString *path in searchPaths) {
            NSString *resolvedPath = [path stringByStandardizingPath];
            if ([[NSFileManager defaultManager] fileExistsAtPath:resolvedPath]) {
                foundPath = resolvedPath;
                PARTICLE_LOG(@"ParticleSystem: Found Particle.metallib at: %@", resolvedPath);
                break;
            }
        }

        if (foundPath) {
            shaderURL = [NSURL fileURLWithPath:foundPath];
        } else {
            PARTICLE_LOG(@"ParticleSystem: Warning - Could not find Particle.metallib in any search path");
            PARTICLE_LOG(@"ParticleSystem: Searched paths:");
            for (NSString *path in searchPaths) {
                PARTICLE_LOG(@"  - %@", [path stringByStandardizingPath]);
            }
            // Return true but with null pipeline - system will work but won't render
            particlePipelineState = nil;
            return true;
        }
    }

    NSError *error = nil;
    id<MTLLibrary> library = [metalDevice newLibraryWithURL:shaderURL error:&error];
    if (!library) {
        PARTICLE_LOG(@"ParticleSystem: Failed to load particle shader library: %@", error.localizedDescription);
        return false;
    }

    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"particle_vertex"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"particle_glow_fragment"];

    if (!vertexFunction || !fragmentFunction) {
        PARTICLE_LOG(@"ParticleSystem: Failed to find particle shader functions");
        return false;
    }

    MTLRenderPipelineDescriptor *pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDesc.vertexFunction = vertexFunction;
    pipelineDesc.fragmentFunction = fragmentFunction;

    // Create vertex descriptor for instanced rendering
    MTLVertexDescriptor *vertexDesc = [[MTLVertexDescriptor alloc] init];

    // Buffer 0: Base quad geometry (per-vertex attributes)
    // Position attribute (attribute 0)
    vertexDesc.attributes[0].format = MTLVertexFormatFloat2;
    vertexDesc.attributes[0].offset = 0;
    vertexDesc.attributes[0].bufferIndex = 0;

    // TexCoord attribute (attribute 1)
    vertexDesc.attributes[1].format = MTLVertexFormatFloat2;
    vertexDesc.attributes[1].offset = 8;
    vertexDesc.attributes[1].bufferIndex = 0;

    // Color attribute (attribute 2) - from base vertex (will be overridden by instance data in shader)
    vertexDesc.attributes[2].format = MTLVertexFormatFloat4;
    vertexDesc.attributes[2].offset = 16;
    vertexDesc.attributes[2].bufferIndex = 0;

    // Scale attribute (attribute 3) - from base vertex (will be overridden by instance data in shader)
    vertexDesc.attributes[3].format = MTLVertexFormatFloat;
    vertexDesc.attributes[3].offset = 32;
    vertexDesc.attributes[3].bufferIndex = 0;

    // Rotation attribute (attribute 4) - from base vertex (will be overridden by instance data in shader)
    vertexDesc.attributes[4].format = MTLVertexFormatFloat;
    vertexDesc.attributes[4].offset = 36;
    vertexDesc.attributes[4].bufferIndex = 0;

    // Buffer 0 layout: Base quad geometry (per-vertex)
    vertexDesc.layouts[0].stride = sizeof(ParticleVertex);
    vertexDesc.layouts[0].stepRate = 1;
    vertexDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    // Note: Instance data is accessed directly in shader via buffer(2), not through vertex descriptor
    // No need to set up vertex descriptor layout for instance buffer

    pipelineDesc.vertexDescriptor = vertexDesc;
    pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pipelineDesc.colorAttachments[0].blendingEnabled = YES;
    pipelineDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipelineDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    pipelineDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    particlePipelineState = [metalDevice newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
    if (!particlePipelineState) {
        PARTICLE_LOG(@"ParticleSystem: Failed to create particle pipeline state: %@", error.localizedDescription);
        return false;
    }

    PARTICLE_LOG(@"ParticleSystem: Metal resources initialized with hardware-accelerated shaders");
    return true;
}

bool ParticleSystem::explodeSprite(uint16_t spriteId, SuperTerminalSprite* sprite) {
    ExplosionConfig config;
    return explodeSpriteAdvanced(spriteId, sprite, config);
}

bool ParticleSystem::explodeSpriteAdvanced(uint16_t spriteId, SuperTerminalSprite* sprite,
                                          const ExplosionConfig& config) {
    PARTICLE_LOG(@"ParticleSystem::explodeSpriteAdvanced: Called with spriteId=%d", spriteId);

    if (!systemEnabled) {
        PARTICLE_LOG(@"ParticleSystem::explodeSpriteAdvanced: FAILED - System not enabled");
        return false;
    }

    if (!sprite) {
        PARTICLE_LOG(@"ParticleSystem::explodeSpriteAdvanced: FAILED - Sprite is null");
        return false;
    }

    if (!sprite_is_loaded(sprite)) {
        PARTICLE_LOG(@"ParticleSystem::explodeSpriteAdvanced: FAILED - Sprite not loaded");
        return false;
    }

    // Skip if no rendering capability (missing shaders)
    if (!particlePipelineState) {
        PARTICLE_LOG(@"ParticleSystem: Explosion skipped - no shader pipeline available");
        return false;
    }

    PARTICLE_LOG(@"ParticleSystem::explodeSpriteAdvanced: All checks passed, creating explosion");

    // Cache sprite texture for rendering
    cacheSprite(spriteId, sprite);

    // Create particles from the sprite
    createParticlesFromSprite(spriteId, config);

    totalParticlesCreated += config.particleCount;
    activeExplosions++;

    PARTICLE_LOG(@"ParticleSystem::explodeSpriteAdvanced: Explosion created successfully");
    return true;
}

bool ParticleSystem::explodeSpriteDirectional(uint16_t spriteId, SuperTerminalSprite* sprite,
                                             float forceX, float forceY, uint16_t particleCount) {
    ExplosionConfig config;
    config.particleCount = particleCount;
    config.directionalBias = {forceX, forceY};
    config.explosionForce = sqrt(forceX * forceX + forceY * forceY);

    return explodeSpriteAdvanced(spriteId, sprite, config);
}

void ParticleSystem::createParticlesFromSprite(uint16_t spriteId, const ExplosionConfig& config) {
    SuperTerminalSprite* sprite = sprite_layer_get_sprite(spriteId);
    if (!sprite) return;

    // Random number generation
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * M_PI);
    std::uniform_real_distribution<float> forceDist(0.5f, 1.5f);
    std::uniform_real_distribution<float> sizeDist(config.fragmentSizeMin, config.fragmentSizeMax);
    std::uniform_real_distribution<float> lifeDist(config.fadeTime * 0.8f, config.fadeTime * 1.2f);
    std::uniform_real_distribution<float> rotDist(-config.rotationSpeed, config.rotationSpeed);

    // Generate fragment texture coordinates
    auto fragCoords = generateFragmentTexCoords(config.particleCount,
                                               config.fragmentSizeMin, config.fragmentSizeMax);

    // Create particles
    for (uint16_t i = 0; i < config.particleCount; ++i) {
        Particle particle;

        // Start at sprite position
        particle.position = simd_make_float2(sprite_get_x(sprite), sprite_get_y(sprite));

        // Random explosion direction
        float angle = angleDist(gen);
        float force = config.explosionForce * forceDist(gen);

        // Apply directional bias
        simd_float2 direction = {cos(angle), sin(angle)};
        direction += config.directionalBias * 0.3f; // 30% bias influence
        direction = simd_normalize(direction);

        particle.velocity = direction * force;
        particle.acceleration = {0.0f, 0.0f};

        // Random rotation
        particle.rotation = angleDist(gen);
        particle.angularVelocity = rotDist(gen);

        // Random scale
        particle.scale = sizeDist(gen) * sprite_get_scale(sprite);
        particle.scaleVelocity = -particle.scale / config.fadeTime; // Shrink over time

        // Set alpha and fade
        particle.alpha = sprite_get_alpha(sprite);
        particle.alphaDecay = 1.0f / config.fadeTime;

        // Fragment texture coordinates
        if (i < fragCoords.size()) {
            float fragSize = sizeDist(gen);
            simd_float2 center = fragCoords[i];
            particle.texCoordMin = center - simd_float2{fragSize * 0.5f, fragSize * 0.5f};
            particle.texCoordMax = center + simd_float2{fragSize * 0.5f, fragSize * 0.5f};

            // Clamp to texture bounds
            particle.texCoordMin = simd_clamp(particle.texCoordMin, simd_float2{0,0}, simd_float2{1,1});
            particle.texCoordMax = simd_clamp(particle.texCoordMax, simd_float2{0,0}, simd_float2{1,1});
        }

        // Set lifetime
        particle.maxLifetime = lifeDist(gen);
        particle.lifetime = 0.0f;

        // Physics properties
        particle.mass = 1.0f;
        particle.drag = 0.995f;
        particle.bounce = 0.4f;

        // Color and effects
        particle.color = config.tintColor;
        particle.glowIntensity = config.enableGlow ? 0.5f : 0.0f;

        // State
        particle.active = true;
        particle.sourceSprite = spriteId;

        // Add to buffer
        particleBuffer->addParticle(particle);
    }

    PARTICLE_LOG(@"ParticleSystem: Created %d particles, buffer now has %zu active particles",
          config.particleCount, particleBuffer->getActiveCount());
}

std::vector<simd_float2> ParticleSystem::generateFragmentTexCoords(uint16_t particleCount,
                                                                   float minSize, float maxSize) {
    std::vector<simd_float2> coords;
    coords.reserve(particleCount);

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> coordDist(0.0f, 1.0f);

    // Generate random texture coordinate centers for fragments
    for (uint16_t i = 0; i < particleCount; ++i) {
        coords.push_back(simd_make_float2(coordDist(gen), coordDist(gen)));
    }

    return coords;
}

void ParticleSystem::cacheSprite(uint16_t spriteId, SuperTerminalSprite* sprite) {
    // Check for emergency shutdown before acquiring lock
    if (is_emergency_shutdown_requested()) {
        return;
    }

    try {
        std::lock_guard<std::mutex> lock(textureCacheMutex);
        if (sprite && sprite_get_texture(sprite)) {
            spriteTextures[spriteId] = (__bridge id<MTLTexture>)sprite_get_texture(sprite);
        }
    } catch (const std::exception& e) {
        NSLog(@"ParticleSystem: Exception in cacheSprite: %s", e.what());
    }
}

void ParticleSystem::pause() {
    if (physicsThread) {
        physicsThread->pause();
    }
}

void ParticleSystem::resume() {
    if (physicsThread) {
        physicsThread->resume();
    }
}

void ParticleSystem::clear() {
    if (particleBuffer) {
        particleBuffer->clear();
    }
    activeExplosions = 0;
}

void ParticleSystem::setTimeScale(float scale) {
    globalTimeScale = scale;
    if (physicsThread) {
        physicsThread->setTimeStep((1.0f / 120.0f) * scale);
    }
}

void ParticleSystem::setWorldBounds(float width, float height) {
    worldBounds = {width, height};
}

size_t ParticleSystem::getActiveParticleCount() const {
    return particleBuffer ? particleBuffer->getActiveCount() : 0;
}

float ParticleSystem::getPhysicsFrameRate() const {
    if (!physicsThread) return 0.0f;
    float avgTime = physicsThread->getAverageFrameTime();
    return avgTime > 0.0f ? 1.0f / avgTime : 0.0f;
}

void ParticleSystem::setEnabled(bool enabled) {
    systemEnabled = enabled;
    if (!enabled) {
        clear();
    }
}

void ParticleSystem::render(id<MTLRenderCommandEncoder> encoder, simd_float4x4 projectionMatrix) {
    if (!systemEnabled || !particlePipelineState || !particleBuffer || !particleInstanceBuffer) {
        return; // Silently skip rendering if shaders not loaded
    }

    size_t activeCount = particleBuffer->getActiveCount();
    PARTICLE_LOG(@"ParticleSystem::render: activeCount from buffer = %zu", activeCount);
    if (activeCount == 0) {
        PARTICLE_LOG(@"ParticleSystem::render: No active particles, skipping render");
        return;
    }

    // Update render data
    updateRenderData();

    // Get active particles
    auto activeParticles = particleBuffer->getActiveParticles();
    PARTICLE_LOG(@"ParticleSystem::render: Retrieved %zu active particles from buffer", activeParticles.size());
    if (activeParticles.empty()) {
        PARTICLE_LOG(@"ParticleSystem::render: Active particles vector is empty, skipping render");
        return;
    }

    // Update uniforms
    ParticleUniforms* uniforms = (ParticleUniforms*)[particleUniformBuffer contents];
    uniforms->projectionMatrix = projectionMatrix;
    uniforms->screenSize = worldBounds;
    uniforms->time = CACurrentMediaTime();
    uniforms->globalScale = 1.0f;

    // Set pipeline state
    [encoder setRenderPipelineState:particlePipelineState];

    // Bind uniforms
    [encoder setVertexBuffer:particleUniformBuffer offset:0 atIndex:1];
    [encoder setFragmentBuffer:particleUniformBuffer offset:0 atIndex:1];

    // Bind sampler
    [encoder setFragmentSamplerState:particleSampler atIndex:0];

    // Get texture from first particle (all particles use same texture for now)
    id<MTLTexture> particleTexture = nil;
    if (!activeParticles.empty()) {
        try {
            std::lock_guard<std::mutex> lock(textureCacheMutex);
            auto it = spriteTextures.find(activeParticles[0].sourceSprite);
            if (it != spriteTextures.end()) {
                particleTexture = it->second;
            }
        } catch (const std::exception& e) {
            NSLog(@"ParticleSystem: Exception accessing texture cache: %s", e.what());
        }
    }

    if (!particleTexture) {
        NSLog(@"ParticleSystem::render: No texture available for particles, skipping render");
        return;
    }

    // Bind texture once for all particles
    [encoder setFragmentTexture:particleTexture atIndex:0];

    // Populate instance data buffer
    ParticleInstanceData* instanceData = (ParticleInstanceData*)[particleInstanceBuffer contents];
    size_t particleCount = 0;

    // Debug: Log first particle details
    if (!activeParticles.empty()) {
        const auto& firstParticle = activeParticles[0];
        NSLog(@"ParticleSystem::render: First particle - pos:(%.1f,%.1f) scale:%.3f alpha:%.3f color:(%.2f,%.2f,%.2f,%.2f)",
              firstParticle.position.x, firstParticle.position.y, firstParticle.scale, firstParticle.alpha,
              firstParticle.color.x, firstParticle.color.y, firstParticle.color.z, firstParticle.color.w);
    }

    for (const auto& particle : activeParticles) {
        if (!particle.active || particleCount >= 8192) break;

        // Populate instance data
        instanceData[particleCount].position = particle.position;
        instanceData[particleCount].velocity = particle.velocity;
        instanceData[particleCount].texCoordMin = particle.texCoordMin;
        instanceData[particleCount].texCoordMax = particle.texCoordMax;
        instanceData[particleCount].color = particle.color;
        instanceData[particleCount].scale = particle.scale * 32.0f; // Make particles bigger and more visible
        instanceData[particleCount].rotation = particle.rotation;
        instanceData[particleCount].alpha = particle.alpha;
        instanceData[particleCount].lifetime = particle.lifetime;
        instanceData[particleCount].glowIntensity = 0.5f; // Default glow intensity

        particleCount++;
    }

    if (particleCount > 0) {
        // Bind vertex buffer (base quad geometry)
        [encoder setVertexBuffer:particleVertexBuffer offset:0 atIndex:0];

        // Bind instance buffer at index 2 as expected by the shader
        [encoder setVertexBuffer:particleInstanceBuffer offset:0 atIndex:2];

        // Draw with instancing
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:6
                  instanceCount:particleCount];

        NSLog(@"ParticleSystem::render: Drew %zu particles using instanced rendering", particleCount);
    } else {
        NSLog(@"ParticleSystem::render: No particles to render");
    }
}

void ParticleSystem::updateRenderData() {
    // This method can be used to update render-specific data
    // For now, the particle data is updated by the physics thread
    // In the future, we could implement GPU-based particle updates here
}

void ParticleSystem::dumpSystemStats() {
    NSLog(@"=== ParticleSystem Stats ===");
    NSLog(@"System Enabled: %s", systemEnabled ? "YES" : "NO");
    NSLog(@"Active Particles: %zu", getActiveParticleCount());
    NSLog(@"Total Created: %llu", totalParticlesCreated);
    NSLog(@"Active Explosions: %llu", activeExplosions);
    NSLog(@"Physics FPS: %.1f", getPhysicsFrameRate());
    NSLog(@"Physics Steps: %llu", physicsThread ? physicsThread->getSimulationSteps() : 0);
    NSLog(@"Buffer Capacity: %zu", particleBuffer ? particleBuffer->getMaxCapacity() : 0);
    NSLog(@"Time Scale: %.2f", globalTimeScale);
    NSLog(@"World Bounds: %.0f x %.0f", worldBounds.x, worldBounds.y);
    NSLog(@"Hardware Acceleration: %s", particlePipelineState ? "YES" : "NO");
    NSLog(@"===========================");
}

} // namespace SuperTerminal

#pragma mark - C API Implementation

static std::unique_ptr<SuperTerminal::ParticleSystem> g_particleSystem = nullptr;

extern "C" {

bool particle_system_initialize(void* metalDevice) {
    NSLog(@"ParticleSystem: Starting initialization...");

    if (g_particleSystem) {
        NSLog(@"ParticleSystem: Already initialized, returning true");
        return true; // Already initialized
    }

    id<MTLDevice> device = nil;

    // If no device passed in, get it from the framework
    if (metalDevice == nullptr) {
        NSLog(@"ParticleSystem: Getting Metal device from framework");
        device = (__bridge id<MTLDevice>)superterminal_get_metal_device();

        // If framework doesn't have a device yet, create our own
        if (!device) {
            NSLog(@"ParticleSystem: Framework Metal device not ready, creating our own");
            device = MTLCreateSystemDefaultDevice();
        }
    } else {
        NSLog(@"ParticleSystem: Using provided Metal device");
        device = (__bridge id<MTLDevice>)metalDevice;
    }

    if (!device) {
        NSLog(@"ParticleSystem: ERROR - No Metal device available on this system");
        return false;
    }

    NSLog(@"ParticleSystem: Metal device obtained successfully");

    // Initialize sprite layer first (needed for explosions)
    NSLog(@"ParticleSystem: Initializing sprite layer with Metal device");
    sprite_layer_init((__bridge void*)device);

    try {
        NSLog(@"ParticleSystem: Creating ParticleSystem instance");
        g_particleSystem = std::make_unique<SuperTerminal::ParticleSystem>(device);

        NSLog(@"ParticleSystem: Calling initialize()");
        bool result = g_particleSystem->initialize();

        NSLog(@"ParticleSystem: Initialize returned %s", result ? "true" : "false");
        return result;
    } catch (const std::exception& e) {
        NSLog(@"ParticleSystem: Exception during initialization: %s", e.what());
        return false;
    } catch (...) {
        NSLog(@"ParticleSystem: Unknown exception during initialization");
        return false;
    }
}

void particle_system_shutdown() {
    NSLog(@"ParticleSystem: C API shutdown called");
    if (g_particleSystem) {
        try {
            g_particleSystem->shutdown();
            g_particleSystem.reset();
            NSLog(@"ParticleSystem: C API shutdown complete");
        } catch (const std::exception& e) {
            NSLog(@"ParticleSystem: Exception during C API shutdown: %s", e.what());
            // Force reset even if shutdown failed
            g_particleSystem.reset();
        }
    }
}

void particle_system_pause() {
    if (g_particleSystem) {
        g_particleSystem->pause();
    }
}

void particle_system_resume() {
    if (g_particleSystem) {
        g_particleSystem->resume();
    }
}

void particle_system_clear() {
    if (g_particleSystem) {
        g_particleSystem->clear();
    }
}

bool particle_system_is_ready() {
    return (g_particleSystem != nullptr);
}

bool sprite_explode(uint16_t spriteId, uint16_t particleCount) {
    NSLog(@"sprite_explode: Called with spriteId=%d, particleCount=%d", spriteId, particleCount);

    if (!g_particleSystem) {
        NSLog(@"sprite_explode: FAILED - No particle system initialized");
        return false;
    }

    SuperTerminalSprite* sprite = sprite_layer_get_sprite(spriteId);
    if (!sprite) {
        NSLog(@"sprite_explode: FAILED - sprite_layer_get_sprite returned null for ID %d", spriteId);
        return false;
    }
    NSLog(@"sprite_explode: Found sprite for ID %d", spriteId);

    SuperTerminal::ExplosionConfig config;
    config.particleCount = particleCount;

    bool result = g_particleSystem->explodeSpriteAdvanced(spriteId, sprite, config);
    NSLog(@"sprite_explode: explodeSpriteAdvanced returned %s", result ? "true" : "false");
    return result;
}

bool sprite_explode_advanced(uint16_t spriteId, uint16_t particleCount,
                            float explosionForce, float gravity, float fadeTime) {
    if (!g_particleSystem) return false;

    SuperTerminalSprite* sprite = sprite_layer_get_sprite(spriteId);
    if (!sprite) return false;

    SuperTerminal::ExplosionConfig config;
    config.particleCount = particleCount;
    config.explosionForce = explosionForce;
    config.gravityStrength = gravity;
    config.fadeTime = fadeTime;

    return g_particleSystem->explodeSpriteAdvanced(spriteId, sprite, config);
}

bool sprite_explode_directional(uint16_t spriteId, uint16_t particleCount,
                               float forceX, float forceY) {
    if (!g_particleSystem) return false;

    SuperTerminalSprite* sprite = sprite_layer_get_sprite(spriteId);
    if (!sprite) return false;

    return g_particleSystem->explodeSpriteDirectional(spriteId, sprite, forceX, forceY, particleCount);
}

void particle_system_set_time_scale(float scale) {
    if (g_particleSystem) {
        g_particleSystem->setTimeScale(scale);
    }
}

void particle_system_set_world_bounds(float width, float height) {
    if (g_particleSystem) {
        g_particleSystem->setWorldBounds(width, height);
    }
}

void particle_system_set_enabled(bool enabled) {
    if (g_particleSystem) {
        g_particleSystem->setEnabled(enabled);
    }
}

uint32_t particle_system_get_active_count() {
    if (g_particleSystem) {
        return (uint32_t)g_particleSystem->getActiveParticleCount();
    }
    return 0;
}

uint64_t particle_system_get_total_created() {
    if (g_particleSystem) {
        return g_particleSystem->getTotalParticlesCreated();
    }
    return 0;
}

float particle_system_get_physics_fps() {
    if (g_particleSystem) {
        return g_particleSystem->getPhysicsFrameRate();
    }
    return 0.0f;
}

void particle_system_dump_stats() {
    if (g_particleSystem) {
        g_particleSystem->dumpSystemStats();
    }
}

void particle_system_render(void* encoder, float projectionMatrix[16]) {
    if (!g_particleSystem) {
        PARTICLE_LOG(@"particle_system_render: No particle system available");
        return;
    }

    PARTICLE_LOG(@"particle_system_render: Called with encoder=%p", encoder);

    size_t activeCount = g_particleSystem->getActiveParticleCount();
    PARTICLE_LOG(@"particle_system_render: Active particles: %zu", activeCount);

    id<MTLRenderCommandEncoder> renderEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;

    // Convert float array to simd_float4x4
    simd_float4x4 projection = {
        simd_make_float4(projectionMatrix[0], projectionMatrix[1], projectionMatrix[2], projectionMatrix[3]),
        simd_make_float4(projectionMatrix[4], projectionMatrix[5], projectionMatrix[6], projectionMatrix[7]),
        simd_make_float4(projectionMatrix[8], projectionMatrix[9], projectionMatrix[10], projectionMatrix[11]),
        simd_make_float4(projectionMatrix[12], projectionMatrix[13], projectionMatrix[14], projectionMatrix[15])
    };

    g_particleSystem->render(renderEncoder, projection);
    PARTICLE_LOG(@"particle_system_render: Render call completed");
}

} // extern "C"
