//
//  Common.metal
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include <metal_stdlib>
using namespace metal;

// MARK: - Common Structures

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
    float4 color [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
    float4 color;
};

struct Uniforms {
    float4x4 projectionMatrix;
    float4x4 modelViewMatrix;
    float2 screenSize;
    float time;
    float deltaTime;
};

// MARK: - Layer-Specific Uniforms

struct BackgroundUniforms {
    float4 backgroundColor;
};

struct TileUniforms {
    float2 viewportOffset;      // Sub-tile offset (0-127)
    int2 viewportPosition;      // Position in tilemap
    float2 tileSize;            // Tile size in pixels (128x128)
    int2 atlasSize;             // Atlas dimensions in tiles
};

struct GraphicsUniforms {
    float4x4 transform;
    float lineWidth;
    float antialiasRadius;
};

struct TextUniforms {
    float2 gridSize;            // 80x25
    float2 cellSize;            // Size of each character cell in pixels
    float cursorBlink;          // Cursor blink phase (0-1)
    int cursorX, cursorY;       // Cursor position
    bool cursorVisible;         // Cursor visibility
};

struct SpriteUniforms {
    int spriteCount;            // Number of active sprites
};

// MARK: - Sprite Instance Data

struct SpriteInstance {
    float2 position;            // Screen position
    float2 scale;               // Scale factor (1.0 = normal)
    float rotation;             // Rotation in radians
    float alpha;                // Alpha transparency
    uint16_t textureId;         // Texture ID in atlas
    uint16_t padding;           // Alignment padding
};

// MARK: - Text Character Data

struct CharacterData {
    uint32_t unicode;           // Unicode codepoint
    float4 inkColor;            // Foreground color
    float4 paperColor;          // Background color
    float2 uvOffset;            // UV offset in font atlas
    float2 uvSize;              // UV size in font atlas
};

// MARK: - Tile Data

struct TileData {
    uint16_t tileId;            // Tile ID
    uint16_t flags;             // Tile flags (flip, rotate, etc.)
};

// MARK: - Color Utilities

float4 unpackRGBA(uint32_t packed) {
    float r = float((packed >> 24) & 0xFF) / 255.0;
    float g = float((packed >> 16) & 0xFF) / 255.0;
    float b = float((packed >> 8) & 0xFF) / 255.0;
    float a = float(packed & 0xFF) / 255.0;
    return float4(r, g, b, a);
}

uint32_t packRGBA(float4 color) {
    uint32_t r = uint32_t(clamp(color.r, 0.0, 1.0) * 255.0);
    uint32_t g = uint32_t(clamp(color.g, 0.0, 1.0) * 255.0);
    uint32_t b = uint32_t(clamp(color.b, 0.0, 1.0) * 255.0);
    uint32_t a = uint32_t(clamp(color.a, 0.0, 1.0) * 255.0);
    return (r << 24) | (g << 16) | (b << 8) | a;
}

// Gamma correction for better color blending
float4 linearToSRGB(float4 color) {
    float4 result;
    result.rgb = select(
        color.rgb * 12.92,
        pow(color.rgb, 1.0/2.4) * 1.055 - 0.055,
        color.rgb > 0.0031308
    );
    result.a = color.a;
    return result;
}

float4 sRGBToLinear(float4 color) {
    float4 result;
    result.rgb = select(
        color.rgb / 12.92,
        pow((color.rgb + 0.055) / 1.055, 2.4),
        color.rgb > 0.04045
    );
    result.a = color.a;
    return result;
}

// MARK: - Mathematical Utilities

float2 rotate2D(float2 point, float angle) {
    float cosA = cos(angle);
    float sinA = sin(angle);
    return float2(
        point.x * cosA - point.y * sinA,
        point.x * sinA + point.y * cosA
    );
}

float4x4 orthographicProjection(float left, float right, float bottom, float top, float nearZ, float farZ) {
    float4x4 result = float4x4(0.0);
    result[0][0] = 2.0 / (right - left);
    result[1][1] = 2.0 / (top - bottom);
    result[2][2] = -2.0 / (farZ - nearZ);
    result[3][0] = -(right + left) / (right - left);
    result[3][1] = -(top + bottom) / (top - bottom);
    result[3][2] = -(farZ + nearZ) / (farZ - nearZ);
    result[3][3] = 1.0;
    return result;
}

float4x4 translationMatrix(float2 translation) {
    float4x4 result = float4x4(1.0);
    result[3][0] = translation.x;
    result[3][1] = translation.y;
    return result;
}

float4x4 scaleMatrix(float2 scale) {
    float4x4 result = float4x4(0.0);
    result[0][0] = scale.x;
    result[1][1] = scale.y;
    result[2][2] = 1.0;
    result[3][3] = 1.0;
    return result;
}

float4x4 rotationMatrix(float angle) {
    float cosA = cos(angle);
    float sinA = sin(angle);

    float4x4 result = float4x4(0.0);
    result[0][0] = cosA;
    result[0][1] = sinA;
    result[1][0] = -sinA;
    result[1][1] = cosA;
    result[2][2] = 1.0;
    result[3][3] = 1.0;
    return result;
}

// MARK: - Texture Sampling Utilities

float4 sampleTexture(texture2d<float> tex, sampler smp, float2 uv) {
    return tex.sample(smp, uv);
}

float4 sampleTextureFiltered(texture2d<float> tex, sampler smp, float2 uv, float2 texelSize) {
    // Simple bilinear filtering for pixel art
    float2 pixelCoord = uv / texelSize;
    float2 fracCoord = fract(pixelCoord);
    float2 baseCoord = (floor(pixelCoord) + 0.5) * texelSize;

    float4 tl = tex.sample(smp, baseCoord);
    float4 tr = tex.sample(smp, baseCoord + float2(texelSize.x, 0.0));
    float4 bl = tex.sample(smp, baseCoord + float2(0.0, texelSize.y));
    float4 br = tex.sample(smp, baseCoord + texelSize);

    float4 top = mix(tl, tr, fracCoord.x);
    float4 bottom = mix(bl, br, fracCoord.x);
    return mix(top, bottom, fracCoord.y);
}

// MARK: - Anti-aliasing Utilities

float edgeDistance(float2 position, float2 center, float2 size) {
    float2 d = abs(position - center) - size * 0.5;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float circleDistance(float2 position, float2 center, float radius) {
    return length(position - center) - radius;
}

float smoothAlpha(float distance, float radius) {
    return 1.0 - smoothstep(-radius, radius, distance);
}

// MARK: - Blending Functions

float4 alphaBlend(float4 source, float4 destination) {
    float sourceAlpha = source.a;
    float destinationAlpha = destination.a * (1.0 - sourceAlpha);
    float finalAlpha = sourceAlpha + destinationAlpha;

    if (finalAlpha == 0.0) {
        return float4(0.0);
    }

    float3 finalColor = (source.rgb * sourceAlpha + destination.rgb * destinationAlpha) / finalAlpha;
    return float4(finalColor, finalAlpha);
}

float4 additiveBlend(float4 source, float4 destination) {
    return float4(source.rgb + destination.rgb, max(source.a, destination.a));
}

float4 multiplyBlend(float4 source, float4 destination) {
    return float4(source.rgb * destination.rgb, source.a * destination.a);
}

// MARK: - Animation Utilities

float easeInOut(float t) {
    return t * t * (3.0 - 2.0 * t);
}

float easeInOutCubic(float t) {
    return t < 0.5 ? 4.0 * t * t * t : 1.0 - pow(-2.0 * t + 2.0, 3.0) / 2.0;
}

float bounce(float t) {
    if (t < 1.0/2.75) {
        return 7.5625 * t * t;
    } else if (t < 2.0/2.75) {
        t -= 1.5/2.75;
        return 7.5625 * t * t + 0.75;
    } else if (t < 2.5/2.75) {
        t -= 2.25/2.75;
        return 7.5625 * t * t + 0.9375;
    } else {
        t -= 2.625/2.75;
        return 7.5625 * t * t + 0.984375;
    }
}

// MARK: - Retro Effects

float4 crtEffect(float4 color, float2 screenPos, float2 screenSize, float time) {
    // Scanline effect
    float scanline = sin(screenPos.y * screenSize.y * 0.5) * 0.1 + 0.9;

    // Screen curvature
    float2 centered = (screenPos - 0.5) * 2.0;
    float vignette = 1.0 - length(centered) * 0.3;

    // Phosphor glow
    float glow = 1.0 + sin(time * 2.0) * 0.02;

    return color * scanline * vignette * glow;
}

float4 pixelateEffect(texture2d<float> tex, sampler smp, float2 uv, float pixelSize) {
    float2 pixelated = floor(uv / pixelSize) * pixelSize;
    return tex.sample(smp, pixelated);
}

// MARK: - Debug Utilities

float4 debugColor(uint32_t index) {
    float4 colors[] = {
        float4(1.0, 0.0, 0.0, 1.0), // Red
        float4(0.0, 1.0, 0.0, 1.0), // Green
        float4(0.0, 0.0, 1.0, 1.0), // Blue
        float4(1.0, 1.0, 0.0, 1.0), // Yellow
        float4(1.0, 0.0, 1.0, 1.0), // Magenta
        float4(0.0, 1.0, 1.0, 1.0), // Cyan
        float4(1.0, 0.5, 0.0, 1.0), // Orange
        float4(0.5, 0.0, 1.0, 1.0)  // Purple
    };
    return colors[index % 8];
}

float4 wireframeColor(float2 barycentric, float4 color) {
    float3 d = fwidth(float3(barycentric, 0.0));
    float3 a3 = smoothstep(float3(0.0), d * 1.5, float3(barycentric, 0.0));
    return mix(float4(1.0, 1.0, 1.0, 1.0), color, min(min(a3.x, a3.y), a3.z));
}

// MARK: - Constants

constant float PI = 3.14159265359;
constant float TWO_PI = 6.28318530718;
constant float HALF_PI = 1.57079632679;
constant float SQRT2 = 1.41421356237;
constant float EPSILON = 1e-6;

// Screen coordinates conversion
constant float2 SCREEN_ORIGIN = float2(0.0, 0.0);
constant float2 TEXT_GRID_SIZE = float2(80.0, 25.0);
constant float2 TILE_SIZE = float2(128.0, 128.0);
constant int MAX_SPRITES = 256;
