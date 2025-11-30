//
//  Background.metal
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include <metal_stdlib>
#include "Common.metal"
using namespace metal;

// MARK: - Background Layer Shaders

struct BackgroundVertexIn {
    float2 position [[attribute(0)]];
};

struct BackgroundVertexOut {
    float4 position [[position]];
    float2 screenPos;
};

// Vertex shader for full-screen quad
vertex BackgroundVertexOut background_vertex(BackgroundVertexIn in [[stage_in]],
                                           constant Uniforms& uniforms [[buffer(1)]],
                                           constant BackgroundUniforms& bgUniforms [[buffer(2)]]) {
    BackgroundVertexOut out;

    // Transform to screen space
    float4 worldPos = float4(in.position, 0.0, 1.0);
    out.position = uniforms.projectionMatrix * uniforms.modelViewMatrix * worldPos;

    // Pass screen position for effects
    out.screenPos = (in.position + 1.0) * 0.5; // Convert from [-1,1] to [0,1]

    return out;
}

// Fragment shader for solid background with optional effects
fragment float4 background_fragment(BackgroundVertexOut in [[stage_in]],
                                  constant Uniforms& uniforms [[buffer(1)]],
                                  constant BackgroundUniforms& bgUniforms [[buffer(2)]]) {
    float4 color = bgUniforms.backgroundColor;

    // Optional retro CRT effect for background
    #ifdef ENABLE_CRT_EFFECT
    color = crtEffect(color, in.screenPos, uniforms.screenSize, uniforms.time);
    #endif

    return color;
}

// Alternative gradient background shader
fragment float4 background_gradient_fragment(BackgroundVertexOut in [[stage_in]],
                                           constant Uniforms& uniforms [[buffer(1)]],
                                           constant BackgroundUniforms& bgUniforms [[buffer(2)]]) {
    float4 topColor = bgUniforms.backgroundColor;
    float4 bottomColor = float4(topColor.rgb * 0.5, topColor.a); // Darker bottom

    // Vertical gradient
    float4 color = mix(bottomColor, topColor, in.screenPos.y);

    // Optional scanline effect
    #ifdef ENABLE_SCANLINES
    float scanline = sin(in.screenPos.y * uniforms.screenSize.y * 0.5) * 0.05 + 0.95;
    color.rgb *= scanline;
    #endif

    return color;
}

// Animated background with subtle effects
fragment float4 background_animated_fragment(BackgroundVertexOut in [[stage_in]],
                                           constant Uniforms& uniforms [[buffer(1)]],
                                           constant BackgroundUniforms& bgUniforms [[buffer(2)]]) {
    float4 baseColor = bgUniforms.backgroundColor;

    // Subtle color animation
    float timePhase = sin(uniforms.time * 0.5) * 0.02 + 1.0;
    float4 color = baseColor * timePhase;

    // Gentle vignette effect
    float2 centered = (in.screenPos - 0.5) * 2.0;
    float vignette = 1.0 - length(centered) * 0.1;
    color.rgb *= vignette;

    // Subtle noise for texture
    float noise = fract(sin(dot(in.screenPos * uniforms.screenSize, float2(12.9898, 78.233))) * 43758.5453);
    color.rgb += (noise - 0.5) * 0.01;

    return color;
}

// Commodore 64 style background with border
fragment float4 background_c64_fragment(BackgroundVertexOut in [[stage_in]],
                                       constant Uniforms& uniforms [[buffer(1)]],
                                       constant BackgroundUniforms& bgUniforms [[buffer(2)]]) {
    float4 color = bgUniforms.backgroundColor;

    // C64-style border effect
    float2 screenSize = uniforms.screenSize;
    float borderSize = 32.0; // pixels

    float2 borderUV = in.screenPos * screenSize;
    bool inBorder = (borderUV.x < borderSize || borderUV.x > screenSize.x - borderSize ||
                     borderUV.y < borderSize || borderUV.y > screenSize.y - borderSize);

    if (inBorder) {
        // Slightly darker border color
        color.rgb *= 0.8;
    }

    // Subtle phosphor glow
    float glow = 1.0 + sin(uniforms.time * 3.0) * 0.01;
    color.rgb *= glow;

    return color;
}
