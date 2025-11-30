//
//  Sprite.metal
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include <metal_stdlib>
#include "Common.metal"
using namespace metal;

// MARK: - Sprite Layer Shaders

// Simple structures for basic sprite rendering
struct SimpleSpriteUniforms {
    float4x4 modelMatrix;
    float4x4 viewProjectionMatrix;
    float4 color;
};

struct SpriteVertexIn {
    float2 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
    float4 color [[attribute(2)]];
    uint16_t spriteId [[attribute(3)]];
    uint16_t flags [[attribute(4)]];
};

struct SpriteVertexOut {
    float4 position [[position]];
    float2 texCoord;
    float4 color;
    uint16_t spriteId;
    float2 screenPos;
};

// Vertex shader for sprites
vertex SpriteVertexOut sprite_vertex(SpriteVertexIn in [[stage_in]],
                                    constant Uniforms& uniforms [[buffer(1)]],
                                    constant SpriteUniforms& spriteUniforms [[buffer(2)]],
                                    uint vertexID [[vertex_id]],
                                    uint instanceID [[instance_id]]) {
    SpriteVertexOut out;

    // Transform position to screen space
    float4 worldPos = float4(in.position, 0.0, 1.0);
    out.position = uniforms.projectionMatrix * uniforms.modelViewMatrix * worldPos;

    // Pass through texture coordinates and color
    out.texCoord = in.texCoord;
    out.color = in.color;
    out.spriteId = in.spriteId;

    // Calculate screen position for effects
    out.screenPos = (out.position.xy / out.position.w + 1.0) * 0.5;

    return out;
}

// Fragment shader for sprite rendering
fragment float4 sprite_fragment(SpriteVertexOut in [[stage_in]],
                               constant Uniforms& uniforms [[buffer(1)]],
                               constant SpriteUniforms& spriteUniforms [[buffer(2)]],
                               texture2d<float> spriteAtlas [[texture(0)]],
                               sampler spriteSampler [[sampler(0)]]) {

    // Sample the sprite atlas
    float4 spriteColor = spriteAtlas.sample(spriteSampler, in.texCoord);

    // Apply vertex color modulation (includes alpha)
    float4 finalColor = spriteColor * in.color;

    // Discard fully transparent pixels
    if (finalColor.a < EPSILON) {
        discard_fragment();
    }

    return finalColor;
}

// Alternative fragment shader with pixel art filtering
fragment float4 sprite_pixelart_fragment(SpriteVertexOut in [[stage_in]],
                                         constant Uniforms& uniforms [[buffer(1)]],
                                         constant SpriteUniforms& spriteUniforms [[buffer(2)]],
                                         texture2d<float> spriteAtlas [[texture(0)]],
                                         sampler spriteSampler [[sampler(0)]]) {

    // Sample with nearest neighbor filtering for crisp pixel art
    float2 texelSize = 1.0 / float2(spriteAtlas.get_width(), spriteAtlas.get_height());
    float2 pixelCoord = in.texCoord / texelSize;
    float2 centeredCoord = (floor(pixelCoord) + 0.5) * texelSize;

    float4 spriteColor = spriteAtlas.sample(spriteSampler, centeredCoord);

    // Apply vertex color modulation
    float4 finalColor = spriteColor * in.color;

    // Discard fully transparent pixels
    if (finalColor.a < EPSILON) {
        discard_fragment();
    }

    return finalColor;
}

// Fragment shader with outline effect
fragment float4 sprite_outline_fragment(SpriteVertexOut in [[stage_in]],
                                        constant Uniforms& uniforms [[buffer(1)]],
                                        constant SpriteUniforms& spriteUniforms [[buffer(2)]],
                                        texture2d<float> spriteAtlas [[texture(0)]],
                                        sampler spriteSampler [[sampler(0)]],
                                        constant float4& outlineColor [[buffer(3)]],
                                        constant float& outlineWidth [[buffer(4)]]) {

    float2 texelSize = 1.0 / float2(spriteAtlas.get_width(), spriteAtlas.get_height());

    // Sample center pixel
    float4 centerColor = spriteAtlas.sample(spriteSampler, in.texCoord);

    // If center pixel is transparent, check for outline
    if (centerColor.a < EPSILON) {
        float outlineAlpha = 0.0;

        // Sample surrounding pixels for outline detection
        for (int x = -1; x <= 1; x++) {
            for (int y = -1; y <= 1; y++) {
                if (x == 0 && y == 0) continue;

                float2 offset = float2(x, y) * texelSize * outlineWidth;
                float4 neighborColor = spriteAtlas.sample(spriteSampler, in.texCoord + offset);

                if (neighborColor.a > EPSILON) {
                    outlineAlpha = 1.0;
                    break;
                }
            }
            if (outlineAlpha > 0.0) break;
        }

        if (outlineAlpha > 0.0) {
            return outlineColor * in.color;
        } else {
            discard_fragment();
        }
    }

    // Render normal sprite pixel
    float4 finalColor = centerColor * in.color;
    return finalColor;
}

// Fragment shader with glow effect
fragment float4 sprite_glow_fragment(SpriteVertexOut in [[stage_in]],
                                     constant Uniforms& uniforms [[buffer(1)]],
                                     constant SpriteUniforms& spriteUniforms [[buffer(2)]],
                                     texture2d<float> spriteAtlas [[texture(0)]],
                                     sampler spriteSampler [[sampler(0)]],
                                     constant float4& glowColor [[buffer(3)]],
                                     constant float& glowRadius [[buffer(4)]]) {

    float2 texelSize = 1.0 / float2(spriteAtlas.get_width(), spriteAtlas.get_height());

    // Sample center pixel
    float4 centerColor = spriteAtlas.sample(spriteSampler, in.texCoord);

    // Calculate glow effect
    float4 glowSample = float4(0.0);
    float totalWeight = 0.0;

    int radius = int(glowRadius);
    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            float2 offset = float2(x, y) * texelSize;
            float distance = length(float2(x, y));

            if (distance <= glowRadius) {
                float weight = exp(-distance * distance / (glowRadius * glowRadius * 0.5));
                float4 sample = spriteAtlas.sample(spriteSampler, in.texCoord + offset);

                glowSample += sample * weight;
                totalWeight += weight;
            }
        }
    }

    if (totalWeight > 0.0) {
        glowSample /= totalWeight;
    }

    // Combine original sprite with glow
    float4 finalColor = centerColor * in.color;

    // Add glow effect
    float glowAmount = glowSample.a * 0.5;
    finalColor.rgb += glowColor.rgb * glowAmount;
    finalColor.a = max(finalColor.a, glowAmount);

    if (finalColor.a < EPSILON) {
        discard_fragment();
    }

    return finalColor;
}

// Fragment shader with color replacement
fragment float4 sprite_color_replace_fragment(SpriteVertexOut in [[stage_in]],
                                             constant Uniforms& uniforms [[buffer(1)]],
                                             constant SpriteUniforms& spriteUniforms [[buffer(2)]],
                                             texture2d<float> spriteAtlas [[texture(0)]],
                                             sampler spriteSampler [[sampler(0)]],
                                             constant float4& sourceColor [[buffer(3)]],
                                             constant float4& targetColor [[buffer(4)]],
                                             constant float& tolerance [[buffer(5)]]) {

    // Sample the sprite atlas
    float4 spriteColor = spriteAtlas.sample(spriteSampler, in.texCoord);

    // Check if color matches source color within tolerance
    float colorDistance = length(spriteColor.rgb - sourceColor.rgb);

    if (colorDistance <= tolerance && spriteColor.a > EPSILON) {
        // Replace with target color, preserving alpha
        spriteColor.rgb = targetColor.rgb;
    }

    // Apply vertex color modulation
    float4 finalColor = spriteColor * in.color;

    if (finalColor.a < EPSILON) {
        discard_fragment();
    }

    return finalColor;
}

// Fragment shader with animation frame support
fragment float4 sprite_animated_fragment(SpriteVertexOut in [[stage_in]],
                                         constant Uniforms& uniforms [[buffer(1)]],
                                         constant SpriteUniforms& spriteUniforms [[buffer(2)]],
                                         texture2d<float> spriteAtlas [[texture(0)]],
                                         sampler spriteSampler [[sampler(0)]],
                                         constant int& frameCount [[buffer(3)]],
                                         constant float& animationSpeed [[buffer(4)]]) {

    // Calculate current animation frame
    float animationTime = uniforms.time * animationSpeed;
    int currentFrame = int(animationTime) % frameCount;

    // Offset texture coordinates based on frame
    float2 frameOffset = float2(currentFrame / 16.0f, 0.0); // Assuming horizontal sprite strip
    float2 animatedTexCoord = in.texCoord + frameOffset;

    // Sample the sprite atlas with animated coordinates
    float4 spriteColor = spriteAtlas.sample(spriteSampler, animatedTexCoord);

    // Apply vertex color modulation
    float4 finalColor = spriteColor * in.color;

    if (finalColor.a < EPSILON) {
        discard_fragment();
    }

    return finalColor;
}

// Fragment shader with shadow effect
fragment float4 sprite_shadow_fragment(SpriteVertexOut in [[stage_in]],
                                       constant Uniforms& uniforms [[buffer(1)]],
                                       constant SpriteUniforms& spriteUniforms [[buffer(2)]],
                                       texture2d<float> spriteAtlas [[texture(0)]],
                                       sampler spriteSampler [[sampler(0)]],
                                       constant float4& shadowColor [[buffer(3)]],
                                       constant float2& shadowOffset [[buffer(4)]],
                                       constant float& shadowBlur [[buffer(5)]]) {

    float2 texelSize = 1.0 / float2(spriteAtlas.get_width(), spriteAtlas.get_height());

    // Sample the main sprite
    float4 spriteColor = spriteAtlas.sample(spriteSampler, in.texCoord);

    // Calculate shadow position
    float2 shadowTexCoord = in.texCoord - shadowOffset * texelSize;

    // Sample shadow area with blur
    float shadowAlpha = 0.0;
    float totalWeight = 0.0;

    int blurRadius = int(shadowBlur);
    for (int x = -blurRadius; x <= blurRadius; x++) {
        for (int y = -blurRadius; y <= blurRadius; y++) {
            float2 offset = float2(x, y) * texelSize;
            float distance = length(float2(x, y));

            if (distance <= shadowBlur) {
                float weight = exp(-distance * distance / (shadowBlur * shadowBlur * 0.5));
                float4 sample = spriteAtlas.sample(spriteSampler, shadowTexCoord + offset);

                shadowAlpha += sample.a * weight;
                totalWeight += weight;
            }
        }
    }

    if (totalWeight > 0.0) {
        shadowAlpha /= totalWeight;
    }

    // Combine sprite and shadow
    float4 finalColor = float4(0.0, 0.0, 0.0, 0.0);

    if (spriteColor.a > EPSILON) {
        // Main sprite pixel - render normally
        finalColor = spriteColor * in.color;
    } else if (shadowAlpha > EPSILON) {
        // Shadow pixel - render shadow
        finalColor = shadowColor;
        finalColor.a *= shadowAlpha;
    } else {
        // Transparent pixel
        discard_fragment();
    }

    return finalColor;
}

// Vertex shader for sepia effect - simplified to match basic sprite rendering
vertex VertexOut sprite_sepia_vertex(VertexIn in [[stage_in]],
                                     constant SimpleSpriteUniforms& uniforms [[buffer(1)]]) {
    VertexOut out;
    float4 worldPos = uniforms.modelMatrix * float4(in.position, 0.0, 1.0);
    out.position = uniforms.viewProjectionMatrix * worldPos;
    out.texCoord = in.texCoord;
    out.color = in.color * uniforms.color;
    return out;
}

// Fragment shader with sepia tone effect
fragment float4 sprite_sepia_fragment(VertexOut in [[stage_in]],
                                      texture2d<float> spriteTexture [[texture(0)]],
                                      sampler texSampler [[sampler(0)]],
                                      constant float& sepiaIntensity [[buffer(3)]]) {

    // Sample the sprite texture
    float4 spriteColor = spriteTexture.sample(texSampler, in.texCoord);

    if (spriteColor.a < 0.001) {
        discard_fragment();
    }

    // Apply vertex color modulation first
    spriteColor = spriteColor * in.color;

    // Convert to grayscale
    float gray = (spriteColor.r + spriteColor.g + spriteColor.b) / 3.0;

    // Create obvious sepia brown colors
    float3 sepiaColor = float3(
        gray * 1.2,      // Red: brighter
        gray * 0.9,      // Green: medium
        gray * 0.6       // Blue: darker (makes it brown)
    );

    // Mix between original and sepia based on intensity
    float3 finalRGB = mix(spriteColor.rgb, sepiaColor, sepiaIntensity);

    return float4(finalRGB, spriteColor.a);
}

// Simple glow fragment shader for basic sprite rendering
fragment float4 sprite_simple_glow_fragment(VertexOut in [[stage_in]],
                                            texture2d<float> spriteTexture [[texture(0)]],
                                            sampler texSampler [[sampler(0)]],
                                            constant float4& glowColor [[buffer(3)]],
                                            constant float& glowRadius [[buffer(4)]]) {

    // Sample the sprite texture
    float4 spriteColor = spriteTexture.sample(texSampler, in.texCoord);

    if (spriteColor.a < 0.001) {
        discard_fragment();
    }

    // Apply vertex color modulation
    spriteColor = spriteColor * in.color;

    // Simple glow effect - just add glow color around edges
    float alpha = spriteColor.a;
    float3 finalRGB = spriteColor.rgb + glowColor.rgb * glowColor.a * 0.3;

    return float4(finalRGB, alpha);
}

// Simple outline fragment shader for basic sprite rendering
fragment float4 sprite_simple_outline_fragment(VertexOut in [[stage_in]],
                                               texture2d<float> spriteTexture [[texture(0)]],
                                               sampler texSampler [[sampler(0)]],
                                               constant float4& outlineColor [[buffer(3)]]) {

    // Sample the sprite texture
    float4 spriteColor = spriteTexture.sample(texSampler, in.texCoord);

    // Apply vertex color modulation
    spriteColor = spriteColor * in.color;

    // Simple outline - just tint the sprite with outline color
    float3 finalRGB = mix(spriteColor.rgb, outlineColor.rgb, 0.5);

    return float4(finalRGB, spriteColor.a);
}

// Debug fragment shader to visualize sprite bounds
fragment float4 sprite_debug_fragment(SpriteVertexOut in [[stage_in]],
                                      constant Uniforms& uniforms [[buffer(1)]],
                                      constant SpriteUniforms& spriteUniforms [[buffer(2)]],
                                      texture2d<float> spriteAtlas [[texture(0)]],
                                      sampler spriteSampler [[sampler(0)]]) {

    // Sample the sprite atlas
    float4 spriteColor = spriteAtlas.sample(spriteSampler, in.texCoord);

    // Create debug border
    float2 border = step(0.05, in.texCoord) * step(0.05, 1.0 - in.texCoord);
    float borderMask = border.x * border.y;

    // Show sprite ID as color tint
    float4 debugTint = debugColor(in.spriteId % 8);

    if (borderMask < 1.0) {
        // Border area - show debug color
        return debugTint * 0.8;
    } else {
        // Inside area - show sprite with tint
        float4 finalColor = spriteColor * in.color;
        finalColor.rgb = mix(finalColor.rgb, debugTint.rgb, 0.2);

        if (finalColor.a < EPSILON) {
            discard_fragment();
        }

        return finalColor;
    }
}

// Compute shader for sprite culling and sorting
kernel void sprite_cull_and_sort(device SpriteInstance* sprites [[buffer(0)]],
                                 device uint* visibleIndices [[buffer(1)]],
                                 device atomic_uint* visibleCount [[buffer(2)]],
                                 constant float4& viewBounds [[buffer(3)]],
                                 uint gid [[thread_position_in_grid]]) {

    if (gid >= MAX_SPRITES) {
        return;
    }

    device SpriteInstance& sprite = sprites[gid];

    // Check if sprite is within view bounds
    if (sprite.position.x >= viewBounds.x &&
        sprite.position.x <= viewBounds.z &&
        sprite.position.y >= viewBounds.y &&
        sprite.position.y <= viewBounds.w) {

        // Add to visible list
        uint index = atomic_fetch_add_explicit(visibleCount, 1, memory_order_relaxed);
        if (index < MAX_SPRITES) {
            visibleIndices[index] = gid;
        }
    }
}

// Compute shader for sprite batch processing
kernel void sprite_batch_transform(device SpriteInstance* sprites [[buffer(0)]],
                                  device SpriteVertexIn* vertices [[buffer(1)]],
                                  constant float4x4& transform [[buffer(2)]],
                                  uint gid [[thread_position_in_grid]]) {

    if (gid >= MAX_SPRITES) {
        return;
    }

    device SpriteInstance& sprite = sprites[gid];

    // Transform sprite position
    float4 worldPos = float4(sprite.position, 0.0, 1.0);
    float4 transformedPos = transform * worldPos;

    // Update vertex data (simplified - would need proper quad generation)
    uint vertexBase = gid * 6; // 6 vertices per sprite

    for (uint i = 0; i < 6; i++) {
        vertices[vertexBase + i].color.a *= sprite.alpha;
    }
}
