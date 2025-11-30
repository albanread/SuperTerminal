//
//  Particle.metal
//  SuperTerminal Framework
//
//  Hardware-Accelerated Particle Rendering Shaders
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include <metal_stdlib>
#include "Common.metal"
using namespace metal;

// MARK: - Particle Rendering Structures

struct ParticleVertex {
    float2 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
    float4 color [[attribute(2)]];
    float scale [[attribute(3)]];
    float rotation [[attribute(4)]];
};

struct ParticleInstanceData {
    float2 position;
    float2 velocity;
    float2 texCoordMin;
    float2 texCoordMax;
    float4 color;
    float scale;
    float rotation;
    float alpha;
    float lifetime;
    float glowIntensity;
};

struct ParticleVertexOut {
    float4 position [[position]];
    float2 texCoord;
    float4 color;
    float2 screenPos;
    float scale;
    float rotation;
    float glowIntensity;
    float lifetime;
};

struct ParticleUniforms {
    float4x4 projectionMatrix;
    float2 screenSize;
    float time;
    float globalScale;
};

// MARK: - Vertex Shader

vertex ParticleVertexOut particle_vertex(ParticleVertex in [[stage_in]],
                                         constant ParticleUniforms& uniforms [[buffer(1)]],
                                         constant ParticleInstanceData* instances [[buffer(2)]],
                                         uint vertexID [[vertex_id]],
                                         uint instanceID [[instance_id]]) {
    ParticleVertexOut out;

    // Get instance data
    ParticleInstanceData instance = instances[instanceID];

    // Apply rotation to vertex position
    float cos_r = cos(instance.rotation);
    float sin_r = sin(instance.rotation);
    float2x2 rotationMatrix = float2x2(cos_r, -sin_r,
                                       sin_r, cos_r);

    // Scale and rotate vertex position
    float2 scaledPos = in.position * instance.scale * uniforms.globalScale;
    float2 rotatedPos = rotationMatrix * scaledPos;

    // Translate to world position
    float2 worldPos = rotatedPos + instance.position;

    // Transform to screen space
    out.position = uniforms.projectionMatrix * float4(worldPos, 0.0, 1.0);

    // Map vertex texture coordinate to instance's texture fragment
    float2 texRange = instance.texCoordMax - instance.texCoordMin;
    out.texCoord = instance.texCoordMin + (in.texCoord * texRange);

    // Pass through color with instance alpha
    out.color = instance.color;
    out.color.a *= instance.alpha;

    // Calculate screen position for effects
    out.screenPos = (out.position.xy / out.position.w + 1.0) * 0.5;

    // Pass through additional data
    out.scale = instance.scale;
    out.rotation = instance.rotation;
    out.glowIntensity = instance.glowIntensity;
    out.lifetime = instance.lifetime;

    return out;
}

// MARK: - Fragment Shaders

// Basic particle fragment shader
fragment float4 particle_fragment(ParticleVertexOut in [[stage_in]],
                                  constant ParticleUniforms& uniforms [[buffer(1)]],
                                  texture2d<float> particleTexture [[texture(0)]],
                                  sampler texSampler [[sampler(0)]]) {

    // Sample the particle texture
    float4 texColor = particleTexture.sample(texSampler, in.texCoord);

    // Apply vertex color modulation
    float4 finalColor = texColor * in.color;

    // Discard fully transparent pixels
    if (finalColor.a < EPSILON) {
        discard_fragment();
    }

    return finalColor;
}

// Particle fragment shader with glow effect
fragment float4 particle_glow_fragment(ParticleVertexOut in [[stage_in]],
                                       constant ParticleUniforms& uniforms [[buffer(1)]],
                                       texture2d<float> particleTexture [[texture(0)]],
                                       sampler texSampler [[sampler(0)]]) {

    // Sample the particle texture
    float4 texColor = particleTexture.sample(texSampler, in.texCoord);

    // Apply vertex color modulation
    float4 finalColor = texColor * in.color;

    // Add glow effect if enabled
    if (in.glowIntensity > 0.0) {
        // Calculate distance from center for radial glow
        float2 center = float2(0.5, 0.5);
        float2 localCoord = (in.texCoord - center) * 2.0; // -1 to 1 range
        float distFromCenter = length(localCoord);

        // Create glow falloff
        float glowFalloff = exp(-distFromCenter * 3.0);
        float3 glowColor = finalColor.rgb * in.glowIntensity * glowFalloff;

        // Add glow to final color
        finalColor.rgb += glowColor;

        // Enhance alpha at edges for better glow visibility
        finalColor.a = max(finalColor.a, glowFalloff * in.glowIntensity * 0.5);
    }

    // Discard fully transparent pixels
    if (finalColor.a < EPSILON) {
        discard_fragment();
    }

    return finalColor;
}

// Particle fragment shader with additive blending for explosions
fragment float4 particle_additive_fragment(ParticleVertexOut in [[stage_in]],
                                           constant ParticleUniforms& uniforms [[buffer(1)]],
                                           texture2d<float> particleTexture [[texture(0)]],
                                           sampler texSampler [[sampler(0)]]) {

    // Sample the particle texture
    float4 texColor = particleTexture.sample(texSampler, in.texCoord);

    // Apply vertex color modulation
    float4 finalColor = texColor * in.color;

    // For additive blending, we want bright colors
    // Boost the brightness based on lifetime (newer particles are brighter)
    float brightnessBoost = 1.0 + (1.0 - min(in.lifetime / 2.0, 1.0)) * 2.0;
    finalColor.rgb *= brightnessBoost;

    // Add subtle sparkle effect
    float2 sparkleCoord = in.texCoord * 16.0 + uniforms.time * 2.0;
    float sparkle = abs(sin(sparkleCoord.x) * cos(sparkleCoord.y)) * 0.3;
    finalColor.rgb += sparkle * finalColor.a;

    // Discard fully transparent pixels
    if (finalColor.a < EPSILON) {
        discard_fragment();
    }

    return finalColor;
}

// Particle fragment shader with trail effect
fragment float4 particle_trail_fragment(ParticleVertexOut in [[stage_in]],
                                        constant ParticleUniforms& uniforms [[buffer(1)]],
                                        texture2d<float> particleTexture [[texture(0)]],
                                        sampler texSampler [[sampler(0)]]) {

    // Sample the particle texture
    float4 texColor = particleTexture.sample(texSampler, in.texCoord);

    // Apply vertex color modulation
    float4 finalColor = texColor * in.color;

    // Create trail effect by stretching the particle
    float2 center = float2(0.5, 0.5);
    float2 offset = in.texCoord - center;

    // Stretch along velocity direction (approximated by rotation)
    float stretchAmount = 1.5;
    float2 stretchedCoord = center + offset * float2(stretchAmount, 1.0);

    // Sample with stretched coordinates for trail
    float4 trailColor = particleTexture.sample(texSampler, stretchedCoord);

    // Blend original and trail
    finalColor = mix(finalColor, trailColor * in.color * 0.7, 0.3);

    // Add motion blur effect based on lifetime
    float motionBlur = min(in.lifetime * 0.5, 1.0);
    finalColor.a *= (1.0 - motionBlur * 0.3);

    // Discard fully transparent pixels
    if (finalColor.a < EPSILON) {
        discard_fragment();
    }

    return finalColor;
}

// MARK: - Compute Shaders for GPU-based Particle Physics

struct ParticlePhysicsData {
    float2 position;
    float2 velocity;
    float2 acceleration;
    float rotation;
    float angularVelocity;
    float scale;
    float scaleVelocity;
    float alpha;
    float alphaDecay;
    float lifetime;
    float maxLifetime;
    float mass;
    float drag;
    float bounce;
    bool active;
};

struct PhysicsUniforms {
    float deltaTime;
    float2 gravity;
    float2 worldBounds;
    float time;
    uint particleCount;
};

// GPU-based particle physics update
kernel void particle_physics_update(device ParticlePhysicsData* particles [[buffer(0)]],
                                    constant PhysicsUniforms& physics [[buffer(1)]],
                                    uint gid [[thread_position_in_grid]]) {

    if (gid >= physics.particleCount) return;

    device ParticlePhysicsData& particle = particles[gid];

    if (!particle.active) return;

    // Update lifetime
    particle.lifetime += physics.deltaTime;
    if (particle.lifetime >= particle.maxLifetime) {
        particle.active = false;
        return;
    }

    // Apply gravity
    particle.acceleration = physics.gravity;

    // Update velocity (Verlet integration)
    particle.velocity += particle.acceleration * physics.deltaTime;
    particle.velocity *= particle.drag; // Apply drag

    // Update position
    particle.position += particle.velocity * physics.deltaTime;

    // Update rotation
    particle.rotation += particle.angularVelocity * physics.deltaTime;

    // Update scale
    particle.scale += particle.scaleVelocity * physics.deltaTime;
    if (particle.scale < 0.01) {
        particle.active = false;
        return;
    }

    // Update alpha (fade out)
    float lifeRatio = particle.lifetime / particle.maxLifetime;
    particle.alpha = 1.0 - (lifeRatio * lifeRatio); // Quadratic fade
    if (particle.alpha <= 0.0) {
        particle.active = false;
        return;
    }

    // Handle collisions
    // Ground collision
    if (particle.position.y <= 0.0 && particle.velocity.y < 0.0) {
        particle.position.y = 0.0;
        particle.velocity.y = -particle.velocity.y * particle.bounce;
        particle.velocity.x *= 0.8; // Friction
    }

    // World bounds
    if (particle.position.x < -200.0 || particle.position.x > physics.worldBounds.x + 200.0 ||
        particle.position.y < -200.0 || particle.position.y > physics.worldBounds.y + 200.0) {
        particle.active = false;
    }
}

// Particle culling and instance data preparation
kernel void particle_prepare_rendering(device const ParticlePhysicsData* physicsData [[buffer(0)]],
                                       device ParticleInstanceData* instanceData [[buffer(1)]],
                                       device uint* visibleIndices [[buffer(2)]],
                                       device atomic_uint* visibleCount [[buffer(3)]],
                                       constant PhysicsUniforms& physics [[buffer(4)]],
                                       uint gid [[thread_position_in_grid]]) {

    if (gid >= physics.particleCount) return;

    const device ParticlePhysicsData& physics_particle = physicsData[gid];

    if (!physics_particle.active) return;

    // Frustum culling (simple bounds check)
    if (physics_particle.position.x < -100.0 || physics_particle.position.x > physics.worldBounds.x + 100.0 ||
        physics_particle.position.y < -100.0 || physics_particle.position.y > physics.worldBounds.y + 100.0) {
        return;
    }

    // Get index for this visible particle
    uint visibleIndex = atomic_fetch_add_explicit(visibleCount, 1, memory_order_relaxed);

    if (visibleIndex >= physics.particleCount) return; // Safety check

    // Store visible particle index
    visibleIndices[visibleIndex] = gid;

    // Prepare instance data for rendering
    device ParticleInstanceData& instance = instanceData[visibleIndex];
    instance.position = physics_particle.position;
    instance.velocity = physics_particle.velocity;
    instance.scale = physics_particle.scale;
    instance.rotation = physics_particle.rotation;
    instance.alpha = physics_particle.alpha;
    instance.lifetime = physics_particle.lifetime;

    // Set color based on lifetime (heat effect)
    float lifeRatio = physics_particle.lifetime / physics_particle.maxLifetime;

    // Fire-like color transition: white -> yellow -> orange -> red -> black
    if (lifeRatio < 0.2) {
        // White to yellow
        instance.color = float4(1.0, 1.0, 1.0 - lifeRatio * 2.0, physics_particle.alpha);
    } else if (lifeRatio < 0.5) {
        // Yellow to orange
        float t = (lifeRatio - 0.2) / 0.3;
        instance.color = float4(1.0, 1.0 - t * 0.5, 0.0, physics_particle.alpha);
    } else if (lifeRatio < 0.8) {
        // Orange to red
        float t = (lifeRatio - 0.5) / 0.3;
        instance.color = float4(1.0, 0.5 - t * 0.5, 0.0, physics_particle.alpha);
    } else {
        // Red fade to black
        float t = (lifeRatio - 0.8) / 0.2;
        instance.color = float4(1.0 - t, 0.0, 0.0, physics_particle.alpha * (1.0 - t));
    }

    // Set texture coordinates (could be randomized or based on source sprite)
    instance.texCoordMin = float2(0.0, 0.0);
    instance.texCoordMax = float2(1.0, 1.0);

    // Set glow intensity based on particle age (younger = more glow)
    instance.glowIntensity = (1.0 - lifeRatio) * 0.8;
}
