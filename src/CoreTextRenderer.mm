//
//  CoreTextRenderer.mm
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  CoreText-based text renderer - replaces STB TrueType with native macOS rendering
//

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <simd/simd.h>
#include "TextCommon.h"
#include "CoreTextRenderer.h"
#import "TextGridManager.h"


// Global CRT effect states
static bool g_crt_glow_enabled = false;
static bool g_crt_scanlines_enabled = false;
static float g_crt_glow_intensity = 0.3f;  // 0.0 to 1.0
static float g_crt_scanline_intensity = 0.15f;  // 0.0 to 1.0

// Text mode configuration table
static TextModeConfig g_text_mode_configs[7] = {
    { TEXT_MODE_20x25, 20, 25, 48.0f, "Giant (20×25)", "Large text for presentations" },
    { TEXT_MODE_40x25, 40, 25, 24.0f, "Large (40×25)", "C64 classic style" },
    { TEXT_MODE_40x50, 40, 50, 24.0f, "Medium (40×50)", "More rows, comfortable" },
    { TEXT_MODE_64x44, 64, 44, 16.0f, "Standard (64×44)", "Balanced default" },
    { TEXT_MODE_80x25, 80, 25, 12.0f, "Compact (80×25)", "Wide code view" },
    { TEXT_MODE_80x50, 80, 50, 12.0f, "Dense (80×50)", "Maximum code" },
    { TEXT_MODE_120x60, 120, 60, 8.0f, "UltraWide (120×60)", "Maximum screen real estate" }
};

// Layer-specific state tracking
struct LayerState {
    TextMode textMode;
    int cursorX;
    int cursorY;
};

static LayerState g_terminal_state = {TEXT_MODE_64x44, 0, 0};  // Layer 5 - default 64×44
static LayerState g_editor_state = {TEXT_MODE_80x50, 0, 0};    // Layer 6 - default 80×50

// Current text mode (shared for now, but layers can have independent modes)
static TextMode g_current_text_mode = TEXT_MODE_64x44;

// External function to check REPL mode state
extern "C" bool editor_is_repl_mode(void);
extern "C" bool editor_is_active(void);

// Glyph cache entry
struct GlyphCacheEntry {
    CGRect atlasRect;      // Position in atlas
    CGSize advance;        // Glyph advance
    CGPoint offset;        // Rendering offset
    bool valid;
};

@interface CoreTextLayer : NSObject

@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property (nonatomic, strong) id<MTLBuffer> vertexBuffer;
@property (nonatomic, strong) id<MTLTexture> fontTexture;
@property (nonatomic, assign) struct TextCell* textGrid;
@property (nonatomic, assign) int cursorX;
@property (nonatomic, assign) int cursorY;
@property (nonatomic, assign) simd_float4 currentInk;
@property (nonatomic, assign) simd_float4 currentPaper;

// Visual cursor position (for rendering, separate from print cursor)
@property (nonatomic, assign) int visualCursorX;
@property (nonatomic, assign) int visualCursorY;
@property (nonatomic, assign) BOOL visualCursorVisible;
@property (nonatomic, assign) float visualCursorBlink;
@property (nonatomic, assign) BOOL isEditorLayer;

// Uniform buffer for shader (contains TextUniforms with cursor info)
@property (nonatomic, strong) id<MTLBuffer> uniformBuffer;

// Main uniforms buffer (contains Uniforms with matrices)
@property (nonatomic, strong) id<MTLBuffer> mainUniformBuffer;

// Viewport control - which rows to render
@property (nonatomic, assign) int viewportStartRow;  // First row to render (default 0)
@property (nonatomic, assign) int viewportRowCount;  // Number of rows to render (default GRID_HEIGHT)

// Scrollback buffer properties
@property (nonatomic, assign) int viewportStartLine;  // Top line visible in viewport (0 to BUFFER_HEIGHT - viewportHeight)
@property (nonatomic, assign) int viewportHeight;     // Number of visible lines (typically matches window)
@property (nonatomic, assign) BOOL autoScroll;        // Auto-scroll to follow cursor

// Layout offsets for centering text grid in viewport
@property (nonatomic, assign) float layoutOffsetX;  // Horizontal centering offset
@property (nonatomic, assign) float layoutOffsetY;  // Vertical centering offset

// CoreText font data
@property (nonatomic, assign) CTFontRef font;
@property (nonatomic, assign) float fontSize;
@property (nonatomic, assign) float charWidth;
@property (nonatomic, assign) float charHeight;
@property (nonatomic, assign) float baseline;
@property (nonatomic, assign) float maxGlyphHeight;  // Maximum atlas glyph height (for paper positioning)

// Font atlas
@property (nonatomic, assign) int atlasWidth;
@property (nonatomic, assign) int atlasHeight;
@property (nonatomic, assign) int atlasX;
@property (nonatomic, assign) int atlasY;
@property (nonatomic, assign) int atlasRowHeight;
@property (nonatomic, strong) NSMutableData* atlasData;
@property (nonatomic, strong) NSMutableDictionary* glyphCache; // unichar -> GlyphCacheEntry

- (instancetype)initWithDevice:(id<MTLDevice>)device;
- (instancetype)initWithDevice:(id<MTLDevice>)device isEditorLayer:(BOOL)isEditor;
- (BOOL)loadFont:(NSString*)fontPath fontSize:(float)size;
- (BOOL)loadFontByName:(NSString*)fontName fontSize:(float)size;
- (BOOL)reloadFontAtSize:(float)size windowWidth:(float)windowWidth windowHeight:(float)windowHeight;
- (void)createFontAtlas;
- (void)createRenderPipeline;
- (GlyphCacheEntry)addGlyphToAtlas:(unichar)character;
- (void)print:(NSString*)text;
- (void)printAt:(int)x y:(int)y text:(NSString*)text;
- (void)clear;
- (void)home;
- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport;
- (void)setColor:(simd_float4)ink paper:(simd_float4)paper;
- (void)setInk:(simd_float4)ink;
- (void)setPaper:(simd_float4)paper;
- (void)createVertexBuffer;

// Scrollback buffer methods
- (void)locateLine:(int)line;
- (void)scrollToLine:(int)line;
- (void)scrollUp:(int)lines;
- (void)scrollDown:(int)lines;
- (void)pageUp;
- (void)pageDown;
- (void)scrollToTop;
- (void)scrollToBottom;
- (int)getCursorLine;
- (int)getCursorColumn;
- (int)getViewportLine;
- (int)getViewportHeight;
- (void)setAutoScroll:(BOOL)enabled;
- (BOOL)getAutoScroll;

@end

@implementation CoreTextLayer

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    return [self initWithDevice:device isEditorLayer:NO];
}

- (instancetype)initWithDevice:(id<MTLDevice>)device isEditorLayer:(BOOL)isEditor {
    NSLog(@"CoreText: initWithDevice called, isEditor=%d", isEditor);
    self = [super init];
    if (self) {
        NSLog(@"CoreText: Setting device and properties");
        self.device = device;
        self.isEditorLayer = isEditor;

        // Initialize text grid with 2000-line scrollback buffer
        NSLog(@"CoreText: Allocating text grid (%d x %d = %d cells)", BUFFER_WIDTH, BUFFER_HEIGHT, BUFFER_WIDTH * BUFFER_HEIGHT);
        self.textGrid = (struct TextCell*)calloc(BUFFER_WIDTH * BUFFER_HEIGHT, sizeof(struct TextCell));
        NSLog(@"CoreText: Text grid allocated at %p", self.textGrid);

        NSLog(@"CoreText: Initializing viewport settings");
        // Initialize viewport (default: render all rows)
        self.viewportStartRow = 0;
        self.viewportRowCount = GRID_HEIGHT;

        // Initialize scrollback viewport
        self.viewportStartLine = 0;
        self.viewportHeight = 60;  // Default, will update based on window
        self.autoScroll = YES;     // Auto-scroll enabled by default

        // Initialize layout offsets (default: no offset)
        self.layoutOffsetX = 0.0f;
        self.layoutOffsetY = 0.0f;

        NSLog(@"CoreText: Initializing cursor and colors");
        // Initialize cursor and colors based on layer type
        self.cursorX = 0;
        self.cursorY = 0;

        // Initialize visual cursor (for rendering)
        self.visualCursorX = 0;
        self.visualCursorY = 0;
        self.visualCursorVisible = NO;
        self.visualCursorBlink = 0.0f;

        if (isEditor) {
            // Editor layer (Layer 6): White text on transparent background (for REPL)
            self.currentInk = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);    // White
            self.currentPaper = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f);  // Transparent
        } else {
            // Terminal layer (Layer 5): White text on transparent background
            self.currentInk = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);    // White
            self.currentPaper = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f);  // Transparent
        }

        // Default font settings
        self.fontSize = 16.0f;
        self.atlasWidth = 2048;  // Larger atlas for CoreText (better quality)
        self.atlasHeight = 2048;
        self.atlasX = 0;
        self.atlasY = 0;
        self.atlasRowHeight = 0;

        NSLog(@"CoreText: Initializing glyph cache and atlas");
        // Initialize glyph cache
        self.glyphCache = [NSMutableDictionary dictionary];

        // Initialize atlas data (grayscale)
        self.atlasData = [NSMutableData dataWithLength:self.atlasWidth * self.atlasHeight];
        memset([self.atlasData mutableBytes], 0, self.atlasWidth * self.atlasHeight);

        NSLog(@"CoreText: Calling clear method");
        [self clear];
        NSLog(@"CoreText: initWithDevice completed successfully");
    }
    return self;
}

- (void)dealloc {
    if (self.textGrid) {
        free(self.textGrid);
    }
    if (self.font) {
        CFRelease(self.font);
    }
    [super dealloc];
}

- (BOOL)loadFont:(NSString*)fontPath fontSize:(float)size {
    self.fontSize = size;
    NSLog(@"CoreText: loadFont called with path: %@, size: %.1f", fontPath, size);

    // Load font from file
    NSURL* fontURL = [NSURL fileURLWithPath:fontPath];
    NSLog(@"CoreText: Creating font descriptors from URL...");
    CFArrayRef descriptors = CTFontManagerCreateFontDescriptorsFromURL((__bridge CFURLRef)fontURL);

    if (!descriptors || CFArrayGetCount(descriptors) == 0) {
        NSLog(@"CoreText ERROR: Could not load font from %@", fontPath);
        if (descriptors) CFRelease(descriptors);
        return NO;
    }

    NSLog(@"CoreText: Creating CTFont from descriptor...");
    CTFontDescriptorRef descriptor = (CTFontDescriptorRef)CFArrayGetValueAtIndex(descriptors, 0);
    self.font = CTFontCreateWithFontDescriptor(descriptor, size, NULL);
    CFRelease(descriptors);

    if (!self.font) {
        NSLog(@"CoreText ERROR: Failed to create CTFont from %@", fontPath);
        return NO;
    }

    NSLog(@"CoreText: Initializing font metrics...");
    [self initializeFontMetrics];

    NSLog(@"CoreText: Creating font atlas...");
    [self createFontAtlas];

    NSLog(@"CoreText: Creating render pipeline...");
    [self createRenderPipeline];

    NSLog(@"CoreText: Creating vertex buffer...");
    [self createVertexBuffer];

    NSLog(@"CoreText font loaded successfully: %@, size=%.1f, charWidth=%.1f, charHeight=%.1f",
          fontPath, size, self.charWidth, self.charHeight);

    return YES;
}

- (BOOL)loadFontByName:(NSString*)fontName fontSize:(float)size {
    self.fontSize = size;
    NSLog(@"CoreText: loadFontByName called with name: %@, size: %.1f", fontName, size);

    // Create font by name
    NSLog(@"CoreText: Creating CTFont by name...");
    self.font = CTFontCreateWithName((__bridge CFStringRef)fontName, size, NULL);

    if (!self.font) {
        NSLog(@"CoreText ERROR: Failed to create CTFont with name: %@", fontName);
        return NO;
    }

    NSLog(@"CoreText: Initializing font metrics...");
    [self initializeFontMetrics];

    NSLog(@"CoreText: Creating font atlas...");
    [self createFontAtlas];

    NSLog(@"CoreText: Creating render pipeline...");
    [self createRenderPipeline];

    NSLog(@"CoreText: Creating vertex buffer...");
    [self createVertexBuffer];

    NSLog(@"CoreText font loaded successfully: %@, size=%.1f, charWidth=%.1f, charHeight=%.1f",
          fontName, size, self.charWidth, self.charHeight);

    return YES;
}

- (BOOL)reloadFontAtSize:(float)size windowWidth:(float)windowWidth windowHeight:(float)windowHeight {
    // Calculate actual font size to fill the window based on current text mode
    TextModeConfig* config = &g_text_mode_configs[g_current_text_mode];

    // Calculate what font size would fill the window
    float charWidthNeeded = windowWidth / config->columns;
    float charHeightNeeded = windowHeight / config->rows;

    // Use the smaller dimension to ensure everything fits
    float calculatedSize = fmin(charWidthNeeded, charHeightNeeded) * 0.95f; // 95% to leave small margin

    self.fontSize = calculatedSize;
    NSLog(@"CoreText: reloadFontAtSize called - window %.0fx%.0f, mode %dx%d, calculated font size: %.1f",
          windowWidth, windowHeight, config->columns, config->rows, calculatedSize);

    // Release old font
    if (self.font) {
        CFRelease(self.font);
        self.font = NULL;
    }

    // Clear glyph cache
    [self.glyphCache removeAllObjects];

    // Reset atlas position
    self.atlasX = 0;
    self.atlasY = 0;
    self.atlasRowHeight = 0;

    // Clear atlas data
    memset([self.atlasData mutableBytes], 0, self.atlasWidth * self.atlasHeight);

    // Try to find font file again
    NSString* fontPath = nil;
    NSString* bundlePath = [[NSBundle mainBundle] pathForResource:@"PetMe64" ofType:@"ttf" inDirectory:@"assets/fonts/petme"];
    if (bundlePath && [[NSFileManager defaultManager] fileExistsAtPath:bundlePath]) {
        fontPath = bundlePath;
    } else {
        // Try without subdirectory
        bundlePath = [[NSBundle mainBundle] pathForResource:@"PetMe64" ofType:@"ttf"];
        if (bundlePath && [[NSFileManager defaultManager] fileExistsAtPath:bundlePath]) {
            fontPath = bundlePath;
        }
    }

    if (!fontPath) {
        NSArray* searchPaths = @[
            @"../Resources/assets/fonts/petme/PetMe64.ttf",
            @"assets/fonts/petme/PetMe64.ttf",
            @"../assets/fonts/petme/PetMe64.ttf",
            @"../../assets/fonts/petme/PetMe64.ttf",
            @"assets/PetMe64.ttf",
            @"../assets/PetMe64.ttf",
            @"../../assets/PetMe64.ttf",
        ];

        for (NSString* path in searchPaths) {
            if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
                fontPath = path;
                break;
            }
        }
    }

    BOOL success = NO;
    if (fontPath) {
        NSLog(@"CoreText: Reloading font from file: %@", fontPath);
        NSURL* fontURL = [NSURL fileURLWithPath:fontPath];
        CFArrayRef descriptors = CTFontManagerCreateFontDescriptorsFromURL((__bridge CFURLRef)fontURL);

        if (descriptors && CFArrayGetCount(descriptors) > 0) {
            CTFontDescriptorRef descriptor = (CTFontDescriptorRef)CFArrayGetValueAtIndex(descriptors, 0);
            self.font = CTFontCreateWithFontDescriptor(descriptor, calculatedSize, NULL);
            CFRelease(descriptors);
            success = (self.font != NULL);
        }
    } else {
        NSLog(@"CoreText: Font file not found, reloading Monaco");
        self.font = CTFontCreateWithName((__bridge CFStringRef)@"Monaco", calculatedSize, NULL);
        success = (self.font != NULL);
    }

    if (success) {
        [self initializeFontMetrics];
        [self createFontAtlas];

        // Upload updated atlas to Metal texture
        if (self.fontTexture) {
            MTLRegion region = MTLRegionMake2D(0, 0, self.atlasWidth, self.atlasHeight);
            [self.fontTexture replaceRegion:region
                                mipmapLevel:0
                                  withBytes:[self.atlasData bytes]
                                bytesPerRow:self.atlasWidth];
        }

        NSLog(@"CoreText: Font reloaded successfully - size %.1fpt, char dimensions %.1fx%.1f",
              calculatedSize, self.charWidth, self.charHeight);
    } else {
        NSLog(@"CoreText ERROR: Failed to reload font at size %.1f", calculatedSize);
    }

    return success;
}

- (void)initializeFontMetrics {
    NSLog(@"CoreText: Getting font metrics...");
    // Get font metrics
    CGFloat ascent = CTFontGetAscent(self.font);
    CGFloat descent = CTFontGetDescent(self.font);
    CGFloat leading = CTFontGetLeading(self.font);

    // Calculate base character height without extra leading
    self.charHeight = ceil(ascent + descent + leading);
    self.baseline = ceil(ascent);
    NSLog(@"CoreText: ascent=%.1f, descent=%.1f, leading=%.1f, charHeight=%.1f",
          ascent, descent, leading, self.charHeight);

    // Get average character width (use 'M' as reference)
    CGGlyph glyph;
    unichar m = 'M';
    CTFontGetGlyphsForCharacters(self.font, &m, &glyph, 1);

    CGSize advance;
    CTFontGetAdvancesForGlyphs(self.font, kCTFontOrientationDefault, &glyph, &advance, 1);
    self.charWidth = ceil(advance.width);

    // Ensure minimum dimensions
    if (self.charWidth < 8.0f) self.charWidth = 8.0f;
    if (self.charHeight < 12.0f) self.charHeight = 12.0f;

    // Calculate maximum glyph height by sampling a tall character
    // This will be used to position paper rectangles correctly
    unichar tallChar = 'M';
    CGGlyph tallGlyph;
    CTFontGetGlyphsForCharacters(self.font, &tallChar, &tallGlyph, 1);
    CGRect tallBoundingRect = CTFontGetBoundingRectsForGlyphs(self.font, kCTFontOrientationDefault, &tallGlyph, NULL, 1);
    self.maxGlyphHeight = ceil(tallBoundingRect.size.height) + 4.0f; // Add padding for rendering

    // Add extra leading for line spacing AFTER base calculations
    // Scale leading proportionally to font size (12pt=5px, scales up/down from there)
    float leadingScale = self.fontSize / 12.2f;
    float extraLeading = ceil(5.0f * leadingScale);
    self.charHeight += extraLeading;

    NSLog(@"CoreText: charWidth=%.1f, charHeight=%.1f (with %.0fpx leading), maxGlyphHeight=%.1f",
          self.charWidth, self.charHeight, extraLeading, self.maxGlyphHeight);
}

- (void)createFontAtlas {
    NSLog(@"CoreText: Pre-caching ASCII characters 32-126...");
    // Pre-cache common ASCII characters
    for (unichar ch = 32; ch <= 126; ch++) {
        [self addGlyphToAtlas:ch];
    }
    NSLog(@"CoreText: Cached %lu glyphs", (unsigned long)[self.glyphCache count]);

    // Create Metal texture from atlas
    NSLog(@"CoreText: Creating Metal texture descriptor %dx%d...", self.atlasWidth, self.atlasHeight);
    MTLTextureDescriptor* textureDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
        width:self.atlasWidth
        height:self.atlasHeight
        mipmapped:NO];
    textureDesc.usage = MTLTextureUsageShaderRead;

    NSLog(@"CoreText: Creating Metal texture...");
    self.fontTexture = [self.device newTextureWithDescriptor:textureDesc];

    if (!self.fontTexture) {
        NSLog(@"CoreText ERROR: Failed to create Metal texture!");
        return;
    }

    // Upload initial atlas data
    NSLog(@"CoreText: Uploading atlas data to Metal texture...");
    MTLRegion region = MTLRegionMake2D(0, 0, self.atlasWidth, self.atlasHeight);
    [self.fontTexture replaceRegion:region
                        mipmapLevel:0
                          withBytes:[self.atlasData bytes]
                        bytesPerRow:self.atlasWidth];

    NSLog(@"CoreText atlas created successfully: %dx%d, cached %lu glyphs",
          self.atlasWidth, self.atlasHeight, (unsigned long)[self.glyphCache count]);
}

- (GlyphCacheEntry)addGlyphToAtlas:(unichar)character {
    // Check cache first
    NSNumber* key = @(character);
    NSValue* cachedValue = self.glyphCache[key];
    if (cachedValue) {
        GlyphCacheEntry entry;
        [cachedValue getValue:&entry];
        return entry;
    }

    // Get glyph from CoreText
    CGGlyph glyph;
    if (!CTFontGetGlyphsForCharacters(self.font, &character, &glyph, 1)) {
        // Character not in font, return empty entry
        GlyphCacheEntry empty = {CGRectZero, CGSizeZero, CGPointZero, false};
        return empty;
    }

    // Get glyph bounds and advance
    CGRect boundingRect;
    CTFontGetBoundingRectsForGlyphs(self.font, kCTFontOrientationHorizontal, &glyph, &boundingRect, 1);

    CGSize advance;
    CTFontGetAdvancesForGlyphs(self.font, kCTFontOrientationHorizontal, &glyph, &advance, 1);

    // Calculate glyph dimensions with padding (2 pixels on each side)
    int glyphWidth = (int)ceil(boundingRect.size.width) + 4;
    int glyphHeight = (int)ceil(boundingRect.size.height) + 4;

    // Check if we need a new row
    if (self.atlasX + glyphWidth > self.atlasWidth) {
        self.atlasX = 0;
        self.atlasY += self.atlasRowHeight;
        self.atlasRowHeight = 0;
    }

    // Check if atlas is full
    if (self.atlasY + glyphHeight > self.atlasHeight) {
        NSLog(@"Warning: Font atlas full, cannot add character %d", (int)character);
        GlyphCacheEntry empty = {CGRectZero, CGSizeZero, CGPointZero, false};
        return empty;
    }

    // Create bitmap context to render glyph
    unsigned char* glyphBitmap = (unsigned char*)calloc(glyphWidth * glyphHeight, 1);

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceGray();
    CGContextRef context = CGBitmapContextCreate(
        glyphBitmap, glyphWidth, glyphHeight,
        8, glyphWidth,
        colorSpace, kCGImageAlphaNone
    );

    // Set high quality rendering
    CGContextSetShouldAntialias(context, true);
    CGContextSetShouldSmoothFonts(context, true);
    CGContextSetAllowsFontSmoothing(context, true);
    CGContextSetShouldSubpixelPositionFonts(context, true);
    CGContextSetShouldSubpixelQuantizeFonts(context, false);

    // Draw glyph - position relative to bounding box
    CGContextSetGrayFillColor(context, 1.0, 1.0);
    // Position at (2, 2) offset minus the bounding rect origin
    CGPoint position = CGPointMake(2.0 - boundingRect.origin.x,
                                   2.0 - boundingRect.origin.y);
    CTFontDrawGlyphs(self.font, &glyph, &position, 1, context);

    // Copy to atlas
    unsigned char* atlasBytes = (unsigned char*)[self.atlasData mutableBytes];
    for (int y = 0; y < glyphHeight; y++) {
        int atlasRow = self.atlasY + y;
        int atlasOffset = atlasRow * self.atlasWidth + self.atlasX;
        memcpy(atlasBytes + atlasOffset, glyphBitmap + y * glyphWidth, glyphWidth);
    }

    CGContextRelease(context);
    CGColorSpaceRelease(colorSpace);
    free(glyphBitmap);

    // Create cache entry
    // Store the bounding box offset so we can position the glyph correctly when rendering
    GlyphCacheEntry entry;
    entry.atlasRect = CGRectMake(self.atlasX, self.atlasY, glyphWidth, glyphHeight);
    entry.advance = advance;
    // Store the offset needed to position this glyph relative to the baseline
    entry.offset = CGPointMake(boundingRect.origin.x, boundingRect.origin.y);
    entry.valid = true;

    // Store in cache
    NSValue* value = [NSValue valueWithBytes:&entry objCType:@encode(GlyphCacheEntry)];
    self.glyphCache[key] = value;

    // Update atlas position
    self.atlasX += glyphWidth;
    if (glyphHeight > self.atlasRowHeight) {
        self.atlasRowHeight = glyphHeight;
    }

    return entry;
}

- (void)createRenderPipeline {
    NSError* error = nil;

    // Create shader source (same as TrueType for compatibility)
    NSString* shaderSource = @""
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct VertexIn {\n"
    "    float2 position [[attribute(0)]];\n"
    "    float2 texCoord [[attribute(1)]];\n"
    "    float4 inkColor [[attribute(2)]];\n"
    "    float4 paperColor [[attribute(3)]];\n"
    "    uint unicode [[attribute(4)]];\n"
    "    float2 gridPos [[attribute(5)]];\n"
    "};\n"
    "\n"
    "struct VertexOut {\n"
    "    float4 position [[position]];\n"
    "    float2 texCoord;\n"
    "    float4 inkColor;\n"
    "    float4 paperColor;\n"
    "    float2 gridPos;\n"
    "};\n"
    "\n"
    "struct Uniforms {\n"
    "    float4x4 projectionMatrix;\n"
    "    float4x4 modelViewMatrix;\n"
    "    float2 screenSize;\n"
    "    float time;\n"
    "    float deltaTime;\n"
    "};\n"
    "\n"
    "struct TextUniforms {\n"
    "    float2 gridSize;\n"
    "    float2 cellSize;\n"
    "    float cursorBlink;\n"
    "    bool crtGlowEnabled;\n"
    "    bool crtScanlinesEnabled;\n"
    "    float crtGlowIntensity;\n"
    "    float crtScanlineIntensity;\n"
    "    int cursorX;\n"
    "    int cursorY;\n"
    "    bool cursorVisible;\n"
    "};\n"
    "\n"
    "vertex VertexOut text_vertex(VertexIn in [[stage_in]],\n"
    "                             constant Uniforms& uniforms [[buffer(1)]],\n"
    "                             constant TextUniforms& textUniforms [[buffer(2)]]) {\n"
    "    VertexOut out;\n"
    "    \n"
    "    // Use pre-calculated screen position from vertex data\n"
    "    float4 screenPos = float4(in.position, 0.0, 1.0);\n"
    "    out.position = uniforms.projectionMatrix * uniforms.modelViewMatrix * screenPos;\n"
    "    \n"
    "    out.texCoord = in.texCoord;\n"
    "    out.inkColor = in.inkColor;\n"
    "    out.paperColor = in.paperColor;\n"
    "    out.gridPos = in.gridPos;\n"
    "    return out;\n"
    "}\n"
    "\n"
    "fragment float4 text_fragment(VertexOut in [[stage_in]],\n"
    "                              constant TextUniforms& textUniforms [[buffer(2)]],\n"
    "                              texture2d<float> fontTexture [[texture(0)]]) {\n"
    "    constexpr sampler textureSampler(mag_filter::linear, min_filter::linear);\n"
    "    \n"
    "    float alpha = fontTexture.sample(textureSampler, in.texCoord).r;\n"
    "    float finalAlpha = alpha;\n"
    "    \n"
    "    // CRT Glow Effect - sample neighboring pixels for phosphor bloom\n"
    "    if (textUniforms.crtGlowEnabled && alpha > 0.01) {\n"
    "        float2 texelSize = 1.0 / float2(fontTexture.get_width(), fontTexture.get_height());\n"
    "        float glowRadius = 1.2;\n"
    "        float intensity = textUniforms.crtGlowIntensity;\n"
    "        \n"
    "        // Sample 8 directions for glow\n"
    "        float glow = alpha;\n"
    "        glow += fontTexture.sample(textureSampler, in.texCoord + float2(glowRadius * texelSize.x, 0)).r * 0.5;\n"
    "        glow += fontTexture.sample(textureSampler, in.texCoord + float2(-glowRadius * texelSize.x, 0)).r * 0.5;\n"
    "        glow += fontTexture.sample(textureSampler, in.texCoord + float2(0, glowRadius * texelSize.y)).r * 0.5;\n"
    "        glow += fontTexture.sample(textureSampler, in.texCoord + float2(0, -glowRadius * texelSize.y)).r * 0.5;\n"
    "        glow += fontTexture.sample(textureSampler, in.texCoord + float2(glowRadius * texelSize.x, glowRadius * texelSize.y)).r * 0.3;\n"
    "        glow += fontTexture.sample(textureSampler, in.texCoord + float2(-glowRadius * texelSize.x, glowRadius * texelSize.y)).r * 0.3;\n"
    "        glow += fontTexture.sample(textureSampler, in.texCoord + float2(glowRadius * texelSize.x, -glowRadius * texelSize.y)).r * 0.3;\n"
    "        glow += fontTexture.sample(textureSampler, in.texCoord + float2(-glowRadius * texelSize.x, -glowRadius * texelSize.y)).r * 0.3;\n"
    "        \n"
    "        // Blend original alpha with glow\n"
    "        finalAlpha = mix(alpha, glow * 0.35, intensity);\n"
    "    }\n"
    "    \n"
    "    // Check if this is a background-only quad (negative paper alpha)\n"
    "    if (in.paperColor.a < 0.0) {\n"
    "        // Background-only quad - always render paper color\n"
    "        float4 bgColor = in.paperColor;\n"
    "        bgColor.a = -bgColor.a; // Make alpha positive\n"
    "        \n"
    "        // Apply scanlines to background if enabled\n"
    "        if (textUniforms.crtScanlinesEnabled) {\n"
    "            float scanline = sin(in.position.y * 3.14159 * 2.0) * 0.5 + 0.5;\n"
    "            float scanlineFactor = 1.0 - (scanline * textUniforms.crtScanlineIntensity);\n"
    "            bgColor.rgb *= scanlineFactor;\n"
    "        }\n"
    "        \n"
    "        return bgColor;\n"
    "    }\n"
    "    \n"
    "    // Blend paper and ink colors based on glyph alpha\n"
    "    float4 finalColor;\n"
    "    if (finalAlpha > 0.01) {\n"
    "        // Glyph pixel - use ink color\n"
    "        finalColor = float4(in.inkColor.rgb, in.inkColor.a * finalAlpha);\n"
    "    } else {\n"
    "        // Background pixel - use paper color\n"
    "        finalColor = in.paperColor;\n"
    "    }\n"
    "    \n"
    "    // Apply scanlines to final color if enabled\n"
    "    if (textUniforms.crtScanlinesEnabled) {\n"
    "        float scanline = sin(in.position.y * 3.14159 * 2.0) * 0.5 + 0.5;\n"
    "        float scanlineFactor = 1.0 - (scanline * textUniforms.crtScanlineIntensity);\n"
    "        finalColor.rgb *= scanlineFactor;\n"
    "    }\n"
    "    \n"
    "    // Cursor is now rendered separately as screen overlay\n"
    "    \n"
    "    return finalColor;\n"
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

    vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[0].offset = 0;
    vertexDescriptor.attributes[0].bufferIndex = 0;

    vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[1].offset = sizeof(simd_float2);
    vertexDescriptor.attributes[1].bufferIndex = 0;

    vertexDescriptor.attributes[2].format = MTLVertexFormatFloat4;
    vertexDescriptor.attributes[2].offset = sizeof(simd_float2) + sizeof(simd_float2);
    vertexDescriptor.attributes[2].bufferIndex = 0;

    vertexDescriptor.attributes[3].format = MTLVertexFormatFloat4;
    vertexDescriptor.attributes[3].offset = sizeof(simd_float2) + sizeof(simd_float2) + sizeof(simd_float4);
    vertexDescriptor.attributes[3].bufferIndex = 0;

    vertexDescriptor.attributes[4].format = MTLVertexFormatUInt;
    vertexDescriptor.attributes[4].offset = sizeof(simd_float2) + sizeof(simd_float2) + sizeof(simd_float4) + sizeof(simd_float4);
    vertexDescriptor.attributes[4].bufferIndex = 0;

    vertexDescriptor.attributes[5].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[5].offset = sizeof(simd_float2) + sizeof(simd_float2) + sizeof(simd_float4) + sizeof(simd_float4) + sizeof(uint32_t);
    vertexDescriptor.attributes[5].bufferIndex = 0;

    vertexDescriptor.layouts[0].stride = sizeof(struct TextVertex);
    vertexDescriptor.layouts[0].stepRate = 1;
    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    // Create render pipeline descriptor
    MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = fragmentFunction;
    pipelineDescriptor.vertexDescriptor = vertexDescriptor;
    pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    // Enable blending for text
    pipelineDescriptor.colorAttachments[0].blendingEnabled = YES;
    pipelineDescriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pipelineDescriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pipelineDescriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDescriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDescriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipelineDescriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    self.pipelineState = [self.device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (!self.pipelineState) {
        NSLog(@"Failed to create render pipeline state: %@", error.localizedDescription);
    }

    [pipelineDescriptor release];
    [vertexDescriptor release];
}

- (void)createVertexBuffer {
    // Only allocate buffer for visible viewport, not entire 2000-line buffer
    // Maximum 100 visible lines should be more than enough for any window size
    size_t bufferSize = BUFFER_WIDTH * 100 * 6 * sizeof(struct TextVertex);
    self.vertexBuffer = [self.device newBufferWithLength:bufferSize options:MTLResourceStorageModeShared];

    // Create uniform buffer for cursor and text mode info
    struct TextUniforms {
        simd_float2 gridSize;
        simd_float2 cellSize;
        float cursorBlink;
        bool crtGlowEnabled;
        bool crtScanlinesEnabled;
        float crtGlowIntensity;
        float crtScanlineIntensity;
        int cursorX, cursorY;
        bool cursorVisible;
    };
    size_t uniformBufferSize = sizeof(struct TextUniforms);
    self.uniformBuffer = [self.device newBufferWithLength:uniformBufferSize options:MTLResourceStorageModeShared];

    // Create main uniforms buffer for projection matrices
    struct Uniforms {
        simd_float4x4 projectionMatrix;
        simd_float4x4 modelViewMatrix;
        simd_float2 screenSize;
        float time;
        float deltaTime;
    };
    size_t mainUniformBufferSize = sizeof(struct Uniforms);
    self.mainUniformBuffer = [self.device newBufferWithLength:mainUniformBufferSize options:MTLResourceStorageModeShared];
}

- (void)scrollUp {
    // Scroll the screen up one line
    // Move lines 1..GRID_HEIGHT-1 to lines 0..GRID_HEIGHT-2
    for (int y = 0; y < GRID_HEIGHT - 1; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            int destIndex = y * GRID_WIDTH + x;
            int srcIndex = (y + 1) * GRID_WIDTH + x;
            self.textGrid[destIndex] = self.textGrid[srcIndex];
        }
    }

    // Clear the last line
    simd_float4 clearPaper = self.isEditorLayer ? self.currentPaper : simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (int x = 0; x < GRID_WIDTH; x++) {
        int index = (GRID_HEIGHT - 1) * GRID_WIDTH + x;
        self.textGrid[index].character = ' ';
        self.textGrid[index].inkColor = self.currentInk;
        self.textGrid[index].paperColor = clearPaper;
    }
}

- (void)print:(NSString*)text {
    NSUInteger length = [text length];

    // Process character by character for proper terminal behavior
    for (NSUInteger i = 0; i < length; i++) {
        unichar ch = [text characterAtIndex:i];

        if (ch == '\n') {
            // Newline: move to start of next line
            self.cursorX = 0;
            self.cursorY++;

            // Handle buffer overflow - scroll entire buffer up
            if (self.cursorY >= BUFFER_HEIGHT) {
                // Move all lines up by 100 lines to free space at bottom (efficient memcpy)
                const int scrollAmount = 100;
                memmove(self.textGrid,
                       self.textGrid + (scrollAmount * BUFFER_WIDTH),
                       (BUFFER_HEIGHT - scrollAmount) * BUFFER_WIDTH * sizeof(struct TextCell));

                // Clear the newly freed lines at bottom (efficient memset)
                memset(self.textGrid + (BUFFER_HEIGHT - scrollAmount) * BUFFER_WIDTH,
                       0,
                       scrollAmount * BUFFER_WIDTH * sizeof(struct TextCell));

                // Adjust cursor and viewport
                self.cursorY = BUFFER_HEIGHT - scrollAmount;
                if (self.viewportStartLine >= scrollAmount) {
                    self.viewportStartLine -= scrollAmount;
                } else {
                    self.viewportStartLine = 0;
                }
            }

            // Auto-scroll viewport to follow cursor if enabled
            if (self.autoScroll && self.cursorY >= self.viewportStartLine + self.viewportHeight) {
                self.viewportStartLine = self.cursorY - self.viewportHeight + 1;
                if (self.viewportStartLine < 0) self.viewportStartLine = 0;
            }
        } else if (ch == '\r') {
            // Carriage return: move to start of current line
            self.cursorX = 0;
        } else {
            // Regular character: print at cursor position
            int index = self.cursorY * BUFFER_WIDTH + self.cursorX;
            self.textGrid[index].character = ch;
            self.textGrid[index].inkColor = self.currentInk;
            self.textGrid[index].paperColor = self.currentPaper;

            // Advance cursor
            self.cursorX++;

            // Wrap to next line if we reach the right edge
            if (self.cursorX >= BUFFER_WIDTH) {
                self.cursorX = 0;
                self.cursorY++;

                // Handle buffer overflow on wrap
                if (self.cursorY >= BUFFER_HEIGHT) {
                    const int scrollAmount = 100;
                    memmove(self.textGrid,
                           self.textGrid + (scrollAmount * BUFFER_WIDTH),
                           (BUFFER_HEIGHT - scrollAmount) * BUFFER_WIDTH * sizeof(struct TextCell));
                    memset(self.textGrid + (BUFFER_HEIGHT - scrollAmount) * BUFFER_WIDTH,
                           0,
                           scrollAmount * BUFFER_WIDTH * sizeof(struct TextCell));
                    self.cursorY = BUFFER_HEIGHT - scrollAmount;
                    if (self.viewportStartLine >= scrollAmount) {
                        self.viewportStartLine -= scrollAmount;
                    } else {
                        self.viewportStartLine = 0;
                    }
                }

                // Auto-scroll viewport
                if (self.autoScroll && self.cursorY >= self.viewportStartLine + self.viewportHeight) {
                    self.viewportStartLine = self.cursorY - self.viewportHeight + 1;
                    if (self.viewportStartLine < 0) self.viewportStartLine = 0;
                }
            }
        }
    }

    // Update layer-specific state after cursor moved
    if (self.isEditorLayer) {
        g_editor_state.cursorX = self.cursorX;
        g_editor_state.cursorY = self.cursorY;
    } else {
        g_terminal_state.cursorX = self.cursorX;
        g_terminal_state.cursorY = self.cursorY;
    }
}

- (void)printAt:(int)x y:(int)y text:(NSString*)text {
    // y is relative to viewport - convert to absolute buffer position
    int absoluteY = self.viewportStartLine + y;

    if (x < 0 || x >= BUFFER_WIDTH || absoluteY < 0 || absoluteY >= BUFFER_HEIGHT) return;

    self.cursorX = x;
    self.cursorY = absoluteY;

    // Update layer state when cursor position set
    if (self.isEditorLayer) {
        g_editor_state.cursorX = x;
        g_editor_state.cursorY = absoluteY;
    } else {
        g_terminal_state.cursorX = x;
        g_terminal_state.cursorY = absoluteY;
    }

    [self print:text];
}

- (void)clear {
    NSLog(@"CoreText: clear called");
    // For terminal layer (not editor), always use transparent paper on clear
    // This keeps layer 5 transparent by default so other layers are visible
    simd_float4 clearPaper = self.isEditorLayer ? self.currentPaper : simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f);

    // Performance: Zero out entire buffer efficiently
    NSLog(@"CoreText: Zeroing buffer with memset");
    memset(self.textGrid, 0, BUFFER_WIDTH * BUFFER_HEIGHT * sizeof(struct TextCell));
    NSLog(@"CoreText: Buffer zeroed");

    // Only initialize visible viewport with spaces (for rendering)
    int visibleCells = BUFFER_WIDTH * (self.viewportHeight > 0 ? self.viewportHeight : 60);
    NSLog(@"CoreText: Initializing %d visible cells", visibleCells);
    for (int i = 0; i < visibleCells; i++) {
        self.textGrid[i].character = ' ';
        self.textGrid[i].inkColor = self.currentInk;
        self.textGrid[i].paperColor = clearPaper;
    }
    NSLog(@"CoreText: Visible cells initialized");

    self.cursorX = 0;
    self.cursorY = 0;

    // Reset viewport to top
    self.viewportStartLine = 0;

    NSLog(@"CoreText: Saving cursor state");
    // Save cursor position to layer-specific state
    if (self.isEditorLayer) {
        g_editor_state.cursorX = 0;
        g_editor_state.cursorY = 0;
    } else {
        g_terminal_state.cursorX = 0;
        g_terminal_state.cursorY = 0;
    }
    NSLog(@"CoreText: clear completed");
}

- (void)home {
    self.cursorX = 0;
    self.cursorY = 0;

    // Save cursor position to layer-specific state
    if (self.isEditorLayer) {
        g_editor_state.cursorX = 0;
        g_editor_state.cursorY = 0;
    } else {
        g_terminal_state.cursorX = 0;
        g_terminal_state.cursorY = 0;
    }
}

- (void)renderWithEncoder:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport {
    if (!self.pipelineState || !self.vertexBuffer || !self.fontTexture || !self.uniformBuffer) return;

    // Generate vertices from text grid
    struct TextVertex* vertices = (struct TextVertex*)[self.vertexBuffer contents];
    int vertexCount = 0;

    // USE TEXTGRIDMANAGER VALUES - Single source of truth!
    // charWidth and charHeight should already be set by coretext_set_cell_size()
    // which is called from MetalRenderer when TextGridManager recalculates

    // Get grid dimensions from TextGridManager
    int activeColumns, activeRows;
    float cellW, cellH;
    text_grid_get_dimensions(&activeColumns, &activeRows);
    text_grid_get_cell_size(&cellW, &cellH);

    // Use TextGridManager's values - they already include gap-filling logic
    // If charWidth/charHeight haven't been set yet, use the manager's values
    if (self.charWidth == 0.0f || self.charHeight == 0.0f) {
        self.charWidth = cellW;
        self.charHeight = cellH;
    }

    // Debug rendering coordinates
    static BOOL debugOnce = NO;
    if (!debugOnce) {
        debugOnce = YES;
        FILE *debugFile = fopen("/tmp/superterminal_debug.log", "a");
        if (debugFile) {
            fprintf(debugFile, "\n=== CORETEXTRENDERER USING TEXTGRIDMANAGER ===\n");
            fprintf(debugFile, "VIEWPORT: %.0fx%.0f points\n", viewport.width, viewport.height);
            fprintf(debugFile, "GRID FROM MANAGER: %dx%d\n", activeColumns, activeRows);
            fprintf(debugFile, "CELL SIZE FROM MANAGER: %.1fx%.1f points\n", cellW, cellH);
            fprintf(debugFile, "RENDERER charWidth/Height: %.1fx%.1f points\n", self.charWidth, self.charHeight);
            fprintf(debugFile, "TOTAL RENDER AREA: %.0fx%.0f points\n",
                    activeColumns * self.charWidth, activeRows * self.charHeight);
            fprintf(debugFile, "=============================================\n");
            fflush(debugFile);
            fclose(debugFile);
        }
    }

    // Two-pass rendering: backgrounds first, then glyphs
    // Pass 1: Render background for all cells with extended height to cover glyph extents
    // Performance optimization: only render the visible viewport region from scrollback buffer
    const int bufferStartLine = self.viewportStartLine;
    const int bufferEndLine = MIN(bufferStartLine + activeRows, BUFFER_HEIGHT);

    int startRow = self.viewportStartRow;
    int endRow = MIN(startRow + self.viewportRowCount, activeRows);

    // Sextant character range for chunky pixel mode
    #define SEXTANT_BASE 0x1FB00
    #define SEXTANT_MAX  0x1FB3F

    for (int bufferY = bufferStartLine; bufferY < bufferEndLine; bufferY++) {
        int screenY_idx = bufferY - bufferStartLine;  // Convert to screen coordinates
        if (screenY_idx < startRow || screenY_idx >= endRow) continue;

        for (int x = 0; x < activeColumns && x < BUFFER_WIDTH; x++) {
            int index = bufferY * BUFFER_WIDTH + x;
            struct TextCell* cell = &self.textGrid[index];

            // Skip completely empty cells
            if (cell->character == 0) continue;

            // Calculate screen position (grid-based)
            // Apply layout offsets for proper centering in viewport
            // Subtract baseline offset to eliminate gap at top of screen
            float screenX = x * self.charWidth + self.layoutOffsetX;
            float screenY = screenY_idx * self.charHeight - (self.baseline - self.maxGlyphHeight) + self.layoutOffsetY;

            // Check if this is a sextant character (chunky pixel mode)
            bool isSextant = (cell->character >= SEXTANT_BASE && cell->character <= SEXTANT_MAX);

            if (isSextant) {
                // Debug log first sextant character found
                static bool logged_first = false;
                if (!logged_first) {
                    FILE* debugFile = fopen("/tmp/superterminal_chunky_debug.log", "a");
                    if (debugFile) {
                        fprintf(debugFile, "RENDER: Found sextant char 0x%X at (%d,%d) ink=(%.2f,%.2f,%.2f,%.2f) paper=(%.2f,%.2f,%.2f,%.2f)\n",
                                cell->character, x, screenY_idx,
                                cell->inkColor.x, cell->inkColor.y, cell->inkColor.z, cell->inkColor.w,
                                cell->paperColor.x, cell->paperColor.y, cell->paperColor.z, cell->paperColor.w);
                        fflush(debugFile);
                        fclose(debugFile);
                    }
                    logged_first = true;
                }

                // Chunky pixel mode: render 2x3 sextant pattern
                uint8_t pattern = (uint8_t)(cell->character - SEXTANT_BASE);

                // Sextant bit layout (2 columns x 3 rows):
                // Bit 5 4  (top row)
                // Bit 3 2  (middle row)
                // Bit 1 0  (bottom row)

                // Calculate sextant pixel dimensions
                float pixelWidth = self.charWidth / 2.0f;
                float pixelHeight = self.charHeight / 3.0f;

                // Adjust Y to align with cell top (same as paper background)
                float baseY = screenY + self.baseline - self.maxGlyphHeight;

                simd_float2 gridPosition = simd_make_float2(x, screenY_idx);

                // Render 6 sub-pixels based on bit pattern
                for (int row = 0; row < 3; row++) {
                    for (int col = 0; col < 2; col++) {
                        int bitIndex = row * 2 + col;
                        bool pixelOn = (pattern >> bitIndex) & 1;

                        // Choose color based on pixel state
                        simd_float4 pixelColor = pixelOn ? cell->inkColor : cell->paperColor;

                        // Skip fully transparent pixels
                        if (pixelColor.w == 0.0f) continue;

                        // Negate alpha for background-only rendering
                        pixelColor.w = -fabs(pixelColor.w);

                        // Calculate sub-pixel position
                        float px = screenX + (col * pixelWidth);
                        float py = baseY + (row * pixelHeight);

                        struct TextVertex pixelQuad[6] = {
                            {{px, py}, {0, 0}, cell->inkColor, pixelColor, cell->character, gridPosition},
                            {{px + pixelWidth, py}, {0, 0}, cell->inkColor, pixelColor, cell->character, gridPosition},
                            {{px, py + pixelHeight}, {0, 0}, cell->inkColor, pixelColor, cell->character, gridPosition},
                            {{px, py + pixelHeight}, {0, 0}, cell->inkColor, pixelColor, cell->character, gridPosition},
                            {{px + pixelWidth, py}, {0, 0}, cell->inkColor, pixelColor, cell->character, gridPosition},
                            {{px + pixelWidth, py + pixelHeight}, {0, 0}, cell->inkColor, pixelColor, cell->character, gridPosition}
                        };

                        memcpy(&vertices[vertexCount], pixelQuad, sizeof(pixelQuad));
                        vertexCount += 6;
                    }
                }

                // Skip normal background rendering for sextant characters
                continue;
            }

            // Debug first character rendering
            if (x == 0 && screenY_idx == startRow && cell->character != 0) {
                FILE *debugFile = fopen("/tmp/superterminal_debug.log", "a");
                if (debugFile) {
                    fprintf(debugFile, "\n=== FIRST CHAR RENDER DEBUG ===\n");
                    fprintf(debugFile, "CHAR: '%c' at grid (%d,%d)\n", cell->character, x, screenY_idx);
                    fprintf(debugFile, "SCREEN POS: (%.1f,%.1f)\n", screenX, screenY);
                    fprintf(debugFile, "CELL SIZE: %.1fx%.1f\n", self.charWidth, self.charHeight);
                    fprintf(debugFile, "VIEWPORT: %.0fx%.0f pixels\n", viewport.width, viewport.height);
                    fprintf(debugFile, "QUAD: (%.1f,%.1f) to (%.1f,%.1f)\n",
                           screenX, screenY, screenX + self.charWidth, screenY + self.charHeight);
                    fprintf(debugFile, "===============================\n");
                    fflush(debugFile);
                    fclose(debugFile);
                }
            }

            // PAPER RECTANGLE SETTINGS (dynamically calculated for each text mode)
            // ========================================================================
            // Problem: Glyphs extend above their cell origin due to ascenders
            // Solution: Position paper to start where glyphs start, not where cells start
            //
            // Dynamic calculations based on actual font metrics:
            //   - maxGlyphHeight: measured from tallest character + rendering padding
            //   - baseline: where glyphs sit (from CTFontGetAscent)
            //   - charHeight: base height + scaled leading (proportional to font size)
            //
            // Glyph positioning formula:
            //   quadY = screenY + baseline - atlasHeight
            //   (Glyphs extend ABOVE cell origin by: baseline - atlasHeight)
            //
            // Paper rectangle settings:
            //   - bgY: screenY + baseline - maxGlyphHeight
            //     (Start paper where tallest glyphs start)
            //   - bgHeight: charHeight
            //     (Papers touch edge-to-edge with no gap, no overlap)
            //
            // Result (scales across all text modes):
            //   - Papers start at same Y as glyphs ✓
            //   - Papers touch exactly at line boundaries ✓
            //   - All glyphs (caps, lowercase, descenders, spaces) covered uniformly ✓
            // Note: bgY and bgHeight will be recalculated with scaling in vertex creation

            // Skip background quad entirely if paper is transparent (alpha = 0)
            // This allows layers below to show through
            if (cell->paperColor.w > 0.0f) {
                // Render full-cell background quad with paper color
                // Use negative alpha as signal to shader that this is a background-only quad
                simd_float4 bgPaperColor = cell->paperColor;
                // Negate the alpha to signal background-only rendering (preserve RGB values)
                bgPaperColor.w = -fabs(bgPaperColor.w);

                // ========================================================================
                float bgY = screenY + self.baseline - self.maxGlyphHeight;
                float bgHeight = self.charHeight;

                simd_float2 gridPosition = simd_make_float2(x, screenY_idx);
                struct TextVertex bgQuad[6] = {
                    {{screenX, bgY}, {0, 0}, cell->inkColor, bgPaperColor, cell->character, gridPosition},
                    {{screenX + self.charWidth, bgY}, {0, 0}, cell->inkColor, bgPaperColor, cell->character, gridPosition},
                    {{screenX, bgY + bgHeight}, {0, 0}, cell->inkColor, bgPaperColor, cell->character, gridPosition},
                    {{screenX, bgY + bgHeight}, {0, 0}, cell->inkColor, bgPaperColor, cell->character, gridPosition},
                    {{screenX + self.charWidth, bgY}, {0, 0}, cell->inkColor, bgPaperColor, cell->character, gridPosition},
                    {{screenX + self.charWidth, bgY + bgHeight}, {0, 0}, cell->inkColor, bgPaperColor, cell->character, gridPosition}
                };

                // Debug logging disabled to reduce verbosity
                // (Enable if needed for debugging background quad rendering issues)

                memcpy(&vertices[vertexCount], bgQuad, sizeof(bgQuad));
                vertexCount += 6;
            }
        }
    }

    // Pass 2: Render glyphs for non-space characters
    // Only render rows within the viewport from scrollback buffer
    for (int bufferY = bufferStartLine; bufferY < bufferEndLine; bufferY++) {
        int screenY_idx = bufferY - bufferStartLine;  // Convert to screen coordinates
        if (screenY_idx < startRow || screenY_idx >= endRow) continue;

        for (int x = 0; x < activeColumns && x < BUFFER_WIDTH; x++) {
            int index = bufferY * BUFFER_WIDTH + x;
            struct TextCell* cell = &self.textGrid[index];

            // Skip spaces and empty cells
            if (cell->character == ' ' || cell->character == 0) continue;

            // Skip sextant characters (already rendered in pass 1)
            if (cell->character >= SEXTANT_BASE && cell->character <= SEXTANT_MAX) continue;

            // Get glyph from cache (add if needed)
            GlyphCacheEntry entry = [self addGlyphToAtlas:cell->character];
            if (!entry.valid) continue;

            // Calculate screen position (grid-based)
            // Apply layout offsets for proper centering in viewport
            // Subtract baseline offset to eliminate gap at top of screen
            float screenX = x * self.charWidth + self.layoutOffsetX;
            float screenY = screenY_idx * self.charHeight - (self.baseline - self.maxGlyphHeight) + self.layoutOffsetY;

            // Calculate texture coordinates
            float u0 = entry.atlasRect.origin.x / (float)self.atlasWidth;
            float v0 = entry.atlasRect.origin.y / (float)self.atlasHeight;
            float u1 = (entry.atlasRect.origin.x + entry.atlasRect.size.width) / (float)self.atlasWidth;
            float v1 = (entry.atlasRect.origin.y + entry.atlasRect.size.height) / (float)self.atlasHeight;

            // Position glyph correctly:
            // CoreText gives us boundingRect.origin which is relative to baseline (Y+ up from baseline)
            // In screen coords (Y+ down), the baseline should be at: screenY + ascent
            // For a glyph at baseline: offset.y = 0, we want its TOP at screenY + ascent - glyphHeight
            // For a descender like 'g': offset.y = negative, so it goes LOWER (more Y)
            // Formula: quadY = screenY + ascent - offset.y - entry.atlasRect.size.height
            float quadX = screenX + entry.offset.x;
            float quadY = screenY + self.baseline - entry.offset.y - entry.atlasRect.size.height;
            float quadW = entry.atlasRect.size.width;
            float quadH = entry.atlasRect.size.height;

            simd_float4 paperColor = cell->paperColor;

            // Debug logging disabled to reduce verbosity
            // (Enable if needed for debugging glyph rendering issues)

            // Generate 6 vertices (2 triangles) for the glyph
            simd_float2 gridPosition = simd_make_float2(x, screenY_idx);

            // Debug: Log gridPos for first few characters to verify
            static int debugCount = 0;
            if (debugCount < 10) {
                printf("[VERTEX DEBUG] Char '%c' at grid (%d,%d) - gridPos=(%.0f,%.0f)\n",
                       (char)cell->character, x, screenY_idx, gridPosition.x, gridPosition.y);
                debugCount++;
                fflush(stdout);
            }

            struct TextVertex quad[6] = {
                {{quadX, quadY}, {u0, v0}, cell->inkColor, paperColor, cell->character, gridPosition},
                {{quadX + quadW, quadY}, {u1, v0}, cell->inkColor, paperColor, cell->character, gridPosition},
                {{quadX, quadY + quadH}, {u0, v1}, cell->inkColor, paperColor, cell->character, gridPosition},
                {{quadX, quadY + quadH}, {u0, v1}, cell->inkColor, paperColor, cell->character, gridPosition},
                {{quadX + quadW, quadY}, {u1, v0}, cell->inkColor, paperColor, cell->character, gridPosition},
                {{quadX + quadW, quadY + quadH}, {u1, v1}, cell->inkColor, paperColor, cell->character, gridPosition}
            };

            memcpy(&vertices[vertexCount], quad, sizeof(quad));
            vertexCount += 6;
        }
    }

    if (vertexCount == 0) return;

    // Update uniform buffer with cursor information
    struct TextUniforms {
        simd_float2 gridSize;       // Current text mode dimensions
        simd_float2 cellSize;       // Size of each character cell in pixels
        float cursorBlink;          // Cursor blink phase (0-1)
        bool crtGlowEnabled;        // CRT glow effect enabled
        bool crtScanlinesEnabled;   // CRT scanlines effect enabled
        float crtGlowIntensity;     // CRT glow intensity (0-1)
        float crtScanlineIntensity; // CRT scanline intensity (0-1)
        int cursorX, cursorY;       // Cursor position in grid coordinates
        bool cursorVisible;         // Cursor visibility
    };

    struct TextUniforms* uniforms = (struct TextUniforms*)[self.uniformBuffer contents];
    uniforms->gridSize = simd_make_float2(text_mode_get_columns(), text_mode_get_rows());
    uniforms->cellSize = simd_make_float2(self.charWidth, self.charHeight);
    uniforms->cursorBlink = self.visualCursorBlink;
    uniforms->crtGlowEnabled = g_crt_glow_enabled;
    uniforms->crtScanlinesEnabled = g_crt_scanlines_enabled;
    uniforms->crtGlowIntensity = g_crt_glow_intensity;
    uniforms->crtScanlineIntensity = g_crt_scanline_intensity;
    uniforms->cursorX = self.visualCursorX;
    uniforms->cursorY = self.visualCursorY;
    uniforms->cursorVisible = self.visualCursorVisible;

    static int renderFrameCount = 0;
    if (renderFrameCount % 60 == 0) {  // Log every 60 frames to reduce spam
        printf("[CURSOR DEBUG RENDER] Frame %d: Uniforms: cursorX=%d, cursorY=%d, visible=%d, blink=%.3f\n",
              renderFrameCount, uniforms->cursorX, uniforms->cursorY, uniforms->cursorVisible ? 1 : 0, uniforms->cursorBlink);
        fflush(stdout);
    }
    renderFrameCount++;

    // Update main uniforms buffer with projection matrices
    struct Uniforms {
        simd_float4x4 projectionMatrix;
        simd_float4x4 modelViewMatrix;
        simd_float2 screenSize;
        float time;
        float deltaTime;
    };

    struct Uniforms* mainUniforms = (struct Uniforms*)[self.mainUniformBuffer contents];

    // Create orthographic projection matrix for screen-space rendering
    // Calculate text coordinate space size
    float textSpaceWidth = activeColumns * self.charWidth;
    float textSpaceHeight = activeRows * self.charHeight;

    // Map text coordinate space to viewport
    float left = 0.0f;
    float right = textSpaceWidth;
    float bottom = textSpaceHeight;
    float top = 0.0f;
    float nearZ = -1.0f;
    float farZ = 1.0f;

    simd_float4x4 projection = {
        .columns[0] = { 2.0f / (right - left), 0.0f, 0.0f, 0.0f },
        .columns[1] = { 0.0f, 2.0f / (top - bottom), 0.0f, 0.0f },
        .columns[2] = { 0.0f, 0.0f, -2.0f / (farZ - nearZ), 0.0f },
        .columns[3] = { -(right + left) / (right - left), -(top + bottom) / (top - bottom), -(farZ + nearZ) / (farZ - nearZ), 1.0f }
    };

    simd_float4x4 identity = matrix_identity_float4x4;

    mainUniforms->projectionMatrix = projection;
    mainUniforms->modelViewMatrix = identity;
    mainUniforms->screenSize = simd_make_float2(viewport.width, viewport.height);
    mainUniforms->time = 0.0f;
    mainUniforms->deltaTime = 0.0f;

    // Render
    [encoder setRenderPipelineState:self.pipelineState];
    [encoder setVertexBuffer:self.vertexBuffer offset:0 atIndex:0];

    // Pass main uniforms buffer to both vertex and fragment shaders at buffer index 1 (matches shader [[buffer(1)]])
    [encoder setVertexBuffer:self.mainUniformBuffer offset:0 atIndex:1];
    [encoder setFragmentBuffer:self.mainUniformBuffer offset:0 atIndex:1];

    // Pass text uniforms buffer to both vertex and fragment shaders at buffer index 2 (matches shader [[buffer(2)]])
    [encoder setVertexBuffer:self.uniformBuffer offset:0 atIndex:2];
    [encoder setFragmentBuffer:self.uniformBuffer offset:0 atIndex:2];

    [encoder setFragmentTexture:self.fontTexture atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertexCount];
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

// Scrollback buffer methods implementation
- (void)locateLine:(int)line {
    if (line >= 0 && line < BUFFER_HEIGHT) {
        self.cursorY = line;
        self.cursorX = 0;  // Reset to start of line
    }
}

- (void)scrollToLine:(int)line {
    // Clamp to valid range
    int maxStart = BUFFER_HEIGHT - self.viewportHeight;
    if (maxStart < 0) maxStart = 0;

    self.viewportStartLine = line;
    if (self.viewportStartLine < 0) self.viewportStartLine = 0;
    if (self.viewportStartLine > maxStart) self.viewportStartLine = maxStart;

    // Disable auto-scroll when manually scrolling
    self.autoScroll = NO;
}

- (void)scrollUp:(int)lines {
    [self scrollToLine:self.viewportStartLine - lines];
}

- (void)scrollDown:(int)lines {
    [self scrollToLine:self.viewportStartLine + lines];
}

- (void)pageUp {
    [self scrollUp:self.viewportHeight];
}

- (void)pageDown {
    [self scrollDown:self.viewportHeight];
}

- (void)scrollToTop {
    [self scrollToLine:0];
}

- (void)scrollToBottom {
    // Scroll to show cursor at bottom of viewport
    int targetLine = self.cursorY - self.viewportHeight + 1;
    if (targetLine < 0) targetLine = 0;

    self.viewportStartLine = targetLine;
    self.autoScroll = YES;  // Re-enable auto-scroll
}

- (int)getCursorLine {
    return self.cursorY;
}

- (int)getCursorColumn {
    return self.cursorX;
}

- (int)getViewportLine {
    return self.viewportStartLine;
}

- (int)getViewportHeight {
    return self.viewportHeight;
}

- (void)setAutoScroll:(BOOL)enabled {
    _autoScroll = enabled;
}

- (BOOL)getAutoScroll {
    return _autoScroll;
}

@end

// Global screen cursor state (shared across all layers)
struct ScreenCursor {
    int x;
    int y;
    bool visible;
    float blinkPhase;
    simd_float4 color;
};

static ScreenCursor g_screenCursor = {
    .x = 0,
    .y = 0,
    .visible = true,
    .blinkPhase = 0.0f,
    .color = {1.0f, 1.0f, 0.0f, 0.5f}  // Semi-transparent yellow
};

// C API Implementation
extern "C" {
    static CoreTextLayer* g_terminalTextLayer = nil;
    static CoreTextLayer* g_editorTextLayer = nil;

    // Forward declaration
    struct TextCell* coretext_editor_get_text_buffer(void);

    // Text mode management functions
    bool text_mode_set(TextMode mode) {
        if (mode < 0 || mode >= 7) {
            NSLog(@"CoreText: Invalid text mode %d", mode);
            return false;
        }

        FILE *debugFile = fopen("/tmp/superterminal_debug.log", "a");
        if (debugFile) {
            fprintf(debugFile, "\n=== TEXT_MODE_SET CALLED ===\n");
            fprintf(debugFile, "SWITCHING TO MODE: %d\n", (int)mode);
            fflush(debugFile);
            fclose(debugFile);
        }

        NSLog(@"CoreText: Switching to text mode %d", mode);
        g_current_text_mode = mode;

        // Save mode to the currently active layer's state
        extern bool editor_is_active(void);
        if (editor_is_active()) {
            g_editor_state.textMode = mode;
            NSLog(@"CoreText: Saved mode %d to editor state", mode);
        } else {
            g_terminal_state.textMode = mode;
            NSLog(@"CoreText: Saved mode %d to terminal state", mode);
        }

        TextModeConfig* config = &g_text_mode_configs[mode];

        // CRITICAL: Update TextGridManager mode to match (they use the same enum now)
        extern void text_grid_set_mode(TextVideoMode mode);
        text_grid_set_mode((TextVideoMode)mode);

        // Get the actual current window size from the main window
        float windowWidth = 1024.0f;
        float windowHeight = 768.0f;

        NSWindow* mainWindow = [[NSApplication sharedApplication] mainWindow];
        if (mainWindow) {
            NSRect contentRect = [mainWindow contentRectForFrameRect:[mainWindow frame]];
            windowWidth = contentRect.size.width;
            windowHeight = contentRect.size.height;
        }

        debugFile = fopen("/tmp/superterminal_debug.log", "a");
        if (debugFile) {
            fprintf(debugFile, "ACTUAL WINDOW SIZE: %.0fx%.0f\n", windowWidth, windowHeight);
            fprintf(debugFile, "TARGET MODE CONFIG: %s (%dx%d, %.1fpt)\n",
                   config->name, config->columns, config->rows, config->fontSize);
            fflush(debugFile);
            fclose(debugFile);
        }

        // CRITICAL: Trigger TextGridManager recalculation for new mode
        extern void text_grid_recalculate_for_viewport(float width, float height);
        extern void text_grid_force_update(void);
        text_grid_recalculate_for_viewport(windowWidth, windowHeight);
        text_grid_force_update();

        // Get updated grid info and set CoreText cell sizes
        extern void text_grid_get_dimensions(int* width, int* height);
        extern void text_grid_get_cell_size(float* width, float* height);

        int gridW, gridH;
        float cellW, cellH;
        text_grid_get_dimensions(&gridW, &gridH);
        text_grid_get_cell_size(&cellW, &cellH);

        // Set the calculated cell sizes in CoreText layers
        coretext_set_cell_size(cellW, cellH);

        debugFile = fopen("/tmp/superterminal_debug.log", "a");
        if (debugFile) {
            fprintf(debugFile, "TEXTGRID RESULT: %dx%d grid, %.1fx%.1f cells\n",
                   gridW, gridH, cellW, cellH);
            fprintf(debugFile, "============================\n");
            fflush(debugFile);
            fclose(debugFile);
        }

        BOOL success = YES;
        if (g_terminalTextLayer) {
            success = [g_terminalTextLayer reloadFontAtSize:config->fontSize
                                                windowWidth:windowWidth
                                               windowHeight:windowHeight] && success;
        }

        if (g_editorTextLayer) {
            success = [g_editorTextLayer reloadFontAtSize:config->fontSize
                                              windowWidth:windowWidth
                                             windowHeight:windowHeight] && success;
        }

        // Save preference
        [[NSUserDefaults standardUserDefaults] setInteger:mode forKey:@"TextMode"];
        [[NSUserDefaults standardUserDefaults] synchronize];

        if (success) {
            NSLog(@"CoreText: Text mode switched to %s (%d×%d, fills window)",
                  config->name, config->columns, config->rows);

            // If editor is active, clear and redisplay the text
            extern bool editor_is_active(void);
            extern void editor_force_redraw(void);

            if (editor_is_active()) {
                NSLog(@"CoreText: Editor is active, forcing redraw after mode change");
                editor_force_redraw();
            }
        } else {
            NSLog(@"CoreText ERROR: Failed to switch text mode");
        }

        return success;
    }

    TextMode text_mode_get(void) {
        return g_current_text_mode;
    }

    bool text_mode_set_for_layer(TextMode mode, int layer) {
        if (mode < 0 || mode >= 7) {
            NSLog(@"CoreText: Invalid text mode %d for layer %d", mode, layer);
            return false;
        }

        NSLog(@"CoreText: Setting text mode %d for layer %d", mode, layer);
        TextModeConfig* config = &g_text_mode_configs[mode];

        // Save to layer-specific state
        if (layer == 5) {
            g_terminal_state.textMode = mode;
        } else if (layer == 6) {
            g_editor_state.textMode = mode;
        }

        // Update global mode (for now - could be layer-independent in future)
        g_current_text_mode = mode;

        // Get window size
        float windowWidth = 1024.0f;
        float windowHeight = 768.0f;
        NSWindow* mainWindow = [[NSApplication sharedApplication] mainWindow];
        if (mainWindow) {
            NSRect contentRect = [mainWindow contentRectForFrameRect:[mainWindow frame]];
            windowWidth = contentRect.size.width;
            windowHeight = contentRect.size.height;
        }

        // Update TextGridManager
        extern void text_grid_set_mode(TextVideoMode mode);
        extern void text_grid_recalculate_for_viewport(float width, float height);
        extern void text_grid_force_update(void);
        text_grid_set_mode((TextVideoMode)mode);
        text_grid_recalculate_for_viewport(windowWidth, windowHeight);
        text_grid_force_update();

        // Get grid dimensions
        extern void text_grid_get_dimensions(int* width, int* height);
        extern void text_grid_get_cell_size(float* width, float* height);
        int gridW, gridH;
        float cellW, cellH;
        text_grid_get_dimensions(&gridW, &gridH);
        text_grid_get_cell_size(&cellW, &cellH);
        coretext_set_cell_size(cellW, cellH);

        // Reload font for the specific layer
        BOOL success = YES;
        if (layer == 5 && g_terminalTextLayer) {
            success = [g_terminalTextLayer reloadFontAtSize:config->fontSize
                                                windowWidth:windowWidth
                                               windowHeight:windowHeight];
            NSLog(@"CoreText: Layer 5 (terminal) font reloaded for mode %s", config->name);
        } else if (layer == 6 && g_editorTextLayer) {
            success = [g_editorTextLayer reloadFontAtSize:config->fontSize
                                              windowWidth:windowWidth
                                             windowHeight:windowHeight];
            NSLog(@"CoreText: Layer 6 (editor) font reloaded for mode %s", config->name);
        }

        return success;
    }

    TextMode text_mode_get_for_layer(int layer) {
        if (layer == 5) {
            return g_terminal_state.textMode;
        } else if (layer == 6) {
            return g_editor_state.textMode;
        }
        return g_current_text_mode;
    }

    void text_mode_save_layer_state(int layer) {
        @autoreleasepool {
            if (layer == 5 && g_terminalTextLayer) {
                g_terminal_state.cursorX = g_terminalTextLayer.cursorX;
                g_terminal_state.cursorY = g_terminalTextLayer.cursorY;
                NSLog(@"CoreText: Saved terminal state - cursor(%d,%d) mode=%d",
                      g_terminal_state.cursorX, g_terminal_state.cursorY, g_terminal_state.textMode);
            } else if (layer == 6 && g_editorTextLayer) {
                g_editor_state.cursorX = g_editorTextLayer.cursorX;
                g_editor_state.cursorY = g_editorTextLayer.cursorY;
                NSLog(@"CoreText: Saved editor state - cursor(%d,%d) mode=%d",
                      g_editor_state.cursorX, g_editor_state.cursorY, g_editor_state.textMode);
            }
        }
    }

    void text_mode_restore_layer_state(int layer) {
        @autoreleasepool {
            if (layer == 5 && g_terminalTextLayer) {
                NSLog(@"CoreText: Restoring terminal state - cursor(%d,%d) mode=%d",
                      g_terminal_state.cursorX, g_terminal_state.cursorY, g_terminal_state.textMode);

                // Always apply terminal's text mode to ensure correct mode is active
                text_mode_set_for_layer(g_terminal_state.textMode, 5);

                // Then restore cursor position (after mode change which might reset cursor)
                g_terminalTextLayer.cursorX = g_terminal_state.cursorX;
                g_terminalTextLayer.cursorY = g_terminal_state.cursorY;

                NSLog(@"CoreText: Terminal state fully restored");
            } else if (layer == 6 && g_editorTextLayer) {
                NSLog(@"CoreText: Restoring editor state - cursor(%d,%d) mode=%d",
                      g_editor_state.cursorX, g_editor_state.cursorY, g_editor_state.textMode);

                // Always apply editor's text mode to ensure correct mode is active
                text_mode_set_for_layer(g_editor_state.textMode, 6);

                // Then restore cursor position (after mode change which might reset cursor)
                g_editorTextLayer.cursorX = g_editor_state.cursorX;
                g_editorTextLayer.cursorY = g_editor_state.cursorY;

                NSLog(@"CoreText: Editor state fully restored");
            }
        }
    }

    int text_mode_get_columns(void) {
        return g_text_mode_configs[g_current_text_mode].columns;
    }

    int text_mode_get_rows(void) {
        return g_text_mode_configs[g_current_text_mode].rows;
    }

    float text_mode_get_font_size(void) {
        return g_text_mode_configs[g_current_text_mode].fontSize;
    }

    const char* text_mode_get_name(void) {
        return g_text_mode_configs[g_current_text_mode].name;
    }

    const char* text_mode_get_description(void) {
        return g_text_mode_configs[g_current_text_mode].description;
    }

    TextModeConfig text_mode_get_config(TextMode mode) {
        if (mode < 0 || mode >= 7) {
            return g_text_mode_configs[TEXT_MODE_64x44]; // Return default
        }
        return g_text_mode_configs[mode];
    }

    void truetype_text_layers_init(void* device) {
        @autoreleasepool {
            NSLog(@"CoreText: truetype_text_layers_init called");
            id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;

            // Create terminal layer (Layer 5)
            NSLog(@"CoreText: Creating terminal text layer...");
            g_terminalTextLayer = [[CoreTextLayer alloc] initWithDevice:metalDevice isEditorLayer:NO];
            NSLog(@"CoreText: Terminal text layer created successfully: %p", g_terminalTextLayer);

            // Create editor layer (Layer 6)
            NSLog(@"CoreText: Creating editor text layer...");
            g_editorTextLayer = [[CoreTextLayer alloc] initWithDevice:metalDevice isEditorLayer:YES];
            NSLog(@"CoreText: Editor text layer created successfully: %p", g_editorTextLayer);

            // Search for font file
            NSLog(@"CoreText: Searching for font file...");
            NSString* fontPath = nil;

            // Try bundle path first with subdirectory
            NSString* bundlePath = [[NSBundle mainBundle] pathForResource:@"PetMe64" ofType:@"ttf" inDirectory:@"assets/fonts/petme"];
            if (bundlePath && [[NSFileManager defaultManager] fileExistsAtPath:bundlePath]) {
                fontPath = bundlePath;
                NSLog(@"CoreText: Found font in bundle: %@", fontPath);
            } else {
                // Try without subdirectory
                bundlePath = [[NSBundle mainBundle] pathForResource:@"PetMe64" ofType:@"ttf"];
                if (bundlePath && [[NSFileManager defaultManager] fileExistsAtPath:bundlePath]) {
                    fontPath = bundlePath;
                    NSLog(@"CoreText: Found font in bundle root: %@", fontPath);
                }
            }

            if (!fontPath) {
                // Try relative paths from executable
                NSArray* searchPaths = @[
                    @"../Resources/assets/fonts/petme/PetMe64.ttf",
                    @"assets/fonts/petme/PetMe64.ttf",
                    @"../assets/fonts/petme/PetMe64.ttf",
                    @"../../assets/fonts/petme/PetMe64.ttf",
                    @"assets/PetMe64.ttf",
                    @"../assets/PetMe64.ttf",
                    @"../../assets/PetMe64.ttf",
                ];

                for (NSString* path in searchPaths) {
                    if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
                        fontPath = path;
                        NSLog(@"CoreText: Found font at: %@", path);
                        break;
                    }
                }
            }

            BOOL success = YES;

            // Check for saved text mode preference
            NSInteger savedMode = [[NSUserDefaults standardUserDefaults] integerForKey:@"TextMode"];
            if (savedMode >= 0 && savedMode < 7) {
                g_current_text_mode = (TextMode)savedMode;
                NSLog(@"CoreText: Restored saved text mode: %s", g_text_mode_configs[savedMode].name);
            }

            TextModeConfig* currentConfig = &g_text_mode_configs[g_current_text_mode];

            // Calculate font size to fill window based on text mode
            float windowWidth = 1024.0f;
            float windowHeight = 768.0f;
            float charWidthNeeded = windowWidth / currentConfig->columns;
            float charHeightNeeded = windowHeight / currentConfig->rows;
            float fontSize = fmin(charWidthNeeded, charHeightNeeded) * 0.95f; // 95% to leave margin

            NSLog(@"CoreText: Initializing with mode %s (%d×%d, %.1fpt fills window)",
                  currentConfig->name, currentConfig->columns, currentConfig->rows, fontSize);

            if (fontPath) {
                NSLog(@"CoreText: Loading font from file: %@", fontPath);
                success = [g_terminalTextLayer loadFont:fontPath fontSize:fontSize] &&
                         [g_editorTextLayer loadFont:fontPath fontSize:fontSize];
            } else {
                // Fallback to system monospace font
                NSLog(@"CoreText: Font file not found, using system font Monaco");
                success = [g_terminalTextLayer loadFontByName:@"Monaco" fontSize:fontSize] &&
                         [g_editorTextLayer loadFontByName:@"Monaco" fontSize:fontSize];
            }

            if (success) {
                NSLog(@"CoreText: Dual text layers initialized successfully!");
            } else {
                NSLog(@"CoreText ERROR: Failed to initialize text layers!");
            }
        }
    }

    bool coretext_text_layers_init(void* device, const char* fontPath, float fontSize) {
        @autoreleasepool {
            id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
            NSString* path = fontPath ? [NSString stringWithUTF8String:fontPath] : nil;

            // Create terminal layer (Layer 5)
            g_terminalTextLayer = [[CoreTextLayer alloc] initWithDevice:metalDevice isEditorLayer:NO];

            // Create editor layer (Layer 6)
            g_editorTextLayer = [[CoreTextLayer alloc] initWithDevice:metalDevice isEditorLayer:YES];

            BOOL success = YES;
            if (path) {
                success = [g_terminalTextLayer loadFont:path fontSize:fontSize] &&
                         [g_editorTextLayer loadFont:path fontSize:fontSize];
            } else {
                // Fallback to system monospace font
                success = [g_terminalTextLayer loadFontByName:@"Monaco" fontSize:fontSize] &&
                         [g_editorTextLayer loadFontByName:@"Monaco" fontSize:fontSize];
            }

            if (success) {
                NSLog(@"CoreText dual text layers initialized successfully");

                // Initialize layers with their default text modes (just save to state, don't apply)
                NSLog(@"CoreText: Initializing default text modes - Terminal: 64×44, Editor: 80×50");

                // Just set the layer state defaults - don't call text_mode_set_for_layer which applies globally
                g_terminal_state.textMode = TEXT_MODE_64x44;
                g_editor_state.textMode = TEXT_MODE_80x50;

                // Start with terminal mode as the active global mode
                g_current_text_mode = TEXT_MODE_64x44;

                // Apply terminal's default mode as the starting mode
                TextModeConfig* config = &g_text_mode_configs[TEXT_MODE_64x44];
                extern void text_grid_set_mode(TextVideoMode mode);
                text_grid_set_mode((TextVideoMode)TEXT_MODE_64x44);

                NSLog(@"CoreText: Default layer states initialized - Terminal: 64×44, Editor: 80×50");
            }
            return success;
        }
    }

    void coretext_editor_set_viewport(int startRow, int rowCount) {
        @autoreleasepool {
            if (g_editorTextLayer) {
                NSLog(@"CoreTextRenderer: Setting editor viewport to rows %d-%d (count=%d)", startRow, startRow + rowCount - 1, rowCount);
                g_editorTextLayer.viewportStartRow = startRow;
                g_editorTextLayer.viewportRowCount = rowCount;
            }
        }
    }

    void coretext_editor_set_viewport_with_layout(int startRow, int rowCount,
                                                 float offsetX, float offsetY,
                                                 float cellWidth, float cellHeight) {
        @autoreleasepool {
            if (g_editorTextLayer) {
                FILE *debugFile = fopen("/tmp/superterminal_debug.log", "a");
                if (debugFile) {
                    fprintf(debugFile, "\n=== CORETEXT EDITOR VIEWPORT DEBUG ===\n");
                    fprintf(debugFile, "VIEWPORT: rows %d-%d (count=%d)\n", startRow, startRow + rowCount - 1, rowCount);
                    fprintf(debugFile, "LAYOUT OFFSET: (%.1f, %.1f)\n", offsetX, offsetY);
                    fprintf(debugFile, "REQUESTED CELL SIZE: %.1fx%.1f\n", cellWidth, cellHeight);
                    fprintf(debugFile, "CURRENT CORETEXT CELL SIZE: %.1fx%.1f\n", g_editorTextLayer.charWidth, g_editorTextLayer.charHeight);
                    fprintf(debugFile, "=====================================\n");
                    fflush(debugFile);
                    fclose(debugFile);
                }

                g_editorTextLayer.viewportStartRow = startRow;
                g_editorTextLayer.viewportRowCount = rowCount;
                g_editorTextLayer.layoutOffsetX = offsetX;
                g_editorTextLayer.layoutOffsetY = offsetY;
            }
        }
    }

    void coretext_set_cell_size(float cellWidth, float cellHeight) {
        @autoreleasepool {
            // Simply set the calculated cell sizes - no extra scaling
            if (g_terminalTextLayer) {
                g_terminalTextLayer.charWidth = cellWidth;
                g_terminalTextLayer.charHeight = cellHeight;
            }
            if (g_editorTextLayer) {
                g_editorTextLayer.charWidth = cellWidth;
                g_editorTextLayer.charHeight = cellHeight;
            }

            FILE *debugFile = fopen("/tmp/superterminal_debug.log", "a");
            if (debugFile) {
                fprintf(debugFile, "CORETEXT: Set cell size to %.1fx%.1f\n", cellWidth, cellHeight);
                fflush(debugFile);
                fclose(debugFile);
            }
        }
    }

    void coretext_terminal_set_cell_size(float cellWidth, float cellHeight) {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                g_terminalTextLayer.charWidth = cellWidth;
                g_terminalTextLayer.charHeight = cellHeight;
            }
        }
    }

    void coretext_editor_set_cell_size(float cellWidth, float cellHeight) {
        @autoreleasepool {
            if (g_editorTextLayer) {
                g_editorTextLayer.charWidth = cellWidth;
                g_editorTextLayer.charHeight = cellHeight;
            }
        }
    }

    bool coretext_terminal_reload_font(float fontSize, float windowWidth, float windowHeight) {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                BOOL success = [g_terminalTextLayer reloadFontAtSize:fontSize
                                                         windowWidth:windowWidth
                                                        windowHeight:windowHeight];
                return success ? true : false;
            }
            return false;
        }
    }

    bool coretext_editor_reload_font(float fontSize, float windowWidth, float windowHeight) {
        @autoreleasepool {
            if (g_editorTextLayer) {
                BOOL success = [g_editorTextLayer reloadFontAtSize:fontSize
                                                       windowWidth:windowWidth
                                                      windowHeight:windowHeight];
                return success ? true : false;
            }
            return false;
        }
    }

    void coretext_terminal_print(const char* text) {
        @autoreleasepool {
            if (g_terminalTextLayer && text) {
                NSString* nsText = [NSString stringWithUTF8String:text];
                [g_terminalTextLayer print:nsText];
            }
        }
    }

    void coretext_terminal_print_at(int x, int y, const char* text) {
        @autoreleasepool {
            if (g_terminalTextLayer && text) {
                NSString* nsText = [NSString stringWithUTF8String:text];
                [g_terminalTextLayer printAt:x y:y text:nsText];
            }
        }
    }

    void coretext_terminal_clear() {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                [g_terminalTextLayer clear];
            }
        }
    }

    void coretext_terminal_home() {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                [g_terminalTextLayer home];
            }
        }
    }

    void coretext_terminal_render(void* encoder, float width, float height) {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
                [g_terminalTextLayer renderWithEncoder:metalEncoder viewport:CGSizeMake(width, height)];

                // Render screen cursor on top
                screen_cursor_render(encoder, width, height);
            }
        }
    }

    void screen_cursor_render(void* encoder, float width, float height) {
        @autoreleasepool {
            if (!g_screenCursor.visible) return;

            // Check blink phase
            float blinkPhase = fmod(g_screenCursor.blinkPhase, 1.0);
            if (blinkPhase >= 0.5) return; // Hide during second half of blink cycle

            // Get active text layer for cell size
            CoreTextLayer* activeLayer = g_editorTextLayer ? g_editorTextLayer : g_terminalTextLayer;
            if (!activeLayer) return;

            id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;

            // Get cell size and grid dimensions from TextGridManager
            float cellWidth, cellHeight;
            int gridWidth, gridHeight;
            text_grid_get_cell_size(&cellWidth, &cellHeight);
            text_grid_get_dimensions(&gridWidth, &gridHeight);

            // Calculate cursor position in TEXT SPACE coordinates (same as text vertices)
            float cursorX = g_screenCursor.x * cellWidth;
            float cursorY = g_screenCursor.y * cellHeight;

            // Create vertex data for cursor quad with glow in text space
            // Expand the quad to allow room for the glow effect
            float glowPadding = cellWidth * 0.5f; // Extra space for glow
            struct CursorVertex {
                simd_float2 position;
                simd_float2 texCoord;
            };

            // Texture coordinates: 0-1 represents the actual cursor, padding allows glow
            float texLeft = glowPadding / (cellWidth + 2.0f * glowPadding);
            float texRight = (glowPadding + cellWidth) / (cellWidth + 2.0f * glowPadding);
            float texTop = glowPadding / (cellHeight + 2.0f * glowPadding);
            float texBottom = (glowPadding + cellHeight) / (cellHeight + 2.0f * glowPadding);

            struct CursorVertex cursorQuad[6] = {
                // Triangle 1
                {{cursorX - glowPadding, cursorY - glowPadding}, {0.0f, 0.0f}},
                {{cursorX + cellWidth + glowPadding, cursorY - glowPadding}, {1.0f, 0.0f}},
                {{cursorX - glowPadding, cursorY + cellHeight + glowPadding}, {0.0f, 1.0f}},
                // Triangle 2
                {{cursorX - glowPadding, cursorY + cellHeight + glowPadding}, {0.0f, 1.0f}},
                {{cursorX + cellWidth + glowPadding, cursorY - glowPadding}, {1.0f, 0.0f}},
                {{cursorX + cellWidth + glowPadding, cursorY + cellHeight + glowPadding}, {1.0f, 1.0f}}
            };

            // Create vertex buffer
            id<MTLBuffer> cursorBuffer = [activeLayer.device newBufferWithBytes:cursorQuad
                                                                         length:sizeof(cursorQuad)
                                                                        options:MTLResourceStorageModeShared];

            // Create projection matrix (same as text rendering)
            float textSpaceWidth = gridWidth * cellWidth;
            float textSpaceHeight = gridHeight * cellHeight;

            float left = 0.0f;
            float right = textSpaceWidth;
            float bottom = textSpaceHeight;
            float top = 0.0f;
            float nearZ = -1.0f;
            float farZ = 1.0f;

            simd_float4x4 projection = {
                .columns[0] = { 2.0f / (right - left), 0.0f, 0.0f, 0.0f },
                .columns[1] = { 0.0f, 2.0f / (top - bottom), 0.0f, 0.0f },
                .columns[2] = { 0.0f, 0.0f, -2.0f / (farZ - nearZ), 0.0f },
                .columns[3] = { -(right + left) / (right - left), -(top + bottom) / (top - bottom), -(farZ + nearZ) / (farZ - nearZ), 1.0f }
            };

            simd_float4x4 identity = matrix_identity_float4x4;

            // Create uniforms buffer for projection matrix
            struct CursorUniforms {
                simd_float4x4 projectionMatrix;
                simd_float4x4 modelViewMatrix;
            };
            struct CursorUniforms cursorUniforms = {
                .projectionMatrix = projection,
                .modelViewMatrix = identity
            };

            id<MTLBuffer> uniformsBuffer = [activeLayer.device newBufferWithBytes:&cursorUniforms
                                                                           length:sizeof(cursorUniforms)
                                                                          options:MTLResourceStorageModeShared];

            // Use a shader with projection matrix (like text rendering)
            static id<MTLRenderPipelineState> cursorPipeline = nil;
            if (!cursorPipeline) {
                NSError* error = nil;
                NSString* shaderSource = @""
                    "#include <metal_stdlib>\n"
                    "using namespace metal;\n"
                    "struct VertexIn { float2 position [[attribute(0)]]; float2 texCoord [[attribute(1)]]; };\n"
                    "struct VertexOut { float4 position [[position]]; float2 texCoord; };\n"
                    "struct Uniforms { float4x4 projectionMatrix; float4x4 modelViewMatrix; };\n"
                    "vertex VertexOut cursor_vertex(VertexIn in [[stage_in]], constant Uniforms& uniforms [[buffer(1)]]) {\n"
                    "    VertexOut out;\n"
                    "    float4 worldPos = float4(in.position, 0.0, 1.0);\n"
                    "    out.position = uniforms.projectionMatrix * uniforms.modelViewMatrix * worldPos;\n"
                    "    out.texCoord = in.texCoord;\n"
                    "    return out;\n"
                    "}\n"
                    "fragment float4 cursor_fragment(VertexOut in [[stage_in]]) {\n"
                    "    // Calculate distance from the rectangular cursor edges\n"
                    "    // Cursor occupies the center portion of the texture coordinates\n"
                    "    float glowPadding = 0.5; // Padding in texture space\n"
                    "    float texLeft = glowPadding / (1.0 + 2.0 * glowPadding);\n"
                    "    float texRight = (glowPadding + 1.0) / (1.0 + 2.0 * glowPadding);\n"
                    "    float texTop = glowPadding / (1.0 + 2.0 * glowPadding);\n"
                    "    float texBottom = (glowPadding + 1.0) / (1.0 + 2.0 * glowPadding);\n"
                    "    \n"
                    "    // Check if we're inside the actual cursor rectangle\n"
                    "    bool insideCursor = in.texCoord.x >= texLeft && in.texCoord.x <= texRight &&\n"
                    "                        in.texCoord.y >= texTop && in.texCoord.y <= texBottom;\n"
                    "    \n"
                    "    // Calculate distance to nearest edge of cursor rectangle\n"
                    "    float distX = max(texLeft - in.texCoord.x, in.texCoord.x - texRight);\n"
                    "    float distY = max(texTop - in.texCoord.y, in.texCoord.y - texBottom);\n"
                    "    float edgeDist = max(distX, distY);\n"
                    "    \n"
                    "    // Core cursor color (bright yellow-white)\n"
                    "    float3 coreColor = float3(1.0, 1.0, 0.3);\n"
                    "    \n"
                    "    // Glow color (soft yellow glow)\n"
                    "    float3 glowColor = float3(1.0, 0.9, 0.4);\n"
                    "    \n"
                    "    float3 finalColor;\n"
                    "    float alpha;\n"
                    "    \n"
                    "    if (insideCursor) {\n"
                    "        // Inside cursor: solid bright color\n"
                    "        finalColor = coreColor;\n"
                    "        alpha = 0.85;\n"
                    "    } else {\n"
                    "        // Outside cursor: glow effect that fades with distance\n"
                    "        float glowDistance = edgeDist / texLeft; // Normalize to padding size\n"
                    "        float glowIntensity = smoothstep(1.0, 0.0, glowDistance);\n"
                    "        glowIntensity = pow(glowIntensity, 0.8); // Softer falloff\n"
                    "        \n"
                    "        finalColor = glowColor;\n"
                    "        alpha = glowIntensity * 0.6;\n"
                    "    }\n"
                    "    \n"
                    "    return float4(finalColor, alpha);\n"
                    "}\n";

                id<MTLLibrary> library = [activeLayer.device newLibraryWithSource:shaderSource options:nil error:&error];
                if (!library) {
                    NSLog(@"Failed to compile cursor shader: %@", error);
                    return;
                }

                id<MTLFunction> vertexFunc = [library newFunctionWithName:@"cursor_vertex"];
                id<MTLFunction> fragmentFunc = [library newFunctionWithName:@"cursor_fragment"];

                MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
                desc.vertexFunction = vertexFunc;
                desc.fragmentFunction = fragmentFunc;
                desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
                desc.colorAttachments[0].blendingEnabled = YES;
                desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
                desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
                desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
                desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

                MTLVertexDescriptor* vertexDesc = [[MTLVertexDescriptor alloc] init];
                vertexDesc.attributes[0].format = MTLVertexFormatFloat2;
                vertexDesc.attributes[0].offset = 0;
                vertexDesc.attributes[0].bufferIndex = 0;
                vertexDesc.attributes[1].format = MTLVertexFormatFloat2;
                vertexDesc.attributes[1].offset = sizeof(simd_float2);
                vertexDesc.attributes[1].bufferIndex = 0;
                vertexDesc.layouts[0].stride = sizeof(simd_float2) * 2;
                desc.vertexDescriptor = vertexDesc;

                cursorPipeline = [activeLayer.device newRenderPipelineStateWithDescriptor:desc error:&error];
                if (!cursorPipeline) {
                    NSLog(@"Failed to create cursor pipeline: %@", error);
                    return;
                }
            }

            // Render the cursor with projection matrix
            [metalEncoder setRenderPipelineState:cursorPipeline];
            [metalEncoder setVertexBuffer:cursorBuffer offset:0 atIndex:0];
            [metalEncoder setVertexBuffer:uniformsBuffer offset:0 atIndex:1];
            [metalEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
        }
    }

    void coretext_editor_render(void* encoder, float width, float height) {
        @autoreleasepool {
            if (g_editorTextLayer) {
                extern bool editor_is_active();
                bool active = editor_is_active();

                if (active) {
                    // Always read current editor buffer content - don't cache
                    struct TextCell* editorBuffer = coretext_editor_get_text_buffer();
                    if (editorBuffer) {
                        // Copy editor buffer to layer's text grid
                        memcpy(g_editorTextLayer.textGrid, editorBuffer, GRID_WIDTH * GRID_HEIGHT * sizeof(struct TextCell));
                    }
                }

                id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
                [g_editorTextLayer renderWithEncoder:metalEncoder viewport:CGSizeMake(width, height)];

                // Render screen cursor on top
                screen_cursor_render(encoder, width, height);
            }
        }
    }

    // CRT Glow effect control
    void coretext_set_crt_glow(bool enabled) {
        g_crt_glow_enabled = enabled;
    }

    bool coretext_get_crt_glow(void) {
        return g_crt_glow_enabled;
    }

    void coretext_set_crt_glow_intensity(float intensity) {
        g_crt_glow_intensity = fmaxf(0.0f, fminf(1.0f, intensity));
    }

    float coretext_get_crt_glow_intensity(void) {
        return g_crt_glow_intensity;
    }

    // CRT Scanlines effect control
    void coretext_set_crt_scanlines(bool enabled) {
        g_crt_scanlines_enabled = enabled;
    }

    bool coretext_get_crt_scanlines(void) {
        return g_crt_scanlines_enabled;
    }

    void coretext_set_crt_scanline_intensity(float intensity) {
        g_crt_scanline_intensity = fmaxf(0.0f, fminf(1.0f, intensity));
    }

    float coretext_get_crt_scanline_intensity(void) {
        return g_crt_scanline_intensity;
    }

    // Legacy font overdraw functions (now map to glow for compatibility)
    void coretext_set_font_overdraw(bool enabled) {
        g_crt_glow_enabled = enabled;
    }

    bool coretext_get_font_overdraw(void) {
        return g_crt_glow_enabled;
    }

    void coretext_terminal_set_color(float ink_r, float ink_g, float ink_b, float ink_a,
                                     float paper_r, float paper_g, float paper_b, float paper_a) {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                simd_float4 ink = simd_make_float4(ink_r, ink_g, ink_b, ink_a);
                simd_float4 paper = simd_make_float4(paper_r, paper_g, paper_b, paper_a);
                [g_terminalTextLayer setColor:ink paper:paper];
            }
        }
    }

    void coretext_terminal_set_ink(float r, float g, float b, float a) {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                simd_float4 ink = simd_make_float4(r, g, b, a);
                [g_terminalTextLayer setInk:ink];
            }
        }
    }

    void coretext_terminal_set_paper(float r, float g, float b, float a) {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                simd_float4 paper = simd_make_float4(r, g, b, a);
                [g_terminalTextLayer setPaper:paper];
            }
        }
    }

    void coretext_poke_colour(int layer, int x, int y, uint32_t ink_colour, uint32_t paper_colour) {
        @autoreleasepool {
            CoreTextLayer* targetLayer = (layer == 5) ? g_terminalTextLayer :
                                         (layer == 6) ? g_editorTextLayer : nil;

            if (targetLayer && x >= 0 && x < GRID_WIDTH && y >= 0 && y < GRID_HEIGHT) {
                int index = y * GRID_WIDTH + x;

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

    void coretext_poke_ink(int layer, int x, int y, uint32_t ink_colour) {
        @autoreleasepool {
            CoreTextLayer* targetLayer = (layer == 5) ? g_terminalTextLayer :
                                         (layer == 6) ? g_editorTextLayer : nil;

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

    void coretext_poke_paper(int layer, int x, int y, uint32_t paper_colour) {
        @autoreleasepool {
            CoreTextLayer* targetLayer = (layer == 5) ? g_terminalTextLayer :
                                         (layer == 6) ? g_editorTextLayer : nil;

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

    // Wrapper functions for backward compatibility with truetype_ prefix
    // Undefine macros first to avoid expansion
    #undef truetype_poke_paper
    #undef truetype_poke_ink
    #undef truetype_poke_colour

    void truetype_poke_paper(int layer, int x, int y, uint32_t paper_colour) {
        coretext_poke_paper(layer, x, y, paper_colour);
    }

    void truetype_poke_ink(int layer, int x, int y, uint32_t ink_colour) {
        coretext_poke_ink(layer, x, y, ink_colour);
    }

    void truetype_poke_colour(int layer, int x, int y, uint32_t ink_colour, uint32_t paper_colour) {
        coretext_poke_colour(layer, x, y, ink_colour, paper_colour);
    }

    void coretext_status(const char* text) {
        @autoreleasepool {
            if (g_editorTextLayer && text) {
                NSString* nsText = [NSString stringWithUTF8String:text];
                for (int x = 0; x < GRID_WIDTH; x++) {
                    int index = (GRID_HEIGHT - 1) * GRID_WIDTH + x;
                    g_editorTextLayer.textGrid[index].character = ' ';
                    g_editorTextLayer.textGrid[index].inkColor = simd_make_float4(0.0f, 0.0f, 0.0f, 1.0f);
                    g_editorTextLayer.textGrid[index].paperColor = simd_make_float4(1.0f, 1.0f, 0.0f, 1.0f);
                }
                [g_editorTextLayer printAt:0 y:(GRID_HEIGHT - 1) text:nsText];
            }
        }
    }

    // ============================================================================
    // DIRECT GRID ACCESS API
    // ============================================================================

    struct TextCell* coretext_get_grid_pointer(int layer) {
        @autoreleasepool {
            CoreTextLayer* targetLayer = (layer == 5) ? g_terminalTextLayer :
                                         (layer == 6) ? g_editorTextLayer : nil;
            return targetLayer ? targetLayer.textGrid : NULL;
        }
    }

    // Alias for compatibility
    struct TextCell* coretext_get_grid_buffer(int layer) {
        return coretext_get_grid_pointer(layer);
    }

    int coretext_get_grid_width(void) {
        return GRID_WIDTH;
    }

    int coretext_get_grid_height(void) {
        return GRID_HEIGHT;
    }

    int coretext_get_grid_stride(void) {
        return GRID_WIDTH;  // Number of cells per row
    }

    void coretext_clear_region(int layer, int x, int y, int width, int height,
                              uint32_t character, uint32_t ink, uint32_t paper) {
        @autoreleasepool {
            struct TextCell* grid = coretext_get_grid_pointer(layer);
            if (!grid) return;

            // Convert colors to float4
            simd_float4 inkColor = simd_make_float4(
                ((ink >> 16) & 0xFF) / 255.0f,
                ((ink >> 8) & 0xFF) / 255.0f,
                (ink & 0xFF) / 255.0f,
                ((ink >> 24) & 0xFF) / 255.0f
            );
            simd_float4 paperColor = simd_make_float4(
                ((paper >> 16) & 0xFF) / 255.0f,
                ((paper >> 8) & 0xFF) / 255.0f,
                (paper & 0xFF) / 255.0f,
                ((paper >> 24) & 0xFF) / 255.0f
            );

            // Clamp to grid bounds
            int x2 = x + width;
            int y2 = y + height;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x2 > GRID_WIDTH) x2 = GRID_WIDTH;
            if (y2 > GRID_HEIGHT) y2 = GRID_HEIGHT;

            // Fast fill using direct memory access
            for (int row = y; row < y2; row++) {
                int index = row * GRID_WIDTH + x;
                for (int col = x; col < x2; col++, index++) {
                    grid[index].character = character;
                    grid[index].inkColor = inkColor;
                    grid[index].paperColor = paperColor;
                }
            }
        }
    }

    void coretext_fill_rect(int layer, int x, int y, int width, int height,
                           uint32_t ink, uint32_t paper) {
        // Fill with space character
        coretext_clear_region(layer, x, y, width, height, ' ', ink, paper);
    }

    void coretext_text_layers_cleanup() {
        @autoreleasepool {
            g_terminalTextLayer = nil;
            g_editorTextLayer = nil;
            NSLog(@"CoreText dual text layers cleaned up");
        }
    }

    struct TextCell* coretext_editor_get_text_buffer() {
        if (g_editorTextLayer) {
            return g_editorTextLayer.textGrid;
        }
        return NULL;
    }

    // Global screen cursor API - used by all modes (editor, REPL, etc)
    void screen_cursor_set_position(int x, int y) {
        g_screenCursor.x = x;
        g_screenCursor.y = y;
    }

    void screen_cursor_set_visible(bool visible) {
        g_screenCursor.visible = visible;
    }

    void screen_cursor_set_blink_phase(float phase) {
        g_screenCursor.blinkPhase = phase;
    }

    void screen_cursor_set_color(float r, float g, float b, float a) {
        g_screenCursor.color = simd_make_float4(r, g, b, a);
    }

    // Legacy API - now redirects to screen cursor
    void coretext_editor_set_cursor_position(int x, int y, bool visible) {
        screen_cursor_set_position(x, y);
        screen_cursor_set_visible(visible);
    }

    void coretext_editor_update_cursor_blink(float phase) {
        screen_cursor_set_blink_phase(phase);
    }

    void coretext_editor_set_cursor_colors(float ink_r, float ink_g, float ink_b, float ink_a,
                                           float paper_r, float paper_g, float paper_b, float paper_a) {
        if (g_editorTextLayer) {
            g_editorTextLayer.currentInk = simd_make_float4(ink_r, ink_g, ink_b, ink_a);
            g_editorTextLayer.currentPaper = simd_make_float4(paper_r, paper_g, paper_b, paper_a);
        }
    }

    // Editor layer text manipulation functions
    void coretext_editor_print(const char* text) {
        @autoreleasepool {
            if (g_editorTextLayer && text) {
                NSString* nsText = [NSString stringWithUTF8String:text];
                [g_editorTextLayer print:nsText];
            }
        }
    }

    void coretext_editor_print_at(int x, int y, const char* text) {
        @autoreleasepool {
            if (g_editorTextLayer && text) {
                // Optimized: avoid NSString allocation for common ASCII strings
                size_t len = strlen(text);

                // Fast path for short ASCII strings (most common case)
                if (len < 256) {
                    unichar buffer[256];
                    BOOL isAscii = YES;

                    for (size_t i = 0; i < len; i++) {
                        unsigned char c = (unsigned char)text[i];
                        if (c > 127) {
                            isAscii = NO;
                            break;
                        }
                        buffer[i] = c;
                    }

                    if (isAscii) {
                        // Direct memory write without NSString allocation
                        g_editorTextLayer.cursorX = x;
                        g_editorTextLayer.cursorY = y;

                        if (y >= 0 && y < GRID_HEIGHT && x >= 0 && x < GRID_WIDTH) {
                            int index = y * GRID_WIDTH + x;
                            int remainingSpace = GRID_WIDTH - x;
                            int copyCount = MIN((int)len, remainingSpace);

                            simd_float4 currentInk = g_editorTextLayer.currentInk;
                            simd_float4 currentPaper = g_editorTextLayer.currentPaper;

                            // Bulk write using SIMD-friendly loop
                            for (int i = 0; i < copyCount; i++) {
                                g_editorTextLayer.textGrid[index + i].character = buffer[i];
                                g_editorTextLayer.textGrid[index + i].inkColor = currentInk;
                                g_editorTextLayer.textGrid[index + i].paperColor = currentPaper;
                            }
                            g_editorTextLayer.cursorX = x + copyCount;
                        }
                        return;
                    }
                }

                // Fallback for UTF-8 or long strings
                NSString* nsText = [NSString stringWithUTF8String:text];
                [g_editorTextLayer printAt:x y:y text:nsText];
            }
        }
    }

    void coretext_editor_clear() {
        @autoreleasepool {
            if (g_editorTextLayer) {
                [g_editorTextLayer clear];
            }
        }
    }

    void coretext_editor_home() {
        @autoreleasepool {
            if (g_editorTextLayer) {
                [g_editorTextLayer home];
            }
        }
    }

    // ============================================================================
    // CHUNKY PIXEL GRAPHICS API
    // ============================================================================
    // Sextant-based chunky pixel mode (2x3 pixels per character cell)
    // Uses Unicode sextant range U+1FB00-U+1FB3F for 64 possible patterns
    // Ink/paper colors are set per character cell using existing color functions

    #define SEXTANT_BASE 0x1FB00
    #define SEXTANT_MAX  0x1FB3F

    // Get chunky pixel resolution based on current text mode
    void chunky_get_resolution(int* width, int* height) {
        if (width) *width = text_mode_get_columns() * 2;  // 2 pixels wide per cell
        if (height) *height = text_mode_get_rows() * 3;   // 3 pixels tall per cell
    }

    // Set a single chunky pixel on or off
    void chunky_pixel(int pixel_x, int pixel_y, bool on) {
        // Log function entry
        FILE* debugFile = fopen("/tmp/superterminal_chunky_debug.log", "a");
        if (debugFile) {
            fprintf(debugFile, "CHUNKY_PIXEL: Called with pixel(%d,%d) on=%d\n", pixel_x, pixel_y, on);
            fflush(debugFile);
            fclose(debugFile);
        }

        // Convert pixel coordinates to character cell coordinates
        int cell_x = pixel_x / 2;
        int cell_y = pixel_y / 3;

        // Get sub-pixel position within cell (0-1 for x, 0-2 for y)
        int sub_x = pixel_x % 2;
        int sub_y = pixel_y % 3;

        // Calculate bit index in sextant pattern
        // Bit layout: row * 2 + col
        int bit_index = sub_y * 2 + sub_x;

        // Get current character at this cell
        struct TextCell* grid = coretext_get_grid_buffer(5);  // Terminal layer
        if (!grid) {
            FILE* debugFile = fopen("/tmp/superterminal_chunky_debug.log", "a");
            if (debugFile) {
                fprintf(debugFile, "CHUNKY: Failed to get grid buffer!\n");
                fflush(debugFile);
                fclose(debugFile);
            }
            return;
        }

        int grid_width = coretext_get_grid_width();
        int grid_height = coretext_get_grid_height();

        if (cell_x < 0 || cell_x >= grid_width || cell_y < 0 || cell_y >= grid_height) {
            FILE* debugFile = fopen("/tmp/superterminal_chunky_debug.log", "a");
            if (debugFile) {
                fprintf(debugFile, "CHUNKY: Out of bounds - pixel(%d,%d) -> cell(%d,%d), grid is %dx%d\n",
                        pixel_x, pixel_y, cell_x, cell_y, grid_width, grid_height);
                fflush(debugFile);
                fclose(debugFile);
            }
            return;
        }

        int index = cell_y * grid_width + cell_x;
        struct TextCell* cell = &grid[index];

        // Get current pattern (default to empty if not sextant)
        uint32_t pattern = 0;
        if (cell->character >= SEXTANT_BASE && cell->character <= SEXTANT_MAX) {
            pattern = cell->character - SEXTANT_BASE;
        }

        // Modify bit
        if (on) {
            pattern |= (1 << bit_index);
        } else {
            pattern &= ~(1 << bit_index);
        }

        // Set new sextant character
        uint32_t new_char = SEXTANT_BASE + pattern;
        cell->character = new_char;

        // Set ink/paper colors from current state
        CoreTextLayer* layer = g_terminalTextLayer;
        if (layer) {
            cell->inkColor = layer.currentInk;
            cell->paperColor = layer.currentPaper;

            static int log_count = 0;
            if (log_count < 5) {  // Only log first 5 pixels
                FILE* debugFile = fopen("/tmp/superterminal_chunky_debug.log", "a");
                if (debugFile) {
                    fprintf(debugFile, "CHUNKY: Set pixel(%d,%d) -> cell(%d,%d) char=0x%X pattern=%d ink=(%.2f,%.2f,%.2f,%.2f) paper=(%.2f,%.2f,%.2f,%.2f)\n",
                            pixel_x, pixel_y, cell_x, cell_y, new_char, pattern,
                            cell->inkColor.x, cell->inkColor.y, cell->inkColor.z, cell->inkColor.w,
                            cell->paperColor.x, cell->paperColor.y, cell->paperColor.z, cell->paperColor.w);
                    fflush(debugFile);
                    fclose(debugFile);
                }
                log_count++;
            }
        } else {
            FILE* debugFile = fopen("/tmp/superterminal_chunky_debug.log", "a");
            if (debugFile) {
                fprintf(debugFile, "CHUNKY: No terminal layer found!\n");
                fflush(debugFile);
                fclose(debugFile);
            }
        }
    }

    // Scrollback buffer C API functions
    void text_locate_line(int line) {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                [g_terminalTextLayer locateLine:line];
            }
        }
    }

    void text_scroll_to_line(int line) {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                [g_terminalTextLayer scrollToLine:line];
            }
        }
    }

    void text_scroll_up(int lines) {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                [g_terminalTextLayer scrollUp:lines];
            }
        }
    }

    void text_scroll_down(int lines) {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                [g_terminalTextLayer scrollDown:lines];
            }
        }
    }

    void text_page_up() {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                [g_terminalTextLayer pageUp];
            }
        }
    }

    void text_page_down() {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                [g_terminalTextLayer pageDown];
            }
        }
    }

    void text_scroll_to_top() {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                [g_terminalTextLayer scrollToTop];
            }
        }
    }

    void text_scroll_to_bottom() {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                [g_terminalTextLayer scrollToBottom];
            }
        }
    }

    int text_get_cursor_line() {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                return [g_terminalTextLayer getCursorLine];
            }
            return 0;
        }
    }

    int text_get_cursor_column() {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                return [g_terminalTextLayer getCursorColumn];
            }
            return 0;
        }
    }

    int text_get_viewport_line() {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                return [g_terminalTextLayer getViewportLine];
            }
            return 0;
        }
    }

    int text_get_viewport_height() {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                return [g_terminalTextLayer getViewportHeight];
            }
            return 60;
        }
    }

    void text_set_autoscroll(bool enabled) {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                [g_terminalTextLayer setAutoScroll:enabled ? YES : NO];
            }
        }
    }

    bool text_get_autoscroll() {
        @autoreleasepool {
            if (g_terminalTextLayer) {
                return [g_terminalTextLayer getAutoScroll] ? true : false;
            }
            return true;
        }
    }

    // Clear all chunky pixels (set all cells to empty sextant pattern)
    void chunky_clear(void) {
        // Log function entry
        FILE* debugFile = fopen("/tmp/superterminal_chunky_debug.log", "a");
        if (debugFile) {
            fprintf(debugFile, "CHUNKY_CLEAR: Called\n");
            fflush(debugFile);
            fclose(debugFile);
        }

        struct TextCell* grid = coretext_get_grid_buffer(5);  // Terminal layer
        if (!grid) return;

        int grid_width = coretext_get_grid_width();
        int grid_height = coretext_get_grid_height();

        // Get current ink/paper colors
        CoreTextLayer* layer = g_terminalTextLayer;
        simd_float4 ink = layer ? layer.currentInk : simd_make_float4(1, 1, 1, 1);
        simd_float4 paper = layer ? layer.currentPaper : simd_make_float4(0, 0, 0, 1);

        for (int y = 0; y < grid_height; y++) {
            for (int x = 0; x < grid_width; x++) {
                int index = y * grid_width + x;
                grid[index].character = SEXTANT_BASE;  // All pixels off
                grid[index].inkColor = ink;
                grid[index].paperColor = paper;
            }
        }
    }

    // Draw a chunky pixel line using Bresenham's algorithm
    void chunky_line(int x1, int y1, int x2, int y2) {
        int dx = abs(x2 - x1);
        int dy = abs(y2 - y1);
        int sx = (x1 < x2) ? 1 : -1;
        int sy = (y1 < y2) ? 1 : -1;
        int err = dx - dy;

        int x = x1;
        int y = y1;

        while (true) {
            chunky_pixel(x, y, true);

            if (x == x2 && y == y2) break;

            int e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x += sx;
            }
            if (e2 < dx) {
                err += dx;
                y += sy;
            }
        }
    }

    // Draw a chunky pixel rectangle
    void chunky_rect(int x, int y, int width, int height, bool filled) {
        if (filled) {
            // Fill rectangle
            for (int py = y; py < y + height; py++) {
                for (int px = x; px < x + width; px++) {
                    chunky_pixel(px, py, true);
                }
            }
        } else {
            // Draw outline
            // Top and bottom edges
            for (int px = x; px < x + width; px++) {
                chunky_pixel(px, y, true);
                chunky_pixel(px, y + height - 1, true);
            }
            // Left and right edges
            for (int py = y; py < y + height; py++) {
                chunky_pixel(x, py, true);
                chunky_pixel(x + width - 1, py, true);
            }
        }
    }
}
