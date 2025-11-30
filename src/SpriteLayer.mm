//
//  SpriteLayer.mm
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <simd/simd.h>
#include "../SpriteEffectSystem.h"
#include "GlobalShutdown.h"

extern "C" {
    void sprite_effect_init(void* device, void* shaderLibrary);
    void sprite_effect_shutdown();
    const char* sprite_get_effect(uint16_t spriteId);
    void* sprite_effect_get_pipeline_state(uint16_t spriteId);
    void sprite_effect_bind_parameters(uint16_t spriteId, void* encoder);
    void sprite_clear_all_effects();
}

// Sprite command types for thread-safe operations
typedef enum {
    SPRITE_CMD_SHOW = 0,
    SPRITE_CMD_HIDE = 1,
    SPRITE_CMD_MOVE = 2,
    SPRITE_CMD_SCALE = 3,
    SPRITE_CMD_ROTATE = 4,
    SPRITE_CMD_ALPHA = 5
} SpriteCommandType;

// Sprite command structure
typedef struct {
    SpriteCommandType type;
    uint16_t spriteId;
    float x, y;           // For show/move commands
    float value;          // For scale/rotation/alpha commands
} SpriteCommand;

// Collision detection types
typedef enum {
    COLLISION_AABB = 0,    // Axis-Aligned Bounding Box (fastest)
    COLLISION_CIRCLE = 1,  // Circle-based collision
    COLLISION_PRECISE = 2  // Pixel-perfect collision (future)
} CollisionType;

// Collision result structure
typedef struct {
    bool colliding;
    float overlap_x;
    float overlap_y;
    float separation_distance;
} CollisionResult;



// Objective-C sprite data class
@interface SuperTerminalSprite : NSObject
@property (nonatomic, assign) uint16_t spriteId;
@property (nonatomic, strong) id<MTLTexture> texture;
@property (nonatomic, assign) float x, y;
@property (nonatomic, assign) float scale;
@property (nonatomic, assign) float rotation;
@property (nonatomic, assign) float alpha;
@property (nonatomic, assign) BOOL visible;
@property (nonatomic, assign) BOOL loaded;
@property (nonatomic, assign) int textureWidth;
@property (nonatomic, assign) int textureHeight;
@end

@implementation SuperTerminalSprite
- (instancetype)init {
    self = [super init];
    if (self) {
        self.spriteId = 0;
        self.texture = nil;
        self.x = 0.0f;
        self.y = 0.0f;
        self.scale = 1.0f;
        self.rotation = 0.0f;
        self.alpha = 1.0f;
        self.visible = NO;
        self.loaded = NO;
        self.textureWidth = 0;
        self.textureHeight = 0;
    }
    return self;
}
@end

// Vertex structure for sprite rendering
struct SpriteVertex {
    simd_float2 position;
    simd_float2 texCoord;
    simd_float4 color;
};

// Uniforms for sprite transformation
struct SpriteUniforms {
    simd_float4x4 modelMatrix;
    simd_float4x4 viewProjectionMatrix;
    simd_float4 color; // RGBA with alpha channel
};

@interface SpriteLayer : NSObject

@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property (nonatomic, strong) id<MTLBuffer> vertexBuffer;
@property (nonatomic, strong) id<MTLBuffer> uniformsBuffer;
@property (nonatomic, strong) id<MTLSamplerState> samplerState;

// Sprite storage using Objective-C collections
@property (nonatomic, strong) NSMutableDictionary<NSNumber*, SuperTerminalSprite*>* sprites;
@property (nonatomic, strong) NSMutableArray<NSNumber*>* renderOrder;

// Command queue for thread-safe sprite operations
@property (nonatomic, strong) NSMutableArray<NSData*>* commandQueue;
@property (nonatomic, strong) NSLock* queueLock;

// Sprite ID management
@property (nonatomic, strong) NSMutableSet<NSNumber*>* freeIds;
@property (nonatomic, assign) uint16_t nextId;

- (instancetype)initWithDevice:(id<MTLDevice>)device;
- (BOOL)loadSprite:(uint16_t)spriteId fromFile:(const char*)filename;
- (void)showSprite:(uint16_t)spriteId atX:(float)x y:(float)y;
- (void)hideSprite:(uint16_t)spriteId;
- (void)moveSprite:(uint16_t)spriteId toX:(float)x y:(float)y;
- (void)scaleSprite:(uint16_t)spriteId scale:(float)scale;
- (void)rotateSprite:(uint16_t)spriteId angle:(float)angle;
- (void)setSprite:(uint16_t)spriteId alpha:(float)alpha;
- (void)releaseSprite:(uint16_t)spriteId;
- (uint16_t)getNextAvailableId;
- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport;
- (void)dumpSpriteState;
- (CGSize)getSpriteSize:(uint16_t)spriteId;
- (float)getSpriteAspectRatio:(uint16_t)spriteId;

// Command queue methods
- (void)queueCommand:(SpriteCommand)command;
- (void)processCommandQueue;

// Clear and shutdown methods
- (void)clearAndReinitialize;
- (void)shutdownSpriteLayer;

@end

@implementation SpriteLayer

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    self = [super init];
    if (self) {
        NSLog(@"SpriteLayer: Initializing sprite system...");

        self.device = device;

        // Initialize sprite storage
        self.sprites = [[NSMutableDictionary alloc] init];
        self.renderOrder = [[NSMutableArray alloc] init];

        // Initialize command queue
        self.commandQueue = [[NSMutableArray alloc] init];
        self.queueLock = [[NSLock alloc] init];

        // Initialize sprite ID management
        self.freeIds = [[NSMutableSet alloc] init];
        self.nextId = 1; // Start from ID 1

        [self createRenderPipeline];
        [self createVertexBuffer];
        [self createSampler];

        NSLog(@"SpriteLayer: Initialization complete - ready for 256 sprites");
    }
    return self;
}

- (void)dealloc {
    NSLog(@"SpriteLayer: Cleaning up sprite system...");
}

- (void)createRenderPipeline {
    NSError* error = nil;

    NSLog(@"SpriteLayer: Creating sprite render pipeline...");

    // Metal shader for sprite rendering with transformation support
    NSString* shaderSource = @""
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct VertexIn {\n"
    "    float2 position [[attribute(0)]];\n"
    "    float2 texCoord [[attribute(1)]];\n"
    "};\n"
    "\n"
    "struct VertexOut {\n"
    "    float4 position [[position]];\n"
    "    float2 texCoord;\n"
    "    float4 color;\n"
    "};\n"
    "\n"
    "struct SpriteUniforms {\n"
    "    float4x4 modelMatrix;\n"
    "    float4x4 viewProjectionMatrix;\n"
    "    float4 color;\n"
    "};\n"
    "\n"
    "vertex VertexOut sprite_vertex(VertexIn in [[stage_in]],\n"
    "                              constant SpriteUniforms& uniforms [[buffer(1)]]) {\n"
    "    VertexOut out;\n"
    "    float4 worldPos = uniforms.modelMatrix * float4(in.position, 0.0, 1.0);\n"
    "    out.position = uniforms.viewProjectionMatrix * worldPos;\n"
    "    out.texCoord = in.texCoord;\n"
    "    out.color = uniforms.color;\n"
    "    return out;\n"
    "}\n"
    "\n"
    "fragment float4 sprite_fragment(VertexOut in [[stage_in]],\n"
    "                               texture2d<float> spriteTexture [[texture(0)]],\n"
    "                               sampler texSampler [[sampler(0)]]) {\n"
    "    float4 texColor = spriteTexture.sample(texSampler, in.texCoord);\n"
    "    // Apply sprite color/alpha modulation\n"
    "    return texColor * in.color;\n"
    "}\n";

    id<MTLLibrary> library = [self.device newLibraryWithSource:shaderSource options:nil error:&error];
    if (error) {
        NSLog(@"SpriteLayer: ERROR - Failed to create shader library: %@", error.localizedDescription);
        return;
    }

    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"sprite_vertex"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"sprite_fragment"];

    // Create render pipeline descriptor
    MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.label = @"Sprite Pipeline";
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = fragmentFunction;
    pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    // Enable alpha blending for transparency
    pipelineDescriptor.colorAttachments[0].blendingEnabled = YES;
    pipelineDescriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pipelineDescriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pipelineDescriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDescriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDescriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipelineDescriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    // Vertex descriptor
    MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[0].offset = 0;
    vertexDescriptor.attributes[0].bufferIndex = 0;
    vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[1].offset = sizeof(simd_float2);
    vertexDescriptor.attributes[1].bufferIndex = 0;
    vertexDescriptor.attributes[2].format = MTLVertexFormatFloat4;
    vertexDescriptor.attributes[2].offset = sizeof(simd_float2) * 2;
    vertexDescriptor.attributes[2].bufferIndex = 0;
    vertexDescriptor.layouts[0].stride = sizeof(SpriteVertex);
    vertexDescriptor.layouts[0].stepRate = 1;
    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    pipelineDescriptor.vertexDescriptor = vertexDescriptor;

    self.pipelineState = [self.device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (error) {
        NSLog(@"SpriteLayer: ERROR - Failed to create render pipeline: %@", error.localizedDescription);
        return;
    }

    NSLog(@"SpriteLayer: Sprite pipeline created successfully");
}

- (void)createVertexBuffer {
    // Create a quad (two triangles) for sprite rendering
    // All sprites use the same quad geometry, transformed by uniforms
    SpriteVertex vertices[] = {
        // Triangle 1
        {{-64.0f, -64.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}, // Bottom left
        {{ 64.0f, -64.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}, // Bottom right
        {{-64.0f,  64.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}, // Top left

        // Triangle 2
        {{ 64.0f, -64.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}, // Bottom right
        {{ 64.0f,  64.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}, // Top right
        {{-64.0f,  64.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}  // Top left
    };

    self.vertexBuffer = [self.device newBufferWithBytes:vertices
                                                 length:sizeof(vertices)
                                                options:MTLResourceStorageModeShared];
    self.vertexBuffer.label = @"Sprite Vertices";

    // Create uniforms buffer for multiple sprites (256 max sprites)
    NSUInteger uniformsBufferSize = sizeof(SpriteUniforms) * 256;
    self.uniformsBuffer = [self.device newBufferWithLength:uniformsBufferSize
                                                    options:MTLResourceStorageModeShared];
    self.uniformsBuffer.label = @"Sprite Uniforms";

    NSLog(@"SpriteLayer: Vertex buffers created");
}

- (void)createSampler {
    MTLSamplerDescriptor* samplerDescriptor = [[MTLSamplerDescriptor alloc] init];
    samplerDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.magFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.mipFilter = MTLSamplerMipFilterNotMipmapped;
    samplerDescriptor.maxAnisotropy = 1;
    samplerDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDescriptor.rAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDescriptor.normalizedCoordinates = YES;
    samplerDescriptor.lodMinClamp = 0;
    samplerDescriptor.lodMaxClamp = FLT_MAX;

    self.samplerState = [self.device newSamplerStateWithDescriptor:samplerDescriptor];

    NSLog(@"SpriteLayer: Texture sampler created");
}

- (id<MTLTexture>)loadTextureFromPNG:(const char*)filename {
    if (!filename) {
        NSLog(@"SpriteLayer: ERROR - No filename provided");
        return nil;
    }

    NSString* baseFilename = [NSString stringWithUTF8String:filename];
    NSImage* image = nil;

    // Try multiple paths like font loading does
    NSArray* searchPaths = @[
        baseFilename,                                                    // Original path
        [@"../" stringByAppendingString:baseFilename],                  // Parent directory
        [[@"../" stringByAppendingString:baseFilename] stringByExpandingTildeInPath], // Expanded parent
        [[NSFileManager defaultManager].currentDirectoryPath stringByAppendingPathComponent:baseFilename], // Current + relative
        [[NSFileManager defaultManager].currentDirectoryPath stringByAppendingPathComponent:[@"../" stringByAppendingString:baseFilename]] // Current + parent + relative
    ];

    for (NSString* tryPath in searchPaths) {
        NSLog(@"SpriteLayer: Trying sprite path: %@", tryPath);
        image = [[NSImage alloc] initWithContentsOfFile:tryPath];
        if (image) {
            NSLog(@"SpriteLayer: Successfully loaded sprite from: %@", tryPath);
            break;
        }
    }

    // Try bundle resource as last resort
    if (!image) {
        NSString* bundlePath = [[NSBundle mainBundle] pathForResource:baseFilename ofType:nil];
        if (bundlePath) {
            NSLog(@"SpriteLayer: Trying bundle path: %@", bundlePath);
            image = [[NSImage alloc] initWithContentsOfFile:bundlePath];
            if (image) {
                NSLog(@"SpriteLayer: Successfully loaded sprite from bundle: %@", bundlePath);
            }
        }
    }

    if (!image) {
        NSLog(@"SpriteLayer: ERROR - Could not load image: %s (tried %lu paths)", filename, (unsigned long)searchPaths.count + 1);
        return nil;
    }

    // Get bitmap representation
    NSBitmapImageRep* bitmap = nil;
    for (NSImageRep* rep in image.representations) {
        if ([rep isKindOfClass:[NSBitmapImageRep class]]) {
            bitmap = (NSBitmapImageRep*)rep;
            break;
        }
    }

    if (!bitmap) {
        // Create bitmap from image
        CGSize imageSize = image.size;
        bitmap = [[NSBitmapImageRep alloc]
                  initWithBitmapDataPlanes:NULL
                  pixelsWide:(NSInteger)imageSize.width
                  pixelsHigh:(NSInteger)imageSize.height
                  bitsPerSample:8
                  samplesPerPixel:4
                  hasAlpha:YES
                  isPlanar:NO
                  colorSpaceName:NSCalibratedRGBColorSpace
                  bytesPerRow:0
                  bitsPerPixel:0];

        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext* context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
        [NSGraphicsContext setCurrentContext:context];
        [image drawInRect:NSMakeRect(0, 0, imageSize.width, imageSize.height)];
        [NSGraphicsContext restoreGraphicsState];
    }

    // Log actual sprite dimensions for debugging
    NSLog(@"SpriteLayer: Loaded sprite %s (%ldx%ld)",
          filename, bitmap.pixelsWide, bitmap.pixelsHigh);

    // Create Metal texture
    MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                 width:bitmap.pixelsWide
                                                                                                height:bitmap.pixelsHigh
                                                                                             mipmapped:NO];
    textureDescriptor.usage = MTLTextureUsageShaderRead;

    id<MTLTexture> texture = [self.device newTextureWithDescriptor:textureDescriptor];
    if (!texture) {
        NSLog(@"SpriteLayer: ERROR - Failed to create Metal texture");
        return nil;
    }

    // Copy bitmap data to texture
    MTLRegion region = MTLRegionMake2D(0, 0, bitmap.pixelsWide, bitmap.pixelsHigh);
    [texture replaceRegion:region
               mipmapLevel:0
                 withBytes:bitmap.bitmapData
               bytesPerRow:bitmap.bytesPerRow];

    NSLog(@"SpriteLayer: Loaded texture %s (%ldx%ld)", filename, bitmap.pixelsWide, bitmap.pixelsHigh);
    return texture;
}

// Create texture from raw RGBA pixel data
- (id<MTLTexture>)createTextureFromPixels:(const uint8_t*)pixels width:(int)width height:(int)height {
    if (!pixels || width <= 0 || height <= 0) {
        NSLog(@"SpriteLayer: ERROR - Invalid pixel data or dimensions");
        return nil;
    }

    // Create Metal texture descriptor
    MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                 width:width
                                                                                                height:height
                                                                                             mipmapped:NO];
    textureDescriptor.usage = MTLTextureUsageShaderRead;

    id<MTLTexture> texture = [self.device newTextureWithDescriptor:textureDescriptor];
    if (!texture) {
        NSLog(@"SpriteLayer: ERROR - Failed to create Metal texture from pixels");
        return nil;
    }

    // Copy pixel data to texture
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    NSUInteger bytesPerRow = width * 4; // RGBA = 4 bytes per pixel
    [texture replaceRegion:region
               mipmapLevel:0
                 withBytes:pixels
               bytesPerRow:bytesPerRow];

    NSLog(@"SpriteLayer: Created texture from pixels (%dx%d)", width, height);
    return texture;
}

- (BOOL)loadSprite:(uint16_t)spriteId fromFile:(const char*)filename {
    if (spriteId > 1024) {
        NSLog(@"SpriteLayer: ERROR - Sprite ID %d exceeds maximum (1024)", spriteId);
        return NO;
    }

    id<MTLTexture> texture = [self loadTextureFromPNG:filename];
    if (!texture) {
        return NO;
    }

    // Create or update sprite
    NSNumber* key = @(spriteId);
    SuperTerminalSprite* sprite = self.sprites[key];
    if (!sprite) {
        sprite = [[SuperTerminalSprite alloc] init];
        self.sprites[key] = sprite;
    }

    sprite.spriteId = spriteId;
    sprite.texture = texture;
    sprite.loaded = YES;
    sprite.visible = NO; // Not visible until explicitly shown
    sprite.x = 0.0f;
    sprite.y = 0.0f;
    sprite.scale = 1.0f;
    sprite.rotation = 0.0f;
    sprite.alpha = 1.0f;
    sprite.textureWidth = texture.width;
    sprite.textureHeight = texture.height;

    NSLog(@"SpriteLayer: Sprite %d loaded successfully from %s (%dx%d)", spriteId, filename, (int)texture.width, (int)texture.height);
    return YES;
}

- (void)showSprite:(uint16_t)spriteId atX:(float)x y:(float)y {
    SpriteCommand command;
    command.type = SPRITE_CMD_SHOW;
    command.spriteId = spriteId;
    command.x = x;
    command.y = y;
    [self queueCommand:command];
}

- (void)hideSprite:(uint16_t)spriteId {
    SpriteCommand command;
    command.type = SPRITE_CMD_HIDE;
    command.spriteId = spriteId;
    [self queueCommand:command];
}

- (void)moveSprite:(uint16_t)spriteId toX:(float)x y:(float)y {
    SpriteCommand command;
    command.type = SPRITE_CMD_MOVE;
    command.spriteId = spriteId;
    command.x = x;
    command.y = y;
    [self queueCommand:command];
}

- (void)scaleSprite:(uint16_t)spriteId scale:(float)scale {
    SpriteCommand command;
    command.type = SPRITE_CMD_SCALE;
    command.spriteId = spriteId;
    command.value = scale;
    [self queueCommand:command];
}

- (void)rotateSprite:(uint16_t)spriteId angle:(float)angle {
    SpriteCommand command;
    command.type = SPRITE_CMD_ROTATE;
    command.spriteId = spriteId;
    command.value = angle;
    [self queueCommand:command];
}

- (void)setSprite:(uint16_t)spriteId alpha:(float)alpha {
    SpriteCommand command;
    command.type = SPRITE_CMD_ALPHA;
    command.spriteId = spriteId;
    command.value = alpha;
    [self queueCommand:command];
}

- (simd_float4x4)createTransformMatrix:(SuperTerminalSprite*)sprite viewportSize:(CGSize)viewport {
    // Create transformation matrix: Translation * Rotation * Scale

    // Calculate texture-based scale factors for 1:1 pixel mapping
    // The base quad is 128x128 (-64 to +64), so we need to scale by actual_size/128
    float textureScaleX = (float)sprite.textureWidth / 128.0f;
    float textureScaleY = (float)sprite.textureHeight / 128.0f;

    // Combine user scale with texture-based scale for correct pixel mapping
    float finalScaleX = sprite.scale * textureScaleX;
    float finalScaleY = sprite.scale * textureScaleY;

    // Scale matrix with texture-corrected scaling
    simd_float4x4 scaleMatrix = simd_matrix(
        simd_make_float4(finalScaleX, 0, 0, 0),
        simd_make_float4(0, finalScaleY, 0, 0),
        simd_make_float4(0, 0, 1, 0),
        simd_make_float4(0, 0, 0, 1)
    );

    // Rotation matrix (around Z axis)
    float c = cosf(sprite.rotation);
    float s = sinf(sprite.rotation);
    simd_float4x4 rotationMatrix = simd_matrix(
        simd_make_float4(c, s, 0, 0),
        simd_make_float4(-s, c, 0, 0),
        simd_make_float4(0, 0, 1, 0),
        simd_make_float4(0, 0, 0, 1)
    );

    // Translation matrix (use raw sprite coordinates)
    simd_float4x4 translationMatrix = simd_matrix(
        simd_make_float4(1, 0, 0, 0),
        simd_make_float4(0, 1, 0, 0),
        simd_make_float4(0, 0, 1, 0),
        simd_make_float4(sprite.x, sprite.y, 0, 1)
    );

    return simd_mul(translationMatrix, simd_mul(rotationMatrix, scaleMatrix));
}



- (void)releaseSprite:(uint16_t)spriteId {
    NSNumber* key = @(spriteId);
    SuperTerminalSprite* sprite = self.sprites[key];

    if (sprite) {
        // Remove from sprites dictionary
        [self.sprites removeObjectForKey:key];

        // Remove from render order
        [self.renderOrder removeObject:key];

        // Add ID back to free pool
        [self.freeIds addObject:key];

        NSLog(@"SpriteLayer: Released sprite ID %d (added to free pool)", spriteId);
    } else {
        NSLog(@"SpriteLayer: WARNING - Attempted to release non-existent sprite ID %d", spriteId);
    }
}

- (uint16_t)getNextAvailableId {
    // First, try to reuse a free ID
    if (self.freeIds.count > 0) {
        NSNumber* freeId = [self.freeIds anyObject];
        [self.freeIds removeObject:freeId];
        uint16_t reusedId = [freeId unsignedShortValue];
        NSLog(@"SpriteLayer: Reusing sprite ID %d from free pool", reusedId);
        return reusedId;
    }

    // No free IDs available, allocate a new one
    if (self.nextId > 1024) {
        NSLog(@"SpriteLayer: ERROR - Exceeded maximum sprite ID (1024)");
        return 0; // Return 0 to indicate failure
    }

    uint16_t newId = self.nextId++;
    NSLog(@"SpriteLayer: Allocated new sprite ID %d", newId);
    return newId;
}

- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport {
    // Check for emergency shutdown before processing
    if (is_emergency_shutdown_requested()) {
        NSLog(@"SpriteLayer: Emergency shutdown detected in render, terminating...");
        return;
    }

    // Process command queue before rendering
    [self processCommandQueue];

    if (self.renderOrder.count == 0) {
        return; // No sprites to render
    }

    // Create view-projection matrix for standard 1024x768 screen coordinates
    // Map coordinates 0-1024 (width) and 0-768 (height) to NDC -1 to +1
    simd_float4x4 viewProjectionMatrix = simd_matrix(
        simd_make_float4(2.0f/1024.0f, 0, 0, 0), // Use fixed 1024 width for screen coordinates
        simd_make_float4(0, -2.0f/768.0f, 0, 0), // Use fixed 768 height for screen coordinates
        simd_make_float4(0, 0, 1, 0),
        simd_make_float4(-1, 1, 0, 1) // Center at (-1, 1) for top-left origin
    );

    // Set up common rendering state
    [encoder setVertexBuffer:self.vertexBuffer offset:0 atIndex:0];
    [encoder setFragmentSamplerState:self.samplerState atIndex:0];

    id<MTLRenderPipelineState> currentPipelineState = nil;

    // Render sprites in order (lower IDs first, higher IDs on top)
    NSUInteger spriteIndex = 0;
    for (NSNumber* spriteKey in self.renderOrder) {
        SuperTerminalSprite* sprite = self.sprites[spriteKey];

        if (!sprite || !sprite.visible || !sprite.loaded) {
            continue;
        }

        // Get pipeline state for this sprite (effect or basic)
        uint16_t spriteId = [spriteKey unsignedShortValue];
        void* effectPipeline = sprite_effect_get_pipeline_state(spriteId);
        id<MTLRenderPipelineState> pipelineState;

        if (effectPipeline) {
            pipelineState = (__bridge id<MTLRenderPipelineState>)effectPipeline;
        } else {
            pipelineState = self.pipelineState;  // Use basic pipeline
        }

        // Only change pipeline state if it's different from current
        if (pipelineState != currentPipelineState) {
            [encoder setRenderPipelineState:pipelineState];
            currentPipelineState = pipelineState;
        }

        // Bind effect parameters if sprite has an effect
        if (effectPipeline) {
            sprite_effect_bind_parameters(spriteId, (__bridge void*)encoder);
        }

        // Calculate buffer offset for this sprite
        NSUInteger bufferOffset = spriteIndex * sizeof(SpriteUniforms);

        // Set up uniforms for this sprite at unique buffer offset
        SpriteUniforms* uniforms = (SpriteUniforms*)((uint8_t*)[self.uniformsBuffer contents] + bufferOffset);
        uniforms->modelMatrix = [self createTransformMatrix:sprite viewportSize:viewport];
        uniforms->viewProjectionMatrix = viewProjectionMatrix;
        uniforms->color = simd_make_float4(1.0f, 1.0f, 1.0f, sprite.alpha);

        // Upload uniforms buffer with unique offset for this sprite
        [encoder setVertexBuffer:self.uniformsBuffer offset:bufferOffset atIndex:1];
        [encoder setFragmentTexture:sprite.texture atIndex:0];

        // Draw the sprite quad (6 vertices = 2 triangles)
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];

        spriteIndex++;
    }
}

- (void)dumpSpriteState {
    NSLog(@"=== SPRITE STATE DUMP ===");
    NSLog(@"Total sprites loaded: %lu", (unsigned long)self.sprites.count);
    NSLog(@"Sprites in render order: %lu", (unsigned long)self.renderOrder.count);

    for (NSNumber* key in self.sprites) {
        SuperTerminalSprite* sprite = self.sprites[key];
        NSLog(@"Sprite %@: loaded=%@, visible=%@, pos=(%.1f,%.1f), scale=%.2f, alpha=%.2f",
              key, sprite.loaded ? @"YES" : @"NO", sprite.visible ? @"YES" : @"NO",
              sprite.x, sprite.y, sprite.scale, sprite.alpha);
    }

    NSLog(@"Render order: %@", self.renderOrder);
    NSLog(@"=== END SPRITE DUMP ===");
}

// Load sprite from pixel data instead of file
- (BOOL)loadSpriteFromPixels:(uint16_t)spriteId pixels:(const uint8_t*)pixels width:(int)width height:(int)height {
    if (!self.sprites || !pixels) {
        NSLog(@"SpriteLayer: ERROR - Sprites not initialized or invalid pixel data");
        return NO;
    }

    NSNumber* key = @(spriteId);
    SuperTerminalSprite* sprite = self.sprites[key];
    if (!sprite) {
        sprite = [[SuperTerminalSprite alloc] init];
        sprite.spriteId = spriteId;
        self.sprites[key] = sprite;
    }

    id<MTLTexture> texture = [self createTextureFromPixels:pixels width:width height:height];
    if (!texture) {
        return NO;
    }

    sprite.texture = texture;
    sprite.loaded = YES;
    sprite.visible = NO; // Start hidden by default
    sprite.textureWidth = width;
    sprite.textureHeight = height;

    NSLog(@"SpriteLayer: Loaded sprite %d from pixel data (%dx%d)", spriteId, width, height);
    return YES;
}

- (CGSize)getSpriteSize:(uint16_t)spriteId {
    if (spriteId == 0 || spriteId >= 256 || !self.sprites[@(spriteId)]) {
        return CGSizeMake(0, 0);
    }

    SuperTerminalSprite* sprite = self.sprites[@(spriteId)];
    if (!sprite.loaded) {
        return CGSizeMake(0, 0);
    }

    return CGSizeMake(sprite.textureWidth, sprite.textureHeight);
}

- (float)getSpriteAspectRatio:(uint16_t)spriteId {
    if (spriteId == 0 || spriteId >= 256 || !self.sprites[@(spriteId)]) {
        return 1.0f;
    }

    SuperTerminalSprite* sprite = self.sprites[@(spriteId)];
    if (!sprite.loaded || sprite.textureHeight == 0) {
        return 1.0f;
    }

    return (float)sprite.textureWidth / (float)sprite.textureHeight;
}

// Command queue implementation
- (void)queueCommand:(SpriteCommand)command {
    NSData* commandData = [NSData dataWithBytes:&command length:sizeof(SpriteCommand)];
    [self.queueLock lock];
    [self.commandQueue addObject:commandData];
    [self.queueLock unlock];
}

- (void)processCommandQueue {
    [self.queueLock lock];
    NSArray* commands = [self.commandQueue copy];
    [self.commandQueue removeAllObjects];
    [self.queueLock unlock];

    for (NSData* commandData in commands) {
        SpriteCommand command;
        [commandData getBytes:&command length:sizeof(SpriteCommand)];

        NSNumber* key = @(command.spriteId);
        SuperTerminalSprite* sprite = self.sprites[key];

        if (!sprite) continue;

        switch (command.type) {
            case SPRITE_CMD_HIDE:
                sprite.visible = NO;
                [self.renderOrder removeObject:key];
                break;

            case SPRITE_CMD_SHOW:
                if (sprite.loaded) {
                    sprite.x = command.x;
                    sprite.y = command.y;
                    sprite.visible = YES;
                    if (![self.renderOrder containsObject:key]) {
                        [self.renderOrder addObject:key];
                    }
                }
                break;

            case SPRITE_CMD_MOVE:
                sprite.x = command.x;
                sprite.y = command.y;
                break;

            case SPRITE_CMD_SCALE:
                sprite.scale = command.value;
                break;

            case SPRITE_CMD_ROTATE:
                sprite.rotation = command.value;
                break;

            case SPRITE_CMD_ALPHA:
                sprite.alpha = command.value;
                break;
        }
    }
}

// Clear and shutdown methods
- (void)clearAndReinitialize {
    NSLog(@"SpriteLayer: Clearing sprites and reinitializing for fresh start...");

    // Clear all sprites and their textures
    [self.sprites removeAllObjects];
    [self.renderOrder removeAllObjects];
    NSLog(@"SpriteLayer: All sprites cleared");

    // Reset sprite ID management
    [self.freeIds removeAllObjects];
    self.nextId = 1;
    NSLog(@"SpriteLayer: Sprite ID management reset");

    // Clear command queue
    [self.queueLock lock];
    [self.commandQueue removeAllObjects];
    [self.queueLock unlock];
    NSLog(@"SpriteLayer: Command queue cleared");

    // Clear all sprite effects
    sprite_clear_all_effects();
    NSLog(@"SpriteLayer: All sprite effects cleared");

    NSLog(@"SpriteLayer: Ready for new sprite data");
}

- (void)shutdownSpriteLayer {
    NSLog(@"SpriteLayer: Shutting down sprite layer completely...");

    // First clear all sprite data
    [self clearAndReinitialize];

    // Release Metal resources
    self.vertexBuffer = nil;
    self.uniformsBuffer = nil;
    self.pipelineState = nil;
    self.samplerState = nil;
    NSLog(@"SpriteLayer: Metal resources released");

    // Release device reference
    self.device = nil;

    NSLog(@"SpriteLayer: Sprite layer shutdown complete - needs reinitialization");
}

@end

// Global sprite layer instance
static SpriteLayer* g_spriteLayer = nil;

// C interface for sprite operations
extern "C" {

// Forward declarations for collision detection
CollisionResult sprite_check_collision_detailed(uint16_t id1, uint16_t id2, CollisionType type);
bool sprite_check_collision(uint16_t id1, uint16_t id2);
bool sprite_check_point_collision(uint16_t id, float x, float y);

// Collision detection helper functions
static CollisionResult check_aabb_collision(SuperTerminalSprite* sprite1, SuperTerminalSprite* sprite2) {
    CollisionResult result = {false, 0.0f, 0.0f, 0.0f};

    if (!sprite1 || !sprite2 || !sprite1.visible || !sprite2.visible || !sprite1.loaded || !sprite2.loaded) {
        return result;
    }

    // Get sprite dimensions (accounting for scale)
    float w1 = sprite1.textureWidth * sprite1.scale * 0.5f;
    float h1 = sprite1.textureHeight * sprite1.scale * 0.5f;
    float w2 = sprite2.textureWidth * sprite2.scale * 0.5f;
    float h2 = sprite2.textureHeight * sprite2.scale * 0.5f;

    // Calculate sprite centers
    float x1 = sprite1.x;
    float y1 = sprite1.y;
    float x2 = sprite2.x;
    float y2 = sprite2.y;

    // Check for AABB overlap
    float dx = fabsf(x1 - x2);
    float dy = fabsf(y1 - y2);

    float overlap_x = (w1 + w2) - dx;
    float overlap_y = (h1 + h2) - dy;

    if (overlap_x > 0 && overlap_y > 0) {
        result.colliding = true;
        result.overlap_x = overlap_x;
        result.overlap_y = overlap_y;
        result.separation_distance = sqrtf(dx * dx + dy * dy);
    }

    return result;
}

static CollisionResult check_circle_collision(SuperTerminalSprite* sprite1, SuperTerminalSprite* sprite2) {
    CollisionResult result = {false, 0.0f, 0.0f, 0.0f};

    if (!sprite1 || !sprite2 || !sprite1.visible || !sprite2.visible || !sprite1.loaded || !sprite2.loaded) {
        return result;
    }

    // Calculate effective radius (average of width and height, scaled)
    float r1 = (sprite1.textureWidth + sprite1.textureHeight) * 0.25f * sprite1.scale;
    float r2 = (sprite2.textureWidth + sprite2.textureHeight) * 0.25f * sprite2.scale;

    // Calculate distance between centers
    float dx = sprite1.x - sprite2.x;
    float dy = sprite1.y - sprite2.y;
    float distance = sqrtf(dx * dx + dy * dy);

    float combined_radius = r1 + r2;

    if (distance < combined_radius) {
        result.colliding = true;
        result.overlap_x = dx;
        result.overlap_y = dy;
        result.separation_distance = distance;
    }

    return result;
}

CollisionResult sprite_check_collision_detailed(uint16_t id1, uint16_t id2, CollisionType type) {
    CollisionResult result = {false, 0.0f, 0.0f, 0.0f};

    if (id1 == id2) {
        return result; // Same sprite can't collide with itself
    }

    if (!g_spriteLayer) return result;

    NSNumber* key1 = @(id1);
    NSNumber* key2 = @(id2);
    SuperTerminalSprite* sprite1 = g_spriteLayer.sprites[key1];
    SuperTerminalSprite* sprite2 = g_spriteLayer.sprites[key2];

    if (!sprite1 || !sprite2) {
        return result;
    }

    switch (type) {
        case COLLISION_AABB:
            return check_aabb_collision(sprite1, sprite2);
        case COLLISION_CIRCLE:
            return check_circle_collision(sprite1, sprite2);
        case COLLISION_PRECISE:
            // TODO: Implement pixel-perfect collision detection
            // For now, fallback to AABB
            return check_aabb_collision(sprite1, sprite2);
        default:
            return result;
    }
}

bool sprite_check_collision(uint16_t id1, uint16_t id2) {
    return sprite_check_collision_detailed(id1, id2, COLLISION_AABB).colliding;
}

bool sprite_check_point_collision(uint16_t id, float x, float y) {
    if (!g_spriteLayer) return false;

    NSNumber* key = @(id);
    SuperTerminalSprite* sprite = g_spriteLayer.sprites[key];
    if (!sprite || !sprite.visible || !sprite.loaded) {
        return false;
    }

    // Get sprite bounds
    float w = sprite.textureWidth * sprite.scale * 0.5f;
    float h = sprite.textureHeight * sprite.scale * 0.5f;

    // Check if point is within sprite bounds
    float dx = fabsf(x - sprite.x);
    float dy = fabsf(y - sprite.y);

    return (dx <= w && dy <= h);
}

void sprite_layer_release(uint16_t id) {
    if (g_spriteLayer) {
        [g_spriteLayer releaseSprite:id];
    }
}

uint16_t sprite_layer_next_id() {
    if (g_spriteLayer) {
        return [g_spriteLayer getNextAvailableId];
    }
    return 0; // Return 0 to indicate failure
}

void sprite_layer_init(void* device) {
    NSLog(@"SpriteLayer: Initializing C interface...");

    if (g_spriteLayer) {
        NSLog(@"SpriteLayer: WARNING - Already initialized, cleaning up previous instance");
        g_spriteLayer = nil;
    }

    id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
    g_spriteLayer = [[SpriteLayer alloc] initWithDevice:metalDevice];

    // Register as active subsystem for shutdown coordination
    register_active_subsystem();

    // Initialize sprite effect system
    // Load the compiled Metal library that contains sprite effect shaders
    NSBundle* bundle = [NSBundle mainBundle];
    NSURL* libraryURL = [bundle URLForResource:@"Sprite" withExtension:@"metallib"];

    if (!libraryURL) {
        // Try relative path for command-line tools
        NSString* executablePath = [[NSBundle mainBundle] executablePath];
        NSString* executableDir = [executablePath stringByDeletingLastPathComponent];
        libraryURL = [NSURL fileURLWithPath:[executableDir stringByAppendingPathComponent:@"Sprite.metallib"]];
    }

    NSError* error = nil;
    id<MTLLibrary> spriteLibrary = nil;

    if (libraryURL && [[NSFileManager defaultManager] fileExistsAtPath:libraryURL.path]) {
        spriteLibrary = [metalDevice newLibraryWithURL:libraryURL error:&error];
        if (error) {
            NSLog(@"SpriteLayer: Error loading Sprite.metallib: %@", error.localizedDescription);
        }
    }

    // Fallback to default library if compiled library not found
    if (!spriteLibrary) {
        spriteLibrary = [metalDevice newDefaultLibrary];
    }

    if (spriteLibrary) {
        sprite_effect_init((__bridge void*)metalDevice, (__bridge void*)spriteLibrary);
        NSLog(@"SpriteLayer: Sprite effect system initialized with Metal library");
    } else {
        NSLog(@"SpriteLayer: WARNING - Could not load any Metal library for effects");
    }

    NSLog(@"SpriteLayer: C interface ready");
}



void sprite_layer_cleanup() {
    NSLog(@"SpriteLayer: Cleaning up C interface...");
    sprite_effect_shutdown();
    g_spriteLayer = nil;

    // Unregister from shutdown system
    unregister_active_subsystem();
}

void sprite_layer_render(void* encoder, float width, float height) {
    // Check for emergency shutdown first
    if (is_emergency_shutdown_requested()) {
        NSLog(@"SpriteLayer: Emergency shutdown detected, skipping render");
        sprite_layer_cleanup();
        return;
    }

    if (!g_spriteLayer) {
        return;
    }

    id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
    [g_spriteLayer renderWithEncoder:metalEncoder viewport:CGSizeMake(width, height)];
}

bool sprite_layer_load(uint16_t id, const char* filename) {
    if (!g_spriteLayer) {
        NSLog(@"SpriteLayer: ERROR - Not initialized");
        return false;
    }

    return [g_spriteLayer loadSprite:id fromFile:filename];
}

void sprite_layer_show(uint16_t id, float x, float y) {
    if (g_spriteLayer) {
        [g_spriteLayer showSprite:id atX:x y:y];
    }
}

void sprite_layer_hide(uint16_t id) {
    if (g_spriteLayer) {
        [g_spriteLayer hideSprite:id];
    }
}

void sprite_layer_move(uint16_t id, float x, float y) {
    if (g_spriteLayer) {
        [g_spriteLayer moveSprite:id toX:x y:y];
    }
}

void sprite_layer_scale(uint16_t id, float scale) {
    if (g_spriteLayer) {
        [g_spriteLayer scaleSprite:id scale:scale];
    }
}

void sprite_layer_rotate(uint16_t id, float angle) {
    if (g_spriteLayer) {
        [g_spriteLayer rotateSprite:id angle:angle];
    }
}

void sprite_layer_alpha(uint16_t id, float alpha) {
    if (g_spriteLayer) {
        [g_spriteLayer setSprite:id alpha:alpha];
    }
}

void sprite_layer_dump_state() {
    if (g_spriteLayer) {
        [g_spriteLayer dumpSpriteState];
    }
}

bool sprite_layer_load_from_pixels(uint16_t id, const uint8_t* pixels, int width, int height) {
    if (!g_spriteLayer) {
        NSLog(@"SpriteLayer: ERROR - Not initialized");
        return false;
    }
    return [g_spriteLayer loadSpriteFromPixels:id pixels:pixels width:width height:height];
}

void sprite_layer_get_size(uint16_t id, int* width, int* height) {
    if (!g_spriteLayer || !width || !height) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }

    CGSize size = [g_spriteLayer getSpriteSize:id];
    *width = (int)size.width;
    *height = (int)size.height;
}

float sprite_layer_get_aspect_ratio(uint16_t id) {
    if (!g_spriteLayer) {
        return 1.0f;
    }
    return [g_spriteLayer getSpriteAspectRatio:id];
}

SuperTerminalSprite* sprite_layer_get_sprite(uint16_t spriteId) {
    if (!g_spriteLayer) return nullptr;

    NSNumber* key = @(spriteId);
    return g_spriteLayer.sprites[key];
}

void sprite_get_size(uint16_t id, int* width, int* height) {
    sprite_layer_get_size(id, width, height);
}

float sprite_get_aspect_ratio(uint16_t id) {
    return sprite_layer_get_aspect_ratio(id);
}

void sprites_clear() {
    NSLog(@"SpriteLayer: sprites_clear() - Performing comprehensive sprite cleanup");

    if (g_spriteLayer) {
        [g_spriteLayer clearAndReinitialize];
        NSLog(@"SpriteLayer: sprites_clear() complete - all sprite memory freed, ready for new sprites");
    } else {
        NSLog(@"SpriteLayer: sprites_clear() - No sprite layer initialized");
    }
}

void sprites_shutdown() {
    NSLog(@"SpriteLayer: sprites_shutdown() - Complete sprite system shutdown");

    if (g_spriteLayer) {
        [g_spriteLayer shutdownSpriteLayer];
        g_spriteLayer = nil;
        NSLog(@"SpriteLayer: sprites_shutdown() complete - all sprite system memory freed");
    } else {
        NSLog(@"SpriteLayer: sprites_shutdown() - No sprite layer to shutdown");
    }

    // Unregister from shutdown system
    unregister_active_subsystem();
}



} // extern "C"
