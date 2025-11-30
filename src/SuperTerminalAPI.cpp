//
//  SuperTerminalAPI.cpp
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "SuperTerminal.h"
#include "CoreTextRenderer.h"

#include <iostream>
#include <unistd.h>
#include <dispatch/dispatch.h>
#include <chrono>
#include <cstdlib>
#include <atomic>
#include "CommandQueue.h"
#include "ConsoleLogger.h"

extern "C" {
    void _exit(int status);
    
    // Forward declaration for shutdown check
    bool is_emergency_shutdown_requested();
    
    // Lua GCD Runtime forward declaration
    const char* lua_gcd_get_last_error(void);
}

#ifdef __OBJC__
#import <simd/simd.h>
#endif

using namespace SuperTerminal;

// External functions from SuperTerminalWindow.mm
extern "C" {
    void* superterminal_create_window(int width, int height);
    void superterminal_run_event_loop();
    void superterminal_set_background_color(void* window_handle, float r, float g, float b, float a);
    bool superterminal_is_key_pressed(int keycode);
    int superterminal_wait_key();
    int superterminal_wait_key_char();
    int superterminal_get_key();
    int superterminal_get_key_immediate();
    
    // Font overdraw control functions
    void truetype_set_font_overdraw(bool enabled);
    bool truetype_get_font_overdraw(void);
    
    // Emphatic shutdown functions
    void superterminal_force_shutdown(void);
    
    // Mouse input functions
    bool input_system_is_mouse_pressed(int button);
    void input_system_get_mouse_position(float* x, float* y);


}

// External functions from MetalRenderer.mm
extern "C" {
    void metal_renderer_wait_frame();
}

// External functions from TextLayer.mm
extern "C" {
    void text_layer_print(const char* text);
    void text_layer_print_at(int x, int y, const char* text);
    void text_layer_clear();
    void text_layer_home();
    void truetype_terminal_set_color(float ink_r, float ink_g, float ink_b, float ink_a,
                                     float paper_r, float paper_g, float paper_b, float paper_a);
    void truetype_terminal_set_ink(float r, float g, float b, float a);
    void truetype_terminal_set_paper(float r, float g, float b, float a);
    // truetype_poke_* functions are now handled by CoreTextRenderer.h macros
    // (removed forward declarations that were preventing macro expansion)
}

// External functions from TileLayer.mm
extern "C" {
    bool tile_load_impl(uint16_t id, const char* filename);
    void tile_scroll_impl(int layer, float dx, float dy);
    void tile_set_viewport_impl(int layer, int x, int y);
    void tile_set_impl(int layer, int map_x, int map_y, uint16_t tile_id);
    uint16_t tile_get_impl(int layer, int map_x, int map_y);
    void tile_create_map_impl(int layer, int width, int height);
    void tile_resize_map_impl(int layer, int new_width, int new_height);
    void tile_clear_map_impl(int layer);
    void tile_fill_map_impl(int layer, uint16_t tile_id);
    void tile_set_region_impl(int layer, int start_x, int start_y, int width, int height, uint16_t tile_id);
    
    // Tile system cleanup functions
    void tiles_clear_impl(void);
    void tiles_shutdown_impl(void);
    void tile_get_map_size_impl(int layer, int* width, int* height);
    void tile_center_viewport_impl(int layer, int tile_x, int tile_y);
    bool tile_is_valid_position_impl(int layer, int x, int y);
}

// Note: TrueType functions now provided by CoreTextRenderer.h via compatibility macros
extern "C" {
    
    // Text grid manager functions
    void text_grid_set_mode(int mode);
    int text_grid_get_mode(void);
    void text_grid_set_scale_mode(int scaleMode);
    int text_grid_get_scale_mode(void);
    void text_grid_get_dimensions(int* width, int* height);
    void text_grid_get_cell_size(float* width, float* height);
    void text_grid_cycle_mode(void);
    void text_grid_cycle_scale(void);
    int text_grid_find_best_mode(float viewportWidth, float viewportHeight);
    void text_grid_auto_fit(float viewportWidth, float viewportHeight);
    void text_grid_force_update(void);
    
    // Minimal graphics layer functions
    void minimal_graphics_layer_clear();
    void minimal_graphics_layer_set_ink(float r, float g, float b, float a);
    void minimal_graphics_layer_set_paper(float r, float g, float b, float a);
    void minimal_graphics_layer_set_line_width(float width);
    void minimal_graphics_layer_draw_line(float x1, float y1, float x2, float y2);
    void minimal_graphics_layer_draw_rect(float x, float y, float w, float h);
    void minimal_graphics_layer_fill_rect(float x, float y, float w, float h);
    void minimal_graphics_layer_draw_circle(float x, float y, float radius);
    void minimal_graphics_layer_fill_circle(float x, float y, float radius);
    void minimal_graphics_layer_draw_text(float x, float y, const char* text, float fontSize);
    void minimal_graphics_layer_draw_linear_gradient(float x1, float y1, float x2, float y2, float r1, float g1, float b1, float a1, float r2, float g2, float b2, float a2);
    void minimal_graphics_layer_fill_linear_gradient_rect(float x, float y, float w, float h, int direction, float r1, float g1, float b1, float a1, float r2, float g2, float b2, float a2);
    void minimal_graphics_layer_draw_radial_gradient(float centerX, float centerY, float radius, float r1, float g1, float b1, float a1, float r2, float g2, float b2, float a2);
    void minimal_graphics_layer_fill_radial_gradient_circle(float centerX, float centerY, float radius, float r1, float g1, float b1, float a1, float r2, float g2, float b2, float a2);
    bool minimal_graphics_layer_load_image(uint16_t imageId, const char* filename);
    void minimal_graphics_layer_draw_image(uint16_t imageId, float x, float y);
    void minimal_graphics_layer_draw_image_scaled(uint16_t imageId, float x, float y, float scale);
    void minimal_graphics_layer_draw_image_rect(uint16_t imageId, float srcX, float srcY, float srcW, float srcH, float dstX, float dstY, float dstW, float dstH);
    bool minimal_graphics_layer_create_image(uint16_t imageId, int width, int height, float r, float g, float b, float a);
    bool minimal_graphics_layer_save_image(uint16_t imageId, const char* filename);
    bool minimal_graphics_layer_capture_screen(uint16_t imageId, int x, int y, int width, int height);
    bool minimal_graphics_layer_get_image_size(uint16_t imageId, int* width, int* height);
    void minimal_graphics_layer_show();
    void minimal_graphics_layer_hide();
    void minimal_graphics_layer_swap();
    void minimal_graphics_layer_set_blend_mode(int blendMode);
    void minimal_graphics_layer_set_blur_filter(float radius);
    void minimal_graphics_layer_set_drop_shadow(float dx, float dy, float blur, float r, float g, float b, float a);
    void minimal_graphics_layer_set_color_matrix(const float matrix[20]);
    void minimal_graphics_layer_clear_filters();
    bool minimal_graphics_layer_read_pixels(uint16_t imageId, int x, int y, int width, int height, uint8_t** outPixels);
    bool minimal_graphics_layer_write_pixels(uint16_t imageId, int x, int y, int width, int height, const uint8_t* pixels);
    void minimal_graphics_layer_free_pixels(uint8_t* pixels);
    void minimal_graphics_layer_wait_queue_empty();
    
    // Matrix transformation functions
    void minimal_graphics_layer_push_matrix();
    void minimal_graphics_layer_pop_matrix();
    void minimal_graphics_layer_translate(float tx, float ty);
    void minimal_graphics_layer_rotate(float degrees);
    void minimal_graphics_layer_scale(float sx, float sy);
    void minimal_graphics_layer_skew(float kx, float ky);
    void minimal_graphics_layer_reset_matrix();
    
    // Path operations functions
    void minimal_graphics_layer_create_path(uint16_t pathId);
    void minimal_graphics_layer_path_move_to(uint16_t pathId, float x, float y);
    void minimal_graphics_layer_path_line_to(uint16_t pathId, float x, float y);
    void minimal_graphics_layer_path_curve_to(uint16_t pathId, float x1, float y1, float x2, float y2, float x3, float y3);
    void minimal_graphics_layer_path_close(uint16_t pathId);
    void minimal_graphics_layer_draw_path(uint16_t pathId);
    void minimal_graphics_layer_fill_path(uint16_t pathId);
    void minimal_graphics_layer_clip_path(uint16_t pathId, int clipOp, bool antiAlias);

    // Sprite layer functions
    bool sprite_layer_load(uint16_t id, const char* filename);
    bool sprite_layer_load_from_pixels(uint16_t id, const uint8_t* pixels, int width, int height);
    void sprite_layer_show(uint16_t id, float x, float y);
    void sprite_layer_hide(uint16_t id);
    void sprite_layer_move(uint16_t id, float x, float y);
    void sprite_layer_scale(uint16_t id, float scale);
    void sprite_layer_rotate(uint16_t id, float angle);
    void sprite_layer_alpha(uint16_t id, float alpha);
    void sprite_layer_release(uint16_t id);
    uint16_t sprite_layer_next_id();
    void sprite_layer_dump_state();
    
    // Sprite rendering functions
    bool minimal_graphics_layer_begin_sprite_render(uint16_t id, int width, int height);
    bool minimal_graphics_layer_end_sprite_render(uint16_t id);
    
    // Tile rendering functions
    bool minimal_graphics_layer_begin_tile_render(uint16_t id, int width, int height);
    bool minimal_graphics_layer_end_tile_render(uint16_t id);
    
    // Overlay Graphics Layer functions (Graphics Layer 2)
    bool overlay_graphics_layer_initialize(void* device, int width, int height);
    void overlay_graphics_layer_shutdown(void);
    bool overlay_graphics_layer_is_initialized(void);
    void overlay_graphics_layer_clear(void);
    void overlay_graphics_layer_clear_with_color(float r, float g, float b, float a);
    void overlay_graphics_layer_set_ink(float r, float g, float b, float a);
    void overlay_graphics_layer_set_paper(float r, float g, float b, float a);
    void overlay_graphics_layer_draw_line(float x1, float y1, float x2, float y2);
    void overlay_graphics_layer_draw_rect(float x, float y, float w, float h);
    void overlay_graphics_layer_fill_rect(float x, float y, float w, float h);
    void overlay_graphics_layer_draw_circle(float x, float y, float radius);
    void overlay_graphics_layer_fill_circle(float x, float y, float radius);
    void overlay_graphics_layer_draw_text(float x, float y, const char* text, float fontSize);
    void overlay_graphics_layer_set_blur_filter(float radius);
    void overlay_graphics_layer_set_drop_shadow(float dx, float dy, float blur, float r, float g, float b, float a);
    void overlay_graphics_layer_clear_filters(void);
    void overlay_graphics_layer_show(void);
    void overlay_graphics_layer_hide(void);
    bool overlay_graphics_layer_is_visible(void);
    void overlay_graphics_layer_present(void);
    void overlay_graphics_layer_render_overlay(void* encoder, void* projectionMatrix);
    
    // Tile creation functions
    bool tile_create_from_pixels_impl(uint16_t id, const uint8_t* pixels, int width, int height);
    bool tile_begin_render_impl(uint16_t id);
    bool tile_end_render_impl(uint16_t id);
    
    // TextEditor functions (from TextEditor.cpp)
    bool editor_toggle_impl();
    bool editor_is_active();
}

static void* g_window = nullptr;

extern "C" {

void superterminal_exit_legacy(int code);

void superterminal_run_legacy(void (*app_start)(void)) {
    std::cout << "SuperTerminal: Creating window..." << std::endl;
    
    // Create a 1024x768 window
    g_window = superterminal_create_window(1024, 768);
    
    if (!g_window) {
        std::cerr << "Failed to create window" << std::endl;
        return;
    }
    
    std::cout << "SuperTerminal: Window created successfully" << std::endl;
    std::cout << "SuperTerminal: Starting event loop..." << std::endl;
    
    // Start app function in background thread if provided
    if (app_start) {
        std::cout << "SuperTerminal: Starting app_start function in background..." << std::endl;
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            app_start();
            std::cout << "SuperTerminal: app_start function completed, initiating automatic cleanup..." << std::endl;
            superterminal_exit_legacy(0);
        });
    }
    
    // Run the main event loop
    superterminal_run_event_loop();
}

void superterminal_exit_legacy(int code) {
    std::cout << "SuperTerminal: Beginning shutdown sequence..." << std::endl;
    
    // Shutdown music system first (stops background threads)
    std::cout << "SuperTerminal: Shutting down music system..." << std::endl;
    if (music_is_playing()) {
        music_stop();
    }
    music_clear_queue();
    music_shutdown();
    
    // Shutdown audio system
    std::cout << "SuperTerminal: Shutting down audio system..." << std::endl;
    if (audio_is_initialized()) {
        audio_shutdown();
    }
    
    // Shutdown synthesis engine
    std::cout << "SuperTerminal: Shutting down synthesis engine..." << std::endl;
    synth_shutdown();
    
    // Cleanup Lua runtime
    std::cout << "SuperTerminal: Cleaning up Lua runtime..." << std::endl;
    lua_cleanup();
    
    // Give systems time to clean up gracefully
    std::cout << "SuperTerminal: Allowing subsystems to finish cleanup..." << std::endl;
    usleep(250000); // 250ms delay for graceful shutdown
    
    std::cout << "SuperTerminal: Shutdown complete. Exiting with code " << code << std::endl;
    exit(code);
}

// Input function implementations
bool isKeyPressed(int key) { 
    return superterminal_is_key_pressed(key); 
}

int key(void) { 
    return superterminal_get_key(); 
}

int getKey(void) {
    return superterminal_get_key();
}

// Immediate key polling for real-time applications
int getKeyImmediate(void) {
    return superterminal_get_key_immediate();
}

int waitKey(void) {
    return superterminal_wait_key();
}

int waitKeyChar(void) {
    return superterminal_wait_key_char();
}

// Font overdraw control functions
void setFontOverdraw(bool enabled) {
    truetype_set_font_overdraw(enabled);
}

bool getFontOverdraw(void) {
    return truetype_get_font_overdraw();
}

// Emphatic shutdown functions
void forceQuit(void) {
    superterminal_force_shutdown();
}

void emphaticExit(int code) {
    std::cout << "SuperTerminal: Emphatic exit requested with code " << code << std::endl;
    std::fflush(stdout);
    _exit(code); // Immediate termination
}

// Mouse function implementations
void mouse_get_position(float* x, float* y) {
    input_system_get_mouse_position(x, y);
}

bool mouse_is_pressed(int button) {
    return input_system_is_mouse_pressed(button);
}

bool mouse_wait_click(int* button, float* x, float* y) {
    // Forward declare interrupt check for graceful shutdown
    // Note: g_lua_should_interrupt is static in LuaRuntime.cpp but we forward-declare
    // it here to allow cross-compilation-unit interrupt checking
    extern std::atomic<bool> g_lua_should_interrupt;
    
    // Simple polling implementation - could be improved with semaphores later
    while (true) {
        // Check for shutdown/interrupt request
        if (is_emergency_shutdown_requested() || g_lua_should_interrupt.load()) {
            return false; // Return false to indicate interruption
        }
        
        for (int b = 0; b < 3; b++) {
            if (mouse_is_pressed(b)) {
                if (button) *button = b;
                mouse_get_position(x, y);
                
                // Wait for button release to avoid multiple clicks
                while (mouse_is_pressed(b)) {
                    // Check for interruption even while waiting for release
                    if (is_emergency_shutdown_requested() || g_lua_should_interrupt.load()) {
                        return false;
                    }
                    usleep(10000); // 10ms sleep
                }
                return true;
            }
        }
        usleep(10000); // 10ms sleep to avoid busy waiting
    }
    return false;
}

bool sprite_mouse_over(uint16_t sprite_id) {
    // TODO: Implement sprite collision detection with mouse
    // This requires access to sprite position and size data
    return false;
}

void mouse_screen_to_text(float screen_x, float screen_y, int* text_x, int* text_y) {
    // Convert screen coordinates to 80x25 text grid
    // Assuming standard terminal dimensions
    const float CHAR_WIDTH = 10.0f;  // Approximate character width in pixels
    const float CHAR_HEIGHT = 16.0f; // Approximate character height in pixels
    
    if (text_x) *text_x = (int)(screen_x / CHAR_WIDTH);
    if (text_y) *text_y = (int)(screen_y / CHAR_HEIGHT);
    
    // Clamp to valid text grid bounds
    if (text_x && *text_x < 0) *text_x = 0;
    if (text_x && *text_x >= 80) *text_x = 79;
    if (text_y && *text_y < 0) *text_y = 0;
    if (text_y && *text_y >= 25) *text_y = 24;
}

// External sprite menu system functions from SpriteMenuSystem.cpp
extern "C" {
    // Sprite menu system functions removed
}

// Sprite Menu System API wrapper functions
// Sprite menu system functions removed - using overlay graphics layer instead

// UI Layer API removed - using overlay graphics layer instead

// Text input implementation
char* accept_at(int x, int y) {
    // Simple text input at specified position
    // Clear the input line first
    set_color(rgba(255, 255, 255, 255), rgba(0, 0, 0, 0));
    print_at(x, y, "                                        "); // Clear line
    print_at(x, y, ""); // Position cursor
    
    std::string input;
    int cursor_pos = x;
    
    while (true) {
        int key = waitKey();
        
        if (key == ST_KEY_RETURN) {
            break;
        }
        else if (key == ST_KEY_ESCAPE) {
            return nullptr;
        }
        else if (key == ST_KEY_BACKSPACE || key == ST_KEY_DELETE) {
            if (!input.empty()) {
                input.pop_back();
                cursor_pos--;
                // Redraw the line
                print_at(x, y, "                                        "); // Clear
                print_at(x, y, input.c_str());
            }
        }
        else if (key >= 32 && key <= 126) { // Printable ASCII
            if (input.length() < 35) { // Limit input length
                input += (char)key;
                cursor_pos++;
                // Redraw the line
                print_at(x, y, input.c_str());
            }
        }
    }
    
    if (input.empty()) {
        return nullptr;
    }
    
    // Allocate and return C string (caller must free)
    char* result = (char*)malloc(input.length() + 1);
    strcpy(result, input.c_str());
    return result;
}

char* accept_ar(void) { 
    // Get current cursor position and delegate to accept_at
    // For now, use a default position
    return accept_at(0, 23); // Bottom of screen
}

void print(const char* text) {
    if (text) {
        std::cout << text << std::flush; // Also print to console for debugging
        truetype_terminal_print(text);
    }
}
void print_at(int x, int y, const char* text) {
    if (text) {
        truetype_terminal_print_at(x, y, text);
    }
}
void cls(void) { 
    coretext_terminal_clear();
}
void home(void) { 
    truetype_terminal_home();
}

void cursor_show(void) {}
void cursor_hide(void) {}
void cursor_move(int x, int y) {}

void set_color(uint32_t ink, uint32_t paper) {
    float ink_r = ((ink >> 16) & 0xFF) / 255.0f;
    float ink_g = ((ink >> 8) & 0xFF) / 255.0f;
    float ink_b = (ink & 0xFF) / 255.0f;
    float ink_a = ((ink >> 24) & 0xFF) / 255.0f;
    
    float paper_r = ((paper >> 16) & 0xFF) / 255.0f;
    float paper_g = ((paper >> 8) & 0xFF) / 255.0f;
    float paper_b = (paper & 0xFF) / 255.0f;
    float paper_a = ((paper >> 24) & 0xFF) / 255.0f;
    
    truetype_terminal_set_color(ink_r, ink_g, ink_b, ink_a, paper_r, paper_g, paper_b, paper_a);
}
void set_ink(uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    
    // Set color for both graphics and text layers
    minimal_graphics_layer_set_ink(r, g, b, a);
    truetype_terminal_set_ink(r, g, b, a);
}
void set_paper(uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    
    truetype_terminal_set_paper(r, g, b, a);
}

extern "C" {
void poke_colour(int layer, int x, int y, uint32_t ink_colour, uint32_t paper_colour) {
    truetype_poke_colour(layer, x, y, ink_colour, paper_colour);
}

void poke_ink(int layer, int x, int y, uint32_t ink_colour) {
    truetype_poke_ink(layer, x, y, ink_colour);
}

void poke_paper(int layer, int x, int y, uint32_t paper_colour) {
    truetype_poke_paper(layer, x, y, paper_colour);
}
}

// Editor text layer functions (Layer 6)
void editor_cls(void) {
    truetype_editor_clear();
}

void editor_print_at(int x, int y, const char* text) {
    if (text) {
        truetype_editor_print_at(x, y, text);
    }
}

void editor_set_color(uint32_t ink, uint32_t paper) {
    float ink_r = ((ink >> 16) & 0xFF) / 255.0f;
    float ink_g = ((ink >> 8) & 0xFF) / 255.0f;
    float ink_b = ((ink >> 0) & 0xFF) / 255.0f;
    float ink_a = ((ink >> 24) & 0xFF) / 255.0f;
    
    float paper_r = ((paper >> 16) & 0xFF) / 255.0f;
    float paper_g = ((paper >> 8) & 0xFF) / 255.0f;
    float paper_b = ((paper >> 0) & 0xFF) / 255.0f;
    float paper_a = ((paper >> 24) & 0xFF) / 255.0f;
    
    editor_set_cursor_colors(ink_r, ink_g, ink_b, ink_a, paper_r, paper_g, paper_b, paper_a);
}

void editor_background_color(uint32_t color) {
    // Set paper color for the entire editor layer
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 0) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    
    struct TextCell* buffer = editor_get_text_buffer();
    if (buffer) {
        for (int i = 0; i < 80 * 25; i++) {
#ifdef __OBJC__
            buffer[i].paperColor = simd_make_float4(r, g, b, a);
#else
            buffer[i].paperColor = {r, g, b, a};
#endif
        }
    }
}

// editor_get_text_buffer is already declared externally, no wrapper needed

// Editor visibility and toggle functions
// F1 = Editor ON, F3 = Editor OFF (NOT a toggle!)
bool editor_toggle(void) {
    printf("SuperTerminalAPI: editor_toggle() called\n");
    
    // Check current editor state - only activate if not already active
    extern bool editor_is_active();
    bool currently_active = editor_is_active();
    
    if (!currently_active) {
        // Save Layer 5 (terminal) state before switching to editor
        extern void text_mode_save_layer_state(int layer);
        text_mode_save_layer_state(5);
        printf("SuperTerminalAPI: Saved terminal (Layer 5) state\n");
        
        // F1: Activate editor - Enable Layer 6 BEFORE calling editor_toggle_impl so rendering works
        layer_set_enabled(6, true);
        
        // Restore Layer 6 (editor) state
        extern void text_mode_restore_layer_state(int layer);
        text_mode_restore_layer_state(6);
        printf("SuperTerminalAPI: Restored editor (Layer 6) state\n");
        
        extern bool editor_toggle_impl();
        bool now_active = editor_toggle_impl();
        
        printf("SuperTerminalAPI: editor_toggle_impl() returned: %s\n", now_active ? "true" : "false");
        printf("SuperTerminalAPI: Editor ACTIVATED, Layer 6 ENABLED\n");
        
        return now_active;
    } else {
        // Already active - F1 does nothing when editor is already on
        printf("SuperTerminalAPI: Editor already active - F1 ignored (use F3 to exit)\n");
        return true;
    }
}

// F3: Turn editor OFF
bool editor_deactivate_api(void) {
    printf("SuperTerminalAPI: editor_deactivate_api() called\n");
    
    // Save Layer 6 (editor) state before switching back to terminal
    extern void text_mode_save_layer_state(int layer);
    text_mode_save_layer_state(6);
    printf("SuperTerminalAPI: Saved editor (Layer 6) state\n");
    
    extern bool editor_is_active();
    bool currently_active = editor_is_active();
    
    if (currently_active) {
        // F3: Deactivate - Disable Layer 6 and set editor inactive
        layer_set_enabled(6, false);
        
        // Deactivate editor directly
        extern void editor_deactivate();
        editor_deactivate();
        
        // Restore Layer 5 (terminal) state after deactivating editor
        extern void text_mode_restore_layer_state(int layer);
        text_mode_restore_layer_state(5);
        printf("SuperTerminalAPI: Restored terminal (Layer 5) state\n");
        
        printf("SuperTerminalAPI: Editor DEACTIVATED, Layer 6 DISABLED\n");
        return false;
    } else {
        // Already inactive - F3 does nothing when editor is already off
        printf("SuperTerminalAPI: Editor already inactive - F3 ignored\n");
        return false;
    }
}

bool editor_visible(void) {
    extern bool editor_is_active();
    return editor_is_active();
}

// Short graphics API names
void line(float x1, float y1, float x2, float y2, uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 0) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    minimal_graphics_layer_set_ink(r, g, b, a);
    minimal_graphics_layer_draw_line(x1, y1, x2, y2);
}

void rect(float x, float y, float w, float h, uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 0) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    minimal_graphics_layer_set_ink(r, g, b, a);
    minimal_graphics_layer_draw_rect(x, y, w, h);
}

void circle(float x, float y, float radius, uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 0) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    minimal_graphics_layer_set_ink(r, g, b, a);
    minimal_graphics_layer_draw_circle(x, y, radius);
}

void draw_text(float x, float y, const char* text, float fontSize, uint32_t color) {
    printf("draw_text: Called with text='%s' x=%.1f y=%.1f fontSize=%.1f color=0x%08x\n", 
           text ? text : "NULL", x, y, fontSize, color);
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 0) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    printf("draw_text: Setting ink color (%.2f,%.2f,%.2f,%.2f)\n", r, g, b, a);
    minimal_graphics_layer_set_ink(r, g, b, a);
    printf("draw_text: Calling minimal_graphics_layer_draw_text\n");
    minimal_graphics_layer_draw_text(x, y, text, fontSize);
    printf("draw_text: Completed\n");
}

void fillrect(float x, float y, float w, float h, uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 0) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    minimal_graphics_layer_set_ink(r, g, b, a);
    minimal_graphics_layer_fill_rect(x, y, w, h);
}

void fillcircle(float x, float y, float radius, uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 0) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    minimal_graphics_layer_set_ink(r, g, b, a);
    minimal_graphics_layer_fill_circle(x, y, radius);
}

void gink(uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 0) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    minimal_graphics_layer_set_ink(r, g, b, a);
}

void gpaper(uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 0) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    minimal_graphics_layer_set_paper(r, g, b, a);
}

void gclear(void) {
    minimal_graphics_layer_clear();
}

void gswap(void) {
    minimal_graphics_layer_swap();
}

void gshow(void) {
    minimal_graphics_layer_show();
}

void ghide(void) {
    minimal_graphics_layer_hide();
}

// Legacy long names for compatibility  
void draw_line(float x1, float y1, float x2, float y2, uint32_t color) {
    line(x1, y1, x2, y2, color);
}

void draw_rect(float x, float y, float w, float h, uint32_t color) {
    rect(x, y, w, h, color);
}

void draw_circle(float x, float y, float radius, uint32_t color) {
    circle(x, y, radius, color);
}

void fill_rect(float x, float y, float w, float h, uint32_t color) {
    fillrect(x, y, w, h, color);
}

void fill_circle(float x, float y, float radius, uint32_t color) {
    fillcircle(x, y, radius, color);
}

void graphics_clear(void) {
    gclear();
}

void graphics_swap(void) {
    gswap();
}

void present(void) {
    gswap();
}

void draw_linear_gradient(float x1, float y1, float x2, float y2, uint32_t color1, uint32_t color2) {
    float r1 = ((color1 >> 16) & 0xFF) / 255.0f;
    float g1 = ((color1 >> 8) & 0xFF) / 255.0f;
    float b1 = ((color1 >> 0) & 0xFF) / 255.0f;
    float a1 = ((color1 >> 24) & 0xFF) / 255.0f;
    float r2 = ((color2 >> 16) & 0xFF) / 255.0f;
    float g2 = ((color2 >> 8) & 0xFF) / 255.0f;
    float b2 = ((color2 >> 0) & 0xFF) / 255.0f;
    float a2 = ((color2 >> 24) & 0xFF) / 255.0f;
    minimal_graphics_layer_draw_linear_gradient(x1, y1, x2, y2, r1, g1, b1, a1, r2, g2, b2, a2);
}

void fill_linear_gradient_rect(float x, float y, float w, float h, uint32_t color1, uint32_t color2, int direction) {
    float r1 = ((color1 >> 16) & 0xFF) / 255.0f;
    float g1 = ((color1 >> 8) & 0xFF) / 255.0f;
    float b1 = ((color1 >> 0) & 0xFF) / 255.0f;
    float a1 = ((color1 >> 24) & 0xFF) / 255.0f;
    float r2 = ((color2 >> 16) & 0xFF) / 255.0f;
    float g2 = ((color2 >> 8) & 0xFF) / 255.0f;
    float b2 = ((color2 >> 0) & 0xFF) / 255.0f;
    float a2 = ((color2 >> 24) & 0xFF) / 255.0f;
    minimal_graphics_layer_fill_linear_gradient_rect(x, y, w, h, direction, r1, g1, b1, a1, r2, g2, b2, a2);
}

void draw_radial_gradient(float centerX, float centerY, float radius, uint32_t color1, uint32_t color2) {
    float r1 = ((color1 >> 16) & 0xFF) / 255.0f;
    float g1 = ((color1 >> 8) & 0xFF) / 255.0f;
    float b1 = ((color1 >> 0) & 0xFF) / 255.0f;
    float a1 = ((color1 >> 24) & 0xFF) / 255.0f;
    float r2 = ((color2 >> 16) & 0xFF) / 255.0f;
    float g2 = ((color2 >> 8) & 0xFF) / 255.0f;
    float b2 = ((color2 >> 0) & 0xFF) / 255.0f;
    float a2 = ((color2 >> 24) & 0xFF) / 255.0f;
    minimal_graphics_layer_draw_radial_gradient(centerX, centerY, radius, r1, g1, b1, a1, r2, g2, b2, a2);
}

void fill_radial_gradient_circle(float centerX, float centerY, float radius, uint32_t color1, uint32_t color2) {
    float r1 = ((color1 >> 16) & 0xFF) / 255.0f;
    float g1 = ((color1 >> 8) & 0xFF) / 255.0f;
    float b1 = ((color1 >> 0) & 0xFF) / 255.0f;
    float a1 = ((color1 >> 24) & 0xFF) / 255.0f;
    float r2 = ((color2 >> 16) & 0xFF) / 255.0f;
    float g2 = ((color2 >> 8) & 0xFF) / 255.0f;
    float b2 = ((color2 >> 0) & 0xFF) / 255.0f;
    float a2 = ((color2 >> 24) & 0xFF) / 255.0f;
    minimal_graphics_layer_fill_radial_gradient_circle(centerX, centerY, radius, r1, g1, b1, a1, r2, g2, b2, a2);
}

bool image_load(uint16_t id, const char* filename) {
    return minimal_graphics_layer_load_image(id, filename);
}

void draw_image(uint16_t id, float x, float y) {
    minimal_graphics_layer_draw_image(id, x, y);
}

void draw_image_scaled(uint16_t id, float x, float y, float scale) {
    minimal_graphics_layer_draw_image_scaled(id, x, y, scale);
}

void draw_image_rect(uint16_t id, float srcX, float srcY, float srcW, float srcH, 
                     float dstX, float dstY, float dstW, float dstH) {
    minimal_graphics_layer_draw_image_rect(id, srcX, srcY, srcW, srcH, dstX, dstY, dstW, dstH);
}

extern "C" void set_blend_mode(int blendMode) {
    minimal_graphics_layer_set_blend_mode(blendMode);
}

extern "C" void set_blur_filter(float radius) {
    minimal_graphics_layer_set_blur_filter(radius);
}

extern "C" void set_drop_shadow(float dx, float dy, float blur, uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 0) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    minimal_graphics_layer_set_drop_shadow(dx, dy, blur, r, g, b, a);
}

extern "C" void set_color_matrix(const float matrix[20]) {
    minimal_graphics_layer_set_color_matrix(matrix);
}

extern "C" void clear_filters() {
    minimal_graphics_layer_clear_filters();
}

extern "C" bool read_pixels(uint16_t imageId, int x, int y, int width, int height, uint8_t** outPixels) {
    return minimal_graphics_layer_read_pixels(imageId, x, y, width, height, outPixels);
}

extern "C" bool write_pixels(uint16_t imageId, int x, int y, int width, int height, const uint8_t* pixels) {
    return minimal_graphics_layer_write_pixels(imageId, x, y, width, height, pixels);
}

extern "C" void free_pixels(uint8_t* pixels) {
    minimal_graphics_layer_free_pixels(pixels);
}

extern "C" void wait_queue_empty() {
    minimal_graphics_layer_wait_queue_empty();
}

// Matrix transformation C API functions
extern "C" void push_matrix() {
    minimal_graphics_layer_push_matrix();
}

extern "C" void pop_matrix() {
    minimal_graphics_layer_pop_matrix();
}

extern "C" void translate(float tx, float ty) {
    minimal_graphics_layer_translate(tx, ty);
}

extern "C" void rotate_degrees(float degrees) {
    minimal_graphics_layer_rotate(degrees);
}

extern "C" void scale(float sx, float sy) {
    minimal_graphics_layer_scale(sx, sy);
}

extern "C" void skew(float kx, float ky) {
    minimal_graphics_layer_skew(kx, ky);
}

extern "C" void reset_matrix() {
    minimal_graphics_layer_reset_matrix();
}

// Path operations C API functions
extern "C" void create_path(uint16_t pathId) {
    minimal_graphics_layer_create_path(pathId);
}

extern "C" void path_move_to(uint16_t pathId, float x, float y) {
    minimal_graphics_layer_path_move_to(pathId, x, y);
}

extern "C" void path_line_to(uint16_t pathId, float x, float y) {
    minimal_graphics_layer_path_line_to(pathId, x, y);
}

extern "C" void path_curve_to(uint16_t pathId, float x1, float y1, float x2, float y2, float x3, float y3) {
    minimal_graphics_layer_path_curve_to(pathId, x1, y1, x2, y2, x3, y3);
}

extern "C" void path_close(uint16_t pathId) {
    minimal_graphics_layer_path_close(pathId);
}

extern "C" void draw_path(uint16_t pathId) {
    minimal_graphics_layer_draw_path(pathId);
}

extern "C" void fill_path(uint16_t pathId) {
    minimal_graphics_layer_fill_path(pathId);
}

extern "C" void clip_path(uint16_t pathId, int clipOp, bool antiAlias) {
    minimal_graphics_layer_clip_path(pathId, clipOp, antiAlias);
}

bool image_create(uint16_t id, int width, int height, uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 0) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    return minimal_graphics_layer_create_image(id, width, height, r, g, b, a);
}

bool image_save(uint16_t id, const char* filename) {
    return minimal_graphics_layer_save_image(id, filename);
}

bool image_capture_screen(uint16_t id, int x, int y, int width, int height) {
    return minimal_graphics_layer_capture_screen(id, x, y, width, height);
}

bool image_get_size(uint16_t id, int* width, int* height) {
    return minimal_graphics_layer_get_image_size(id, width, height);
}

bool sprite_load(uint16_t id, const char* filename) {
    return sprite_layer_load(id, filename);
}

bool sprite_create_from_pixels(uint16_t id, const uint8_t* pixels, int width, int height) {
    return sprite_layer_load_from_pixels(id, pixels, width, height);
}

void sprite_show(uint16_t id, float x, float y) {
    sprite_layer_show(id, x, y);
}

void sprite_hide(uint16_t id) {
    sprite_layer_hide(id);
}

void sprite_move(uint16_t id, float x, float y) {
    sprite_layer_move(id, x, y);
}

void sprite_scale(uint16_t id, float scale) {
    sprite_layer_scale(id, scale);
}

void sprite_rotate(uint16_t id, float angle) {
    sprite_layer_rotate(id, angle);
}

void sprite_release(uint16_t id) {
    sprite_layer_release(id);
}

uint16_t sprite_next_id() {
    return sprite_layer_next_id();
}

void sprite_alpha(uint16_t id, float alpha) {
    sprite_layer_alpha(id, alpha);
}

bool sprite_begin_render(uint16_t id, int width, int height) {
    return minimal_graphics_layer_begin_sprite_render(id, width, height);
}

bool sprite_end_render(uint16_t id) {
    return minimal_graphics_layer_end_sprite_render(id);
}

bool tile_create_from_pixels(uint16_t id, const uint8_t* pixels, int width, int height) {
    extern bool tile_create_from_pixels_impl(uint16_t id, const uint8_t* pixels, int width, int height);
    return tile_create_from_pixels_impl(id, pixels, width, height);
}

bool tile_begin_render(uint16_t id) {
    extern bool tile_begin_render_impl(uint16_t id);
    return tile_begin_render_impl(id);
}

bool tile_end_render(uint16_t id) {
    extern bool tile_end_render_impl(uint16_t id);
    return tile_end_render_impl(id);
}

}

bool tile_load(uint16_t id, const char* filename) {
    return tile_load_impl(id, filename);
}

void tile_scroll(int layer, float dx, float dy) {
    tile_scroll_impl(layer, dx, dy);
}

void tile_set_viewport(int layer, int x, int y) {
    tile_set_viewport_impl(layer, x, y);
}

void tile_set(int layer, int map_x, int map_y, uint16_t tile_id) {
    tile_set_impl(layer, map_x, map_y, tile_id);
}

uint16_t tile_get(int layer, int map_x, int map_y) {
    return tile_get_impl(layer, map_x, map_y);
}

void tile_create_map(int layer, int width, int height) {
    tile_create_map_impl(layer, width, height);
}

void tile_resize_map(int layer, int new_width, int new_height) {
    tile_resize_map_impl(layer, new_width, new_height);
}

void tile_clear_map(int layer) {
    tile_clear_map_impl(layer);
}

// Comprehensive tile system cleanup functions
void tiles_clear(void) {
    tiles_clear_impl();
}

void tiles_shutdown(void) {
    tiles_shutdown_impl();
}

void tile_fill_map(int layer, uint16_t tile_id) {
    tile_fill_map_impl(layer, tile_id);
}

void tile_set_region(int layer, int start_x, int start_y, int width, int height, uint16_t tile_id) {
    tile_set_region_impl(layer, start_x, start_y, width, height, tile_id);
}

void tile_get_map_size(int layer, int* width, int* height) {
    tile_get_map_size_impl(layer, width, height);
}

void tile_center_viewport(int layer, int tile_x, int tile_y) {
    tile_center_viewport_impl(layer, tile_x, tile_y);
}

// UI element rendering functions
// UI render functions removed - using direct graphics drawing instead

bool tile_is_valid_position(int layer, int x, int y) {
    return tile_is_valid_position_impl(layer, x, y);
}

void background_color(uint32_t color) {
    if (g_window) {
        float r = ((color >> 0) & 0xFF) / 255.0f;
        float g = ((color >> 8) & 0xFF) / 255.0f;
        float b = ((color >> 16) & 0xFF) / 255.0f;
        float a = ((color >> 24) & 0xFF) / 255.0f;
        superterminal_set_background_color(g_window, r, g, b, a);
    }
}

uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}

uint64_t time_ms(void) {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
    return static_cast<uint64_t>(millis.count());
}

void sleep_ms(uint64_t ms) {
    usleep(ms * 1000);
}

void console(const char* message) {
    if (!message) return;
    
    // Execute console output on the UI thread via NSLog
    // Make a copy of the message string for the lambda
    std::string msg(message);
    
    g_command_queue.executeVoidCommand([msg]() {
        console_nslog(msg.c_str());
    });
}

void status(const char* text) {
    truetype_status(text);
}

// Global layer enable/disable state
static bool g_layer_enabled[10] = {true, true, true, true, true, true, false, true, true, true}; // Layer 6 (editor) disabled by default, Layer 9 (UI) enabled

void layer_set_enabled(int layer, bool enabled) {
    if (layer >= 1 && layer <= 9) {
        g_layer_enabled[layer] = enabled;
        printf("SuperTerminalAPI: Layer %d %s\n", layer, enabled ? "ENABLED" : "DISABLED");
    }
}

bool layer_is_enabled(int layer) {
    if (layer >= 1 && layer <= 9) {
        return g_layer_enabled[layer];
    }
    return false;
}

void layer_enable_all(void) {
    for (int i = 1; i <= 9; i++) {
        g_layer_enabled[i] = true;
    }
    printf("SuperTerminalAPI: All layers ENABLED\n");
}

void layer_disable_all(void) {
    for (int i = 1; i <= 9; i++) {
        g_layer_enabled[i] = false;
    }
    printf("SuperTerminalAPI: All layers DISABLED\n");
}

// C interface to check layer state from renderer
extern "C" bool superterminal_layer_is_enabled(int layer) {
    return layer_is_enabled(layer);
}

// Lua execution functions
bool lua_init_runtime(void) {
    return lua_init();
}

void lua_cleanup_runtime(void) {
    lua_cleanup();
}

// exec_lua wrapper functions removed - clean slate for reliable design

const char* lua_last_error(void) {
    return lua_gcd_get_last_error();
}

void wait_frame() {
    metal_renderer_wait_frame();
}

// Overlay Graphics Layer API functions (Graphics Layer 2)
bool overlay_initialize(void* device, int width, int height) {
    return overlay_graphics_layer_initialize(device, width, height);
}

void overlay_shutdown() {
    overlay_graphics_layer_shutdown();
}

bool overlay_is_initialized() {
    return overlay_graphics_layer_is_initialized();
}

void overlay_clear() {
    overlay_graphics_layer_clear();
}

void overlay_clear_with_color(float r, float g, float b, float a) {
    overlay_graphics_layer_clear_with_color(r, g, b, a);
}

void overlay_set_ink(float r, float g, float b, float a) {
    overlay_graphics_layer_set_ink(r, g, b, a);
}

void overlay_set_paper(float r, float g, float b, float a) {
    overlay_graphics_layer_set_paper(r, g, b, a);
}

void overlay_draw_line(float x1, float y1, float x2, float y2) {
    overlay_graphics_layer_draw_line(x1, y1, x2, y2);
}

void overlay_draw_rect(float x, float y, float w, float h) {
    overlay_graphics_layer_draw_rect(x, y, w, h);
}

void overlay_fill_rect(float x, float y, float w, float h) {
    overlay_graphics_layer_fill_rect(x, y, w, h);
}

void overlay_draw_circle(float x, float y, float radius) {
    overlay_graphics_layer_draw_circle(x, y, radius);
}

void overlay_fill_circle(float x, float y, float radius) {
    overlay_graphics_layer_fill_circle(x, y, radius);
}

void overlay_draw_text(float x, float y, const char* text, float fontSize) {
    overlay_graphics_layer_draw_text(x, y, text, fontSize);
}

void overlay_set_blur_filter(float radius) {
    overlay_graphics_layer_set_blur_filter(radius);
}

void overlay_set_drop_shadow(float dx, float dy, float blur, float r, float g, float b, float a) {
    overlay_graphics_layer_set_drop_shadow(dx, dy, blur, r, g, b, a);
}

void overlay_clear_filters() {
    overlay_graphics_layer_clear_filters();
}

void overlay_present() {
    overlay_graphics_layer_present();
}

void overlay_show() {
    overlay_graphics_layer_show();
}

void overlay_hide() {
    overlay_graphics_layer_hide();
}

bool overlay_is_visible() {
    return overlay_graphics_layer_is_visible();
}

// Text Grid Mode Functions
void setVideoMode(int mode) {
    text_grid_set_mode(mode);
}

int getVideoMode() {
    return text_grid_get_mode();
}

void setTextScale(int scale) {
    text_grid_set_scale_mode(scale);
}

int getTextScale() {
    return text_grid_get_scale_mode();
}

void cycleVideoMode() {
    text_grid_cycle_mode();
}

void cycleTextScale() {
    text_grid_cycle_scale();
}

void getTextGridSize(int* width, int* height) {
    text_grid_get_dimensions(width, height);
}

void getTextCellSize(float* width, float* height) {
    text_grid_get_cell_size(width, height);
}

void autoFitText() {
    // Get current viewport size from window system
    float width = 1024.0f;  // Default fallback
    float height = 768.0f;
    
    // TODO: Get actual viewport size from MetalRenderer
    text_grid_auto_fit(width, height);
}

int findBestVideoMode(float width, float height) {
    return text_grid_find_best_mode(width, height);
}

void refreshTextGrid() {
    text_grid_force_update();
}




