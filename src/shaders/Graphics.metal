//
//  Graphics.metal
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include <metal_stdlib>
#include "Common.metal"
using namespace metal;

// MARK: - Graphics Layer Shaders

struct GraphicsVertexIn {
    float2 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
    float4 color [[attribute(2)]];
};

struct GraphicsVertexOut {
    float4 position [[position]];
    float2 texCoord;
    float4 color;
};

// Vertex shader for graphics layer
vertex GraphicsVertexOut graphics_vertex(GraphicsVertexIn in [[stage_in]],
                                        constant Uniforms& uniforms [[buffer(1)]],
                                        constant GraphicsUniforms& graphicsUniforms [[buffer(2)]]) {
    GraphicsVertexOut out;

    // Transform position
    float4 worldPos = float4(in.position, 0.0, 1.0);
    out.position = uniforms.projectionMatrix * uniforms.modelViewMatrix * worldPos;

    // Pass through texture coordinates and color
    out.texCoord = in.texCoord;
    out.color = in.color;

    return out;
}

// Fragment shader for graphics layer (displays backing texture)
fragment float4 graphics_fragment(GraphicsVertexOut in [[stage_in]],
                                 constant Uniforms& uniforms [[buffer(1)]],
                                 constant GraphicsUniforms& graphicsUniforms [[buffer(2)]],
                                 texture2d<float> backingTexture [[texture(0)]],
                                 sampler textureSampler [[sampler(0)]]) {

    // Sample the backing texture (contains Skia-rendered content)
    float4 texColor = backingTexture.sample(textureSampler, in.texCoord);

    // Apply vertex color modulation
    float4 finalColor = texColor * in.color;

    return finalColor;
}

// Alternative fragment shader for direct primitive rendering
fragment float4 graphics_primitive_fragment(GraphicsVertexOut in [[stage_in]],
                                           constant Uniforms& uniforms [[buffer(1)]],
                                           constant GraphicsUniforms& graphicsUniforms [[buffer(2)]]) {

    // Direct color rendering for when drawing primitives without texture
    return in.color;
}

// Line rendering with anti-aliasing
fragment float4 graphics_line_fragment(GraphicsVertexOut in [[stage_in]],
                                      constant Uniforms& uniforms [[buffer(1)]],
                                      constant GraphicsUniforms& graphicsUniforms [[buffer(2)]]) {

    // Calculate distance from line center for anti-aliasing
    float2 coord = in.texCoord;
    float distance = abs(coord.y - 0.5) * 2.0; // Distance from line center

    // Apply anti-aliasing
    float alpha = smoothstep(graphicsUniforms.lineWidth + graphicsUniforms.antialiasRadius,
                            graphicsUniforms.lineWidth - graphicsUniforms.antialiasRadius,
                            distance);

    float4 color = in.color;
    color.a *= alpha;

    return color;
}

// Circle rendering with anti-aliasing
fragment float4 graphics_circle_fragment(GraphicsVertexOut in [[stage_in]],
                                        constant Uniforms& uniforms [[buffer(1)]],
                                        constant GraphicsUniforms& graphicsUniforms [[buffer(2)]]) {

    // Calculate distance from center
    float2 center = float2(0.5, 0.5);
    float2 coord = in.texCoord;
    float distance = length(coord - center);

    // Apply anti-aliasing for circle edge
    float radius = 0.5;
    float alpha = smoothstep(radius + graphicsUniforms.antialiasRadius,
                            radius - graphicsUniforms.antialiasRadius,
                            distance);

    float4 color = in.color;
    color.a *= (1.0 - alpha);

    return color;
}

// Filled circle with anti-aliasing
fragment float4 graphics_filled_circle_fragment(GraphicsVertexOut in [[stage_in]],
                                               constant Uniforms& uniforms [[buffer(1)]],
                                               constant GraphicsUniforms& graphicsUniforms [[buffer(2)]]) {

    // Calculate distance from center
    float2 center = float2(0.5, 0.5);
    float2 coord = in.texCoord;
    float distance = length(coord - center);

    // Apply anti-aliasing for filled circle
    float radius = 0.5;
    float alpha = 1.0 - smoothstep(radius - graphicsUniforms.antialiasRadius,
                                  radius + graphicsUniforms.antialiasRadius,
                                  distance);

    float4 color = in.color;
    color.a *= alpha;

    return color;
}

// Rectangle with anti-aliased edges
fragment float4 graphics_rect_fragment(GraphicsVertexOut in [[stage_in]],
                                      constant Uniforms& uniforms [[buffer(1)]],
                                      constant GraphicsUniforms& graphicsUniforms [[buffer(2)]]) {

    float2 coord = in.texCoord;

    // Calculate distance to rectangle edges
    float2 rectSize = float2(1.0, 1.0);
    float2 center = float2(0.5, 0.5);

    float distance = edgeDistance(coord, center, rectSize);

    // Apply anti-aliasing
    float alpha = smoothAlpha(distance, graphicsUniforms.antialiasRadius);

    float4 color = in.color;
    color.a *= (1.0 - alpha);

    return color;
}

// Gradient fill fragment shader
fragment float4 graphics_gradient_fragment(GraphicsVertexOut in [[stage_in]],
                                          constant Uniforms& uniforms [[buffer(1)]],
                                          constant GraphicsUniforms& graphicsUniforms [[buffer(2)]],
                                          constant float4* gradientColors [[buffer(3)]],
                                          constant float* gradientStops [[buffer(4)]],
                                          constant int& gradientCount [[buffer(5)]]) {

    // Linear gradient based on texture coordinate
    float t = in.texCoord.x; // Horizontal gradient

    // Find the appropriate gradient segment
    float4 color = gradientColors[0];

    for (int i = 0; i < gradientCount - 1; i++) {
        if (t >= gradientStops[i] && t <= gradientStops[i + 1]) {
            float segmentT = (t - gradientStops[i]) / (gradientStops[i + 1] - gradientStops[i]);
            color = mix(gradientColors[i], gradientColors[i + 1], segmentT);
            break;
        }
    }

    return color;
}

// Pattern fill fragment shader
fragment float4 graphics_pattern_fragment(GraphicsVertexOut in [[stage_in]],
                                         constant Uniforms& uniforms [[buffer(1)]],
                                         constant GraphicsUniforms& graphicsUniforms [[buffer(2)]],
                                         texture2d<float> patternTexture [[texture(1)]],
                                         sampler patternSampler [[sampler(1)]]) {

    // Tile the pattern texture
    float2 patternCoord = fract(in.texCoord * 4.0); // 4x4 tiling
    float4 patternColor = patternTexture.sample(patternSampler, patternCoord);

    // Modulate with vertex color
    return patternColor * in.color;
}

// Bezier curve rendering
fragment float4 graphics_bezier_fragment(GraphicsVertexOut in [[stage_in]],
                                        constant Uniforms& uniforms [[buffer(1)]],
                                        constant GraphicsUniforms& graphicsUniforms [[buffer(2)]]) {

    // Quadratic bezier curve evaluation
    float2 coord = in.texCoord;

    // Curve parameters (would be passed as uniforms in real implementation)
    float t = coord.x;
    float curve = t * t; // Simple quadratic curve

    float distance = abs(coord.y - curve);

    // Apply anti-aliasing
    float alpha = smoothstep(graphicsUniforms.lineWidth + graphicsUniforms.antialiasRadius,
                            graphicsUniforms.lineWidth - graphicsUniforms.antialiasRadius,
                            distance);

    float4 color = in.color;
    color.a *= alpha;

    return color;
}

// Compute shader for bitmap operations
kernel void graphics_bitmap_operation(texture2d<float, access::read> inputTexture [[texture(0)]],
                                     texture2d<float, access::write> outputTexture [[texture(1)]],
                                     constant int& operation [[buffer(0)]],
                                     constant float4& parameters [[buffer(1)]],
                                     uint2 gid [[thread_position_in_grid]]) {

    if (gid.x >= outputTexture.get_width() || gid.y >= outputTexture.get_height()) {
        return;
    }

    float4 inputColor = inputTexture.read(gid);
    float4 outputColor = inputColor;

    // Apply bitmap operation based on operation type
    switch (operation) {
        case 0: // Brightness
            outputColor.rgb *= parameters.x;
            break;
        case 1: // Contrast
            outputColor.rgb = (outputColor.rgb - 0.5) * parameters.x + 0.5;
            break;
        case 2: // Saturation
            {
                float gray = dot(outputColor.rgb, float3(0.299, 0.587, 0.114));
                outputColor.rgb = mix(float3(gray), outputColor.rgb, parameters.x);
            }
            break;
        case 3: // Hue shift
            // Simplified hue shift (would need proper HSV conversion)
            outputColor.rgb = outputColor.bgr;
            break;
        default:
            break;
    }

    outputTexture.write(outputColor, gid);
}

// Blur effect compute shader
kernel void graphics_blur(texture2d<float, access::read> inputTexture [[texture(0)]],
                         texture2d<float, access::write> outputTexture [[texture(1)]],
                         constant float& blurRadius [[buffer(0)]],
                         uint2 gid [[thread_position_in_grid]]) {

    if (gid.x >= outputTexture.get_width() || gid.y >= outputTexture.get_height()) {
        return;
    }

    float2 texelSize = 1.0 / float2(inputTexture.get_width(), inputTexture.get_height());
    float4 color = float4(0.0);
    float totalWeight = 0.0;

    int radius = int(blurRadius);

    // Gaussian blur
    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            uint2 samplePos = uint2(int(gid.x) + x, int(gid.y) + y);

            if (samplePos.x < inputTexture.get_width() && samplePos.y < inputTexture.get_height()) {
                float distance = length(float2(x, y));
                float weight = exp(-distance * distance / (2.0 * blurRadius * blurRadius));

                color += inputTexture.read(samplePos) * weight;
                totalWeight += weight;
            }
        }
    }

    if (totalWeight > 0.0) {
        color /= totalWeight;
    }

    outputTexture.write(color, gid);
}
