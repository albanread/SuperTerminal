//
//  TileLayer.mm
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <simd/simd.h>
#import <ImageIO/ImageIO.h>
#include "TileCommon.h"
#include <iostream>
#include <filesystem>
#include <memory>
#include <vector>

// Simplified forward declarations
extern "C" {
    bool tile_create_from_pixels_impl(uint16_t tileId, const uint8_t* pixels, int width, int height);
}

@interface TileLayer : NSObject

@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property (nonatomic, strong) id<MTLBuffer> vertexBuffer;
@property (nonatomic, strong) id<MTLBuffer> uniformBuffer;
@property (nonatomic, strong) id<MTLTexture> tileAtlas;
@property (nonatomic, strong) id<MTLSamplerState> samplerState;

@property (nonatomic, assign) struct Viewport viewport;
@property (nonatomic, assign) BOOL needsUpdate;

// C++ members (cannot be properties)
@end

@interface TileLayer() {
    std::unique_ptr<TileMap> _tileMap;
}

- (instancetype)initWithDevice:(id<MTLDevice>)device;
- (BOOL)loadTile:(uint16_t)tileId fromFile:(const char*)filename;
- (BOOL)loadTile:(uint16_t)tileId fromPixels:(const uint8_t*)pixels width:(int)width height:(int)height;
- (void)scroll:(float)dx dy:(float)dy;
- (void)setViewport:(int)x y:(int)y;
- (void)setTile:(int)mapX y:(int)mapY tileId:(uint16_t)tileId;
- (uint16_t)getTile:(int)mapX y:(int)mapY;
- (void)createTileMap:(int)width height:(int)height;
- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport;
- (simd_float2)worldToScreen:(float)worldX y:(float)worldY;
- (simd_float2)screenToWorld:(float)screenX y:(float)screenY;
- (void)resizeTileMap:(int)newWidth height:(int)newHeight;
- (void)clearTileMap;
- (void)fillTileMap:(uint16_t)tileId;
- (void)setTileRegion:(int)startX y:(int)startY width:(int)width height:(int)height tileId:(uint16_t)tileId;
- (void)centerViewport:(int)tileX y:(int)tileY;
- (BOOL)isValidPosition:(int)x y:(int)y;

// Property getter for C interface
- (TileMap*)getTileMapPtr;

@end

@implementation TileLayer

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    self = [super init];
    if (self) {
        NSLog(@"TileLayer: Initializing tile system...");

        self.device = device;
        self.needsUpdate = YES;

        // Initialize viewport at origin
        _viewport.x = 0;
        _viewport.y = 0;
        _viewport.offsetX = 0.0f;
        _viewport.offsetY = 0.0f;

        // Create default tile map (1024x1024 for testing)
        _tileMap = std::make_unique<TileMap>(1024, 1024);

        [self createRenderPipeline];
        [self createVertexBuffer];
        [self createTileAtlas];
        [self createSampler];

        NSLog(@"TileLayer: Initialization complete");
        NSLog(@"  Viewport: %dx%d tiles", VIEWPORT_WIDTH, VIEWPORT_HEIGHT);
        NSLog(@"  Tile Size: %dx%d pixels", TILE_SIZE, TILE_SIZE);
        NSLog(@"  Atlas: %d tiles maximum", MAX_TILES);
    }
    return self;
}

- (void)dealloc {
    NSLog(@"TileLayer: Cleaning up tile system...");
    [self shutdownTileLayer];
    [super dealloc];
}

- (void)shutdownTileLayer {
    NSLog(@"TileLayer: Shutting down and releasing all resources...");

    // Clear tile map data
    if (_tileMap) {
        _tileMap->clear();
        _tileMap.reset(); // Release the unique_ptr
        NSLog(@"TileLayer: Tile map memory freed");
    }

    // Release Metal resources
    self.vertexBuffer = nil;
    self.uniformBuffer = nil;
    self.tileAtlas = nil;
    self.pipelineState = nil;
    self.samplerState = nil;
    self.device = nil;

    NSLog(@"TileLayer: All Metal resources released");
    self.needsUpdate = NO;
}

- (void)clearAndReinitialize {
    NSLog(@"TileLayer: Clearing tiles and reinitializing for fresh start...");

    // Clear tile map data but keep structure
    if (_tileMap) {
        _tileMap->clear();
        NSLog(@"TileLayer: Tile map data cleared");
    }

    // Clear atlas texture (set to transparent)
    if (self.tileAtlas) {
        // Create a command buffer to clear the atlas
        id<MTLCommandQueue> commandQueue = [self.device newCommandQueue];
        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];

        // Clear the entire atlas texture
        id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
        MTLRegion region = MTLRegionMake2D(0, 0, self.tileAtlas.width, self.tileAtlas.height);

        // Create temporary clear texture
        MTLTextureDescriptor* clearDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                               width:self.tileAtlas.width
                                                                                              height:self.tileAtlas.height
                                                                                           mipmapped:NO];
        clearDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        id<MTLTexture> clearTexture = [self.device newTextureWithDescriptor:clearDesc];

        // Copy clear texture to atlas (effectively clearing it)
        [blitEncoder copyFromTexture:clearTexture
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(self.tileAtlas.width, self.tileAtlas.height, 1)
                           toTexture:self.tileAtlas
                    destinationSlice:0
                    destinationLevel:0
                   destinationOrigin:MTLOriginMake(0, 0, 0)];

        [blitEncoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        NSLog(@"TileLayer: Atlas texture cleared");
    }

    // Reset viewport
    _viewport.x = 0;
    _viewport.y = 0;
    _viewport.offsetX = 0.0f;
    _viewport.offsetY = 0.0f;

    self.needsUpdate = YES;
    NSLog(@"TileLayer: Ready for new tile data");
}

- (void)createRenderPipeline {
    NSError* error = nil;

    NSLog(@"TileLayer: Creating tile render pipeline...");

    // Metal shader for tile rendering
    NSString* shaderSource = @""
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct TileVertexIn {\n"
    "    float2 position [[attribute(0)]];\n"
    "    float2 texCoord [[attribute(1)]];\n"
    "    uint16_t tileId [[attribute(2)]];\n"
    "    uint16_t flags [[attribute(3)]];\n"
    "};\n"
    "\n"
    "struct TileVertexOut {\n"
    "    float4 position [[position]];\n"
    "    float2 texCoord;\n"
    "    uint16_t tileId;\n"
    "};\n"
    "\n"
    "struct TileUniforms {\n"
    "    float2 viewportOffset;\n"
    "    int2 viewportPosition;\n"
    "    float2 tileSize;\n"
    "    int2 atlasSize;\n"
    "};\n"
    "\n"
    "vertex TileVertexOut tile_vertex(TileVertexIn in [[stage_in]],\n"
    "                                constant TileUniforms& uniforms [[buffer(1)]],\n"
    "                                constant float2& viewportSize [[buffer(2)]]) {\n"
    "    TileVertexOut out;\n"
    "    \n"
    "    // Convert world position to screen coordinates with sub-tile offset\n"
    "    float2 screenPos = in.position - uniforms.viewportOffset;\n"
    "    \n"
    "    // Use logical viewport size (divide drawable size by 2 for retina)\n"
    "    float2 logicalViewport = viewportSize / 2.0;\n"
    "    \n"
    "    // Convert to NDC (Normalized Device Coordinates)\n"
    "    out.position.x = (screenPos.x / logicalViewport.x) * 2.0 - 1.0;\n"
    "    out.position.y = 1.0 - (screenPos.y / logicalViewport.y) * 2.0;  // Flip Y\n"
    "    out.position.z = 0.0;\n"
    "    out.position.w = 1.0;\n"
    "    \n"
    "    out.texCoord = in.texCoord;\n"
    "    out.tileId = in.tileId;\n"
    "    \n"
    "    return out;\n"
    "}\n"
    "\n"
    "fragment float4 tile_fragment(TileVertexOut in [[stage_in]],\n"
    "                             texture2d<float> tileAtlas [[texture(0)]],\n"
    "                             sampler tileSampler [[sampler(0)]]) {\n"
    "    \n"
    "    float4 color = tileAtlas.sample(tileSampler, in.texCoord);\n"
    "    \n"
    "    // Discard fully transparent pixels\n"
    "    if (color.a < 0.01) {\n"
    "        discard_fragment();\n"
    "    }\n"
    "    \n"
    "    return color;\n"
    "}\n";

    id<MTLLibrary> library = [self.device newLibraryWithSource:shaderSource options:nil error:&error];
    if (error) {
        NSLog(@"TileLayer: ERROR - Failed to create shader library: %@", error.localizedDescription);
        return;
    }

    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"tile_vertex"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"tile_fragment"];

    // Create pipeline descriptor
    MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = fragmentFunction;
    pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    // Enable alpha blending for transparent tiles
    pipelineDescriptor.colorAttachments[0].blendingEnabled = YES;
    pipelineDescriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pipelineDescriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pipelineDescriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDescriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDescriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipelineDescriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    // Set up vertex attributes
    MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[0].offset = 0;
    vertexDescriptor.attributes[0].bufferIndex = 0;
    vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[1].offset = sizeof(simd_float2);
    vertexDescriptor.attributes[1].bufferIndex = 0;
    vertexDescriptor.attributes[2].format = MTLVertexFormatUShort;
    vertexDescriptor.attributes[2].offset = sizeof(simd_float2) * 2;
    vertexDescriptor.attributes[2].bufferIndex = 0;
    vertexDescriptor.attributes[3].format = MTLVertexFormatUShort;
    vertexDescriptor.attributes[3].offset = sizeof(simd_float2) * 2 + sizeof(uint16_t);
    vertexDescriptor.attributes[3].bufferIndex = 0;
    vertexDescriptor.layouts[0].stride = sizeof(TileVertex);
    vertexDescriptor.layouts[0].stepRate = 1;
    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    pipelineDescriptor.vertexDescriptor = vertexDescriptor;

    self.pipelineState = [self.device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (error) {
        NSLog(@"TileLayer: ERROR - Failed to create render pipeline: %@", error.localizedDescription);
        return;
    }

    NSLog(@"TileLayer: Tile pipeline created successfully");
}

- (void)createVertexBuffer {
    // Create vertex buffer for maximum viewport tiles (32x24 tiles * 6 vertices per tile)
    NSUInteger bufferSize = VIEWPORT_WIDTH * VIEWPORT_HEIGHT * 6 * sizeof(TileVertex);
    self.vertexBuffer = [self.device newBufferWithLength:bufferSize options:MTLResourceStorageModeShared];
    self.vertexBuffer.label = @"Tile Vertices";

    // Create uniforms buffer
    self.uniformBuffer = [self.device newBufferWithLength:sizeof(TileUniforms) + sizeof(simd_float2)
                                                   options:MTLResourceStorageModeShared];
    self.uniformBuffer.label = @"Tile Uniforms";

    NSLog(@"TileLayer: Vertex buffers created (max %d vertices)", VIEWPORT_WIDTH * VIEWPORT_HEIGHT * 6);
}

- (void)createTileAtlas {
    // Create 2048x2048 atlas texture (can hold 256 tiles in 16x16 grid)
    MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                 width:2048
                                                                                                height:2048
                                                                                              mipmapped:NO];
    textureDescriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    textureDescriptor.storageMode = MTLStorageModeShared;

    self.tileAtlas = [self.device newTextureWithDescriptor:textureDescriptor];
    self.tileAtlas.label = @"Tile Atlas";

    if (!self.tileAtlas) {
        NSLog(@"TileLayer: ERROR - Failed to create tile atlas texture");
        return;
    }

    NSLog(@"TileLayer: Tile atlas created (2048x2048, 256 tiles max)");
}

- (void)createSampler {
    MTLSamplerDescriptor* samplerDescriptor = [[MTLSamplerDescriptor alloc] init];
    samplerDescriptor.minFilter = MTLSamplerMinMagFilterNearest;
    samplerDescriptor.magFilter = MTLSamplerMinMagFilterNearest;
    samplerDescriptor.mipFilter = MTLSamplerMipFilterNotMipmapped;
    samplerDescriptor.maxAnisotropy = 1;
    samplerDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDescriptor.rAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDescriptor.normalizedCoordinates = YES;
    samplerDescriptor.lodMinClamp = 0;
    samplerDescriptor.lodMaxClamp = FLT_MAX;

    self.samplerState = [self.device newSamplerStateWithDescriptor:samplerDescriptor];

    NSLog(@"TileLayer: Sampler created with pixel-perfect filtering");
}

- (BOOL)loadTile:(uint16_t)tileId fromFile:(const char*)filename {
    @autoreleasepool {
        if (tileId == 0 || tileId > MAX_TILES) {
            NSLog(@"TileLayer: Invalid tile ID %d (must be 1-%d)", tileId, MAX_TILES);
            return NO;
        }

        NSString* path = [NSString stringWithUTF8String:filename];
        NSURL* url = [NSURL fileURLWithPath:path];

        CGImageSourceRef imageSource = CGImageSourceCreateWithURL((__bridge CFURLRef)url, NULL);
        if (!imageSource) {
            NSLog(@"TileLayer: Failed to load tile image: %s", filename);
            return NO;
        }

        CGImageRef image = CGImageSourceCreateImageAtIndex(imageSource, 0, NULL);
        CFRelease(imageSource);

        if (!image) {
            NSLog(@"TileLayer: Failed to create CGImage from: %s", filename);
            return NO;
        }

        // Verify tile is 128x128
        size_t width = CGImageGetWidth(image);
        size_t height = CGImageGetHeight(image);

        if (width != TILE_SIZE || height != TILE_SIZE) {
            NSLog(@"TileLayer: Tile %s is %zux%zu, expected %dx%d", filename, width, height, TILE_SIZE, TILE_SIZE);
            CGImageRelease(image);
            return NO;
        }

        // Calculate position in atlas (16x16 grid)
        int atlasX = (tileId - 1) % 16;
        int atlasY = (tileId - 1) / 16;

        // Create temporary context to extract RGBA data
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(NULL, TILE_SIZE, TILE_SIZE, 8, TILE_SIZE * 4,
                                                   colorSpace, kCGImageAlphaPremultipliedLast);

        if (!context) {
            NSLog(@"TileLayer: Failed to create bitmap context for tile %d", tileId);
            CGColorSpaceRelease(colorSpace);
            CGImageRelease(image);
            return NO;
        }

        CGContextDrawImage(context, CGRectMake(0, 0, TILE_SIZE, TILE_SIZE), image);
        void* data = CGBitmapContextGetData(context);

        // Copy tile data to atlas
        MTLRegion region = MTLRegionMake2D(atlasX * TILE_SIZE, atlasY * TILE_SIZE, TILE_SIZE, TILE_SIZE);
        [self.tileAtlas replaceRegion:region
                          mipmapLevel:0
                            withBytes:data
                          bytesPerRow:TILE_SIZE * 4];

        CGContextRelease(context);
        CGColorSpaceRelease(colorSpace);
        CGImageRelease(image);

        NSLog(@"TileLayer: Loaded tile %d from %s at atlas position (%d,%d)", tileId, filename, atlasX, atlasY);
        return YES;
    }
}

- (BOOL)loadTile:(uint16_t)tileId fromPixels:(const uint8_t*)pixels width:(int)width height:(int)height {
    @autoreleasepool {
        if (tileId == 0 || tileId > MAX_TILES) {
            NSLog(@"TileLayer: Invalid tile ID %d (must be 1-%d)", tileId, MAX_TILES);
            return NO;
        }

        if (!pixels) {
            NSLog(@"TileLayer: NULL pixels provided for tile %d", tileId);
            return NO;
        }

        // Verify tile is 128x128
        if (width != TILE_SIZE || height != TILE_SIZE) {
            NSLog(@"TileLayer: Tile %d is %dx%d, expected %dx%d", tileId, width, height, TILE_SIZE, TILE_SIZE);
            return NO;
        }

        // Calculate position in atlas (16x16 grid)
        int atlasX = (tileId - 1) % 16;
        int atlasY = (tileId - 1) / 16;

        // Copy tile data directly to atlas
        MTLRegion region = MTLRegionMake2D(atlasX * TILE_SIZE, atlasY * TILE_SIZE, TILE_SIZE, TILE_SIZE);
        [self.tileAtlas replaceRegion:region
                          mipmapLevel:0
                            withBytes:pixels
                          bytesPerRow:TILE_SIZE * 4];

        NSLog(@"TileLayer: Loaded tile %d from pixels at atlas position (%d,%d)", tileId, atlasX, atlasY);
        return YES;
    }
}

- (void)scroll:(float)dx dy:(float)dy {
    _viewport.offsetX += dx;
    _viewport.offsetY += dy;

    // Handle horizontal tile boundary crossing
    while (_viewport.offsetX >= TILE_SIZE) {
        _viewport.offsetX -= TILE_SIZE;
        _viewport.x++;
        self.needsUpdate = YES; // New tiles visible
    }
    while (_viewport.offsetX < 0) {
        _viewport.offsetX += TILE_SIZE;
        _viewport.x--;
        self.needsUpdate = YES; // New tiles visible
    }

    // Handle vertical tile boundary crossing
    while (_viewport.offsetY >= TILE_SIZE) {
        _viewport.offsetY -= TILE_SIZE;
        _viewport.y++;
        self.needsUpdate = YES; // New tiles visible
    }
    while (_viewport.offsetY < 0) {
        _viewport.offsetY += TILE_SIZE;
        _viewport.y--;
        self.needsUpdate = YES; // New tiles visible
    }

    // Clamp viewport to map bounds
    if (_tileMap) {
        _viewport.x = std::max(0, std::min(_viewport.x, _tileMap->width - VIEWPORT_WIDTH));
        _viewport.y = std::max(0, std::min(_viewport.y, _tileMap->height - VIEWPORT_HEIGHT));
    }

    // Always update for smooth sub-pixel scrolling
    self.needsUpdate = YES;
}

- (void)setViewport:(int)x y:(int)y {
    if (_tileMap) {
        _viewport.x = std::max(0, std::min(x, _tileMap->width - VIEWPORT_WIDTH));
        _viewport.y = std::max(0, std::min(y, _tileMap->height - VIEWPORT_HEIGHT));
        _viewport.offsetX = 0.0f;
        _viewport.offsetY = 0.0f;
        self.needsUpdate = YES;
    }
}

- (void)setTile:(int)mapX y:(int)mapY tileId:(uint16_t)tileId {
    if (_tileMap) {
        _tileMap->setTile(mapX, mapY, tileId);

        // Check if this tile is in current viewport
        if (mapX >= _viewport.x && mapX < _viewport.x + VIEWPORT_WIDTH &&
            mapY >= _viewport.y && mapY < _viewport.y + VIEWPORT_HEIGHT) {
            self.needsUpdate = YES;
        }
    }
}

- (uint16_t)getTile:(int)mapX y:(int)mapY {
    return _tileMap ? _tileMap->getTile(mapX, mapY) : 0;
}

- (void)createTileMap:(int)width height:(int)height {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        NSLog(@"TileLayer: Invalid tile map size %dx%d", width, height);
        return;
    }

    NSLog(@"TileLayer: Creating tile map %dx%d", width, height);

    _tileMap = std::make_unique<TileMap>(width, height);

    // Reset viewport to origin
    _viewport.x = 0;
    _viewport.y = 0;
    _viewport.offsetX = 0.0f;
    _viewport.offsetY = 0.0f;

    self.needsUpdate = YES;
}

- (void)resizeTileMap:(int)newWidth height:(int)newHeight {
    if (!_tileMap || newWidth <= 0 || newHeight <= 0 || newWidth > 4096 || newHeight > 4096) {
        NSLog(@"TileLayer: Invalid resize parameters %dx%d", newWidth, newHeight);
        return;
    }

    NSLog(@"TileLayer: Resizing tile map from %dx%d to %dx%d", _tileMap->width, _tileMap->height, newWidth, newHeight);

    _tileMap->resize(newWidth, newHeight);

    // Adjust viewport to stay within bounds
    _viewport.x = std::max(0, std::min(_viewport.x, _tileMap->width - VIEWPORT_WIDTH));
    _viewport.y = std::max(0, std::min(_viewport.y, _tileMap->height - VIEWPORT_HEIGHT));

    self.needsUpdate = YES;
}

- (void)clearTileMap {
    if (!_tileMap) {
        NSLog(@"TileLayer: No tile map to clear");
        return;
    }

    NSLog(@"TileLayer: Clearing tile map");
    _tileMap->clear();
    self.needsUpdate = YES;
}

- (void)fillTileMap:(uint16_t)tileId {
    if (!_tileMap) {
        NSLog(@"TileLayer: No tile map to fill");
        return;
    }

    NSLog(@"TileLayer: Filling tile map with tile %d", tileId);
    _tileMap->fill(tileId);
    self.needsUpdate = YES;
}

- (void)setTileRegion:(int)startX y:(int)startY width:(int)width height:(int)height tileId:(uint16_t)tileId {
    if (!_tileMap) {
        NSLog(@"TileLayer: No tile map for region operation");
        return;
    }

    // Clamp region to map bounds
    int endX = std::min(startX + width, _tileMap->width);
    int endY = std::min(startY + height, _tileMap->height);
    startX = std::max(0, startX);
    startY = std::max(0, startY);

    NSLog(@"TileLayer: Setting region (%d,%d) to (%d,%d) with tile %d", startX, startY, endX, endY, tileId);

    bool needsViewportUpdate = false;

    for (int y = startY; y < endY; y++) {
        for (int x = startX; x < endX; x++) {
            _tileMap->setTile(x, y, tileId);

            // Check if this tile is in current viewport
            if (x >= _viewport.x && x < _viewport.x + VIEWPORT_WIDTH &&
                y >= _viewport.y && y < _viewport.y + VIEWPORT_HEIGHT) {
                needsViewportUpdate = true;
            }
        }
    }

    if (needsViewportUpdate) {
        self.needsUpdate = YES;
    }
}

- (void)centerViewport:(int)tileX y:(int)tileY {
    if (!_tileMap) {
        NSLog(@"TileLayer: No tile map for centering viewport");
        return;
    }

    // Center viewport on specified tile
    int newX = tileX - VIEWPORT_WIDTH / 2;
    int newY = tileY - VIEWPORT_HEIGHT / 2;

    // Clamp to map bounds
    newX = std::max(0, std::min(newX, _tileMap->width - VIEWPORT_WIDTH));
    newY = std::max(0, std::min(newY, _tileMap->height - VIEWPORT_HEIGHT));

    _viewport.x = newX;
    _viewport.y = newY;
    _viewport.offsetX = 0.0f;
    _viewport.offsetY = 0.0f;

    self.needsUpdate = YES;

    NSLog(@"TileLayer: Centered viewport on tile (%d,%d), viewport now at (%d,%d)", tileX, tileY, newX, newY);
}

- (BOOL)isValidPosition:(int)x y:(int)y {
    if (!_tileMap) {
        return NO;
    }

    return (x >= 0 && x < _tileMap->width && y >= 0 && y < _tileMap->height);
}

- (void)updateViewport {
    if (!self.vertexBuffer || !_tileMap) {
        NSLog(@"TileLayer: Cannot update viewport - missing buffer=%p or tileMap=%p",
              self.vertexBuffer, _tileMap.get());
        return;
    }

    // Viewport update (debug logging disabled)

    TileVertex* vertices = (TileVertex*)[self.vertexBuffer contents];
    int vertexIndex = 0;
    int tilesRendered = 0;

    // Generate vertices for all tiles in viewport
    for (int tileY = 0; tileY < VIEWPORT_HEIGHT; tileY++) {
        for (int tileX = 0; tileX < VIEWPORT_WIDTH; tileX++) {
            // Calculate world position of this tile (relative to viewport)
            float worldX = tileX * TILE_SIZE;
            float worldY = tileY * TILE_SIZE;

            // Get tile ID from map
            int mapX = _viewport.x + tileX;
            int mapY = _viewport.y + tileY;
            uint16_t tileId = _tileMap->getTile(mapX, mapY);

            // Skip empty tiles
            if (tileId == 0) {
                continue;
            }

            tilesRendered++;

            // Calculate UV coordinates in atlas (16x16 grid)
            int atlasX = (tileId - 1) % 16;
            int atlasY = (tileId - 1) / 16;
            float uMin = atlasX / 16.0f;
            float vMin = atlasY / 16.0f;
            float uMax = (atlasX + 1) / 16.0f;
            float vMax = (atlasY + 1) / 16.0f;

            // Generate 6 vertices for 2 triangles (quad)
            // Triangle 1
            vertices[vertexIndex++] = {
                .position = {worldX, worldY},
                .texCoord = {uMin, vMin},
                .tileId = tileId,
                .flags = 0
            };
            vertices[vertexIndex++] = {
                .position = {worldX + TILE_SIZE, worldY},
                .texCoord = {uMax, vMin},
                .tileId = tileId,
                .flags = 0
            };
            vertices[vertexIndex++] = {
                .position = {worldX, worldY + TILE_SIZE},
                .texCoord = {uMin, vMax},
                .tileId = tileId,
                .flags = 0
            };

            // Triangle 2
            vertices[vertexIndex++] = {
                .position = {worldX + TILE_SIZE, worldY},
                .texCoord = {uMax, vMin},
                .tileId = tileId,
                .flags = 0
            };
            vertices[vertexIndex++] = {
                .position = {worldX + TILE_SIZE, worldY + TILE_SIZE},
                .texCoord = {uMax, vMax},
                .tileId = tileId,
                .flags = 0
            };
            vertices[vertexIndex++] = {
                .position = {worldX, worldY + TILE_SIZE},
                .texCoord = {uMin, vMax},
                .tileId = tileId,
                .flags = 0
            };
        }
    }

    self.needsUpdate = NO;
}

- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewportSize {
    if (!self.pipelineState || !self.vertexBuffer || !self.tileAtlas) {
        return;
    }

    // Components ready, viewport configured

    // Update vertex data if needed
    if (self.needsUpdate) {
        // Updating viewport vertices (debug logging disabled)
        [self updateViewport];
    }

    [encoder setRenderPipelineState:self.pipelineState];
    [encoder setVertexBuffer:self.vertexBuffer offset:0 atIndex:0];

    // Set up uniforms
    TileUniforms* uniforms = (TileUniforms*)[self.uniformBuffer contents];
    uniforms->viewportOffset = simd_make_float2(_viewport.offsetX, _viewport.offsetY);
    uniforms->viewportPosition = simd_make_int2(_viewport.x, _viewport.y);
    uniforms->tileSize = simd_make_float2(TILE_SIZE, TILE_SIZE);
    uniforms->atlasSize = simd_make_int2(16, 16);

    // Set viewport size
    simd_float2* viewportSizePtr = (simd_float2*)((uint8_t*)[self.uniformBuffer contents] + sizeof(TileUniforms));
    *viewportSizePtr = simd_make_float2(viewportSize.width, viewportSize.height);

    [encoder setVertexBuffer:self.uniformBuffer offset:0 atIndex:1];
    [encoder setVertexBuffer:self.uniformBuffer offset:sizeof(TileUniforms) atIndex:2];

    [encoder setFragmentTexture:self.tileAtlas atIndex:0];
    [encoder setFragmentSamplerState:self.samplerState atIndex:0];

    // Draw all visible tiles (calculated in updateViewport)
    int maxVertices = VIEWPORT_WIDTH * VIEWPORT_HEIGHT * 6;
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:maxVertices];
}

- (simd_float2)worldToScreen:(float)worldX y:(float)worldY {
    float screenX = worldX - _viewport.offsetX - (_viewport.x * TILE_SIZE);
    float screenY = worldY - _viewport.offsetY - (_viewport.y * TILE_SIZE);
    return simd_make_float2(screenX, screenY);
}

- (simd_float2)screenToWorld:(float)screenX y:(float)screenY {
    float worldX = screenX + _viewport.offsetX + (_viewport.x * TILE_SIZE);
    float worldY = screenY + _viewport.offsetY + (_viewport.y * TILE_SIZE);
    return simd_make_float2(worldX, worldY);
}

- (TileMap*)getTileMapPtr {
    return _tileMap.get();
}

@end

// MARK: - C Interface for Lua Integration

static TileLayer* g_tileLayer1 = nil;  // Background tile layer
static TileLayer* g_tileLayer2 = nil;  // Foreground tile layer

extern "C" {
    void tile_layers_init(void* device) {
        @autoreleasepool {
            NSLog(@"TileLayer C API: Initializing tile layers with device=%p", device);
            if (device) {
                id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;

                // Create both tile layers
                g_tileLayer1 = [[TileLayer alloc] initWithDevice:metalDevice];
                g_tileLayer2 = [[TileLayer alloc] initWithDevice:metalDevice];

                NSLog(@"TileLayer: Both tile layers initialized successfully: layer1=%p, layer2=%p",
                      g_tileLayer1, g_tileLayer2);
            } else {
                NSLog(@"TileLayer C API: ERROR - No device provided for initialization");
            }
        }
    }

    void tile_layers_cleanup() {
        @autoreleasepool {
            g_tileLayer1 = nil;
            g_tileLayer2 = nil;
            NSLog(@"TileLayer: Tile layers cleaned up");
        }
    }

    extern "C" {
        // Implementation moved here from forward declaration

        // C interface functions
        bool tile_load_impl(uint16_t tileId, const char* filename) {
            // Load tile into both layers' shared atlas (they share the same atlas)
            if (g_tileLayer1 && filename) {
                bool result1 = [g_tileLayer1 loadTile:tileId fromFile:filename];
                // Also load into second layer's atlas (or they could share the same atlas)
                if (g_tileLayer2) {
                    [g_tileLayer2 loadTile:tileId fromFile:filename];
                }
                return result1;
            }
            return false;
        }

        bool tile_create_from_pixels_impl(uint16_t tileId, const uint8_t* pixels, int width, int height) {
            // Load tile into both layers from pixels
            if (g_tileLayer1 && pixels) {
                bool result1 = [g_tileLayer1 loadTile:tileId fromPixels:pixels width:width height:height];
                // Also load into second layer's atlas
                if (g_tileLayer2) {
                    [g_tileLayer2 loadTile:tileId fromPixels:pixels width:width height:height];
                }
                return result1;
            }
            return false;
        }

        bool tile_begin_render_impl(uint16_t tileId) {
            NSLog(@"DEBUG: tile_begin_render_impl called - tileId=%d", tileId);

            // Standard tile size is 128x128
            extern bool minimal_graphics_layer_begin_tile_render(uint16_t id, int width, int height);
            return minimal_graphics_layer_begin_tile_render(tileId, 128, 128);
        }

        bool tile_end_render_impl(uint16_t tileId) {
            NSLog(@"DEBUG: tile_end_render_impl called - tileId=%d", tileId);

            extern bool minimal_graphics_layer_end_tile_render(uint16_t id);
            return minimal_graphics_layer_end_tile_render(tileId);
        }
    }

    void tile_scroll_impl(int layer, float dx, float dy) {
        @autoreleasepool {
            TileLayer* tileLayer = (layer == 1) ? g_tileLayer1 : g_tileLayer2;
            if (tileLayer) {
                [tileLayer scroll:dx dy:dy];
            }
        }
    }

    void tile_set_viewport_impl(int layer, int x, int y) {
        @autoreleasepool {
            TileLayer* tileLayer = (layer == 1) ? g_tileLayer1 : g_tileLayer2;
            if (tileLayer) {
                [tileLayer setViewport:x y:y];
            }
        }
    }

    void tile_set_impl(int layer, int mapX, int mapY, uint16_t tileId) {
        @autoreleasepool {
            TileLayer* tileLayer = (layer == 1) ? g_tileLayer1 : g_tileLayer2;
            if (tileLayer) {
                [tileLayer setTile:mapX y:mapY tileId:tileId];
                if ((mapX + mapY) % 100 == 0) { // Log every 100th tile to avoid spam
                    NSLog(@"TileLayer C API: Set tile at (%d,%d) to ID %d on layer %d", mapX, mapY, tileId, layer);
                }
            }
        }
    }

    uint16_t tile_get_impl(int layer, int mapX, int mapY) {
        @autoreleasepool {
            TileLayer* tileLayer = (layer == 1) ? g_tileLayer1 : g_tileLayer2;
            if (tileLayer) {
                return [tileLayer getTile:mapX y:mapY];
            }
            return 0;
        }
    }

    void tile_create_map_impl(int layer, int width, int height) {
        @autoreleasepool {
            NSLog(@"TileLayer C API: Creating map for layer %d, size %dx%d", layer, width, height);
            TileLayer* tileLayer = (layer == 1) ? g_tileLayer1 : g_tileLayer2;
            if (tileLayer) {
                [tileLayer createTileMap:width height:height];
                NSLog(@"TileLayer C API: Map created for layer %d", layer);
            } else {
                NSLog(@"TileLayer C API: ERROR - No tile layer %d found", layer);
            }
        }
    }

    void tile_layer_render(int layer, void* encoder, float width, float height) {
        @autoreleasepool {
            TileLayer* tileLayer = (layer == 1) ? g_tileLayer1 : g_tileLayer2;
            if (tileLayer && encoder) {
                id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
                [tileLayer renderWithEncoder:metalEncoder viewport:CGSizeMake(width, height)];
            } else {
                NSLog(@"TileLayer C API: Missing tileLayer=%p or encoder=%p", tileLayer, encoder);
            }
        }
    }

    void tile_world_to_screen(int layer, float worldX, float worldY, float* screenX, float* screenY) {
        @autoreleasepool {
            TileLayer* tileLayer = (layer == 1) ? g_tileLayer1 : g_tileLayer2;
            if (tileLayer && screenX && screenY) {
                simd_float2 screen = [tileLayer worldToScreen:worldX y:worldY];
                *screenX = screen.x;
                *screenY = screen.y;
            }
        }
    }

    void tile_screen_to_world(int layer, float screenX, float screenY, float* worldX, float* worldY) {
        @autoreleasepool {
            TileLayer* tileLayer = (layer == 1) ? g_tileLayer1 : g_tileLayer2;
            if (tileLayer && worldX && worldY) {
                simd_float2 world = [tileLayer screenToWorld:screenX y:screenY];
                *worldX = world.x;
                *worldY = world.y;
            }
        }
    }

    void tile_resize_map_impl(int layer, int newWidth, int newHeight) {
        @autoreleasepool {
            TileLayer* tileLayer = (layer == 1) ? g_tileLayer1 : g_tileLayer2;
            if (tileLayer) {
                [tileLayer resizeTileMap:newWidth height:newHeight];
            }
        }
    }

    void tile_clear_map_impl(int layer) {
        @autoreleasepool {
            TileLayer* tileLayer = (layer == 1) ? g_tileLayer1 : g_tileLayer2;
            if (tileLayer) {
                [tileLayer clearTileMap];
            }
        }
    }

    void tiles_clear_impl() {
        @autoreleasepool {
            NSLog(@"TileLayer: tiles_clear() - Performing comprehensive cleanup");

            // Clear and reinitialize both layers (preserves objects but clears data)
            if (g_tileLayer1) {
                [g_tileLayer1 clearAndReinitialize];
                NSLog(@"TileLayer: Layer 1 cleared and reinitialized");
            }

            if (g_tileLayer2) {
                [g_tileLayer2 clearAndReinitialize];
                NSLog(@"TileLayer: Layer 2 cleared and reinitialized");
            }

            NSLog(@"TileLayer: tiles_clear() complete - all tile memory freed, ready for new tiles");
        }
    }

    void tiles_shutdown_impl() {
        @autoreleasepool {
            NSLog(@"TileLayer: tiles_shutdown() - Complete system shutdown");

            // Full shutdown and deallocation
            if (g_tileLayer1) {
                [g_tileLayer1 shutdownTileLayer];
                g_tileLayer1 = nil;
                NSLog(@"TileLayer: Layer 1 shut down and deallocated");
            }

            if (g_tileLayer2) {
                [g_tileLayer2 shutdownTileLayer];
                g_tileLayer2 = nil;
                NSLog(@"TileLayer: Layer 2 shut down and deallocated");
            }

            NSLog(@"TileLayer: tiles_shutdown() complete - all tile system memory freed");
        }
    }

    void tile_fill_map_impl(int layer, uint16_t tileId) {
        @autoreleasepool {
            TileLayer* tileLayer = (layer == 1) ? g_tileLayer1 : g_tileLayer2;
            if (tileLayer) {
                [tileLayer fillTileMap:tileId];
            }
        }
    }

    void tile_set_region_impl(int layer, int startX, int startY, int width, int height, uint16_t tileId) {
        @autoreleasepool {
            TileLayer* tileLayer = (layer == 1) ? g_tileLayer1 : g_tileLayer2;
            if (tileLayer) {
                [tileLayer setTileRegion:startX y:startY width:width height:height tileId:tileId];
            }
        }
    }

    void tile_get_map_size_impl(int layer, int* width, int* height) {
        @autoreleasepool {
            TileLayer* tileLayer = (layer == 1) ? g_tileLayer1 : g_tileLayer2;
            if (tileLayer && width && height) {
                TileMap* tileMap = [tileLayer getTileMapPtr];
                if (tileMap) {
                    *width = tileMap->width;
                    *height = tileMap->height;
                } else {
                    *width = 0;
                    *height = 0;
                }
            }
        }
    }

    void tile_center_viewport_impl(int layer, int tileX, int tileY) {
        @autoreleasepool {
            TileLayer* tileLayer = (layer == 1) ? g_tileLayer1 : g_tileLayer2;
            if (tileLayer) {
                [tileLayer centerViewport:tileX y:tileY];
            }
        }
    }

    bool tile_is_valid_position_impl(int layer, int x, int y) {
        @autoreleasepool {
            TileLayer* tileLayer = (layer == 1) ? g_tileLayer1 : g_tileLayer2;
            if (tileLayer) {
                return [tileLayer isValidPosition:x y:y];
            }
            return false;
        }
    }
}
