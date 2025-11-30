//
//  Tile.metal
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include <metal_stdlib>
#include "Common.metal"
using namespace metal;

// MARK: - Tile Layer Shaders

struct TileVertexIn {
    float2 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
    uint16_t tileId [[attribute(2)]];
    uint16_t flags [[attribute(3)]];
};

struct TileVertexOut {
    float4 position [[position]];
    float2 texCoord;
    float2 worldPos;
    uint16_t tileId;
    uint16_t flags;
};

// Vertex shader for tiles
vertex TileVertexOut tile_vertex(TileVertexIn in [[stage_in]],
                                constant Uniforms& uniforms [[buffer(1)]],
                                constant TileUniforms& tileUniforms [[buffer(2)]],
                                uint vertexID [[vertex_id]],
                                uint instanceID [[instance_id]]) {
    TileVertexOut out;

    // Apply viewport offset for seamless scrolling
    float2 scrolledPosition = in.position - tileUniforms.viewportOffset;

    // Transform to screen space
    float4 worldPos = float4(scrolledPosition, 0.0, 1.0);
    out.position = uniforms.projectionMatrix * uniforms.modelViewMatrix * worldPos;

    // Pass through texture coordinates and tile data
    out.texCoord = in.texCoord;
    out.worldPos = scrolledPosition;
    out.tileId = in.tileId;
    out.flags = in.flags;

    return out;
}

// Fragment shader for tile rendering
fragment float4 tile_fragment(TileVertexOut in [[stage_in]],
                             constant Uniforms& uniforms [[buffer(1)]],
                             constant TileUniforms& tileUniforms [[buffer(2)]],
                             texture2d<float> tileAtlas [[texture(0)]],
                             sampler tileSampler [[sampler(0)]]) {

    // Skip empty tiles
    if (in.tileId == 0) {
        discard_fragment();
    }

    // Calculate UV coordinates in tile atlas
    int atlasX = in.tileId % tileUniforms.atlasSize.x;
    int atlasY = in.tileId / tileUniforms.atlasSize.x;

    float2 atlasUVSize = 1.0 / float2(tileUniforms.atlasSize);
    float2 atlasUV = float2(atlasX, atlasY) * atlasUVSize;

    // Apply texture coordinate offset within the tile
    float2 finalTexCoord = atlasUV + (in.texCoord * atlasUVSize);

    // Handle tile flipping flags
    if (in.flags & 1) { // Horizontal flip
        finalTexCoord.x = atlasUV.x + atlasUVSize.x - (in.texCoord.x * atlasUVSize.x);
    }
    if (in.flags & 2) { // Vertical flip
        finalTexCoord.y = atlasUV.y + atlasUVSize.y - (in.texCoord.y * atlasUVSize.y);
    }

    // Sample the tile atlas
    float4 tileColor = tileAtlas.sample(tileSampler, finalTexCoord);

    // Discard fully transparent pixels
    if (tileColor.a < EPSILON) {
        discard_fragment();
    }

    return tileColor;
}

// Fragment shader with seamless scrolling visualization
fragment float4 tile_debug_fragment(TileVertexOut in [[stage_in]],
                                   constant Uniforms& uniforms [[buffer(1)]],
                                   constant TileUniforms& tileUniforms [[buffer(2)]],
                                   texture2d<float> tileAtlas [[texture(0)]],
                                   sampler tileSampler [[sampler(0)]]) {

    // Skip empty tiles
    if (in.tileId == 0) {
        discard_fragment();
    }

    // Calculate UV coordinates in tile atlas
    int atlasX = in.tileId % tileUniforms.atlasSize.x;
    int atlasY = in.tileId / tileUniforms.atlasSize.x;

    float2 atlasUVSize = 1.0 / float2(tileUniforms.atlasSize);
    float2 atlasUV = float2(atlasX, atlasY) * atlasUVSize;
    float2 finalTexCoord = atlasUV + (in.texCoord * atlasUVSize);

    // Sample the tile atlas
    float4 tileColor = tileAtlas.sample(tileSampler, finalTexCoord);

    // Add debug grid overlay
    float2 gridPos = fract(in.worldPos / tileUniforms.tileSize);
    float gridLine = step(0.95, max(gridPos.x, gridPos.y)) + step(max(gridPos.x, gridPos.y), 0.05);

    if (gridLine > 0.0) {
        tileColor = mix(tileColor, float4(1.0, 0.0, 1.0, 1.0), 0.3); // Magenta grid
    }

    // Color tint based on tile ID for debugging
    float4 debugTint = debugColor(in.tileId % 8);
    tileColor.rgb = mix(tileColor.rgb, debugTint.rgb, 0.1);

    return tileColor;
}

// Fragment shader with animated tiles
fragment float4 tile_animated_fragment(TileVertexOut in [[stage_in]],
                                      constant Uniforms& uniforms [[buffer(1)]],
                                      constant TileUniforms& tileUniforms [[buffer(2)]],
                                      texture2d<float> tileAtlas [[texture(0)]],
                                      sampler tileSampler [[sampler(0)]],
                                      constant int& animationFrames [[buffer(3)]],
                                      constant float& animationSpeed [[buffer(4)]]) {

    // Skip empty tiles
    if (in.tileId == 0) {
        discard_fragment();
    }

    // Calculate animation frame
    float animationTime = uniforms.time * animationSpeed;
    int currentFrame = int(animationTime) % animationFrames;

    // Offset tile ID by animation frame
    uint16_t animatedTileId = in.tileId + currentFrame;

    // Calculate UV coordinates in tile atlas
    int atlasX = animatedTileId % tileUniforms.atlasSize.x;
    int atlasY = animatedTileId / tileUniforms.atlasSize.x;

    float2 atlasUVSize = 1.0 / float2(tileUniforms.atlasSize);
    float2 atlasUV = float2(atlasX, atlasY) * atlasUVSize;
    float2 finalTexCoord = atlasUV + (in.texCoord * atlasUVSize);

    // Sample the tile atlas
    float4 tileColor = tileAtlas.sample(tileSampler, finalTexCoord);

    return tileColor;
}

// Fragment shader with parallax scrolling effect
fragment float4 tile_parallax_fragment(TileVertexOut in [[stage_in]],
                                       constant Uniforms& uniforms [[buffer(1)]],
                                       constant TileUniforms& tileUniforms [[buffer(2)]],
                                       texture2d<float> tileAtlas [[texture(0)]],
                                       sampler tileSampler [[sampler(0)]],
                                       constant float& parallaxFactor [[buffer(3)]]) {

    // Skip empty tiles
    if (in.tileId == 0) {
        discard_fragment();
    }

    // Apply parallax scrolling to texture coordinates
    float2 parallaxOffset = tileUniforms.viewportOffset * parallaxFactor;
    float2 parallaxTexCoord = in.texCoord + (parallaxOffset / tileUniforms.tileSize);

    // Calculate UV coordinates in tile atlas
    int atlasX = in.tileId % tileUniforms.atlasSize.x;
    int atlasY = in.tileId / tileUniforms.atlasSize.x;

    float2 atlasUVSize = 1.0 / float2(tileUniforms.atlasSize);
    float2 atlasUV = float2(atlasX, atlasY) * atlasUVSize;
    float2 finalTexCoord = atlasUV + (fract(parallaxTexCoord) * atlasUVSize);

    // Sample the tile atlas
    float4 tileColor = tileAtlas.sample(tileSampler, finalTexCoord);

    return tileColor;
}

// Fragment shader with lighting effects
fragment float4 tile_lit_fragment(TileVertexOut in [[stage_in]],
                                  constant Uniforms& uniforms [[buffer(1)]],
                                  constant TileUniforms& tileUniforms [[buffer(2)]],
                                  texture2d<float> tileAtlas [[texture(0)]],
                                  texture2d<float> normalMap [[texture(1)]],
                                  sampler tileSampler [[sampler(0)]],
                                  sampler normalSampler [[sampler(1)]],
                                  constant float3& lightDirection [[buffer(3)]],
                                  constant float4& lightColor [[buffer(4)]]) {

    // Skip empty tiles
    if (in.tileId == 0) {
        discard_fragment();
    }

    // Calculate UV coordinates in tile atlas
    int atlasX = in.tileId % tileUniforms.atlasSize.x;
    int atlasY = in.tileId / tileUniforms.atlasSize.x;

    float2 atlasUVSize = 1.0 / float2(tileUniforms.atlasSize);
    float2 atlasUV = float2(atlasX, atlasY) * atlasUVSize;
    float2 finalTexCoord = atlasUV + (in.texCoord * atlasUVSize);

    // Sample the tile atlas
    float4 tileColor = tileAtlas.sample(tileSampler, finalTexCoord);

    // Sample normal map if available
    float3 normal = float3(0.0, 0.0, 1.0); // Default normal
    if (normalMap.get_width() > 0) {
        float3 normalSample = normalMap.sample(normalSampler, finalTexCoord).rgb;
        normal = normalize(normalSample * 2.0 - 1.0);
    }

    // Calculate lighting
    float3 lightDir = normalize(-lightDirection);
    float lightIntensity = max(dot(normal, lightDir), 0.2); // Ambient light minimum

    // Apply lighting
    tileColor.rgb *= lightColor.rgb * lightIntensity;

    return tileColor;
}

// Compute shader for tile culling
kernel void tile_cull(device TileData* tiles [[buffer(0)]],
                     device uint* visibleTiles [[buffer(1)]],
                     device atomic_uint* visibleCount [[buffer(2)]],
                     constant TileUniforms& tileUniforms [[buffer(3)]],
                     constant float4& viewBounds [[buffer(4)]],
                     uint2 gid [[thread_position_in_grid]]) {

    uint tileIndex = gid.y * tileUniforms.atlasSize.x + gid.x;

    if (gid.x >= tileUniforms.atlasSize.x || gid.y >= tileUniforms.atlasSize.y) {
        return;
    }

    device TileData& tile = tiles[tileIndex];

    // Skip empty tiles
    if (tile.tileId == 0) {
        return;
    }

    // Calculate tile world position
    float2 tilePos = float2(gid) * tileUniforms.tileSize.x - tileUniforms.viewportOffset;

    // Check if tile is within view bounds
    if (tilePos.x + tileUniforms.tileSize.x >= viewBounds.x &&
        tilePos.x <= viewBounds.z &&
        tilePos.y + tileUniforms.tileSize.y >= viewBounds.y &&
        tilePos.y <= viewBounds.w) {

        // Add to visible list
        uint index = atomic_fetch_add_explicit(visibleCount, 1, memory_order_relaxed);
        visibleTiles[index] = tileIndex;
    }
}

// Compute shader for tile map updates
kernel void tile_update_map(device TileData* tileMap [[buffer(0)]],
                           constant int2& mapSize [[buffer(1)]],
                           constant int2& updateRegion [[buffer(2)]],
                           constant int2& regionSize [[buffer(3)]],
                           constant uint16_t& newTileId [[buffer(4)]],
                           uint2 gid [[thread_position_in_grid]]) {

    int2 pos = int2(gid) + updateRegion;

    if (pos.x < 0 || pos.x >= mapSize.x || pos.y < 0 || pos.y >= mapSize.y) {
        return;
    }

    if (int2(gid).x >= regionSize.x || int2(gid).y >= regionSize.y) {
        return;
    }

    uint index = pos.y * mapSize.x + pos.x;
    tileMap[index].tileId = newTileId;
}

// Compute shader for procedural tile generation
kernel void tile_generate_procedural(device TileData* tileMap [[buffer(0)]],
                                     constant int2& mapSize [[buffer(1)]],
                                     constant float& noiseScale [[buffer(2)]],
                                     constant float& heightThreshold [[buffer(3)]],
                                     constant uint16_t* tileTypes [[buffer(4)]],
                                     uint2 gid [[thread_position_in_grid]]) {

    if (gid.x >= uint(mapSize.x) || gid.y >= uint(mapSize.y)) {
        return;
    }

    uint index = gid.y * mapSize.x + gid.x;

    // Generate noise-based height value
    float2 noisePos = float2(gid) * noiseScale;
    float noise = fract(sin(dot(noisePos, float2(12.9898, 78.233))) * 43758.5453);

    // Select tile type based on height
    uint16_t tileId = tileTypes[0]; // Default tile

    if (noise < heightThreshold * 0.3) {
        tileId = tileTypes[1]; // Water
    } else if (noise < heightThreshold * 0.6) {
        tileId = tileTypes[2]; // Grass
    } else if (noise < heightThreshold * 0.8) {
        tileId = tileTypes[3]; // Stone
    } else {
        tileId = tileTypes[4]; // Mountain
    }

    tileMap[index].tileId = tileId;
    tileMap[index].flags = 0;
}
