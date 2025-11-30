//
//  TextLayer.mm
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <CoreText/CoreText.h>
#import <ImageIO/ImageIO.h>
#import <simd/simd.h>
#include "TextCommon.h"
#import "TextGridManager.h"

@interface TextLayer : NSObject

@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property (nonatomic, strong) id<MTLBuffer> vertexBuffer;
@property (nonatomic, strong) id<MTLTexture> fontAtlas;
@property (nonatomic, assign) struct TextCell* textGrid;
@property (nonatomic, assign) int cursorX;
@property (nonatomic, assign) int cursorY;
@property (nonatomic, assign) simd_float4 currentInk;
@property (nonatomic, assign) simd_float4 currentPaper;
@property (nonatomic, strong) TextGridManager *gridManager;
@property (nonatomic, assign) TextGridLayout currentLayout;
@property (nonatomic, assign) CGSize lastViewportSize;
@property (nonatomic, assign) BOOL needsLayoutUpdate;
@property (nonatomic, assign) float layoutOffsetX;
@property (nonatomic, assign) float layoutOffsetY;
@property (nonatomic, assign) float layoutCellWidth;
@property (nonatomic, assign) float layoutCellHeight;
@property (nonatomic, assign) int viewportStartRow;
@property (nonatomic, assign) int viewportRowCount;

- (instancetype)initWithDevice:(id<MTLDevice>)device;
- (void)createFontAtlas;
- (void)createRenderPipeline;
- (void)debugSaveFontAtlas;
- (void)print:(NSString*)text;
- (void)printAt:(int)x y:(int)y text:(NSString*)text;
- (void)clear;
- (void)home;
- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport;

@end

@implementation TextLayer

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

        // Initialize grid manager
        self.gridManager = [TextGridManager sharedManager];
        self.lastViewportSize = CGSizeZero;
        self.needsLayoutUpdate = YES;

        // Register for grid recalculation callbacks
        extern void text_grid_register_recalc_callback(void (*callback)(TextGridLayout, void*), void* userData);
        text_grid_register_recalc_callback(textLayerGridRecalcCallback, (__bridge void*)self);

        [self createFontAtlas];
        [self createRenderPipeline];
        [self createVertexBuffer];
        [self clear];
    }
    return self;
}

- (void)dealloc {
    if (self.textGrid) {
        free(self.textGrid);
    }

    // Unregister callback
    extern void text_grid_unregister_recalc_callback(void (*callback)(TextGridLayout, void*));
    text_grid_unregister_recalc_callback(textLayerGridRecalcCallback);

    [super dealloc];
}

- (void)createFontAtlas {
    NSLog(@"Loading font atlas from PNG...");

    // Load the pre-generated font atlas PNG
    NSString* atlasPath = [[NSBundle mainBundle] pathForResource:@"font_atlas" ofType:@"png"];
    if (!atlasPath) {
        // Search upward from executable directory to find assets folder
        NSString* executableDir = [[[NSBundle mainBundle] executablePath] stringByDeletingLastPathComponent];
        NSString* searchDir = executableDir;

        // Search up the directory tree from executable location for assets folder
        for (int i = 0; i < 10; i++) {  // Limit search depth to prevent infinite loop
            NSString* assetsPath = [searchDir stringByAppendingPathComponent:@"assets"];
            NSString* fontAtlasPath = [assetsPath stringByAppendingPathComponent:@"font_atlas.png"];

            if ([[NSFileManager defaultManager] fileExistsAtPath:fontAtlasPath]) {
                atlasPath = fontAtlasPath;
                NSLog(@"Found font atlas at: %@", atlasPath);
                break;
            }

            // Move up one directory
            NSString* parentDir = [searchDir stringByDeletingLastPathComponent];
            if ([parentDir isEqualToString:searchDir] || [parentDir isEqualToString:@"/"]) {
                // Reached root or can't go up further
                break;
            }
            searchDir = parentDir;
        }

        // Final fallback to original behavior
        if (!atlasPath) {
            atlasPath = [executableDir stringByAppendingPathComponent:@"../assets/font_atlas.png"];
            NSLog(@"Using fallback atlas path: %@", atlasPath);
        }
    }

    NSLog(@"Trying atlas path: %@", atlasPath);
    NSImage* atlasImage = [[NSImage alloc] initWithContentsOfFile:atlasPath];
    if (!atlasImage) {
        NSLog(@"Failed to load font atlas from: %@", atlasPath);
        return;
    }

    NSLog(@"Loaded atlas image: %.0fx%.0f", atlasImage.size.width, atlasImage.size.height);

    // Get bitmap representation
    NSBitmapImageRep* bitmapRep = nil;
    for (NSImageRep* rep in atlasImage.representations) {
        if ([rep isKindOfClass:[NSBitmapImageRep class]]) {
            bitmapRep = (NSBitmapImageRep*)rep;
            break;
        }
    }

    if (!bitmapRep) {
        NSLog(@"Could not get bitmap representation");
        return;
    }

    int width = (int)[bitmapRep pixelsWide];
    int height = (int)[bitmapRep pixelsHigh];
    NSLog(@"Bitmap dimensions: %dx%d", width, height);

    // Convert to grayscale data for R8 texture
    unsigned char* atlasData = (unsigned char*)malloc(width * height);
    unsigned char* srcData = [bitmapRep bitmapData];
    int bytesPerPixel = (int)[bitmapRep bitsPerPixel] / 8;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int srcIndex = (y * width + x) * bytesPerPixel;
            int dstIndex = y * width + x;

            if (bytesPerPixel == 1) {
                // Already grayscale
                atlasData[dstIndex] = srcData[srcIndex];
            } else if (bytesPerPixel >= 3) {
                // Convert RGB to grayscale using red channel (assuming white text)
                atlasData[dstIndex] = srcData[srcIndex];
            } else {
                atlasData[dstIndex] = 0;
            }
        }
    }

    // Create Metal texture
    MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                                                 width:width
                                                                                                height:height
                                                                                             mipmapped:NO];
    textureDescriptor.usage = MTLTextureUsageShaderRead;

    self.fontAtlas = [self.device newTextureWithDescriptor:textureDescriptor];

    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [self.fontAtlas replaceRegion:region mipmapLevel:0 withBytes:atlasData bytesPerRow:width];

    free(atlasData);
    NSLog(@"Font atlas loaded successfully");
}

- (void)createRenderPipeline {
    NSError* error = nil;

    // Create shaders
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
    "    // Simple coordinate conversion - map (0,0) to (-1,-1) and (width,height) to (1,1)\n"
    "    out.position.x = (in.position.x / viewportSize.x) * 2.0 - 1.0;\n"
    "    out.position.y = 1.0 - (in.position.y / viewportSize.y) * 2.0;  // Flip Y\n"
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
    "        return float4(in.inkColor.rgb, in.inkColor.a * alpha);\n"
    "    } else {\n"
    "        return in.paperColor;\n"
    "    }\n"
    "}\n";

    id<MTLLibrary> library = [self.device newLibraryWithSource:shaderSource options:nil error:&error];
    if (!library) {
        NSLog(@"Failed to create text shader library: %@", error.localizedDescription);
        return;
    }

    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"text_vertex"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"text_fragment"];

    // Create pipeline descriptor
    MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
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

    // Vertex descriptor
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

    // Grid position
    vertexDescriptor.attributes[5].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[5].offset = sizeof(simd_float2) + sizeof(simd_float2) + sizeof(simd_float4) + sizeof(simd_float4) + sizeof(uint32_t);
    vertexDescriptor.attributes[5].bufferIndex = 0;

    vertexDescriptor.layouts[0].stride = sizeof(struct TextVertex);

    pipelineDescriptor.vertexDescriptor = vertexDescriptor;

    self.pipelineState = [self.device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (!self.pipelineState) {
        NSLog(@"Failed to create text pipeline state: %@", error.localizedDescription);
    }
}

- (void)createVertexBuffer {
    // Create buffer for maximum possible characters (each character = 6 vertices for 2 triangles)
    NSUInteger bufferSize = GRID_WIDTH * GRID_HEIGHT * 6 * sizeof(struct TextVertex);
    self.vertexBuffer = [self.device newBufferWithLength:bufferSize options:MTLResourceStorageModeShared];
}

- (void)print:(NSString*)text {
    NSLog(@"TextLayer print called with: '%@'", text);
    for (NSUInteger i = 0; i < text.length; i++) {
        unichar ch = [text characterAtIndex:i];

        if (ch == '\n') {
            self.cursorX = 0;
            self.cursorY++;
            if (self.cursorY >= GRID_HEIGHT) {
                // Scroll up
                memmove(self.textGrid, self.textGrid + GRID_WIDTH,
                       (GRID_HEIGHT - 1) * GRID_WIDTH * sizeof(struct TextCell));
                // Clear last line
                memset(self.textGrid + (GRID_HEIGHT - 1) * GRID_WIDTH, 0,
                       GRID_WIDTH * sizeof(struct TextCell));
                self.cursorY = GRID_HEIGHT - 1;
            }
        } else if (ch >= 32 && ch < 127) { // Printable ASCII
            int index = self.cursorY * GRID_WIDTH + self.cursorX;
            self.textGrid[index].character = ch;
            self.textGrid[index].inkColor = self.currentInk;
            self.textGrid[index].paperColor = self.currentPaper;
            NSLog(@"Set character '%c' at (%d,%d)", (char)ch, self.cursorX, self.cursorY);

            self.cursorX++;
            if (self.cursorX >= GRID_WIDTH) {
                self.cursorX = 0;
                self.cursorY++;
                if (self.cursorY >= GRID_HEIGHT) {
                    self.cursorY = GRID_HEIGHT - 1;
                }
            }
        }
    }
    [self debugPrintGrid];
}

- (void)printAt:(int)x y:(int)y text:(NSString*)text {
    if (x >= 0 && x < GRID_WIDTH && y >= 0 && y < GRID_HEIGHT) {
        self.cursorX = x;
        self.cursorY = y;
        [self print:text];
    }
}

- (void)clear {
    memset(self.textGrid, 0, GRID_WIDTH * GRID_HEIGHT * sizeof(struct TextCell));
    // Fill with spaces
    for (int i = 0; i < GRID_WIDTH * GRID_HEIGHT; i++) {
        self.textGrid[i].character = ' ';
        self.textGrid[i].inkColor = self.currentInk;
        self.textGrid[i].paperColor = self.currentPaper;
    }
}

- (void)home {
    self.cursorX = 0;
    self.cursorY = 0;
}

// Callback function for grid recalculation notifications
void textLayerGridRecalcCallback(TextGridLayout layout, void* userData) {
    TextLayer* textLayer = (__bridge TextLayer*)userData;
    textLayer.needsLayoutUpdate = YES;
    NSLog(@"TextLayer: Grid recalculation callback - marking for update");
}

- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport {
    if (!self.pipelineState || !self.vertexBuffer || !self.fontAtlas) {
        // Text layer render failed - missing components (message suppressed to avoid 60fps spam)
        return;
    }

    // Recalculate layout if viewport changed or update needed
    if (!CGSizeEqualToSize(viewport, self.lastViewportSize) || self.needsLayoutUpdate) {
        self.currentLayout = [self.gridManager calculateLayoutForViewport:viewport
                                                                    mode:[self.gridManager currentMode]
                                                               scaleMode:[self.gridManager scaleMode]];
        self.lastViewportSize = viewport;
        self.needsLayoutUpdate = NO;

        // Log layout info for debugging
        [self.gridManager logLayoutInfo:self.currentLayout];
    }

    // Update vertex buffer with current text grid
    struct TextVertex* vertices = (struct TextVertex*)[self.vertexBuffer contents];
    int vertexCount = 0;
    int charCount = 0;

    // Use adaptive cell dimensions from grid manager
    const float charWidth = self.currentLayout.cellWidth;
    const float charHeight = self.currentLayout.cellHeight;
    const float offsetX = self.currentLayout.offsetX;
    const float offsetY = self.currentLayout.offsetY;
    const int maxCols = self.currentLayout.gridWidth;
    const int maxRows = self.currentLayout.gridHeight;

    // Font atlas constants
    const float atlasWidth = 128.0f;
    const float atlasHeight = 48.0f;
    const float charTexSizeX = 8.0f / atlasWidth;  // 8 pixels wide in 128px atlas = 0.0625
    const float charTexSizeY = 8.0f / atlasHeight; // 8 pixels tall in 48px atlas = 0.1667

    for (int y = 0; y < maxRows && y < GRID_HEIGHT; y++) {
        for (int x = 0; x < maxCols && x < GRID_WIDTH; x++) {
            struct TextCell* cell = &self.textGrid[y * GRID_WIDTH + x];

            if (cell->character == 0 || cell->character == ' ') {
                continue; // Skip empty cells
            }

            charCount++;

            // Calculate character position in atlas
            int charIndex = cell->character - 32; // ASCII offset
            if (charIndex < 0 || charIndex >= 96) continue; // Only ASCII 32-127

            int charX = charIndex % 16; // 16 characters per row in 8x8 atlas
            int charY = charIndex / 16;

            float texLeft = charX * charTexSizeX;
            float texRight = (charX + 1) * charTexSizeX;
            float texTop = charY * charTexSizeY;
            float texBottom = (charY + 1) * charTexSizeY;

            // Calculate screen position with centering offset
            float screenX = x * charWidth + offsetX;
            float screenY = y * charHeight + offsetY;

            // Debug actual rendering coordinates
            if (x == 0 && y == 0) {
                FILE *debugFile = fopen("/tmp/superterminal_debug.log", "a");
                if (debugFile) {
                    fprintf(debugFile, "\n=== TEXTLAYER RENDERING DEBUG ===\n");
                    fprintf(debugFile, "CHAR AT (0,0): '%c' (ASCII %d)\n", cell->character, cell->character);
                    fprintf(debugFile, "CELL SIZE: %.1fx%.1f (charWidth x charHeight)\n", charWidth, charHeight);
                    fprintf(debugFile, "GRID OFFSETS: %.1fx%.1f (offsetX x offsetY)\n", offsetX, offsetY);
                    fprintf(debugFile, "SCREEN POSITION: %.1fx%.1f (screenX x screenY)\n", screenX, screenY);
                    fprintf(debugFile, "QUAD CORNERS: (%.1f,%.1f) to (%.1f,%.1f)\n",
                           screenX, screenY, screenX + charWidth, screenY + charHeight);
                    fprintf(debugFile, "VIEWPORT SIZE: %dx%d chars, cell %.1fx%.1f\n",
                           maxCols, maxRows, charWidth, charHeight);
                    fprintf(debugFile, "==================================\n");
                    fflush(debugFile);
                    fclose(debugFile);
                }
            }

            // Create quad (2 triangles = 6 vertices)
            simd_float2 gridPosition = simd_make_float2(x, y);
            struct TextVertex quad[6] = {
                // Triangle 1
                {{screenX, screenY}, {texLeft, texTop}, cell->inkColor, cell->paperColor, cell->character, gridPosition},
                {{screenX + charWidth, screenY}, {texRight, texTop}, cell->inkColor, cell->paperColor, cell->character, gridPosition},
                {{screenX, screenY + charHeight}, {texLeft, texBottom}, cell->inkColor, cell->paperColor, cell->character, gridPosition},

                // Triangle 2
                {{screenX + charWidth, screenY}, {texRight, texTop}, cell->inkColor, cell->paperColor, cell->character, gridPosition},
                {{screenX + charWidth, screenY + charHeight}, {texRight, texBottom}, cell->inkColor, cell->paperColor, cell->character, gridPosition},
                {{screenX, screenY + charHeight}, {texLeft, texBottom}, cell->inkColor, cell->paperColor, cell->character, gridPosition}
            };

            memcpy(&vertices[vertexCount], quad, sizeof(quad));
            vertexCount += 6;
        }
    }

    // Log render info (throttled to avoid spam)
    static int logCounter = 0;
    if (++logCounter % 60 == 0) {  // Log every 60 frames
        NSLog(@"Text render: %d chars, %d vertices, grid %dx%d, cell %.1fx%.1f, offset %.1f,%.1f",
              charCount, vertexCount, maxCols, maxRows, charWidth, charHeight, offsetX, offsetY);
    }

    if (vertexCount == 0) {
        NSLog(@"No vertices to draw - no visible characters");
        return; // Nothing to draw
    }

    // Debug first few vertices
    for (int i = 0; i < MIN(12, vertexCount); i += 6) {
        NSLog(@"Vertex %d: pos=(%.1f,%.1f) tex=(%.3f,%.3f) ink=(%.1f,%.1f,%.1f,%.1f) paper=(%.1f,%.1f,%.1f,%.1f)",
              i, vertices[i].position.x, vertices[i].position.y,
              vertices[i].texCoord.x, vertices[i].texCoord.y,
              vertices[i].inkColor.x, vertices[i].inkColor.y, vertices[i].inkColor.z, vertices[i].inkColor.w,
              vertices[i].paperColor.x, vertices[i].paperColor.y, vertices[i].paperColor.z, vertices[i].paperColor.w);
    }

    // Set pipeline state
    [encoder setRenderPipelineState:self.pipelineState];

    // Set vertex buffer
    [encoder setVertexBuffer:self.vertexBuffer offset:0 atIndex:0];

    // Set viewport size
    simd_float2 viewportSize = simd_make_float2(viewport.width, viewport.height);
    [encoder setVertexBytes:&viewportSize length:sizeof(viewportSize) atIndex:1];

    // Set texture
    [encoder setFragmentTexture:self.fontAtlas atIndex:0];

    // Draw
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertexCount];
}



- (void)debugPrintGrid {
    NSLog(@"=== TEXT GRID DEBUG ===");
    for (int y = 0; y < 5; y++) { // Just first 5 rows
        NSMutableString* line = [NSMutableString string];
        for (int x = 0; x < GRID_WIDTH; x++) {
            struct TextCell* cell = &self.textGrid[y * GRID_WIDTH + x];
            if (cell->character >= 32 && cell->character < 127) {
                [line appendFormat:@"%c", (char)cell->character];
            } else {
                [line appendString:@" "];
            }
        }
        NSLog(@"Row %d: '%@'", y, line);
    }
    NSLog(@"Cursor at (%d, %d)", self.cursorX, self.cursorY);
}

- (void)debugSaveFontAtlas {
    NSLog(@"Debug: Font atlas is now loaded from PNG, no need to save");
}

- (void)setColor:(simd_float4)ink paper:(simd_float4)paper {
    self.currentInk = ink;
    self.currentPaper = paper;
}

- (void)setInk:(simd_float4)ink {
    self.currentInk = ink;
}

- (void)setPaper:(simd_float4)paper {
    self.currentPaper = paper;
}

@end

// C interface
static TextLayer* g_textLayer = nil;

extern "C" {
    void text_layer_init(void* device) {
        @autoreleasepool {
            id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
            g_textLayer = [[TextLayer alloc] initWithDevice:metalDevice];
            NSLog(@"Text layer initialized");
        }
    }

    void text_layer_print(const char* text) {
        @autoreleasepool {
            if (g_textLayer && text) {
                NSString* nsText = [NSString stringWithUTF8String:text];
                [g_textLayer print:nsText];
            }
        }
    }

    void text_layer_print_at(int x, int y, const char* text) {
        @autoreleasepool {
            if (g_textLayer && text) {
                NSString* nsText = [NSString stringWithUTF8String:text];
                [g_textLayer printAt:x y:y text:nsText];
            }
        }
    }

    void text_layer_clear() {
        @autoreleasepool {
            if (g_textLayer) {
                [g_textLayer clear];
            }
        }
    }

    void text_layer_home() {
        @autoreleasepool {
            if (g_textLayer) {
                [g_textLayer home];
            }
        }
    }

    void text_layer_render(void* encoder, float width, float height) {
        @autoreleasepool {
            if (g_textLayer) {
                id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
                [g_textLayer renderWithEncoder:metalEncoder viewport:CGSizeMake(width, height)];
            }
        }
    }

    void text_layer_set_color(float ink_r, float ink_g, float ink_b, float ink_a,
                              float paper_r, float paper_g, float paper_b, float paper_a) {
        @autoreleasepool {
            if (g_textLayer) {
                simd_float4 ink = simd_make_float4(ink_r, ink_g, ink_b, ink_a);
                simd_float4 paper = simd_make_float4(paper_r, paper_g, paper_b, paper_a);
                [g_textLayer setColor:ink paper:paper];
            }
        }
    }

    void text_layer_set_ink(float r, float g, float b, float a) {
        @autoreleasepool {
            if (g_textLayer) {
                simd_float4 ink = simd_make_float4(r, g, b, a);
                [g_textLayer setInk:ink];
            }
        }
    }

    void text_layer_set_paper(float r, float g, float b, float a) {
        @autoreleasepool {
            if (g_textLayer) {
                simd_float4 paper = simd_make_float4(r, g, b, a);
                [g_textLayer setPaper:paper];
            }
        }
    }

    void text_layer_cleanup() {
        @autoreleasepool {
            g_textLayer = nil;
            NSLog(@"Text layer cleaned up");
        }
    }
}
