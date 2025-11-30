//
//  ParticleSystem.mm
//  SuperTerminal Framework
//
//  Simplified Native Particle Explosion System Implementation (v2)
//  Physics runs in main loop - NO BACKGROUND THREADS
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import "ParticleSystem.h"
#import <Foundation/Foundation.h>
#import <random>
#import <chrono>
#import <algorithm>

// File logging for particle system
static FILE* g_particle_log_file = nullptr;

static void particle_log_init() {
    if (!g_particle_log_file) {
        NSString* logPath = [NSHomeDirectory() stringByAppendingPathComponent:@"particle_system.log"];
        g_particle_log_file = fopen([logPath UTF8String], "w");
        if (g_particle_log_file) {
            fprintf(g_particle_log_file, "=== Particle System Log ===\n");
            fflush(g_particle_log_file);
            NSLog(@"ParticleSystem: Logging to %@", logPath);
        }
    }
}

static void particle_log(const char* format, ...) {
    if (!g_particle_log_file) particle_log_init();
    if (g_particle_log_file) {
        // Get timestamp
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%H:%M:%S", timeinfo);

        fprintf(g_particle_log_file, "[%s] ", timestamp);

        va_list args;
        va_start(args, format);
        vfprintf(g_particle_log_file, format, args);
        va_end(args);

        fprintf(g_particle_log_file, "\n");
        fflush(g_particle_log_file);
    }
}

// Debug logging control
#define PARTICLE_DEBUG_LOGGING 1
#if PARTICLE_DEBUG_LOGGING
    #define PARTICLE_LOG(fmt, ...) particle_log([fmt UTF8String], ##__VA_ARGS__)
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

    // Helper functions to access sprite properties
    float sprite_get_x(SuperTerminalSprite* sprite);
    float sprite_get_y(SuperTerminalSprite* sprite);
    float sprite_get_scale(SuperTerminalSprite* sprite);
    float sprite_get_alpha(SuperTerminalSprite* sprite);
    bool sprite_is_loaded(SuperTerminalSprite* sprite);
    void* sprite_get_texture(SuperTerminalSprite* sprite);
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

#pragma mark - ParticleSystem Implementation

ParticleSystem::ParticleSystem(id<MTLDevice> device, size_t maxPart)
    : metalDevice(device), commandQueue(nil), particlePipelineState(nil),
      particleVertexBuffer(nil), particleInstanceBuffer(nil), particleUniformBuffer(nil),
      particleSampler(nil), maxParticles(maxPart),
      systemEnabled(true), globalTimeScale(1.0f),
      gravity(simd_make_float2(0.0f, 98.0f)),
      worldBounds(simd_make_float2(1024.0f, 768.0f)),
      totalParticlesCreated(0), activeExplosions(0) {

    PARTICLE_LOG(@"ParticleSystem: Constructor called (SIMPLIFIED - no threads)");
    particles.reserve(maxParticles);
}

ParticleSystem::~ParticleSystem() {
    shutdown();
}

bool ParticleSystem::initialize() {
    PARTICLE_LOG(@"ParticleSystem: Initializing simplified particle system...");

    // Initialize sprite layer (needed for sprite explosions)
    PARTICLE_LOG(@"ParticleSystem: Initializing sprite layer...");
    sprite_layer_init((__bridge void*)metalDevice);

    // Initialize Metal resources for instanced rendering
    if (!initializeMetalResources()) {
        PARTICLE_LOG(@"ParticleSystem: Failed to initialize Metal resources");
        return false;
    }

    PARTICLE_LOG(@"ParticleSystem: Initialization complete (main-loop physics)");
    return true;
}

void ParticleSystem::shutdown() {
    PARTICLE_LOG(@"ParticleSystem: Shutting down particle system...");

    // Clear all particles (no mutex needed - single threaded)
    particles.clear();

    // Release Metal resources
    particlePipelineState = nil;
    particleVertexBuffer = nil;
    particleInstanceBuffer = nil;
    particleUniformBuffer = nil;
    particleSampler = nil;
    commandQueue = nil;

    // Clear texture cache
    spriteTextures.clear();

    PARTICLE_LOG(@"ParticleSystem: Shutdown complete");
}

bool ParticleSystem::initializeMetalResources() {
    PARTICLE_LOG(@"ParticleSystem: Creating Metal resources for instanced rendering");

    // Create command queue
    commandQueue = [metalDevice newCommandQueue];
    if (!commandQueue) {
        PARTICLE_LOG(@"ParticleSystem: Failed to create command queue");
        return false;
    }

    // Create vertex buffer for base quad geometry (instanced rendering)
    particleVertexBuffer = [metalDevice newBufferWithLength:sizeof(ParticleVertex) * 6
                                                     options:MTLResourceStorageModeShared];
    if (!particleVertexBuffer) {
        PARTICLE_LOG(@"ParticleSystem: Failed to create vertex buffer");
        return false;
    }

    // Initialize base quad geometry
    ParticleVertex* quadVertices = (ParticleVertex*)[particleVertexBuffer contents];

    // Triangle 1
    quadVertices[0] = {{-1.0f, -1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f};
    quadVertices[1] = {{ 1.0f, -1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f};
    quadVertices[2] = {{-1.0f,  1.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f};

    // Triangle 2
    quadVertices[3] = {{ 1.0f, -1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f};
    quadVertices[4] = {{ 1.0f,  1.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f};
    quadVertices[5] = {{-1.0f,  1.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f};

    // Create instance buffer
    particleInstanceBuffer = [metalDevice newBufferWithLength:sizeof(ParticleInstanceData) * maxParticles
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

    // Load shader library
    NSBundle *frameworkBundle = [NSBundle bundleWithIdentifier:@"com.superterminal.SuperTerminal"];
    if (!frameworkBundle) {
        frameworkBundle = [NSBundle mainBundle];
    }

    NSURL *shaderURL = [frameworkBundle URLForResource:@"Particle" withExtension:@"metallib"];

    // Try build directory if not found in bundle
    if (!shaderURL) {
        NSString *currentDir = [[NSFileManager defaultManager] currentDirectoryPath];
        NSArray *searchPaths = @[
            [currentDir stringByAppendingPathComponent:@"Particle.metallib"],
            [currentDir stringByAppendingPathComponent:@"build/Particle.metallib"],
            [[currentDir stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"build/Particle.metallib"],
            [currentDir stringByAppendingPathComponent:@"../build/Particle.metallib"]
        ];

        for (NSString *path in searchPaths) {
            NSString *resolvedPath = [path stringByStandardizingPath];
            if ([[NSFileManager defaultManager] fileExistsAtPath:resolvedPath]) {
                shaderURL = [NSURL fileURLWithPath:resolvedPath];
                PARTICLE_LOG(@"ParticleSystem: Found Particle.metallib at: %@", resolvedPath);
                break;
            }
        }

        if (!shaderURL) {
            PARTICLE_LOG(@"ParticleSystem: Warning - Could not find Particle.metallib");
            particlePipelineState = nil;
            return true; // Non-fatal - system works but won't render
        }
    }

    NSError *error = nil;
    id<MTLLibrary> library = [metalDevice newLibraryWithURL:shaderURL error:&error];
    if (!library) {
        PARTICLE_LOG(@"ParticleSystem: Failed to load shader library: %@", error.localizedDescription);
        return false;
    }

    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"particle_vertex"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"particle_glow_fragment"];

    if (!vertexFunction || !fragmentFunction) {
        PARTICLE_LOG(@"ParticleSystem: Failed to find shader functions");
        return false;
    }

    // Create render pipeline
    MTLRenderPipelineDescriptor *pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDesc.vertexFunction = vertexFunction;
    pipelineDesc.fragmentFunction = fragmentFunction;

    // Vertex descriptor for instanced rendering
    MTLVertexDescriptor *vertexDesc = [[MTLVertexDescriptor alloc] init];

    // Base quad geometry attributes
    vertexDesc.attributes[0].format = MTLVertexFormatFloat2;  // position
    vertexDesc.attributes[0].offset = 0;
    vertexDesc.attributes[0].bufferIndex = 0;

    vertexDesc.attributes[1].format = MTLVertexFormatFloat2;  // texCoord
    vertexDesc.attributes[1].offset = 8;
    vertexDesc.attributes[1].bufferIndex = 0;

    vertexDesc.attributes[2].format = MTLVertexFormatFloat4;  // color
    vertexDesc.attributes[2].offset = 16;
    vertexDesc.attributes[2].bufferIndex = 0;

    vertexDesc.attributes[3].format = MTLVertexFormatFloat;   // scale
    vertexDesc.attributes[3].offset = 32;
    vertexDesc.attributes[3].bufferIndex = 0;

    vertexDesc.attributes[4].format = MTLVertexFormatFloat;   // rotation
    vertexDesc.attributes[4].offset = 36;
    vertexDesc.attributes[4].bufferIndex = 0;

    vertexDesc.layouts[0].stride = sizeof(ParticleVertex);
    vertexDesc.layouts[0].stepRate = 1;
    vertexDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    pipelineDesc.vertexDescriptor = vertexDesc;
    pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    // Enable blending for transparency
    pipelineDesc.colorAttachments[0].blendingEnabled = YES;
    pipelineDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipelineDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    pipelineDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    particlePipelineState = [metalDevice newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
    if (!particlePipelineState) {
        PARTICLE_LOG(@"ParticleSystem: Failed to create pipeline state: %@", error.localizedDescription);
        return false;
    }

    PARTICLE_LOG(@"ParticleSystem: Metal resources initialized successfully");
    return true;
}

#pragma mark - Main Update (Physics in Main Loop)

void ParticleSystem::update(float deltaTime) {
    if (!systemEnabled) return;

    // Apply time scale
    deltaTime *= globalTimeScale;

    // Update all active particles
    for (auto& particle : particles) {
        if (!particle.active) continue;

        updateParticlePhysics(particle, deltaTime);
        handleCollisions(particle);
    }

    // Remove inactive particles (compact vector)
    particles.erase(
        std::remove_if(particles.begin(), particles.end(),
                      [](const Particle& p) { return !p.active; }),
        particles.end()
    );
}

void ParticleSystem::updateParticlePhysics(Particle& particle, float deltaTime) {
    // Update lifetime
    particle.lifetime += deltaTime;
    if (particle.lifetime >= particle.maxLifetime) {
        particle.active = false;
        return;
    }

    // Apply gravity
    particle.acceleration = gravity * particle.mass;

    // Update velocity
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

void ParticleSystem::handleCollisions(Particle& particle) {
    // Simple ground collision (y = 0)
    if (particle.position.y <= 0.0f && particle.velocity.y > 0.0f) {
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
    if (particle.position.x < -200.0f || particle.position.x > worldBounds.x + 200.0f ||
        particle.position.y < -200.0f || particle.position.y > worldBounds.y + 200.0f) {
        particle.active = false;
    }
}

#pragma mark - Explosion API

bool ParticleSystem::explodeSprite(uint16_t spriteId, SuperTerminalSprite* sprite) {
    ExplosionConfig config;
    return explodeSpriteAdvanced(spriteId, sprite, config);
}

bool ParticleSystem::explodeSpriteAdvanced(uint16_t spriteId, SuperTerminalSprite* sprite,
                                          const ExplosionConfig& config) {
    particle_log("ParticleSystem::explodeSpriteAdvanced: Called with spriteId=%d, particleCount=%d", spriteId, config.particleCount);

    if (!systemEnabled) {
        particle_log("ParticleSystem: FAILED - System not enabled");
        return false;
    }

    if (!sprite) {
        particle_log("ParticleSystem: FAILED - Sprite is null");
        return false;
    }

    if (!sprite_is_loaded(sprite)) {
        particle_log("ParticleSystem: FAILED - Sprite not loaded (spriteId=%d)", spriteId);
        return false;
    }

    particle_log("ParticleSystem: Sprite valid, creating particles at position (%.1f, %.1f)",
          sprite_get_x(sprite), sprite_get_y(sprite));

    // Cache sprite texture
    cacheSprite(spriteId, sprite);

    // Create particles
    createParticlesFromSprite(spriteId, config);

    totalParticlesCreated += config.particleCount;
    activeExplosions++;

    particle_log("ParticleSystem: Explosion created! Total particles: %zu, Active count: %zu",
          particles.size(), getActiveParticleCount());

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
    if (!sprite) {
        particle_log("ParticleSystem::createParticlesFromSprite: FAILED - Could not get sprite %d", spriteId);
        return;
    }

    // Check if we have room for more particles
    if (particles.size() + config.particleCount > maxParticles) {
        particle_log("ParticleSystem: FAILED - Max particle limit reached (%zu/%zu)", particles.size(), maxParticles);
        return;
    }

    particle_log("ParticleSystem::createParticlesFromSprite: Creating %d particles for sprite %d",
          config.particleCount, spriteId);

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
        direction += config.directionalBias * 0.3f;
        float len = simd_length(direction);
        if (len > 0.001f) {
            direction /= len;
        }

        particle.velocity = direction * force;
        particle.acceleration = {0.0f, 0.0f};

        // Random rotation
        particle.rotation = angleDist(gen);
        particle.angularVelocity = rotDist(gen);

        // Random scale
        particle.scale = sizeDist(gen) * sprite_get_scale(sprite);
        particle.scaleVelocity = -particle.scale / config.fadeTime;

        // Set alpha
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

        // Add to vector (no mutex needed)
        particles.push_back(particle);
    }

    particle_log("ParticleSystem: Successfully created %d particles! Total in system: %zu, Active: %zu",
          config.particleCount, particles.size(), getActiveParticleCount());
}

std::vector<simd_float2> ParticleSystem::generateFragmentTexCoords(uint16_t particleCount,
                                                                   float minSize, float maxSize) {
    std::vector<simd_float2> coords;
    coords.reserve(particleCount);

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> coordDist(0.0f, 1.0f);

    for (uint16_t i = 0; i < particleCount; ++i) {
        coords.push_back(simd_make_float2(coordDist(gen), coordDist(gen)));
    }

    return coords;
}

void ParticleSystem::cacheSprite(uint16_t spriteId, SuperTerminalSprite* sprite) {
    particle_log("ParticleSystem::cacheSprite: Called for sprite %d", spriteId);

    if (!sprite) {
        particle_log("ParticleSystem::cacheSprite: FAILED - sprite is null");
        return;
    }

    void* texturePtr = sprite_get_texture(sprite);
    particle_log("ParticleSystem::cacheSprite: sprite_get_texture returned %p", texturePtr);

    if (!texturePtr) {
        particle_log("ParticleSystem::cacheSprite: FAILED - sprite texture is null");
        return;
    }

    id<MTLTexture> texture = (__bridge id<MTLTexture>)texturePtr;
    spriteTextures[spriteId] = texture;
    particle_log("ParticleSystem::cacheSprite: Successfully cached texture %p for sprite %d (cache size: %zu)",
          texture, spriteId, spriteTextures.size());
}

#pragma mark - System Control

void ParticleSystem::pause() {
    globalTimeScale = 0.0f;
}

void ParticleSystem::resume() {
    globalTimeScale = 1.0f;
}

void ParticleSystem::clear() {
    particles.clear();
    activeExplosions = 0;
}

void ParticleSystem::setTimeScale(float scale) {
    globalTimeScale = scale;
}

void ParticleSystem::setWorldBounds(float width, float height) {
    worldBounds = {width, height};
}

void ParticleSystem::setEnabled(bool enabled) {
    systemEnabled = enabled;
    if (!enabled) {
        clear();
    }
}

#pragma mark - Rendering

void ParticleSystem::render(id<MTLRenderCommandEncoder> encoder, simd_float4x4 projectionMatrix) {
    if (!systemEnabled || !particlePipelineState || !particleInstanceBuffer) {
        if (!systemEnabled) particle_log("ParticleSystem::render: System not enabled");
        if (!particlePipelineState) particle_log("ParticleSystem::render: No pipeline state");
        if (!particleInstanceBuffer) particle_log("ParticleSystem::render: No instance buffer");
        return;
    }

    // Count active particles
    size_t activeCount = 0;
    for (const auto& p : particles) {
        if (p.active) activeCount++;
    }

    if (activeCount == 0) return;

    particle_log("ParticleSystem::render: Rendering %zu active particles", activeCount);

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

    // Get texture from first particle
    id<MTLTexture> particleTexture = nil;
    if (!particles.empty() && !spriteTextures.empty()) {
        uint16_t sourceSprite = particles[0].sourceSprite;
        particle_log("ParticleSystem::render: Looking for texture for sprite %d (cache has %zu entries)",
              sourceSprite, spriteTextures.size());
        auto it = spriteTextures.find(sourceSprite);
        if (it != spriteTextures.end()) {
            particleTexture = it->second;
            particle_log("ParticleSystem::render: Found texture %p", particleTexture);
        } else {
            particle_log("ParticleSystem::render: Texture NOT found for sprite %d", sourceSprite);
        }
    } else {
        particle_log("ParticleSystem::render: particles.empty=%d, spriteTextures.empty=%d",
              particles.empty(), spriteTextures.empty());
    }

    if (!particleTexture) {
        particle_log("ParticleSystem::render: No texture available - skipping render");
        return;
    }

    // Bind texture
    [encoder setFragmentTexture:particleTexture atIndex:0];

    // Populate instance data buffer
    ParticleInstanceData* instanceData = (ParticleInstanceData*)[particleInstanceBuffer contents];
    size_t instanceIndex = 0;

    for (const auto& p : particles) {
        if (!p.active) continue;

        instanceData[instanceIndex].position = p.position;
        instanceData[instanceIndex].velocity = p.velocity;
        instanceData[instanceIndex].texCoordMin = p.texCoordMin;
        instanceData[instanceIndex].texCoordMax = p.texCoordMax;
        instanceData[instanceIndex].color = p.color;
        instanceData[instanceIndex].scale = p.scale * 32.0f; // Make particles much larger and visible
        instanceData[instanceIndex].rotation = p.rotation;
        instanceData[instanceIndex].alpha = p.alpha;
        instanceData[instanceIndex].lifetime = p.lifetime;
        instanceData[instanceIndex].glowIntensity = p.glowIntensity;

        if (instanceIndex == 0) {
            particle_log("ParticleSystem::render: First particle pos=(%.1f, %.1f) scale=%.3f alpha=%.3f",
                  p.position.x, p.position.y, p.scale * 32.0f, p.alpha);
        }

        instanceIndex++;
    }

    // Bind vertex buffer (base quad)
    [encoder setVertexBuffer:particleVertexBuffer offset:0 atIndex:0];

    // Bind instance buffer
    [encoder setVertexBuffer:particleInstanceBuffer offset:0 atIndex:2];

    // Single instanced draw call for all particles
    particle_log("ParticleSystem::render: Drawing %zu particle instances", activeCount);
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:6
              instanceCount:activeCount];
    particle_log("ParticleSystem::render: Draw call complete");
}

#pragma mark - Queries

size_t ParticleSystem::getActiveParticleCount() const {
    size_t count = 0;
    for (const auto& p : particles) {
        if (p.active) count++;
    }
    return count;
}

void ParticleSystem::dumpSystemStats() {
    NSLog(@"=== Particle System Stats ===");
    NSLog(@"Total particles created: %llu", totalParticlesCreated);
    NSLog(@"Active particles: %zu", getActiveParticleCount());
    NSLog(@"Active explosions: %llu", activeExplosions);
    NSLog(@"Particles in vector: %zu", particles.size());
    NSLog(@"Max particles: %zu", maxParticles);
    NSLog(@"System enabled: %s", systemEnabled ? "YES" : "NO");
    NSLog(@"Time scale: %.2f", globalTimeScale);
    NSLog(@"===========================");
}

} // namespace SuperTerminal

#pragma mark - C API Implementation

using namespace SuperTerminal;

// Global instance (single source of truth)
static ParticleSystem* g_particleSystem = nullptr;

extern "C" {

bool particle_system_initialize(void* metalDevice) {
    particle_log("ParticleSystem C API: Initializing (simplified v2)...");
    NSLog(@"ParticleSystem C API: Initializing (simplified v2)...");

    if (g_particleSystem) {
        particle_log("ParticleSystem C API: Already initialized");
        NSLog(@"ParticleSystem C API: Already initialized");
        return true;
    }

    if (!metalDevice) {
        particle_log("ParticleSystem C API: ERROR - No Metal device provided");
        NSLog(@"ParticleSystem C API: ERROR - No Metal device provided");
        return false;
    }

    id<MTLDevice> device = (__bridge id<MTLDevice>)metalDevice;
    g_particleSystem = new ParticleSystem(device);

    if (!g_particleSystem->initialize()) {
        particle_log("ParticleSystem C API: Initialization failed");
        NSLog(@"ParticleSystem C API: Initialization failed");
        delete g_particleSystem;
        g_particleSystem = nullptr;
        return false;
    }

    particle_log("ParticleSystem C API: Initialized successfully");
    NSLog(@"ParticleSystem C API: Initialized successfully");
    return true;
}

void particle_system_shutdown() {
    if (g_particleSystem) {
        particle_log("ParticleSystem C API: Shutting down...");
        NSLog(@"ParticleSystem C API: Shutting down...");
        g_particleSystem->shutdown();
        delete g_particleSystem;
        g_particleSystem = nullptr;
        particle_log("ParticleSystem C API: Shutdown complete");
        NSLog(@"ParticleSystem C API: Shutdown complete");
    }
}

void particle_system_update(float deltaTime) {
    if (g_particleSystem) {
        g_particleSystem->update(deltaTime);
    }
}

void particle_system_render(void* encoder, simd_float4x4 projectionMatrix) {
    if (g_particleSystem && encoder) {
        id<MTLRenderCommandEncoder> mtlEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
        g_particleSystem->render(mtlEncoder, projectionMatrix);
    }
}

bool sprite_explode(uint16_t spriteId, uint16_t particleCount) {
    particle_log("sprite_explode: Called with spriteId=%d, particleCount=%d", spriteId, particleCount);

    if (!g_particleSystem) {
        particle_log("sprite_explode: FAILED - g_particleSystem is null");
        return false;
    }

    SuperTerminalSprite* sprite = sprite_layer_get_sprite(spriteId);
    if (!sprite) {
        particle_log("sprite_explode: FAILED - sprite_layer_get_sprite returned null for ID %d", spriteId);
        return false;
    }

    particle_log("sprite_explode: Got sprite, calling explodeSpriteAdvanced...");

    ExplosionConfig config;
    config.particleCount = particleCount;

    bool result = g_particleSystem->explodeSpriteAdvanced(spriteId, sprite, config);
    particle_log("sprite_explode: Result = %s", result ? "true" : "false");
    return result;
}

bool sprite_explode_advanced(uint16_t spriteId, uint16_t particleCount,
                            float explosionForce, float gravity, float fadeTime) {
    if (!g_particleSystem) return false;

    SuperTerminalSprite* sprite = sprite_layer_get_sprite(spriteId);
    if (!sprite) return false;

    ExplosionConfig config;
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
    return g_particleSystem ? (uint32_t)g_particleSystem->getActiveParticleCount() : 0;
}

uint64_t particle_system_get_total_created() {
    return g_particleSystem ? g_particleSystem->getTotalParticlesCreated() : 0;
}

void particle_system_dump_stats() {
    if (g_particleSystem) {
        g_particleSystem->dumpSystemStats();
    }
}

bool particle_system_is_ready() {
    return g_particleSystem != nullptr;
}

} // extern "C"
