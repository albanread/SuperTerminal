//
//  TrueTypeTextLayer.mm
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
- (BOOL)loadFont:(NSString*)fontPath fontSize:(float)size;
- (void)createFontAtlas;
- (void)createRenderPipeline;
- (void)print:(NSString*)text;
- (void)printAt:(int)x y:(int)y text:(NSString*)text;
- (void)clear;
- (void)home;
- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport;

@end

@implementation TrueTypeTextLayer

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    self = [super init];
    if (self) {
        self.device = device;

        // Initialize text grid
        self.textGrid = (struct TextCell*)calloc(GRID_WIDTH * GRID_HEIGHT, sizeof(struct TextCell));

        // Initialize cursor and colors
        self.cursorX = 0;
        self.cursorY = 0;
        self.currentInk = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);    // White
        self.currentPaper = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f);  // Transparent

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
        // Fallback to system monospace font
        NSLog(@"Could not load font from %@, using system monospace", fontPath);

        // Get system monospace font
        NSFont* systemFont = [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightRegular];
        if (!systemFont) {
            NSLog(@"Could not get system monospace font");
            return NO;
        }

        // For system fonts, we'll need to use CoreText to get the font data
        // This is more complex, so let's try with a bundled font first
        NSBundle* bundle = [NSBundle mainBundle];
        NSString* petmePath = [bundle pathForResource:@"PetMe64" ofType:@"ttf"];
        if (!petmePath) {
            petmePath = [bundle pathForResource:@"PetMe" ofType:@"ttf"];
        }

        if (petmePath) {
            fontFileData = [NSData dataWithContentsOfFile:petmePath];
            NSLog(@"Using bundled PetMe font: %@", petmePath);
        }
    }

    if (!fontFileData) {
        NSLog(@"No font data available");
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
    "    uint32_t unicode [[attribute(4)]];\n"
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
    "    float alpha = fontTexture.sample(textureSampler, in.texCoord).r;\n"
    "    \n"
    "    if (alpha > 0.1) {\n"
    "        // Character pixel - use ink color\n"
    "        return float4(in.inkColor.rgb, in.inkColor.a * alpha);\n"
    "    } else {\n"
    "        // Background pixel - use paper color\n"
    "        return in.paperColor;\n"
    "    }\n"
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

    // Ink color
    vertexDescriptor.attributes[2].format = MTLVertexFormatFloat4;
    vertexDescriptor.attributes[2].offset = sizeof(simd_float2) + sizeof(simd_float2);
    vertexDescriptor.attributes[2].bufferIndex = 0;

    // Paper color
    vertexDescriptor.attributes[3].format = MTLVertexFormatFloat4;
    vertexDescriptor.attributes[3].offset = sizeof(simd_float2) + sizeof(simd_float2) + sizeof(simd_float4);
    vertexDescriptor.attributes[3].bufferIndex = 0;

    // Unicode
    vertexDescriptor.attributes[4].format = MTLVertexFormatUInt;
    vertexDescriptor.attributes[4].offset = sizeof(simd_float2) + sizeof(simd_float2) + sizeof(simd_float4) + sizeof(simd_float4);
    vertexDescriptor.attributes[4].bufferIndex = 0;

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
    // Clear with visible background colors instead of transparent
    for (int i = 0; i < GRID_WIDTH * GRID_HEIGHT; i++) {
        self.textGrid[i].character = ' ';
        self.textGrid[i].inkColor = simd_make_float4(0.8f, 0.8f, 0.8f, 1.0f);  // Light gray text
        self.textGrid[i].paperColor = simd_make_float4(0.1f, 0.1f, 0.1f, 1.0f); // Dark gray background
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

    [encoder setRenderPipelineState:self.pipelineState];
    [encoder setFragmentTexture:self.fontTexture atIndex:0];

    // Set viewport size
    simd_float2 viewportSize = simd_make_float2(viewport.width, viewport.height);
    [encoder setVertexBytes:&viewportSize length:sizeof(viewportSize) atIndex:1];

    // Update vertex buffer
    struct TextVertex* vertices = (struct TextVertex*)[self.vertexBuffer contents];
    int vertexCount = 0;

    // Calculate cell size based on viewport and grid dimensions
    float cellWidth = viewport.width / GRID_WIDTH;
    float cellHeight = viewport.height / GRID_HEIGHT;

    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            int index = y * GRID_WIDTH + x;
            struct TextCell* cell = &self.textGrid[index];

            // Calculate screen position for this cell
            float screenX = x * cellWidth;
            float screenY = y * cellHeight;

            if (y == 0 && x < 10) {
                NSLog(@"Cell[%d,%d]: char=%d, ink=(%.2f,%.2f,%.2f,%.2f), paper=(%.2f,%.2f,%.2f,%.2f)",
                      x, y, cell->character,
                      cell->inkColor.x, cell->inkColor.y, cell->inkColor.z, cell->inkColor.w,
                      cell->paperColor.x, cell->paperColor.y, cell->paperColor.z, cell->paperColor.w);
            }

            // Render background quad for this cell (always render background)
            vertices[vertexCount++] = (struct TextVertex){
                .position = simd_make_float2(screenX, screenY),
                .texCoord = simd_make_float2(0.0f, 0.0f),  // Empty texture coords for background
                .inkColor = cell->inkColor,
                .paperColor = cell->paperColor,
                .unicode = 32  // Space character for background
            };
            vertices[vertexCount++] = (struct TextVertex){
                .position = simd_make_float2(screenX + cellWidth, screenY),
                .texCoord = simd_make_float2(0.0f, 0.0f),
                .inkColor = cell->inkColor,
                .paperColor = cell->paperColor,
                .unicode = 32
            };
            vertices[vertexCount++] = (struct TextVertex){
                .position = simd_make_float2(screenX, screenY + cellHeight),
                .texCoord = simd_make_float2(0.0f, 0.0f),
                .inkColor = cell->inkColor,
                .paperColor = cell->paperColor,
                .unicode = 32
            };
            vertices[vertexCount++] = (struct TextVertex){
                .position = simd_make_float2(screenX + cellWidth, screenY),
                .texCoord = simd_make_float2(0.0f, 0.0f),
                .inkColor = cell->inkColor,
                .paperColor = cell->paperColor,
                .unicode = 32
            };
            vertices[vertexCount++] = (struct TextVertex){
                .position = simd_make_float2(screenX + cellWidth, screenY + cellHeight),
                .texCoord = simd_make_float2(0.0f, 0.0f),
                .inkColor = cell->inkColor,
                .paperColor = cell->paperColor,
                .unicode = 32
            };
            vertices[vertexCount++] = (struct TextVertex){
                .position = simd_make_float2(screenX, screenY + cellHeight),
                .texCoord = simd_make_float2(0.0f, 0.0f),
                .inkColor = cell->inkColor,
                .paperColor = cell->paperColor,
                .unicode = 32
            };

            // If there's a character, render it on top of the background
            if (cell->character != 0 && cell->character >= 32 && cell->character <= 126) {
                // Get baked character data
                stbtt_bakedchar* bc = &self.bakedChars[cell->character - 32];

                // Get quad for this character
                stbtt_aligned_quad quad;
                float xpos = screenX;
                float ypos = screenY;
                stbtt_GetBakedQuad(self.bakedChars, self.atlasWidth, self.atlasHeight,
                                  cell->character - 32, &xpos, &ypos, &quad, 1);

                // Create vertices for this character (2 triangles = 6 vertices)
                vertices[vertexCount++] = (struct TextVertex){
                    .position = simd_make_float2(quad.x0, quad.y0),
                    .texCoord = simd_make_float2(quad.s0, quad.t0),
                    .inkColor = cell->inkColor,
                    .paperColor = cell->paperColor,
                    .unicode = cell->character
                };
                vertices[vertexCount++] = (struct TextVertex){
                    .position = simd_make_float2(quad.x1, quad.y0),
                    .texCoord = simd_make_float2(quad.s1, quad.t0),
                    .inkColor = cell->inkColor,
                    .paperColor = cell->paperColor,
                    .unicode = cell->character
                };
                vertices[vertexCount++] = (struct TextVertex){
                    .position = simd_make_float2(quad.x0, quad.y1),
                    .texCoord = simd_make_float2(quad.s0, quad.t1),
                    .inkColor = cell->inkColor,
                    .paperColor = cell->paperColor,
                    .unicode = cell->character
                };
                vertices[vertexCount++] = (struct TextVertex){
                    .position = simd_make_float2(quad.x1, quad.y0),
                    .texCoord = simd_make_float2(quad.s1, quad.t0),
                    .inkColor = cell->inkColor,
                    .paperColor = cell->paperColor,
                    .unicode = cell->character
                };
                vertices[vertexCount++] = (struct TextVertex){
                    .position = simd_make_float2(quad.x1, quad.y1),
                    .texCoord = simd_make_float2(quad.s1, quad.t1),
                    .inkColor = cell->inkColor,
                    .paperColor = cell->paperColor,
                    .unicode = cell->character
                };
                vertices[vertexCount++] = (struct TextVertex){
                    .position = simd_make_float2(quad.x0, quad.y1),
                    .texCoord = simd_make_float2(quad.s0, quad.t1),
                    .inkColor = cell->inkColor,
                    .paperColor = cell->paperColor,
                    .unicode = cell->character
                };
            }
        }
    }

    NSLog(@"TrueTypeTextLayer: Generated %d vertices for rendering", vertexCount);

    if (vertexCount > 0) {
        NSLog(@"TrueTypeTextLayer: Setting vertex buffer and drawing %d triangles", vertexCount/3);
        [encoder setVertexBuffer:self.vertexBuffer offset:0 atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertexCount];
        NSLog(@"TrueTypeTextLayer: Draw call completed");
    } else {
        NSLog(@"TrueTypeTextLayer: No vertices to render!");
    }
}

@end
