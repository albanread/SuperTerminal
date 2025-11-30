//
//  Text.metal
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include <metal_stdlib>
#include "Common.metal"
using namespace metal;

// MARK: - Text Layer Shaders

struct TextVertexIn {
    float2 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
    float4 inkColor [[attribute(2)]];
    float4 paperColor [[attribute(3)]];
    uint32_t unicode [[attribute(4)]];
    float2 gridPos [[attribute(5)]];
};

struct TextVertexOut {
    float4 position [[position]];
    float2 texCoord;
    float4 inkColor;
    float4 paperColor;
    float2 gridPos;
    uint32_t unicode;
};

// Vertex shader for text characters
vertex TextVertexOut text_vertex(TextVertexIn in [[stage_in]],
                                constant Uniforms& uniforms [[buffer(1)]],
                                constant TextUniforms& textUniforms [[buffer(2)]],
                                uint vertexID [[vertex_id]],
                                uint instanceID [[instance_id]]) {
    TextVertexOut out;

    // Use grid position from vertex attribute
    out.gridPos = in.gridPos;

    // Calculate character cell position
    float2 cellPos = in.gridPos * textUniforms.cellSize;
    float2 worldPos = cellPos + in.position * textUniforms.cellSize;

    // Transform to screen space
    float4 screenPos = float4(worldPos, 0.0, 1.0);
    out.position = uniforms.projectionMatrix * uniforms.modelViewMatrix * screenPos;

    // Pass through texture coordinates and colors
    out.texCoord = in.texCoord;
    out.inkColor = in.inkColor;
    out.paperColor = in.paperColor;
    out.unicode = in.unicode;

    return out;
}

// Fragment shader for text rendering
fragment float4 text_fragment(TextVertexOut in [[stage_in]],
                             constant Uniforms& uniforms [[buffer(1)]],
                             constant TextUniforms& textUniforms [[buffer(2)]],
                             texture2d<float> fontAtlas [[texture(0)]],
                             sampler fontSampler [[sampler(0)]]) {

    // Sample the font atlas
    float4 glyphSample = fontAtlas.sample(fontSampler, in.texCoord);

    // Use alpha channel as mask for the glyph
    float glyphAlpha = glyphSample.a;

    // Blend ink and paper colors
    float4 finalColor;
    if (glyphAlpha > EPSILON) {
        // Character pixel - use ink color
        finalColor = in.inkColor;
        finalColor.a *= glyphAlpha;
    } else {
        // Background pixel - use paper color
        finalColor = in.paperColor;
    }

    // Handle cursor rendering
    bool isCursorPos = (int(in.gridPos.x) == textUniforms.cursorX &&
                       int(in.gridPos.y) == textUniforms.cursorY);

    if (isCursorPos && textUniforms.cursorVisible) {
        float cursorPhase = sin(textUniforms.cursorBlink * TWO_PI);
        if (cursorPhase > 0.0) {
            // Cursor is visible - invert colors or use cursor color
            if (glyphAlpha > EPSILON) {
                // On character - use paper color for cursor
                finalColor = in.paperColor;
                finalColor.a = 1.0;
            } else {
                // On background - use ink color for cursor
                finalColor = in.inkColor;
                finalColor.a = 1.0;
            }
        }
    }

    return finalColor;
}

// Alternative fragment shader with C64-style effects
fragment float4 text_c64_fragment(TextVertexOut in [[stage_in]],
                                 constant Uniforms& uniforms [[buffer(1)]],
                                 constant TextUniforms& textUniforms [[buffer(2)]],
                                 texture2d<float> fontAtlas [[texture(0)]],
                                 sampler fontSampler [[sampler(0)]]) {

    // Sample the font atlas with slight blur for C64 effect
    float4 glyphSample = fontAtlas.sample(fontSampler, in.texCoord);
    float glyphAlpha = glyphSample.a;

    // Add slight glow effect to characters
    float2 texelSize = 1.0 / float2(fontAtlas.get_width(), fontAtlas.get_height());
    float4 glowSample = 0.0;

    // Sample surrounding pixels for glow
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            float2 offset = float2(dx, dy) * texelSize * 0.5;
            glowSample += fontAtlas.sample(fontSampler, in.texCoord + offset);
        }
    }
    glowSample /= 9.0;

    // Combine main glyph with glow
    float combinedAlpha = max(glyphAlpha, glowSample.a * 0.3);

    float4 finalColor;
    if (combinedAlpha > EPSILON) {
        if (glyphAlpha > glowSample.a * 0.3) {
            // Main character pixel
            finalColor = in.inkColor;
        } else {
            // Glow pixel
            finalColor = in.inkColor * 0.6;
        }
        finalColor.a *= combinedAlpha;
    } else {
        // Background pixel
        finalColor = in.paperColor;
    }

    // Cursor handling (same as basic version)
    bool isCursorPos = (int(in.gridPos.x) == textUniforms.cursorX &&
                       int(in.gridPos.y) == textUniforms.cursorY);

    if (isCursorPos && textUniforms.cursorVisible) {
        float cursorPhase = sin(textUniforms.cursorBlink * TWO_PI);
        if (cursorPhase > 0.0) {
            if (glyphAlpha > EPSILON) {
                finalColor = in.paperColor;
                finalColor.a = 1.0;
            } else {
                finalColor = in.inkColor;
                finalColor.a = 1.0;
            }
        }
    }

    // Add subtle scanline effect
    float2 screenPos = in.position.xy / uniforms.screenSize;
    float scanline = sin(screenPos.y * uniforms.screenSize.y * 0.5) * 0.05 + 0.95;
    finalColor.rgb *= scanline;

    return finalColor;
}

// Syntax highlighting fragment shader for editor layer
fragment float4 text_editor_fragment(TextVertexOut in [[stage_in]],
                                    constant Uniforms& uniforms [[buffer(1)]],
                                    constant TextUniforms& textUniforms [[buffer(2)]],
                                    texture2d<float> fontAtlas [[texture(0)]],
                                    sampler fontSampler [[sampler(0)]]) {

    // Sample the font atlas
    float4 glyphSample = fontAtlas.sample(fontSampler, in.texCoord);
    float glyphAlpha = glyphSample.a;

    // Enhanced text rendering for code editor
    float4 finalColor;

    if (glyphAlpha > EPSILON) {
        // Character pixel - use syntax-highlighted ink color
        finalColor = in.inkColor;
        finalColor.a *= glyphAlpha;

        // Add slight anti-aliasing
        float2 texelSize = 1.0 / float2(fontAtlas.get_width(), fontAtlas.get_height());
        float4 smoothSample = sampleTextureFiltered(fontAtlas, fontSampler, in.texCoord, texelSize);
        finalColor.a = smoothSample.a;
    } else {
        // Background pixel - use semi-transparent paper for editor
        finalColor = in.paperColor;
        finalColor.a *= 0.9; // Slight transparency for overlay effect
    }

    // Editor cursor (different style from terminal cursor)
    bool isCursorPos = (int(in.gridPos.x) == textUniforms.cursorX &&
                       int(in.gridPos.y) == textUniforms.cursorY);

    if (isCursorPos && textUniforms.cursorVisible) {
        float cursorPhase = sin(textUniforms.cursorBlink * TWO_PI * 2.0); // Faster blink
        if (cursorPhase > 0.0) {
            // Editor cursor is a vertical line
            float2 localPos = fract(in.texCoord);
            if (localPos.x < 0.1) { // Left edge of character cell
                finalColor = float4(1.0, 1.0, 1.0, 1.0); // White cursor line
            }
        }
    }

    // Add subtle border for editor mode
    float2 screenPos = in.position.xy / uniforms.screenSize;
    float2 border = step(0.02, screenPos) * step(0.02, 1.0 - screenPos);
    float borderMask = border.x * border.y;

    if (borderMask < 1.0) {
        finalColor = mix(float4(0.2, 0.2, 0.8, 0.8), finalColor, borderMask);
    }

    return finalColor;
}

// Debug fragment shader to visualize character cells
fragment float4 text_debug_fragment(TextVertexOut in [[stage_in]],
                                   constant Uniforms& uniforms [[buffer(1)]],
                                   constant TextUniforms& textUniforms [[buffer(2)]],
                                   texture2d<float> fontAtlas [[texture(0)]],
                                   sampler fontSampler [[sampler(0)]]) {

    // Sample the font atlas
    float4 glyphSample = fontAtlas.sample(fontSampler, in.texCoord);
    float glyphAlpha = glyphSample.a;

    // Show character cell boundaries
    float2 localPos = fract(in.texCoord);
    bool isBorder = (localPos.x < 0.05 || localPos.x > 0.95 ||
                    localPos.y < 0.05 || localPos.y > 0.95);

    float4 finalColor;

    if (isBorder) {
        // Cell border - show in debug color
        finalColor = float4(1.0, 0.0, 1.0, 0.5); // Magenta border
    } else if (glyphAlpha > EPSILON) {
        // Character pixel
        finalColor = in.inkColor;
        finalColor.a *= glyphAlpha;
    } else {
        // Background pixel
        finalColor = in.paperColor;
    }

    // Show grid coordinates as color tint
    float2 gridNorm = in.gridPos / textUniforms.gridSize;
    finalColor.rgb *= (1.0 + gridNorm.x * 0.2);
    finalColor.rgb *= (1.0 + gridNorm.y * 0.2);

    return finalColor;
}

// Compute shader for updating character buffer
kernel void text_update_buffer(device CharacterData* characters [[buffer(0)]],
                              constant TextUniforms& textUniforms [[buffer(1)]],
                              uint2 gid [[thread_position_in_grid]]) {

    uint gridWidth = uint(textUniforms.gridSize.x);
    uint gridHeight = uint(textUniforms.gridSize.y);

    if (gid.x >= gridWidth || gid.y >= gridHeight) {
        return;
    }

    uint index = gid.y * gridWidth + gid.x;
    device CharacterData& character = characters[index];

    // Update character data (this would be called from CPU with actual character data)
    // For now, just ensure UV coordinates are set properly

    if (character.unicode != 0) {
        // Calculate UV coordinates in font atlas based on unicode value
        // This is a simplified version - real implementation would have a character map
        uint atlasWidth = 16; // Assuming 16x16 character atlas
        uint charIndex = character.unicode % 256;
        uint atlasX = charIndex % atlasWidth;
        uint atlasY = charIndex / atlasWidth;

        float2 uvSize = float2(1.0 / float(atlasWidth), 1.0 / float(atlasWidth));
        character.uvOffset = float2(atlasX, atlasY) * uvSize;
        character.uvSize = uvSize;
    }
}
