//
//  MetalRenderer.mm
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
#include "TextCommon.h"
#include "CoreTextRenderer.h"
// ParticleSystemC.h removed - C API declarations now in ParticleSystem.h (forward declared above)
#include <mutex>
#include <condition_variable>
#include <vector>
#include <string>


// Audio system for frame-based music updates
extern "C" {
    void music_update_frame(); // Drive frame-based music timing
    void audio_check_emergency_shutdown(); // Check audio system emergency shutdown
    void editor_update(); // Update text editor system
    bool command_queue_process_single(); // Process single command from queue
    void particle_system_update(float delta_time); // Update particle physics (v2 - main loop)
    void particle_system_render(void* encoder, simd_float4x4 projectionMatrix); // Render particles (v2)

    // Editor cursor functions
    bool editor_is_active(void);
    int editor_get_cursor_line(void);
    int editor_get_cursor_column(void);

    // REPL Console functions
    bool repl_is_active();
    void repl_update(float delta_time);
    void repl_render();

    // Overlay graphics functions
    bool overlay_is_initialized();
    bool overlay_is_visible();
    void overlay_show();
}

// Forward declaration for TrueTypeTextLayer
@class TrueTypeTextLayer;

// Frame synchronization globals
static std::mutex g_frame_mutex;
static std::condition_variable g_frame_cv;
static uint64_t g_frame_counter = 0;
static bool g_frame_ready = false;

// External text layer functions
extern "C" {
    void text_layer_init(void* device);
    void text_layer_render(void* encoder, float width, float height);
    void text_layer_cleanup();
    void text_layer_print(const char* text);
    void text_layer_print_at(int x, int y, const char* text);
    void text_layer_clear();
    void text_layer_home();

    // Note: TrueType/CoreText dual text layer functions now provided by CoreTextRenderer.h

    // Minimal graphics layer functions
    void minimal_graphics_layer_init(void* device, float width, float height);
    void minimal_graphics_layer_render(void* encoder, float width, float height);
    void minimal_graphics_layer_cleanup();

    // Tile layer functions (Layers 2 & 3)
    void tile_layers_init(void* device);
    void tile_layer_render(int layer, void* encoder, float width, float height);
    void tile_layers_cleanup();

    // Sprite layer functions (Layer 7)
    void sprite_layer_init(void* device);
    void sprite_layer_render(void* encoder, float width, float height);
    void sprite_layer_cleanup();

    // OverlayGraphicsLayer functions (Graphics Layer 2 - Top overlay layer)
    bool overlay_graphics_layer_initialize(void* device, int width, int height);
    void overlay_graphics_layer_shutdown(void);
    void overlay_graphics_layer_set_ink(float r, float g, float b, float a);
    void overlay_graphics_layer_fill_rect(float x, float y, float w, float h);
    void overlay_graphics_layer_draw_text(float x, float y, const char* text, float fontSize);
    void overlay_graphics_layer_present(void);
    void overlay_graphics_layer_render_overlay(void* encoder, void* projectionMatrix);
    void ui_update_content(void);
    void ui_draw_overlay(void);

    // Text grid manager functions
    void text_grid_recalculate_for_viewport(float viewportWidth, float viewportHeight);
    void text_grid_force_update(void);
    void text_grid_get_dimensions(int* width, int* height);
    void text_grid_get_cell_size(float* width, float* height);
}

// Forward declaration for layer control
extern "C" bool superterminal_layer_is_enabled(int layer);

@interface MetalRenderer : NSObject <MTKViewDelegate>

@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property (nonatomic, strong) id<MTLRenderPipelineState> backgroundPipelineState;
@property (nonatomic, strong) id<MTLBuffer> vertexBuffer;
@property (nonatomic, strong) MTKView* metalView;
@property (nonatomic, assign) simd_float4 backgroundColor;

// UI update system
@property (nonatomic, assign) int uiFrameCounter;

- (instancetype)initWithDevice:(id<MTLDevice>)device;
- (void)setupWithView:(MTKView*)view;
- (void)renderBackgroundLayer:(simd_float4)backgroundColor;
- (void)renderTerminalTextLayer:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport;
- (void)renderEditorTextLayer:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport;
- (void)renderGraphicsLayer:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport;
- (void)renderSpriteLayer:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport;
- (void)renderParticleLayer:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport;
- (void)renderOverlayLayer:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport;

@end

@implementation MetalRenderer

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    self = [super init];
    if (self) {
        self.device = device;
        self.commandQueue = [device newCommandQueue];
        self.backgroundColor = simd_make_float4(0.0f, 0.0f, 0.0f, 1.0f);

        // Initialize UI system
        self.uiFrameCounter = 0;

        [self createVertexBuffer];
        [self createBackgroundPipeline];

        // Initialize TrueType dual text layers C interface
        truetype_text_layers_init((__bridge void*)device);

        // Also initialize fallback text layer
        text_layer_init((__bridge void*)device);

        // Initialize minimal graphics layer
        minimal_graphics_layer_init((__bridge void*)device, 1024.0f, 768.0f);

        // Initialize tile layers (Layers 2 & 3)
        tile_layers_init((__bridge void*)device);

        // Initialize sprite layer (Layer 7)
        sprite_layer_init((__bridge void*)device);

        // Initialize overlay graphics layer (Graphics Layer 2 - Top overlay layer)
        if (overlay_graphics_layer_initialize((__bridge void*)device, 1024, 768)) {
            NSLog(@"Overlay graphics layer initialized successfully");
        } else {
            NSLog(@"WARNING: Failed to initialize overlay graphics layer");
        }
    }
    return self;
}

// Frame synchronization function for scripts
extern "C" void metal_renderer_wait_frame() {
    std::unique_lock<std::mutex> lock(g_frame_mutex);
    uint64_t current_frame = g_frame_counter;

    // Wait for the next frame to complete
    g_frame_cv.wait(lock, [current_frame] {
        return g_frame_counter > current_frame;
    });
}

- (void)setupWithView:(MTKView*)view {
    self.metalView = view;
    view.device = self.device;
    view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    view.clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    view.preferredFramesPerSecond = 60;
    view.delegate = self;
}

- (void)createVertexBuffer {
    // Create full-screen quad vertices
    float vertices[] = {
        // Position (x, y)
        -1.0f, -1.0f,  // Bottom left
         1.0f, -1.0f,  // Bottom right
        -1.0f,  1.0f,  // Top left

         1.0f, -1.0f,  // Bottom right
         1.0f,  1.0f,  // Top right
        -1.0f,  1.0f   // Top left
    };

    self.vertexBuffer = [self.device newBufferWithBytes:vertices
                                                 length:sizeof(vertices)
                                                options:MTLResourceStorageModeShared];
    self.vertexBuffer.label = @"Background Vertices";
}

- (void)createBackgroundPipeline {
    NSError* error = nil;

    // Create vertex and fragment shaders
    NSString* shaderSource = @""
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct VertexIn {\n"
    "    float2 position [[attribute(0)]];\n"
    "};\n"
    "\n"
    "struct VertexOut {\n"
    "    float4 position [[position]];\n"
    "};\n"
    "\n"
    "vertex VertexOut background_vertex(VertexIn in [[stage_in]]) {\n"
    "    VertexOut out;\n"
    "    out.position = float4(in.position, 0.0, 1.0);\n"
    "    return out;\n"
    "}\n"
    "\n"
    "fragment float4 background_fragment(VertexOut in [[stage_in]],\n"
    "                                   constant float4& backgroundColor [[buffer(0)]]) {\n"
    "    return backgroundColor;\n"
    "}\n";

    id<MTLLibrary> library = [self.device newLibraryWithSource:shaderSource options:nil error:&error];
    if (!library) {
        NSLog(@"Failed to create shader library: %@", error.localizedDescription);
        return;
    }

    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"background_vertex"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"background_fragment"];

    // Create render pipeline descriptor
    MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.label = @"Background Pipeline";
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = fragmentFunction;
    pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    // Set up vertex descriptor
    MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[0].offset = 0;
    vertexDescriptor.attributes[0].bufferIndex = 0;
    vertexDescriptor.layouts[0].stride = 2 * sizeof(float);
    vertexDescriptor.layouts[0].stepRate = 1;
    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    pipelineDescriptor.vertexDescriptor = vertexDescriptor;

    // Create pipeline state
    self.backgroundPipelineState = [self.device newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                                                               error:&error];
    if (!self.backgroundPipelineState) {
        NSLog(@"Failed to create background pipeline state: %@", error.localizedDescription);
    }
}

- (void)renderBackgroundLayer:(simd_float4)backgroundColor {
    if (!self.metalView || !self.backgroundPipelineState) {
        return;
    }

    // Get drawable
    id<CAMetalDrawable> drawable = self.metalView.currentDrawable;
    if (!drawable) {
        return;
    }

    // Create command buffer
    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    commandBuffer.label = @"SuperTerminal Render Command";

    // Create render pass descriptor
    MTLRenderPassDescriptor* renderPassDescriptor = self.metalView.currentRenderPassDescriptor;
    if (!renderPassDescriptor) {
        return;
    }

    // Create render encoder
    id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    renderEncoder.label = @"SuperTerminal Render Encoder";

    // LAYER 1: Render background first
    [renderEncoder setRenderPipelineState:self.backgroundPipelineState];
    // LAYER 1: Background color (always render but can be disabled)
    if (superterminal_layer_is_enabled(1)) {
        [renderEncoder setVertexBuffer:self.vertexBuffer offset:0 atIndex:0];
        [renderEncoder setFragmentBytes:&backgroundColor length:sizeof(backgroundColor) atIndex:0];
        [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    } else {
        NSLog(@"MetalRenderer: Background layer DISABLED - skipping");
    }

    // LAYER 2: Render background tile layer
    if (superterminal_layer_is_enabled(2)) {
        tile_layer_render(1, (__bridge void*)renderEncoder, self.metalView.drawableSize.width, self.metalView.drawableSize.height);
    } else {
        NSLog(@"MetalRenderer: Tile layer 1 DISABLED - skipping");
    }

    // LAYER 3: Render foreground tile layer
    if (superterminal_layer_is_enabled(3)) {
        tile_layer_render(2, (__bridge void*)renderEncoder, self.metalView.drawableSize.width, self.metalView.drawableSize.height);
    } else {
        NSLog(@"MetalRenderer: Tile layer 2 DISABLED - skipping");
    }

    // LAYER 4: Render simple graphics layer (below text)
    if (superterminal_layer_is_enabled(4)) {
        [self renderGraphicsLayer:renderEncoder viewport:CGSizeMake(self.metalView.drawableSize.width, self.metalView.drawableSize.height)];
    }

    // LAYER 5: Render terminal text layer
    if (superterminal_layer_is_enabled(5)) {
        [self renderTerminalTextLayer:renderEncoder viewport:CGSizeMake(self.metalView.drawableSize.width, self.metalView.drawableSize.height)];
    } else {
        NSLog(@"MetalRenderer: Terminal text layer DISABLED - skipping");
    }

    // LAYER 7: Render sprite layer (below editor text)
    if (superterminal_layer_is_enabled(7)) {
        [self renderSpriteLayer:renderEncoder viewport:CGSizeMake(self.metalView.drawableSize.width, self.metalView.drawableSize.height)];
    } else {
        NSLog(@"MetalRenderer: Sprite layer DISABLED - skipping");
    }

    // LAYER 6: Render editor text layer (on top of sprites) - for full-screen editor
    bool layer6_enabled = superterminal_layer_is_enabled(6);
    if (layer6_enabled) {
        // First render solid blue background for editor
        // COMMENTED OUT - not needed with transparent paper colors
        // [renderEncoder setRenderPipelineState:self.backgroundPipelineState];
        // simd_float4 editorBlue = simd_make_float4(0.0f, 0.0f, 1.0f, 1.0f); // Solid blue
        // [renderEncoder setVertexBuffer:self.vertexBuffer offset:0 atIndex:0];
        // [renderEncoder setFragmentBytes:&editorBlue length:sizeof(editorBlue) atIndex:0];
        // [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
        // NSLog(@"MetalRenderer: Rendered solid blue background for editor");

        // NSLog(@"MetalRenderer: Rendering Layer 6 (editor text layer)");
        [self renderEditorTextLayer:renderEncoder viewport:CGSizeMake(self.metalView.drawableSize.width, self.metalView.drawableSize.height)];

        // NSLog(@"MetalRenderer: Layer 6 (editor) render complete");
    }

    // LAYER 8: Render particle layer (frontmost layer for explosions and effects)
    if (superterminal_layer_is_enabled(8)) {
        // NSLog(@"MetalRenderer: Rendering Layer 8 (particle layer)");
        [self renderParticleLayer:renderEncoder viewport:CGSizeMake(self.metalView.drawableSize.width, self.metalView.drawableSize.height)];
        // NSLog(@"MetalRenderer: Layer 8 (particle) render complete");
    } else {
        // NSLog(@"MetalRenderer: Particle layer DISABLED - skipping");
    }

    // LAYER 9: Render overlay graphics layer (top-most layer for UI overlays)
    if (superterminal_layer_is_enabled(9)) {
        // Draw UI elements every frame
        ui_draw_overlay();
        [self renderOverlayLayer:renderEncoder viewport:CGSizeMake(self.metalView.drawableSize.width, self.metalView.drawableSize.height)];
    }

    // End encoding
    [renderEncoder endEncoding];

    // Present drawable
    [commandBuffer presentDrawable:drawable];

    // Commit command buffer and add completion handler for frame sync
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
        // Signal that the frame is complete
        {
            std::lock_guard<std::mutex> lock(g_frame_mutex);
            g_frame_ready = true;
            g_frame_counter++;
        }
        g_frame_cv.notify_all();
    }];

    [commandBuffer commit];
}

// MARK: - MTKViewDelegate

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    // Handle view size changes - trigger text grid recalculation
    // Write debug to file
    FILE *debugFile = fopen("/tmp/superterminal_debug.log", "a");
    if (debugFile) {
        fprintf(debugFile, "\n=== METALRENDERER SIZE CHANGE DEBUG ===\n");
        fprintf(debugFile, "DRAWABLE SIZE: %.0fx%.0f pixels\n", size.width, size.height);
    }

    // Get the actual window frame size in points (not pixels)
    // This is crucial for proper scaling on Retina displays
    CGSize windowSize = size;
    CGSize contentSize = size;

    if (view.window) {
        NSRect frame = view.window.frame;
        NSRect contentRect = [view.window contentRectForFrameRect:frame];
        windowSize = frame.size;
        contentSize = contentRect.size;

        if (debugFile) {
            fprintf(debugFile, "WINDOW FRAME: %.0fx%.0f points\n", windowSize.width, windowSize.height);
            fprintf(debugFile, "CONTENT RECT: %.0fx%.0f points\n", contentSize.width, contentSize.height);
            fprintf(debugFile, "DRAWABLE SIZE: %.0fx%.0f pixels\n", size.width, size.height);
        }

        // Use content size for text grid calculations
        windowSize = contentSize;
        if (debugFile) {
            fprintf(debugFile, "USING FOR TEXTGRID: %.0fx%.0f points\n", windowSize.width, windowSize.height);
        }
    } else {
        if (debugFile) {
            fprintf(debugFile, "NO WINDOW - using drawable size: %.0fx%.0f\n", size.width, size.height);
        }
    }

    // Trigger text grid manager to recalculate layout for new size
    extern void text_grid_recalculate_for_viewport(float width, float height);
    extern void text_grid_force_update(void);
    text_grid_recalculate_for_viewport(windowSize.width, windowSize.height);

    // Force immediate update of all text layers
    text_grid_force_update();

    // Get updated grid info for logging
    extern void text_grid_get_dimensions(int* width, int* height);
    extern void text_grid_get_cell_size(float* width, float* height);
    extern void coretext_set_cell_size(float cellWidth, float cellHeight);

    int gridW, gridH;
    float cellW, cellH;
    text_grid_get_dimensions(&gridW, &gridH);
    text_grid_get_cell_size(&cellW, &cellH);

    // CRITICAL: Set CoreText to use TextGridManager's scaled cell sizes
    coretext_set_cell_size(cellW, cellH);

    if (debugFile) {
        fprintf(debugFile, "FINAL RESULT: Grid %dx%d, Cell %.1fx%.1f, Viewport %.0fx%.0f\n",
                gridW, gridH, cellW, cellH, windowSize.width, windowSize.height);
        fprintf(debugFile, "========================================\n");
        fflush(debugFile);
        fclose(debugFile);
    }
}

- (void)drawInMTKView:(MTKView *)view {
    // Update UI content every 60 frames (once per second)
    self.uiFrameCounter++;
    if (self.uiFrameCounter >= 60) {
        self.uiFrameCounter = 0;
        ui_update_content();
    }

    // This is the main render loop - called every frame

    // Process pending command queue items (thread-safe API calls)
    // Process up to 10 commands per frame to avoid blocking rendering
    for (int i = 0; i < 10; i++) {
        if (!command_queue_process_single()) {
            break; // No more commands to process
        }
    }

    // Update text editor system
    editor_update();

    // Update REPL console system
    static auto last_frame_time = std::chrono::steady_clock::now();
    auto current_time = std::chrono::steady_clock::now();
    float delta_time = std::chrono::duration<float>(current_time - last_frame_time).count();
    last_frame_time = current_time;

    if (repl_is_active()) {
        repl_update(delta_time);
    }

    // Update particle system physics (v2 - simplified, runs in main loop)
    particle_system_update(delta_time);

    // Update frame-based music player timing
    music_update_frame();

    // Check audio system for emergency shutdown
    audio_check_emergency_shutdown();

    // Overlay graphics layer updates are handled automatically during render

    // Render REPL console if active (before background to ensure it's visible)
    if (repl_is_active()) {
        repl_render();
        // Force overlay to be visible after REPL renders
        if (overlay_is_initialized() && !overlay_is_visible()) {
            overlay_show();
        }
    }

    [self renderBackgroundLayer:self.backgroundColor];
}

- (void)renderTerminalTextLayer:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport {
    // Render Layer 5: Terminal text layer (for print, print_at commands)
    truetype_terminal_render((__bridge void*)encoder, viewport.width, viewport.height);
}

- (void)renderEditorTextLayer:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport {
    // Render Layer 6: Editor text layer (for full-screen editor)
    coretext_editor_render((__bridge void*)encoder, viewport.width, viewport.height);
}



- (void)renderGraphicsLayer:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport {
    // Render Layer 4: Minimal graphics layer (for draw_line, draw_rect, etc.)
    minimal_graphics_layer_render((__bridge void*)encoder, viewport.width, viewport.height);
}

- (void)renderSpriteLayer:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport {
    // Render Layer 7: Sprite layer (below editor text overlay)
    sprite_layer_render((__bridge void*)encoder, viewport.width, viewport.height);
}

- (void)renderParticleLayer:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport {
    // Render Layer 8: Particle layer (frontmost layer for explosions and effects)
    // NSLog(@"MetalRenderer: renderParticleLayer called with viewport %.0fx%.0f", viewport.width, viewport.height);

    // Use fixed 1024x768 coordinate system to match sprites
    simd_float4x4 projectionMatrix = {
        simd_make_float4(2.0f / 1024.0f, 0.0f, 0.0f, 0.0f),
        simd_make_float4(0.0f, -2.0f / 768.0f, 0.0f, 0.0f),
        simd_make_float4(0.0f, 0.0f, 1.0f, 0.0f),
        simd_make_float4(-1.0f, 1.0f, 0.0f, 1.0f)
    };

    particle_system_render((__bridge void*)encoder, projectionMatrix);
}

- (void)renderOverlayLayer:(id<MTLRenderCommandEncoder>)encoder viewport:(CGSize)viewport {
    // Render Graphics Layer 2: Overlay graphics layer (top-most layer for UI overlays)
    // Use fixed 1024x768 coordinate system to match sprites and particles
    simd_float4x4 projectionMatrix = {
        simd_make_float4(2.0f / 1024.0f, 0.0f, 0.0f, 0.0f),
        simd_make_float4(0.0f, -2.0f / 768.0f, 0.0f, 0.0f),
        simd_make_float4(0.0f, 0.0f, -1.0f, 0.0f),
        simd_make_float4(-1.0f, 1.0f, 0.0f, 1.0f)
    };

    overlay_graphics_layer_render_overlay((__bridge void*)encoder, (void*)&projectionMatrix);
}

@end

// C interface
static MetalRenderer* g_metalRenderer = nil;

extern "C" {
    // Forward declaration for particle system initialization
    bool particle_system_initialize(void* metalDevice);

    void metal_renderer_init(void* device) {
        @autoreleasepool {
            id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
            g_metalRenderer = [[MetalRenderer alloc] initWithDevice:metalDevice];
            NSLog(@"Metal renderer initialized");

            // Initialize particle system with Metal device
            if (particle_system_initialize(device)) {
                NSLog(@"Particle system initialized successfully");
            } else {
                NSLog(@"Warning: Failed to initialize particle system");
            }
        }
    }

    void metal_renderer_setup_view(void* view) {
        @autoreleasepool {
            if (g_metalRenderer) {
                MTKView* metalView = (__bridge MTKView*)view;
                [g_metalRenderer setupWithView:metalView];
                NSLog(@"Metal renderer view setup complete");
            }
        }
    }

    void metal_renderer_render_background(float r, float g, float b, float a) {
        @autoreleasepool {
            if (g_metalRenderer) {
                simd_float4 color = simd_make_float4(r, g, b, a);
                g_metalRenderer.backgroundColor = color;
            }
        }
    }

    void metal_renderer_cleanup() {
        @autoreleasepool {
            // Cleanup sprite menu system
            // Sprite menu system removed - no longer needed

            // Cleanup UI layer
            overlay_graphics_layer_shutdown();

            sprite_layer_cleanup();
            tile_layers_cleanup();
            minimal_graphics_layer_cleanup();
            truetype_text_layers_cleanup();
            text_layer_cleanup();
            g_metalRenderer = nil;
            NSLog(@"Metal renderer cleaned up");
        }
    }
}
