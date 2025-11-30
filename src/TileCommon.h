//
//  TileCommon.h
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#ifndef TILECOMMON_H
#define TILECOMMON_H

#import <simd/simd.h>
#include <stdint.h>
#include <vector>
#include <algorithm>
#include <memory>

// Tile system constants
static const int VIEWPORT_WIDTH = 32;   // 32 tiles wide
static const int VIEWPORT_HEIGHT = 24;  // 24 tiles tall
static const int TILE_SIZE = 128;       // 128x128 pixels per tile
static const int MAX_TILES = 256;       // Maximum tiles in atlas (16x16 grid)

// Viewport structure for tracking position and offset
struct Viewport {
    int x, y;                   // Tile map position (which tiles are at viewport origin)
    float offsetX, offsetY;     // Sub-tile pixel offset (0.0 to 127.999)
};

// Tile data structure
struct TileData {
    uint16_t tileId;            // Tile ID in atlas (0-255)
    uint16_t flags;             // Tile flags (flip, rotate, etc.)
};

// Vertex structure for tile rendering
struct TileVertex {
    simd_float2 position;       // World position in pixels
    simd_float2 texCoord;       // UV coordinates in tile atlas
    uint16_t tileId;            // Tile ID for debugging
    uint16_t flags;             // Tile flags
};

// Tile uniforms for shader
struct TileUniforms {
    simd_float2 viewportOffset;      // Sub-tile offset (0-127)
    simd_int2 viewportPosition;      // Position in tilemap
    simd_float2 tileSize;            // Tile size in pixels (128x128)
    simd_int2 atlasSize;             // Atlas dimensions in tiles (16x16)
};

// Simple tile map class
class TileMap {
private:
    std::vector<uint16_t> tiles;
    
public:
    int width, height;
    
    TileMap(int w, int h) : width(w), height(h) {
        tiles.resize(w * h, 0);
    }
    
    uint16_t getTile(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return 0;
        return tiles[y * width + x];
    }
    
    void setTile(int x, int y, uint16_t tileId) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        tiles[y * width + x] = tileId;
    }
    
    void resize(int newWidth, int newHeight) {
        std::vector<uint16_t> newTiles(newWidth * newHeight, 0);
        
        // Copy existing tiles
        int copyWidth = std::min(width, newWidth);
        int copyHeight = std::min(height, newHeight);
        
        for (int y = 0; y < copyHeight; y++) {
            for (int x = 0; x < copyWidth; x++) {
                newTiles[y * newWidth + x] = getTile(x, y);
            }
        }
        
        tiles = std::move(newTiles);
        width = newWidth;
        height = newHeight;
    }
    
    void clear() {
        std::fill(tiles.begin(), tiles.end(), 0);
    }
    
    void fill(uint16_t tileId) {
        std::fill(tiles.begin(), tiles.end(), tileId);
    }
};

#endif /* TILECOMMON_H */