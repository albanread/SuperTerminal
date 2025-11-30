//
//  SimpleGraphicsLayer.mm
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <simd/simd.h>

@interface SimpleGraphicsLayer : NSObject

@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property (nonatomic, strong) id<MTLBuffer> vertexBuffer;
@property (nonatomic, assign) CGSize canvasSize;

// Drawing state
@property (nonatomic, assign) simd_float4 currentColor;
@property (nonatomic, assign) float lineWidth;

// Vertex data for drawing primitives
@property (nonatomic, strong) NSMutableData* vertexData;
@property (nonatomic, assign) NSUInteger vertexCount;

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

// Vertex structure for simple graphics
struct SimpleVertex {
    simd_float2 position;
    simd_float4 color;
};

@implementation SimpleGraphicsLayer

- (instancetype)initWithDevice:(id<MTLDevice>)device canvasSize:(CGSize)size {
    self = [super init];
    if (self) {
        NSLog(@"SimpleGraphicsLayer: Initializing with device %p, size %.0fx%.0f", device, size.width, size.height);

        self.device = device;
        self.canvasSize = size;
        self.currentColor = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f); // White
        self.lineWidth = 1.0f;

        // Initialize vertex data storage
        self.vertexData = [[NSMutableData alloc] init];
        self.vertexCount = 0;

        [self createRenderPipeline];
        [self createVertexBuffer];

        NSLog(@"SimpleGraphicsLayer: Initialization complete");
    }
    return self;
}

- (void)createRenderPipeline {
    NSError* error = nil;

    NSLog(@"SimpleGraphicsLayer: Creating render pipeline...");

    // Simple vertex and fragment shaders for colored primitives
    NSString* shaderSource = @""
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct VertexIn {\n"
    "    float2 position [[attribute(0)]];\n"
    "    float4 color [[attribute(1)]];\n"
    "};\n"
    "\n"
    "struct VertexOut {\n"
    "    float4 position [[position]];\n"
    "    float4 color;\n"
    "};\n"
    "\n"
    "vertex VertexOut simple_graphics_vertex(VertexIn in [[stage_in]],\n"
    "                                        constant float2& viewportSize [[buffer(1)]]) {\n"
    "    VertexOut out;\n"
    "    // Convert from pixel coordinates to NDC\n"
    "    float2 pixelSpacePosition = in.position.xy;\n"
    "    float2 viewportSize2 = viewportSize;\n"
    "    float2 ndc = (pixelSpacePosition / (viewportSize2 / 2.0)) - 1.0;\n"
    "    ndc.y = -ndc.y; // Flip Y coordinate\n"
    "    out.position = float4(ndc, 0.0, 1.0);\n"
    "    out.color = in.color;\n"
    "    return out;\n"
    "}\n"
    "\n"
    "fragment float4 simple_graphics_fragment(VertexOut in [[stage_in]]) {\n"
    "    return in.color;\n"
    "}\n";

    id<MTLLibrary> library = [self.device newLibraryWithSource:shaderSource options:nil error:&error];
    if (!library) {
        NSLog(@"SimpleGraphicsLayer: ERROR - Failed to create shader library: %@", error.localizedDescription);
        return;
    }

    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"simple_graphics_vertex"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"simple_graphics_fragment"];

    if (!vertexFunction || !fragmentFunction) {
        NSLog(@"SimpleGraphicsLayer: ERROR - Failed to find shader functions");
        return;
    }

    // Create render pipeline descriptor
    MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.label = @"Simple Graphics Pipeline";
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

    // Color
    vertexDescriptor.attributes[1].format = MTLVertexFormatFloat4;
    vertexDescriptor.attributes[1].offset = sizeof(simd_float2);
    vertexDescriptor.attributes[1].bufferIndex = 0;

    // Layout
    vertexDescriptor.layouts[0].stride = sizeof(struct SimpleVertex);
    vertexDescriptor.layouts[0].stepRate = 1;
    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    pipelineDescriptor.vertexDescriptor = vertexDescriptor;

    // Create pipeline state
    self.pipelineState = [self.device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (!self.pipelineState) {
        NSLog(@"SimpleGraphicsLayer: ERROR - Failed to create pipeline state: %@", error.localizedDescription);
        return;
    }

    NSLog(@"SimpleGraphicsLayer: Render pipeline created successfully");
}

- (void)createVertexBuffer {
    // Create a buffer large enough for many vertices
    NSUInteger maxVertices = 10000;
    NSUInteger bufferSize = maxVertices * sizeof(struct SimpleVertex);

    self.vertexBuffer = [self.device newBufferWithLength:bufferSize options:MTLResourceStorageModeShared];
    if (!self.vertexBuffer) {
        NSLog(@"SimpleGraphicsLayer: ERROR - Failed to create vertex buffer");
        return;
    }

    self.vertexBuffer.label = @"Simple Graphics Vertex Buffer";
    NSLog(@"SimpleGraphicsLayer: Vertex buffer created: %zu bytes", bufferSize);
}

- (void)clear {
    // Clear all vertex data
    [self.vertexData setLength:0];
    self.vertexCount = 0;
}

- (void)setColor:(simd_float4)color {
    self.currentColor = color;
}

- (void)setLineWidth:(float)width {
    self.lineWidth = width;
}

- (void)addVertex:(simd_float2)position color:(simd_float4)color {
    struct SimpleVertex vertex;
    vertex.position = position;
    vertex.color = color;

    [self.vertexData appendBytes:&vertex length:sizeof(vertex)];
    self.vertexCount++;
}

- (void)drawLineFromX:(float)x1 y:(float)y1 toX:(float)x2 y:(float)y2 {
    // Simple line drawing using two vertices
    [self addVertex:simd_make_float2(x1, y1) color:self.currentColor];
    [self addVertex:simd_make_float2(x2, y2) color:self.currentColor];
}

- (void)drawRectX:(float)x y:(float)y width:(float)w height:(float)h {
    // Draw rectangle outline using line segments
    [self drawLineFromX:x y:y toX:x + w y:y];           // Top
    [self drawLineFromX:x + w y:y toX:x + w y:y + h];   // Right
    [self drawLineFromX:x + w y:y + h toX:x y:y + h];   // Bottom
    [self drawLineFromX:x y:y + h toX:x y:y];           // Left
}

- (void)fillRectX:(float)x y:(float)y width:(float)w height:(float)h {
    // Fill rectangle using two triangles
    // Triangle 1
    [self addVertex:simd_make_float2(x, y) color:self.currentColor];         // Top left
    [self addVertex:simd_make_float2(x + w, y) color:self.currentColor];     // Top right
    [self addVertex:simd_make_float2(x, y + h) color:self.currentColor];     // Bottom left

    // Triangle 2
    [self addVertex:simd_make_float2(x + w, y) color:self.currentColor];     // Top right
    [self addVertex:simd_make_float2(x + w, y + h) color:self.currentColor]; // Bottom right
    [self addVertex:simd_make_float2(x, y + h) color:self.currentColor];     // Bottom left
}

- (void)drawCircleX:(float)x y:(float)y radius:(float)radius {
    // Draw circle outline using line segments
    int segments = 32;
    float angleStep = 2.0f * M_PI / segments;

    for (int i = 0; i < segments; i++) {
        float angle1 = i * angleStep;
        float angle2 = (i + 1) * angleStep;

        float x1 = x + radius * cosf(angle1);
        float y1 = y + radius * sinf(angle1);
        float x2 = x + radius * cosf(angle2);
        float y2 = y + radius * sinf(angle2);

        [self drawLineFromX:x1 y:y1 toX:x2 y:y2];
    }
}

- (void)fillCircleX:(float)x y:(float)y radius:(float)radius {
    // Fill circle using triangular segments from center
    int segments = 32;
    float angleStep = 2.0f * M_PI / segments;

    for (int i = 0; i < segments; i++) {
        float angle1 = i * angleStep;
        float angle2 = (i + 1) * angleStep;

        float x1 = x + radius * cosf(angle1);
        float y1 = y + radius * sinf(angle1);
        float x2 = x + radius * cosf(angle2);
        float y2 = y + radius * sinf(angle2);

        // Triangle from center to edge
        [self addVertex:simd_make_float2(x, y) color:self.currentColor];     // Center
        [self addVertex:simd_make_float2(x1, y1) color:self.currentColor];   // Point 1
        [self addVertex:simd_make_float2(x2, y2) color:self.currentColor];   // Point 2
    }
}

- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport {
    if (!self.pipelineState || !self.vertexBuffer || self.vertexCount == 0) {
        return;
    }

    // Copy vertex data to buffer
    void* bufferPointer = [self.vertexBuffer contents];
    memcpy(bufferPointer, [self.vertexData bytes], [self.vertexData length]);

    [encoder setRenderPipelineState:self.pipelineState];
    [encoder setVertexBuffer:self.vertexBuffer offset:0 atIndex:0];

    // Set viewport size
    simd_float2 viewportSize = simd_make_float2(viewport.width, viewport.height);
    [encoder setVertexBytes:&viewportSize length:sizeof(viewportSize) atIndex:1];

    // Draw primitives
    // Most primitives are lines, but filled shapes use triangles
    // For simplicity, draw everything as individual lines or triangles

    NSUInteger remainingVertices = self.vertexCount;
    NSUInteger offset = 0;

    // Draw in batches (lines are pairs, triangles are triplets)
    while (remainingVertices >= 3) {
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:offset vertexCount:3];
        remainingVertices -= 3;
        offset += 3;
    }
    while (remainingVertices >= 2) {
        [encoder drawPrimitives:MTLPrimitiveTypeLine vertexStart:offset vertexCount:2];
        remainingVertices -= 2;
        offset += 2;
    }
}

@end

// C Interface - Global graphics layer instance
static SimpleGraphicsLayer* g_simpleGraphicsLayer = nil;

extern "C" {
    void simple_graphics_layer_init(void* device, float width, float height) {
        @autoreleasepool {
            id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
            CGSize canvasSize = CGSizeMake(width, height);
            g_simpleGraphicsLayer = [[SimpleGraphicsLayer alloc] initWithDevice:metalDevice canvasSize:canvasSize];
            NSLog(@"Simple graphics layer initialized: %.0fx%.0f", width, height);
        }
    }

    void simple_graphics_layer_clear() {
        @autoreleasepool {
            if (g_simpleGraphicsLayer) {
                [g_simpleGraphicsLayer clear];
            }
        }
    }

    void simple_graphics_layer_set_color(float r, float g, float b, float a) {
        @autoreleasepool {
            if (g_simpleGraphicsLayer) {
                simd_float4 color = simd_make_float4(r, g, b, a);
                [g_simpleGraphicsLayer setColor:color];
            }
        }
    }

    void simple_graphics_layer_set_line_width(float width) {
        @autoreleasepool {
            if (g_simpleGraphicsLayer) {
                [g_simpleGraphicsLayer setLineWidth:width];
            }
        }
    }

    void simple_graphics_layer_draw_line(float x1, float y1, float x2, float y2) {
        @autoreleasepool {
            if (g_simpleGraphicsLayer) {
                [g_simpleGraphicsLayer drawLineFromX:x1 y:y1 toX:x2 y:y2];
            }
        }
    }

    void simple_graphics_layer_draw_rect(float x, float y, float w, float h) {
        @autoreleasepool {
            if (g_simpleGraphicsLayer) {
                [g_simpleGraphicsLayer drawRectX:x y:y width:w height:h];
            }
        }
    }

    void simple_graphics_layer_fill_rect(float x, float y, float w, float h) {
        @autoreleasepool {
            if (g_simpleGraphicsLayer) {
                [g_simpleGraphicsLayer fillRectX:x y:y width:w height:h];
            }
        }
    }

    void simple_graphics_layer_draw_circle(float x, float y, float radius) {
        @autoreleasepool {
            if (g_simpleGraphicsLayer) {
                [g_simpleGraphicsLayer drawCircleX:x y:y radius:radius];
            }
        }
    }

    void simple_graphics_layer_fill_circle(float x, float y, float radius) {
        @autoreleasepool {
            if (g_simpleGraphicsLayer) {
                [g_simpleGraphicsLayer fillCircleX:x y:y radius:radius];
            }
        }
    }

    void simple_graphics_layer_render(void* encoder, float width, float height) {
        @autoreleasepool {
            if (g_simpleGraphicsLayer) {
                id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
                [g_simpleGraphicsLayer renderWithEncoder:metalEncoder viewport:CGSizeMake(width, height)];
            }
        }
    }

    void simple_graphics_layer_cleanup() {
        @autoreleasepool {
            g_simpleGraphicsLayer = nil;
            NSLog(@"Simple graphics layer cleaned up");
        }
    }
}
