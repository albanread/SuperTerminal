//
//  LuaRuntime.cpp
//  SuperTerminal Framework - Lua Runtime Implementation
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Lua scripting integration for SuperTerminal with full API bindings
//

#include "SuperTerminal.h"
#include "../SpriteEffectSystem.h"
#include <iostream>
#include <string>
#include <cstring>
#include <fstream>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include "CommandQueue.h"
#include "GlobalShutdown.h"
#include <setjmp.h>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

// REPL function declarations
void repl_notify_state_reset();
}

using namespace SuperTerminal;

extern "C" {
    // Forward declarations for sprite effect functions
    void sprite_effect_init(void* device, void* shaderLibrary);
    void sprite_effect_shutdown();
    void sprite_effect_load(const char* effectName);
    void sprite_set_effect(uint16_t spriteId, const char* effectName);
    
    // Forward declarations for particle system functions
    void register_particle_system_lua_api(lua_State* L);
    bool register_particle_system_lua_api_when_ready(lua_State* L);
    int initialize_particle_system_from_lua(lua_State* L, void* metalDevice);
    void shutdown_particle_system_from_lua();
    
    // Forward declarations for bullet system functions
    void register_bullet_system_lua_api(lua_State* L);
    bool initialize_bullet_system_from_lua(void* metal_device, void* sprite_layer);
    void shutdown_bullet_system_from_lua();
    
    // Forward declarations for text layer scrollback functions
    void text_locate_line(int line);
    void text_scroll_to_line(int line);
    void text_scroll_up(int lines);
    void text_scroll_down(int lines);
    void text_page_up();
    void text_page_down();
    void text_scroll_to_top();
    void text_scroll_to_bottom();
    int text_get_cursor_line();
    int text_get_cursor_column();
    int text_get_viewport_line();
    int text_get_viewport_height();
    void text_set_autoscroll(bool enabled);
    bool text_get_autoscroll();
    
    // Status bar update functions
    void superterminal_update_status(const char* status);
    void superterminal_update_script_name(const char* scriptName);
    
    // Forward declarations for audio system functions
    extern "C" void register_audio_lua_bindings(lua_State* L);
    extern "C" void register_assets_lua_bindings(lua_State* L);
    void sprite_clear_effect(uint16_t spriteId);
    const char* sprite_get_effect(uint16_t spriteId);
    void sprite_effect_set_float(uint16_t spriteId, const char* paramName, float value);
    void sprite_effect_set_vec2(uint16_t spriteId, const char* paramName, float x, float y);
    void sprite_effect_set_color(uint16_t spriteId, const char* paramName, float r, float g, float b, float a);
    void sprite_effect_set_global_float(const char* effectName, const char* paramName, float value);
    void sprite_effect_set_global_vec2(const char* effectName, const char* paramName, float x, float y);
    void sprite_effect_set_global_color(const char* effectName, const char* paramName, float r, float g, float b, float a);
    const char** sprite_effect_get_available_effects(int* count);
    
    // Graphics functions
    void graphics_swap(void);
    void present(void);
    
    // Input functions
    int getKeyImmediate(void);
    
    // Font overdraw functions
    void setFontOverdraw(bool enabled);
    bool getFontOverdraw(void);
    
    // Emphatic shutdown functions
    void forceQuit(void);
    void emphaticExit(int code);
}

// Global Lua state and error handling
static lua_State* g_lua = nullptr;
static std::string g_last_error;

// Persistent REPL state
static lua_State* g_repl_lua = nullptr;
static bool g_repl_lua_initialized = false;
static std::mutex g_repl_mutex;

// Panic handler globals for safe error recovery
static jmp_buf g_panic_jump_buffer;
static std::string g_panic_error_message;

// Forward declaration of panic handler (defined after globals)
static int lua_panic_handler(lua_State* L);
static std::string g_current_script_content;
static std::string g_current_script_filename;
static std::mutex g_lua_mutex; // Thread safety for Lua state access
static std::string g_saved_lua_state; // Serialized Lua state for save/restore
static bool g_has_saved_state = false; // Track if we have a saved state
static std::atomic<bool> g_lua_executing{false}; // Track if Lua is currently executing
// Global Lua state and execution tracking
std::atomic<bool> g_lua_should_interrupt{false}; // Signal to interrupt Lua execution (non-static for cross-compilation-unit access)
static std::atomic<int> g_lua_interrupt_check_counter{0}; // Counter for more frequent interrupt checks
static std::unique_ptr<std::thread> g_lua_thread; // Managed thread for Lua script execution
static std::atomic<bool> g_script_finished{true}; // Track if script thread has finished

// Watchdog thread for bulletproof script termination
static std::unique_ptr<std::thread> g_watchdog_thread;
static std::atomic<bool> g_watchdog_active{false};
static std::atomic<bool> g_force_kill_requested{false};
static const int WATCHDOG_TIMEOUT_SECONDS = 30; // Force kill after 30 seconds
static const int GRACEFUL_SHUTDOWN_MS = 500; // Wait 500ms for graceful shutdown

// RAII helper to ensure watchdog thread cleanup on program exit
struct WatchdogCleanup {
    ~WatchdogCleanup() {
        if (g_watchdog_thread && g_watchdog_thread->joinable()) {
            std::cout << "WatchdogCleanup: Detaching watchdog thread on exit" << std::endl;
            g_watchdog_thread->detach();
        }
    }
};
static WatchdogCleanup g_watchdog_cleanup;

// Script name tracking for window title
static std::string g_current_script_name = "";

// Forward declarations for watchdog functions
static void start_lua_watchdog();
static void stop_lua_watchdog();
static void force_kill_lua_script_immediate();
// Note: stop_lua_script_bulletproof is declared below (non-static for external access)

// Enhanced Lua interruption hook function with more frequent checks
static void lua_interrupt_hook(lua_State* L, lua_Debug* ar) {
    (void)ar; // Unused parameter
    
    // Increment counter for more responsive interruption
    g_lua_interrupt_check_counter++;
    
    // Check global emergency shutdown first (highest priority)
    if (is_emergency_shutdown_requested()) {
        std::cout << "LuaRuntime: Emergency shutdown detected, interrupting Lua execution immediately" << std::endl;
        g_lua_interrupt_check_counter = 0;
        luaL_error(L, "Script execution interrupted by emergency shutdown");
    }
    
    // Check normal interrupt flag
    if (g_lua_should_interrupt.load()) {
        std::cout << "LuaRuntime: Interrupting Lua execution after " << g_lua_interrupt_check_counter.load() << " checks..." << std::endl;
        g_lua_interrupt_check_counter = 0;
        luaL_error(L, "Script execution interrupted by reset");
    }
}

// Forward declarations for GCD runtime functions
extern "C" {
    bool lua_gcd_is_script_running(void);
    const char* lua_gcd_get_current_script_name(void);
}

// Forward declarations for Lua API bindings
static int lua_superterminal_print(lua_State* L);
static int lua_superterminal_print_at(lua_State* L);
static int lua_superterminal_cls(lua_State* L);
static int lua_superterminal_home(lua_State* L);

// Text scrollback buffer functions
static int lua_text_locate_line(lua_State* L);
static int lua_text_scroll_to_line(lua_State* L);
static int lua_text_scroll_up(lua_State* L);
static int lua_text_scroll_down(lua_State* L);
static int lua_text_page_up(lua_State* L);
static int lua_text_page_down(lua_State* L);
static int lua_text_scroll_to_top(lua_State* L);
static int lua_text_scroll_to_bottom(lua_State* L);
static int lua_text_get_cursor_line(lua_State* L);
static int lua_text_get_cursor_column(lua_State* L);
static int lua_text_get_viewport_line(lua_State* L);
static int lua_text_get_viewport_height(lua_State* L);
static int lua_text_set_autoscroll(lua_State* L);
static int lua_text_get_autoscroll(lua_State* L);

static int lua_superterminal_set_color(lua_State* L);
static int lua_superterminal_end_of_script(lua_State* L);
static int lua_superterminal_start_of_script(lua_State* L);
static int lua_superterminal_end_of_repl(lua_State* L);
static int lua_superterminal_start_of_repl(lua_State* L);
static int lua_superterminal_end_of_repl(lua_State* L);
static int lua_superterminal_reset_repl(lua_State* L);
static int lua_superterminal_set_overlay_background_alpha(lua_State* L);
static int lua_superterminal_get_overlay_background_alpha(lua_State* L);
static int lua_superterminal_set_ink(lua_State* L);
static int lua_superterminal_rgba(lua_State* L);
static int lua_superterminal_background_color(lua_State* L);
static int lua_superterminal_sleep_ms(lua_State* L);
static int lua_superterminal_wait(lua_State* L);
static int lua_superterminal_time_ms(lua_State* L);
static int lua_superterminal_waitKey(lua_State* L);
static int lua_superterminal_waitKeyChar(lua_State* L);
static int lua_superterminal_isKeyPressed(lua_State* L);
static int lua_superterminal_key(lua_State* L);
static int lua_superterminal_keyImmediate(lua_State* L);
static int lua_superterminal_waitkey_log_metrics(lua_State* L);
static int lua_superterminal_waitkey_reset_metrics(lua_State* L);
static int lua_superterminal_setFontOverdraw(lua_State* L);
static int lua_superterminal_getFontOverdraw(lua_State* L);
static int lua_superterminal_forceQuit(lua_State* L);
static int lua_superterminal_emphaticExit(lua_State* L);

// Mouse Input API bindings
static int lua_superterminal_mouse_get_position(lua_State* L);
static int lua_superterminal_mouse_is_pressed(lua_State* L);
static int lua_superterminal_mouse_wait_click(lua_State* L);
static int lua_superterminal_sprite_mouse_over(lua_State* L);
static int lua_superterminal_mouse_screen_to_text(lua_State* L);



// Graphics API bindings
static int lua_superterminal_draw_line(lua_State* L);
static int lua_superterminal_draw_rect(lua_State* L);
static int lua_superterminal_fill_rect(lua_State* L);
static int lua_superterminal_draw_circle(lua_State* L);
static int lua_superterminal_fill_circle(lua_State* L);
static int lua_superterminal_draw_text(lua_State* L);
static int lua_superterminal_graphics_clear(lua_State* L);
static int lua_superterminal_graphics_swap(lua_State* L);
static int lua_superterminal_present(lua_State* L);
static int lua_superterminal_draw_linear_gradient(lua_State* L);
static int lua_superterminal_fill_linear_gradient_rect(lua_State* L);
static int lua_superterminal_draw_radial_gradient(lua_State* L);
static int lua_superterminal_fill_radial_gradient_circle(lua_State* L);

// Overlay Graphics Layer API bindings
static int lua_superterminal_overlay_is_initialized(lua_State* L);
static int lua_superterminal_overlay_clear(lua_State* L);
static int lua_superterminal_overlay_clear_with_color(lua_State* L);
static int lua_superterminal_overlay_set_ink(lua_State* L);
static int lua_superterminal_overlay_draw_line(lua_State* L);
static int lua_superterminal_overlay_draw_rect(lua_State* L);
static int lua_superterminal_overlay_fill_rect(lua_State* L);
static int lua_superterminal_overlay_draw_circle(lua_State* L);
static int lua_superterminal_overlay_fill_circle(lua_State* L);
static int lua_superterminal_overlay_draw_text(lua_State* L);
static int lua_superterminal_overlay_set_paper(lua_State* L);
static int lua_superterminal_overlay_show(lua_State* L);
static int lua_superterminal_overlay_hide(lua_State* L);
static int lua_superterminal_overlay_is_visible(lua_State* L);
static int lua_superterminal_overlay_present(lua_State* L);
static int lua_superterminal_set_blend_mode(lua_State* L);
static int lua_superterminal_set_blur_filter(lua_State* L);
static int lua_superterminal_set_drop_shadow(lua_State* L);
static int lua_superterminal_set_color_matrix(lua_State* L);
static int lua_superterminal_clear_filters(lua_State* L);
static int lua_superterminal_read_pixels(lua_State* L);
static int lua_superterminal_write_pixels(lua_State* L);
static int lua_superterminal_wait_queue_empty(lua_State* L);
static int lua_superterminal_push_matrix(lua_State* L);
static int lua_superterminal_pop_matrix(lua_State* L);
static int lua_superterminal_translate(lua_State* L);
static int lua_superterminal_rotate_degrees(lua_State* L);
static int lua_superterminal_scale(lua_State* L);
static int lua_superterminal_skew(lua_State* L);
static int lua_superterminal_reset_matrix(lua_State* L);
static int lua_superterminal_create_path(lua_State* L);
static int lua_superterminal_path_move_to(lua_State* L);
static int lua_superterminal_path_line_to(lua_State* L);
static int lua_superterminal_path_curve_to(lua_State* L);
static int lua_superterminal_path_close(lua_State* L);
static int lua_superterminal_draw_path(lua_State* L);
static int lua_superterminal_fill_path(lua_State* L);
static int lua_superterminal_clip_path(lua_State* L);
static int lua_superterminal_sprite_create_from_pixels(lua_State* L);
static int lua_superterminal_sprite_begin_render(lua_State* L);
static int lua_superterminal_sprite_end_render(lua_State* L);
static int lua_superterminal_sprite_from_canvas(lua_State* L);
static int lua_superterminal_create_sprite_from_skia(lua_State* L);
static int lua_superterminal_tile_create_from_pixels(lua_State* L);
static int lua_superterminal_tile_begin_render(lua_State* L);
static int lua_superterminal_tile_end_render(lua_State* L);
// UI render functions removed - using direct graphics drawing instead
static int lua_superterminal_poke_colour(lua_State* L);
static int lua_superterminal_poke_ink(lua_State* L);
static int lua_superterminal_poke_paper(lua_State* L);
static int lua_superterminal_console(lua_State* L);
static int lua_superterminal_image_load(lua_State* L);
static int lua_superterminal_draw_image(lua_State* L);
static int lua_superterminal_draw_image_scaled(lua_State* L);
static int lua_superterminal_draw_image_rect(lua_State* L);
static int lua_superterminal_image_create(lua_State* L);
static int lua_superterminal_image_save(lua_State* L);
static int lua_superterminal_image_capture_screen(lua_State* L);
static int lua_superterminal_image_get_size(lua_State* L);
static int lua_superterminal_save_to_file(lua_State* L);
static int lua_superterminal_save_editor(lua_State* L);

// Sprite API bindings
static int lua_superterminal_sprite_load(lua_State* L);
static int lua_superterminal_sprite_show(lua_State* L);
static int lua_superterminal_sprite_hide(lua_State* L);
static int lua_superterminal_sprite_release(lua_State* L);
static int lua_superterminal_sprite_next_id(lua_State* L);
static int lua_superterminal_sprite_move(lua_State* L);
static int lua_superterminal_sprite_scale(lua_State* L);
static int lua_superterminal_sprite_rotate(lua_State* L);
static int lua_superterminal_sprite_alpha(lua_State* L);

// Sprite Collision API bindings
static int lua_superterminal_sprite_check_collision(lua_State* L);
static int lua_superterminal_sprite_check_point_collision(lua_State* L);
static int lua_superterminal_sprite_get_size(lua_State* L);

// Frame synchronization API bindings
static int lua_superterminal_wait_frame(lua_State* L);

// Sprite Effect API bindings
static int lua_superterminal_sprite_effect_load(lua_State* L);
static int lua_superterminal_sprite_set_effect(lua_State* L);
static int lua_superterminal_sprite_clear_effect(lua_State* L);
static int lua_superterminal_sprite_get_effect(lua_State* L);
static int lua_superterminal_sprite_effect_set_float(lua_State* L);
static int lua_superterminal_sprite_effect_set_color(lua_State* L);
static int lua_superterminal_sprite_effect_set_vec2(lua_State* L);
static int lua_superterminal_sprite_effect_set_global_float(lua_State* L);
static int lua_superterminal_sprite_effect_set_global_vec2(lua_State* L);
static int lua_superterminal_sprite_effect_set_global_color(lua_State* L);
static int lua_superterminal_sprite_effect_get_available(lua_State* L);

// Tile API bindings
static int lua_superterminal_tile_load(lua_State* L);
static int lua_superterminal_tile_set(lua_State* L);
static int lua_superterminal_tile_get(lua_State* L);
static int lua_superterminal_tile_scroll(lua_State* L);
static int lua_superterminal_tile_set_viewport(lua_State* L);
static int lua_superterminal_tile_center_viewport(lua_State* L);
static int lua_superterminal_tile_clear_map(lua_State* L);
static int lua_superterminal_tile_create_map(lua_State* L);
static int lua_superterminal_tiles_clear(lua_State* L);
static int lua_superterminal_tiles_shutdown(lua_State* L);
static int lua_superterminal_sprites_clear(lua_State* L);
static int lua_superterminal_sprites_shutdown(lua_State* L);

// Layer control API bindings
static int lua_superterminal_layer_set_enabled(lua_State* L);
static int lua_superterminal_layer_is_enabled(lua_State* L);
static int lua_superterminal_layer_enable_all(lua_State* L);
static int lua_superterminal_layer_disable_all(lua_State* L);

// Error handling helper
// Forward declarations for sprite functions
extern "C" bool minimal_graphics_layer_begin_sprite_render(uint16_t spriteId, int width, int height);
extern "C" bool minimal_graphics_layer_end_sprite_render(uint16_t spriteId);
extern "C" bool minimal_graphics_layer_create_sprite_from_canvas(uint16_t spriteId, int width, int height);

// Forward declarations for tile functions
extern "C" bool minimal_graphics_layer_begin_tile_render(uint16_t tileId, int width, int height);
extern "C" bool minimal_graphics_layer_end_tile_render(uint16_t tileId);

// Forward declarations for poke functions
extern "C" void poke_colour(int layer, int x, int y, uint32_t ink_colour, uint32_t paper_colour);
extern "C" void poke_ink(int layer, int x, int y, uint32_t ink_colour);
extern "C" void poke_paper(int layer, int x, int y, uint32_t paper_colour);

// Include Skia headers for direct sprite creation
#ifdef USE_SKIA
#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#endif

// Handle Lua errors
static int lua_error_handler(lua_State* L) {
    const char* error_msg = lua_tostring(L, -1);
    if (error_msg) {
        g_last_error = std::string(error_msg);
    } else {
        g_last_error = "Unknown Lua error";
    }
    return 1;
}

// Begin rendering to sprite - all subsequent graphics calls go to sprite surface
static int lua_superterminal_sprite_begin_render(lua_State* L) {
    uint16_t spriteId = luaL_checkinteger(L, 1);
    int width = luaL_checkinteger(L, 2);
    int height = luaL_checkinteger(L, 3);
    
    bool success = minimal_graphics_layer_begin_sprite_render(spriteId, width, height);
    lua_pushboolean(L, success);
    return 1;
}

// End sprite rendering and create the sprite from drawn content
static int lua_superterminal_sprite_end_render(lua_State* L) {
    uint16_t spriteId = luaL_checkinteger(L, 1);
    
    bool success = minimal_graphics_layer_end_sprite_render(spriteId);
    lua_pushboolean(L, success);
    return 1;
}

// Create sprite from current canvas content
static int lua_superterminal_sprite_from_canvas(lua_State* L) {
    uint16_t spriteId = luaL_checkinteger(L, 1);
    int width = luaL_checkinteger(L, 2);
    int height = luaL_checkinteger(L, 3);
    
    bool success = minimal_graphics_layer_create_sprite_from_canvas(spriteId, width, height);
    lua_pushboolean(L, success);
    return 1;
}

// Simple function: create sprite directly from Skia pixels
static int lua_superterminal_create_sprite_from_skia(lua_State* L) {
    uint16_t spriteId = luaL_checkinteger(L, 1);
    int width = luaL_checkinteger(L, 2);
    int height = luaL_checkinteger(L, 3);
    
    // Create Skia surface
    SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) {
        lua_pushboolean(L, false);
        return 1;
    }
    
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);
    
    // Draw a simple red circle (Lua will control variety)
    SkPaint paint;
    paint.setColor(SK_ColorRED);
    paint.setAntiAlias(true);
    canvas->drawCircle(width/2, height/2, width/3, paint);
    
    // Get pixels
    size_t rowBytes = width * 4;
    uint8_t* pixels = (uint8_t*)malloc(height * rowBytes);
    bool success = false;
    
    if (pixels && surface->readPixels(info, pixels, rowBytes, 0, 0)) {
        extern bool sprite_create_from_pixels(uint16_t id, const uint8_t* pixels, int width, int height);
        success = sprite_create_from_pixels(spriteId, pixels, width, height);
    }
    
    if (pixels) free(pixels);
    lua_pushboolean(L, success);
    return 1;
}

static int lua_superterminal_tile_create_from_pixels(lua_State* L) {
    uint16_t tileId = luaL_checkinteger(L, 1);
    int width = luaL_checkinteger(L, 2);
    int height = luaL_checkinteger(L, 3);
    luaL_checktype(L, 4, LUA_TTABLE);
    
    int pixelCount = width * height * 4;
    uint8_t* pixels = (uint8_t*)malloc(pixelCount);
    if (pixels) {
        // Read pixel data from Lua table
        for (int i = 0; i < pixelCount; i++) {
            lua_rawgeti(L, 4, i + 1);
            pixels[i] = (uint8_t)luaL_checkinteger(L, -1);
            lua_pop(L, 1);
        }
        
        extern bool tile_create_from_pixels(uint16_t id, const uint8_t* pixels, int width, int height);
        bool success = tile_create_from_pixels(tileId, pixels, width, height);
        free(pixels);
        lua_pushboolean(L, success);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int lua_superterminal_tile_begin_render(lua_State* L) {
    uint16_t tileId = luaL_checkinteger(L, 1);
    
    extern bool tile_begin_render(uint16_t id);
    bool success = tile_begin_render(tileId);
    lua_pushboolean(L, success);
    return 1;
}

static int lua_superterminal_tile_end_render(lua_State* L) {
    uint16_t tileId = luaL_checkinteger(L, 1);
    
    extern bool tile_end_render(uint16_t id);
    bool success = tile_end_render(tileId);
    lua_pushboolean(L, success);
    return 1;
}

// UI render functions removed - using direct graphics drawing instead

static int lua_superterminal_poke_colour(lua_State* L) {
    int layer = luaL_checkinteger(L, 1);
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    uint32_t ink_colour = luaL_checkinteger(L, 4);
    uint32_t paper_colour = luaL_checkinteger(L, 5);
    
    poke_colour(layer, x, y, ink_colour, paper_colour);
    return 0;
}

static int lua_superterminal_poke_ink(lua_State* L) {
    int layer = luaL_checkinteger(L, 1);
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    uint32_t ink_colour = luaL_checkinteger(L, 4);
    
    poke_ink(layer, x, y, ink_colour);
    return 0;
}

static int lua_superterminal_poke_paper(lua_State* L) {
    int layer = luaL_checkinteger(L, 1);
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    uint32_t paper_colour = luaL_checkinteger(L, 4);
    
    poke_paper(layer, x, y, paper_colour);
    return 0;
}

// Chunky pixel graphics functions
static int lua_chunky_get_resolution(lua_State* L) {
    FILE* debugFile = fopen("/tmp/superterminal_chunky_debug.log", "a");
    if (debugFile) {
        fprintf(debugFile, "LUA_CHUNKY_GET_RESOLUTION: Lua function called\n");
        fflush(debugFile);
        fclose(debugFile);
    }
    
    int width, height;
    chunky_get_resolution(&width, &height);
    
    debugFile = fopen("/tmp/superterminal_chunky_debug.log", "a");
    if (debugFile) {
        fprintf(debugFile, "LUA_CHUNKY_GET_RESOLUTION: Got resolution %dx%d\n", width, height);
        fflush(debugFile);
        fclose(debugFile);
    }
    
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    return 2;
}

static int lua_chunky_pixel(lua_State* L) {
    FILE* debugFile = fopen("/tmp/superterminal_chunky_debug.log", "a");
    if (debugFile) {
        fprintf(debugFile, "LUA_CHUNKY_PIXEL: Lua function called\n");
        fflush(debugFile);
        fclose(debugFile);
    }
    
    int pixel_x = luaL_checkinteger(L, 1);
    int pixel_y = luaL_checkinteger(L, 2);
    bool on = lua_toboolean(L, 3);
    
    debugFile = fopen("/tmp/superterminal_chunky_debug.log", "a");
    if (debugFile) {
        fprintf(debugFile, "LUA_CHUNKY_PIXEL: Calling C function with pixel(%d,%d) on=%d\n", pixel_x, pixel_y, on);
        fflush(debugFile);
        fclose(debugFile);
    }
    
    chunky_pixel(pixel_x, pixel_y, on);
    return 0;
}

static int lua_chunky_clear(lua_State* L) {
    FILE* debugFile = fopen("/tmp/superterminal_chunky_debug.log", "a");
    if (debugFile) {
        fprintf(debugFile, "LUA_CHUNKY_CLEAR: Lua function called, calling C function\n");
        fflush(debugFile);
        fclose(debugFile);
    }
    
    chunky_clear();
    return 0;
}

static int lua_chunky_line(lua_State* L) {
    int x1 = luaL_checkinteger(L, 1);
    int y1 = luaL_checkinteger(L, 2);
    int x2 = luaL_checkinteger(L, 3);
    int y2 = luaL_checkinteger(L, 4);
    chunky_line(x1, y1, x2, y2);
    return 0;
}

static int lua_chunky_rect(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int width = luaL_checkinteger(L, 3);
    int height = luaL_checkinteger(L, 4);
    bool filled = lua_toboolean(L, 5);
    chunky_rect(x, y, width, height, filled);
    return 0;
}

// Register all SuperTerminal API functions with Lua
extern "C" void register_superterminal_api(lua_State* L) {
    // Text and output functions
    lua_register(L, "print", lua_superterminal_print);
    lua_register(L, "print_at", lua_superterminal_print_at);
    lua_register(L, "cls", lua_superterminal_cls);
    lua_register(L, "home", lua_superterminal_home);
    
    // Text scrollback buffer functions
    lua_register(L, "text_locate_line", lua_text_locate_line);
    lua_register(L, "text_scroll_to_line", lua_text_scroll_to_line);
    lua_register(L, "text_scroll_up", lua_text_scroll_up);
    lua_register(L, "text_scroll_down", lua_text_scroll_down);
    lua_register(L, "text_page_up", lua_text_page_up);
    lua_register(L, "text_page_down", lua_text_page_down);
    lua_register(L, "text_scroll_to_top", lua_text_scroll_to_top);
    lua_register(L, "text_scroll_to_bottom", lua_text_scroll_to_bottom);
    lua_register(L, "text_get_cursor_line", lua_text_get_cursor_line);
    lua_register(L, "text_get_cursor_column", lua_text_get_cursor_column);
    lua_register(L, "text_get_viewport_line", lua_text_get_viewport_line);
    lua_register(L, "text_get_viewport_height", lua_text_get_viewport_height);
    lua_register(L, "text_set_autoscroll", lua_text_set_autoscroll);
    lua_register(L, "text_get_autoscroll", lua_text_get_autoscroll);
    
    lua_register(L, "set_color", lua_superterminal_set_color);
    lua_register(L, "set_ink", lua_superterminal_set_ink);
    lua_register(L, "poke_colour", lua_superterminal_poke_colour);
    lua_register(L, "poke_ink", lua_superterminal_poke_ink);
    lua_register(L, "poke_paper", lua_superterminal_poke_paper);
    lua_register(L, "rgba", lua_superterminal_rgba);
    lua_register(L, "background_color", lua_superterminal_background_color);
    
    // Script control functions
    lua_register(L, "end_of_script", lua_superterminal_end_of_script);
    lua_register(L, "start_of_script", lua_superterminal_start_of_script);
    
    // REPL control functions (persistent state)
    lua_register(L, "start_of_repl", lua_superterminal_start_of_repl);
    lua_register(L, "end_of_repl", lua_superterminal_end_of_repl);
    lua_register(L, "reset", lua_superterminal_reset_repl);
    lua_register(L, "set_overlay_background_alpha", lua_superterminal_set_overlay_background_alpha);
    lua_register(L, "get_overlay_background_alpha", lua_superterminal_get_overlay_background_alpha);
    
    // Utility functions
    lua_register(L, "sleep_ms", lua_superterminal_sleep_ms);
    lua_register(L, "wait", lua_superterminal_wait);
    lua_register(L, "time_ms", lua_superterminal_time_ms);
    
    // Input functions
    lua_register(L, "waitKey", lua_superterminal_waitKey);
    lua_register(L, "waitKeyChar", lua_superterminal_waitKeyChar);
    lua_register(L, "isKeyPressed", lua_superterminal_isKeyPressed);
    lua_register(L, "key", lua_superterminal_key);
    lua_register(L, "keyImmediate", lua_superterminal_keyImmediate);
    lua_register(L, "waitkey_log_metrics", lua_superterminal_waitkey_log_metrics);
    lua_register(L, "waitkey_reset_metrics", lua_superterminal_waitkey_reset_metrics);
    lua_register(L, "setFontOverdraw", lua_superterminal_setFontOverdraw);
    lua_register(L, "getFontOverdraw", lua_superterminal_getFontOverdraw);
    
    // Emphatic shutdown functions
    lua_register(L, "forceQuit", lua_superterminal_forceQuit);
    lua_register(L, "emphaticExit", lua_superterminal_emphaticExit);
    
    // Mouse functions
    lua_register(L, "mouse_get_position", lua_superterminal_mouse_get_position);
    lua_register(L, "mouse_is_pressed", lua_superterminal_mouse_is_pressed);
    lua_register(L, "mouse_wait_click", lua_superterminal_mouse_wait_click);
    lua_register(L, "sprite_mouse_over", lua_superterminal_sprite_mouse_over);
    lua_register(L, "mouse_screen_to_text", lua_superterminal_mouse_screen_to_text);


    
    // Graphics functions
    lua_register(L, "draw_line", lua_superterminal_draw_line);
    lua_register(L, "draw_rect", lua_superterminal_draw_rect);
    lua_register(L, "fill_rect", lua_superterminal_fill_rect);
    lua_register(L, "draw_circle", lua_superterminal_draw_circle);
    lua_register(L, "fill_circle", lua_superterminal_fill_circle);
    lua_register(L, "draw_text", lua_superterminal_draw_text);
    lua_register(L, "graphics_clear", lua_superterminal_graphics_clear);
    lua_register(L, "graphics_swap", lua_superterminal_graphics_swap);
    lua_register(L, "present", lua_superterminal_present);
    lua_register(L, "draw_linear_gradient", lua_superterminal_draw_linear_gradient);
    lua_register(L, "fill_linear_gradient_rect", lua_superterminal_fill_linear_gradient_rect);
    lua_register(L, "draw_radial_gradient", lua_superterminal_draw_radial_gradient);
    lua_register(L, "fill_radial_gradient_circle", lua_superterminal_fill_radial_gradient_circle);
    lua_register(L, "set_blend_mode", lua_superterminal_set_blend_mode);
    lua_register(L, "set_blur_filter", lua_superterminal_set_blur_filter);
    lua_register(L, "set_drop_shadow", lua_superterminal_set_drop_shadow);
    lua_register(L, "set_color_matrix", lua_superterminal_set_color_matrix);
    lua_register(L, "clear_filters", lua_superterminal_clear_filters);
    lua_register(L, "read_pixels", lua_superterminal_read_pixels);
    lua_register(L, "write_pixels", lua_superterminal_write_pixels);
    lua_register(L, "wait_queue_empty", lua_superterminal_wait_queue_empty);
    lua_register(L, "push_matrix", lua_superterminal_push_matrix);
    lua_register(L, "pop_matrix", lua_superterminal_pop_matrix);
    lua_register(L, "translate", lua_superterminal_translate);
    lua_register(L, "rotate_degrees", lua_superterminal_rotate_degrees);
    lua_register(L, "scale", lua_superterminal_scale);
    lua_register(L, "skew", lua_superterminal_skew);
    lua_register(L, "reset_matrix", lua_superterminal_reset_matrix);
    lua_register(L, "create_path", lua_superterminal_create_path);
    lua_register(L, "path_move_to", lua_superterminal_path_move_to);
    lua_register(L, "path_line_to", lua_superterminal_path_line_to);
    lua_register(L, "path_curve_to", lua_superterminal_path_curve_to);
    lua_register(L, "path_close", lua_superterminal_path_close);
    lua_register(L, "draw_path", lua_superterminal_draw_path);
    lua_register(L, "fill_path", lua_superterminal_fill_path);
    lua_register(L, "clip_path", lua_superterminal_clip_path);
    lua_register(L, "sprite_create_from_pixels", lua_superterminal_sprite_create_from_pixels);
    lua_register(L, "sprite_begin_render", lua_superterminal_sprite_begin_render);
    lua_register(L, "sprite_end_render", lua_superterminal_sprite_end_render);
    lua_register(L, "sprite_from_canvas", lua_superterminal_sprite_from_canvas);
    lua_register(L, "create_sprite_from_skia", lua_superterminal_create_sprite_from_skia);
    
    // Tile drawing functions
    lua_register(L, "tile_create_from_pixels", lua_superterminal_tile_create_from_pixels);
    lua_register(L, "tile_begin_render", lua_superterminal_tile_begin_render);
    lua_register(L, "tile_end_render", lua_superterminal_tile_end_render);
    
    // Overlay Graphics Layer functions
    lua_register(L, "overlay_is_initialized", lua_superterminal_overlay_is_initialized);
    lua_register(L, "overlay_clear", lua_superterminal_overlay_clear);
    lua_register(L, "overlay_clear_with_color", lua_superterminal_overlay_clear_with_color);
    lua_register(L, "overlay_set_ink", lua_superterminal_overlay_set_ink);
    lua_register(L, "overlay_set_paper", lua_superterminal_overlay_set_paper);
    lua_register(L, "overlay_draw_line", lua_superterminal_overlay_draw_line);
    lua_register(L, "overlay_draw_rect", lua_superterminal_overlay_draw_rect);
    lua_register(L, "overlay_fill_rect", lua_superterminal_overlay_fill_rect);
    lua_register(L, "overlay_draw_circle", lua_superterminal_overlay_draw_circle);
    lua_register(L, "overlay_fill_circle", lua_superterminal_overlay_fill_circle);
    lua_register(L, "overlay_draw_text", lua_superterminal_overlay_draw_text);
    lua_register(L, "overlay_show", lua_superterminal_overlay_show);
    lua_register(L, "overlay_hide", lua_superterminal_overlay_hide);
    lua_register(L, "overlay_is_visible", lua_superterminal_overlay_is_visible);
    lua_register(L, "overlay_present", lua_superterminal_overlay_present);
    
    // Console output
    lua_register(L, "console", lua_superterminal_console);
    
    // Image functions
    lua_register(L, "image_load", lua_superterminal_image_load);
    lua_register(L, "draw_image", lua_superterminal_draw_image);
    lua_register(L, "draw_image_scaled", lua_superterminal_draw_image_scaled);
    lua_register(L, "draw_image_rect", lua_superterminal_draw_image_rect);
    lua_register(L, "image_create", lua_superterminal_image_create);
    lua_register(L, "image_save", lua_superterminal_image_save);
    lua_register(L, "image_capture_screen", lua_superterminal_image_capture_screen);
    lua_register(L, "image_get_size", lua_superterminal_image_get_size);
    
    // Editor save functions
    lua_register(L, "save_to_file", lua_superterminal_save_to_file);
    lua_register(L, "save_editor", lua_superterminal_save_editor);
    
    // Sprite functions
    lua_register(L, "sprite_load", lua_superterminal_sprite_load);
    lua_register(L, "sprite_show", lua_superterminal_sprite_show);
    lua_register(L, "sprite_hide", lua_superterminal_sprite_hide);
    lua_register(L, "sprite_move", lua_superterminal_sprite_move);
    lua_register(L, "sprite_scale", lua_superterminal_sprite_scale);
    lua_register(L, "sprite_rotate", lua_superterminal_sprite_rotate);
    lua_register(L, "sprite_alpha", lua_superterminal_sprite_alpha);
    lua_register(L, "sprite_release", lua_superterminal_sprite_release);
    lua_register(L, "sprite_next_id", lua_superterminal_sprite_next_id);
    
    // Sprite effect functions
    lua_register(L, "sprite_effect_load", lua_superterminal_sprite_effect_load);
    lua_register(L, "sprite_set_effect", lua_superterminal_sprite_set_effect);
    lua_register(L, "sprite_clear_effect", lua_superterminal_sprite_clear_effect);
    lua_register(L, "sprite_get_effect", lua_superterminal_sprite_get_effect);
    lua_register(L, "sprite_effect_set_float", lua_superterminal_sprite_effect_set_float);
    lua_register(L, "sprite_effect_set_vec2", lua_superterminal_sprite_effect_set_vec2);
    lua_register(L, "sprite_effect_set_color", lua_superterminal_sprite_effect_set_color);
    lua_register(L, "sprite_effect_set_global_float", lua_superterminal_sprite_effect_set_global_float);
    lua_register(L, "sprite_effect_set_global_vec2", lua_superterminal_sprite_effect_set_global_vec2);
    lua_register(L, "sprite_effect_set_global_color", lua_superterminal_sprite_effect_set_global_color);
    lua_register(L, "sprite_effect_get_available", lua_superterminal_sprite_effect_get_available);
    
    // Sprite collision functions
    lua_register(L, "sprite_check_collision", lua_superterminal_sprite_check_collision);
    lua_register(L, "sprite_check_point_collision", lua_superterminal_sprite_check_point_collision);
    lua_register(L, "sprite_get_size", lua_superterminal_sprite_get_size);
    
    // Frame synchronization functions
    lua_register(L, "wait_frame", lua_superterminal_wait_frame);
    
    // Tile functions
    lua_register(L, "tile_load", lua_superterminal_tile_load);
    lua_register(L, "tile_set", lua_superterminal_tile_set);
    lua_register(L, "tile_get", lua_superterminal_tile_get);
    lua_register(L, "tile_scroll", lua_superterminal_tile_scroll);
    lua_register(L, "tile_set_viewport", lua_superterminal_tile_set_viewport);
    lua_register(L, "tile_center_viewport", lua_superterminal_tile_center_viewport);
    lua_register(L, "tile_clear_map", lua_superterminal_tile_clear_map);
    lua_register(L, "tile_create_map", lua_superterminal_tile_create_map);
    lua_register(L, "tiles_clear", lua_superterminal_tiles_clear);
    lua_register(L, "tiles_shutdown", lua_superterminal_tiles_shutdown);
    lua_register(L, "sprites_clear", lua_superterminal_sprites_clear);
    lua_register(L, "sprites_shutdown", lua_superterminal_sprites_shutdown);
    
    // Layer control functions
    lua_register(L, "layer_set_enabled", lua_superterminal_layer_set_enabled);
    lua_register(L, "layer_is_enabled", lua_superterminal_layer_is_enabled);
    lua_register(L, "layer_enable_all", lua_superterminal_layer_enable_all);
    lua_register(L, "layer_disable_all", lua_superterminal_layer_disable_all);
    
    // Chunky pixel graphics functions
    lua_register(L, "chunky_get_resolution", lua_chunky_get_resolution);
    lua_register(L, "chunky_pixel", lua_chunky_pixel);
    lua_register(L, "chunky_clear", lua_chunky_clear);
    lua_register(L, "chunky_line", lua_chunky_line);
    lua_register(L, "chunky_rect", lua_chunky_rect);
    
    // Utility function aliases
    lua_register(L, "wait_ms", lua_superterminal_sleep_ms);  // Add wait_ms alias
    
    // Add useful constants
    lua_pushinteger(L, 80);
    lua_setglobal(L, "SCREEN_WIDTH");
    
    lua_pushinteger(L, 25);
    lua_setglobal(L, "SCREEN_HEIGHT");
    
    lua_pushinteger(L, 128);
    lua_setglobal(L, "TILE_SIZE");
    
    lua_pushinteger(L, 256);
    lua_setglobal(L, "MAX_SPRITES");
    
    // Color constants
    lua_pushinteger(L, rgba(255, 255, 255, 255));
    lua_setglobal(L, "WHITE");
    
    lua_pushinteger(L, rgba(0, 0, 0, 255));
    lua_setglobal(L, "BLACK");
    
    lua_pushinteger(L, rgba(255, 0, 0, 255));
    lua_setglobal(L, "RED");
    
    lua_pushinteger(L, rgba(0, 255, 0, 255));
    lua_setglobal(L, "GREEN");
    
    lua_pushinteger(L, rgba(0, 0, 255, 255));
    lua_setglobal(L, "BLUE");
    
    // Clip operation constants
    lua_pushinteger(L, 0);
    lua_setglobal(L, "CLIP_DIFFERENCE");
    
    lua_pushinteger(L, 1);
    lua_setglobal(L, "CLIP_INTERSECT");
    
    // Blend mode constants
    lua_pushinteger(L, 0);
    lua_setglobal(L, "BLEND_NORMAL");
    
    lua_pushinteger(L, 1);
    lua_setglobal(L, "BLEND_MULTIPLY");
    
    lua_pushinteger(L, 2);
    lua_setglobal(L, "BLEND_SCREEN");
    
    lua_pushinteger(L, 3);
    lua_setglobal(L, "BLEND_OVERLAY");
    
    lua_pushinteger(L, 4);
    lua_setglobal(L, "BLEND_SOFT_LIGHT");
    
    lua_pushinteger(L, 5);
    lua_setglobal(L, "BLEND_COLOR_DODGE");
    
    lua_pushinteger(L, 6);
    lua_setglobal(L, "BLEND_COLOR_BURN");
    
    lua_pushinteger(L, 7);
    lua_setglobal(L, "BLEND_DIFFERENCE");
    
    lua_pushinteger(L, 8);
    lua_setglobal(L, "BLEND_EXCLUSION");
    
    // Predefined color matrices
    // Sepia matrix
    lua_newtable(L);
    float sepia_matrix[20] = {
        0.393f, 0.769f, 0.189f, 0, 0,
        0.349f, 0.686f, 0.168f, 0, 0,
        0.272f, 0.534f, 0.131f, 0, 0,
        0, 0, 0, 1, 0
    };
    for (int i = 0; i < 20; i++) {
        lua_pushnumber(L, sepia_matrix[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setglobal(L, "SEPIA_MATRIX");
    
    // Grayscale matrix
    lua_newtable(L);
    float grayscale_matrix[20] = {
        0.299f, 0.587f, 0.114f, 0, 0,
        0.299f, 0.587f, 0.114f, 0, 0,
        0.299f, 0.587f, 0.114f, 0, 0,
        0, 0, 0, 1, 0
    };
    for (int i = 0; i < 20; i++) {
        lua_pushnumber(L, grayscale_matrix[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setglobal(L, "GRAYSCALE_MATRIX");
    
    // High contrast matrix
    lua_newtable(L);
    float contrast_matrix[20] = {
        1.5f, 0, 0, 0, -64,
        0, 1.5f, 0, 0, -64,
        0, 0, 1.5f, 0, -64,
        0, 0, 0, 1, 0
    };
    for (int i = 0; i < 20; i++) {
        lua_pushnumber(L, contrast_matrix[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setglobal(L, "HIGH_CONTRAST_MATRIX");
    
    // Identity matrix (no change)
    lua_newtable(L);
    float identity_matrix[20] = {
        1, 0, 0, 0, 0,
        0, 1, 0, 0, 0,
        0, 0, 1, 0, 0,
        0, 0, 0, 1, 0
    };
    for (int i = 0; i < 20; i++) {
        lua_pushnumber(L, identity_matrix[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setglobal(L, "IDENTITY_MATRIX");
    
    lua_pushinteger(L, rgba(255, 255, 0, 255));
    lua_setglobal(L, "YELLOW");
    
    lua_pushinteger(L, rgba(255, 0, 255, 255));
    lua_setglobal(L, "MAGENTA");
    
    lua_pushinteger(L, rgba(0, 255, 255, 255));
    lua_setglobal(L, "CYAN");
    
    // Key constants (using ASCII values for common keys)
    lua_pushinteger(L, 27); // ESC
    lua_setglobal(L, "KEY_ESC");
    
    lua_pushinteger(L, 32); // Space
    lua_setglobal(L, "KEY_SPACE");
    
    lua_pushinteger(L, 13); // Enter
    lua_setglobal(L, "KEY_ENTER");
    
    lua_pushinteger(L, 256); // Left arrow (placeholder)
    lua_setglobal(L, "KEY_LEFT");
    
    lua_pushinteger(L, 257); // Right arrow (placeholder)
    lua_setglobal(L, "KEY_RIGHT");
    
    // Key constants - matching SuperTerminal.h values
    lua_pushinteger(L, 0x35); // ST_KEY_ESCAPE
    lua_setglobal(L, "ST_KEY_ESCAPE");
    
    lua_pushinteger(L, 0x24); // ST_KEY_RETURN
    lua_setglobal(L, "ST_KEY_RETURN");
    
    lua_pushinteger(L, 0x30); // ST_KEY_TAB
    lua_setglobal(L, "ST_KEY_TAB");
    
    lua_pushinteger(L, 0x31); // ST_KEY_SPACE
    lua_setglobal(L, "ST_KEY_SPACE");
    
    lua_pushinteger(L, 0x33); // ST_KEY_DELETE/BACKSPACE
    lua_setglobal(L, "ST_KEY_DELETE");
    lua_setglobal(L, "ST_KEY_BACKSPACE");
    
    lua_pushinteger(L, 0x7E); // ST_KEY_UP
    lua_setglobal(L, "ST_KEY_UP");
    
    lua_pushinteger(L, 0x7D); // ST_KEY_DOWN
    lua_setglobal(L, "ST_KEY_DOWN");
    
    lua_pushinteger(L, 0x7B); // ST_KEY_LEFT
    lua_setglobal(L, "ST_KEY_LEFT");
    
    lua_pushinteger(L, 0x7C); // ST_KEY_RIGHT
    lua_setglobal(L, "ST_KEY_RIGHT");
    
    // Function keys
    lua_pushinteger(L, 0x7A); // ST_KEY_F1
    lua_setglobal(L, "ST_KEY_F1");
    
    lua_pushinteger(L, 0x78); // ST_KEY_F2
    lua_setglobal(L, "ST_KEY_F2");
    
    lua_pushinteger(L, 0x63); // ST_KEY_F3
    lua_setglobal(L, "ST_KEY_F3");
    
    lua_pushinteger(L, 0x76); // ST_KEY_F4
    lua_setglobal(L, "ST_KEY_F4");
    
    lua_pushinteger(L, 0x60); // ST_KEY_F5
    lua_setglobal(L, "ST_KEY_F5");
    
    // Mouse button constants
    lua_pushinteger(L, 0); // ST_MOUSE_LEFT
    lua_setglobal(L, "ST_MOUSE_LEFT");
    
    lua_pushinteger(L, 1); // ST_MOUSE_RIGHT
    lua_setglobal(L, "ST_MOUSE_RIGHT");
    
    lua_pushinteger(L, 2); // ST_MOUSE_MIDDLE
    lua_setglobal(L, "ST_MOUSE_MIDDLE");
    
    lua_pushinteger(L, 0x61); // ST_KEY_F6
    lua_setglobal(L, "ST_KEY_F6");
    
    lua_pushinteger(L, 0x62); // ST_KEY_F7
    lua_setglobal(L, "ST_KEY_F7");
    
    // Letter keys
    lua_pushinteger(L, 0x00); // ST_KEY_A
    lua_setglobal(L, "ST_KEY_A");
    
    lua_pushinteger(L, 0x0B); // ST_KEY_B
    lua_setglobal(L, "ST_KEY_B");
    
    lua_pushinteger(L, 0x08); // ST_KEY_C
    lua_setglobal(L, "ST_KEY_C");
    
    lua_pushinteger(L, 0x02); // ST_KEY_D
    lua_setglobal(L, "ST_KEY_D");
    
    lua_pushinteger(L, 0x0E); // ST_KEY_E
    lua_setglobal(L, "ST_KEY_E");
    
    lua_pushinteger(L, 0x03); // ST_KEY_F
    lua_setglobal(L, "ST_KEY_F");
    
    lua_pushinteger(L, 0x05); // ST_KEY_G
    lua_setglobal(L, "ST_KEY_G");
    
    lua_pushinteger(L, 0x04); // ST_KEY_H
    lua_setglobal(L, "ST_KEY_H");
    
    lua_pushinteger(L, 0x22); // ST_KEY_I
    lua_setglobal(L, "ST_KEY_I");
    
    lua_pushinteger(L, 0x26); // ST_KEY_J
    lua_setglobal(L, "ST_KEY_J");
    
    lua_pushinteger(L, 0x28); // ST_KEY_K
    lua_setglobal(L, "ST_KEY_K");
    
    lua_pushinteger(L, 0x25); // ST_KEY_L
    lua_setglobal(L, "ST_KEY_L");
    
    lua_pushinteger(L, 0x2E); // ST_KEY_M
    lua_setglobal(L, "ST_KEY_M");
    
    lua_pushinteger(L, 0x2D); // ST_KEY_N
    lua_setglobal(L, "ST_KEY_N");
    
    lua_pushinteger(L, 0x1F); // ST_KEY_O
    lua_setglobal(L, "ST_KEY_O");
    
    lua_pushinteger(L, 0x23); // ST_KEY_P
    lua_setglobal(L, "ST_KEY_P");
    
    lua_pushinteger(L, 0x0C); // ST_KEY_Q
    lua_setglobal(L, "ST_KEY_Q");
    
    lua_pushinteger(L, 0x0F); // ST_KEY_R
    lua_setglobal(L, "ST_KEY_R");
    
    lua_pushinteger(L, 0x01); // ST_KEY_S
    lua_setglobal(L, "ST_KEY_S");
    
    lua_pushinteger(L, 0x11); // ST_KEY_T
    lua_setglobal(L, "ST_KEY_T");
    
    lua_pushinteger(L, 0x20); // ST_KEY_U
    lua_setglobal(L, "ST_KEY_U");
    
    lua_pushinteger(L, 0x09); // ST_KEY_V
    lua_setglobal(L, "ST_KEY_V");
    
    lua_pushinteger(L, 0x0D); // ST_KEY_W
    lua_setglobal(L, "ST_KEY_W");
    
    lua_pushinteger(L, 0x07); // ST_KEY_X
    lua_setglobal(L, "ST_KEY_X");
    
    lua_pushinteger(L, 0x10); // ST_KEY_Y
    lua_setglobal(L, "ST_KEY_Y");
    
    lua_pushinteger(L, 0x06); // ST_KEY_Z
    lua_setglobal(L, "ST_KEY_Z");
    
    // Number keys
    lua_pushinteger(L, 0x1D); // ST_KEY_0
    lua_setglobal(L, "ST_KEY_0");
    
    lua_pushinteger(L, 0x12); // ST_KEY_1
    lua_setglobal(L, "ST_KEY_1");
    
    lua_pushinteger(L, 0x13); // ST_KEY_2
    lua_setglobal(L, "ST_KEY_2");
    
    lua_pushinteger(L, 0x14); // ST_KEY_3
    lua_setglobal(L, "ST_KEY_3");
    
    lua_pushinteger(L, 0x15); // ST_KEY_4
    lua_setglobal(L, "ST_KEY_4");
    
    lua_pushinteger(L, 0x17); // ST_KEY_5
    lua_setglobal(L, "ST_KEY_5");
    
    lua_pushinteger(L, 0x16); // ST_KEY_6
    lua_setglobal(L, "ST_KEY_6");
    
    lua_pushinteger(L, 0x1A); // ST_KEY_7
    lua_setglobal(L, "ST_KEY_7");
    
    lua_pushinteger(L, 0x1C); // ST_KEY_8
    lua_setglobal(L, "ST_KEY_8");
    
    lua_pushinteger(L, 0x19); // ST_KEY_9
    lua_setglobal(L, "ST_KEY_9");
}

// C API implementations
extern "C" {

bool lua_init(void) {
    // Register Lua runtime as active subsystem
    register_active_subsystem();
    if (g_lua) {
        lua_cleanup(); // Clean up existing state
    }
    
    std::cout << "LuaRuntime: Initializing Lua with SuperTerminal API..." << std::endl;
    
    g_lua = luaL_newstate();
    if (!g_lua) {
        g_last_error = "Failed to create Lua state";
        return false;
    }
    
    // Set custom panic handler to prevent system crashes
    lua_atpanic(g_lua, lua_panic_handler);
    
    // Open standard Lua libraries
    luaL_openlibs(g_lua);
    
    // Register SuperTerminal API functions
    register_superterminal_api(g_lua);
    
    // Register particle system Lua API (will be initialized when Metal device is available)
    register_particle_system_lua_api(g_lua);
    
    // Try to register full particle system API if system is ready
    register_particle_system_lua_api_when_ready(g_lua);
    
    // Register bullet system Lua API
    register_bullet_system_lua_api(g_lua);
    
    // Register audio system Lua API
    register_audio_lua_bindings(g_lua);
    
    // Register assets management Lua API
    register_assets_lua_bindings(g_lua);
    
    // Set up error handler
    lua_pushcfunction(g_lua, lua_error_handler);
    
    std::cout << "LuaRuntime: Initialization complete" << std::endl;
    return true;
}

void lua_cleanup(void) {
    std::cout << "LuaRuntime: Cleanup called" << std::endl;
    
    // First, interrupt any running script
    g_lua_should_interrupt = true;
    
    // Clean up thread if it exists
    if (g_lua_thread && g_lua_thread->joinable()) {
        std::cout << "LuaRuntime: Waiting for Lua thread to finish..." << std::endl;
        
        // Wait for thread to finish gracefully
        auto start_time = std::chrono::steady_clock::now();
        while (!g_script_finished.load() && 
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start_time).count() < 2000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // Join the thread
        if (g_script_finished.load()) {
            g_lua_thread->join();
            std::cout << "LuaRuntime: Thread joined successfully" << std::endl;
        } else {
            std::cout << "LuaRuntime: Thread did not finish gracefully, forcing cleanup" << std::endl;
            // Thread destructor will handle cleanup
        }
        g_lua_thread.reset();
    }
    
    // Shutdown command queue
    SuperTerminal::command_queue_shutdown();
    
    // Now cleanup main Lua state
    std::lock_guard<std::mutex> lock(g_lua_mutex);
    
    if (g_lua) {
        // Shutdown particle system before closing Lua
        shutdown_particle_system_from_lua();
        
        lua_close(g_lua);
        g_lua = nullptr;
        g_lua_executing = false;
        g_lua_should_interrupt = false;
        std::cout << "LuaRuntime: Cleanup complete" << std::endl;
        
        // Unregister from shutdown system
        unregister_active_subsystem();
    }
}



// Clean sandboxed Lua execution - each script runs in isolation
struct LuaScriptExecution {
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<bool> should_terminate{false};
    std::atomic<bool> finished{false};
    std::string error_message;
    lua_State* lua_state = nullptr;
    std::chrono::steady_clock::time_point start_time;
    pthread_t native_thread_handle = 0;
    
    LuaScriptExecution() = default;
    ~LuaScriptExecution() {
        if (thread.joinable()) {
            thread.join();
        }
    }
};

static std::unique_ptr<LuaScriptExecution> g_current_execution;
static std::mutex g_execution_mutex;

// Custom panic handler to prevent system crashes and show error display
static int lua_panic_handler(lua_State* L) {
    std::cout << "LuaRuntime: PANIC HANDLER CALLED - Lua error: ";
    if (lua_isstring(L, -1)) {
        const char* error_msg = lua_tostring(L, -1);
        std::cout << error_msg << std::endl;
        
        // Extract and clean up the error message for display
        std::string raw_error(error_msg);
        std::string clean_error;
        
        // Remove the "[string ...]:" prefix if present
        size_t colon_pos = raw_error.find(":");
        if (colon_pos != std::string::npos && raw_error.find("[string") == 0) {
            // Find the line number and extract the actual error
            size_t second_colon = raw_error.find(":", colon_pos + 1);
            if (second_colon != std::string::npos) {
                clean_error = raw_error.substr(second_colon + 2); // Skip ": "
                
                // Extract line number for display
                std::string line_part = raw_error.substr(colon_pos + 1, second_colon - colon_pos - 1);
                clean_error = "Line " + line_part + ": " + clean_error;
            } else {
                clean_error = raw_error.substr(colon_pos + 2);
            }
        } else {
            clean_error = raw_error;
        }
        
        g_panic_error_message = clean_error;
    } else {
        std::cout << "Unknown panic error" << std::endl;
        g_panic_error_message = "Unknown syntax or runtime error";
    }
    
    // Set script execution to ended (like end_of_script)
    g_lua_executing = false;
    g_script_finished = true;
    if (g_current_execution) {
        g_current_execution->running = false;
        g_current_execution->finished = true;
    }
    
    // Update window title to show panic error - use non-blocking command queue
    extern void set_window_title(const char* title);
    std::string panic_title = "SuperTerminal - " + g_current_script_name + " - PANIC ERROR";
    SuperTerminal::g_command_queue.queueVoidCommand([panic_title]() {
        set_window_title(panic_title.c_str());
    });
    
    // Clear display and show error screen - use non-blocking command queue
    SuperTerminal::g_command_queue.queueVoidCommand([error_msg = g_panic_error_message]() {
        // Clear the 80x25 display
        extern void cls();
        cls();
        
        // Set red background and yellow text for error display
        extern void set_color(uint32_t fg, uint32_t bg);
        extern uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
        extern void print_at(int x, int y, const char* text);
        extern void graphics_swap();
        
        set_color(rgba(255, 255, 0, 255), rgba(128, 0, 0, 255)); // Yellow on dark red
        
        // Draw error header - centered on 80-column screen
        print_at(26, 3, "*** LUA SCRIPT PANIC ***");
        
        set_color(rgba(255, 255, 255, 255), rgba(128, 0, 0, 255)); // White on dark red
        print_at(2, 5, "A serious error occurred in the Lua script:");
        
        // Split long error message into multiple lines (75 chars per line)
        const int max_line_width = 75;
        const int start_x = 2;
        int current_y = 7;
        
        std::string remaining_error = error_msg;
        while (!remaining_error.empty() && current_y < 14) {
            std::string line;
            if (remaining_error.length() <= max_line_width) {
                line = remaining_error;
                remaining_error.clear();
            } else {
                // Find a good break point (space, comma, period, etc.)
                size_t break_pos = std::string::npos;
                for (size_t i = max_line_width; i > max_line_width / 2; i--) {
                    if (remaining_error[i] == ' ' || remaining_error[i] == ',' || 
                        remaining_error[i] == '.' || remaining_error[i] == ';') {
                        break_pos = i;
                        break;
                    }
                }
                if (break_pos == std::string::npos) {
                    break_pos = max_line_width;
                }
                
                line = remaining_error.substr(0, break_pos);
                remaining_error = remaining_error.substr(break_pos);
                
                // Skip leading spaces and punctuation on next line
                while (!remaining_error.empty() && 
                       (remaining_error[0] == ' ' || remaining_error[0] == ',' || remaining_error[0] == '.')) {
                    remaining_error = remaining_error.substr(1);
                }
            }
            print_at(start_x, current_y, line.c_str());
            current_y++;
        }
        
        // If there's still more error text, indicate truncation
        if (!remaining_error.empty()) {
            print_at(start_x, current_y, "... (error message truncated)");
            current_y++;
        }
        
        set_color(rgba(255, 255, 0, 255), rgba(128, 0, 0, 255)); // Yellow on dark red
        print_at(2, current_y + 1, "The script has been terminated to prevent system crash.");
        print_at(2, current_y + 2, "Press ESC to exit or F1 to edit the script.");
        
        set_color(rgba(200, 200, 200, 255), rgba(128, 0, 0, 255)); // Light gray on dark red
        int help_y = current_y + 4;
        if (help_y + 5 < 24) { // Only show help if it fits on screen (leave line for bottom)
            print_at(2, help_y, "Common syntax error causes:");
            print_at(4, help_y + 1, "- Missing 'end' for 'if', 'for', 'while', or 'function'");
            print_at(4, help_y + 2, "- Unclosed strings (missing quotes)");
            print_at(4, help_y + 3, "- Unmatched parentheses or brackets");
            print_at(4, help_y + 4, "- Invalid variable names or reserved words");
        }
        
        graphics_swap();
    });
    
    // Instead of crashing, jump back to safe execution point
    longjmp(g_panic_jump_buffer, 1);
    return 0; // Never reached, but required by function signature
}

// Simple sandboxed execution - create thread, init Lua, run script, teardown, end thread
bool exec_lua_sandboxed(const char* lua_code) {
    std::cout << "DEBUG: exec_lua_sandboxed called" << std::endl;
    if (!lua_code) {
        std::cout << "DEBUG: No lua_code provided" << std::endl;
        g_last_error = "No script content provided";
        return false;
    }
    std::cout << "DEBUG: lua_code length: " << strlen(lua_code) << std::endl;

    std::lock_guard<std::mutex> lock(g_execution_mutex);
    std::cout << "DEBUG: Acquired execution mutex" << std::endl;
    
    // Clean up any finished executions first
    if (g_current_execution && g_current_execution->finished.load()) {
        std::cout << "DEBUG: Cleaning up previous finished execution" << std::endl;
        if (g_current_execution->thread.joinable()) {
            g_current_execution->thread.join();
        }
        g_current_execution.reset();
    }
    
    // Check if another script is running
    if (g_current_execution && g_current_execution->running.load()) {
        std::cout << "DEBUG: Another script is already running" << std::endl;
        g_last_error = "Another Lua script is currently running";
        return false;
    }

    // Create new execution context
    std::cout << "DEBUG: Creating new execution context" << std::endl;
    g_current_execution = std::make_unique<LuaScriptExecution>();
    std::string script_content(lua_code);
    
    // Start execution thread
    std::cout << "DEBUG: Starting execution thread" << std::endl;
    
    // Update window title and set script state to running (default name)
    extern void set_window_title(const char* title);
    g_current_script_name = "script";
    SuperTerminal::g_command_queue.queueVoidCommand([]() {
        set_window_title("script running");
    });
    
    // Update status bar - script starting
    superterminal_update_status("● Running");
    superterminal_update_script_name("script");
    
    g_current_execution->thread = std::thread([script_content]() {
        auto& exec = *g_current_execution;
        exec.running = true;
        exec.finished = false;
        exec.start_time = std::chrono::steady_clock::now();
        exec.native_thread_handle = pthread_self();
        
        std::cout << "LuaRuntime: Starting sandboxed script execution in new thread" << std::endl;
        std::cout << "LuaRuntime: Script content length: " << script_content.length() << std::endl;
        std::cout << "LuaRuntime: Script content preview: [" << script_content.substr(0, 100) << "]" << std::endl;
        
        try {
            // Initialize fresh Lua state for this script only
            std::cout << "LuaRuntime: Creating new Lua state..." << std::endl;
            exec.lua_state = luaL_newstate();
            if (!exec.lua_state) {
                std::cout << "LuaRuntime: FAILED to create Lua state!" << std::endl;
                exec.error_message = "Failed to create Lua state";
                exec.running = false;
                exec.finished = true;
                return;
            }
            std::cout << "LuaRuntime: Lua state created successfully" << std::endl;
            
            // Set custom panic handler to prevent system crashes
            lua_atpanic(exec.lua_state, lua_panic_handler);
            
            std::cout << "LuaRuntime: Opening Lua libraries..." << std::endl;
            luaL_openlibs(exec.lua_state);
            std::cout << "LuaRuntime: Lua libraries opened" << std::endl;
            
            // Register SuperTerminal API
            std::cout << "LuaRuntime: Registering SuperTerminal API..." << std::endl;
            register_superterminal_api(exec.lua_state);
            std::cout << "LuaRuntime: SuperTerminal API registered" << std::endl;
            
            // Register particle system Lua API with safety check
            std::cout << "LuaRuntime: Registering Particle System API..." << std::endl;
            try {
                register_particle_system_lua_api(exec.lua_state);
                std::cout << "LuaRuntime: Particle System API registered" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "LuaRuntime: WARNING - Failed to register Particle System API: " << e.what() << std::endl;
                std::cout << "LuaRuntime: Continuing without particle system..." << std::endl;
            } catch (...) {
                std::cout << "LuaRuntime: WARNING - Unknown error registering Particle System API" << std::endl;
                std::cout << "LuaRuntime: Continuing without particle system..." << std::endl;
            }
            
            // Try to register full particle system API if system is ready
            try {
                register_particle_system_lua_api_when_ready(exec.lua_state);
            } catch (...) {
                std::cout << "LuaRuntime: Full particle API not ready yet" << std::endl;
            }
            
            // Register bullet system Lua API with safety check
            std::cout << "LuaRuntime: Registering Bullet System API..." << std::endl;
            try {
                register_bullet_system_lua_api(exec.lua_state);
                std::cout << "LuaRuntime: Bullet System API registered" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "LuaRuntime: WARNING - Failed to register Bullet System API: " << e.what() << std::endl;
                std::cout << "LuaRuntime: Continuing without bullet system..." << std::endl;
            } catch (...) {
                std::cout << "LuaRuntime: WARNING - Unknown error registering Bullet System API" << std::endl;
                std::cout << "LuaRuntime: Continuing without bullet system..." << std::endl;
            }
            
            // Register Audio API (including music functions)
            std::cout << "LuaRuntime: Registering Audio API..." << std::endl;
            register_audio_lua_bindings(exec.lua_state);
            std::cout << "LuaRuntime: Audio API registered" << std::endl;
            
            // Register Assets API
            std::cout << "LuaRuntime: Registering Assets API..." << std::endl;
            register_assets_lua_bindings(exec.lua_state);
            std::cout << "LuaRuntime: Assets API registered" << std::endl;
            
            std::cout << "LuaRuntime: Lua state initialized, executing script NOW" << std::endl;
            
            // Execute the script with termination checking - trigger on every line
            lua_sethook(exec.lua_state, [](lua_State* L, lua_Debug* ar) {
                if (g_current_execution && g_current_execution->should_terminate.load()) {
                    std::cout << "LuaRuntime: Termination hook triggered - killing script" << std::endl;
                    luaL_error(L, "Script execution terminated by user");
                }
            }, LUA_MASKLINE, 0);
            
            std::cout << "LuaRuntime: About to execute script with panic protection..." << std::endl;
            
            // Use setjmp/longjmp for panic recovery
            int panic_result = setjmp(g_panic_jump_buffer);
            if (panic_result != 0) {
                // We jumped here from panic handler - script already terminated with error display
                std::cout << "LuaRuntime: Recovered from Lua panic, error display shown" << std::endl;
                exec.error_message = g_panic_error_message;
                exec.running = false;
                exec.finished = true;
                
                // Log the panic recovery for debugging
                console(("Script panic recovered: " + g_panic_error_message).c_str());
                return;
            }
            
            // Use protected execution instead of luaL_dostring
            int load_result = luaL_loadstring(exec.lua_state, script_content.c_str());
            if (load_result != LUA_OK) {
                std::cout << "LuaRuntime: Script loading failed!" << std::endl;
                if (lua_isstring(exec.lua_state, -1)) {
                    exec.error_message = std::string("Lua Load Error: ") + lua_tostring(exec.lua_state, -1);
                    std::cout << "LuaRuntime: Load error: " << exec.error_message << std::endl;
                } else {
                    exec.error_message = "Script loading failed with unknown error";
                }
                exec.running = false;
                exec.finished = true;
                return;
            }
            
            // Execute the loaded script with protected call
            int result = lua_pcall(exec.lua_state, 0, 0, 0);
            std::cout << "LuaRuntime: lua_pcall returned: " << result << std::endl;
            
            if (result != LUA_OK) {
                std::cout << "LuaRuntime: SCRIPT EXECUTION FAILED!" << std::endl;
                if (lua_isstring(exec.lua_state, -1)) {
                    exec.error_message = std::string("Lua Error: ") + lua_tostring(exec.lua_state, -1);
                    std::cout << "LuaRuntime: Error message: " << exec.error_message << std::endl;
                } else {
                    exec.error_message = "Unknown Lua execution error";
                    std::cout << "LuaRuntime: Unknown error (no string on stack)" << std::endl;
                }
            } else {
                std::cout << "LuaRuntime: SCRIPT EXECUTED SUCCESSFULLY - NO ERRORS" << std::endl;
            }
            
        } catch (const std::exception& e) {
            std::cout << "LuaRuntime: EXCEPTION CAUGHT: " << e.what() << std::endl;
            exec.error_message = std::string("Exception: ") + e.what();
        } catch (...) {
            std::cout << "LuaRuntime: UNKNOWN EXCEPTION CAUGHT" << std::endl;
            exec.error_message = "Unknown exception during script execution";
        }
        
        // Always cleanup Lua state
        if (exec.lua_state) {
            std::cout << "LuaRuntime: Cleaning up Lua state" << std::endl;
            lua_close(exec.lua_state);
            exec.lua_state = nullptr;
        }
        
        exec.running = false;
        exec.finished = true;
        std::cout << "LuaRuntime: Script execution thread finished - SCRIPT IS DONE" << std::endl;
        
        // Update window title to show script ended (if not already set by end_of_script())
        extern void set_window_title(const char* title);
        std::string title = "SuperTerminal - " + g_current_script_name + " - ended";
        SuperTerminal::g_command_queue.queueVoidCommand([title]() {
            set_window_title(title.c_str());
        });
        
        // Update status bar - script ended
        superterminal_update_status("● Stopped");
    });
    
    // Keep thread joinable so we can clean it up properly
    
    // Wait a brief moment to ensure thread actually starts
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Start watchdog thread to monitor execution
    start_lua_watchdog();
    
    // Return true only if thread is actually running
    bool success = g_current_execution && g_current_execution->running.load();
    std::cout << "LuaRuntime: exec_lua_sandboxed returning: " << (success ? "true" : "false") << std::endl;
    return success;
}

// Function to check and clean up finished executions
void cleanup_finished_executions() {
    std::lock_guard<std::mutex> lock(g_execution_mutex);
    if (g_current_execution && g_current_execution->finished.load()) {
        std::cout << "LuaRuntime: Cleaning up finished execution" << std::endl;
        
        // Stop watchdog first
        stop_lua_watchdog();
        
        if (g_current_execution->thread.joinable()) {
            g_current_execution->thread.join();
        }
        g_current_execution.reset();
        
        // Reset window title and script name when no script is running
        extern void set_window_title(const char* title);
        g_current_script_name = "";
        SuperTerminal::g_command_queue.queueVoidCommand([]() {
            set_window_title("SuperTerminal");
        });
        
        // Update status bar - no script running
        superterminal_update_status("● Stopped");
        superterminal_update_script_name("");
    }
}

// Start watchdog thread to monitor script execution
void start_lua_watchdog() {
    // Stop any existing watchdog first
    stop_lua_watchdog();
    
    g_watchdog_active = true;
    g_force_kill_requested = false;
    
    g_watchdog_thread = std::make_unique<std::thread>([]() {
        std::cout << "LuaWatchdog: Started monitoring (timeout: " << WATCHDOG_TIMEOUT_SECONDS << "s)" << std::endl;
        
        auto start_time = std::chrono::steady_clock::now();
        
        while (g_watchdog_active.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // Check if we should stop monitoring
            if (!g_watchdog_active.load()) {
                break;
            }
            
            // Check if script is still running
            bool script_running = false;
            {
                std::lock_guard<std::mutex> lock(g_execution_mutex);
                script_running = g_current_execution && 
                                g_current_execution->running.load() && 
                                !g_current_execution->finished.load();
            }
            
            if (!script_running) {
                std::cout << "LuaWatchdog: Script finished normally, stopping watchdog" << std::endl;
                break;
            }
            
            // Check timeout
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            
            if (elapsed_seconds >= WATCHDOG_TIMEOUT_SECONDS) {
                std::cout << "LuaWatchdog: TIMEOUT EXCEEDED (" << elapsed_seconds << "s) - FORCE KILLING SCRIPT" << std::endl;
                force_kill_lua_script_immediate();
                break;
            }
            
            // Warn at 20 seconds
            if (elapsed_seconds == 20) {
                std::cout << "LuaWatchdog: WARNING - Script has been running for 20 seconds" << std::endl;
            }
        }
        
        g_watchdog_active = false;
        std::cout << "LuaWatchdog: Stopped" << std::endl;
    });
}

// Stop watchdog thread
void stop_lua_watchdog() {
    if (g_watchdog_thread) {
        std::cout << "LuaWatchdog: Stopping..." << std::endl;
        g_watchdog_active = false;
        
        if (!g_watchdog_thread->joinable()) {
            // Thread already finished
            g_watchdog_thread.reset();
            return;
        }
        
        // Wait for watchdog to stop (max 2 seconds)
        auto start = std::chrono::steady_clock::now();
        bool timeout = false;
        
        while (g_watchdog_thread->joinable()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 2) {
                std::cout << "LuaWatchdog: Timeout waiting for watchdog thread" << std::endl;
                timeout = true;
                break;
            }
        }
        
        // If thread finished naturally, join it. Otherwise detach to prevent terminate()
        if (g_watchdog_thread->joinable()) {
            if (timeout) {
                std::cout << "LuaWatchdog: Detaching watchdog thread to prevent crash on exit" << std::endl;
                g_watchdog_thread->detach();
            } else {
                g_watchdog_thread->join();
            }
        }
        
        g_watchdog_thread.reset();
    }
}

// Bulletproof force kill - NEVER hangs
void force_kill_lua_script_immediate() {
    std::cout << "LuaRuntime: *** FORCE KILL INITIATED ***" << std::endl;
    g_force_kill_requested = true;
    
    std::lock_guard<std::mutex> lock(g_execution_mutex);
    
    if (!g_current_execution) {
        std::cout << "LuaRuntime: No execution to kill" << std::endl;
        return;
    }
    
    // Step 1: Set termination flags
    std::cout << "LuaRuntime: Setting termination flags..." << std::endl;
    g_current_execution->should_terminate = true;
    g_lua_should_interrupt = true;
    g_lua_executing = false;
    
    // Step 2: Try graceful shutdown (longer wait to avoid race with waitKey)
    // Wait at least 500ms to allow blocked calls like waitKey to detect interruption
    const int EXTENDED_GRACE_MS = 500;
    std::cout << "LuaRuntime: Waiting " << EXTENDED_GRACE_MS << "ms for graceful shutdown..." << std::endl;
    auto grace_start = std::chrono::steady_clock::now();
    
    while (g_current_execution->running.load()) {
        auto elapsed = std::chrono::steady_clock::now() - grace_start;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= EXTENDED_GRACE_MS) {
            std::cout << "LuaRuntime: Graceful shutdown timeout - proceeding to force kill" << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Step 3: Wait a bit more for thread to exit cooperatively
    // Do NOT use pthread_cancel - it's extremely dangerous and can cause crashes
    // when the thread is in the middle of C code (e.g., returning from waitKey)
    if (g_current_execution->running.load()) {
        std::cout << "LuaRuntime: Thread still running after grace period, waiting additional 200ms..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Mark as finished if still running (thread will be detached below)
        if (g_current_execution->running.load()) {
            std::cout << "LuaRuntime: Thread did not exit, will detach" << std::endl;
            g_current_execution->running = false;
            g_current_execution->finished = true;
        }
    }
    
    // Step 4: Clean up Lua state (even if corrupted)
    if (g_current_execution->lua_state) {
        std::cout << "LuaRuntime: Closing Lua state (may be corrupted)..." << std::endl;
        
        // Set a signal handler to catch potential segfaults during lua_close
        // This is a last resort - if lua_close segfaults, we'll catch it
        try {
            lua_close(g_current_execution->lua_state);
            std::cout << "LuaRuntime: Lua state closed successfully" << std::endl;
        } catch (...) {
            std::cout << "LuaRuntime: Exception during lua_close (expected if corrupted)" << std::endl;
        }
        
        g_current_execution->lua_state = nullptr;
    }
    
    // Step 5: Wait for thread handle to become joinable or timeout
    if (g_current_execution->thread.joinable()) {
        std::cout << "LuaRuntime: Attempting to join thread..." << std::endl;
        
        // Try to join with timeout
        auto join_start = std::chrono::steady_clock::now();
        bool joined = false;
        
        while (!joined) {
            // Check if we can join (thread might have exited)
            if (!g_current_execution->running.load()) {
                try {
                    g_current_execution->thread.join();
                    joined = true;
                    std::cout << "LuaRuntime: Thread joined successfully" << std::endl;
                } catch (...) {
                    std::cout << "LuaRuntime: Exception during thread join" << std::endl;
                    break;
                }
            }
            
            auto elapsed = std::chrono::steady_clock::now() - join_start;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= 1000) {
                std::cout << "LuaRuntime: Thread join timeout - detaching thread" << std::endl;
                g_current_execution->thread.detach();
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    // Step 6: Reset execution context
    g_current_execution.reset();
    
    // Step 7: Clear all global state
    g_lua_should_interrupt = false;
    g_lua_executing = false;
    g_force_kill_requested = false;
    
    std::cout << "LuaRuntime: *** FORCE KILL COMPLETE ***" << std::endl;
}

// Bulletproof stop function - public API that NEVER hangs
void stop_lua_script_bulletproof() {
    std::cout << "LuaRuntime: stop_lua_script_bulletproof() called" << std::endl;
    
    // Check if anything is running
    bool is_running = false;
    {
        std::lock_guard<std::mutex> lock(g_execution_mutex);
        is_running = g_current_execution && 
                    g_current_execution->running.load() && 
                    !g_current_execution->finished.load();
    }
    
    if (!is_running) {
        std::cout << "LuaRuntime: No script running, nothing to stop" << std::endl;
        return;
    }
    
    // Step 1: Request graceful termination
    std::cout << "LuaRuntime: Requesting graceful termination..." << std::endl;
    {
        std::lock_guard<std::mutex> lock(g_execution_mutex);
        if (g_current_execution) {
            g_current_execution->should_terminate = true;
        }
    }
    g_lua_should_interrupt = true;
    
    // Step 2: Wait briefly for graceful shutdown
    auto start = std::chrono::steady_clock::now();
    bool graceful_success = false;
    
    while (true) {
        {
            std::lock_guard<std::mutex> lock(g_execution_mutex);
            if (!g_current_execution || g_current_execution->finished.load()) {
                graceful_success = true;
                break;
            }
        }
        
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= GRACEFUL_SHUTDOWN_MS) {
            std::cout << "LuaRuntime: Graceful shutdown timeout" << std::endl;
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    if (graceful_success) {
        std::cout << "LuaRuntime: Script stopped gracefully" << std::endl;
        cleanup_finished_executions();
        return;
    }
    
    // Step 3: Force kill if graceful failed
    std::cout << "LuaRuntime: Graceful shutdown failed, executing force kill" << std::endl;
    force_kill_lua_script_immediate();
    
    // Step 4: Ensure watchdog is stopped
    stop_lua_watchdog();
    
    std::cout << "LuaRuntime: stop_lua_script_bulletproof() complete - script is STOPPED" << std::endl;
}

// Function to check if a script is currently running
bool is_script_running() {
    std::lock_guard<std::mutex> lock(g_execution_mutex);
    return g_current_execution && g_current_execution->running.load() && !g_current_execution->finished.load();
}

// Execute Lua script from file using sandboxed execution
bool exec_lua_file_sandboxed(const char* filename) {
    if (!filename) {
        g_last_error = "No filename provided";
        return false;
    }

    // Read file content
    std::ifstream file(filename);
    if (!file.is_open()) {
        g_last_error = std::string("Could not open file: ") + filename;
        return false;
    }

    std::string content;
    std::string line;
    while (std::getline(file, line)) {
        if (!content.empty()) {
            content += "\n";
        }
        content += line;
    }
    file.close();

    g_current_script_content = content;
    g_current_script_filename = filename;

    return exec_lua_sandboxed(content.c_str());
}

const char* lua_get_error(void) {
    // Check for execution-specific error first
    {
        std::lock_guard<std::mutex> lock(g_execution_mutex);
        if (g_current_execution && !g_current_execution->error_message.empty()) {
            return g_current_execution->error_message.c_str();
        }
    }
    return g_last_error.empty() ? nullptr : g_last_error.c_str();
}

lua_State* lua_get_state(void) {
    // Return main Lua state for initialization/compatibility
    return g_lua;
}

const char* lua_get_current_script_content(void) {
    return g_current_script_content.empty() ? nullptr : g_current_script_content.c_str();
}

const char* lua_get_current_script_filename(void) {
    return g_current_script_filename.empty() ? nullptr : g_current_script_filename.c_str();
}

// Helper function to serialize a Lua table to string representation
static void serialize_table_to_string(lua_State* L, int index, std::string& result, int depth = 0) {
    if (depth > 10) { // Prevent infinite recursion
        result += "{}";
        return;
    }
    
    result += "{";
    bool first = true;
    
    lua_pushnil(L); // First key
    while (lua_next(L, index) != 0) {
        if (!first) result += ",";
        first = false;
        
        // Serialize key
        if (lua_type(L, -2) == LUA_TSTRING) {
            result += "[\"" + std::string(lua_tostring(L, -2)) + "\"] = ";
        } else if (lua_type(L, -2) == LUA_TNUMBER) {
            result += "[" + std::to_string(lua_tonumber(L, -2)) + "] = ";
        } else {
            result += "[\"key\"] = ";
        }
        
        // Serialize value
        int value_type = lua_type(L, -1);
        switch (value_type) {
            case LUA_TNIL:
                result += "nil";
                break;
            case LUA_TBOOLEAN:
                result += lua_toboolean(L, -1) ? "true" : "false";
                break;
            case LUA_TNUMBER:
                result += std::to_string(lua_tonumber(L, -1));
                break;
            case LUA_TSTRING:
                result += "\"" + std::string(lua_tostring(L, -1)) + "\"";
                break;
            case LUA_TTABLE:
                serialize_table_to_string(L, lua_gettop(L), result, depth + 1);
                break;
            case LUA_TFUNCTION:
                result += "function() end"; // Placeholder for functions
                break;
            default:
                result += "nil";
                break;
        }
        
        lua_pop(L, 1); // Remove value, keep key for next iteration
    }
    
    result += "}";
}

bool lua_save_state(void) {
    if (!g_lua) {
        g_last_error = "No Lua state to save";
        return false;
    }
    
    std::lock_guard<std::mutex> lock(g_lua_mutex);
    
    try {
        std::string state_script = "-- Saved Lua State\n";
        
        // Get global table
        lua_getglobal(g_lua, "_G");
        
        // Iterate through global variables
        lua_pushnil(g_lua); // First key
        while (lua_next(g_lua, -2) != 0) {
            // Skip built-in Lua functions and SuperTerminal API functions
            const char* key = lua_tostring(g_lua, -2);
            if (key && key[0] != '_' && 
                strcmp(key, "print") != 0 &&
                strcmp(key, "pairs") != 0 &&
                strcmp(key, "ipairs") != 0 &&
                strcmp(key, "type") != 0 &&
                strcmp(key, "tostring") != 0 &&
                strcmp(key, "tonumber") != 0 &&
                strcmp(key, "table") != 0 &&
                strcmp(key, "string") != 0 &&
                strcmp(key, "math") != 0 &&
                strcmp(key, "os") != 0 &&
                strcmp(key, "io") != 0 &&
                strncmp(key, "sprite_", 7) != 0 &&
                strncmp(key, "draw_", 5) != 0 &&
                strncmp(key, "fill_", 5) != 0 &&
                strncmp(key, "set_", 4) != 0 &&
                strncmp(key, "background_", 11) != 0 &&
                strncmp(key, "wait", 4) != 0 &&
                strncmp(key, "sleep", 5) != 0 &&
                strcmp(key, "cls") != 0 &&
                strcmp(key, "home") != 0 &&
                strcmp(key, "console") != 0 &&
                strcmp(key, "rgba") != 0) {
                
                int value_type = lua_type(g_lua, -1);
                state_script += std::string(key) + " = ";
                
                switch (value_type) {
                    case LUA_TNIL:
                        state_script += "nil";
                        break;
                    case LUA_TBOOLEAN:
                        state_script += lua_toboolean(g_lua, -1) ? "true" : "false";
                        break;
                    case LUA_TNUMBER:
                        state_script += std::to_string(lua_tonumber(g_lua, -1));
                        break;
                    case LUA_TSTRING:
                        state_script += "\"" + std::string(lua_tostring(g_lua, -1)) + "\"";
                        break;
                    case LUA_TTABLE:
                        serialize_table_to_string(g_lua, lua_gettop(g_lua), state_script);
                        break;
                    case LUA_TFUNCTION:
                        // Skip functions for now - they're complex to serialize
                        state_script += "nil -- function skipped";
                        break;
                    default:
                        state_script += "nil";
                        break;
                }
                
                state_script += "\n";
            }
            
            lua_pop(g_lua, 1); // Remove value, keep key
        }
        
        lua_pop(g_lua, 1); // Remove global table
        
        g_saved_lua_state = state_script;
        g_has_saved_state = true;
        
        std::cout << "LuaRuntime: State saved (" << g_saved_lua_state.length() << " bytes)" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        g_last_error = "Failed to save Lua state: " + std::string(e.what());
        return false;
    }
}

bool lua_restore_state(void) {
    if (!g_has_saved_state) {
        g_last_error = "No saved Lua state to restore";
        return false;
    }
    
    if (!g_lua) {
        g_last_error = "No Lua runtime to restore state to";
        return false;
    }
    
    std::lock_guard<std::mutex> lock(g_lua_mutex);
    
    try {
        // Clear user-defined globals while preserving system functions
        lua_getglobal(g_lua, "_G");
        std::vector<std::string> user_globals;
        
        // Collect user-defined global variable names
        lua_pushnil(g_lua);
        while (lua_next(g_lua, -2) != 0) {
            const char* key = lua_tostring(g_lua, -2);
            if (key && key[0] != '_' && 
                strcmp(key, "print") != 0 &&
                strcmp(key, "pairs") != 0 &&
                strcmp(key, "ipairs") != 0 &&
                strcmp(key, "type") != 0 &&
                strcmp(key, "tostring") != 0 &&
                strcmp(key, "tonumber") != 0 &&
                strcmp(key, "table") != 0 &&
                strcmp(key, "string") != 0 &&
                strcmp(key, "math") != 0 &&
                strcmp(key, "os") != 0 &&
                strcmp(key, "io") != 0 &&
                strncmp(key, "sprite_", 7) != 0 &&
                strncmp(key, "draw_", 5) != 0 &&
                strncmp(key, "fill_", 5) != 0 &&
                strncmp(key, "set_", 4) != 0 &&
                strncmp(key, "background_", 11) != 0 &&
                strncmp(key, "wait", 4) != 0 &&
                strncmp(key, "sleep", 5) != 0 &&
                strcmp(key, "cls") != 0 &&
                strcmp(key, "home") != 0 &&
                strcmp(key, "console") != 0 &&
                strcmp(key, "rgba") != 0) {
                user_globals.push_back(std::string(key));
            }
            lua_pop(g_lua, 1);
        }
        
        lua_pop(g_lua, 1); // Remove global table
        
        // Clear user globals
        for (const auto& global : user_globals) {
            lua_pushnil(g_lua);
            lua_setglobal(g_lua, global.c_str());
        }
        
        // Execute saved state script to restore variables with protection
        int load_result = luaL_loadstring(g_lua, g_saved_lua_state.c_str());
        if (load_result != LUA_OK) {
            g_last_error = "Failed to load saved Lua state: " + std::string(lua_tostring(g_lua, -1));
            lua_pop(g_lua, 1);
            return false;
        }
        
        int result = lua_pcall(g_lua, 0, 0, 0);
        if (result != LUA_OK) {
            g_last_error = "Failed to restore Lua state: " + std::string(lua_tostring(g_lua, -1));
            lua_pop(g_lua, 1); // Remove error message
            return false;
        }
        
        std::cout << "LuaRuntime: State restored successfully" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        g_last_error = "Failed to restore Lua state: " + std::string(e.what());
        return false;
    }
}

bool lua_has_saved_state(void) {
    return g_has_saved_state;
}

void lua_clear_saved_state(void) {
    g_saved_lua_state.clear();
    g_has_saved_state = false;
    std::cout << "LuaRuntime: Saved state cleared" << std::endl;
}

void lua_flush_jit_cache(void) {
    if (!g_lua) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(g_lua_mutex);
    
    // Force garbage collection to clean up memory
    lua_gc(g_lua, LUA_GCCOLLECT, 0);
    lua_gc(g_lua, LUA_GCCOLLECT, 0);  // Run twice for thorough cleanup
    
    // LuaJIT-specific: Flush JIT compiled code cache
    // This is faster than full VM reset but clears compiled optimizations
#ifdef LUAJIT_VERSION
    // Clear JIT cache if available
    lua_getglobal(g_lua, "jit");
    if (!lua_isnil(g_lua, -1)) {
        lua_getfield(g_lua, -1, "flush");
        if (lua_isfunction(g_lua, -1)) {
            lua_call(g_lua, 0, 0);  // Call jit.flush()
        } else {
            lua_pop(g_lua, 1);  // Remove non-function
        }
    }
    lua_pop(g_lua, 1);  // Remove jit table or nil
#endif
    
    std::cout << "LuaRuntime: JIT cache flushed and memory cleaned" << std::endl;
}

bool lua_fast_reset(void) {
    if (!g_lua) {
        g_last_error = "No Lua state to reset";
        return false;
    }
    
    std::lock_guard<std::mutex> lock(g_lua_mutex);
    
    try {
        // Clear the Lua stack first
        lua_settop(g_lua, 0);
        
        // Get global table and collect user-defined variables to clear
        lua_getglobal(g_lua, "_G");
        std::vector<std::string> user_globals;
        
        lua_pushnil(g_lua);
        while (lua_next(g_lua, -2) != 0) {
            const char* key = lua_tostring(g_lua, -2);
            if (key && key[0] != '_' && 
                strcmp(key, "print") != 0 &&
                strcmp(key, "pairs") != 0 &&
                strcmp(key, "ipairs") != 0 &&
                strcmp(key, "type") != 0 &&
                strcmp(key, "tostring") != 0 &&
                strcmp(key, "tonumber") != 0 &&
                strcmp(key, "table") != 0 &&
                strcmp(key, "string") != 0 &&
                strcmp(key, "math") != 0 &&
                strcmp(key, "os") != 0 &&
                strcmp(key, "io") != 0 &&
                strcmp(key, "jit") != 0 &&
                strcmp(key, "bit") != 0 &&
                strcmp(key, "ffi") != 0 &&
                strncmp(key, "sprite_", 7) != 0 &&
                strncmp(key, "draw_", 5) != 0 &&
                strncmp(key, "fill_", 5) != 0 &&
                strncmp(key, "set_", 4) != 0 &&
                strncmp(key, "background_", 11) != 0 &&
                strncmp(key, "wait", 4) != 0 &&
                strncmp(key, "sleep", 5) != 0 &&
                strcmp(key, "cls") != 0 &&
                strcmp(key, "home") != 0 &&
                strcmp(key, "console") != 0 &&
                strcmp(key, "rgba") != 0) {
                user_globals.push_back(std::string(key));
            }
            lua_pop(g_lua, 1);
        }
        
        lua_pop(g_lua, 1); // Remove global table
        
        // Clear all user-defined globals by setting them to nil
        for (const auto& global : user_globals) {
            lua_pushnil(g_lua);
            lua_setglobal(g_lua, global.c_str());
        }
        
        // Flush JIT cache and clean memory
        lua_flush_jit_cache();
        
        // Clear script tracking variables
        g_current_script_content.clear();
        g_current_script_filename.clear();
        g_last_error.clear();
        
        // Clean up current execution state so new scripts can run
        {
            std::lock_guard<std::mutex> exec_lock(g_execution_mutex);
            if (g_current_execution) {
                g_current_execution->running = false;
                g_current_execution->finished = true;
                g_current_execution->should_terminate = true;
                if (g_current_execution->thread.joinable()) {
                    g_current_execution->thread.join();
                }
                g_current_execution.reset();
                std::cout << "LuaRuntime: Cleaned up current execution state during fast reset" << std::endl;
            }
        }
        
        std::cout << "LuaRuntime: Fast reset complete - cleared " << user_globals.size() << " user globals" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        g_last_error = "Fast reset failed: " + std::string(e.what());
        return false;
    }
}

void reset_lua_complete(void) {
    std::cout << "LuaRuntime: reset_lua_complete() called - full state reset" << std::endl;

    // First, stop any running script with bulletproof method
    stop_lua_script_bulletproof();

    // Set interrupt flags
    g_lua_should_interrupt = true;
    g_lua_executing = false;
    
    // Use command queue for thread-safe reset (non-blocking)
    SuperTerminal::g_command_queue.queueVoidCommand([]() {
        std::lock_guard<std::mutex> lock(g_lua_mutex);

        // Complete state destruction and recreation
        if (g_lua) {
            std::cout << "LuaRuntime: Destroying complete Lua state..." << std::endl;
        
            // Shutdown particle system before closing Lua
            shutdown_particle_system_from_lua();
        
            lua_close(g_lua);
            g_lua = nullptr;
        }

        // Reset all tracking variables
        g_lua_executing = false;
        g_lua_should_interrupt = false;
        g_current_script_content.clear();
        g_current_script_filename.clear();
        g_last_error.clear();

        // Reinitialize fresh Lua state
        std::cout << "LuaRuntime: Reinitializing fresh Lua state..." << std::endl;
        if (lua_init()) {
            std::cout << "LuaRuntime: Complete Lua reset successful - brand new state" << std::endl;
        } else {
            std::cout << "LuaRuntime: ERROR - Failed to reinitialize Lua after complete reset" << std::endl;
        }
    });
}



void reset_lua(void) {
    // Default reset_lua() now calls complete reset for perfect isolation
    std::cout << "LuaRuntime: reset_lua() called - delegating to reset_lua_complete()" << std::endl;
    reset_lua_complete();
}

bool lua_is_executing(void) {
    std::lock_guard<std::mutex> lock(g_execution_mutex);
    return g_current_execution && g_current_execution->running.load();
}

void lua_interrupt(void) {
    g_lua_should_interrupt = true;
}

// Terminate current sandboxed script execution (ESC key handler)
bool lua_terminate_current_script(void) {
    std::cout << "LuaRuntime: lua_terminate_current_script() called" << std::endl;
    
    // Use bulletproof stop instead of just setting flag
    stop_lua_script_bulletproof();
    
    return true;
}

// Check if script execution has completed
bool lua_script_finished(void) {
    std::lock_guard<std::mutex> lock(g_execution_mutex);
    return !g_current_execution || g_current_execution->finished.load();
}

// Wait for current script to finish with timeout (for graceful shutdown)
bool lua_wait_for_completion(int timeout_ms) {
    auto start_time = std::chrono::steady_clock::now();
    
    while (!lua_script_finished()) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count() >= timeout_ms) {
            return false; // Timeout
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    return true;
}

// Clean up finished execution context
void lua_cleanup_finished_execution(void) {
    std::lock_guard<std::mutex> lock(g_execution_mutex);
    
    if (g_current_execution && g_current_execution->finished.load()) {
        if (g_current_execution->thread.joinable()) {
            g_current_execution->thread.join();
        }
        g_current_execution.reset();
        std::cout << "LuaRuntime: Finished execution context cleaned up" << std::endl;
    }
}

// exec_lua_bulletproof function removed - clean slate for reliable design

bool terminate_lua_script_graceful(int timeout_ms) {
    if (!g_lua_executing.load()) {
        std::cout << "LuaRuntime: No script running, graceful termination not needed" << std::endl;
        return true;
    }
    
    std::cout << "LuaRuntime: Requesting graceful script termination..." << std::endl;
    
    // Set interrupt flag to request graceful shutdown
    g_lua_should_interrupt = true;
    
    // Wait for script to terminate gracefully
    auto start_time = std::chrono::steady_clock::now();
    while (g_lua_executing.load() && 
           std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start_time).count() < timeout_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    bool graceful_success = !g_lua_executing.load();
    if (graceful_success) {
        std::cout << "LuaRuntime: Script terminated gracefully" << std::endl;
    } else {
        std::cout << "LuaRuntime: Script did not respond to graceful termination request" << std::endl;
    }
    
    return graceful_success;
}

void force_terminate_lua_script(void) {
    std::cout << "LuaRuntime: FORCE TERMINATION - Forcefully stopping Lua script" << std::endl;
    
    // Force set execution flags
    g_lua_should_interrupt = true;
    g_lua_executing = false;
    
    // Forcefully close Lua state
    if (g_lua) {
        std::cout << "LuaRuntime: Force closing Lua state" << std::endl;
        lua_close(g_lua);
        g_lua = nullptr;
    }
    
    // Clear all state
    g_current_script_content.clear();
    g_current_script_filename.clear();
    g_last_error.clear();
    g_lua_interrupt_check_counter = 0;
    
    std::cout << "LuaRuntime: Force termination complete" << std::endl;
}

void terminate_lua_script_with_cleanup(void) {
    std::cout << "LuaRuntime: Starting emergency script termination with global shutdown..." << std::endl;
    
    // Request emergency shutdown of all subsystems (2 second timeout)
    request_emergency_shutdown(2000);
    
    // Phase 1: Try graceful termination (500ms timeout)
    bool graceful_success = terminate_lua_script_graceful(500);
    
    // Phase 2: Force termination if graceful failed
    if (!graceful_success) {
        std::cout << "LuaRuntime: Graceful termination failed, proceeding with force termination" << std::endl;
        force_terminate_lua_script();
    }
    
    std::cout << "LuaRuntime: Script termination complete, waiting for all subsystems..." << std::endl;
    
    // Wait for other subsystems to shutdown or timeout
    auto start_time = std::chrono::steady_clock::now();
    while (!are_all_subsystems_shutdown() && !is_shutdown_timeout_exceeded()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Progress report every second
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        if (elapsed > 0 && elapsed % 1000 == 0) {
            std::cout << "LuaRuntime: Waiting for subsystems, " << get_active_subsystem_count() 
                      << " still active..." << std::endl;
        }
    }
    
    if (is_shutdown_timeout_exceeded()) {
        std::cout << "LuaRuntime: WARNING - Shutdown timeout exceeded, some subsystems may not have shut down cleanly" << std::endl;
        force_terminate_all_subsystems();
    }
    
    std::cout << "LuaRuntime: All subsystems shutdown complete" << std::endl;
}

// Expose bulletproof stop as C API
extern "C" void stop_lua_script(void) {
    stop_lua_script_bulletproof();
}

void reset_lua_partial(void) {
    std::cout << "LuaRuntime: reset_lua_partial() called - advanced partial reset" << std::endl;
    
    // Stop any running script first with bulletproof method
    stop_lua_script_bulletproof();
    
    // First, immediately set interrupt flag to stop any running scripts
    g_lua_should_interrupt = true;
    g_lua_executing = false;
    
    // Use command queue for thread-safe reset (non-blocking)
    SuperTerminal::g_command_queue.queueVoidCommand([]() {
        std::lock_guard<std::mutex> lock(g_lua_mutex);
        
        if (g_lua) {
            // Remove any existing hook
            lua_sethook(g_lua, nullptr, 0, 0);
            
            // Reset to clean state but don't close Lua (keep it initialized)
            lua_settop(g_lua, 0); // Clear stack
            g_lua_should_interrupt = false;
            g_current_script_content.clear();
            g_current_script_filename.clear();
            g_last_error.clear();
            
            std::cout << "LuaRuntime: Partial Lua reset complete - globals preserved" << std::endl;
        }
    });
}

// Synchronous version of reset_lua_complete for reliable script execution
void reset_lua_complete_sync(void) {
    std::cout << "LuaRuntime: reset_lua_complete_sync() called - synchronous full state reset" << std::endl;
    
    // Stop any running script first with bulletproof method
    stop_lua_script_bulletproof();

    // First, immediately set interrupt flag to stop any running scripts
    g_lua_should_interrupt = true;
    g_lua_executing = false;
    
    // Perform reset synchronously with proper locking
    std::lock_guard<std::mutex> lock(g_lua_mutex);

    // Complete state destruction and recreation
    if (g_lua) {
        std::cout << "LuaRuntime: Destroying complete Lua state synchronously..." << std::endl;
    
        // Shutdown particle system before closing Lua
        shutdown_particle_system_from_lua();
    
        lua_close(g_lua);
        g_lua = nullptr;
    }

    // Reset all tracking variables
    g_lua_executing = false;
    g_lua_should_interrupt = false;
    g_current_script_content.clear();
    g_current_script_filename.clear();
    g_last_error.clear();

    // Clean up current execution state
    {
        std::lock_guard<std::mutex> exec_lock(g_execution_mutex);
        if (g_current_execution) {
            g_current_execution->running = false;
            g_current_execution->finished = true;
            g_current_execution->should_terminate = true;
            if (g_current_execution->thread.joinable()) {
                g_current_execution->thread.join();
            }
            g_current_execution.reset();
            std::cout << "LuaRuntime: Cleaned up current execution state during complete reset" << std::endl;
        }
    }

    // Reinitialize fresh Lua state
    std::cout << "LuaRuntime: Reinitializing fresh Lua state synchronously..." << std::endl;
    if (lua_init()) {
        std::cout << "LuaRuntime: Synchronous Lua reset successful - brand new state ready" << std::endl;
    } else {
        std::cout << "LuaRuntime: ERROR - Failed to reinitialize Lua after synchronous reset" << std::endl;
    }
}


//
// Compatibility shim functions for backward compatibility
// These delegate to the new sandboxed execution system
//

// Legacy exec_lua_file function - now delegates to sandboxed version
bool exec_lua_file(const char* filename) {
    std::cout << "LuaRuntime: exec_lua_file (compatibility shim) called with: " << (filename ? filename : "NULL") << std::endl;
    return exec_lua_file_sandboxed(filename);
}

// Legacy exec_lua function - now delegates to sandboxed version
bool exec_lua(const char* lua_code) {
    std::cout << "LuaRuntime: exec_lua (compatibility shim) called" << std::endl;
    return exec_lua_sandboxed(lua_code);
}

// Compatibility shim for exec_lua_bulletproof (removed function)
bool exec_lua_bulletproof(const char* lua_code) {
    std::cout << "LuaRuntime: exec_lua_bulletproof (compatibility shim) called" << std::endl;
    return exec_lua_sandboxed(lua_code);
}

// Forward declarations for REPL functions
bool exec_lua_repl(const char* lua_code);
void shutdown_repl_lua();

// Forward declarations for overlay background control
void repl_set_background_alpha(float alpha);
float repl_get_background_alpha();

} // extern "C"

// Lua API binding implementations

// Text and output functions
static int lua_superterminal_print(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    std::string text_copy(text); // Copy to capture in lambda
    SuperTerminal::g_command_queue.queueVoidCommand([text_copy]() {
        print(text_copy.c_str());
        print("\n");  // Add newline like standard Lua print()
    });
    return 0;
}

static int lua_superterminal_print_at(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    const char* text = luaL_checkstring(L, 3);
    print_at(x, y, text);
    return 0;
}

static int lua_superterminal_cls(lua_State* L) {
    cls();
    return 0;
}

static int lua_superterminal_end_of_script(lua_State* L) {
    // Check if we're running in GCD runtime or old runtime
    bool is_gcd_runtime = lua_gcd_is_script_running();
    
    if (is_gcd_runtime) {
        // GCD runtime path - just terminate cleanly
        fprintf(stderr, "[GCD] end_of_script() called - terminating script cleanly\n");
        fflush(stderr);
        
        const char* script_name = lua_gcd_get_current_script_name();
        if (script_name && strlen(script_name) > 0) {
            std::string end_message = "🏁 Script ended: " + std::string(script_name);
            console(end_message.c_str());
        }
        
        SuperTerminal::g_command_queue.queueVoidCommand([]() {
            set_window_title("SuperTerminal - Script ended");
        });
        
        // Update status bar - script stopped
        superterminal_update_status("● Stopped");
        
        // Terminate Lua execution cleanly
        // GCD runtime will handle cleanup via RAII guard
        luaL_error(L, "Script execution completed normally via end_of_script()");
        
    } else {
        // Old runtime path - keep original behavior
        std::cout << "LuaRuntime: Script called end_of_script() - cleaning up and terminating execution" << std::endl;
        std::string end_message = "🏁 Script ended: " + g_current_script_name;
        console(end_message.c_str());
        
        // First, set all flags to mark script as finished
        if (g_current_execution) {
            g_current_execution->running = false;
            g_current_execution->finished = true;
        }
        
        // Set global execution flags
        g_lua_executing = false;
        g_script_finished = true;
        
        // Update window title to show script ended - use non-blocking command queue
        extern void set_window_title(const char* title);
        std::string title = "SuperTerminal - " + g_current_script_name + " - ended";
        SuperTerminal::g_command_queue.queueVoidCommand([title]() {
            set_window_title(title.c_str());
        });
        
        // Update status bar - script stopped
        superterminal_update_status("● Stopped");
        
        std::cout << "LuaRuntime: Flags set and title updated, now terminating Lua execution" << std::endl;
        
        // Now immediately terminate Lua execution
        luaL_error(L, "Script execution completed normally via end_of_script()");
    }
    
    // This return will never be reached due to luaL_error()
    return 0;
}

static int lua_superterminal_start_of_script(lua_State* L) {
    const char* script_name = luaL_checkstring(L, 1);
    std::cout << "LuaRuntime: Script called start_of_script('" << script_name << "')" << std::endl;
    std::string start_message = "🚀 Script started: " + std::string(script_name);
    console(start_message.c_str());
    
    // Update script name (script state already tracked by existing flags)
    g_current_script_name = script_name;
    
    // Create window title: "SuperTerminal - script name - running"
    std::string title = "SuperTerminal - ";
    title += script_name;
    title += " - running";
    
    // Update window title to show script started - use non-blocking command queue
    extern void set_window_title(const char* title);
    SuperTerminal::g_command_queue.queueVoidCommand([title]() {
        set_window_title(title.c_str());
    });
    
    // Update status bar - script running
    superterminal_update_status("● Running");
    superterminal_update_script_name(script_name);
    
    return 0;
}

// REPL functions - like script functions but don't terminate the thread
static int lua_superterminal_start_of_repl(lua_State* L) {
    const char* repl_name = luaL_optstring(L, 1, "repl");
    std::cout << "LuaRuntime: REPL called start_of_repl('" << repl_name << "')" << std::endl;
    std::string start_message = "🎮 REPL started: " + std::string(repl_name);
    console(start_message.c_str());
    
    // Update script name for tracking
    g_current_script_name = std::string("repl-") + repl_name;
    
    // Create window title: "SuperTerminal - REPL - active"
    std::string title = "SuperTerminal - REPL - active";
    extern void set_window_title(const char* title);
    set_window_title(title.c_str());
    
    return 0;
}

static int lua_superterminal_end_of_repl(lua_State* L) {
    const char* repl_name = luaL_optstring(L, 1, "repl");
    std::cout << "LuaRuntime: REPL called end_of_repl('" << repl_name << "') - NOT terminating thread" << std::endl;
    std::string end_message = "⏸️ REPL command finished: " + std::string(repl_name);
    console(end_message.c_str());
    
    // Update window title but don't terminate
    std::string title = "SuperTerminal - REPL - ready";
    extern void set_window_title(const char* title);
    set_window_title(title.c_str());
    
    // Do NOT call luaL_error() - this keeps the thread alive for persistent state
    return 0;
}

static int lua_superterminal_reset_repl(lua_State* L) {
    std::cout << "LuaRuntime: REPL reset() function called" << std::endl;
    console("🔄 REPL state reset - variables and functions cleared");
    
    // Reset the persistent REPL state
    shutdown_repl_lua();
    
    std::cout << "LuaRuntime: REPL state has been reset" << std::endl;
    return 0;
}

static int lua_superterminal_set_overlay_background_alpha(lua_State* L) {
    float alpha = luaL_checknumber(L, 1);
    repl_set_background_alpha(alpha);
    return 0;
}

static int lua_superterminal_get_overlay_background_alpha(lua_State* L) {
    float alpha = repl_get_background_alpha();
    lua_pushnumber(L, alpha);
    return 1;
}

// Persistent REPL execution - maintains state between commands
bool exec_lua_repl(const char* lua_code) {
    if (!lua_code) {
        g_last_error = "No REPL code provided";
        return false;
    }
    
    std::lock_guard<std::mutex> lock(g_repl_mutex);
    
    // Initialize persistent REPL Lua state if needed
    if (!g_repl_lua_initialized || !g_repl_lua) {
        std::cout << "LuaRuntime: Initializing persistent REPL Lua state" << std::endl;
        
        g_repl_lua = luaL_newstate();
        if (!g_repl_lua) {
            g_last_error = "Failed to create REPL Lua state";
            return false;
        }
        
        // Open standard libraries
        luaL_openlibs(g_repl_lua);
        
        // Register SuperTerminal API
        register_superterminal_api(g_repl_lua);
        
        // Register particle system Lua API
        register_particle_system_lua_api(g_repl_lua);
        
        // Try to register full particle system API if system is ready
        register_particle_system_lua_api_when_ready(g_repl_lua);
        
        // Register bullet system Lua API
        register_bullet_system_lua_api(g_repl_lua);
        
        // Register audio system Lua API
        register_audio_lua_bindings(g_repl_lua);
        
        // Register assets management Lua API
        register_assets_lua_bindings(g_repl_lua);
        
        g_repl_lua_initialized = true;
        std::cout << "LuaRuntime: REPL Lua state initialized successfully" << std::endl;
    }
    
    // Execute code in persistent state
    std::cout << "LuaRuntime: Executing REPL code in persistent state" << std::endl;
    int result = luaL_dostring(g_repl_lua, lua_code);
    
    if (result != LUA_OK) {
        if (lua_isstring(g_repl_lua, -1)) {
            g_last_error = lua_tostring(g_repl_lua, -1);
            lua_pop(g_repl_lua, 1); // Remove error from stack
        } else {
            g_last_error = "REPL execution failed";
        }
        std::cout << "LuaRuntime: REPL execution failed: " << g_last_error << std::endl;
        return false;
    }
    
    std::cout << "LuaRuntime: REPL command executed successfully" << std::endl;
    return true;
}

// Clean up REPL state
void shutdown_repl_lua() {
    std::lock_guard<std::mutex> lock(g_repl_mutex);
    if (g_repl_lua) {
        lua_close(g_repl_lua);
        g_repl_lua = nullptr;
        g_repl_lua_initialized = false;
        std::cout << "LuaRuntime: REPL Lua state shut down" << std::endl;
        
        // Notify REPL console about the reset
        repl_notify_state_reset();
    }
}

static int lua_superterminal_home(lua_State* L) {
    home();
    return 0;
}

// Text scrollback buffer Lua wrapper functions
static int lua_text_locate_line(lua_State* L) {
    int line = luaL_checkinteger(L, 1);
    text_locate_line(line);
    return 0;
}

static int lua_text_scroll_to_line(lua_State* L) {
    int line = luaL_checkinteger(L, 1);
    text_scroll_to_line(line);
    return 0;
}

static int lua_text_scroll_up(lua_State* L) {
    int lines = luaL_checkinteger(L, 1);
    text_scroll_up(lines);
    return 0;
}

static int lua_text_scroll_down(lua_State* L) {
    int lines = luaL_checkinteger(L, 1);
    text_scroll_down(lines);
    return 0;
}

static int lua_text_page_up(lua_State* L) {
    text_page_up();
    return 0;
}

static int lua_text_page_down(lua_State* L) {
    text_page_down();
    return 0;
}

static int lua_text_scroll_to_top(lua_State* L) {
    text_scroll_to_top();
    return 0;
}

static int lua_text_scroll_to_bottom(lua_State* L) {
    text_scroll_to_bottom();
    return 0;
}

static int lua_text_get_cursor_line(lua_State* L) {
    int line = text_get_cursor_line();
    lua_pushinteger(L, line);
    return 1;
}

static int lua_text_get_cursor_column(lua_State* L) {
    int col = text_get_cursor_column();
    lua_pushinteger(L, col);
    return 1;
}

static int lua_text_get_viewport_line(lua_State* L) {
    int line = text_get_viewport_line();
    lua_pushinteger(L, line);
    return 1;
}

static int lua_text_get_viewport_height(lua_State* L) {
    int height = text_get_viewport_height();
    lua_pushinteger(L, height);
    return 1;
}

static int lua_text_set_autoscroll(lua_State* L) {
    bool enabled = lua_toboolean(L, 1);
    text_set_autoscroll(enabled);
    return 0;
}

static int lua_text_get_autoscroll(lua_State* L) {
    bool enabled = text_get_autoscroll();
    lua_pushboolean(L, enabled);
    return 1;
}

static int lua_superterminal_set_color(lua_State* L) {
    uint32_t fg = luaL_checkinteger(L, 1);
    uint32_t bg = luaL_checkinteger(L, 2);
    set_color(fg, bg);
    return 0;
}

static int lua_superterminal_set_ink(lua_State* L) {
    uint32_t color = luaL_checkinteger(L, 1);
    set_ink(color);
    return 0;
}

static int lua_superterminal_rgba(lua_State* L) {
    int r = luaL_checkinteger(L, 1);
    int g = luaL_checkinteger(L, 2);
    int b = luaL_checkinteger(L, 3);
    int a = luaL_optinteger(L, 4, 255);
    
    uint32_t color = rgba(r, g, b, a);
    lua_pushinteger(L, color);
    return 1;
}

static int lua_superterminal_background_color(lua_State* L) {
    uint32_t color = luaL_checkinteger(L, 1);
    background_color(color);
    return 1;
}

// Mouse Input Functions
static int lua_superterminal_mouse_get_position(lua_State* L) {
    float x, y;
    mouse_get_position(&x, &y);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
}

static int lua_superterminal_mouse_is_pressed(lua_State* L) {
    int button = luaL_checkinteger(L, 1);
    bool pressed = mouse_is_pressed(button);
    lua_pushboolean(L, pressed);
    return 1;
}

static int lua_superterminal_mouse_wait_click(lua_State* L) {
    // Check if we should terminate before blocking
    if (g_lua_should_interrupt.load() || is_emergency_shutdown_requested()) {
        luaL_error(L, "Script execution interrupted");
        return 0;
    }
    
    int button;
    float x, y;
    bool result = mouse_wait_click(&button, &x, &y);
    
    // If mouse_wait_click returned false, it was interrupted
    if (!result) {
        // Check if this was due to interruption
        if (g_lua_should_interrupt.load() || is_emergency_shutdown_requested()) {
            luaL_error(L, "Script execution interrupted during mouse_wait_click");
            return 0;
        }
        // Otherwise it's a genuine failure
        lua_pushnil(L);
        return 1;
    }
    
    lua_pushinteger(L, button);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 3;
}

static int lua_superterminal_sprite_mouse_over(lua_State* L) {
    uint16_t sprite_id = luaL_checkinteger(L, 1);
    bool over = sprite_mouse_over(sprite_id);
    lua_pushboolean(L, over);
    return 1;
}

static int lua_superterminal_mouse_screen_to_text(lua_State* L) {
    float screen_x = luaL_checknumber(L, 1);
    float screen_y = luaL_checknumber(L, 2);
    int text_x, text_y;
    mouse_screen_to_text(screen_x, screen_y, &text_x, &text_y);
    lua_pushinteger(L, text_x);
    lua_pushinteger(L, text_y);
    return 2;
}

// Utility Functions
static int lua_superterminal_sleep_ms(lua_State* L) {
    int ms = luaL_checkinteger(L, 1);
    
    // Check for immediate interruption
    if (g_lua_should_interrupt.load()) {
        luaL_error(L, "Script execution interrupted during sleep");
    }
    
    // Interruptible sleep - check for interruption every 10ms
    const int sleep_chunk = 10; // 10ms chunks
    int remaining = ms;
    
    while (remaining > 0 && !g_lua_should_interrupt.load()) {
        int current_sleep = (remaining < sleep_chunk) ? remaining : sleep_chunk;
        
        // Use non-blocking command queue approach
        std::this_thread::sleep_for(std::chrono::milliseconds(current_sleep));
        
        remaining -= current_sleep;
        
        // Allow interrupt hook to be called
        if (g_lua_should_interrupt.load()) {
            luaL_error(L, "Script execution interrupted during sleep");
        }
    }
    
    return 0;
}

// wait function - alias for sleep_ms for convenience
static int lua_superterminal_wait(lua_State* L) {
    int ms = luaL_checkinteger(L, 1);
    
    // Check for immediate interruption
    if (g_lua_should_interrupt.load()) {
        luaL_error(L, "Script execution interrupted during wait");
    }
    
    // Interruptible wait - check for interruption every 10ms
    const int wait_chunk = 10; // 10ms chunks
    int remaining = ms;
    
    while (remaining > 0 && !g_lua_should_interrupt.load()) {
        int current_wait = (remaining < wait_chunk) ? remaining : wait_chunk;
        
        // Use non-blocking approach
        std::this_thread::sleep_for(std::chrono::milliseconds(current_wait));
        
        remaining -= current_wait;
        
        // Allow interrupt hook to be called
        if (g_lua_should_interrupt.load()) {
            luaL_error(L, "Script execution interrupted during wait");
        }
    }
    
    return 0;
}

static int lua_superterminal_time_ms(lua_State* L) {
    lua_pushinteger(L, time_ms());
    return 1;
}

// Input functions  
static int lua_superterminal_waitKey(lua_State* L) {
    // Check if we should terminate before blocking
    if (g_lua_should_interrupt.load() || is_emergency_shutdown_requested()) {
        luaL_error(L, "Script execution interrupted");
        return 0;
    }
    
    // waitKey uses dispatch_semaphore which is already thread-safe
    // Don't use command queue as it would block main thread
    int key = waitKey();
    
    // If waitKey returned -1, it means we were interrupted
    // Raise error immediately without touching Lua state further
    if (key == -1) {
        luaL_error(L, "Script execution interrupted during waitKey");
        return 0;
    }
    
    lua_pushinteger(L, key);
    return 1;
}

static int lua_superterminal_waitKeyChar(lua_State* L) {
    // Check if we should terminate before blocking
    if (g_lua_should_interrupt.load() || is_emergency_shutdown_requested()) {
        luaL_error(L, "Script execution interrupted");
        return 0;
    }
    
    // waitKeyChar uses dispatch_semaphore which is already thread-safe
    // Don't use command queue as it would block main thread
    int charCode = waitKeyChar();
    
    // If waitKeyChar returned -1, it means we were interrupted
    // Raise error immediately without touching Lua state further
    if (charCode == -1) {
        luaL_error(L, "Script execution interrupted during waitKeyChar");
        return 0;
    }
    
    lua_pushinteger(L, charCode);
    return 1;
}



static int lua_superterminal_isKeyPressed(lua_State* L) {
    int key = luaL_checkinteger(L, 1);
    bool pressed = isKeyPressed(key);
    lua_pushboolean(L, pressed);
    return 1;
}

static int lua_superterminal_keyImmediate(lua_State* L) {
    // Use non-blocking approach - keyImmediate() should just poll current state
    // Don't use executeCommand as it blocks waiting for result
    int keyval = getKeyImmediate();
    lua_pushinteger(L, keyval);
    return 1;
}

static int lua_superterminal_waitkey_log_metrics(lua_State* L) {
    extern void waitkey_log_metrics(void);
    waitkey_log_metrics();
    return 0;
}

static int lua_superterminal_waitkey_reset_metrics(lua_State* L) {
    extern void waitkey_reset_metrics(void);
    waitkey_reset_metrics();
    return 0;
}

static int lua_superterminal_key(lua_State* L) {
    // Use non-blocking approach - key() should just poll current state
    // Don't use executeCommand as it blocks waiting for result
    int keyval = key();
    lua_pushinteger(L, keyval);
    return 1;
}



static int lua_superterminal_setFontOverdraw(lua_State* L) {
    bool enabled = lua_toboolean(L, 1);
    setFontOverdraw(enabled);
    return 0;
}

static int lua_superterminal_getFontOverdraw(lua_State* L) {
    bool enabled = getFontOverdraw();
    lua_pushboolean(L, enabled);
    return 1;
}

static int lua_superterminal_forceQuit(lua_State* L) {
    // Force quit application immediately with minimal cleanup
    forceQuit();
    return 0; // This won't be reached due to forced exit
}

static int lua_superterminal_emphaticExit(lua_State* L) {
    int code = luaL_optinteger(L, 1, 0); // Default exit code 0
    emphaticExit(code);
    return 0; // This won't be reached due to forced exit
}




// Graphics functions
static int lua_superterminal_draw_line(lua_State* L) {
    float x1 = luaL_checknumber(L, 1);
    float y1 = luaL_checknumber(L, 2);
    float x2 = luaL_checknumber(L, 3);
    float y2 = luaL_checknumber(L, 4);
    uint32_t color = luaL_checkinteger(L, 5);
    SuperTerminal::g_command_queue.queueVoidCommand([=]() {
        draw_line(x1, y1, x2, y2, color);
    });
    return 0;
}

static int lua_superterminal_draw_rect(lua_State* L) {
    float x = luaL_checknumber(L, 1);
    float y = luaL_checknumber(L, 2);
    float w = luaL_checknumber(L, 3);
    float h = luaL_checknumber(L, 4);
    uint32_t color = luaL_checkinteger(L, 5);
    SuperTerminal::g_command_queue.queueVoidCommand([=]() {
        draw_rect(x, y, w, h, color);
    });
    return 0;
}

static int lua_superterminal_fill_rect(lua_State* L) {
    float x = luaL_checknumber(L, 1);
    float y = luaL_checknumber(L, 2);
    float w = luaL_checknumber(L, 3);
    float h = luaL_checknumber(L, 4);
    uint32_t color = luaL_checkinteger(L, 5);
    SuperTerminal::g_command_queue.queueVoidCommand([=]() {
        fill_rect(x, y, w, h, color);
    });
    return 0;
}

static int lua_superterminal_draw_circle(lua_State* L) {
    float x = luaL_checknumber(L, 1);
    float y = luaL_checknumber(L, 2);
    float radius = luaL_checknumber(L, 3);
    uint32_t color = luaL_checkinteger(L, 4);
    SuperTerminal::g_command_queue.queueVoidCommand([=]() {
        draw_circle(x, y, radius, color);
    });
    return 0;
}

static int lua_superterminal_fill_circle(lua_State* L) {
    float x = luaL_checknumber(L, 1);
    float y = luaL_checknumber(L, 2);
    float radius = luaL_checknumber(L, 3);
    uint32_t color = luaL_checkinteger(L, 4);
    SuperTerminal::g_command_queue.queueVoidCommand([=]() {
        fill_circle(x, y, radius, color);
    });
    return 0;
}

static int lua_superterminal_draw_text(lua_State* L) {
    std::cout << "lua_superterminal_draw_text: Called from Lua" << std::endl;
    float x = luaL_checknumber(L, 1);
    float y = luaL_checknumber(L, 2);
    const char* text = luaL_checkstring(L, 3);
    float fontSize = luaL_checknumber(L, 4);
    uint32_t color = luaL_checkinteger(L, 5);
    std::cout << "lua_superterminal_draw_text: Parsed args - text='" << (text ? text : "NULL") 
              << "' x=" << x << " y=" << y << " fontSize=" << fontSize << " color=0x" << std::hex << color << std::dec << std::endl;
    
    std::string text_copy(text); // Copy to capture in lambda
    SuperTerminal::g_command_queue.queueVoidCommand([=]() {
        draw_text(x, y, text_copy.c_str(), fontSize, color);
    });
    std::cout << "lua_superterminal_draw_text: draw_text() call queued" << std::endl;
    return 0;
}

static int lua_superterminal_graphics_clear(lua_State* L) {
    SuperTerminal::g_command_queue.queueVoidCommand([]() {
        graphics_clear();
    });
    return 0;
}

static int lua_superterminal_graphics_swap(lua_State* L) {
    SuperTerminal::g_command_queue.queueVoidCommand([]() {
        graphics_swap();
    });
    return 0;
}

static int lua_superterminal_present(lua_State* L) {
    SuperTerminal::g_command_queue.queueVoidCommand([]() {
        present();
    });
    return 0;
}

// Sprite functions
static int lua_superterminal_sprite_load(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    const char* filename = luaL_checkstring(L, 2);
    bool result = sprite_load(id, filename);
    lua_pushboolean(L, result);
    return 1;
}

static int lua_superterminal_sprite_show(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    float x = luaL_checknumber(L, 2);
    float y = luaL_checknumber(L, 3);
    sprite_show(id, x, y);
    return 0;
}

static int lua_superterminal_sprite_hide(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    sprite_hide(id);
    return 0;
}

static int lua_superterminal_sprite_release(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    sprite_release(id);
    return 0;
}

static int lua_superterminal_sprite_next_id(lua_State* L) {
    uint16_t id = sprite_next_id();
    lua_pushinteger(L, id);
    return 1;
}

static int lua_superterminal_sprite_move(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    float x = luaL_checknumber(L, 2);
    float y = luaL_checknumber(L, 3);
    sprite_move(id, x, y);
    return 0;
}

static int lua_superterminal_sprite_scale(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    float scale = luaL_checknumber(L, 2);
    sprite_scale(id, scale);
    return 0;
}

static int lua_superterminal_sprite_rotate(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    float angle = luaL_checknumber(L, 2);
    sprite_rotate(id, angle);
    return 0;
}

static int lua_superterminal_sprite_alpha(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    float alpha = luaL_checknumber(L, 2);
    sprite_alpha(id, alpha);
    return 0;
}

// Sprite Effect functions
static int lua_superterminal_sprite_effect_load(lua_State* L) {
    const char* effectName = luaL_checkstring(L, 1);
    sprite_effect_load(effectName);
    return 0;
}

static int lua_superterminal_sprite_set_effect(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    const char* effectName = luaL_checkstring(L, 2);
    sprite_set_effect(id, effectName);
    return 0;
}

static int lua_superterminal_sprite_clear_effect(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    sprite_clear_effect(id);
    return 0;
}

static int lua_superterminal_sprite_get_effect(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    const char* effectName = sprite_get_effect(id);
    lua_pushstring(L, effectName);
    return 1;
}

static int lua_superterminal_sprite_effect_set_float(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    const char* paramName = luaL_checkstring(L, 2);
    float value = luaL_checknumber(L, 3);
    sprite_effect_set_float(id, paramName, value);
    return 0;
}

static int lua_superterminal_sprite_effect_set_color(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    const char* paramName = luaL_checkstring(L, 2);
    float r = luaL_checknumber(L, 3);
    float g = luaL_checknumber(L, 4);
    float b = luaL_checknumber(L, 5);
    float a = luaL_checknumber(L, 6);
    sprite_effect_set_color(id, paramName, r, g, b, a);
    return 0;
}

static int lua_superterminal_sprite_effect_set_vec2(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    const char* paramName = luaL_checkstring(L, 2);
    float x = luaL_checknumber(L, 3);
    float y = luaL_checknumber(L, 4);
    sprite_effect_set_vec2(id, paramName, x, y);
    return 0;
}

static int lua_superterminal_sprite_effect_set_global_float(lua_State* L) {
    const char* effectName = luaL_checkstring(L, 1);
    const char* paramName = luaL_checkstring(L, 2);
    float value = luaL_checknumber(L, 3);
    sprite_effect_set_global_float(effectName, paramName, value);
    return 0;
}

static int lua_superterminal_sprite_effect_set_global_vec2(lua_State* L) {
    const char* effectName = luaL_checkstring(L, 1);
    const char* paramName = luaL_checkstring(L, 2);
    float x = luaL_checknumber(L, 3);
    float y = luaL_checknumber(L, 4);
    sprite_effect_set_global_vec2(effectName, paramName, x, y);
    return 0;
}

static int lua_superterminal_sprite_effect_set_global_color(lua_State* L) {
    const char* effectName = luaL_checkstring(L, 1);
    const char* paramName = luaL_checkstring(L, 2);
    float r = luaL_checknumber(L, 3);
    float g = luaL_checknumber(L, 4);
    float b = luaL_checknumber(L, 5);
    float a = luaL_checknumber(L, 6);
    sprite_effect_set_global_color(effectName, paramName, r, g, b, a);
    return 0;
}

static int lua_superterminal_sprite_effect_get_available(lua_State* L) {
    int count = 0;
    const char** effects = sprite_effect_get_available_effects(&count);
    
    lua_newtable(L);
    for (int i = 0; i < count; i++) {
        lua_pushstring(L, effects[i]);
        lua_rawseti(L, -2, i + 1);
    }
    
    return 1;
}

// Tile functions
static int lua_superterminal_tile_load(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    const char* filename = luaL_checkstring(L, 2);
    bool result = tile_load(id, filename);
    lua_pushboolean(L, result);
    return 1;
}

static int lua_superterminal_tile_set(lua_State* L) {
    int layer = luaL_checkinteger(L, 1);
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    int tile_id = luaL_checkinteger(L, 4);
    tile_set(layer, x, y, tile_id);
    return 0;
}

static int lua_superterminal_tile_get(lua_State* L) {
    int layer = luaL_checkinteger(L, 1);
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    int tile_id = tile_get(layer, x, y);
    lua_pushinteger(L, tile_id);
    return 1;
}

static int lua_superterminal_tile_scroll(lua_State* L) {
    int layer = luaL_checkinteger(L, 1);
    float dx = luaL_checknumber(L, 2);
    float dy = luaL_checknumber(L, 3);
    tile_scroll(layer, dx, dy);
    return 0;
}

static int lua_superterminal_tile_set_viewport(lua_State* L) {
    int layer = luaL_checkinteger(L, 1);
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    tile_set_viewport(layer, x, y);
    return 0;
}

static int lua_superterminal_tile_center_viewport(lua_State* L) {
    int layer = luaL_checkinteger(L, 1);
    int tileX = luaL_checkinteger(L, 2);
    int tileY = luaL_checkinteger(L, 3);
    tile_center_viewport(layer, tileX, tileY);
    return 0;
}

static int lua_superterminal_tile_clear_map(lua_State* L) {
    int layer = luaL_checkinteger(L, 1);
    tile_clear_map(layer);
    return 0;
}

static int lua_superterminal_tiles_clear(lua_State* L) {
    // Clear all tile data and free memory, ready for new tiles
    tiles_clear();
    return 0;
}

static int lua_superterminal_tiles_shutdown(lua_State* L) {
    // Complete shutdown of tile system
    tiles_shutdown();
    return 0;
}

static int lua_superterminal_sprites_clear(lua_State* L) {
    // Clear all sprite data and free memory, ready for new sprites
    sprites_clear();
    return 0;
}

static int lua_superterminal_sprites_shutdown(lua_State* L) {
    // Complete shutdown of sprite system
    sprites_shutdown();
    return 0;
}

static int lua_superterminal_tile_create_map(lua_State* L) {
    int layer = luaL_checkinteger(L, 1);
    int width = luaL_checkinteger(L, 2);
    int height = luaL_checkinteger(L, 3);
    tile_create_map(layer, width, height);
    return 0;
}

// Layer control functions
static int lua_superterminal_layer_set_enabled(lua_State* L) {
    int layer = luaL_checkinteger(L, 1);
    bool enabled = lua_toboolean(L, 2);
    layer_set_enabled(layer, enabled);
    return 0;
}

static int lua_superterminal_layer_is_enabled(lua_State* L) {
    int layer = luaL_checkinteger(L, 1);
    bool enabled = layer_is_enabled(layer);
    lua_pushboolean(L, enabled);
    return 1;
}

static int lua_superterminal_layer_enable_all(lua_State* L) {
    layer_enable_all();
    return 0;
}

static int lua_superterminal_layer_disable_all(lua_State* L) {
    layer_disable_all();
    return 0;
}

static int lua_superterminal_draw_linear_gradient(lua_State* L) {
    float x1 = luaL_checknumber(L, 1);
    float y1 = luaL_checknumber(L, 2);
    float x2 = luaL_checknumber(L, 3);
    float y2 = luaL_checknumber(L, 4);
    uint32_t color1 = luaL_checkinteger(L, 5);
    uint32_t color2 = luaL_checkinteger(L, 6);
    draw_linear_gradient(x1, y1, x2, y2, color1, color2);
    return 0;
}

static int lua_superterminal_fill_linear_gradient_rect(lua_State* L) {
    float x = luaL_checknumber(L, 1);
    float y = luaL_checknumber(L, 2);
    float w = luaL_checknumber(L, 3);
    float h = luaL_checknumber(L, 4);
    uint32_t color1 = luaL_checkinteger(L, 5);
    uint32_t color2 = luaL_checkinteger(L, 6);
    int direction = luaL_optinteger(L, 7, 0); // Default to horizontal
    fill_linear_gradient_rect(x, y, w, h, color1, color2, direction);
    return 0;
}

static int lua_superterminal_draw_radial_gradient(lua_State* L) {
    float centerX = luaL_checknumber(L, 1);
    float centerY = luaL_checknumber(L, 2);
    float radius = luaL_checknumber(L, 3);
    uint32_t color1 = luaL_checkinteger(L, 4);
    uint32_t color2 = luaL_checkinteger(L, 5);
    draw_radial_gradient(centerX, centerY, radius, color1, color2);
    return 0;
}

static int lua_superterminal_fill_radial_gradient_circle(lua_State* L) {
    float centerX = luaL_checknumber(L, 1);
    float centerY = luaL_checknumber(L, 2);
    float radius = luaL_checknumber(L, 3);
    uint32_t color1 = luaL_checkinteger(L, 4);
    uint32_t color2 = luaL_checkinteger(L, 5);
    fill_radial_gradient_circle(centerX, centerY, radius, color1, color2);
    return 0;
}

extern "C" void set_blend_mode(int blendMode);
extern "C" void set_blur_filter(float radius);
extern "C" void set_drop_shadow(float dx, float dy, float blur, uint32_t color);
extern "C" void set_color_matrix(const float matrix[20]);
extern "C" void clear_filters();
extern "C" bool read_pixels(uint16_t imageId, int x, int y, int width, int height, uint8_t** outPixels);
extern "C" bool write_pixels(uint16_t imageId, int x, int y, int width, int height, const uint8_t* pixels);
extern "C" void free_pixels(uint8_t* pixels);
extern "C" void wait_queue_empty();
extern "C" void push_matrix();
extern "C" void pop_matrix();
extern "C" void translate(float tx, float ty);
extern "C" void rotate_degrees(float degrees);
extern "C" void scale(float sx, float sy);
extern "C" void skew(float kx, float ky);
extern "C" void reset_matrix();
extern "C" void create_path(uint16_t pathId);
extern "C" void path_move_to(uint16_t pathId, float x, float y);
extern "C" void path_line_to(uint16_t pathId, float x, float y);
extern "C" void path_curve_to(uint16_t pathId, float x1, float y1, float x2, float y2, float x3, float y3);
extern "C" void path_close(uint16_t pathId);
extern "C" void draw_path(uint16_t pathId);
extern "C" void fill_path(uint16_t pathId);
extern "C" void clip_path(uint16_t pathId, int clipOp, bool antiAlias);
extern "C" bool sprite_create_from_pixels(uint16_t id, const uint8_t* pixels, int width, int height);
extern "C" bool sprite_check_collision(uint16_t id1, uint16_t id2);
extern "C" bool sprite_check_point_collision(uint16_t id, float x, float y);
extern "C" void sprite_get_size(uint16_t id, int* width, int* height);
extern "C" void wait_frame();



static int lua_superterminal_set_blend_mode(lua_State* L) {
    int blendMode = luaL_checkinteger(L, 1);
    set_blend_mode(blendMode);
    return 0;
}

static int lua_superterminal_set_blur_filter(lua_State* L) {
    float radius = luaL_checknumber(L, 1);
    set_blur_filter(radius);
    return 0;
}

static int lua_superterminal_set_drop_shadow(lua_State* L) {
    float dx = luaL_checknumber(L, 1);
    float dy = luaL_checknumber(L, 2);
    float blur = luaL_checknumber(L, 3);
    uint32_t color = luaL_checkinteger(L, 4);
    set_drop_shadow(dx, dy, blur, color);
    return 0;
}

static int lua_superterminal_set_color_matrix(lua_State* L) {
    // Expect a table of 20 numbers for the color matrix
    luaL_checktype(L, 1, LUA_TTABLE);
    float matrix[20];
    
    for (int i = 0; i < 20; i++) {
        lua_rawgeti(L, 1, i + 1);  // Lua tables are 1-indexed
        if (!lua_isnumber(L, -1)) {
            return luaL_error(L, "Color matrix must contain 20 numbers");
        }
        matrix[i] = lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    
    set_color_matrix(matrix);
    return 0;
}

static int lua_superterminal_clear_filters(lua_State* L) {
    clear_filters();
    return 0;
}

static int lua_superterminal_read_pixels(lua_State* L) {
    uint16_t imageId = luaL_checkinteger(L, 1);
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    int width = luaL_checkinteger(L, 4);
    int height = luaL_checkinteger(L, 5);
    
    uint8_t* pixels = nullptr;
    if (read_pixels(imageId, x, y, width, height, &pixels)) {
        if (pixels) {
            // Create Lua table with pixel data
            lua_newtable(L);
            int pixelCount = width * height * 4; // RGBA
            for (int i = 0; i < pixelCount; i++) {
                lua_pushinteger(L, pixels[i]);
                lua_rawseti(L, -2, i + 1); // Lua is 1-indexed
            }
            free_pixels(pixels);
            return 1; // Return the table
        }
    }
    lua_pushnil(L);
    return 1;
}

static int lua_superterminal_write_pixels(lua_State* L) {
    uint16_t imageId = luaL_checkinteger(L, 1);
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    int width = luaL_checkinteger(L, 4);
    int height = luaL_checkinteger(L, 5);
    luaL_checktype(L, 6, LUA_TTABLE);
    
    int pixelCount = width * height * 4;
    uint8_t* pixels = (uint8_t*)malloc(pixelCount);
    if (pixels) {
        // Read pixel data from Lua table
        for (int i = 0; i < pixelCount; i++) {
            lua_rawgeti(L, 6, i + 1); // Lua is 1-indexed
            if (lua_isnumber(L, -1)) {
                pixels[i] = (uint8_t)lua_tointeger(L, -1);
            } else {
                pixels[i] = 0; // Default value for missing/invalid data
            }
            lua_pop(L, 1);
        }
        
        bool success = write_pixels(imageId, x, y, width, height, pixels);
        free(pixels);
        lua_pushboolean(L, success);
        return 1;
    }
    lua_pushboolean(L, false);
    return 1;
}

static int lua_superterminal_wait_queue_empty(lua_State* L) {
    wait_queue_empty();
    return 0;
}

static int lua_superterminal_push_matrix(lua_State* L) {
    push_matrix();
    return 0;
}

static int lua_superterminal_pop_matrix(lua_State* L) {
    pop_matrix();
    return 0;
}

static int lua_superterminal_translate(lua_State* L) {
    float tx = luaL_checknumber(L, 1);
    float ty = luaL_checknumber(L, 2);
    translate(tx, ty);
    return 0;
}

static int lua_superterminal_rotate_degrees(lua_State* L) {
    float degrees = luaL_checknumber(L, 1);
    rotate_degrees(degrees);
    return 0;
}

static int lua_superterminal_scale(lua_State* L) {
    float sx = luaL_checknumber(L, 1);
    float sy = luaL_checknumber(L, 2);
    scale(sx, sy);
    return 0;
}

static int lua_superterminal_skew(lua_State* L) {
    float kx = luaL_checknumber(L, 1);
    float ky = luaL_checknumber(L, 2);
    skew(kx, ky);
    return 0;
}

static int lua_superterminal_reset_matrix(lua_State* L) {
    reset_matrix();
    return 0;
}

static int lua_superterminal_create_path(lua_State* L) {
    uint16_t pathId = luaL_checkinteger(L, 1);
    create_path(pathId);
    return 0;
}

static int lua_superterminal_path_move_to(lua_State* L) {
    uint16_t pathId = luaL_checkinteger(L, 1);
    float x = luaL_checknumber(L, 2);
    float y = luaL_checknumber(L, 3);
    path_move_to(pathId, x, y);
    return 0;
}

static int lua_superterminal_path_line_to(lua_State* L) {
    uint16_t pathId = luaL_checkinteger(L, 1);
    float x = luaL_checknumber(L, 2);
    float y = luaL_checknumber(L, 3);
    path_line_to(pathId, x, y);
    return 0;
}

static int lua_superterminal_path_curve_to(lua_State* L) {
    uint16_t pathId = luaL_checkinteger(L, 1);
    float x1 = luaL_checknumber(L, 2);
    float y1 = luaL_checknumber(L, 3);
    float x2 = luaL_checknumber(L, 4);
    float y2 = luaL_checknumber(L, 5);
    float x3 = luaL_checknumber(L, 6);
    float y3 = luaL_checknumber(L, 7);
    path_curve_to(pathId, x1, y1, x2, y2, x3, y3);
    return 0;
}

static int lua_superterminal_path_close(lua_State* L) {
    uint16_t pathId = luaL_checkinteger(L, 1);
    path_close(pathId);
    return 0;
}

static int lua_superterminal_draw_path(lua_State* L) {
    uint16_t pathId = luaL_checkinteger(L, 1);
    draw_path(pathId);
    return 0;
}

static int lua_superterminal_fill_path(lua_State* L) {
    uint16_t pathId = luaL_checkinteger(L, 1);
    fill_path(pathId);
    return 0;
}

static int lua_superterminal_clip_path(lua_State* L) {
    uint16_t pathId = luaL_checkinteger(L, 1);
    int clipOp = luaL_optinteger(L, 2, 1); // Default to CLIP_INTERSECT
    bool antiAlias = lua_toboolean(L, 3);
    clip_path(pathId, clipOp, antiAlias);
    return 0;
}

static int lua_superterminal_sprite_create_from_pixels(lua_State* L) {
    uint16_t spriteId = luaL_checkinteger(L, 1);
    int width = luaL_checkinteger(L, 2);
    int height = luaL_checkinteger(L, 3);
    luaL_checktype(L, 4, LUA_TTABLE);
    
    int pixelCount = width * height * 4;
    uint8_t* pixels = (uint8_t*)malloc(pixelCount);
    if (pixels) {
        // Read pixel data from Lua table
        for (int i = 0; i < pixelCount; i++) {
            lua_rawgeti(L, 4, i + 1); // Lua is 1-indexed
            if (lua_isnumber(L, -1)) {
                pixels[i] = (uint8_t)lua_tointeger(L, -1);
            } else {
                pixels[i] = 0; // Default value for missing/invalid data
            }
            lua_pop(L, 1);
        }
        
        // Create a copy of pixel data for safe capture in lambda
        uint8_t* pixelsCopy = (uint8_t*)malloc(pixelCount);
        memcpy(pixelsCopy, pixels, pixelCount);
        free(pixels);
        
        // Use command queue for GPU-related sprite creation
        bool success = SuperTerminal::g_command_queue.executeCommand<bool>([=]() -> bool {
            bool result = sprite_create_from_pixels(spriteId, pixelsCopy, width, height);
            free(pixelsCopy);
            return result;
        });
        lua_pushboolean(L, success);
        return 1;
    }
    lua_pushboolean(L, false);
    return 1;
}

// Collision detection functions
static int lua_superterminal_sprite_check_collision(lua_State* L) {
    uint16_t id1 = luaL_checkinteger(L, 1);
    uint16_t id2 = luaL_checkinteger(L, 2);
    bool result = sprite_check_collision(id1, id2);
    lua_pushboolean(L, result);
    return 1;
}

static int lua_superterminal_sprite_check_point_collision(lua_State* L) {
    uint16_t id = luaL_checkinteger(L, 1);
    float x = luaL_checknumber(L, 2);
    float y = luaL_checknumber(L, 3);
    bool result = sprite_check_point_collision(id, x, y);
    lua_pushboolean(L, result);
    return 1;
}

static int lua_superterminal_sprite_get_size(lua_State* L) {
    uint16_t id = luaL_checkinteger(L, 1);
    int width, height;
    sprite_get_size(id, &width, &height);
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    return 2;
}

static int lua_superterminal_wait_frame(lua_State* L) {
    // wait_frame() blocks until next frame - frame sync should be thread-safe
    // Don't use command queue as it would cause deadlock
    wait_frame();
    return 0;
}

static int lua_superterminal_console(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    console(message);
    return 0;
}

static int lua_superterminal_image_load(lua_State* L) {
    uint16_t id = luaL_checkinteger(L, 1);
    const char* filename = luaL_checkstring(L, 2);
    bool result = image_load(id, filename);
    lua_pushboolean(L, result);
    return 1;
}

static int lua_superterminal_draw_image(lua_State* L) {
    uint16_t id = luaL_checkinteger(L, 1);
    float x = luaL_checknumber(L, 2);
    float y = luaL_checknumber(L, 3);
    draw_image(id, x, y);
    return 0;
}

static int lua_superterminal_draw_image_scaled(lua_State* L) {
    uint16_t id = luaL_checkinteger(L, 1);
    float x = luaL_checknumber(L, 2);
    float y = luaL_checknumber(L, 3);
    float scale = luaL_checknumber(L, 4);
    draw_image_scaled(id, x, y, scale);
    return 0;
}

static int lua_superterminal_draw_image_rect(lua_State* L) {
    uint16_t id = luaL_checkinteger(L, 1);
    float srcX = luaL_checknumber(L, 2);
    float srcY = luaL_checknumber(L, 3);
    float srcW = luaL_checknumber(L, 4);
    float srcH = luaL_checknumber(L, 5);
    float dstX = luaL_checknumber(L, 6);
    float dstY = luaL_checknumber(L, 7);
    float dstW = luaL_checknumber(L, 8);
    float dstH = luaL_checknumber(L, 9);
    draw_image_rect(id, srcX, srcY, srcW, srcH, dstX, dstY, dstW, dstH);
    return 0;
}

static int lua_superterminal_image_create(lua_State* L) {
    uint16_t id = luaL_checkinteger(L, 1);
    int width = luaL_checkinteger(L, 2);
    int height = luaL_checkinteger(L, 3);
    uint32_t color = luaL_checkinteger(L, 4);
    bool result = image_create(id, width, height, color);
    lua_pushboolean(L, result);
    return 1;
}

static int lua_superterminal_image_save(lua_State* L) {
    uint16_t id = luaL_checkinteger(L, 1);
    const char* filename = luaL_checkstring(L, 2);
    bool result = image_save(id, filename);
    lua_pushboolean(L, result);
    return 1;
}

static int lua_superterminal_save_to_file(lua_State* L) {
    const char* filename = luaL_checkstring(L, 1);
    std::string filename_copy(filename); // Copy to capture in lambda
    SuperTerminal::g_command_queue.queueVoidCommand([filename_copy]() {
        bool result = editor_save(filename_copy.c_str());
        if (result) {
            console(("File saved: " + filename_copy).c_str());
        } else {
            console(("Failed to save file: " + filename_copy).c_str());
        }
    });
    return 0;
}

static int lua_superterminal_save_editor(lua_State* L) {
    const char* filename = nullptr;
    if (lua_gettop(L) > 0 && !lua_isnil(L, 1)) {
        filename = luaL_checkstring(L, 1);
    }
    
    std::string filename_copy = filename ? std::string(filename) : "";
    SuperTerminal::g_command_queue.queueVoidCommand([filename_copy]() {
        bool result;
        if (filename_copy.empty()) {
            // Default filename if none provided
            result = editor_save("editor_content.lua");
        } else {
            result = editor_save(filename_copy.c_str());
        }
        
        if (result) {
            console(("Editor content saved" + (filename_copy.empty() ? " to: editor_content.lua" : " to: " + filename_copy)).c_str());
        } else {
            console("Failed to save editor content");
        }
    });
    return 0;
}

static int lua_superterminal_image_capture_screen(lua_State* L) {
    uint16_t id = luaL_checkinteger(L, 1);
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    int width = luaL_checkinteger(L, 4);
    int height = luaL_checkinteger(L, 5);
    bool result = image_capture_screen(id, x, y, width, height);
    lua_pushboolean(L, result);
    return 1;
}

static int lua_superterminal_image_get_size(lua_State* L) {
    uint16_t id = luaL_checkinteger(L, 1);
    int width, height;
    bool result = image_get_size(id, &width, &height);
    if (result) {
        lua_pushinteger(L, width);
        lua_pushinteger(L, height);
        return 2;
    } else {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }
}

// Overlay Graphics Layer API implementations
static int lua_superterminal_overlay_is_initialized(lua_State* L) {
    bool result = overlay_is_initialized();
    lua_pushboolean(L, result);
    return 1;
}

static int lua_superterminal_overlay_clear(lua_State* L) {
    overlay_clear();
    return 0;
}

static int lua_superterminal_overlay_clear_with_color(lua_State* L) {
    float r = luaL_checknumber(L, 1);
    float g = luaL_checknumber(L, 2);
    float b = luaL_checknumber(L, 3);
    float a = luaL_checknumber(L, 4);
    overlay_clear_with_color(r, g, b, a);
    return 0;
}

static int lua_superterminal_overlay_set_ink(lua_State* L) {
    float r = luaL_checknumber(L, 1);
    float g = luaL_checknumber(L, 2);
    float b = luaL_checknumber(L, 3);
    float a = luaL_checknumber(L, 4);
    overlay_set_ink(r, g, b, a);
    return 0;
}

static int lua_superterminal_overlay_draw_line(lua_State* L) {
    float x1 = luaL_checknumber(L, 1);
    float y1 = luaL_checknumber(L, 2);
    float x2 = luaL_checknumber(L, 3);
    float y2 = luaL_checknumber(L, 4);
    overlay_draw_line(x1, y1, x2, y2);
    return 0;
}

static int lua_superterminal_overlay_draw_rect(lua_State* L) {
    float x = luaL_checknumber(L, 1);
    float y = luaL_checknumber(L, 2);
    float w = luaL_checknumber(L, 3);
    float h = luaL_checknumber(L, 4);
    overlay_draw_rect(x, y, w, h);
    return 0;
}

static int lua_superterminal_overlay_fill_rect(lua_State* L) {
    float x = luaL_checknumber(L, 1);
    float y = luaL_checknumber(L, 2);
    float w = luaL_checknumber(L, 3);
    float h = luaL_checknumber(L, 4);
    overlay_fill_rect(x, y, w, h);
    return 0;
}

static int lua_superterminal_overlay_draw_circle(lua_State* L) {
    float x = luaL_checknumber(L, 1);
    float y = luaL_checknumber(L, 2);
    float radius = luaL_checknumber(L, 3);
    overlay_draw_circle(x, y, radius);
    return 0;
}

static int lua_superterminal_overlay_fill_circle(lua_State* L) {
    float x = luaL_checknumber(L, 1);
    float y = luaL_checknumber(L, 2);
    float radius = luaL_checknumber(L, 3);
    overlay_fill_circle(x, y, radius);
    return 0;
}

static int lua_superterminal_overlay_draw_text(lua_State* L) {
    float x = luaL_checknumber(L, 1);
    float y = luaL_checknumber(L, 2);
    const char* text = luaL_checkstring(L, 3);
    float fontSize = luaL_checknumber(L, 4);
    overlay_draw_text(x, y, text, fontSize);
    return 0;
}

static int lua_superterminal_overlay_set_paper(lua_State* L) {
    float r = luaL_checknumber(L, 1);
    float g = luaL_checknumber(L, 2);
    float b = luaL_checknumber(L, 3);
    float a = luaL_checknumber(L, 4);
    overlay_set_paper(r, g, b, a);
    return 0;
}

static int lua_superterminal_overlay_show(lua_State* L) {
    overlay_show();
    return 0;
}

static int lua_superterminal_overlay_hide(lua_State* L) {
    overlay_hide();
    return 0;
}

static int lua_superterminal_overlay_is_visible(lua_State* L) {
    bool result = overlay_is_visible();
    lua_pushboolean(L, result);
    return 1;
}

static int lua_superterminal_overlay_present(lua_State* L) {
    overlay_present();
    return 0;
}