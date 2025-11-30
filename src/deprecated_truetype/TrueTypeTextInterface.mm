//
//  TrueTypeTextInterface.mm
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <simd/simd.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "include/stb_truetype.h"
#include "TextCommon.h"

// Global font overdraw state
static bool g_font_overdraw_enabled = true; // Default to enabled (current behavior)

// External function to check REPL mode state
extern "C" bool editor_is_repl_mode(void);
extern "C" bool editor_is_active(void);

@interface TrueTypeTextLayer : NSObject

@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property (nonatomic, strong) id<MTLBuffer> vertexBuffer;
@property (nonatomic, strong) id<MTLTexture> fontTexture;
@property (nonatomic, assign) struct TextCell* textGrid;
@property (nonatomic, assign) int cursorX;
@property (nonatomic, assign) int cursorY;
@property (nonatomic, assign) simd_float4 currentInk;
@property (nonatomic, assign) simd_float4 currentPaper;
@property (nonatomic, assign) BOOL isEditorLayer;

// TrueType font data
@property (nonatomic, assign) stbtt_fontinfo fontInfo;
@property (nonatomic, assign) unsigned char* fontData;
@property (nonatomic, assign) float fontSize;
@property (nonatomic, assign) float fontScale;
@property (nonatomic, assign) int fontAscent;
@property (nonatomic, assign) int fontDescent;
@property (nonatomic, assign) int fontLineGap;

// Font atlas
@property (nonatomic, assign) int atlasWidth;
@property (nonatomic, assign) int atlasHeight;
@property (nonatomic, assign) stbtt_bakedchar* bakedChars;

- (instancetype)initWithDevice:(id<MTLDevice>)device;
- (instancetype)initWithDevice:(id<MTLDevice>)device isEditorLayer:(BOOL)isEditor;
- (BOOL)loadFont:(NSString*)fontPath fontSize:(float)size;
- (void)createFontAtlas;
- (void)createRenderPipeline;
- (void)print:(NSString*)text;
- (void)printAt:(int)x y:(int)y text:(NSString*)text;
- (void)clear;
- (void)home;
- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport;
- (void)setColor:(simd_float4)ink paper:(simd_float4)paper;
- (void)setInk:(simd_float4)ink;
- (void)setPaper:(simd_float4)paper;

@end

@implementation TrueTypeTextLayer

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    return [self initWithDevice:device isEditorLayer:NO];
}

- (instancetype)initWithDevice:(id<MTLDevice>)device isEditorLayer:(BOOL)isEditor {
    self = [super init];
    if (self) {
        self.device = device;
        self.isEditorLayer = isEditor;

        // Initialize text grid
        self.textGrid = (struct TextCell*)calloc(GRID_WIDTH * GRID_HEIGHT, sizeof(struct TextCell));

        // Initialize cursor and colors based on layer type
        self.cursorX = 0;
        self.cursorY = 0;

        if (isEditor) {
            // Editor layer (Layer 6): White text on solid blue background
            self.currentInk = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);    // White
            self.currentPaper = simd_make_float4(0.0f, 0.0f, 1.0f, 1.0f);  // Solid blue
        } else {
            // Terminal layer (Layer 5): White text on transparent background
            self.currentInk = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);    // White
            self.currentPaper = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f);  // Transparent
        }

        // Default font settings
        self.fontSize = 16.0f;
        self.atlasWidth = 512;
        self.atlasHeight = 512;

        [self clear];
    }
    return self;
}

- (void)dealloc {
    if (self.textGrid) {
        free(self.textGrid);
    }
    if (self.fontData) {
        free(self.fontData);
    }
    if (self.bakedChars) {
        free(self.bakedChars);
    }
    [super dealloc];
}

- (BOOL)loadFont:(NSString*)fontPath fontSize:(float)size {
    self.fontSize = size;

    // Try to load the font file
    NSData* fontFileData = [NSData dataWithContentsOfFile:fontPath];
    if (!fontFileData) {
        NSLog(@"Could not load font from %@", fontPath);
        return NO;
    }

    // Copy font data
    size_t dataSize = [fontFileData length];
    self.fontData = (unsigned char*)malloc(dataSize);
    memcpy(self.fontData, [fontFileData bytes], dataSize);

    // Initialize font info
    if (!stbtt_InitFont(&_fontInfo, self.fontData, 0)) {
        NSLog(@"Failed to initialize font");
        return NO;
    }

    // Calculate font metrics
    self.fontScale = stbtt_ScaleForPixelHeight(&_fontInfo, size);
    stbtt_GetFontVMetrics(&_fontInfo, &_fontAscent, &_fontDescent, &_fontLineGap);

    NSLog(@"Font loaded: scale=%.3f, ascent=%d, descent=%d, lineGap=%d",
          self.fontScale, self.fontAscent, self.fontDescent, self.fontLineGap);

    // Create font atlas
    [self createFontAtlas];

    // Create render pipeline
    [self createRenderPipeline];

    // Create vertex buffer
    [self createVertexBuffer];

    return YES;
}

- (void)createFontAtlas {
    // Allocate baked character data for ASCII range
    self.bakedChars = (stbtt_bakedchar*)malloc(96 * sizeof(stbtt_bakedchar));

    // Create bitmap for atlas
    unsigned char* atlasPixels = (unsigned char*)malloc(self.atlasWidth * self.atlasHeight);

    // Bake font bitmap - covers ASCII 32-126 (95 characters)
    int result = stbtt_BakeFontBitmap(self.fontData, 0, self.fontSize,
                                     atlasPixels, self.atlasWidth, self.atlasHeight,
                                     32, 96, self.bakedChars);

    if (result <= 0) {
        NSLog(@"Font baking failed");
        free(atlasPixels);
        return;
    }

    NSLog(@"Font atlas created: %dx%d, used %d rows", self.atlasWidth, self.atlasHeight, result);

    // Create Metal texture
    MTLTextureDescriptor* textureDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                                           width:self.atlasWidth
                                                                                          height:self.atlasHeight
                                                                                       mipmapped:NO];
    textureDesc.usage = MTLTextureUsageShaderRead;

    self.fontTexture = [self.device newTextureWithDescriptor:textureDesc];

    // Upload atlas data
    MTLRegion region = MTLRegionMake2D(0, 0, self.atlasWidth, self.atlasHeight);
    [self.fontTexture replaceRegion:region mipmapLevel:0 withBytes:atlasPixels bytesPerRow:self.atlasWidth];

    free(atlasPixels);

    NSLog(@"Font texture created successfully");
}

- (void)createRenderPipeline {
    NSError* error = nil;

    // Create shader source
    NSString* shaderSource = @""
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct VertexIn {\n"
    "    float2 position [[attribute(0)]];\n"
    "    float2 texCoord [[attribute(1)]];\n"
    "    float4 inkColor [[attribute(2)]];\n"
    "    float4 paperColor [[attribute(3)]];\n"
    "};\n"
    "\n"
    "struct VertexOut {\n"
    "    float4 position [[position]];\n"
    "    float2 texCoord;\n"
    "    float4 inkColor;\n"
    "    float4 paperColor;\n"
    "};\n"
    "\n"
    "vertex VertexOut text_vertex(VertexIn in [[stage_in]],\n"
    "                             constant float2& viewportSize [[buffer(1)]]) {\n"
    "    VertexOut out;\n"
    "    out.position.x = (in.position.x / viewportSize.x) * 2.0 - 1.0;\n"
    "    out.position.y = 1.0 - (in.position.y / viewportSize.y) * 2.0;\n"
    "    out.position.z = 0.0;\n"
    "    out.position.w = 1.0;\n"
    "    out.texCoord = in.texCoord;\n"
    "    out.inkColor = in.inkColor;\n"
    "    out.paperColor = in.paperColor;\n"
    "    return out;\n"
    "}\n"
    "\n"
    "fragment float4 text_fragment(VertexOut in [[stage_in]],\n"
    "                              texture2d<float> fontTexture [[texture(0)]]) {\n"
    "    constexpr sampler textureSampler(mag_filter::linear, min_filter::linear);\n"
    "    \n"
    "    // Sample center pixel\n"
    "    float alpha = fontTexture.sample(textureSampler, in.texCoord).r;\n"
    "    \n"
    "    // Conditional boldness based on overdraw setting\n"
    "    float finalAlpha = alpha;\n"
    "    \n"
    "    // Check overdraw flag (passed via paperColor.a > 1.5)\n"
    "    if (in.paperColor.a > 1.5) { // Use paperColor alpha > 1.5 as overdraw flag\n"
    "        // Add boldness by sampling neighboring pixels and taking maximum\n"
    "        float2 texelSize = 1.0 / float2(fontTexture.get_width(), fontTexture.get_height());\n"
    "        float boldOffset = 0.8; // Adjust this value to control boldness (0.5-1.5)\n"
    "        \n"
    "        // Sample neighboring pixels to create bold effect\n"
    "        float maxAlpha = alpha;\n"
    "        maxAlpha = max(maxAlpha, fontTexture.sample(textureSampler, in.texCoord + float2(boldOffset * texelSize.x, 0)).r);\n"
    "        maxAlpha = max(maxAlpha, fontTexture.sample(textureSampler, in.texCoord + float2(-boldOffset * texelSize.x, 0)).r);\n"
    "        maxAlpha = max(maxAlpha, fontTexture.sample(textureSampler, in.texCoord + float2(0, boldOffset * texelSize.y)).r);\n"
    "        maxAlpha = max(maxAlpha, fontTexture.sample(textureSampler, in.texCoord + float2(0, -boldOffset * texelSize.y)).r);\n"
    "        finalAlpha = maxAlpha;\n"
    "    }\n"
    "    \n"
    "    // Use ink color with final alpha\n"
    "    return float4(in.inkColor.rgb, in.inkColor.a * finalAlpha);\n"
    "}";

    // Compile shaders
    id<MTLLibrary> library = [self.device newLibraryWithSource:shaderSource options:nil error:&error];
    if (!library) {
        NSLog(@"Failed to create shader library: %@", error.localizedDescription);
        return;
    }

    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"text_vertex"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"text_fragment"];

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

    // Ink Color
    vertexDescriptor.attributes[2].format = MTLVertexFormatFloat4;
    vertexDescriptor.attributes[2].offset = sizeof(simd_float2) + sizeof(simd_float2);
    vertexDescriptor.attributes[2].bufferIndex = 0;

    // Paper Color
    vertexDescriptor.attributes[3].format = MTLVertexFormatFloat4;
    vertexDescriptor.attributes[3].offset = sizeof(simd_float2) + sizeof(simd_float2) + sizeof(simd_float4);
    vertexDescriptor.attributes[3].bufferIndex = 0;

    // Layout
    vertexDescriptor.layouts[0].stride = sizeof(struct TextVertex);
    vertexDescriptor.layouts[0].stepRate = 1;
    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    // Create render pipeline descriptor
    MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.label = @"TrueType Text Pipeline";
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = fragmentFunction;
    pipelineDescriptor.vertexDescriptor = vertexDescriptor;

    // Color attachment
    pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pipelineDescriptor.colorAttachments[0].blendingEnabled = YES;
    pipelineDescriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pipelineDescriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pipelineDescriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDescriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDescriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipelineDescriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    // Create pipeline state
    self.pipelineState = [self.device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (!self.pipelineState) {
        NSLog(@"Failed to create pipeline state: %@", error.localizedDescription);
    }

    NSLog(@"Render pipeline created successfully");
}

- (void)createVertexBuffer {
    // Create vertex buffer for all possible characters (6 vertices per character for 2 triangles)
    size_t bufferSize = GRID_WIDTH * GRID_HEIGHT * 6 * sizeof(struct TextVertex);
    self.vertexBuffer = [self.device newBufferWithLength:bufferSize options:MTLResourceStorageModeShared];
    NSLog(@"Vertex buffer created: %zu bytes", bufferSize);
}

- (void)print:(NSString*)text {
    for (int i = 0; i < [text length]; i++) {
        unichar ch = [text characterAtIndex:i];

        if (ch == '\n') {
            self.cursorX = 0;
            self.cursorY++;
            if (self.cursorY >= GRID_HEIGHT) {
                self.cursorY = GRID_HEIGHT - 1;
                // TODO: Scroll the grid up
            }
        } else {
            if (self.cursorX < GRID_WIDTH && self.cursorY < GRID_HEIGHT) {
                int index = self.cursorY * GRID_WIDTH + self.cursorX;
                self.textGrid[index].character = ch;
                self.textGrid[index].inkColor = self.currentInk;
                self.textGrid[index].paperColor = self.currentPaper;
            }

            self.cursorX++;
            if (self.cursorX >= GRID_WIDTH) {
                self.cursorX = 0;
                self.cursorY++;
                if (self.cursorY >= GRID_HEIGHT) {
                    self.cursorY = GRID_HEIGHT - 1;
                    // TODO: Scroll the grid up
                }
            }
        }
    }
}

- (void)printAt:(int)x y:(int)y text:(NSString*)text {
    if (x >= 0 && x < GRID_WIDTH && y >= 0 && y < GRID_HEIGHT) {
        self.cursorX = x;
        self.cursorY = y;
        [self print:text];
    }
}

- (void)clear {
    // For editor layer, truly clear to make transparent
    // For terminal layer, set default colors
    if (self.isEditorLayer) {
        // Editor layer: use transparent background in REPL mode, blue in full-screen mode
        simd_float4 clearPaperColor = editor_is_repl_mode() ?
            simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f) :  // Transparent in REPL mode
            simd_make_float4(0.0f, 0.0f, 1.0f, 1.0f);   // Solid blue in full-screen mode

        for (int i = 0; i < GRID_WIDTH * GRID_HEIGHT; i++) {
            self.textGrid[i].character = ' ';
            self.textGrid[i].inkColor = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f); // White text
            self.textGrid[i].paperColor = clearPaperColor;
        }
    } else {
        // Terminal layer: clear but set default background
        for (int i = 0; i < GRID_WIDTH * GRID_HEIGHT; i++) {
            self.textGrid[i].character = ' ';
            self.textGrid[i].inkColor = self.currentInk;
            self.textGrid[i].paperColor = self.currentPaper;
        }
    }
    self.cursorX = 0;
    self.cursorY = 0;
}

- (void)home {
    self.cursorX = 0;
    self.cursorY = 0;
}

- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport {
    if (!self.pipelineState || !self.fontTexture || !self.bakedChars) {
        return;
    }

    // First, render background rectangle for editor layer - only when editor is actually active
    if (self.isEditorLayer && editor_is_active()) {
        // Create background rectangle vertices (2 triangles = 6 vertices)
        float vw = (float)viewport.width;
        float vh = (float)viewport.height;

        // Adjust rectangle size for REPL mode (bottom 6 lines only)
        float rect_y_start = 0;
        float rect_height = vh;

        if (editor_is_repl_mode()) {
            // REPL mode: only cover bottom 6 lines of 25-line grid
            const float cellHeight = vh / 25.0f;  // Height of each text row
            const int REPL_MODE_LINES = 6;
            const int REPL_MODE_START_Y = 19;     // Start at row 19 (0-based)

            rect_y_start = REPL_MODE_START_Y * cellHeight;
            rect_height = REPL_MODE_LINES * cellHeight;
        }

        // Use transparent background in REPL mode, solid blue in full-screen mode
        simd_float4 backgroundPaper = editor_is_repl_mode() ?
            simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f) :  // Transparent in REPL mode
            self.currentPaper;                            // Solid blue in full-screen mode

        struct TextVertex backgroundVertices[6] = {
            // Triangle 1
            { .position = {0, rect_y_start}, .texCoord = {0, 0}, .inkColor = self.currentInk, .paperColor = backgroundPaper, .unicode = ' ' },
            { .position = {vw, rect_y_start}, .texCoord = {0, 0}, .inkColor = self.currentInk, .paperColor = backgroundPaper, .unicode = ' ' },
            { .position = {0, rect_y_start + rect_height}, .texCoord = {0, 0}, .inkColor = self.currentInk, .paperColor = backgroundPaper, .unicode = ' ' },
            // Triangle 2
            { .position = {vw, rect_y_start}, .texCoord = {0, 0}, .inkColor = self.currentInk, .paperColor = backgroundPaper, .unicode = ' ' },
            { .position = {vw, rect_y_start + rect_height}, .texCoord = {0, 0}, .inkColor = self.currentInk, .paperColor = backgroundPaper, .unicode = ' ' },
            { .position = {0, rect_y_start + rect_height}, .texCoord = {0, 0}, .inkColor = self.currentInk, .paperColor = backgroundPaper, .unicode = ' ' }
        };

        // Render background
        [encoder setRenderPipelineState:self.pipelineState];
        [encoder setFragmentTexture:self.fontTexture atIndex:0];
        simd_float2 viewportSize = simd_make_float2(viewport.width, viewport.height);
        [encoder setVertexBytes:&viewportSize length:sizeof(viewportSize) atIndex:1];
        [encoder setVertexBytes:backgroundVertices length:sizeof(backgroundVertices) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];

        // NSLog(@"TrueTypeTextInterface: Rendered background rectangle for editor layer with paper color (%.2f,%.2f,%.2f,%.2f)",
        //       self.currentPaper.x, self.currentPaper.y, self.currentPaper.z, self.currentPaper.w);
    }

    [encoder setRenderPipelineState:self.pipelineState];
    [encoder setFragmentTexture:self.fontTexture atIndex:0];

    // Set viewport size
    simd_float2 viewportSize = simd_make_float2(viewport.width, viewport.height);
    [encoder setVertexBytes:&viewportSize length:sizeof(viewportSize) atIndex:1];

    // Update vertex buffer
    struct TextVertex* vertices = (struct TextVertex*)[self.vertexBuffer contents];
    int vertexCount = 0;

    // Debug: Log rendering attempt (disabled to reduce spam)
    // NSLog(@"TrueTypeTextInterface: Starting render for %s layer", self.isEditorLayer ? "EDITOR" : "TERMINAL");

    // Calculate cell size based on viewport and grid dimensions
    float cellWidth = viewport.width / GRID_WIDTH;
    float cellHeight = viewport.height / GRID_HEIGHT;

    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            int index = y * GRID_WIDTH + x;
            struct TextCell* cell = &self.textGrid[index];

            // Calculate screen position
            float screenX = x * cellWidth;
            float screenY = y * cellHeight;

            // ALWAYS render a background quad for every cell to ensure paper color shows
            // This creates the full cell background regardless of character content
            stbtt_aligned_quad quad;
            quad.x0 = screenX;
            quad.y0 = screenY;
            quad.x1 = screenX + cellWidth;
            quad.y1 = screenY + cellHeight;

            // For character cells, get proper texture coordinates
            if (cell->character >= 32 && cell->character <= 126) {
                // Get baked character data for supported characters
                float xpos = screenX;
                float ypos = screenY;
                stbtt_GetBakedQuad(self.bakedChars, self.atlasWidth, self.atlasHeight,
                                  cell->character - 32, &xpos, &ypos, &quad, 1);
            } else {
                // For empty/unsupported characters, use dummy texture coordinates
                // This will show paper color since glyph alpha will be 0
                quad.s0 = 0.0f;
                quad.t0 = 0.0f;
                quad.s1 = 0.0f;
                quad.t1 = 0.0f;
            }

            // Create vertices for this character (2 triangles = 6 vertices)
            // Pass overdraw flag via paperColor alpha channel
            simd_float4 paperColorWithFlag = cell->paperColor;
            if (g_font_overdraw_enabled) {
                paperColorWithFlag.w = 2.0f; // Use alpha > 1.5 as overdraw flag
            }

            // Triangle 1
            vertices[vertexCount++] = (struct TextVertex){
                .position = simd_make_float2(quad.x0, quad.y0),
                .texCoord = simd_make_float2(quad.s0, quad.t0),
                .inkColor = cell->inkColor,
                .paperColor = paperColorWithFlag,
                .unicode = cell->character
            };
            vertices[vertexCount++] = (struct TextVertex){
                .position = simd_make_float2(quad.x1, quad.y0),
                .texCoord = simd_make_float2(quad.s1, quad.t0),
                .inkColor = cell->inkColor,
                .paperColor = paperColorWithFlag,
                .unicode = cell->character
            };
            vertices[vertexCount++] = (struct TextVertex){
                .position = simd_make_float2(quad.x0, quad.y1),
                .texCoord = simd_make_float2(quad.s0, quad.t1),
                .inkColor = cell->inkColor,
                .paperColor = paperColorWithFlag,
                .unicode = cell->character
            };

            // Triangle 2
            vertices[vertexCount++] = (struct TextVertex){
                .position = simd_make_float2(quad.x1, quad.y0),
                .texCoord = simd_make_float2(quad.s1, quad.t0),
                .inkColor = cell->inkColor,
                .paperColor = paperColorWithFlag,
                .unicode = cell->character
            };
            vertices[vertexCount++] = (struct TextVertex){
                .position = simd_make_float2(quad.x1, quad.y1),
                .texCoord = simd_make_float2(quad.s1, quad.t1),
                .inkColor = cell->inkColor,
                .paperColor = paperColorWithFlag,
                .unicode = cell->character
            };
            vertices[vertexCount++] = (struct TextVertex){
                .position = simd_make_float2(quad.x0, quad.y1),
                .texCoord = simd_make_float2(quad.s0, quad.t1),
                .inkColor = cell->inkColor,
                .paperColor = paperColorWithFlag,
                .unicode = cell->character
            };
        }
    }

    // Debug: Log vertex generation results (disabled to reduce spam)
    // NSLog(@"TrueTypeTextInterface: Generated %d vertices for %s layer", vertexCount, self.isEditorLayer ? "EDITOR" : "TERMINAL");

    if (self.isEditorLayer && vertexCount > 0) {
        // Debug: Check first few cells for editor layer
        // for (int i = 0; i < 5 && i < GRID_WIDTH * GRID_HEIGHT; i++) {
        //     struct TextCell* cell = &self.textGrid[i];
        //     NSLog(@"Editor cell[%d]: char=%d, paper=(%.2f,%.2f,%.2f,%.2f)",
        //           i, cell->character,
        //           cell->paperColor.x, cell->paperColor.y, cell->paperColor.z, cell->paperColor.w);
        // }
    }

    if (vertexCount > 0) {
        [encoder setVertexBuffer:self.vertexBuffer offset:0 atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertexCount];
    } else {
        NSLog(@"TrueTypeTextInterface: WARNING - No vertices generated for %s layer!", self.isEditorLayer ? "EDITOR" : "TERMINAL");
    }
}

- (void)setColor:(simd_float4)ink paper:(simd_float4)paper {
    self.currentInk = ink;
    // In REPL mode, force transparent background for editor layer
    if (self.isEditorLayer && editor_is_repl_mode()) {
        self.currentPaper = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f); // Transparent
    } else {
        self.currentPaper = paper;
    }
}

- (void)setInk:(simd_float4)ink {
    self.currentInk = ink;
}

- (void)setPaper:(simd_float4)paper {
    // In REPL mode, force transparent background for editor layer
    if (self.isEditorLayer && editor_is_repl_mode()) {
        self.currentPaper = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f); // Transparent
    } else {
        self.currentPaper = paper;
    }
}

@end

// C Interface - Single TrueType text layer instance
static TrueTypeTextLayer* g_trueTypeTextLayer = nil;

// Dual text layer instances
static TrueTypeTextLayer* g_terminalTextLayer = nil;  // Layer 5 - Terminal output
static TrueTypeTextLayer* g_editorTextLayer = nil;    // Layer 6 - Editor overlay

extern "C" {
    void truetype_text_layer_init(void* device) {
        @autoreleasepool {
            id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
            g_trueTypeTextLayer = [[TrueTypeTextLayer alloc] initWithDevice:metalDevice];

            // Try to load font - look for PetMe font first, then fallback
            NSString* fontPath = nil;
            NSBundle* bundle = [NSBundle mainBundle];

            // Check current directory first (for development builds)
            NSString* currentDir = [[NSFileManager defaultManager] currentDirectoryPath];
            NSArray* petmeVariants = @[@"PetMe", @"PetMe64", @"PetMe128", @"PetMe2X"];

            for (NSString* variant in petmeVariants) {
                // Try current directory assets folder
                fontPath = [currentDir stringByAppendingPathComponent:[NSString stringWithFormat:@"assets/%@.ttf", variant]];
                if ([[NSFileManager defaultManager] fileExistsAtPath:fontPath]) {
                    NSLog(@"Found %@ font at: %@", variant, fontPath);
                    break;
                }

                // Try bundle resources
                fontPath = [bundle pathForResource:variant ofType:@"ttf"];
                if (fontPath) {
                    NSLog(@"Found %@ font in bundle at: %@", variant, fontPath);
                    break;
                }

                fontPath = nil;
            }

            // Load the font for both layers
            BOOL terminalFontLoaded = NO;
            BOOL editorFontLoaded = NO;
            if (fontPath) {
                terminalFontLoaded = [g_terminalTextLayer loadFont:fontPath fontSize:16.0f];
                editorFontLoaded = [g_editorTextLayer loadFont:fontPath fontSize:16.0f];
            }

            if (!terminalFontLoaded || !editorFontLoaded) {
                NSLog(@"Failed to load TrueType font for one or both layers, text rendering may not work properly");
            }

            NSLog(@"TrueType single text layer initialized");
        }
    }

    void truetype_text_layers_init(void* device) {
        @autoreleasepool {
            id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;

            // Initialize both text layers
            g_terminalTextLayer = [[TrueTypeTextLayer alloc] initWithDevice:metalDevice];
            g_editorTextLayer = [[TrueTypeTextLayer alloc] initWithDevice:metalDevice isEditorLayer:YES];

            NSLog(@"Created terminal and editor text layers");

            // Try to load font - look for PetMe font first, then fallback
            NSString* fontPath = nil;
            NSBundle* bundle = [NSBundle mainBundle];

            // Check current directory first (for development builds)
            NSString* currentDir = [[NSFileManager defaultManager] currentDirectoryPath];
            NSArray* petmeVariants = @[@"PetMe", @"PetMe64", @"PetMe128", @"PetMe2X"];

            for (NSString* variant in petmeVariants) {
                // Try current directory assets folder
                fontPath = [currentDir stringByAppendingPathComponent:[NSString stringWithFormat:@"assets/%@.ttf", variant]];
                if ([[NSFileManager defaultManager] fileExistsAtPath:fontPath]) {
                    NSLog(@"Found %@ font at: %@", variant, fontPath);
                    break;
                }

                // Try bundle resources
                fontPath = [bundle pathForResource:variant ofType:@"ttf"];
                if (fontPath) {
                    NSLog(@"Found %@ font in bundle at: %@", variant, fontPath);
                    break;
                }

                fontPath = nil;
            }

            // Load the font for both layers
            BOOL terminalFontLoaded = NO;
            BOOL editorFontLoaded = NO;
            if (fontPath) {
                terminalFontLoaded = [g_terminalTextLayer loadFont:fontPath fontSize:16.0f];
                editorFontLoaded = [g_editorTextLayer loadFont:fontPath fontSize:16.0f];
            }

            if (!terminalFontLoaded || !editorFontLoaded) {
                NSLog(@"Failed to load TrueType font for one or both layers, text rendering may not work properly");
            }

            NSLog(@"TrueType dual text layers initialized via C interface");
        }
    }

// C interface functions for font overdraw control
extern "C" {
    void truetype_set_font_overdraw(bool enabled) {
        g_font_overdraw_enabled = enabled;
        NSLog(@"Font overdraw %s", enabled ? "ENABLED" : "DISABLED");
    }

    bool truetype_get_font_overdraw() {
        return g_font_overdraw_enabled;
    }
}

    // Terminal layer functions (Layer 5) - for print(), print_at()
    void truetype_terminal_print(const char* text) {
        @autoreleasepool {
            if (g_terminalTextLayer && text) {
                NSString* nsText = [NSString stringWithUTF8String:text];
                [g_terminalTextLayer print:nsText];
            }
        }
    }

    void truetype_terminal_print_at(int x, int y, const char* text) {
        @autoreleasepool {
            if (g_terminalTextLayer && text) {
                NSString* nsText = [NSString stringWithUTF8String:text];
                [g_terminalTextLayer printAt:x y:y text:nsText];
            }
        }
    }

    void truetype_terminal_clear() {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                [g_terminalTextLayer clear];
            }
        }
    }

    void truetype_terminal_home() {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                [g_terminalTextLayer home];
            }
        }
    }

    void truetype_terminal_render(void* encoder, float width, float height) {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
                [g_terminalTextLayer renderWithEncoder:metalEncoder viewport:CGSizeMake(width, height)];
            }
        }
    }

    // Editor layer functions (Layer 6) - for editor system
    void truetype_editor_print(const char* text) {
        @autoreleasepool {
            if (g_editorTextLayer && text) {
                NSString* nsText = [NSString stringWithUTF8String:text];
                [g_editorTextLayer print:nsText];
            }
        }
    }

    void truetype_editor_print_at(int x, int y, const char* text) {
        @autoreleasepool {
            if (g_editorTextLayer && text) {
                NSString* nsText = [NSString stringWithUTF8String:text];
                [g_editorTextLayer printAt:x y:y text:nsText];
            }
        }
    }

    void truetype_editor_clear() {
        @autoreleasepool {
            if (g_editorTextLayer) {
                [g_editorTextLayer clear];
            }
        }
    }

    void truetype_editor_render(void* encoder, float width, float height) {
        @autoreleasepool {
            if (g_editorTextLayer) {
                static bool editor_content_loaded = false;
                extern bool editor_is_active();
                bool active = editor_is_active();

                // Load content once when editor becomes active
                if (active && !editor_content_loaded) {
                    // Set solid blue background for entire layer
                    for (int y = 0; y < GRID_HEIGHT; y++) {
                        for (int x = 0; x < GRID_WIDTH; x++) {
                            int index = y * GRID_WIDTH + x;
                            g_editorTextLayer.textGrid[index].character = ' ';
                            g_editorTextLayer.textGrid[index].inkColor = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);
                            g_editorTextLayer.textGrid[index].paperColor = simd_make_float4(0.0f, 0.0f, 0.6f, 1.0f);
                        }
                    }

                    // Load text content from editor buffer once
                    extern struct TextCell* editor_get_text_buffer();
                    struct TextCell* editorBuffer = editor_get_text_buffer();
                    if (editorBuffer) {
                        for (int i = 0; i < GRID_WIDTH * GRID_HEIGHT; i++) {
                            if (editorBuffer[i].character != ' ') {
                                g_editorTextLayer.textGrid[i] = editorBuffer[i];
                            }
                        }
                    }

                    editor_content_loaded = true;
                } else if (!active) {
                    editor_content_loaded = false;
                }

                // Render the layer
                id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
                [g_editorTextLayer renderWithEncoder:metalEncoder viewport:CGSizeMake(width, height)];
            }
        }
    }

    // Legacy compatibility functions - route to terminal layer
    void truetype_text_layer_print(const char* text) {
        truetype_terminal_print(text);
    }

    void truetype_text_layer_print_at(int x, int y, const char* text) {
        truetype_terminal_print_at(x, y, text);
    }

    void truetype_text_layer_clear() {
        truetype_terminal_clear();
    }

    void truetype_text_layer_home() {
        truetype_terminal_home();
    }

    void truetype_text_layer_render(void* encoder, float width, float height) {
        truetype_terminal_render(encoder, width, height);
    }

    // Status function - displays on last line of top overlay (editor layer)
    void truetype_status(const char* text) {
        @autoreleasepool {
            if (g_editorTextLayer && text) {
                NSString* nsText = [NSString stringWithUTF8String:text];
                // Clear the last line (row 24) and write status text
                for (int x = 0; x < GRID_WIDTH; x++) {
                    int index = (GRID_HEIGHT - 1) * GRID_WIDTH + x;
                    g_editorTextLayer.textGrid[index].character = ' ';
                    g_editorTextLayer.textGrid[index].inkColor = simd_make_float4(0.0f, 0.0f, 0.0f, 1.0f); // Black text
                    g_editorTextLayer.textGrid[index].paperColor = simd_make_float4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow background
                }
                // Write the status text at the beginning of last line
                [g_editorTextLayer printAt:0 y:(GRID_HEIGHT - 1) text:nsText];
                NSLog(@"Status displayed: %s", text);
            }
        }
    }



    void truetype_text_layers_cleanup() {
        @autoreleasepool {
            g_terminalTextLayer = nil;
            g_editorTextLayer = nil;
            NSLog(@"TrueType dual text layers cleaned up");
        }
    }

    // Direct buffer access functions for editor layer
    struct TextCell* editor_get_text_buffer() {
        if (g_editorTextLayer) {
            return g_editorTextLayer.textGrid;
        }
        return NULL;
    }

    void editor_set_cursor_colors(float ink_r, float ink_g, float ink_b, float ink_a,
                                 float paper_r, float paper_g, float paper_b, float paper_a) {
        if (g_editorTextLayer) {
            g_editorTextLayer.currentInk = simd_make_float4(ink_r, ink_g, ink_b, ink_a);
            g_editorTextLayer.currentPaper = simd_make_float4(paper_r, paper_g, paper_b, paper_a);
        }
    }

    // Terminal text layer color functions
    void truetype_terminal_set_color(float ink_r, float ink_g, float ink_b, float ink_a,
                                     float paper_r, float paper_g, float paper_b, float paper_a) {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                simd_float4 ink = simd_make_float4(ink_r, ink_g, ink_b, ink_a);
                simd_float4 paper = simd_make_float4(paper_r, paper_g, paper_b, paper_a);
                [g_terminalTextLayer setColor:ink paper:paper];
            }
        }
    }

    // Direct color grid manipulation functions
    void truetype_poke_colour(int layer, int x, int y, uint32_t ink_colour, uint32_t paper_colour) {
            @autoreleasepool {
                TrueTypeTextLayer* targetLayer = nil;

                if (layer == 5) {
                    targetLayer = g_terminalTextLayer;
                } else if (layer == 6) {
                    targetLayer = g_editorTextLayer;
                }

                if (targetLayer && x >= 0 && x < GRID_WIDTH && y >= 0 && y < GRID_HEIGHT) {
                    int index = y * GRID_WIDTH + x;

                    // Convert uint32_t RGBA to simd_float4
                    float ink_r = ((ink_colour >> 16) & 0xFF) / 255.0f;
                    float ink_g = ((ink_colour >> 8) & 0xFF) / 255.0f;
                    float ink_b = (ink_colour & 0xFF) / 255.0f;
                    float ink_a = ((ink_colour >> 24) & 0xFF) / 255.0f;

                    float paper_r = ((paper_colour >> 16) & 0xFF) / 255.0f;
                    float paper_g = ((paper_colour >> 8) & 0xFF) / 255.0f;
                    float paper_b = (paper_colour & 0xFF) / 255.0f;
                    float paper_a = ((paper_colour >> 24) & 0xFF) / 255.0f;

                    targetLayer.textGrid[index].inkColor = simd_make_float4(ink_r, ink_g, ink_b, ink_a);
                    targetLayer.textGrid[index].paperColor = simd_make_float4(paper_r, paper_g, paper_b, paper_a);
                }
            }
        }

    void truetype_poke_ink(int layer, int x, int y, uint32_t ink_colour) {
            @autoreleasepool {
                TrueTypeTextLayer* targetLayer = nil;

                if (layer == 5) {
                    targetLayer = g_terminalTextLayer;
                } else if (layer == 6) {
                    targetLayer = g_editorTextLayer;
                }

                if (targetLayer && x >= 0 && x < GRID_WIDTH && y >= 0 && y < GRID_HEIGHT) {
                    int index = y * GRID_WIDTH + x;

                    float r = ((ink_colour >> 16) & 0xFF) / 255.0f;
                    float g = ((ink_colour >> 8) & 0xFF) / 255.0f;
                    float b = (ink_colour & 0xFF) / 255.0f;
                    float a = ((ink_colour >> 24) & 0xFF) / 255.0f;

                    targetLayer.textGrid[index].inkColor = simd_make_float4(r, g, b, a);
                }
            }
        }

    void truetype_poke_paper(int layer, int x, int y, uint32_t paper_colour) {
            @autoreleasepool {
                TrueTypeTextLayer* targetLayer = nil;

                if (layer == 5) {
                    targetLayer = g_terminalTextLayer;
                } else if (layer == 6) {
                    targetLayer = g_editorTextLayer;
                }

                if (targetLayer && x >= 0 && x < GRID_WIDTH && y >= 0 && y < GRID_HEIGHT) {
                    int index = y * GRID_WIDTH + x;

                    float r = ((paper_colour >> 16) & 0xFF) / 255.0f;
                    float g = ((paper_colour >> 8) & 0xFF) / 255.0f;
                    float b = (paper_colour & 0xFF) / 255.0f;
                    float a = ((paper_colour >> 24) & 0xFF) / 255.0f;

                    targetLayer.textGrid[index].paperColor = simd_make_float4(r, g, b, a);
                }
            }
        }

    void truetype_terminal_set_ink(float r, float g, float b, float a) {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                simd_float4 ink = simd_make_float4(r, g, b, a);
                [g_terminalTextLayer setInk:ink];
            }
        }
    }

    void truetype_terminal_set_paper(float r, float g, float b, float a) {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                simd_float4 paper = simd_make_float4(r, g, b, a);
                [g_terminalTextLayer setPaper:paper];
            }
        }
    }

}
