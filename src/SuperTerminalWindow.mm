//
//  SuperTerminalWindow.mm
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#include <chrono>
#include <queue>
#include <mutex>
#include "CommandQueue.h"
#include "GlobalShutdown.h"


// External Metal renderer functions
extern "C" {
    void metal_renderer_init(void* device);
    void metal_renderer_setup_view(void* view);
    void metal_renderer_render_background(float r, float g, float b, float a);
    void metal_renderer_cleanup();

    // Input system functions
    void input_system_key_down(int keycode);
    void input_system_key_up(int keycode);
    bool input_system_is_key_pressed(int keycode);
    int input_system_wait_key();
    int input_system_get_key();

    // REPL Console functions
    bool repl_initialize();
    void repl_shutdown();
    bool repl_is_initialized();
    void repl_activate();
    void repl_deactivate();
    void repl_toggle();
    bool repl_is_active();
    void repl_handle_key(int key, int modifiers);
    void repl_handle_character(char ch);
    void repl_update(float delta_time);
    void repl_render();

    // Mouse input system functions
    // Mouse input functions
    void input_system_mouse_down(int button, float x, float y);
    void input_system_mouse_up(int button, float x, float y);
    void input_system_mouse_move(float x, float y);
    bool input_system_is_mouse_pressed(int button);
    void input_system_get_mouse_position(float* x, float* y);
    void input_system_get_viewport_size(float* width, float* height);

    // Overlay graphics layer functions
    bool overlay_graphics_layer_initialize(void* device, int width, int height);
    void overlay_graphics_layer_shutdown(void);
    void overlay_graphics_layer_render_overlay(void* encoder, simd_float4x4 projectionMatrix);

    // Editor functions
    bool editor_is_active(void);
    void editor_key_pressed(int key, int keycode, bool shift, bool ctrl, bool alt, bool cmd);
    void editor_update(void);
    bool editor_toggle(void);
    void editor_set_cursor_from_mouse(float x, float y);
    void editor_mouse_down(float x, float y);
    void editor_mouse_drag(float x, float y);
    void editor_mouse_up(float x, float y);
    void editor_scroll_vertical(int lines);

    // Metal device access
    void* superterminal_get_metal_device(void);

    // Command queue functions
    void command_queue_init();
    void command_queue_shutdown();

    // Menu system functions
    void superterminal_setup_menus(void* nsview);
    void superterminal_create_menu_bar(void);
    void superterminal_create_context_menu(void);
    void superterminal_show_context_menu(float x, float y);
    void superterminal_update_menu_states(void);
    void superterminal_cleanup_menus(void);
    void trigger_run_script(void);

    // Text grid manager functions
    void text_grid_cycle_mode(void);
    void text_grid_cycle_scale(void);
    void text_grid_auto_fit(float viewportWidth, float viewportHeight);
    int text_grid_get_mode(void);
    void text_grid_set_mode(int mode);
    void text_grid_force_update(void);

    // Text layer functions
    void text_layer_clear(void);
    void text_layer_home(void);

    // Clipboard functions
    void macos_clipboard_set_text(const char* text);
    char* macos_clipboard_get_text(void);
    void macos_clipboard_free_text(char* text);
}

// Forward declaration for C++ termination handler
extern "C" void superterminal_application_will_terminate();

// macOS Clipboard Integration Functions
extern "C" void macos_clipboard_set_text(const char* text) {
    if (!text) return;

    NSString* nsText = [NSString stringWithUTF8String:text];
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    [pasteboard setString:nsText forType:NSPasteboardTypeString];
}

extern "C" char* macos_clipboard_get_text(void) {
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    NSString* text = [pasteboard stringForType:NSPasteboardTypeString];

    if (!text) return nullptr;

    const char* utf8Text = [text UTF8String];
    if (!utf8Text) return nullptr;

    // Allocate memory for the string (caller must free with macos_clipboard_free_text)
    size_t len = strlen(utf8Text);
    char* result = (char*)malloc(len + 1);
    if (result) {
        strcpy(result, utf8Text);
    }
    return result;
}

extern "C" void macos_clipboard_free_text(char* text) {
    if (text) {
        free(text);
    }
}

// Application delegate to handle termination
@interface SuperTerminalAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation SuperTerminalAppDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    NSLog(@"App finished launching");
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    NSLog(@"Application should terminate - calling cleanup");

    // Return NSTerminateLater and perform cleanup asynchronously
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{
        // Call the C++ termination handler
        superterminal_application_will_terminate();

        // After cleanup is complete, tell NSApplication it's safe to terminate
        dispatch_async(dispatch_get_main_queue(), ^{
            [NSApp replyToApplicationShouldTerminate:YES];
        });
    });

    return NSTerminateLater;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    NSLog(@"Application will terminate");
}
@end

static NSWindow* g_window = nil;
static MTKView* g_metalView = nil;
static id<MTLDevice> g_metalDevice = nil;
static simd_float4 g_backgroundColor = {0.0f, 0.0f, 0.0f, 1.0f};

// Saved video mode for Ctrl+L refresh
static int g_saved_video_mode = -1;

// Input state tracking
static bool g_keyStates[256] = {false};
static int g_lastReturnedKey = 0;
static int g_keyHoldFrames = 0;
static const int KEY_REPEAT_DELAY = 3; // Reduced from 10 to 3 frames for better responsiveness
static auto g_lastKeyTime = std::chrono::steady_clock::now();
static auto g_keyPressTime = std::chrono::steady_clock::now();
static bool g_keyWaiting = false;
static dispatch_semaphore_t g_keySemaphore = nil;

// waitKey performance metrics
struct WaitKeyMetrics {
    uint64_t call_count = 0;
    uint64_t immediate_returns = 0;  // Key already in queue
    uint64_t semaphore_waits = 0;    // Had to wait for key
    uint64_t interrupted_waits = 0;  // Interrupted by shutdown
    double total_wait_ms = 0.0;
    double min_wait_ms = std::numeric_limits<double>::max();
    double max_wait_ms = 0.0;
    uint64_t timeout_iterations = 0;  // Total number of 100ms timeouts

    // Response time latency metrics (keypress to waitKey return)
    uint64_t latency_samples = 0;
    double total_latency_ms = 0.0;
    double min_latency_ms = std::numeric_limits<double>::max();
    double max_latency_ms = 0.0;

    std::mutex metrics_mutex;

    void recordImmediate() {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        call_count++;
        immediate_returns++;
    }

    void recordWait(double wait_ms, uint64_t iterations) {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        call_count++;
        semaphore_waits++;
        total_wait_ms += wait_ms;
        timeout_iterations += iterations;
        if (wait_ms < min_wait_ms) min_wait_ms = wait_ms;
        if (wait_ms > max_wait_ms) max_wait_ms = wait_ms;
    }

    void recordInterrupted(double wait_ms, uint64_t iterations) {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        call_count++;
        interrupted_waits++;
        total_wait_ms += wait_ms;
        timeout_iterations += iterations;
    }

    void recordLatency(double latency_ms) {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        latency_samples++;
        total_latency_ms += latency_ms;
        if (latency_ms < min_latency_ms) min_latency_ms = latency_ms;
        if (latency_ms > max_latency_ms) max_latency_ms = latency_ms;
    }

    void logStats() {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        if (call_count == 0) {
            NSLog(@"[waitKey Metrics] No calls recorded");
            return;
        }

        double avg_wait_ms = semaphore_waits > 0 ? (total_wait_ms / semaphore_waits) : 0.0;
        double avg_iterations = semaphore_waits > 0 ? (double(timeout_iterations) / semaphore_waits) : 0.0;
        double avg_latency_ms = latency_samples > 0 ? (total_latency_ms / latency_samples) : 0.0;

        NSLog(@"[waitKey Metrics] ====================================");
        NSLog(@"[waitKey Metrics] Total calls: %llu", call_count);
        NSLog(@"[waitKey Metrics] Immediate returns: %llu (%.1f%%)",
              immediate_returns, 100.0 * immediate_returns / call_count);
        NSLog(@"[waitKey Metrics] Semaphore waits: %llu (%.1f%%)",
              semaphore_waits, 100.0 * semaphore_waits / call_count);
        NSLog(@"[waitKey Metrics] Interrupted: %llu (%.1f%%)",
              interrupted_waits, 100.0 * interrupted_waits / call_count);
        NSLog(@"[waitKey Metrics] Wait time (ms) - Min: %.2f, Max: %.2f, Avg: %.2f",
              min_wait_ms == std::numeric_limits<double>::max() ? 0.0 : min_wait_ms,
              max_wait_ms, avg_wait_ms);
        NSLog(@"[waitKey Metrics] Avg timeout iterations per wait: %.2f (%.0fms per iteration)",
              avg_iterations, avg_iterations * 100.0);

        // Response time latency stats
        if (latency_samples > 0) {
            NSLog(@"[waitKey Metrics] ------------------------------------");
            NSLog(@"[waitKey Metrics] RESPONSE TIME LATENCY (keypress -> return):");
            NSLog(@"[waitKey Metrics] Samples: %llu", latency_samples);
            NSLog(@"[waitKey Metrics] Min: %.3f ms, Max: %.3f ms, Avg: %.3f ms",
                  min_latency_ms, max_latency_ms, avg_latency_ms);
            if (max_latency_ms > 10.0) {
                NSLog(@"[waitKey Metrics] WARNING: Max latency exceeds 10ms threshold!");
            }
        }

        NSLog(@"[waitKey Metrics] ====================================");
    }

    void reset() {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        call_count = 0;
        immediate_returns = 0;
        semaphore_waits = 0;
        interrupted_waits = 0;
        total_wait_ms = 0.0;
        min_wait_ms = std::numeric_limits<double>::max();
        max_wait_ms = 0.0;
        timeout_iterations = 0;
        latency_samples = 0;
        total_latency_ms = 0.0;
        min_latency_ms = std::numeric_limits<double>::max();
        max_latency_ms = 0.0;
    }
};

static WaitKeyMetrics g_waitkey_metrics;

// Keyboard input queue with timestamp for latency measurement
struct KeyEvent {
    int keycode;
    int charCode;
    std::chrono::steady_clock::time_point timestamp;
};

static std::queue<KeyEvent> g_keyQueue;
static std::mutex g_keyQueueMutex;

// ESC key state tracking for script termination
static auto g_esc_press_start = std::chrono::steady_clock::now();
static bool g_esc_held = false;

// Mouse state tracking
static bool g_mouseButtonStates[3] = {false, false, false}; // Left, Right, Middle
static float g_mouseX = 0.0f;
static float g_mouseY = 0.0f;
static bool g_mouseWaiting = false;
static dispatch_semaphore_t g_mouseSemaphore = nil;
static int g_waitMouseButton = -1;
static float g_waitMouseX = 0.0f;
static float g_waitMouseY = 0.0f;

@interface SuperTerminalView : MTKView
@end

@implementation SuperTerminalView

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)keyDown:(NSEvent *)event {
    int keycode = [event keyCode];
    // NSLog(@"keyDown: keycode=%d, setting g_keyStates[%d]=true", keycode, keycode);
    g_keyStates[keycode] = true;

    // Get the character representation
    NSString* characters = [event charactersIgnoringModifiers];
    int charCode = -1;
    if (characters && [characters length] > 0) {
        unichar character = [characters characterAtIndex:0];
        if (character >= 32 && character <= 126) {
            charCode = (int)character; // Valid ASCII printable character
        }
    }

    // Pass modifier information to apps that need it
    if ([event modifierFlags] & NSEventModifierFlagControl) {
        keycode |= 0x1000; // Add Control modifier flag
    }
    if ([event modifierFlags] & NSEventModifierFlagShift) {
        keycode |= 0x2000; // Add Shift modifier flag
    }
    if ([event modifierFlags] & NSEventModifierFlagOption) {
        keycode |= 0x4000; // Add Option modifier flag
    }
    if ([event modifierFlags] & NSEventModifierFlagCommand) {
        keycode |= 0x8000; // Add Command modifier flag
    }

    // Add key to queue with timestamp for latency measurement
    {
        std::lock_guard<std::mutex> lock(g_keyQueueMutex);
        KeyEvent keyEvent;
        keyEvent.keycode = keycode;
        keyEvent.charCode = charCode;
        keyEvent.timestamp = std::chrono::steady_clock::now();
        g_keyQueue.push(keyEvent);
    }

    // Signal semaphore if someone is waiting
    if (g_keySemaphore) {
        dispatch_semaphore_signal(g_keySemaphore);
    }

    // Handle character input for REPL if active
    if (repl_is_active() && charCode >= 32 && charCode <= 126) {
        repl_handle_character((char)charCode);
    }

    // Let apps handle all key processing, including editor mode
    input_system_key_down(keycode);
}

- (void)keyUp:(NSEvent *)event {
    int keycode = [event keyCode];
    // NSLog(@"keyUp: keycode=%d, setting g_keyStates[%d]=false", keycode, keycode);
    g_keyStates[keycode] = false;
    input_system_key_up(keycode);
}

// Mouse event handlers
- (void)mouseDown:(NSEvent *)event {
    NSPoint location = [event locationInWindow];
    NSPoint viewLocation = [self convertPoint:location fromView:nil];

    float x = viewLocation.x;
    float y = self.bounds.size.height - viewLocation.y; // Flip Y coordinate for standard coordinate system

    g_mouseButtonStates[0] = true; // Left button
    g_mouseX = x;
    g_mouseY = y;



    // Signal waiting threads
    if (g_mouseWaiting && g_mouseSemaphore) {
        g_waitMouseButton = 0;
        g_waitMouseX = x;
        g_waitMouseY = y;
        dispatch_semaphore_signal(g_mouseSemaphore);
    }

    input_system_mouse_down(0, x, y);

    // If editor is active, start selection
    if (editor_is_active()) {
        editor_mouse_down(x, y);
    }
}

- (void)mouseUp:(NSEvent *)event {
    NSPoint location = [event locationInWindow];
    NSPoint viewLocation = [self convertPoint:location fromView:nil];

    float x = viewLocation.x;
    float y = self.bounds.size.height - viewLocation.y;

    g_mouseButtonStates[0] = false; // Left button
    g_mouseX = x;
    g_mouseY = y;

    NSLog(@"mouseUp: Left button at (%.1f, %.1f)", x, y);
    input_system_mouse_up(0, x, y);

    // If editor is active, finalize selection
    if (editor_is_active()) {
        editor_mouse_up(x, y);
    }
}

- (void)rightMouseDown:(NSEvent *)event {
    NSPoint location = [event locationInWindow];
    NSPoint viewLocation = [self convertPoint:location fromView:nil];

    float x = viewLocation.x;
    float y = self.bounds.size.height - viewLocation.y;

    g_mouseButtonStates[1] = true; // Right button
    g_mouseX = x;
    g_mouseY = y;



    // Signal waiting threads
    if (g_mouseWaiting && g_mouseSemaphore) {
        g_waitMouseButton = 1;
        g_waitMouseX = x;
        g_waitMouseY = y;
        dispatch_semaphore_signal(g_mouseSemaphore);
    }

    input_system_mouse_down(1, x, y);

    // Mouse click handled by input system only
}

- (void)rightMouseUp:(NSEvent *)event {
    NSPoint location = [event locationInWindow];
    NSPoint viewLocation = [self convertPoint:location fromView:nil];

    float x = viewLocation.x;
    float y = self.bounds.size.height - viewLocation.y;

    g_mouseButtonStates[1] = false; // Right button
    g_mouseX = x;
    g_mouseY = y;

    NSLog(@"rightMouseUp: Right button at (%.1f, %.1f)", x, y);
    input_system_mouse_up(1, x, y);

    // Show context menu on right-click
    superterminal_show_context_menu(x, y);
}

- (void)mouseMoved:(NSEvent *)event {
    NSPoint location = [event locationInWindow];
    NSPoint viewLocation = [self convertPoint:location fromView:nil];

    float x = viewLocation.x;
    float y = self.bounds.size.height - viewLocation.y;

    g_mouseX = x;
    g_mouseY = y;

    input_system_mouse_move(x, y);

    // Mouse move handled by input system only
}

- (void)mouseDragged:(NSEvent *)event {
    NSPoint location = [event locationInWindow];
    NSPoint viewLocation = [self convertPoint:location fromView:nil];

    float x = viewLocation.x;
    float y = self.bounds.size.height - viewLocation.y;

    g_mouseX = x;
    g_mouseY = y;

    // If editor is active, update selection
    if (editor_is_active()) {
        editor_mouse_drag(x, y);
    } else {
        // Handle dragging same as mouse move for other cases
        [self mouseMoved:event];
    }
}

- (void)rightMouseDragged:(NSEvent *)event {
    // Handle right-dragging same as mouse move
    [self mouseMoved:event];
}

- (void)scrollWheel:(NSEvent *)event {
    CGFloat deltaY = [event deltaY];

    // NSLog(@"scrollWheel: deltaY=%.2f", deltaY);

    // If editor is active, scroll the editor
    if (editor_is_active()) {
        // Convert scroll delta to lines (adjust sensitivity as needed)
        int scroll_lines = (int)(deltaY * 0.5); // 0.5 makes scrolling less sensitive
        if (scroll_lines != 0) {
            editor_scroll_vertical(-scroll_lines); // Negative for natural scrolling
        }
    }
}

@end



// C functions
extern "C" {
    void* superterminal_create_window(int width, int height) {
        @autoreleasepool {
            NSLog(@"Creating window %dx%d", width, height);

            // Initialize input system
            g_keySemaphore = dispatch_semaphore_create(0);
            memset(g_keyStates, 0, sizeof(g_keyStates));

            // Initialize mouse input system
            g_mouseSemaphore = dispatch_semaphore_create(0);
            memset(g_mouseButtonStates, 0, sizeof(g_mouseButtonStates));
            g_mouseX = 0.0f;
            g_mouseY = 0.0f;
            g_mouseWaiting = false;

            // Make sure we have an app
            NSApplication* app = [NSApplication sharedApplication];
            [app setActivationPolicy:NSApplicationActivationPolicyRegular];

            // Set up application delegate for proper termination handling
            SuperTerminalAppDelegate* delegate = [[SuperTerminalAppDelegate alloc] init];
            [app setDelegate:delegate];

            // Get Metal device
            g_metalDevice = MTLCreateSystemDefaultDevice();
            if (!g_metalDevice) {
                NSLog(@"Metal is not supported on this device");
                return nil;
            }

            // Create window
            NSRect frame = NSMakeRect(100, 100, width, height);
            NSUInteger styleMask = NSWindowStyleMaskTitled |
                                  NSWindowStyleMaskClosable |
                                  NSWindowStyleMaskMiniaturizable |
                                  NSWindowStyleMaskResizable;

            g_window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:styleMask
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];

            [g_window setTitle:@"SuperTerminal"];

            // Create custom Metal view with input handling
            g_metalView = [[SuperTerminalView alloc] initWithFrame:[[g_window contentView] bounds]
                                                            device:g_metalDevice];
            [g_metalView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

            // Set window content view to Metal view
            [g_window setContentView:g_metalView];

            // Initialize Metal renderer
            metal_renderer_init((__bridge void*)g_metalDevice);
            metal_renderer_setup_view((__bridge void*)g_metalView);

            // Initialize command queue on main thread
            command_queue_init();

            // Initialize overlay graphics layer (Graphics Layer 2)
            overlay_graphics_layer_initialize((__bridge void*)g_metalDevice, 1024, 768);

            [g_window center];
            [g_window makeKeyAndOrderFront:nil];

            // Activate app and bring to front
            [app activateIgnoringOtherApps:YES];
            [g_window orderFrontRegardless];
            [g_window makeMainWindow];
            [g_window makeKeyWindow];

            // Make the Metal view the first responder to receive keyboard events
            [g_window makeFirstResponder:g_metalView];

            // Setup menu system
            superterminal_setup_menus((__bridge void*)g_metalView);

            // Ensure the view starts drawing
            [g_metalView setNeedsDisplay:YES];

            // Initialize saved video mode to current mode
            g_saved_video_mode = text_grid_get_mode();

            NSLog(@"Window created and shown");
            return (__bridge void*)g_window;
        }
    }

    void superterminal_run_event_loop() {
        @autoreleasepool {
            NSLog(@"Starting event loop");
            NSApplication* app = [NSApplication sharedApplication];



            // Give the window a moment to initialize
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 0.1 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
                if (g_window) {
                    [g_window makeKeyAndOrderFront:nil];
                    [app activateIgnoringOtherApps:YES];
                    // Ensure the Metal view gets first responder status
                    [g_window makeFirstResponder:g_metalView];
                }
            });

            [app run];
        }
    }

    void superterminal_set_background_color(void* window_handle, float r, float g, float b, float a) {
        @autoreleasepool {
            NSLog(@"Setting background color: %.2f, %.2f, %.2f, %.2f", r, g, b, a);
            g_backgroundColor = simd_make_float4(r, g, b, a);
            metal_renderer_render_background(r, g, b, a);
        }
    }

    // Input system implementation
    bool superterminal_is_key_pressed(int keycode) {
        if (keycode < 0 || keycode >= 256) return false;
        return g_keyStates[keycode];
    }

    int superterminal_wait_key() {
        // Forward declare Lua interrupt checks (both old and new runtime)
        extern std::atomic<bool> g_lua_should_interrupt;
        extern bool lua_gcd_is_script_running(void);
        extern bool lua_gcd_is_on_lua_queue(void);

        // Start timing
        auto start_time = std::chrono::steady_clock::now();

        // Check if we have a key in the queue already
        {
            std::lock_guard<std::mutex> lock(g_keyQueueMutex);
            if (!g_keyQueue.empty()) {
                KeyEvent keyEvent = g_keyQueue.front();
                g_keyQueue.pop();

                // Calculate response time latency (keypress to return)
                auto return_time = std::chrono::steady_clock::now();
                double latency_ms = std::chrono::duration<double, std::milli>(return_time - keyEvent.timestamp).count();

                // Record immediate return with latency
                g_waitkey_metrics.recordImmediate();
                g_waitkey_metrics.recordLatency(latency_ms);

                // Log if latency is high
                // if (latency_ms > 10.0) {
                //     NSLog(@"[waitKey] HIGH LATENCY (immediate): %.3fms for key 0x%X", latency_ms, keyEvent.keycode);
                // }

                return keyEvent.keycode; // Return keycode
            }
        }

        // No key in queue, wait for next key press with timeout loop
        // This allows us to check for emergency shutdown and be interruptible
        g_keyWaiting = true;
        uint64_t timeout_count = 0;

        while (true) {
            // Check for emergency shutdown, old Lua interrupt, or termination request
            if (is_emergency_shutdown_requested() || g_lua_should_interrupt.load()) {
                g_keyWaiting = false;

                // Record interrupted wait
                auto end_time = std::chrono::steady_clock::now();
                double wait_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
                g_waitkey_metrics.recordInterrupted(wait_ms, timeout_count);

                // NSLog(@"[waitKey] INTERRUPTED after %.2fms (%llu timeouts)", wait_ms, timeout_count);

                return -1; // Return error code to indicate interruption
            }

            // If we're running in GCD context, check if script has been cancelled
            if (lua_gcd_is_on_lua_queue() && !lua_gcd_is_script_running()) {
                g_keyWaiting = false;

                // Record interrupted wait
                auto end_time = std::chrono::steady_clock::now();
                double wait_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
                g_waitkey_metrics.recordInterrupted(wait_ms, timeout_count);

                // NSLog(@"[waitKey] GCD CANCELLED after %.2fms (%llu timeouts)", wait_ms, timeout_count);

                return -1; // Script was cancelled
            }

            // Wait with timeout (100ms) instead of forever
            dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC);
            long result = dispatch_semaphore_wait(g_keySemaphore, timeout);

            if (result == 0) {
                // Semaphore was signaled - we have a key
                break;
            }

            // Timeout occurred - increment counter and loop again to check shutdown flags
            timeout_count++;

            // Log if we're waiting unusually long (>1 second)
            // if (timeout_count > 0 && timeout_count % 10 == 0) {
            //     auto current_time = std::chrono::steady_clock::now();
            //     double elapsed_ms = std::chrono::duration<double, std::milli>(current_time - start_time).count();
            //     NSLog(@"[waitKey] WARNING: Still waiting after %.2fms (%llu timeouts)",
            //           elapsed_ms, timeout_count);
            // }
        }

        g_keyWaiting = false;

        // Get key from queue
        std::lock_guard<std::mutex> lock(g_keyQueueMutex);
        if (!g_keyQueue.empty()) {
            KeyEvent keyEvent = g_keyQueue.front();
            g_keyQueue.pop();

            // Calculate response time latency (keypress to return)
            auto return_time = std::chrono::steady_clock::now();
            double latency_ms = std::chrono::duration<double, std::milli>(return_time - keyEvent.timestamp).count();

            // Record successful wait
            auto end_time = std::chrono::steady_clock::now();
            double wait_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            g_waitkey_metrics.recordWait(wait_ms, timeout_count);
            g_waitkey_metrics.recordLatency(latency_ms);

            // Log if wait was unusually long or latency was high
            // if (wait_ms > 500.0) {
            //     NSLog(@"[waitKey] SLOW: Waited %.2fms (%llu timeouts) for key 0x%X, latency: %.3fms",
            //           wait_ms, timeout_count, keyEvent.keycode, latency_ms);
            // } else if (latency_ms > 10.0) {
            //     NSLog(@"[waitKey] HIGH LATENCY: %.3fms for key 0x%X", latency_ms, keyEvent.keycode);
            // }

            return keyEvent.keycode; // Return keycode
        }

        // Should never happen - log error
        // auto end_time = std::chrono::steady_clock::now();
        // double wait_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
        // NSLog(@"[waitKey] ERROR: Semaphore signaled but queue empty after %.2fms", wait_ms);

        return -1;
    }

    int superterminal_wait_key_char() {
        // Forward declare Lua interrupt checks (both old and new runtime)
        extern std::atomic<bool> g_lua_should_interrupt;
        extern bool lua_gcd_is_script_running(void);
        extern bool lua_gcd_is_on_lua_queue(void);

        // Check if we have a key in the queue already
        {
            std::lock_guard<std::mutex> lock(g_keyQueueMutex);
            if (!g_keyQueue.empty()) {
                KeyEvent keyEvent = g_keyQueue.front();
                g_keyQueue.pop();
                return keyEvent.charCode; // Return character code
            }
        }

        // No key in queue, wait for next key press with timeout loop
        g_keyWaiting = true;

        while (true) {
            // Check for emergency shutdown, old Lua interrupt, or termination request
            if (is_emergency_shutdown_requested() || g_lua_should_interrupt.load()) {
                g_keyWaiting = false;
                return -1; // Return error code to indicate interruption
            }

            // If we're running in GCD context, check if script has been cancelled
            if (lua_gcd_is_on_lua_queue() && !lua_gcd_is_script_running()) {
                g_keyWaiting = false;
                return -1; // Script was cancelled
            }

            // Wait with timeout (100ms) instead of forever
            dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC);
            long result = dispatch_semaphore_wait(g_keySemaphore, timeout);

            if (result == 0) {
                // Semaphore was signaled - we have a key
                break;
            }
            // Timeout occurred - loop again to check shutdown flags
        }

        g_keyWaiting = false;

        // Get key from queue
        std::lock_guard<std::mutex> lock(g_keyQueueMutex);
        if (!g_keyQueue.empty()) {
            KeyEvent keyEvent = g_keyQueue.front();
            g_keyQueue.pop();
            return keyEvent.charCode; // Return character code
        }

        return -1; // Should never happen
    }

    int superterminal_get_key() {
        auto currentTime = std::chrono::steady_clock::now();

        // Find currently pressed keys
        bool anyKeyPressed = false;
        int pressedKey = 0;

        for (int i = 0; i < 256; i++) {
            if (g_keyStates[i]) {
                anyKeyPressed = true;
                pressedKey = i;
                // Add modifiers based on current modifier key states
                if (g_keyStates[0x3B]) pressedKey |= 0x1000; // Control
                if (g_keyStates[0x38] || g_keyStates[0x3C]) pressedKey |= 0x2000; // Shift
                if (g_keyStates[0x3A] || g_keyStates[0x3D]) pressedKey |= 0x4000; // Option
                if (g_keyStates[0x37] || g_keyStates[0x36]) pressedKey |= 0x8000; // Command
                break;
            }
        }

        // No keys pressed - reset state
        if (!anyKeyPressed) {
            g_lastReturnedKey = 0;
            g_keyHoldFrames = 0;
            return -1;
        }

        // New key press - return immediately
        if (pressedKey != g_lastReturnedKey) {
            g_lastReturnedKey = pressedKey;
            g_keyHoldFrames = 0;
            g_keyPressTime = currentTime;
            g_lastKeyTime = currentTime;
            return pressedKey;
        }

        // Same key held - use time-based delay for better responsiveness
        auto timeSincePress = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - g_keyPressTime).count();
        auto timeSinceLastReturn = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - g_lastKeyTime).count();

        // Use shorter time-based delays: 50ms initial, 25ms repeat
        if (timeSincePress >= 50 && timeSinceLastReturn >= 25) {
            g_lastKeyTime = currentTime;
            return pressedKey;
        }

        // Also support legacy frame-based system as fallback
        g_keyHoldFrames++;
        if (g_keyHoldFrames == 1 || g_keyHoldFrames > KEY_REPEAT_DELAY) {
            if (g_keyHoldFrames > KEY_REPEAT_DELAY) {
                g_keyHoldFrames = KEY_REPEAT_DELAY - 2; // Faster reset for continuous repeat
            }
            g_lastKeyTime = currentTime;
            return pressedKey;
        }

        // Still in delay period
        return -1;
    }

    void* superterminal_get_metal_device(void) {
        return (__bridge void*)g_metalDevice;
    }

    // Mouse input system functions
    void input_system_mouse_down(int button, float x, float y) {
        if (button >= 0 && button < 3) {
            g_mouseButtonStates[button] = true;
            g_mouseX = x;
            g_mouseY = y;
            NSLog(@"Mouse button %d down at (%.1f, %.1f)", button, x, y);
        }
    }

    void input_system_mouse_up(int button, float x, float y) {
        if (button >= 0 && button < 3) {
            g_mouseButtonStates[button] = false;
            g_mouseX = x;
            g_mouseY = y;
            NSLog(@"Mouse button %d up at (%.1f, %.1f)", button, x, y);
        }
    }

    void input_system_mouse_move(float x, float y) {
        g_mouseX = x;
        g_mouseY = y;
    }

    bool input_system_is_mouse_pressed(int button) {
        if (button >= 0 && button < 3) {
            return g_mouseButtonStates[button];
        }
        return false;
    }

    void input_system_get_mouse_position(float* x, float* y) {
        if (x) *x = g_mouseX;
        if (y) *y = g_mouseY;
    }

    void input_system_get_viewport_size(float* width, float* height) {
        if (g_metalView) {
            CGSize drawableSize = g_metalView.drawableSize;
            if (width) *width = drawableSize.width;
            if (height) *height = drawableSize.height;
        } else {
            // Fallback to default size if Metal view not available
            if (width) *width = 1024.0f;
            if (height) *height = 768.0f;
        }
    }

    // Input system implementations
    void input_system_key_down(int keycode) {
        // Extract base keycode and modifiers
        int base_keycode = keycode & 0x0FFF;
        bool ctrl = (keycode & 0x1000) != 0;
        bool shift = (keycode & 0x2000) != 0;
        bool alt = (keycode & 0x4000) != 0;
        bool cmd = (keycode & 0x8000) != 0;

        // Handle F4 for REPL toggle
        bool isF4 = (base_keycode == 0x76);  // F4 - Toggle REPL Console
        if (isF4) {
            NSLog(@"F4 pressed - toggling REPL console");
            if (!repl_is_initialized()) {
                repl_initialize();
            }
            repl_toggle();
            return;
        }

        // If REPL is active, let it handle most input first
        if (repl_is_active()) {
            // Calculate modifier flags for REPL
            int modifiers = 0;
            if (ctrl) modifiers |= 0x40000;   // Control
            if (shift) modifiers |= 0x20000;  // Shift
            if (alt) modifiers |= 0x80000;    // Alt/Option
            if (cmd) modifiers |= 0x100000;   // Command

            repl_handle_key(base_keycode, modifiers);

            // Don't process other keys when REPL is active, except ESC
            if (base_keycode != 0x35) { // Allow ESC to fall through
                return;
            }
        }

        // Ctrl+L - Clear screen and refresh display (refresh screen command)
        if (ctrl && base_keycode == 0x25) {  // L key
            NSLog(@"Ctrl+L pressed - clearing screen and refreshing display");

            // Get current mode to restore it (or use saved mode if available)
            int current_mode = text_grid_get_mode();
            int mode_to_restore = (g_saved_video_mode >= 0) ? g_saved_video_mode : current_mode;

            // Clear the screen
            text_layer_clear();

            // Reset cursor to home position
            text_layer_home();

            // Restore/refresh the video mode
            text_grid_set_mode(mode_to_restore);

            // Force update to ensure display is refreshed
            text_grid_force_update();

            NSLog(@"Screen cleared, cursor reset, mode restored to %d", mode_to_restore);
            return;
        }

        // Handle ESC key for script termination
        bool isESC = (base_keycode == 0x35);  // ESC - Terminate running Lua script

        // Handle function keys for Lua text editor
        bool isF1 = (base_keycode == 0x7A);  // F1 - Toggle Lua editor
        bool isF2 = (base_keycode == 0x78);  // F2 - Save file
        bool isF3 = (base_keycode == 0x63);  // F3 - Disable Lua editor
        bool isF8 = (base_keycode == 0x64);  // F8 - Execute code

        // ESC - Terminate running script (hold for 1 second)
        if (isESC) {
            g_esc_press_start = std::chrono::steady_clock::now();
            g_esc_held = true;

            extern bool is_script_running(void);
            if (is_script_running()) {
                NSLog(@"ESC pressed - script running, hold for 1 second to terminate");
            } else {
                NSLog(@"ESC pressed - no script running, but allowing ESC timer anyway");
            }
            return;
        }

        // F1 - Activate editor (only if not already active)
        if (isF1) {
            if (!editor_is_active()) {
                NSLog(@"F1 pressed - activating editor");
                bool editor_now_active = editor_toggle();
                if (editor_now_active) {
                    NSLog(@"Editor activated - overlay graphics layer ready");
                }
            } else {
                NSLog(@"F1 pressed - editor already active, use F3/ESC to exit");
            }
            return;
        }

        // F8 - Execute script
        if (isF8) {
            NSLog(@"F8 pressed - executing script");
            // Call the run script function
            trigger_run_script();
            return;
        }

        // Debug: Log all key presses
        NSLog(@"Key pressed: keycode=%d, base_keycode=%d", keycode, base_keycode);

        // F9 - Toggle sprite menu system
        if (base_keycode == 0x65) {  // F9
            NSLog(@"F9 pressed - overlay graphics test");
            // TODO: Add overlay graphics layer test here
            return;
        }

        // F10 - Cycle video modes
        if (base_keycode == 0x6D) {  // F10
            NSLog(@"F10 pressed - cycling video mode");
            FILE *debugFile = fopen("/tmp/superterminal_debug.log", "a");
            if (debugFile) {
                fprintf(debugFile, "\n=== F10 VIDEO MODE CHANGE ===\n");
                fflush(debugFile);
                fclose(debugFile);
            }
            text_grid_cycle_mode();
            // Save the new mode for Ctrl+L refresh
            g_saved_video_mode = text_grid_get_mode();
            return;
        }

        // F11 - Cycle text scaling
        if (base_keycode == 0x67) {  // F11
            NSLog(@"F11 pressed - cycling text scale");
            FILE *debugFile = fopen("/tmp/superterminal_debug.log", "a");
            if (debugFile) {
                fprintf(debugFile, "\n=== F11 TEXT SCALE CHANGE ===\n");
                fflush(debugFile);
                fclose(debugFile);
            }
            text_grid_cycle_scale();
            // Save the new mode for Ctrl+L refresh
            g_saved_video_mode = text_grid_get_mode();
            return;
        }

        // F12 - Auto-fit text to window
        if (base_keycode == 0x6F) {  // F12
            NSLog(@"F12 pressed - auto-fitting text grid");

            // Get actual viewport size from Metal view
            float viewport_width = 1024.0f;
            float viewport_height = 768.0f;

            if (g_metalView) {
                CGSize drawableSize = g_metalView.drawableSize;
                viewport_width = drawableSize.width;
                viewport_height = drawableSize.height;
                NSLog(@"F12: Using actual viewport size %.0fx%.0f", viewport_width, viewport_height);
            } else {
                NSLog(@"F12: Using fallback viewport size %.0fx%.0f", viewport_width, viewport_height);
            }

            text_grid_auto_fit(viewport_width, viewport_height);
            // Save the new mode for Ctrl+L refresh
            g_saved_video_mode = text_grid_get_mode();
            return;
        }

        // Keys handled by input system only

        // F3 - Let the editor handle it when active (removed interception)

        // When text editor is active, it handles its own key processing
        if (editor_is_active()) {
            // Convert keycode to ASCII character for text input
            int ascii_char = 0;

            // Handle printable ASCII characters (letters, numbers, symbols)
            if (base_keycode >= 0x00 && base_keycode <= 0x32) {
                // Map common keycodes to ASCII
                static const char keycode_to_ascii[] = {
                    'a', 's', 'd', 'f', 'h', 'g', 'z', 'x', 'c', 'v',    // 0x00-0x09
                    0, 'b', 'q', 'w', 'e', 'r', 'y', 't', '1', '2',      // 0x0A-0x13
                    '3', '4', '6', '5', '=', '9', '7', '-', '8', '0',    // 0x14-0x1D
                    ']', 'o', 'u', '[', 'i', 'p', 0, 'l', 'j', '\'',    // 0x1E-0x27
                    'k', ';', '\\', ',', '/', 'n', 'm', '.', 0, ' '      // 0x28-0x31
                };

                if (base_keycode <= 0x31 && keycode_to_ascii[base_keycode] != 0) {
                    ascii_char = keycode_to_ascii[base_keycode];

                    // Apply shift modifications
                    if (shift) {
                        if (ascii_char >= 'a' && ascii_char <= 'z') {
                            ascii_char = ascii_char - 'a' + 'A'; // Convert to uppercase
                        } else {
                            // Shift symbols
                            switch (ascii_char) {
                                case '1': ascii_char = '!'; break;
                                case '2': ascii_char = '@'; break;
                                case '3': ascii_char = '#'; break;
                                case '4': ascii_char = '$'; break;
                                case '5': ascii_char = '%'; break;
                                case '6': ascii_char = '^'; break;
                                case '7': ascii_char = '&'; break;
                                case '8': ascii_char = '*'; break;
                                case '9': ascii_char = '('; break;
                                case '0': ascii_char = ')'; break;
                                case '-': ascii_char = '_'; break;
                                case '=': ascii_char = '+'; break;
                                case '[': ascii_char = '{'; break;
                                case ']': ascii_char = '}'; break;
                                case '\\': ascii_char = '|'; break;
                                case ';': ascii_char = ':'; break;
                                case '\'': ascii_char = '"'; break;
                                case ',': ascii_char = '<'; break;
                                case '.': ascii_char = '>'; break;
                                case '/': ascii_char = '?'; break;
                            }
                        }
                    }
                }
            }

            // ESC key should be handled for script termination BEFORE editor gets it
            if (base_keycode == 0x35) { // ESC key
                // Don't let editor consume ESC - let it fall through to script termination handler
                // The editor will handle ESC later if no script is running
            } else {
                // Pass converted ASCII char and original keycode to editor
                editor_key_pressed(ascii_char, base_keycode, shift, ctrl, alt, cmd);
                return; // Editor consumes the key, app doesn't see it
            }
        }

        // If editor is not active, keys go to normal app input system
    }

    void input_system_key_up(int keycode) {
        int base_keycode = keycode & 0x0FFF;
        bool isESC = (base_keycode == 0x35);  // ESC key

        if (isESC && g_esc_held) {
            auto esc_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - g_esc_press_start).count();

            NSLog(@"ESC released after %ld ms", esc_duration);

            if (esc_duration >= 1000) {  // 1 second hold
                NSLog(@"ESC held for 1+ seconds - terminating script");
                extern bool is_script_running(void);
                extern void cleanup_finished_executions(void);
                extern bool lua_terminate_current_script(void);

                cleanup_finished_executions();
                if (is_script_running()) {
                    if (lua_terminate_current_script()) {
                        NSLog(@"Script termination signal sent");
                    } else {
                        NSLog(@"Failed to send termination signal");
                    }
                } else {
                    NSLog(@"No script running to terminate");
                }
            } else {
                NSLog(@"ESC not held long enough (need 1000ms)");
            }

            g_esc_held = false;
        }
    }

    bool input_system_is_key_pressed(int keycode) {
        return superterminal_is_key_pressed(keycode);
    }

    int input_system_wait_key() {
        return superterminal_wait_key();
    }

    int input_system_get_key() {
        return superterminal_get_key();
    }

    // Add immediate key polling function for real-time applications
    int input_system_get_key_immediate() {
        for (int i = 0; i < 256; i++) {
            if (g_keyStates[i]) {
                int keycode = i;
                // Add modifier flags
                if (g_keyStates[0x3B]) keycode |= 0x1000; // Control
                if (g_keyStates[0x38] || g_keyStates[0x3C]) keycode |= 0x2000; // Shift
                if (g_keyStates[0x3A] || g_keyStates[0x3D]) keycode |= 0x4000; // Option
                if (g_keyStates[0x37] || g_keyStates[0x36]) keycode |= 0x8000; // Command
                return keycode;
            }
        }
        return -1;
    }

    int superterminal_get_key_immediate() {
        return input_system_get_key_immediate();
    }

    // waitKey metrics functions
    void waitkey_log_metrics() {
        g_waitkey_metrics.logStats();
    }

    void waitkey_reset_metrics() {
        g_waitkey_metrics.reset();
        NSLog(@"[waitKey Metrics] Metrics reset");
    }

    void superterminal_set_window_title(const char* title) {
        @autoreleasepool {
            if (g_window) {
                NSString* nsTitle = [NSString stringWithUTF8String:title];
                [g_window setTitle:nsTitle];
            }

        }
    }

    // Function to set window title using command queue
    void set_window_title(const char* title) {
        if (!title) return;

        std::string title_copy(title); // Copy for safe capture in lambda
        SuperTerminal::g_command_queue.queueVoidCommand([title_copy]() {
            NSString* titleString = [NSString stringWithUTF8String:title_copy.c_str()];

            // Try to use the global window first
            if (g_window) {
                [g_window setTitle:titleString];
                NSLog(@"Window title updated to: %@", titleString);
                return;
            }

            // Fallback to main window
            NSWindow* mainWindow = [[NSApplication sharedApplication] mainWindow];
            if (mainWindow) {
                [mainWindow setTitle:titleString];
                NSLog(@"Window title updated to: %@", titleString);
            } else {
                NSLog(@"Warning: No window available to set title: %@", titleString);
            }
        });
    }

    // Native macOS input dialog
    char* show_macos_input_dialog(const char* title, const char* message, const char* defaultText) {
        @autoreleasepool {
            NSAlert *alert = [[NSAlert alloc] init];
            [alert setMessageText:[NSString stringWithUTF8String:title]];
            [alert setInformativeText:[NSString stringWithUTF8String:message]];
            [alert addButtonWithTitle:@"OK"];
            [alert addButtonWithTitle:@"Cancel"];

            // Create text field
            NSTextField *textField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 300, 22)];
            [textField setStringValue:[NSString stringWithUTF8String:defaultText]];
            [textField setEditable:YES];
            [textField setSelectable:YES];
            [alert setAccessoryView:textField];

            // Make text field first responder
            [alert layout];
            [[alert window] makeFirstResponder:textField];

            NSModalResponse response = [alert runModal];

            if (response == NSAlertFirstButtonReturn) {
                NSString *result = [textField stringValue];
                const char *cString = [result UTF8String];
                char *returnValue = (char*)malloc(strlen(cString) + 1);
                strcpy(returnValue, cString);
                return returnValue;
            }

            return nullptr;
        }
    }

} // extern "C"
