//
//  SkiaGraphicsLayer.mm
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <simd/simd.h>

#ifdef USE_SKIA
#include "skia.h"
#endif

@interface SkiaGraphicsLayer : NSObject

@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property (nonatomic, strong) id<MTLBuffer> vertexBuffer;
@property (nonatomic, strong) id<MTLTexture> canvasTexture;
@property (nonatomic, assign) CGSize canvasSize;

#ifdef USE_SKIA
// Skia objects
@property (nonatomic, assign) sk_sp<SkSurface> surface;
@property (nonatomic, assign) SkCanvas* canvas;
@property (nonatomic, assign) SkPaint paint;
@property (nonatomic, assign) void* pixelData;
@property (nonatomic, assign) size_t rowBytes;
#endif

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
- (void)drawTextX:(float)x y:(float)y text:(NSString*)text fontSize:(float)fontSize;

// Render to Metal
- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport;

@end

@implementation SkiaGraphicsLayer

- (instancetype)initWithDevice:(id<MTLDevice>)device canvasSize:(CGSize)size {
    self = [super init];
    if (self) {
        NSLog(@"SkiaGraphicsLayer: Initializing with device %p, size %.0fx%.0f", device, size.width, size.height);

        self.device = device;
        self.canvasSize = size;
        self.currentColor = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f); // White
        self.lineWidth = 1.0f;

#ifdef USE_SKIA
        [self createSkiaSurface];
#endif
        [self createRenderPipeline];
        [self createVertexBuffer];

        NSLog(@"SkiaGraphicsLayer: Initialization complete");
    }
    return self;
}

- (void)dealloc {
#ifdef USE_SKIA
    if (self.pixelData) {
        free(self.pixelData);
        self.pixelData = nullptr;
    }
#endif
}

#ifdef USE_SKIA
- (void)createSkiaSurface {
    int width = (int)self.canvasSize.width;
    int height = (int)self.canvasSize.height;

    NSLog(@"SkiaGraphicsLayer: Creating Skia surface %dx%d", width, height);

    // Create pixel buffer for Skia
    self.rowBytes = width * 4; // RGBA
    size_t dataSize = height * self.rowBytes;
    self.pixelData = malloc(dataSize);

    if (!self.pixelData) {
        NSLog(@"SkiaGraphicsLayer: ERROR - Failed to allocate pixel data");
        return;
    }

    // Clear to transparent
    memset(self.pixelData, 0, dataSize);

    // Create Skia surface
    SkImageInfo imageInfo = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    self.surface = SkSurfaces::WrapPixels(imageInfo, self.pixelData, self.rowBytes);

    if (!self.surface) {
        NSLog(@"SkiaGraphicsLayer: ERROR - Failed to create Skia surface");
        free(self.pixelData);
        self.pixelData = nullptr;
        return;
    }

    self.canvas = self.surface->getCanvas();

    // Set up default paint
    self.paint.setAntiAlias(true);
    self.paint.setStyle(SkPaint::kStroke_Style);
    self.paint.setStrokeWidth(1.0f);
    self.paint.setColor(SK_ColorWHITE);

    // Create Metal texture
    [self createMetalTexture];

    NSLog(@"SkiaGraphicsLayer: Skia surface created successfully");
}

- (void)createMetalTexture {
    int width = (int)self.canvasSize.width;
    int height = (int)self.canvasSize.height;

    MTLTextureDescriptor* textureDescriptor = [[MTLTextureDescriptor alloc] init];
    textureDescriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
    textureDescriptor.width = width;
    textureDescriptor.height = height;
    textureDescriptor.usage = MTLTextureUsageShaderRead;

    self.canvasTexture = [self.device newTextureWithDescriptor:textureDescriptor];
    if (!self.canvasTexture) {
        NSLog(@"SkiaGraphicsLayer: ERROR - Failed to create Metal texture");
        return;
    }

    NSLog(@"SkiaGraphicsLayer: Metal texture created successfully");
}
#else
- (void)createSkiaSurface {
    NSLog(@"SkiaGraphicsLayer: Skia not available, creating stub surface");
    // Create a minimal Metal texture for non-Skia builds
    int width = (int)self.canvasSize.width;
    int height = (int)self.canvasSize.height;

    MTLTextureDescriptor* textureDescriptor = [[MTLTextureDescriptor alloc] init];
    textureDescriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
    textureDescriptor.width = width;
    textureDescriptor.height = height;
    textureDescriptor.usage = MTLTextureUsageShaderRead;

    self.canvasTexture = [self.device newTextureWithDescriptor:textureDescriptor];
}
#endif

- (void)createRenderPipeline {
    NSError* error = nil;

    NSLog(@"SkiaGraphicsLayer: Creating render pipeline...");

    // Shader for displaying the Skia-rendered texture
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
    "};\n"
    "\n"
    "vertex VertexOut skia_graphics_vertex(VertexIn in [[stage_in]],\n"
    "                                      constant float2& viewportSize [[buffer(1)]]) {\n"
    "    VertexOut out;\n"
    "    out.position = float4(in.position, 0.0, 1.0);\n"
    "    out.texCoord = in.texCoord;\n"
    "    return out;\n"
    "}\n"
    "\n"
    "fragment float4 skia_graphics_fragment(VertexOut in [[stage_in]],\n"
    "                                       texture2d<float> canvasTexture [[texture(0)]]) {\n"
    "    constexpr sampler textureSampler(mag_filter::linear, min_filter::linear);\n"
    "    return canvasTexture.sample(textureSampler, in.texCoord);\n"
    "}\n";

    id<MTLLibrary> library = [self.device newLibraryWithSource:shaderSource options:nil error:&error];
    if (!library) {
        NSLog(@"SkiaGraphicsLayer: ERROR - Failed to create shader library: %@", error.localizedDescription);
        return;
    }

    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"skia_graphics_vertex"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"skia_graphics_fragment"];

    if (!vertexFunction || !fragmentFunction) {
        NSLog(@"SkiaGraphicsLayer: ERROR - Failed to find shader functions");
        return;
    }

    // Create render pipeline descriptor
    MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.label = @"Skia Graphics Pipeline";
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = fragmentFunction;
    pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    // Enable alpha blending
    pipelineDescriptor.colorAttachments[0].blendingEnabled = YES;
    pipelineDescriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pipelineDescriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pipelineDescriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDescriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDescriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipelineDescriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    // Create vertex descriptor
    MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];

    // Position
    vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[0].offset = 0;
    vertexDescriptor.attributes[0].bufferIndex = 0;

    // Texture coordinates
    vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[1].offset = sizeof(simd_float2);
    vertexDescriptor.attributes[1].bufferIndex = 0;

    // Layout
    vertexDescriptor.layouts[0].stride = sizeof(simd_float2) + sizeof(simd_float2);
    vertexDescriptor.layouts[0].stepRate = 1;
    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    pipelineDescriptor.vertexDescriptor = vertexDescriptor;

    // Create pipeline state
    self.pipelineState = [self.device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (!self.pipelineState) {
        NSLog(@"SkiaGraphicsLayer: ERROR - Failed to create pipeline state: %@", error.localizedDescription);
        return;
    }

    NSLog(@"SkiaGraphicsLayer: Render pipeline created successfully");
}

- (void)createVertexBuffer {
    // Full-screen quad vertices
    struct Vertex {
        simd_float2 position;
        simd_float2 texCoord;
    };

    struct Vertex vertices[] = {
        // Triangle 1
        {{-1.0f, -1.0f}, {0.0f, 1.0f}}, // Bottom left
        {{ 1.0f, -1.0f}, {1.0f, 1.0f}}, // Bottom right
        {{-1.0f,  1.0f}, {0.0f, 0.0f}}, // Top left

        // Triangle 2
        {{ 1.0f, -1.0f}, {1.0f, 1.0f}}, // Bottom right
        {{ 1.0f,  1.0f}, {1.0f, 0.0f}}, // Top right
        {{-1.0f,  1.0f}, {0.0f, 0.0f}}  // Top left
    };

    self.vertexBuffer = [self.device newBufferWithBytes:vertices
                                                 length:sizeof(vertices)
                                                options:MTLResourceStorageModeShared];
    if (!self.vertexBuffer) {
        NSLog(@"SkiaGraphicsLayer: ERROR - Failed to create vertex buffer");
        return;
    }

    self.vertexBuffer.label = @"Skia Graphics Vertex Buffer";
    NSLog(@"SkiaGraphicsLayer: Vertex buffer created: %zu bytes", sizeof(vertices));
}

- (void)clear {
#ifdef USE_SKIA
    if (self.canvas) {
        self.canvas->clear(SK_ColorTRANSPARENT);
    }
#endif
}

- (void)setColor:(simd_float4)color {
    self.currentColor = color;
#ifdef USE_SKIA
    // Convert to Skia color (0-255 range)
    SkColor skColor = SkColorSetARGB(
        (int)(color.w * 255),  // Alpha
        (int)(color.x * 255),  // Red
        (int)(color.y * 255),  // Green
        (int)(color.z * 255)   // Blue
    );
    self.paint.setColor(skColor);
#endif
}

- (void)setLineWidth:(float)width {
    self.lineWidth = width;
#ifdef USE_SKIA
    self.paint.setStrokeWidth(width);
#endif
}

- (void)drawLineFromX:(float)x1 y:(float)y1 toX:(float)x2 y:(float)y2 {
#ifdef USE_SKIA
    if (self.canvas) {
        self.paint.setStyle(SkPaint::kStroke_Style);
        self.canvas->drawLine(x1, y1, x2, y2, self.paint);
    }
#else
    NSLog(@"SkiaGraphicsLayer: drawLine called (Skia not available)");
#endif
}

- (void)drawRectX:(float)x y:(float)y width:(float)w height:(float)h {
#ifdef USE_SKIA
    if (self.canvas) {
        self.paint.setStyle(SkPaint::kStroke_Style);
        SkRect rect = SkRect::MakeXYWH(x, y, w, h);
        self.canvas->drawRect(rect, self.paint);
    }
#else
    NSLog(@"SkiaGraphicsLayer: drawRect called (Skia not available)");
#endif
}

- (void)fillRectX:(float)x y:(float)y width:(float)w height:(float)h {
#ifdef USE_SKIA
    if (self.canvas) {
        self.paint.setStyle(SkPaint::kFill_Style);
        SkRect rect = SkRect::MakeXYWH(x, y, w, h);
        self.canvas->drawRect(rect, self.paint);
    }
#else
    NSLog(@"SkiaGraphicsLayer: fillRect called (Skia not available)");
#endif
}

- (void)drawCircleX:(float)x y:(float)y radius:(float)radius {
#ifdef USE_SKIA
    if (self.canvas) {
        self.paint.setStyle(SkPaint::kStroke_Style);
        self.canvas->drawCircle(x, y, radius, self.paint);
    }
#else
    NSLog(@"SkiaGraphicsLayer: drawCircle called (Skia not available)");
#endif
}

- (void)fillCircleX:(float)x y:(float)y radius:(float)radius {
#ifdef USE_SKIA
    if (self.canvas) {
        self.paint.setStyle(SkPaint::kFill_Style);
        self.canvas->drawCircle(x, y, radius, self.paint);
    }
#else
    NSLog(@"SkiaGraphicsLayer: fillCircle called (Skia not available)");
#endif
}

- (void)drawTextX:(float)x y:(float)y text:(NSString*)text fontSize:(float)fontSize {
#ifdef USE_SKIA
    if (self.canvas && text) {
        // Create font
        SkFont font;
        font.setSize(fontSize);
        font.setTypeface(nullptr);

        // Set paint style for text
        SkPaint textPaint = self.paint;
        textPaint.setStyle(SkPaint::kFill_Style);
        textPaint.setAntiAlias(true);

        // Convert NSString to SkString
        const char* cString = [text UTF8String];
        SkString skString(cString);

        // Draw the text
        self.canvas->drawString(skString, x, y, font, textPaint);
    }
#else
    NSLog(@"SkiaGraphicsLayer: drawText called (Skia not available)");
#endif
}

- (void)updateTexture {
#ifdef USE_SKIA
    // Upload Skia pixel data to Metal texture
    if (self.canvasTexture && self.pixelData) {
        int width = (int)self.canvasSize.width;
        int height = (int)self.canvasSize.height;

        MTLRegion region = MTLRegionMake2D(0, 0, width, height);
        [self.canvasTexture replaceRegion:region
                              mipmapLevel:0
                                withBytes:self.pixelData
                              bytesPerRow:self.rowBytes];
    }
#endif
}

- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport {
    if (!self.pipelineState || !self.canvasTexture || !self.vertexBuffer) {
        return;
    }

    // Update texture with current Skia content
    [self updateTexture];

    [encoder setRenderPipelineState:self.pipelineState];
    [encoder setVertexBuffer:self.vertexBuffer offset:0 atIndex:0];
    [encoder setFragmentTexture:self.canvasTexture atIndex:0];

    // Set viewport size
    simd_float2 viewportSize = simd_make_float2(viewport.width, viewport.height);
    [encoder setVertexBytes:&viewportSize length:sizeof(viewportSize) atIndex:1];

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
}

@end

// C Interface - Global graphics layer instance
static SkiaGraphicsLayer* g_skiaGraphicsLayer = nil;

extern "C" {
    void skia_graphics_layer_init(void* device, float width, float height) {
        @autoreleasepool {
            id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
            CGSize canvasSize = CGSizeMake(width, height);
            g_skiaGraphicsLayer = [[SkiaGraphicsLayer alloc] initWithDevice:metalDevice canvasSize:canvasSize];
            NSLog(@"Skia graphics layer initialized: %.0fx%.0f", width, height);
        }
    }

    void skia_graphics_layer_clear() {
        @autoreleasepool {
            if (g_skiaGraphicsLayer) {
                [g_skiaGraphicsLayer clear];
            }
        }
    }

    void skia_graphics_layer_set_color(float r, float g, float b, float a) {
        @autoreleasepool {
            if (g_skiaGraphicsLayer) {
                simd_float4 color = simd_make_float4(r, g, b, a);
                [g_skiaGraphicsLayer setColor:color];
            }
        }
    }

    void skia_graphics_layer_set_line_width(float width) {
        @autoreleasepool {
            if (g_skiaGraphicsLayer) {
                [g_skiaGraphicsLayer setLineWidth:width];
            }
        }
    }

    void skia_graphics_layer_draw_line(float x1, float y1, float x2, float y2) {
        @autoreleasepool {
            if (g_skiaGraphicsLayer) {
                [g_skiaGraphicsLayer drawLineFromX:x1 y:y1 toX:x2 y:y2];
            }
        }
    }

    void skia_graphics_layer_draw_rect(float x, float y, float w, float h) {
        @autoreleasepool {
            if (g_skiaGraphicsLayer) {
                [g_skiaGraphicsLayer drawRectX:x y:y width:w height:h];
            }
        }
    }

    void skia_graphics_layer_fill_rect(float x, float y, float w, float h) {
        @autoreleasepool {
            if (g_skiaGraphicsLayer) {
                [g_skiaGraphicsLayer fillRectX:x y:y width:w height:h];
            }
        }
    }

    void skia_graphics_layer_draw_circle(float x, float y, float radius) {
        @autoreleasepool {
            if (g_skiaGraphicsLayer) {
                [g_skiaGraphicsLayer drawCircleX:x y:y radius:radius];
            }
        }
    }

    void skia_graphics_layer_fill_circle(float x, float y, float radius) {
        @autoreleasepool {
            if (g_skiaGraphicsLayer) {
                [g_skiaGraphicsLayer fillCircleX:x y:y radius:radius];
            }
        }
    }

    void skia_graphics_layer_render(void* encoder, float width, float height) {
        @autoreleasepool {
            if (g_skiaGraphicsLayer) {
                id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
                [g_skiaGraphicsLayer renderWithEncoder:metalEncoder viewport:CGSizeMake(width, height)];
            }
        }
    }

    void skia_graphics_layer_draw_text(float x, float y, const char* text, float fontSize) {
        @autoreleasepool {
            if (g_skiaGraphicsLayer && text) {
                NSString* nsText = [NSString stringWithUTF8String:text];
                [g_skiaGraphicsLayer drawTextX:x y:y text:nsText fontSize:fontSize];
            }
        }
    }

    void skia_graphics_layer_cleanup() {
        @autoreleasepool {
            g_skiaGraphicsLayer = nil;
            NSLog(@"Skia graphics layer cleaned up");
        }
    }
}
