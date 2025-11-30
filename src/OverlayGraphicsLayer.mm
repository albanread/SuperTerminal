//
//  OverlayGraphicsLayer.mm
//  SuperTerminal Framework - Overlay Graphics Layer (Graphics Layer 2)
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  A transparent overlay graphics layer that renders on top of all other layers
//  Based on the working MinimalGraphicsLayer implementation
//

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <simd/simd.h>
#include <vector>
#include <map>

#ifdef USE_SKIA
#include "skia.h"
#include "include/effects/SkGradientShader.h"
#include "include/effects/SkImageFilters.h"
#include "include/core/SkColorFilter.h"
#include "include/effects/SkColorMatrix.h"
#include "include/core/SkSurface.h"
#include "include/core/SkImage.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/codec/SkCodec.h"
#include "include/encode/SkPngEncoder.h"
#include "include/core/SkData.h"
#include "include/core/SkStream.h"
#include "include/ports/SkFontMgr_mac_ct.h"
#include "GlobalShutdown.h"
#include <unordered_map>

// Global image storage to keep sk_sp<SkImage> alive
static std::unordered_map<uint16_t, sk_sp<SkImage>> g_globalImages;

// Global path storage to keep SkPathBuilder objects alive
static std::unordered_map<uint16_t, SkPathBuilder> g_globalPaths;

// Forward declarations with C linkage
extern "C" {
    bool sprite_create_from_pixels(uint16_t id, const uint8_t* pixels, int width, int height);
    bool tile_create_from_pixels_impl(uint16_t id, const uint8_t* pixels, int width, int height);
}
#endif

// Graphics command types
typedef enum {
    GRAPHICS_CMD_CLEAR,
    GRAPHICS_CMD_DRAW_LINE,
    GRAPHICS_CMD_DRAW_RECT,
    GRAPHICS_CMD_FILL_RECT,
    GRAPHICS_CMD_DRAW_CIRCLE,
    GRAPHICS_CMD_FILL_CIRCLE,
    GRAPHICS_CMD_DRAW_TEXT,
    GRAPHICS_CMD_LINEAR_GRADIENT,
    GRAPHICS_CMD_LINEAR_GRADIENT_RECT,
    GRAPHICS_CMD_RADIAL_GRADIENT,
    GRAPHICS_CMD_RADIAL_GRADIENT_CIRCLE,
    GRAPHICS_CMD_DRAW_IMAGE,
    GRAPHICS_CMD_DRAW_IMAGE_SCALED,
    GRAPHICS_CMD_DRAW_IMAGE_RECT,
    GRAPHICS_CMD_CREATE_IMAGE,
    GRAPHICS_CMD_SAVE_IMAGE,
    GRAPHICS_CMD_CAPTURE_SCREEN,
    GRAPHICS_CMD_SET_BLEND_MODE,
    GRAPHICS_CMD_SET_BLUR_FILTER,
    GRAPHICS_CMD_SET_DROP_SHADOW,
    GRAPHICS_CMD_SET_COLOR_MATRIX,
    GRAPHICS_CMD_CLEAR_FILTERS,
    GRAPHICS_CMD_READ_PIXELS,
    GRAPHICS_CMD_WRITE_PIXELS,
    GRAPHICS_CMD_PUSH_MATRIX,
    GRAPHICS_CMD_POP_MATRIX,
    GRAPHICS_CMD_TRANSLATE,
    GRAPHICS_CMD_ROTATE,
    GRAPHICS_CMD_SCALE,
    GRAPHICS_CMD_SKEW,
    GRAPHICS_CMD_RESET_MATRIX,
    GRAPHICS_CMD_CREATE_PATH,
    GRAPHICS_CMD_PATH_MOVE_TO,
    GRAPHICS_CMD_PATH_LINE_TO,
    GRAPHICS_CMD_PATH_CURVE_TO,
    GRAPHICS_CMD_PATH_CLOSE,
    GRAPHICS_CMD_DRAW_PATH,
    GRAPHICS_CMD_FILL_PATH,
    GRAPHICS_CMD_CLIP_PATH,
    GRAPHICS_CMD_SWAP,
    GRAPHICS_CMD_BEGIN_SPRITE_REDIRECT,
    GRAPHICS_CMD_END_SPRITE_REDIRECT,
    GRAPHICS_CMD_BEGIN_TILE_REDIRECT,
    GRAPHICS_CMD_END_TILE_REDIRECT
} GraphicsCommandType;

// Blend mode constants
typedef enum {
    BLEND_NORMAL = 0,
    BLEND_MULTIPLY,
    BLEND_SCREEN,
    BLEND_OVERLAY,
    BLEND_SOFT_LIGHT,
    BLEND_COLOR_DODGE,
    BLEND_COLOR_BURN,
    BLEND_DIFFERENCE,
    BLEND_EXCLUSION
} BlendModeType;

// Graphics command structure
typedef struct {
    GraphicsCommandType type;
    float r, g, b, a;  // Color for all commands
    float r2, g2, b2, a2;  // Second color for gradients
    union {
        struct {
            float x1, y1, x2, y2;
        } drawLine;
        struct {
            float x, y, w, h;
        } rect;
        struct {
            float x, y, radius;
        } circle;
        struct {
            float x1, y1, x2, y2;
        } linearGradient;
        struct {
            float x, y, w, h;
            int direction;  // 0=horizontal, 1=vertical, 2=diagonal
        } linearGradientRect;
        struct {
            float centerX, centerY, radius;
        } radialGradient;
        struct {
            uint16_t imageId;
            float x, y;
        } drawImage;
        struct {
            uint16_t imageId;
            float x, y, scale;
        } drawImageScaled;
        struct {
            uint16_t imageId;
            float srcX, srcY, srcW, srcH;
            float dstX, dstY, dstW, dstH;
        } drawImageRect;
        struct {
            uint16_t imageId;
            int width, height;
        } createImage;
        struct {
            uint16_t imageId;
            char filename[256];
        } saveImage;
        struct {
            uint16_t imageId;
            int x, y, width, height;
        } captureScreen;
        struct {
            int blendMode;
        } setBlendMode;
        struct {
            float tx, ty;
        } translate;
        struct {
            float degrees;
        } rotate;
        struct {
            float sx, sy;
        } scale;
        struct {
            float kx, ky;
        } skew;
        struct {
            float x, y;
            float fontSize;
            char text[256];
        } drawText;
        struct {
            uint16_t pathId;
        } createPath;
        struct {
            uint16_t pathId;
            float x, y;
        } pathMoveTo;
        struct {
            uint16_t pathId;
            float x, y;
        } pathLineTo;
        struct {
            uint16_t pathId;
            float x1, y1, x2, y2, x3, y3;
        } pathCurveTo;
        struct {
            uint16_t pathId;
        } pathClose;
        struct {
            uint16_t pathId;
        } drawPath;
        struct {
            uint16_t pathId;
        } fillPath;
        struct {
            uint16_t pathId;
            int clipOp;
            bool antiAlias;
        } clipPath;
        struct {
            float radius;
        } setBlurFilter;
        struct {
            float dx, dy, blur, r, g, b, a;
        } setDropShadow;
        struct {
            float matrix[20];  // 4x5 color matrix
        } setColorMatrix;
        struct {
            uint16_t imageId;
            int x, y, width, height;
            uint8_t* pixelData;  // RGBA data, will be allocated/freed
        } readPixels;
        struct {
            uint16_t imageId;
            int x, y, width, height;
            uint8_t* pixelData;  // RGBA data to write
            int dataSize;        // Size of pixel data
        } writePixels;
        struct {
            uint16_t spriteId;
            int width, height;
        } beginSpriteRedirect;
        struct {
            uint16_t spriteId;
        } endSpriteRedirect;
        struct {
            uint16_t tileId;
            int width, height;
        } beginTileRedirect;
        struct {
            uint16_t tileId;
        } endTileRedirect;
        struct {
            uint16_t uiElementId;
            int width, height;
        } beginUiRedirect;
        struct {
            uint16_t uiElementId;
        } endUiRedirect;
    } params;
} GraphicsCommand;

// Global blend mode state
static int g_currentBlendMode = BLEND_NORMAL;

// Global transformation matrix stack
static std::vector<SkMatrix> g_matrixStack;
static SkMatrix g_currentMatrix = SkMatrix::I();

// Global filter state
static sk_sp<SkImageFilter> g_currentImageFilter = nullptr;
static sk_sp<SkColorFilter> g_currentColorFilter = nullptr;
static bool g_filtersEnabled = false;

// Global sprite creation state
static bool g_creatingSprite = false;
static uint16_t g_pendingSpriteId = 0;
static int g_pendingSpriteWidth = 0;
static int g_pendingSpriteHeight = 0;
static sk_sp<SkSurface> g_tempSkiaSurface = nullptr;
static SkCanvas* g_originalCanvas = nullptr;

// Global tile creation state
static bool g_creatingTile = false;
static uint16_t g_pendingTileId = 0;
static int g_pendingTileWidth = 0;
static int g_pendingTileHeight = 0;
static sk_sp<SkSurface> g_tempTileSkiaSurface = nullptr;
static SkCanvas* g_originalTileCanvas = nullptr;

// UI Layer function declaration
extern "C" {
    bool ui_set_element_texture_from_pixels(uint16_t id, void* pixels, int width, int height);
}

// Global UI element creation state
static bool g_creatingUiElement = false;
static uint16_t g_pendingUiElementId = 0;
static int g_pendingUiElementWidth = 0;
static int g_pendingUiElementHeight = 0;
static sk_sp<SkSurface> g_tempUiSkiaSurface = nullptr;
static SkCanvas* g_originalUiCanvas = nullptr;

// Function to convert our blend mode enum to SkBlendMode
static SkBlendMode convertBlendMode(int blendMode) {
    switch(blendMode) {
        case BLEND_NORMAL: return SkBlendMode::kSrcOver;
        case BLEND_MULTIPLY: return SkBlendMode::kMultiply;
        case BLEND_SCREEN: return SkBlendMode::kScreen;
        case BLEND_OVERLAY: return SkBlendMode::kOverlay;
        case BLEND_SOFT_LIGHT: return SkBlendMode::kSoftLight;
        case BLEND_COLOR_DODGE: return SkBlendMode::kColorDodge;
        case BLEND_COLOR_BURN: return SkBlendMode::kColorBurn;
        case BLEND_DIFFERENCE: return SkBlendMode::kDifference;
        case BLEND_EXCLUSION: return SkBlendMode::kExclusion;
        default: return SkBlendMode::kSrcOver;
    }
}

// Function to apply current filters to a paint object
static void applyFiltersToPaint(SkPaint& paint) {
    if (g_filtersEnabled) {
        if (g_currentImageFilter) {
            paint.setImageFilter(g_currentImageFilter);
        }
        if (g_currentColorFilter) {
            paint.setColorFilter(g_currentColorFilter);
        }
    }
    paint.setBlendMode(convertBlendMode(g_currentBlendMode));
}

@interface OverlaySkiaGraphicsLayer : NSObject
@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLTexture> frontTexture;
@property (nonatomic, strong) id<MTLTexture> backTexture;
@property (nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property (nonatomic, strong) id<MTLBuffer> vertexBuffer;
@property (nonatomic, assign) CGSize canvasSize;

// Command queue for thread-safe graphics operations
@property (nonatomic, strong) NSMutableArray<NSData*>* commandQueue;
@property (nonatomic, strong) NSLock* queueLock;

// Queue synchronization for wait operations
@property (nonatomic, strong) NSCondition* queueCondition;
@property (nonatomic, assign) BOOL queueProcessing;

// Persistent graphics state
@property (nonatomic, assign) simd_float4 inkColor;
@property (nonatomic, assign) simd_float4 paperColor;
@property (nonatomic, assign) BOOL graphicsVisible;

#ifdef USE_SKIA
// Double buffered surfaces
@property (nonatomic, assign) sk_sp<SkSurface> frontSurface;
@property (nonatomic, assign) sk_sp<SkSurface> backSurface;
@property (nonatomic, assign) SkCanvas* frontCanvas;
@property (nonatomic, assign) SkCanvas* backCanvas;
@property (nonatomic, assign) SkPaint paint;
@property (nonatomic, assign) void* frontPixelData;
@property (nonatomic, assign) void* backPixelData;

// Image storage (256 images max) - store raw SkImage pointers
@property (nonatomic, strong) NSMutableDictionary<NSNumber*, NSValue*>* loadedImages;
@property (nonatomic, assign) size_t rowBytes;
#endif

- (instancetype)initWithDevice:(id<MTLDevice>)device canvasSize:(CGSize)size;
- (void)clear;
- (void)setColor:(float)r g:(float)g b:(float)b a:(float)a;
- (void)drawLineFromX:(float)x1 y:(float)y1 toX:(float)x2 y:(float)y2;
- (void)drawRectX:(float)x y:(float)y width:(float)w height:(float)h;
- (void)fillRectX:(float)x y:(float)y width:(float)w height:(float)h;
- (void)drawCircleX:(float)x y:(float)y radius:(float)radius;
- (void)fillCircleX:(float)x y:(float)y radius:(float)radius;
- (void)drawTextX:(float)x y:(float)y text:(NSString*)text fontSize:(float)fontSize;
- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport;
- (void)processCommandQueue;
- (void)queueCommand:(GraphicsCommand)command;
- (void)waitQueueEmpty;
- (void)swapBuffers;
- (SkCanvas*)getCanvas;
- (void)setCanvas:(SkCanvas*)canvas;
@end

@implementation OverlaySkiaGraphicsLayer

- (instancetype)initWithDevice:(id<MTLDevice>)device canvasSize:(CGSize)size {
    self = [super init];
    if (self) {
        NSLog(@"OverlaySkiaGraphicsLayer: Initializing");
        self.device = device;
        self.canvasSize = size;

        // Initialize command queue
        self.commandQueue = [[NSMutableArray alloc] init];
        self.queueLock = [[NSLock alloc] init];

        // Initialize queue synchronization
        self.queueCondition = [[NSCondition alloc] init];
        self.queueProcessing = NO;

        // Initialize persistent graphics state
        self.inkColor = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f); // White ink
        self.paperColor = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f); // Transparent paper
        self.graphicsVisible = YES; // Graphics visible by default

#ifdef USE_SKIA
        // Initialize image storage
        self.loadedImages = [[NSMutableDictionary alloc] init];
#endif

        [self createCanvasTextures];
        [self createSkiaSurfaces];
        [self createRenderPipeline];
        [self createVertexBuffer];

        // Initialize with transparent canvas - only clear once at startup
        [self clear];

        NSLog(@"OverlaySkiaGraphicsLayer: Initialization complete");
    }
    return self;
}

- (void)createCanvasTextures {
    MTLTextureDescriptor* textureDescriptor = [[MTLTextureDescriptor alloc] init];
    textureDescriptor.pixelFormat = MTLPixelFormatBGRA8Unorm;
    textureDescriptor.width = (int)self.canvasSize.width;
    textureDescriptor.height = (int)self.canvasSize.height;
    textureDescriptor.usage = MTLTextureUsageShaderRead;

    self.frontTexture = [self.device newTextureWithDescriptor:textureDescriptor];
    self.backTexture = [self.device newTextureWithDescriptor:textureDescriptor];
}

- (void)createSkiaSurfaces {
#ifdef USE_SKIA
    int width = (int)self.canvasSize.width;
    int height = (int)self.canvasSize.height;

    self.rowBytes = width * 4; // RGBA
    size_t dataSize = height * self.rowBytes;

    // Create front buffer
    self.frontPixelData = malloc(dataSize);
    memset(self.frontPixelData, 0, dataSize);

    // Create back buffer
    self.backPixelData = malloc(dataSize);
    memset(self.backPixelData, 0, dataSize);

    SkImageInfo imageInfo = SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType);
    self.frontSurface = SkSurfaces::WrapPixels(imageInfo, self.frontPixelData, self.rowBytes);
    self.backSurface = SkSurfaces::WrapPixels(imageInfo, self.backPixelData, self.rowBytes);

    self.frontCanvas = self.frontSurface->getCanvas();
    self.backCanvas = self.backSurface->getCanvas();

    // Setup default paint with explicit properties
    self.paint.setAntiAlias(true);
    self.paint.setStyle(SkPaint::kFill_Style);  // Default to fill
    self.paint.setStrokeWidth(2.0f);
    self.paint.setColor(SK_ColorWHITE);
    NSLog(@"OverlaySkiaGraphicsLayer: Default paint setup complete");
#endif
}

- (void)createRenderPipeline {
    NSError* error = nil;
    NSString* shaderSource = @""
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VertexIn {\n"
    "    float2 position [[attribute(0)]];\n"
    "    float2 texCoord [[attribute(1)]];\n"
    "};\n"
    "struct VertexOut {\n"
    "    float4 position [[position]];\n"
    "    float2 texCoord;\n"
    "};\n"
    "vertex VertexOut working_vertex(VertexIn in [[stage_in]]) {\n"
    "    VertexOut out;\n"
    "    out.position = float4(in.position, 0.0, 1.0);\n"
    "    out.texCoord = in.texCoord;\n"
    "    return out;\n"
    "}\n"
    "fragment float4 working_fragment(VertexOut in [[stage_in]], texture2d<float> tex [[texture(0)]]) {\n"
    "    constexpr sampler samp(mag_filter::linear, min_filter::linear);\n"
    "    return tex.sample(samp, in.texCoord);\n"
    "}\n";

    id<MTLLibrary> library = [self.device newLibraryWithSource:shaderSource options:nil error:&error];
    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"working_vertex"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"working_fragment"];

    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vertexFunction;
    desc.fragmentFunction = fragmentFunction;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    desc.colorAttachments[0].blendingEnabled = YES;
    desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    MTLVertexDescriptor* vertexDesc = [[MTLVertexDescriptor alloc] init];
    vertexDesc.attributes[0].format = MTLVertexFormatFloat2;
    vertexDesc.attributes[0].offset = 0;
    vertexDesc.attributes[0].bufferIndex = 0;
    vertexDesc.attributes[1].format = MTLVertexFormatFloat2;
    vertexDesc.attributes[1].offset = 2 * sizeof(float);
    vertexDesc.attributes[1].bufferIndex = 0;
    vertexDesc.layouts[0].stride = 4 * sizeof(float);
    desc.vertexDescriptor = vertexDesc;

    self.pipelineState = [self.device newRenderPipelineStateWithDescriptor:desc error:&error];
}

- (void)createVertexBuffer {
    float vertices[] = {
        -1.0f, -1.0f, 0.0f, 1.0f,  // Bottom left
         1.0f, -1.0f, 1.0f, 1.0f,  // Bottom right
        -1.0f,  1.0f, 0.0f, 0.0f,  // Top left
         1.0f, -1.0f, 1.0f, 1.0f,  // Bottom right
         1.0f,  1.0f, 1.0f, 0.0f,  // Top right
        -1.0f,  1.0f, 0.0f, 0.0f   // Top left
    };
    self.vertexBuffer = [self.device newBufferWithBytes:vertices length:sizeof(vertices) options:MTLResourceStorageModeShared];
}

- (void)clear {
#ifdef USE_SKIA
    if (self.backCanvas) {
        NSLog(@"OverlaySkiaGraphicsLayer: Clearing back buffer");
        self.backCanvas->clear(SK_ColorTRANSPARENT);
    }
#endif
}

- (void)setColor:(float)r g:(float)g b:(float)b a:(float)a {
#ifdef USE_SKIA
    // Clamp color values to [0,1] range to prevent Skia overflow
    r = fmax(0.0f, fmin(1.0f, r));
    g = fmax(0.0f, fmin(1.0f, g));
    b = fmax(0.0f, fmin(1.0f, b));
    a = fmax(0.0f, fmin(1.0f, a));

    SkColor color = SkColorSetARGB((int)(a*255), (int)(r*255), (int)(g*255), (int)(b*255));
    self.paint.setColor(color);
#endif
}

- (void)drawLineFromX:(float)x1 y:(float)y1 toX:(float)x2 y:(float)y2 {
#ifdef USE_SKIA
    if (self.backCanvas) {
        NSLog(@"OverlaySkiaGraphicsLayer: Drawing line from (%.1f,%.1f) to (%.1f,%.1f)", x1, y1, x2, y2);
        SkPaint linePaint;
        linePaint.setAntiAlias(true);
        linePaint.setStyle(SkPaint::kStroke_Style);
        linePaint.setStrokeWidth(2.0f);
        linePaint.setColor(self.paint.getColor());
        self.backCanvas->drawLine(x1, y1, x2, y2, linePaint);
    }
#endif
}

- (void)drawRectX:(float)x y:(float)y width:(float)w height:(float)h {
#ifdef USE_SKIA
    if (self.backCanvas) {
        NSLog(@"OverlaySkiaGraphicsLayer: Drawing rect at (%.1f,%.1f) size %.1fx%.1f", x, y, w, h);
        SkPaint rectPaint;
        rectPaint.setAntiAlias(true);
        rectPaint.setStyle(SkPaint::kStroke_Style);
        rectPaint.setStrokeWidth(2.0f);
        rectPaint.setColor(self.paint.getColor());
        SkRect rect = SkRect::MakeXYWH(x, y, w, h);
        self.backCanvas->drawRect(rect, rectPaint);
    }
#endif
}

- (void)fillRectX:(float)x y:(float)y width:(float)w height:(float)h {
#ifdef USE_SKIA
    if (self.backCanvas) {
        NSLog(@"OverlaySkiaGraphicsLayer: Filling rect at (%.1f,%.1f) size %.1fx%.1f", x, y, w, h);
        SkPaint fillPaint;
        fillPaint.setAntiAlias(true);
        fillPaint.setStyle(SkPaint::kFill_Style);
        fillPaint.setColor(self.paint.getColor());
        SkRect rect = SkRect::MakeXYWH(x, y, w, h);
        self.backCanvas->drawRect(rect, fillPaint);
    }
#endif
}

- (void)drawCircleX:(float)x y:(float)y radius:(float)radius {
#ifdef USE_SKIA
    if (self.backCanvas) {
        NSLog(@"OverlaySkiaGraphicsLayer: Drawing circle at (%.1f,%.1f) radius %.1f", x, y, radius);
        SkPaint circlePaint;
        circlePaint.setAntiAlias(true);
        circlePaint.setStyle(SkPaint::kStroke_Style);
        circlePaint.setStrokeWidth(2.0f);
        circlePaint.setColor(self.paint.getColor());
        self.backCanvas->drawCircle(x, y, radius, circlePaint);
    }
#endif
}

- (void)fillCircleX:(float)x y:(float)y radius:(float)radius {
#ifdef USE_SKIA
    if (self.backCanvas) {
        SkPaint fillPaint;
        fillPaint.setAntiAlias(true);
        fillPaint.setStyle(SkPaint::kFill_Style);
        fillPaint.setColor(self.paint.getColor());
        self.backCanvas->drawCircle(x, y, radius, fillPaint);
    }
#endif
}

- (void)drawTextX:(float)x y:(float)y text:(NSString*)text fontSize:(float)fontSize {
    NSLog(@"OverlaySkiaGraphicsLayer: drawTextX called with text='%@' at (%.1f,%.1f) fontSize=%.1f", text, x, y, fontSize);
    if (!text) return;

    GraphicsCommand command;
    command.type = GRAPHICS_CMD_DRAW_TEXT;
    command.r = ((self.paint.getColor() >> 16) & 0xFF) / 255.0f;
    command.g = ((self.paint.getColor() >> 8) & 0xFF) / 255.0f;
    command.b = (self.paint.getColor() & 0xFF) / 255.0f;
    command.a = ((self.paint.getColor() >> 24) & 0xFF) / 255.0f;
    command.params.drawText.x = x;
    command.params.drawText.y = y;
    command.params.drawText.fontSize = fontSize;

    const char* cString = [text UTF8String];
    strncpy(command.params.drawText.text, cString, sizeof(command.params.drawText.text) - 1);
    command.params.drawText.text[sizeof(command.params.drawText.text) - 1] = '\0';

    NSLog(@"OverlaySkiaGraphicsLayer: Queueing GRAPHICS_CMD_DRAW_TEXT command with text='%s'", command.params.drawText.text);
    [self queueCommand:command];
}

- (void)updateTexture {
#ifdef USE_SKIA
    if (self.frontTexture && self.frontPixelData) {
        MTLRegion region = MTLRegionMake2D(0, 0, (int)self.canvasSize.width, (int)self.canvasSize.height);
        [self.frontTexture replaceRegion:region mipmapLevel:0 withBytes:self.frontPixelData bytesPerRow:self.rowBytes];
    }
#endif
}

- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport {
    if (!self.pipelineState || !self.frontTexture || !self.vertexBuffer) {
        return;
    }

    // Process queued commands on render thread
    [self processCommandQueue];

    // Only render if graphics are visible
    if (self.graphicsVisible) {
        [self updateTexture];
        [encoder setRenderPipelineState:self.pipelineState];
        [encoder setVertexBuffer:self.vertexBuffer offset:0 atIndex:0];
        [encoder setFragmentTexture:self.frontTexture atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    }
}


- (void)queueCommand:(GraphicsCommand)command {
    // NSLog(@"OverlaySkiaGraphicsLayer: Queueing command type %d on thread %@", command.type, [NSThread currentThread]);
    if (command.type == GRAPHICS_CMD_DRAW_TEXT) {
        // NSLog(@"OverlaySkiaGraphicsLayer: *** QUEUEING GRAPHICS_CMD_DRAW_TEXT: text='%s' ***", command.params.drawText.text);
    }
    NSData* commandData = [NSData dataWithBytes:&command length:sizeof(GraphicsCommand)];
    [self.queueCondition lock];
    [self.commandQueue addObject:commandData];
    // NSLog(@"OverlaySkiaGraphicsLayer: Queue now has %lu commands", (unsigned long)[self.commandQueue count]);
    [self.queueCondition unlock];
}

- (void)processCommandQueue {
#ifdef USE_SKIA
    if (!self.backCanvas) return;

    [self.queueCondition lock];
    self.queueProcessing = YES;
    NSArray* commands = [self.commandQueue copy];
    NSUInteger commandCount = [self.commandQueue count];
    [self.commandQueue removeAllObjects];
    [self.queueCondition unlock];

    // Process commands without logging (too spammy at 60fps)

    for (NSData* commandData in commands) {
        GraphicsCommand command;
        [commandData getBytes:&command length:sizeof(GraphicsCommand)];

        // Get color for this command
        // Clamp color values to prevent Skia overflow
        float r = fmax(0.0f, fmin(1.0f, command.r));
        float g = fmax(0.0f, fmin(1.0f, command.g));
        float b = fmax(0.0f, fmin(1.0f, command.b));
        float a = fmax(0.0f, fmin(1.0f, command.a));

        SkColor color = SkColorSetARGB(
            (int)(a * 255),
            (int)(r * 255),
            (int)(g * 255),
            (int)(b * 255)
        );

        // NSLog(@"OverlaySkiaGraphicsLayer: Processing command type: %d", command.type);
        switch (command.type) {
            case GRAPHICS_CMD_CLEAR:
                // NSLog(@"OverlaySkiaGraphicsLayer: Processing GRAPHICS_CMD_CLEAR");
                self.backCanvas->clear(SK_ColorTRANSPARENT);
                break;

            case GRAPHICS_CMD_DRAW_LINE: {
                // NSLog(@"OverlaySkiaGraphicsLayer: Processing GRAPHICS_CMD_DRAW_LINE");
                self.backCanvas->save();
                self.backCanvas->concat(g_currentMatrix);

                SkPaint linePaint;
                linePaint.setAntiAlias(true);
                linePaint.setStyle(SkPaint::kStroke_Style);
                linePaint.setStrokeWidth(1.0f);
                linePaint.setColor(color);

                applyFiltersToPaint(linePaint);

                self.backCanvas->drawLine(
                    command.params.drawLine.x1, command.params.drawLine.y1,
                    command.params.drawLine.x2, command.params.drawLine.y2,
                    linePaint
                );

                self.backCanvas->restore();
                break;
            }

            case GRAPHICS_CMD_DRAW_RECT: {
                // NSLog(@"OverlaySkiaGraphicsLayer: Processing GRAPHICS_CMD_DRAW_RECT");
                self.backCanvas->save();
                self.backCanvas->concat(g_currentMatrix);

                SkPaint rectPaint;
                rectPaint.setAntiAlias(true);
                rectPaint.setStyle(SkPaint::kStroke_Style);
                rectPaint.setStrokeWidth(2.0f);
                rectPaint.setColor(color);
                applyFiltersToPaint(rectPaint);
                SkRect rect = SkRect::MakeXYWH(
                    command.params.rect.x, command.params.rect.y,
                    command.params.rect.w, command.params.rect.h
                );
                self.backCanvas->drawRect(rect, rectPaint);

                self.backCanvas->restore();
                break;
            }

            case GRAPHICS_CMD_FILL_RECT: {
                // NSLog(@"OverlaySkiaGraphicsLayer: Processing GRAPHICS_CMD_FILL_RECT");
                self.backCanvas->save();
                self.backCanvas->concat(g_currentMatrix);

                SkPaint fillPaint;
                fillPaint.setAntiAlias(true);
                fillPaint.setStyle(SkPaint::kFill_Style);
                fillPaint.setColor(color);
                applyFiltersToPaint(fillPaint);
                SkRect rect = SkRect::MakeXYWH(
                    command.params.rect.x, command.params.rect.y,
                    command.params.rect.w, command.params.rect.h
                );
                self.backCanvas->drawRect(rect, fillPaint);

                self.backCanvas->restore();
                break;
            }

            case GRAPHICS_CMD_DRAW_CIRCLE: {
                self.backCanvas->save();
                self.backCanvas->concat(g_currentMatrix);

                SkPaint circlePaint;
                circlePaint.setAntiAlias(true);
                circlePaint.setStyle(SkPaint::kStroke_Style);
                circlePaint.setStrokeWidth(2.0f);
                circlePaint.setColor(color);
                applyFiltersToPaint(circlePaint);
                self.backCanvas->drawCircle(
                    command.params.circle.x, command.params.circle.y,
                    command.params.circle.radius, circlePaint
                );

                self.backCanvas->restore();
                break;
            }

            case GRAPHICS_CMD_FILL_CIRCLE: {
                self.backCanvas->save();
                self.backCanvas->concat(g_currentMatrix);

                SkPaint fillPaint;
                fillPaint.setAntiAlias(true);
                fillPaint.setStyle(SkPaint::kFill_Style);
                fillPaint.setColor(color);
                applyFiltersToPaint(fillPaint);
                self.backCanvas->drawCircle(
                    command.params.circle.x, command.params.circle.y,
                    command.params.circle.radius, fillPaint
                );

                self.backCanvas->restore();
                break;
            }

            case GRAPHICS_CMD_DRAW_TEXT: {
                // NSLog(@"OverlaySkiaGraphicsLayer: *** COMMAND QUEUE PROCESSING GRAPHICS_CMD_DRAW_TEXT ***");
                // NSLog(@"OverlaySkiaGraphicsLayer: Processing GRAPHICS_CMD_DRAW_TEXT: text='%s' at (%.1f,%.1f) fontSize=%.1f",
                //       command.params.drawText.text, command.params.drawText.x, command.params.drawText.y, command.params.drawText.fontSize);

                self.backCanvas->save();

                // Create font with normal size
                SkFont font;
                font.setSize(command.params.drawText.fontSize);

                // Create typeface using CoreText font manager for macOS
                sk_sp<SkTypeface> typeface = nullptr;

                // Get CoreText font manager
                sk_sp<SkFontMgr> fontMgr = SkFontMgr_New_CoreText(nullptr);
                if (!fontMgr) {
                    NSLog(@"OverlaySkiaGraphicsLayer: WARNING - Could not create CoreText font manager");
                    typeface = SkTypeface::MakeEmpty();
                } else {
                    // Use Monaco monospaced font in bold (built-in macOS system font)
                    typeface = fontMgr->legacyMakeTypeface("Monaco", SkFontStyle::Bold());
                    if (typeface) {
                        // NSLog(@"OverlaySkiaGraphicsLayer: Loaded Monaco monospaced font (Bold)");
                    }

                    // Fallback to Menlo if Monaco doesn't work
                    if (!typeface) {
                        typeface = fontMgr->legacyMakeTypeface("Menlo", SkFontStyle::Bold());
                        if (typeface) {
                            // NSLog(@"OverlaySkiaGraphicsLayer: Loaded Menlo monospaced font (Bold)");
                        }
                    }

                    // Another fallback - try SF Mono
                    if (!typeface) {
                        typeface = fontMgr->legacyMakeTypeface("SF Mono", SkFontStyle::Bold());
                        if (typeface) {
                            // NSLog(@"OverlaySkiaGraphicsLayer: Loaded SF Mono font (Bold)");
                        }
                    }

                    // Last resort: use empty typeface
                    if (!typeface) {
                        typeface = SkTypeface::MakeEmpty();
                        NSLog(@"OverlaySkiaGraphicsLayer: WARNING - Using empty typeface, no text will be visible");
                    }
                }

                font.setTypeface(typeface);
                // NSLog(@"OverlaySkiaGraphicsLayer: Successfully created typeface: %s",
                //       typeface ? "valid" : "null");

                // Set paint style for text - use actual ink color
                SkPaint textPaint;
                textPaint.setStyle(SkPaint::kFill_Style);
                textPaint.setAntiAlias(true);

                // Use the actual ink color from the command
                // Clamp color values to prevent Skia overflow
                float r = fmax(0.0f, fmin(1.0f, command.r));
                float g = fmax(0.0f, fmin(1.0f, command.g));
                float b = fmax(0.0f, fmin(1.0f, command.b));
                float a = fmax(0.0f, fmin(1.0f, command.a));

                SkColor inkColor = SkColorSetARGB(
                    (int)(a * 255),
                    (int)(r * 255),
                    (int)(g * 255),
                    (int)(b * 255)
                );
                textPaint.setColor(inkColor);
                // NSLog(@"OverlaySkiaGraphicsLayer: Using ink color RGBA(%.2f,%.2f,%.2f,%.2f)",
                //       command.r, command.g, command.b, command.a);

                // Draw the text at simple coordinates (no baseline adjustment)
                SkString skString(command.params.drawText.text);
                float simpleY = command.params.drawText.y + command.params.drawText.fontSize; // Simple offset

                // NSLog(@"OverlaySkiaGraphicsLayer: About to draw Skia text: '%s' at (%.1f,%.1f) with fontSize %.1f",
                //       command.params.drawText.text, command.params.drawText.x, simpleY, command.params.drawText.fontSize);

                self.backCanvas->drawString(skString,
                    command.params.drawText.x, simpleY,
                    font, textPaint);
                // NSLog(@"OverlaySkiaGraphicsLayer: Skia drawString completed");

                self.backCanvas->restore();
                break;
            }

            case GRAPHICS_CMD_LINEAR_GRADIENT: {
                self.backCanvas->save();
                self.backCanvas->concat(g_currentMatrix);

                SkColor color1 = SkColorSetARGB(command.a * 255, command.r * 255, command.g * 255, command.b * 255);
                SkColor color2 = SkColorSetARGB(command.a2 * 255, command.r2 * 255, command.g2 * 255, command.b2 * 255);
                SkColor colors[] = {color1, color2};
                SkPoint points[] = {
                    SkPoint::Make(command.params.linearGradient.x1, command.params.linearGradient.y1),
                    SkPoint::Make(command.params.linearGradient.x2, command.params.linearGradient.y2)
                };

                sk_sp<SkShader> gradient = SkGradientShader::MakeLinear(points, colors, nullptr, 2, SkTileMode::kClamp);
                SkPaint gradientPaint;
                gradientPaint.setAntiAlias(true);
                gradientPaint.setShader(gradient);
                applyFiltersToPaint(gradientPaint);
                SkRect rect = SkRect::MakeXYWH(
                    fmin(command.params.linearGradient.x1, command.params.linearGradient.x2) - 10,
                    fmin(command.params.linearGradient.y1, command.params.linearGradient.y2) - 10,
                    fabs(command.params.linearGradient.x2 - command.params.linearGradient.x1) + 20,
                    fabs(command.params.linearGradient.y2 - command.params.linearGradient.y1) + 20
                );
                self.backCanvas->drawRect(rect, gradientPaint);

                self.backCanvas->restore();
                break;
            }

            case GRAPHICS_CMD_LINEAR_GRADIENT_RECT: {
                self.backCanvas->save();
                self.backCanvas->concat(g_currentMatrix);

                SkColor color1 = SkColorSetARGB(command.a * 255, command.r * 255, command.g * 255, command.b * 255);
                SkColor color2 = SkColorSetARGB(command.a2 * 255, command.r2 * 255, command.g2 * 255, command.b2 * 255);
                SkColor colors[2] = {color1, color2};
                SkPoint points[2];
                // Set gradient direction based on direction parameter
                switch (command.params.linearGradientRect.direction) {
                    case 0: // Horizontal
                        points[0] = SkPoint::Make(command.params.linearGradientRect.x, command.params.linearGradientRect.y);
                        points[1] = SkPoint::Make(command.params.linearGradientRect.x + command.params.linearGradientRect.w, command.params.linearGradientRect.y);
                        break;
                    case 1: // Vertical
                        points[0] = SkPoint::Make(command.params.linearGradientRect.x, command.params.linearGradientRect.y);
                        points[1] = SkPoint::Make(command.params.linearGradientRect.x, command.params.linearGradientRect.y + command.params.linearGradientRect.h);
                        break;
                    case 2: // Diagonal
                        points[0] = SkPoint::Make(command.params.linearGradientRect.x, command.params.linearGradientRect.y);
                        points[1] = SkPoint::Make(command.params.linearGradientRect.x + command.params.linearGradientRect.w, command.params.linearGradientRect.y + command.params.linearGradientRect.h);
                        break;
                    default:
                        points[0] = SkPoint::Make(command.params.linearGradientRect.x, command.params.linearGradientRect.y);
                        points[1] = SkPoint::Make(command.params.linearGradientRect.x + command.params.linearGradientRect.w, command.params.linearGradientRect.y);
                        break;
                }
                auto gradient = SkGradientShader::MakeLinear(points, colors, nullptr, 2, SkTileMode::kClamp);
                SkPaint gradientPaint;
                gradientPaint.setAntiAlias(true);
                gradientPaint.setShader(gradient);
                applyFiltersToPaint(gradientPaint);
                SkRect rect = SkRect::MakeXYWH(
                    command.params.linearGradientRect.x, command.params.linearGradientRect.y,
                    command.params.linearGradientRect.w, command.params.linearGradientRect.h
                );
                self.backCanvas->drawRect(rect, gradientPaint);

                self.backCanvas->restore();
                break;
            }

            case GRAPHICS_CMD_RADIAL_GRADIENT: {
                self.backCanvas->save();
                self.backCanvas->concat(g_currentMatrix);

                SkColor color1 = SkColorSetARGB(command.a * 255, command.r * 255, command.g * 255, command.b * 255);
                SkColor color2 = SkColorSetARGB(command.a2 * 255, command.r2 * 255, command.g2 * 255, command.b2 * 255);
                SkColor colors[2] = {color1, color2};
                SkPoint center = SkPoint::Make(command.params.radialGradient.centerX, command.params.radialGradient.centerY);
                auto gradient = SkGradientShader::MakeRadial(center, command.params.radialGradient.radius, colors, nullptr, 2, SkTileMode::kClamp);
                SkPaint gradientPaint;
                gradientPaint.setAntiAlias(true);
                gradientPaint.setShader(gradient);
                applyFiltersToPaint(gradientPaint);
                SkRect rect = SkRect::MakeXYWH(
                    command.params.radialGradient.centerX - command.params.radialGradient.radius,
                    command.params.radialGradient.centerY - command.params.radialGradient.radius,
                    command.params.radialGradient.radius * 2,
                    command.params.radialGradient.radius * 2
                );
                self.backCanvas->drawRect(rect, gradientPaint);

                self.backCanvas->restore();
                break;
            }

            case GRAPHICS_CMD_RADIAL_GRADIENT_CIRCLE: {
                self.backCanvas->save();
                self.backCanvas->concat(g_currentMatrix);

                SkColor color1 = SkColorSetARGB(command.a * 255, command.r * 255, command.g * 255, command.b * 255);
                SkColor color2 = SkColorSetARGB(command.a2 * 255, command.r2 * 255, command.g2 * 255, command.b2 * 255);
                SkColor colors[2] = {color1, color2};
                SkPoint center = SkPoint::Make(command.params.radialGradient.centerX, command.params.radialGradient.centerY);
                auto gradient = SkGradientShader::MakeRadial(center, command.params.radialGradient.radius, colors, nullptr, 2, SkTileMode::kClamp);
                SkPaint gradientPaint;
                gradientPaint.setAntiAlias(true);
                gradientPaint.setShader(gradient);
                self.backCanvas->drawCircle(
                    command.params.radialGradient.centerX, command.params.radialGradient.centerY,
                    command.params.radialGradient.radius, gradientPaint
                );

                self.backCanvas->restore();
                break;
            }

            case GRAPHICS_CMD_DRAW_IMAGE: {
                self.backCanvas->save();
                self.backCanvas->concat(g_currentMatrix);

                NSNumber* imageKey = @(command.params.drawImage.imageId);
                NSValue* imageValue = self.loadedImages[imageKey];
                if (imageValue) {
                    SkImage* image = (SkImage*)[imageValue pointerValue];
                    if (image) {
                        SkPaint imagePaint;
                        imagePaint.setAntiAlias(true);
                        applyFiltersToPaint(imagePaint);
                        self.backCanvas->drawImage(image,
                                                  command.params.drawImage.x,
                                                  command.params.drawImage.y,
                                                  SkSamplingOptions(), &imagePaint);
                    }
                }

                self.backCanvas->restore();
                break;
            }

            case GRAPHICS_CMD_DRAW_IMAGE_SCALED: {
                self.backCanvas->save();
                self.backCanvas->concat(g_currentMatrix);

                NSNumber* imageKey = @(command.params.drawImageScaled.imageId);
                NSValue* imageValue = self.loadedImages[imageKey];
                if (imageValue) {
                    SkImage* image = (SkImage*)[imageValue pointerValue];
                    if (image) {
                        SkPaint imagePaint;
                        imagePaint.setAntiAlias(true);
                        applyFiltersToPaint(imagePaint);

                        float scale = command.params.drawImageScaled.scale;
                        SkRect dst = SkRect::MakeXYWH(
                            command.params.drawImageScaled.x,
                            command.params.drawImageScaled.y,
                            image->width() * scale,
                            image->height() * scale
                        );

                        self.backCanvas->drawImageRect(image, dst, SkSamplingOptions(), &imagePaint);
                    }
                }

                self.backCanvas->restore();
                break;
            }

            case GRAPHICS_CMD_DRAW_IMAGE_RECT: {
                self.backCanvas->save();
                self.backCanvas->concat(g_currentMatrix);

                NSNumber* imageKey = @(command.params.drawImageRect.imageId);
                NSValue* imageValue = self.loadedImages[imageKey];
                if (imageValue) {
                    SkImage* image = (SkImage*)[imageValue pointerValue];
                    if (image) {
                        SkPaint imagePaint;
                        imagePaint.setAntiAlias(true);
                        applyFiltersToPaint(imagePaint);


                        SkRect src = SkRect::MakeXYWH(
                            command.params.drawImageRect.srcX,
                            command.params.drawImageRect.srcY,
                            command.params.drawImageRect.srcW,
                            command.params.drawImageRect.srcH
                        );

                        SkRect dst = SkRect::MakeXYWH(
                            command.params.drawImageRect.dstX,
                            command.params.drawImageRect.dstY,
                            command.params.drawImageRect.dstW,
                            command.params.drawImageRect.dstH
                        );

                        self.backCanvas->drawImageRect(image, src, dst, SkSamplingOptions(), &imagePaint, SkCanvas::kStrict_SrcRectConstraint);
                    }
                }

                self.backCanvas->restore();
                break;
            }

            case GRAPHICS_CMD_CREATE_IMAGE: {
                int width = command.params.createImage.width;
                int height = command.params.createImage.height;
                SkColor fillColor = SkColorSetARGB(command.a * 255, command.r * 255, command.g * 255, command.b * 255);

                SkImageInfo imageInfo = SkImageInfo::MakeN32Premul(width, height);
                sk_sp<SkSurface> surface = SkSurfaces::Raster(imageInfo);
                if (surface) {
                    SkCanvas* canvas = surface->getCanvas();
                    canvas->clear(fillColor);

                    sk_sp<SkImage> image = surface->makeImageSnapshot();
                    if (image) {
                        NSNumber* key = @(command.params.createImage.imageId);
                        g_globalImages[command.params.createImage.imageId] = image;
                        NSValue* imageValue = [NSValue valueWithPointer:image.get()];
                        self.loadedImages[key] = imageValue;
                        NSLog(@"Created image %d (%dx%d)", command.params.createImage.imageId, width, height);
                    }
                }
                break;
            }

            case GRAPHICS_CMD_SAVE_IMAGE: {
                uint16_t imageId = command.params.saveImage.imageId;
                const char* filename = command.params.saveImage.filename;

                auto it = g_globalImages.find(imageId);
                if (it != g_globalImages.end()) {
                    sk_sp<SkImage> image = it->second;
                    sk_sp<SkData> pngData = SkPngEncoder::Encode(nullptr, image.get(), {});
                    if (pngData) {
                        NSString* path = [NSString stringWithUTF8String:filename];
                        NSData* data = [NSData dataWithBytes:pngData->data() length:pngData->size()];
                        BOOL success = [data writeToFile:path atomically:YES];
                        if (success) {
                            NSLog(@"Saved image %d to %s", imageId, filename);
                        } else {
                            NSLog(@"Failed to save image %d to %s", imageId, filename);
                        }
                    }
                } else {
                    NSLog(@"Image %d not found for saving", imageId);
                }
                break;
            }

            case GRAPHICS_CMD_CAPTURE_SCREEN: {
                // Capture current back buffer to an image
                if (self.backSurface) {
                    sk_sp<SkImage> snapshot = self.backSurface->makeImageSnapshot();
                    if (snapshot) {
                        int x = command.params.captureScreen.x;
                        int y = command.params.captureScreen.y;
                        int w = command.params.captureScreen.width;
                        int h = command.params.captureScreen.height;

                        // For now, just use the full snapshot (subset support can be added later)
                        sk_sp<SkImage> finalImage = snapshot;

                        if (finalImage) {
                            NSNumber* key = @(command.params.captureScreen.imageId);
                            g_globalImages[command.params.captureScreen.imageId] = finalImage;
                            NSValue* imageValue = [NSValue valueWithPointer:finalImage.get()];
                            self.loadedImages[key] = imageValue;
                            NSLog(@"Captured screen to image %d (%dx%d)", command.params.captureScreen.imageId, w, h);
                        }
                    }
                }
                break;
            }

            case GRAPHICS_CMD_SWAP:
                [self swapBuffers];
                break;

            case GRAPHICS_CMD_SET_BLEND_MODE: {
                g_currentBlendMode = command.params.setBlendMode.blendMode;
                break;
            }

            case GRAPHICS_CMD_SET_BLUR_FILTER: {
                float radius = command.params.setBlurFilter.radius;
                if (radius > 0) {
                    g_currentImageFilter = SkImageFilters::Blur(radius, radius, nullptr);
                    g_filtersEnabled = true;
                } else {
                    g_currentImageFilter = nullptr;
                    if (!g_currentColorFilter) {
                        g_filtersEnabled = false;
                    }
                }
                break;
            }

            case GRAPHICS_CMD_SET_DROP_SHADOW: {
                float dx = command.params.setDropShadow.dx;
                float dy = command.params.setDropShadow.dy;
                float blur = command.params.setDropShadow.blur;
                SkColor shadowColor = SkColorSetARGB(
                    (int)(command.params.setDropShadow.a * 255),
                    (int)(command.params.setDropShadow.r * 255),
                    (int)(command.params.setDropShadow.g * 255),
                    (int)(command.params.setDropShadow.b * 255)
                );
                g_currentImageFilter = SkImageFilters::DropShadow(dx, dy, blur, blur, shadowColor, nullptr);
                g_filtersEnabled = true;
                break;
            }

            case GRAPHICS_CMD_SET_COLOR_MATRIX: {
                g_currentColorFilter = SkColorFilters::Matrix(command.params.setColorMatrix.matrix);
                g_filtersEnabled = true;
                break;
            }

            case GRAPHICS_CMD_CLEAR_FILTERS: {
                g_currentImageFilter = nullptr;
                g_currentColorFilter = nullptr;
                g_filtersEnabled = false;
                break;
            }

            case GRAPHICS_CMD_READ_PIXELS: {
                uint16_t imageId = command.params.readPixels.imageId;
                int x = command.params.readPixels.x;
                int y = command.params.readPixels.y;
                int width = command.params.readPixels.width;
                int height = command.params.readPixels.height;

                if (imageId == 0) {
                    // Read from screen (back buffer)
                    if (self.backCanvas) {
                        SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
                        size_t rowBytes = width * 4;
                        uint8_t* pixels = (uint8_t*)malloc(height * rowBytes);
                        if (pixels) {
                            if (self.backCanvas->readPixels(info, pixels, rowBytes, x, y)) {
                                // Store pixels in command for retrieval
                                command.params.readPixels.pixelData = pixels;
                            } else {
                                free(pixels);
                                command.params.readPixels.pixelData = nullptr;
                            }
                        }
                    }
                } else {
                    // Read from image
                    auto it = g_globalImages.find(imageId);
                    if (it != g_globalImages.end()) {
                        sk_sp<SkImage> image = it->second;
                        SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
                        size_t rowBytes = width * 4;
                        uint8_t* pixels = (uint8_t*)malloc(height * rowBytes);
                        if (pixels) {
                            if (image->readPixels(nullptr, info, pixels, rowBytes, x, y)) {
                                command.params.readPixels.pixelData = pixels;
                            } else {
                                free(pixels);
                                command.params.readPixels.pixelData = nullptr;
                            }
                        }
                    }
                }
                break;
            }

            case GRAPHICS_CMD_WRITE_PIXELS: {
                uint16_t imageId = command.params.writePixels.imageId;
                int x = command.params.writePixels.x;
                int y = command.params.writePixels.y;
                int width = command.params.writePixels.width;
                int height = command.params.writePixels.height;
                uint8_t* pixels = command.params.writePixels.pixelData;

                if (pixels) {
                    if (imageId == 0) {
                        // Write to screen (back buffer)
                        if (self.backCanvas) {
                            SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
                            size_t rowBytes = width * 4;
                            self.backCanvas->writePixels(info, pixels, rowBytes, x, y);
                        }
                    } else {
                        // Writing to existing images is complex - for now, create new image
                        SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
                        size_t rowBytes = width * 4;
                        sk_sp<SkData> data = SkData::MakeWithCopy(pixels, height * rowBytes);
                        sk_sp<SkImage> newImage = SkImages::RasterFromData(info, data, rowBytes);
                        if (newImage) {
                            g_globalImages[imageId] = newImage;
                        }
                    }
                    // Free the pixel data after use
                    free(pixels);
                }
                break;
            }
            case GRAPHICS_CMD_PUSH_MATRIX: {
                g_matrixStack.push_back(g_currentMatrix);
                break;
            }

            case GRAPHICS_CMD_POP_MATRIX: {
                if (!g_matrixStack.empty()) {
                    g_currentMatrix = g_matrixStack.back();
                    g_matrixStack.pop_back();
                }
                break;
            }

            case GRAPHICS_CMD_TRANSLATE: {
                g_currentMatrix.preTranslate(command.params.translate.tx, command.params.translate.ty);
                break;
            }

            case GRAPHICS_CMD_ROTATE: {
                g_currentMatrix.preRotate(command.params.rotate.degrees);
                break;
            }

            case GRAPHICS_CMD_SCALE: {
                g_currentMatrix.preScale(command.params.scale.sx, command.params.scale.sy);
                break;
            }

            case GRAPHICS_CMD_SKEW: {
                g_currentMatrix.preSkew(command.params.skew.kx, command.params.skew.ky);
                break;
            }

            case GRAPHICS_CMD_RESET_MATRIX: {
                g_currentMatrix.setIdentity();
                break;
            }

            case GRAPHICS_CMD_CREATE_PATH: {
                uint16_t pathId = command.params.createPath.pathId;
                g_globalPaths[pathId] = SkPathBuilder();
                break;
            }

            case GRAPHICS_CMD_PATH_MOVE_TO: {
                uint16_t pathId = command.params.pathMoveTo.pathId;
                auto it = g_globalPaths.find(pathId);
                if (it != g_globalPaths.end()) {
                    it->second.moveTo(command.params.pathMoveTo.x, command.params.pathMoveTo.y);
                }
                break;
            }

            case GRAPHICS_CMD_PATH_LINE_TO: {
                uint16_t pathId = command.params.pathLineTo.pathId;
                auto it = g_globalPaths.find(pathId);
                if (it != g_globalPaths.end()) {
                    it->second.lineTo(command.params.pathLineTo.x, command.params.pathLineTo.y);
                }
                break;
            }

            case GRAPHICS_CMD_PATH_CURVE_TO: {
                uint16_t pathId = command.params.pathCurveTo.pathId;
                auto it = g_globalPaths.find(pathId);
                if (it != g_globalPaths.end()) {
                    it->second.cubicTo(
                        command.params.pathCurveTo.x1, command.params.pathCurveTo.y1,
                        command.params.pathCurveTo.x2, command.params.pathCurveTo.y2,
                        command.params.pathCurveTo.x3, command.params.pathCurveTo.y3
                    );
                }
                break;
            }

            case GRAPHICS_CMD_PATH_CLOSE: {
                uint16_t pathId = command.params.pathClose.pathId;
                auto it = g_globalPaths.find(pathId);
                if (it != g_globalPaths.end()) {
                    it->second.close();
                }
                break;
            }

            case GRAPHICS_CMD_DRAW_PATH: {
                uint16_t pathId = command.params.drawPath.pathId;
                auto it = g_globalPaths.find(pathId);
                if (it != g_globalPaths.end()) {
                    self.backCanvas->save();
                    self.backCanvas->concat(g_currentMatrix);

                    SkPaint pathPaint;
                    pathPaint.setAntiAlias(true);
                    pathPaint.setStyle(SkPaint::kStroke_Style);
                    pathPaint.setStrokeWidth(2.0f);

                    // Use current ink color
                    SkColor inkColor = SkColorSetARGB(
                        (int)(self.inkColor.w * 255),
                        (int)(self.inkColor.x * 255),
                        (int)(self.inkColor.y * 255),
                        (int)(self.inkColor.z * 255)
                    );
                    pathPaint.setColor(inkColor);
                    applyFiltersToPaint(pathPaint);

                    SkPath path = it->second.snapshot();
                    self.backCanvas->drawPath(path, pathPaint);
                    self.backCanvas->restore();
                }
                break;
            }

            case GRAPHICS_CMD_FILL_PATH: {
                uint16_t pathId = command.params.fillPath.pathId;
                auto it = g_globalPaths.find(pathId);
                if (it != g_globalPaths.end()) {
                    self.backCanvas->save();
                    self.backCanvas->concat(g_currentMatrix);

                    SkPaint pathPaint;
                    pathPaint.setAntiAlias(true);
                    pathPaint.setStyle(SkPaint::kFill_Style);

                    // Use current ink color
                    SkColor inkColor = SkColorSetARGB(
                        (int)(self.inkColor.w * 255),
                        (int)(self.inkColor.x * 255),
                        (int)(self.inkColor.y * 255),
                        (int)(self.inkColor.z * 255)
                    );
                    pathPaint.setColor(inkColor);
                    applyFiltersToPaint(pathPaint);

                    SkPath path = it->second.snapshot();
                    self.backCanvas->drawPath(path, pathPaint);
                    self.backCanvas->restore();
                }
                break;
            }

            case GRAPHICS_CMD_CLIP_PATH: {
                uint16_t pathId = command.params.clipPath.pathId;
                auto it = g_globalPaths.find(pathId);
                if (it != g_globalPaths.end()) {
                    SkClipOp clipOp = (SkClipOp)command.params.clipPath.clipOp;
                    bool antiAlias = command.params.clipPath.antiAlias;
                    SkPath path = it->second.snapshot();
                    self.backCanvas->clipPath(path, clipOp, antiAlias);
                }
                break;
            }

            case GRAPHICS_CMD_BEGIN_SPRITE_REDIRECT: {
                uint16_t spriteId = command.params.beginSpriteRedirect.spriteId;
                int width = command.params.beginSpriteRedirect.width;
                int height = command.params.beginSpriteRedirect.height;

                NSLog(@"DEBUG: Processing BEGIN_SPRITE_REDIRECT command - spriteId=%d, size=%dx%d", spriteId, width, height);

                // Create temporary Skia surface for this sprite
                SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
                g_tempSkiaSurface = SkSurfaces::Raster(info);
                if (g_tempSkiaSurface) {
                    g_tempSkiaSurface->getCanvas()->clear(SK_ColorTRANSPARENT);

                    // Store original canvas and redirect to temp surface
                    g_originalCanvas = self.backCanvas;
                    self.backCanvas = g_tempSkiaSurface->getCanvas();

                    g_creatingSprite = true;
                    g_pendingSpriteId = spriteId;
                    g_pendingSpriteWidth = width;
                    g_pendingSpriteHeight = height;

                    NSLog(@"DEBUG: Canvas redirected to temp surface for sprite %d", spriteId);
                } else {
                    NSLog(@"DEBUG: FAILED to create temp surface for sprite %d", spriteId);
                }
                break;
            }

            case GRAPHICS_CMD_END_SPRITE_REDIRECT: {
                uint16_t spriteId = command.params.endSpriteRedirect.spriteId;

                NSLog(@"DEBUG: Processing END_SPRITE_REDIRECT command - spriteId=%d", spriteId);
                NSLog(@"DEBUG: Current state - g_creatingSprite=%s, g_pendingSpriteId=%d",
                      g_creatingSprite ? "true" : "false", g_pendingSpriteId);

                if (g_creatingSprite && spriteId == g_pendingSpriteId && g_tempSkiaSurface && g_originalCanvas) {
                    // Restore original canvas
                    self.backCanvas = g_originalCanvas;
                    NSLog(@"DEBUG: Canvas restored to original");

                    // Extract pixels from temp surface
                    SkImageInfo info = SkImageInfo::Make(g_pendingSpriteWidth, g_pendingSpriteHeight,
                                                       kRGBA_8888_SkColorType, kPremul_SkAlphaType);
                    size_t rowBytes = g_pendingSpriteWidth * 4;
                    uint8_t* pixels = (uint8_t*)malloc(g_pendingSpriteHeight * rowBytes);
                    bool success = false;

                    if (pixels && g_tempSkiaSurface->readPixels(info, pixels, rowBytes, 0, 0)) {
                        NSLog(@"DEBUG: Successfully read %dx%d pixels from temp surface", g_pendingSpriteWidth, g_pendingSpriteHeight);

                        success = sprite_create_from_pixels(spriteId, pixels, g_pendingSpriteWidth, g_pendingSpriteHeight);
                        NSLog(@"DEBUG: sprite_create_from_pixels(%d) returned: %s", spriteId, success ? "SUCCESS" : "FAILED");
                    } else {
                        NSLog(@"DEBUG: FAILED to read pixels from temp surface");
                    }

                    // Clean up
                    if (pixels) free(pixels);
                    g_tempSkiaSurface = nullptr;
                    g_originalCanvas = nullptr;
                    g_creatingSprite = false;
                    g_pendingSpriteId = 0;
                    g_pendingSpriteWidth = 0;
                    g_pendingSpriteHeight = 0;

                    NSLog(@"DEBUG: END_SPRITE_REDIRECT cleanup complete, success=%s", success ? "SUCCESS" : "FAILED");
                } else {
                    NSLog(@"DEBUG: END_SPRITE_REDIRECT FAILED - invalid state or sprite ID mismatch");
                }
                break;
            }

            case GRAPHICS_CMD_BEGIN_TILE_REDIRECT: {
                uint16_t tileId = command.params.beginTileRedirect.tileId;
                int width = command.params.beginTileRedirect.width;
                int height = command.params.beginTileRedirect.height;

                NSLog(@"DEBUG: Processing BEGIN_TILE_REDIRECT command - tileId=%d, size=%dx%d", tileId, width, height);

                // Create temporary Skia surface for this tile
                SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
                g_tempTileSkiaSurface = SkSurfaces::Raster(info);
                if (g_tempTileSkiaSurface) {
                    g_tempTileSkiaSurface->getCanvas()->clear(SK_ColorTRANSPARENT);

                    // Store original canvas and redirect to temp surface
                    g_originalTileCanvas = self.backCanvas;
                    self.backCanvas = g_tempTileSkiaSurface->getCanvas();

                    g_creatingTile = true;
                    g_pendingTileId = tileId;
                    g_pendingTileWidth = width;
                    g_pendingTileHeight = height;

                    NSLog(@"DEBUG: Canvas redirected to temp surface for tile %d", tileId);
                } else {
                    NSLog(@"DEBUG: FAILED to create temp surface for tile %d", tileId);
                }
                break;
            }

            case GRAPHICS_CMD_END_TILE_REDIRECT: {
                uint16_t tileId = command.params.endTileRedirect.tileId;

                NSLog(@"DEBUG: Processing END_TILE_REDIRECT command - tileId=%d", tileId);
                NSLog(@"DEBUG: Current state - g_creatingTile=%s, g_pendingTileId=%d",
                      g_creatingTile ? "true" : "false", g_pendingTileId);

                if (g_creatingTile && tileId == g_pendingTileId && g_tempTileSkiaSurface && g_originalTileCanvas) {
                    // Restore original canvas
                    self.backCanvas = g_originalTileCanvas;
                    NSLog(@"DEBUG: Canvas restored to original");

                    // Extract pixels from temp surface
                    SkImageInfo info = SkImageInfo::Make(g_pendingTileWidth, g_pendingTileHeight,
                                                       kRGBA_8888_SkColorType, kPremul_SkAlphaType);
                    size_t rowBytes = g_pendingTileWidth * 4;
                    uint8_t* pixels = (uint8_t*)malloc(g_pendingTileHeight * rowBytes);
                    bool success = false;

                    if (pixels && g_tempTileSkiaSurface->readPixels(info, pixels, rowBytes, 0, 0)) {
                        NSLog(@"DEBUG: Successfully read %dx%d pixels from temp surface", g_pendingTileWidth, g_pendingTileHeight);

                        success = tile_create_from_pixels_impl(tileId, pixels, g_pendingTileWidth, g_pendingTileHeight);
                        NSLog(@"DEBUG: tile_create_from_pixels_impl(%d) returned: %s", tileId, success ? "SUCCESS" : "FAILED");
                    } else {
                        NSLog(@"DEBUG: FAILED to read pixels from temp surface");
                    }

                    // Clean up
                    if (pixels) free(pixels);
                    g_tempTileSkiaSurface = nullptr;
                    g_originalTileCanvas = nullptr;
                    g_creatingTile = false;
                    g_pendingTileId = 0;
                    g_pendingTileWidth = 0;
                    g_pendingTileHeight = 0;

                    NSLog(@"DEBUG: END_TILE_REDIRECT cleanup complete, success=%s", success ? "SUCCESS" : "FAILED");
                } else {
                    NSLog(@"DEBUG: END_TILE_REDIRECT FAILED - invalid state or tile ID mismatch");
                }
                break;
            }


        }
    }

    // Signal that queue processing is complete
    [self.queueCondition lock];
    self.queueProcessing = NO;
    [self.queueCondition broadcast];
    [self.queueCondition unlock];
#endif
}

- (void)waitQueueEmpty {
    [self.queueCondition lock];

    // Wait while there are commands in the queue OR queue is currently being processed
    while ([self.commandQueue count] > 0 || self.queueProcessing) {
        [self.queueCondition wait];
    }

    [self.queueCondition unlock];
}

- (void)swapBuffers {
#ifdef USE_SKIA
    // NSLog(@"OverlaySkiaGraphicsLayer: Swapping front and back buffers");

    // Swap the surfaces
    sk_sp<SkSurface> tempSurface = self.frontSurface;
    self.frontSurface = self.backSurface;
    self.backSurface = tempSurface;

    // Swap the canvases
    SkCanvas* tempCanvas = self.frontCanvas;
    self.frontCanvas = self.backCanvas;
    self.backCanvas = tempCanvas;

    // Swap the pixel data pointers
    void* tempPixelData = self.frontPixelData;
    self.frontPixelData = self.backPixelData;
    self.backPixelData = tempPixelData;

    // Swap the Metal textures
    id<MTLTexture> tempTexture = self.frontTexture;
    self.frontTexture = self.backTexture;
    self.backTexture = tempTexture;

    // NSLog(@"OverlaySkiaGraphicsLayer: Buffer swap complete");
#endif
}

- (void)dealloc {
#ifdef USE_SKIA
    if (self.frontPixelData) {
        free(self.frontPixelData);
        self.frontPixelData = nullptr;
    }
    if (self.backPixelData) {
        free(self.backPixelData);
        self.backPixelData = nullptr;
    }
#endif
}

- (SkCanvas*)getCanvas {
    return self.backCanvas;
}

- (void)setCanvas:(SkCanvas*)canvas {
    self.backCanvas = canvas;
}

@end

// Global state
static bool g_minimalGraphicsInitialized = false;
static OverlaySkiaGraphicsLayer* g_overlayLayer = nil;
static bool g_overlayVisible = true;

extern "C" {
    bool overlay_graphics_layer_initialize(void* device, int width, int height) {
        NSLog(@"OverlayGraphicsLayer: initialize called with device=%p size=%dx%d", device, width, height);

        // Register as active subsystem for shutdown coordination
        register_active_subsystem();

        id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;

        g_overlayLayer = [[OverlaySkiaGraphicsLayer alloc] initWithDevice:metalDevice canvasSize:CGSizeMake(width, height)];
        g_minimalGraphicsInitialized = (g_overlayLayer != nil);

        NSLog(@"OverlayGraphicsLayer: initialize complete - success=%d", g_minimalGraphicsInitialized);
        return g_minimalGraphicsInitialized;
    }

    void overlay_graphics_layer_clear(void) {
        // NSLog(@"C API: overlay_graphics_layer_clear called on thread %@", [NSThread currentThread]);
        if (g_overlayLayer) {
            GraphicsCommand command;
            command.type = GRAPHICS_CMD_CLEAR;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_clear_with_color(float r, float g, float b, float a) {
        // NSLog(@"C API: overlay_graphics_layer_clear_with_color called with RGBA(%.2f,%.2f,%.2f,%.2f)", r, g, b, a);
        if (g_overlayLayer) {
            GraphicsCommand command;
            command.type = GRAPHICS_CMD_CLEAR;
            command.r = r;
            command.g = g;
            command.b = b;
            command.a = a;
            [g_overlayLayer queueCommand:command];
        }
    }

    bool overlay_graphics_layer_is_initialized(void) {
        return g_minimalGraphicsInitialized && g_overlayLayer != nil;
    }

    void overlay_graphics_layer_shutdown(void) {
        NSLog(@"OverlayGraphicsLayer: shutdown called");
        g_overlayLayer = nil;

        // Unregister from shutdown system
        unregister_active_subsystem();
        g_minimalGraphicsInitialized = NO;
    }

    void overlay_graphics_layer_set_color(float r, float g, float b, float a) {
        NSLog(@"overlay_graphics_layer_set_color: RGBA(%.2f, %.2f, %.2f, %.2f) - storing for next draw", r, g, b, a);
        // Store color in layer for next draw commands - no need to queue this
        if (g_overlayLayer) {
            [g_overlayLayer setColor:r g:g b:b a:a];
        }
    }

    void overlay_graphics_layer_set_line_width(float width) {
        // Line width handled by Skia paint
    }

    void overlay_graphics_layer_draw_line(float x1, float y1, float x2, float y2) {
        NSLog(@"overlay_graphics_layer_draw_line: (%.1f,%.1f) to (%.1f,%.1f)", x1, y1, x2, y2);
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_DRAW_LINE;
            command.r = g_overlayLayer.inkColor.x;
            command.g = g_overlayLayer.inkColor.y;
            command.b = g_overlayLayer.inkColor.z;
            command.a = g_overlayLayer.inkColor.w;
            command.params.drawLine.x1 = x1;
            command.params.drawLine.y1 = y1;
            command.params.drawLine.x2 = x2;
            command.params.drawLine.y2 = y2;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_draw_rect(float x, float y, float w, float h) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_DRAW_RECT;
            command.r = g_overlayLayer.inkColor.x;
            command.g = g_overlayLayer.inkColor.y;
            command.b = g_overlayLayer.inkColor.z;
            command.a = g_overlayLayer.inkColor.w;
            command.params.rect.x = x;
            command.params.rect.y = y;
            command.params.rect.w = w;
            command.params.rect.h = h;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_fill_rect(float x, float y, float w, float h) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_FILL_RECT;
            command.r = g_overlayLayer.inkColor.x;
            command.g = g_overlayLayer.inkColor.y;
            command.b = g_overlayLayer.inkColor.z;
            command.a = g_overlayLayer.inkColor.w;
            command.params.rect.x = x;
            command.params.rect.y = y;
            command.params.rect.w = w;
            command.params.rect.h = h;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_draw_circle(float x, float y, float radius) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_DRAW_CIRCLE;
            command.r = g_overlayLayer.inkColor.x;
            command.g = g_overlayLayer.inkColor.y;
            command.b = g_overlayLayer.inkColor.z;
            command.a = g_overlayLayer.inkColor.w;
            command.params.circle.x = x;
            command.params.circle.y = y;
            command.params.circle.radius = radius;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_fill_circle(float x, float y, float radius) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_FILL_CIRCLE;
            command.r = g_overlayLayer.inkColor.x;
            command.g = g_overlayLayer.inkColor.y;
            command.b = g_overlayLayer.inkColor.z;
            command.a = g_overlayLayer.inkColor.w;
            command.params.circle.x = x;
            command.params.circle.y = y;
            command.params.circle.radius = radius;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_draw_text(float x, float y, const char* text, float fontSize) {
        // NSLog(@"overlay_graphics_layer_draw_text: called with text='%s' at (%.1f,%.1f) fontSize=%.1f", text ? text : "NULL", x, y, fontSize);
        if (g_overlayLayer && text) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_DRAW_TEXT;
            command.r = g_overlayLayer.inkColor.x;
            command.g = g_overlayLayer.inkColor.y;
            command.b = g_overlayLayer.inkColor.z;
            command.a = g_overlayLayer.inkColor.w;
            command.params.drawText.x = x;
            command.params.drawText.y = y;
            command.params.drawText.fontSize = fontSize;
            strncpy(command.params.drawText.text, text, sizeof(command.params.drawText.text) - 1);
            command.params.drawText.text[sizeof(command.params.drawText.text) - 1] = '\0';
            // NSLog(@"overlay_graphics_layer_draw_text: Calling queueCommand with ink color (%.2f,%.2f,%.2f,%.2f)",
            //       command.r, command.g, command.b, command.a);
            [g_overlayLayer queueCommand:command];
        } else {
            NSLog(@"overlay_graphics_layer_draw_text: ERROR - g_overlayLayer=%p text=%p", g_overlayLayer, text);
        }
    }

    void overlay_graphics_layer_render(void* encoder, float width, float height) {
        // Check for emergency shutdown first
        if (is_emergency_shutdown_requested()) {
            NSLog(@"OverlayGraphicsLayer: Emergency shutdown detected, terminating render");
            overlay_graphics_layer_shutdown();
            return;
        }

        if (g_overlayLayer) {
            id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
            [g_overlayLayer renderWithEncoder:metalEncoder viewport:CGSizeMake(width, height)];
        }
    }

    void overlay_graphics_layer_swap() {
        // NSLog(@"C API: overlay_graphics_layer_swap called on thread %@", [NSThread currentThread]);
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_SWAP;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_cleanup() {
        NSLog(@"OverlayGraphicsLayer: cleanup called");
        overlay_graphics_layer_shutdown();
    }

    void overlay_graphics_layer_present(void) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            command.type = GRAPHICS_CMD_SWAP;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_render_overlay(void* encoder, void* projectionMatrix) {
        // Check for emergency shutdown first
        if (is_emergency_shutdown_requested()) {
            NSLog(@"OverlayGraphicsLayer: Emergency shutdown detected, skipping overlay render");
            overlay_graphics_layer_shutdown();
            return;
        }

        if (g_overlayLayer && encoder && g_overlayVisible) {
            id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
            [g_overlayLayer renderWithEncoder:metalEncoder viewport:CGSizeMake(1024.0f, 768.0f)];
        }
    }

    void overlay_graphics_layer_show(void) {
        NSLog(@"OverlayGraphicsLayer: show called - overlay visible");
        g_overlayVisible = true;
    }

    void overlay_graphics_layer_hide(void) {
        NSLog(@"OverlayGraphicsLayer: hide called - overlay hidden");
        g_overlayVisible = false;
    }

    bool overlay_graphics_layer_is_visible(void) {
        return g_overlayVisible;
    }

    void overlay_graphics_layer_set_ink(float r, float g, float b, float a) {
        // NSLog(@"overlay_graphics_layer_set_ink: RGBA(%.2f, %.2f, %.2f, %.2f)", r, g, b, a);
        if (g_overlayLayer) {
            g_overlayLayer.inkColor = simd_make_float4(r, g, b, a);
        }
    }

    void overlay_graphics_layer_set_paper(float r, float g, float b, float a) {
        NSLog(@"overlay_graphics_layer_set_paper: RGBA(%.2f, %.2f, %.2f, %.2f)", r, g, b, a);
        if (g_overlayLayer) {
            g_overlayLayer.paperColor = simd_make_float4(r, g, b, a);
        }
    }





    void overlay_graphics_layer_draw_linear_gradient(float x1, float y1, float x2, float y2, float r1, float g1, float b1, float a1, float r2, float g2, float b2, float a2) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_LINEAR_GRADIENT;
            command.r = r1;
            command.g = g1;
            command.b = b1;
            command.a = a1;
            command.r2 = r2;
            command.g2 = g2;
            command.b2 = b2;
            command.a2 = a2;
            command.params.linearGradient.x1 = x1;
            command.params.linearGradient.y1 = y1;
            command.params.linearGradient.x2 = x2;
            command.params.linearGradient.y2 = y2;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_fill_linear_gradient_rect(float x, float y, float w, float h, int direction, float r1, float g1, float b1, float a1, float r2, float g2, float b2, float a2) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_LINEAR_GRADIENT_RECT;
            command.r = r1;
            command.g = g1;
            command.b = b1;
            command.a = a1;
            command.r2 = r2;
            command.g2 = g2;
            command.b2 = b2;
            command.a2 = a2;
            command.params.linearGradientRect.x = x;
            command.params.linearGradientRect.y = y;
            command.params.linearGradientRect.w = w;
            command.params.linearGradientRect.h = h;
            command.params.linearGradientRect.direction = direction;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_draw_radial_gradient(float centerX, float centerY, float radius, float r1, float g1, float b1, float a1, float r2, float g2, float b2, float a2) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_RADIAL_GRADIENT;
            command.r = r1;
            command.g = g1;
            command.b = b1;
            command.a = a1;
            command.r2 = r2;
            command.g2 = g2;
            command.b2 = b2;
            command.a2 = a2;
            command.params.radialGradient.centerX = centerX;
            command.params.radialGradient.centerY = centerY;
            command.params.radialGradient.radius = radius;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_fill_radial_gradient_circle(float centerX, float centerY, float radius, float r1, float g1, float b1, float a1, float r2, float g2, float b2, float a2) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_RADIAL_GRADIENT_CIRCLE;
            command.r = r1;
            command.g = g1;
            command.b = b1;
            command.a = a1;
            command.r2 = r2;
            command.g2 = g2;
            command.b2 = b2;
            command.a2 = a2;
            command.params.radialGradient.centerX = centerX;
            command.params.radialGradient.centerY = centerY;
            command.params.radialGradient.radius = radius;
            [g_overlayLayer queueCommand:command];
        }
    }

    bool overlay_graphics_layer_load_image(uint16_t imageId, const char* filename) {
        if (!g_overlayLayer) {
            NSLog(@"overlay_graphics_layer_load_image: No working layer");
            return false;
        }

        NSString* path = [NSString stringWithUTF8String:filename];
        NSData* data = [NSData dataWithContentsOfFile:path];
        if (!data) {
            NSLog(@"overlay_graphics_layer_load_image: Failed to load file %s", filename);
            return false;
        }

        sk_sp<SkData> skData = SkData::MakeWithoutCopy([data bytes], [data length]);
        sk_sp<SkImage> image = SkImages::DeferredFromEncodedData(skData);
        if (!image) {
            NSLog(@"overlay_graphics_layer_load_image: Failed to decode image %s", filename);
            return false;
        }

        NSNumber* key = @(imageId);
        // Store in global map to keep sk_sp alive, and pointer in dictionary for lookup
        g_globalImages[imageId] = image;
        NSValue* imageValue = [NSValue valueWithPointer:image.get()];
        g_overlayLayer.loadedImages[key] = imageValue;

        NSLog(@"overlay_graphics_layer_load_image: Loaded image %d (%s) size %dx%d",
              imageId, filename, image->width(), image->height());
        return true;
    }

    void overlay_graphics_layer_draw_image(uint16_t imageId, float x, float y) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_DRAW_IMAGE;
            command.params.drawImage.imageId = imageId;
            command.params.drawImage.x = x;
            command.params.drawImage.y = y;
            [g_overlayLayer queueCommand:command];
        }
    }

    bool overlay_graphics_layer_create_image(uint16_t imageId, int width, int height, float r, float g, float b, float a) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_CREATE_IMAGE;
            command.r = r;
            command.g = g;
            command.b = b;
            command.a = a;
            command.params.createImage.imageId = imageId;
            command.params.createImage.width = width;
            command.params.createImage.height = height;
            [g_overlayLayer queueCommand:command];
            return true;
        }
        return false;
    }

    bool overlay_graphics_layer_save_image(uint16_t imageId, const char* filename) {
        if (g_overlayLayer && filename) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_SAVE_IMAGE;
            command.params.saveImage.imageId = imageId;
            strncpy(command.params.saveImage.filename, filename, 255);
            command.params.saveImage.filename[255] = '\0';
            [g_overlayLayer queueCommand:command];
            return true;
        }
        return false;
    }

    bool overlay_graphics_layer_capture_screen(uint16_t imageId, int x, int y, int width, int height) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_CAPTURE_SCREEN;
            command.params.captureScreen.imageId = imageId;
            command.params.captureScreen.x = x;
            command.params.captureScreen.y = y;
            command.params.captureScreen.width = width;
            command.params.captureScreen.height = height;
            [g_overlayLayer queueCommand:command];
            return true;
        }
        return false;
    }

    bool overlay_graphics_layer_get_image_size(uint16_t imageId, int* width, int* height) {
        if (g_overlayLayer && width && height) {
            auto it = g_globalImages.find(imageId);
            if (it != g_globalImages.end()) {
                sk_sp<SkImage> image = it->second;
                *width = image->width();
                *height = image->height();
                return true;
            }
        }
        return false;
    }

    void overlay_graphics_layer_draw_image_scaled(uint16_t imageId, float x, float y, float scale) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_DRAW_IMAGE_SCALED;
            command.params.drawImageScaled.imageId = imageId;
            command.params.drawImageScaled.x = x;
            command.params.drawImageScaled.y = y;
            command.params.drawImageScaled.scale = scale;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_draw_image_rect(uint16_t imageId, float srcX, float srcY, float srcW, float srcH,
                                               float dstX, float dstY, float dstW, float dstH) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_DRAW_IMAGE_RECT;
            command.params.drawImageRect.imageId = imageId;
            command.params.drawImageRect.srcX = srcX;
            command.params.drawImageRect.srcY = srcY;
            command.params.drawImageRect.srcW = srcW;
            command.params.drawImageRect.srcH = srcH;
            command.params.drawImageRect.dstX = dstX;
            command.params.drawImageRect.dstY = dstY;
            command.params.drawImageRect.dstW = dstW;
            command.params.drawImageRect.dstH = dstH;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_set_blend_mode(int blendMode) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_SET_BLEND_MODE;
            command.params.setBlendMode.blendMode = blendMode;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_set_blur_filter(float radius) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_SET_BLUR_FILTER;
            command.params.setBlurFilter.radius = radius;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_set_drop_shadow(float dx, float dy, float blur, float r, float g, float b, float a) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_SET_DROP_SHADOW;
            command.params.setDropShadow.dx = dx;
            command.params.setDropShadow.dy = dy;
            command.params.setDropShadow.blur = blur;
            command.params.setDropShadow.r = r;
            command.params.setDropShadow.g = g;
            command.params.setDropShadow.b = b;
            command.params.setDropShadow.a = a;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_set_color_matrix(const float matrix[20]) {
        if (g_overlayLayer && matrix) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_SET_COLOR_MATRIX;
            memcpy(command.params.setColorMatrix.matrix, matrix, sizeof(float) * 20);
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_clear_filters() {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_CLEAR_FILTERS;
            [g_overlayLayer queueCommand:command];
        }
    }

    // Pixel access functions - these need to be synchronous
    bool overlay_graphics_layer_read_pixels(uint16_t imageId, int x, int y, int width, int height, uint8_t** outPixels) {
        if (!g_overlayLayer || !outPixels) return false;

        if (imageId == 0) {
            // Read from screen (back buffer)
            if (g_overlayLayer.backCanvas) {
                SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
                size_t rowBytes = width * 4;
                uint8_t* pixels = (uint8_t*)malloc(height * rowBytes);
                if (pixels) {
                    if (g_overlayLayer.backCanvas->readPixels(info, pixels, rowBytes, x, y)) {
                        *outPixels = pixels;
                        return true;
                    } else {
                        free(pixels);
                    }
                }
            }
        } else {
            // Read from image
            auto it = g_globalImages.find(imageId);
            if (it != g_globalImages.end()) {
                sk_sp<SkImage> image = it->second;
                SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
                size_t rowBytes = width * 4;
                uint8_t* pixels = (uint8_t*)malloc(height * rowBytes);
                if (pixels) {
                    if (image->readPixels(nullptr, info, pixels, rowBytes, x, y)) {
                        *outPixels = pixels;
                        return true;
                    } else {
                        free(pixels);
                    }
                }
            }
        }
        return false;
    }

    bool overlay_graphics_layer_write_pixels(uint16_t imageId, int x, int y, int width, int height, const uint8_t* pixels) {
        if (!g_overlayLayer || !pixels) return false;

        if (imageId == 0) {
            // Write to screen (back buffer) - queue command for thread safety
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_WRITE_PIXELS;
            command.params.writePixels.imageId = imageId;
            command.params.writePixels.x = x;
            command.params.writePixels.y = y;
            command.params.writePixels.width = width;
            command.params.writePixels.height = height;
            command.params.writePixels.dataSize = width * height * 4;
            command.params.writePixels.pixelData = (uint8_t*)malloc(command.params.writePixels.dataSize);
            if (command.params.writePixels.pixelData) {
                memcpy(command.params.writePixels.pixelData, pixels, command.params.writePixels.dataSize);
                [g_overlayLayer queueCommand:command];
                return true;
            }
        } else {
            // Write to image - create new image with pixel data
            SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
            size_t rowBytes = width * 4;
            sk_sp<SkData> data = SkData::MakeWithCopy(pixels, height * rowBytes);
            sk_sp<SkImage> newImage = SkImages::RasterFromData(info, data, rowBytes);
            if (newImage) {
                g_globalImages[imageId] = newImage;
                return true;
            }
        }
        return false;
    }

    void overlay_graphics_layer_free_pixels(uint8_t* pixels) {
        if (pixels) {
            free(pixels);
        }
    }

    // Matrix transformation functions
    void overlay_graphics_layer_push_matrix() {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_PUSH_MATRIX;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_pop_matrix() {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_POP_MATRIX;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_translate(float tx, float ty) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_TRANSLATE;
            command.params.translate.tx = tx;
            command.params.translate.ty = ty;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_rotate(float degrees) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_ROTATE;
            command.params.rotate.degrees = degrees;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_scale(float sx, float sy) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_SCALE;
            command.params.scale.sx = sx;
            command.params.scale.sy = sy;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_skew(float kx, float ky) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_SKEW;
            command.params.skew.kx = kx;
            command.params.skew.ky = ky;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_reset_matrix() {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_RESET_MATRIX;
            [g_overlayLayer queueCommand:command];
        }
    }

    // Path operations functions
    void overlay_graphics_layer_create_path(uint16_t pathId) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_CREATE_PATH;
            command.params.createPath.pathId = pathId;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_path_move_to(uint16_t pathId, float x, float y) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_PATH_MOVE_TO;
            command.params.pathMoveTo.pathId = pathId;
            command.params.pathMoveTo.x = x;
            command.params.pathMoveTo.y = y;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_path_line_to(uint16_t pathId, float x, float y) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_PATH_LINE_TO;
            command.params.pathLineTo.pathId = pathId;
            command.params.pathLineTo.x = x;
            command.params.pathLineTo.y = y;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_path_curve_to(uint16_t pathId, float x1, float y1, float x2, float y2, float x3, float y3) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_PATH_CURVE_TO;
            command.params.pathCurveTo.pathId = pathId;
            command.params.pathCurveTo.x1 = x1;
            command.params.pathCurveTo.y1 = y1;
            command.params.pathCurveTo.x2 = x2;
            command.params.pathCurveTo.y2 = y2;
            command.params.pathCurveTo.x3 = x3;
            command.params.pathCurveTo.y3 = y3;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_path_close(uint16_t pathId) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_PATH_CLOSE;
            command.params.pathClose.pathId = pathId;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_draw_path(uint16_t pathId) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_DRAW_PATH;
            command.params.drawPath.pathId = pathId;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_fill_path(uint16_t pathId) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_FILL_PATH;
            command.params.fillPath.pathId = pathId;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_clip_path(uint16_t pathId, int clipOp, bool antiAlias) {
        if (g_overlayLayer) {
            GraphicsCommand command;
            memset(&command, 0, sizeof(command));
            command.type = GRAPHICS_CMD_CLIP_PATH;
            command.params.clipPath.pathId = pathId;
            command.params.clipPath.clipOp = clipOp;
            command.params.clipPath.antiAlias = antiAlias;
            [g_overlayLayer queueCommand:command];
        }
    }

    void overlay_graphics_layer_wait_queue_empty() {
        if (g_overlayLayer) {
            [g_overlayLayer waitQueueEmpty];
        }
    }



    // Create sprite from current Skia canvas content
    bool overlay_graphics_layer_create_sprite_from_canvas(uint16_t spriteId, int width, int height) {
        if (!g_overlayLayer) return false;

        // Get current canvas content
        SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);

        // Get pixels from current canvas region
        size_t rowBytes = width * 4;
        uint8_t* pixels = (uint8_t*)malloc(height * rowBytes);
        if (!pixels) return false;

        if ([g_overlayLayer getCanvas]->readPixels(info, pixels, rowBytes, 0, 0)) {
            // Create sprite from pixels

            bool success = sprite_create_from_pixels(spriteId, pixels, width, height);
            free(pixels);
            return success;
        }

        free(pixels);
        return false;
    }

    // Immediate sprite rendering approach
    bool overlay_graphics_layer_begin_sprite_render(uint16_t spriteId, int width, int height) {
        NSLog(@"DEBUG: sprite_begin_render called - spriteId=%d, size=%dx%d", spriteId, width, height);

        if (!g_overlayLayer) {
            NSLog(@"DEBUG: sprite_begin_render FAILED - no working layer");
            return false;
        }
        if (g_creatingSprite) {
            NSLog(@"DEBUG: sprite_begin_render FAILED - already creating sprite %d", g_pendingSpriteId);
            return false;
        }

        // Create temporary Skia surface immediately
        SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        g_tempSkiaSurface = SkSurfaces::Raster(info);
        if (!g_tempSkiaSurface) {
            NSLog(@"DEBUG: sprite_begin_render FAILED - could not create Skia surface");
            return false;
        }

        // Queue BEGIN_SPRITE_REDIRECT command
        GraphicsCommand command;
        memset(&command, 0, sizeof(command));
        command.type = GRAPHICS_CMD_BEGIN_SPRITE_REDIRECT;
        command.params.beginSpriteRedirect.spriteId = spriteId;
        command.params.beginSpriteRedirect.width = width;
        command.params.beginSpriteRedirect.height = height;
        [g_overlayLayer queueCommand:command];

        NSLog(@"DEBUG: Queued BEGIN_SPRITE_REDIRECT command for sprite %d", spriteId);
        return true;
    }

    // Queue sprite end redirect command
    bool overlay_graphics_layer_end_sprite_render(uint16_t spriteId) {
        NSLog(@"DEBUG: sprite_end_render called - spriteId=%d", spriteId);

        if (!g_overlayLayer) {
            NSLog(@"DEBUG: sprite_end_render FAILED - no working layer");
            return false;
        }

        // Queue END_SPRITE_REDIRECT command
        GraphicsCommand command;
        memset(&command, 0, sizeof(command));
        command.type = GRAPHICS_CMD_END_SPRITE_REDIRECT;
        command.params.endSpriteRedirect.spriteId = spriteId;
        [g_overlayLayer queueCommand:command];

        NSLog(@"DEBUG: Queued END_SPRITE_REDIRECT command for sprite %d", spriteId);

        // Process the command queue to execute the drawing commands
        [g_overlayLayer processCommandQueue];
        NSLog(@"DEBUG: Processed command queue for sprite %d", spriteId);

        return true;
    }

    // Check if currently creating a sprite
    bool overlay_graphics_layer_is_sprite_rendering() {
        return g_creatingSprite;
    }

    // Tile rendering functions
    bool overlay_graphics_layer_begin_tile_render(uint16_t tileId, int width, int height) {
        NSLog(@"DEBUG: tile_begin_render called - tileId=%d, size=%dx%d", tileId, width, height);

        if (!g_overlayLayer) {
            NSLog(@"DEBUG: tile_begin_render FAILED - no working layer");
            return false;
        }
        if (g_creatingTile) {
            NSLog(@"DEBUG: tile_begin_render FAILED - already creating tile %d", g_pendingTileId);
            return false;
        }

        // Create temporary Skia surface immediately
        SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        g_tempTileSkiaSurface = SkSurfaces::Raster(info);
        if (!g_tempTileSkiaSurface) {
            NSLog(@"DEBUG: tile_begin_render FAILED - could not create Skia surface");
            return false;
        }

        // Queue BEGIN_TILE_REDIRECT command
        GraphicsCommand command;
        memset(&command, 0, sizeof(command));
        command.type = GRAPHICS_CMD_BEGIN_TILE_REDIRECT;
        command.params.beginTileRedirect.tileId = tileId;
        command.params.beginTileRedirect.width = width;
        command.params.beginTileRedirect.height = height;
        [g_overlayLayer queueCommand:command];

        NSLog(@"DEBUG: Queued BEGIN_TILE_REDIRECT command for tile %d", tileId);
        return true;
    }

    // Queue tile end redirect command
    bool overlay_graphics_layer_end_tile_render(uint16_t tileId) {
        NSLog(@"DEBUG: tile_end_render called - tileId=%d", tileId);

        if (!g_overlayLayer) {
            NSLog(@"DEBUG: tile_end_render FAILED - no working layer");
            return false;
        }

        // Queue END_TILE_REDIRECT command
        GraphicsCommand command;
        memset(&command, 0, sizeof(command));
        command.type = GRAPHICS_CMD_END_TILE_REDIRECT;
        command.params.endTileRedirect.tileId = tileId;
        [g_overlayLayer queueCommand:command];

        NSLog(@"DEBUG: Queued END_TILE_REDIRECT command for tile %d", tileId);
        return true;
    }

    // Check if currently creating a tile
    bool overlay_graphics_layer_is_tile_rendering() {
        return g_creatingTile;
    }

    // UI element rendering functions


    // Check if currently creating a UI element
    bool overlay_graphics_layer_is_ui_rendering() {
        return g_creatingUiElement;
    }
}
