//
//  GraphicsLayer.mm
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>
#import <simd/simd.h>

@interface GraphicsLayer : NSObject

@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property (nonatomic, strong) id<MTLBuffer> vertexBuffer;
@property (nonatomic, strong) id<MTLTexture> canvasTexture;
@property (nonatomic, assign) CGSize canvasSize;

// Core Graphics context
@property (nonatomic, assign) CGContextRef cgContext;
@property (nonatomic, assign) void* pixelData;

// Drawing state
@property (nonatomic, assign) simd_float4 currentColor;
@property (nonatomic, assign) float lineWidth;

- (instancetype)initWithDevice:(id<MTLDevice>)device canvasSize:(CGSize)size;
- (void)clear;
- (void)setColor:(simd_float4)color;
- (void)setLineWidth:(float)width;

// Drawing functions
- (void)drawLineFromX:(float)x1 y:(float)y1 toX:(float)x2 y:(float)y2;
- (void)drawRectX:(float)x y:(float)y width:(float)w height:(float)h;
- (void)fillRectX:(float)x y:(float)y width:(float)w height:(float)h;
- (void)drawCircleX:(float)x y:(float)y radius:(float)radius;
- (void)fillCircleX:(float)x y:(float)y radius:(float)radius;

// Render to Metal
- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport;

@end

@implementation GraphicsLayer

- (instancetype)initWithDevice:(id<MTLDevice>)device canvasSize:(CGSize)size {
    self = [super init];
    if (self) {
        NSLog(@"GraphicsLayer: Initializing with device %p, size %.0fx%.0f", device, size.width, size.height);
        self.device = device;
        self.canvasSize = size;
        self.currentColor = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f); // White
        self.lineWidth = 1.0f;

        // Absolutely minimal - no resource creation
        NSLog(@"GraphicsLayer: Minimal initialization - no resources created");
    }
    return self;
}

- (void)dealloc {
    // No Core Graphics resources to clean up in minimal version
}

// Stub methods - no actual implementation
- (void)createMinimalTexture {
    NSLog(@"GraphicsLayer: createMinimalTexture - stubbed");
}

- (void)createRenderPipeline {
    NSLog(@"GraphicsLayer: createRenderPipeline - stubbed");
}

- (void)createVertexBuffer {
    NSLog(@"GraphicsLayer: createVertexBuffer - stubbed");
}

- (void)clear {
    // Clear the texture to transparent in minimal version
    if (self.canvasTexture) {
        int width = (int)self.canvasSize.width;
        int height = (int)self.canvasSize.height;
        int bytesPerPixel = 4;
        int bytesPerRow = width * bytesPerPixel;
        size_t dataSize = height * bytesPerRow;
        void* pixelData = calloc(1, dataSize);

        if (pixelData) {
            MTLRegion region = MTLRegionMake2D(0, 0, width, height);
            [self.canvasTexture replaceRegion:region
                                  mipmapLevel:0
                                    withBytes:pixelData
                                  bytesPerRow:bytesPerRow];
            free(pixelData);
        }
    }
}

- (void)setColor:(simd_float4)color {
    self.currentColor = color;
    // No Core Graphics context in minimal version
}

- (void)setLineWidth:(float)width {
    self.lineWidth = width;
    // No Core Graphics context in minimal version
}

- (void)drawLineFromX:(float)x1 y:(float)y1 toX:(float)x2 y:(float)y2 {
    // Drawing not implemented in minimal version
    NSLog(@"GraphicsLayer: drawLine called (not implemented in minimal version)");
}

- (void)drawRectX:(float)x y:(float)y width:(float)w height:(float)h {
    // Drawing not implemented in minimal version
    NSLog(@"GraphicsLayer: drawRect called (not implemented in minimal version)");
}

- (void)fillRectX:(float)x y:(float)y width:(float)w height:(float)h {
    // Drawing not implemented in minimal version
    NSLog(@"GraphicsLayer: fillRect called (not implemented in minimal version)");
}

- (void)drawCircleX:(float)x y:(float)y radius:(float)radius {
    // Drawing not implemented in minimal version
    NSLog(@"GraphicsLayer: drawCircle called (not implemented in minimal version)");
}

- (void)fillCircleX:(float)x y:(float)y radius:(float)radius {
    // Drawing not implemented in minimal version
    NSLog(@"GraphicsLayer: fillCircle called (not implemented in minimal version)");
}

- (void)updateTexture {
    // No pixel data to update in minimal version
}

- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport {
    // Absolutely minimal - do nothing
    return;
}

@end

// C Interface - Global graphics layer instance
static GraphicsLayer* g_graphicsLayer = nil;

extern "C" {
    void graphics_layer_init(void* device, float width, float height) {
        @autoreleasepool {
            id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
            CGSize canvasSize = CGSizeMake(width, height);
            g_graphicsLayer = [[GraphicsLayer alloc] initWithDevice:metalDevice canvasSize:canvasSize];
            NSLog(@"Graphics layer initialized: %.0fx%.0f", width, height);
        }
    }

    void graphics_layer_clear() {
        @autoreleasepool {
            if (g_graphicsLayer) {
                [g_graphicsLayer clear];
            }
        }
    }

    void graphics_layer_set_color(float r, float g, float b, float a) {
        @autoreleasepool {
            if (g_graphicsLayer) {
                simd_float4 color = simd_make_float4(r, g, b, a);
                [g_graphicsLayer setColor:color];
            }
        }
    }

    void graphics_layer_set_line_width(float width) {
        @autoreleasepool {
            if (g_graphicsLayer) {
                [g_graphicsLayer setLineWidth:width];
            }
        }
    }

    void graphics_layer_draw_line(float x1, float y1, float x2, float y2) {
        @autoreleasepool {
            if (g_graphicsLayer) {
                [g_graphicsLayer drawLineFromX:x1 y:y1 toX:x2 y:y2];
            }
        }
    }

    void graphics_layer_draw_rect(float x, float y, float w, float h) {
        @autoreleasepool {
            if (g_graphicsLayer) {
                [g_graphicsLayer drawRectX:x y:y width:w height:h];
            }
        }
    }

    void graphics_layer_fill_rect(float x, float y, float w, float h) {
        @autoreleasepool {
            if (g_graphicsLayer) {
                [g_graphicsLayer fillRectX:x y:y width:w height:h];
            }
        }
    }

    void graphics_layer_draw_circle(float x, float y, float radius) {
        @autoreleasepool {
            if (g_graphicsLayer) {
                [g_graphicsLayer drawCircleX:x y:y radius:radius];
            }
        }
    }

    void graphics_layer_fill_circle(float x, float y, float radius) {
        @autoreleasepool {
            if (g_graphicsLayer) {
                [g_graphicsLayer fillCircleX:x y:y radius:radius];
            }
        }
    }

    void graphics_layer_render(void* encoder, float width, float height) {
        @autoreleasepool {
            if (g_graphicsLayer) {
                id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
                [g_graphicsLayer renderWithEncoder:metalEncoder viewport:CGSizeMake(width, height)];
            }
        }
    }

    void graphics_layer_cleanup() {
        @autoreleasepool {
            g_graphicsLayer = nil;
            NSLog(@"Graphics layer cleaned up");
        }
    }
}
