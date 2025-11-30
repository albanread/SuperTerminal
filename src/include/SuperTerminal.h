//
//  SuperTerminal.h
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#ifndef SUPERTERMINAL_H
#define SUPERTERMINAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __OBJC__
#import <simd/simd.h>
#else
typedef float simd_float4 __attribute__((__ext_vector_type__(4)));
#endif

// Text cell structure for direct buffer access
#ifndef TEXTCELL_DEFINED
#define TEXTCELL_DEFINED
struct TextCell {
    uint32_t character;    // Unicode codepoint
    simd_float4 inkColor;  // Foreground color (RGBA)
    simd_float4 paperColor; // Background color (RGBA)
};
#endif

//! Project version number for SuperTerminal.
extern double SuperTerminalVersionNumber;

//! Project version string for SuperTerminal.
extern const unsigned char SuperTerminalVersionString[];

#ifdef __cplusplus
extern "C" {
#endif

// MARK: - Framework Entry Point

/**
 * Main entry point for SuperTerminal applications.
 * This function initializes the SuperTerminal environment and calls the provided
 * app_start function in a background thread.
 *
 * @param app_start Function pointer to application entry point
 */
void superterminal_run(void (*app_start)(void));

/**
 * Exit the SuperTerminal application with the specified code.
 *
 * @param code Exit code (0 for success)
 */
void superterminal_exit(int code);

/**
 * Check if the application is currently terminating.
 *
 * @return true if application is shutting down, false otherwise
 */
bool superterminal_is_terminating(void);

// MARK: - Input Functions

/**
 * Check if a key is currently pressed (non-blocking).
 *
 * @param key Key code to check
 * @return true if key is pressed, false otherwise
 */
bool isKeyPressed(int key);

/**
 * Get the current key state (non-blocking).
 *
 * @return Current key code, or 0 if no key pressed
 */
int key(void);

/**
 * Wait for a key press (blocking).
 * This function blocks the calling thread until a key is pressed.
 *
 * @return Key code of the pressed key
 */
int waitKey(void);

/**
 * Wait for a key press and return ASCII character (blocking).
 * This function blocks the calling thread until a key is pressed.
 * Returns the ASCII character representation if available.
 *
 * @return ASCII character code (32-126) or -1 if not a printable character
 */
int waitKeyChar(void);

/**
 * Wait for the next frame to be rendered (blocking).
 * This function blocks the calling thread until the next frame is presented.
 * Use this for frame-synchronized animation and timing.
 */
void wait_frame(void);

/**
 * Log waitKey performance metrics (min, max, avg, count).
 * Outputs detailed statistics about waitKey blocking times to console.
 */
void waitkey_log_metrics(void);

/**
 * Reset waitKey performance metrics.
 * Clears all accumulated statistics.
 */
void waitkey_reset_metrics(void);

/**
 * Accept line input at specified position (blocking).
 * Blocks until user presses Enter.
 *
 * @param x X coordinate (0-79)
 * @param y Y coordinate (0-24)
 * @return Pointer to input string (caller must free)
 */
char* accept_at(int x, int y);

/**
 * Accept fixed-length input at specified position (blocking).
 * Blocks until n characters are entered or Enter is pressed.
 *
 * @param x X coordinate (0-79)
 * @param y Y coordinate (0-24)
 * @param n Maximum number of characters to accept
 * @return Pointer to input string (caller must free)
 */
char* accept_at_n(int x, int y, int n);

/**
 * Accept line input at current cursor position (blocking).
 * Blocks until user presses Enter.
 *
 * @return Pointer to input string (caller must free)
 */
char* accept_ar(void);

// MARK: - Text Output (Commodore 64 Style)

/**
 * Print text at current cursor position.
 *
 * @param text Text to print (UTF-8 encoded)
 */
void print(const char* text);

/**
 * Print text at specified position.
 *
 * @param x X coordinate (0-79)
 * @param y Y coordinate (0-24)
 * @param text Text to print (UTF-8 encoded)
 */
void print_at(int x, int y, const char* text);

/**
 * Clear the text screen.
 */
void cls(void);

/**
 * Move cursor to home position (0,0).
 */
void home(void);

/**
 * Display status text on the last line of the top overlay layer.
 * This appears as a status bar that's always visible on top.
 *
 * @param text Status text to display (UTF-8 encoded)
 */
void status(const char* text);

// MARK: - Cursor Control

/**
 * Show the cursor.
 */
void cursor_show(void);

/**
 * Hide the cursor.
 */
void cursor_hide(void);

/**
 * Move cursor to specified position.
 *
 * @param x X coordinate (0-79)
 * @param y Y coordinate (0-24)
 */
void cursor_move(int x, int y);

// MARK: - Color Control (High Color RGBA)

/**
 * Set both foreground and background colors.
 *
 * @param ink Foreground color (RGBA packed as uint32_t)
 * @param paper Background color (RGBA packed as uint32_t)
 */
void set_color(uint32_t ink, uint32_t paper);

/**
 * Set foreground color only.
 *
 * @param color Foreground color (RGBA packed as uint32_t)
 */
void set_ink(uint32_t color);

/**
 * Set background color only.
 *
 * @param color Background color (RGBA packed as uint32_t)
 */
void set_paper(uint32_t color);

// MARK: - Editor Control

/**
 * Check if the editor is currently visible.
 *
 * @return true if editor is visible, false otherwise
 */
bool editor_visible(void);

// MARK: - Editor Text Operations (Layer 6)

/**
 * Clear the editor text layer (Layer 6).
 */
void editor_cls(void);

/**
 * Print text at specified position on editor layer (Layer 6).
 *
 * @param x X coordinate (0-79)
 * @param y Y coordinate (0-24)
 * @param text Text to print (UTF-8 encoded)
 */
void editor_print_at(int x, int y, const char* text);

/**
 * Set text and background colors for editor layer (Layer 6).
 *
 * @param ink Text color (RGBA packed as uint32_t)
 * @param paper Background color (RGBA packed as uint32_t)
 */
void editor_set_color(uint32_t ink, uint32_t paper);

/**
 * Set background color for editor layer (Layer 6).
 *
 * @param color Background color (RGBA packed as uint32_t)
 */
void editor_background_color(uint32_t color);

/**
 * Get direct access to editor text buffer (Layer 6).
 * Returns pointer to 80x25 TextCell array or NULL if not available.
 *
 * @return Pointer to TextCell buffer or NULL
 */
struct TextCell* editor_get_text_buffer(void);

/**
 * Set current ink and paper colors for editor layer cursor.
 *
 * @param ink_r Red component of ink color (0.0-1.0)
 * @param ink_g Green component of ink color (0.0-1.0)
 * @param ink_b Blue component of ink color (0.0-1.0)
 * @param ink_a Alpha component of ink color (0.0-1.0)
 * @param paper_r Red component of paper color (0.0-1.0)
 * @param paper_g Green component of paper color (0.0-1.0)
 * @param paper_b Blue component of paper color (0.0-1.0)
 * @param paper_a Alpha component of paper color (0.0-1.0)
 */
void editor_set_cursor_colors(float ink_r, float ink_g, float ink_b, float ink_a,
                             float paper_r, float paper_g, float paper_b, float paper_a);

// MARK: - Graphics Operations (Layer 4)

/**
 * Draw a line between two points.
 *
 * @param x1 Start X coordinate
 * @param y1 Start Y coordinate
 * @param x2 End X coordinate
 * @param y2 End Y coordinate
 * @param color Line color (RGBA packed as uint32_t)
 */
void draw_line(float x1, float y1, float x2, float y2, uint32_t color);

/**
 * Draw a rectangle outline.
 *
 * @param x X coordinate of top-left corner
 * @param y Y coordinate of top-left corner
 * @param w Width
 * @param h Height
 * @param color Outline color (RGBA packed as uint32_t)
 */
void draw_rect(float x, float y, float w, float h, uint32_t color);

/**
 * Draw a circle outline.
 *
 * @param x Center X coordinate
 * @param y Center Y coordinate
 * @param radius Circle radius
 * @param color Outline color (RGBA packed as uint32_t)
 */
void draw_circle(float x, float y, float radius, uint32_t color);

/**
 * Draw text at specified position
 * @param x X coordinate for text position
 * @param y Y coordinate for text position
 * @param text Text string to draw
 * @param fontSize Font size in pixels
 * @param color Text color (RGBA packed as uint32_t)
 */
void draw_text(float x, float y, const char* text, float fontSize, uint32_t color);

/**
 * Fill a rectangle with solid color.
 *
 * @param x X coordinate of top-left corner
 * @param y Y coordinate of top-left corner
 * @param w Width
 * @param h Height
 * @param color Fill color (RGBA packed as uint32_t)
 */
void fill_rect(float x, float y, float w, float h, uint32_t color);

/**
 * Fill a circle with solid color.
 *
 * @param x Center X coordinate
 * @param y Center Y coordinate
 * @param radius Circle radius
 * @param color Fill color (RGBA packed as uint32_t)
 */
void fill_circle(float x, float y, float radius, uint32_t color);

/**
 * Clear the graphics layer.
 */
void graphics_clear(void);

/**
 * Swap the front and back graphics buffers for smooth animation.
 * All drawing operations occur on the back buffer until this is called.
 */
void graphics_swap(void);

/**
 * Draw a linear gradient between two points.
 *
 * @param x1 Start X coordinate
 * @param y1 Start Y coordinate
 * @param x2 End X coordinate
 * @param y2 End Y coordinate
 * @param color1 Start color (RGBA packed as uint32_t)
 * @param color2 End color (RGBA packed as uint32_t)
 */
void draw_linear_gradient(float x1, float y1, float x2, float y2, uint32_t color1, uint32_t color2);

/**
 * Fill a rectangle with a linear gradient.
 *
 * @param x X coordinate of top-left corner
 * @param y Y coordinate of top-left corner
 * @param w Width
 * @param h Height
 * @param color1 Start color (RGBA packed as uint32_t)
 * @param color2 End color (RGBA packed as uint32_t)
 * @param direction 0=horizontal, 1=vertical, 2=diagonal
 */
void fill_linear_gradient_rect(float x, float y, float w, float h, uint32_t color1, uint32_t color2, int direction);

/**
 * Draw a radial gradient from center point.
 *
 * @param centerX Center X coordinate
 * @param centerY Center Y coordinate
 * @param radius Gradient radius
 * @param color1 Center color (RGBA packed as uint32_t)
 * @param color2 Edge color (RGBA packed as uint32_t)
 */
void draw_radial_gradient(float centerX, float centerY, float radius, uint32_t color1, uint32_t color2);

/**
 * Fill a circle with a radial gradient.
 *
 * @param centerX Center X coordinate
 * @param centerY Center Y coordinate
 * @param radius Circle radius
 * @param color1 Center color (RGBA packed as uint32_t)
 * @param color2 Edge color (RGBA packed as uint32_t)
 */
void fill_radial_gradient_circle(float centerX, float centerY, float radius, uint32_t color1, uint32_t color2);

/**
 * Load an image from file.
 *
 * @param id Image ID (0-255)
 * @param filename Path to image file (PNG, JPEG, etc.)
 * @return true if loaded successfully, false otherwise
 */
bool image_load(uint16_t id, const char* filename);

/**
 * Draw an image at specified position.
 *
 * @param id Image ID
 * @param x X coordinate
 * @param y Y coordinate
 */
void draw_image(uint16_t id, float x, float y);

/**
 * Draw an image with scaling.
 *
 * @param id Image ID
 * @param x X coordinate
 * @param y Y coordinate
 * @param scale Scale factor (1.0 = original size)
 */
void draw_image_scaled(uint16_t id, float x, float y, float scale);

/**
 * Draw a rectangular region of an image.
 *
 * @param id Image ID
 * @param srcX Source rectangle X coordinate
 * @param srcY Source rectangle Y coordinate
 * @param srcW Source rectangle width
 * @param srcH Source rectangle height
 * @param dstX Destination X coordinate
 * @param dstY Destination Y coordinate
 * @param dstW Destination width
 * @param dstH Destination height
 */
void draw_image_rect(uint16_t id, float srcX, float srcY, float srcW, float srcH,
                     float dstX, float dstY, float dstW, float dstH);

/**
 * Create a blank image of specified dimensions.
 *
 * @param id Image ID (0-255)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param color Fill color (RGBA packed as uint32_t)
 * @return true if created successfully, false otherwise
 */
bool image_create(uint16_t id, int width, int height, uint32_t color);

/**
 * Save an image to PNG file.
 *
 * @param id Image ID
 * @param filename Path to save PNG file
 * @return true if saved successfully, false otherwise
 */
bool image_save(uint16_t id, const char* filename);

/**
 * Capture the current graphics layer to an image.
 *
 * @param id Image ID to store the capture
 * @param x X coordinate of capture region
 * @param y Y coordinate of capture region
 * @param width Width of capture region
 * @param height Height of capture region
 * @return true if captured successfully, false otherwise
 */
bool image_capture_screen(uint16_t id, int x, int y, int width, int height);

/**
 * Get image dimensions.
 *
 * @param id Image ID
 * @param width Pointer to store width
 * @param height Pointer to store height
 * @return true if image exists, false otherwise
 */
bool image_get_size(uint16_t id, int* width, int* height);





// MARK: - Sprite Operations (Layer 7)

/**
 * Load a sprite from PNG file.
 * Sprite must be 128x128 pixels.
 *
 * @param id Sprite ID (0-255)
 * @param filename Path to PNG file
 * @return true if loaded successfully, false otherwise
 */
bool sprite_load(uint16_t id, const char* filename);

/**
 * Show a sprite at specified position.
 *
 * @param id Sprite ID (0-255)
 * @param x X coordinate (sub-pixel positioning)
 * @param y Y coordinate (sub-pixel positioning)
 */
void sprite_show(uint16_t id, float x, float y);

/**
 * Hide a sprite.
 *
 * @param id Sprite ID (0-255)
 */
void sprite_hide(uint16_t id);

/**
 * Release a sprite and free its resources.
 * The sprite ID becomes available for reuse.
 *
 * @param id Sprite ID (0-255)
 */
void sprite_release(uint16_t id);

/**
 * Get the next available sprite ID.
 * Returns 0 if no IDs are available.
 *
 * @return Next available sprite ID (1-1024) or 0 if none available
 */
uint16_t sprite_next_id();

/**
 * Move a sprite to new position.
 *
 * @param id Sprite ID (0-255)
 * @param x New X coordinate
 * @param y New Y coordinate
 */
void sprite_move(uint16_t id, float x, float y);

/**
 * Scale a sprite.
 *
 * @param id Sprite ID (0-255)
 * @param scale Scale factor (1.0 = normal size)
 */
void sprite_scale(uint16_t id, float scale);

/**
 * Rotate a sprite.
 *
 * @param id Sprite ID (0-255)
 * @param angle Rotation angle in radians
 */
void sprite_rotate(uint16_t id, float angle);

/**
 * Set sprite transparency.
 *
 * @param id Sprite ID (1-255)
 * @param alpha Alpha value (0.0 = transparent, 1.0 = opaque)
 */
void sprite_alpha(uint16_t id, float alpha);

/**
 * Begin rendering to a sprite using Skia drawing commands.
 * All subsequent drawing commands will be captured to create the sprite.
 *
 * @param id Sprite ID (1-255)
 * @param width Width of sprite in pixels
 * @param height Height of sprite in pixels
 * @return true if rendering began successfully, false otherwise
 */
bool sprite_begin_render(uint16_t id, int width, int height);

/**
 * End sprite rendering and create the sprite from captured drawing commands.
 *
 * @param id Sprite ID (1-255) - must match the ID used in sprite_begin_render
 * @return true if sprite created successfully, false otherwise
 */
bool sprite_end_render(uint16_t id);



/**
 * Create a tile from pixel data.
 *
 * @param id Tile ID (1-255)
 * @param pixels RGBA pixel data (4 bytes per pixel)
 * @param width Width in pixels (must be 128)
 * @param height Height in pixels (must be 128)
 * @return true if tile created successfully, false otherwise
 */
bool tile_create_from_pixels(uint16_t id, const uint8_t* pixels, int width, int height);

/**
 * Begin rendering to a tile using Skia drawing commands.
 * All subsequent drawing commands will be captured to create the tile.
 * Tiles are always 128x128 pixels.
 *
 * @param id Tile ID (1-255)
 * @return true if rendering began successfully, false otherwise
 */
bool tile_begin_render(uint16_t id);

/**
 * End tile rendering and create the tile from captured drawing commands.
 *
 * @param id Tile ID (1-255) - must match the ID used in tile_begin_render
 * @return true if tile created successfully, false otherwise
 */
bool tile_end_render(uint16_t id);

/**
 * Get sprite texture dimensions.
 *
 * @param id Sprite ID (1-255)
 * @param width Pointer to store width (can be NULL)
 * @param height Pointer to store height (can be NULL)
 */
void sprite_get_size(uint16_t id, int* width, int* height);

/**
 * Get sprite aspect ratio (width/height).
 *
 * @param id Sprite ID (1-255)
 * @return Aspect ratio (1.0 for square sprites)
 */
float sprite_get_aspect_ratio(uint16_t id);

/**
 * Create a sprite from pixel data.
 *
 * @param id Sprite ID (1-255)
 * @param pixels RGBA pixel data (4 bytes per pixel)
 * @param width Width in pixels
 * @param height Height in pixels
 * @return true if sprite created successfully, false otherwise
 */
bool sprite_create_from_pixels(uint16_t id, const uint8_t* pixels, int width, int height);

/**
 * Check if two sprites are colliding using AABB collision detection.
 *
 * @param id1 First sprite ID (1-255)
 * @param id2 Second sprite ID (1-255)
 * @return true if sprites are colliding, false otherwise
 */
bool sprite_check_collision(uint16_t id1, uint16_t id2);

/**
 * Check if a point collides with a sprite.
 *
 * @param id Sprite ID (1-255)
 * @param x X coordinate
 * @param y Y coordinate
 * @return true if point is within sprite bounds, false otherwise
 */
bool sprite_check_point_collision(uint16_t id, float x, float y);

/**
 * Clear all sprite data and free allocated memory.
 * Clears all sprites and resets the system for new sprite data.
 * More comprehensive than individual sprite_release - frees memory and clears effects.
 */
void sprites_clear(void);

/**
 * Complete shutdown of sprite system.
 * Deallocates all sprite system memory and resources.
 * Sprite system must be reinitialized before use after calling this.
 */
void sprites_shutdown(void);

// MARK: - Tile Operations (Layers 2 & 3)

/**
 * Load a tile from PNG file.
 * Tile must be 128x128 pixels.
 *
 * @param id Tile ID
 * @param filename Path to PNG file
 * @return true if loaded successfully, false otherwise
 */
bool tile_load(uint16_t id, const char* filename);

/**
 * Scroll a tile layer.
 *
 * @param layer Tile layer (1 or 2)
 * @param dx X scroll offset (pixels)
 * @param dy Y scroll offset (pixels)
 */
void tile_scroll(int layer, float dx, float dy);

/**
 * Set tile layer viewport position.
 *
 * @param layer Tile layer (1 or 2)
 * @param x Viewport X position in tile coordinates
 * @param y Viewport Y position in tile coordinates
 */
void tile_set_viewport(int layer, int x, int y);

/**
 * Set a tile in the tile map.
 *
 * @param layer Tile layer (1 or 2)
 * @param map_x X position in tile map
 * @param map_y Y position in tile map
 * @param tile_id ID of tile to place
 */
void tile_set(int layer, int map_x, int map_y, uint16_t tile_id);

/**
 * Get a tile from the tile map.
 *
 * @param layer Tile layer (1 or 2)
 * @param map_x X position in tile map
 * @param map_y Y position in tile map
 * @return Tile ID at specified position
 */
uint16_t tile_get(int layer, int map_x, int map_y);

/**
 * Create a tile map with specified dimensions.
 *
 * @param layer Tile layer (1 or 2)
 * @param width Width in tiles (1-4096)
 * @param height Height in tiles (1-4096)
 */
void tile_create_map(int layer, int width, int height);

/**
 * Resize existing tile map.
 *
 * @param layer Tile layer (1 or 2)
 * @param new_width New width in tiles
 * @param new_height New height in tiles
 */
void tile_resize_map(int layer, int new_width, int new_height);

/**
 * Clear tile map (set all tiles to 0).
 *
 * @param layer Tile layer (1 or 2)
 */
void tile_clear_map(int layer);

/**
 * Clear all tile data and free allocated memory.
 * Clears both tile layers and resets them for new tile data.
 * More comprehensive than tile_clear_map - frees memory and clears atlas.
 */
void tiles_clear(void);

/**
 * Complete shutdown of tile system.
 * Deallocates all tile system memory and resources.
 * Tile system must be reinitialized before use after calling this.
 */
void tiles_shutdown(void);

/**
 * Fill entire tile map with specified tile.
 *
 * @param layer Tile layer (1 or 2)
 * @param tile_id Tile ID to fill with
 */
void tile_fill_map(int layer, uint16_t tile_id);

/**
 * Set a rectangular region of tiles.
 *
 * @param layer Tile layer (1 or 2)
 * @param start_x Starting X position
 * @param start_y Starting Y position
 * @param width Width of region
 * @param height Height of region
 * @param tile_id Tile ID to set
 */
void tile_set_region(int layer, int start_x, int start_y, int width, int height, uint16_t tile_id);

/**
 * Get tile map dimensions.
 *
 * @param layer Tile layer (1 or 2)
 * @param width Pointer to store width
 * @param height Pointer to store height
 */
void tile_get_map_size(int layer, int* width, int* height);

/**
 * Center viewport on specified tile.
 *
 * @param layer Tile layer (1 or 2)
 * @param tile_x X coordinate of tile to center on
 * @param tile_y Y coordinate of tile to center on
 */
void tile_center_viewport(int layer, int tile_x, int tile_y);

/**
 * Check if tile position is valid within map bounds.
 *
 * @param layer Tile layer (1 or 2)
 * @param x X coordinate to check
 * @param y Y coordinate to check
 * @return true if position is valid, false otherwise
 */
bool tile_is_valid_position(int layer, int x, int y);

// MARK: - Background Layer (Layer 1)

/**
 * Set background color.
 *
 * @param color Background color (RGBA packed as uint32_t)
 */
void background_color(uint32_t color);

// MARK: - Utility Functions

/**
 * Pack RGBA values into uint32_t color.
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @param a Alpha component (0-255)
 * @return Packed color value
 */
uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

// MARK: - Layer Control Functions

/**
 * Enable or disable a rendering layer.
 *
 * @param layer Layer number (1=background, 2=tile1, 3=tile2, 4=graphics, 5=sprites, 6=text)
 * @param enabled true to enable, false to disable
 */
void layer_set_enabled(int layer, bool enabled);

/**
 * Check if a rendering layer is enabled.
 *
 * @param layer Layer number (1=background, 2=tile1, 3=tile2, 4=graphics, 5=sprites, 6=text)
 * @return true if enabled, false if disabled
 */
bool layer_is_enabled(int layer);

/**
 * Enable all rendering layers.
 */
void layer_enable_all(void);

/**
 * Disable all rendering layers.
 */
void layer_disable_all(void);

// MARK: - Lua Execution Functions

/**
 * Execute Lua script in sandboxed environment (recommended approach).
 * Creates fresh thread with isolated Lua state that is completely torn down after execution.
 *
 * @param lua_code Lua code to execute
 * @return true if script thread started successfully, false on error
 */
bool exec_lua_sandboxed(const char* lua_code);

/**
 * Execute Lua script file in sandboxed environment.
 * Creates fresh thread with isolated Lua state that is completely torn down after execution.
 *
 * @param filename Path to Lua file
 * @return true if script thread started successfully, false on error
 */
bool exec_lua_file_sandboxed(const char* filename);

/**
 * Check if a Lua script is currently executing.
 *
 * @return true if script is running, false otherwise
 */
bool lua_is_executing(void);

/**
 * Terminate the currently running Lua script (ESC key handler).
 *
 * @return true if termination signal sent, false if no script running
 */
bool lua_terminate_current_script(void);

/**
 * Check if the current script execution has finished.
 *
 * @return true if no script running or script finished, false if still executing
 */
bool lua_script_finished(void);

/**
 * Wait for current script to complete with timeout.
 *
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return true if script finished within timeout, false on timeout
 */
bool lua_wait_for_completion(int timeout_ms);

/**
 * Clean up finished script execution context.
 * Call this after script finishes to free resources.
 */
void lua_cleanup_finished_execution(void);

/**
 * Legacy exec_lua_file function (compatibility shim).
 * Delegates to exec_lua_file_sandboxed for backward compatibility.
 *
 * @param filename Path to Lua file
 * @return true if script executed successfully, false on error
 */
bool exec_lua_file(const char* filename);

/**
 * Legacy exec_lua function (compatibility shim).
 * Delegates to exec_lua_sandboxed for backward compatibility.
 *
 * @param lua_code Lua code to execute
 * @return true if script executed successfully, false on error
 */
bool exec_lua(const char* lua_code);

/**
 * Legacy exec_lua_bulletproof function (compatibility shim).
 * Delegates to exec_lua_sandboxed for backward compatibility.
 *
 * @param lua_code Lua code to execute
 * @return true if script executed successfully, false on error
 */
bool exec_lua_bulletproof(const char* lua_code);

/**
 * Check if a Lua script is currently running.
 *
 * @return true if a script is currently executing, false otherwise
 */
bool is_script_running(void);

/**
 * Clean up any finished script executions.
 */
void cleanup_finished_executions(void);

/**
 * Signal that a script is starting with a custom name.
 * Updates the window title to show the script name and running status.
 *
 * Usage in Lua: start_of_script("My Script Name")
 *
 * @param script_name Name to display in window title
 */
void start_of_script(const char* script_name);

/**
 * Signal that the current script has finished execution.
 * Scripts should call this function when they are done to allow
 * new scripts to run immediately without waiting.
 *
 * Usage in Lua: end_of_script()
 */
void end_of_script(void);

/**
 * Set the window title dynamically.
 *
 * @param title New window title string
 */
void set_window_title(const char* title);

/**
 * Get last Lua error message.
 *
 * @return Error message string, or empty string if no error
 */
const char* lua_get_error(void);

/**
 * Get the current script content that was loaded for Lua execution.
 *
 * @return Script content string, or NULL if no script loaded
 */
const char* lua_get_current_script_content(void);

/**
 * Get the filename of the current script that was loaded for Lua execution.
 *
 * @return Script filename string, or NULL if no script loaded
 */
const char* lua_get_current_script_filename(void);

/**
 * Save the current Lua state for later restoration.
 * This creates a snapshot of all global variables, functions, and state.
 *
 * @return true if state saved successfully, false on error
 */
bool lua_save_state(void);

/**
 * Restore the previously saved Lua state.
 * This reverts all global variables, functions, and state to the saved snapshot.
 *
 * @return true if state restored successfully, false on error
 */
bool lua_restore_state(void);

/**
 * Check if a saved Lua state is available for restoration.
 *
 * @return true if saved state exists, false otherwise
 */
bool lua_has_saved_state(void);

/**
 * Clear any saved Lua state to free memory.
 */
void lua_clear_saved_state(void);

/**
 * Fast Lua state reset using LuaJIT-optimized methods.
 * This preserves the VM but clears user globals and flushes JIT cache.
 *
 * @return true if reset successful, false on error
 */
bool lua_fast_reset(void);

/**
 * LuaJIT-optimized memory cleanup and JIT cache flush.
 * Faster than full VM reset, suitable for frequent use.
 */
void lua_flush_jit_cache(void);

/**
 * Initialize Lua runtime with SuperTerminal APIs.
 *
 * @return true if initialization successful, false on error
 */
bool lua_init(void);

/**
 * Cleanup Lua runtime.
 */
void lua_cleanup(void);

/**
 * Reset Lua runtime and interrupt any currently executing script (complete reset).
 * This is the default and recommended reset method. It completely destroys and
 * recreates the Lua VM, providing perfect isolation between script runs.
 */
void reset_lua(void);

/**
 * Complete Lua state reset - destroys and recreates the entire Lua VM.
 * This is an alias for reset_lua() and the preferred method for clean isolation.
 * Provides a truly clean slate with no state leakage between scripts.
 */
void reset_lua_complete(void);

/**
 * Partial Lua reset - advanced option that preserves global state.
 * This interrupts running scripts but preserves global variables, functions,
 * and modules. Use only when you specifically need to maintain state between runs.
 */
void reset_lua_partial(void);

/**
 * Synchronous complete Lua state reset - destroys and recreates the entire Lua VM.
 * This function blocks until the reset is completely finished, ensuring the Lua
 * state is ready for immediate use. Use this when you need guaranteed completion
 * before proceeding with script execution.
 */
void reset_lua_complete_sync(void);

/**
 * Check if Lua is currently executing a script.
 *
 * @return true if Lua script is running, false otherwise
 */
bool lua_is_executing(void);

/**
 * Interrupt a currently executing Lua script.
 * Sets an interrupt flag that will be checked during script execution.
 */
void lua_interrupt(void);

/**
 * Terminate a Lua script with cleanup.
 * Forcefully stops script execution and performs necessary cleanup.
 */
void terminate_lua_script_with_cleanup(void);

// MARK: - Editor Mode Functions

/**
 * Toggle full-screen editor mode.
 *
 * @return true if editor is now active, false if deactivated
 */
bool editor_toggle(void);

/**
 * Check if editor mode is currently active.
 *
 * @return true if editor is active, false otherwise
 */
bool editor_is_active(void);

/**
 * Get current cursor line position (1-based).
 *
 * @return Current line number (1-based)
 */
int editor_get_cursor_line(void);

/**
 * Get current cursor column position (1-based).
 *
 * @return Current column number (1-based)
 */
int editor_get_cursor_column(void);

/**
 * Check if editor content has been modified.
 *
 * @return true if content is modified, false otherwise
 */
bool editor_is_modified(void);

/**
 * Get current filename being edited.
 *
 * @return Filename string, or "untitled.lua" if no file loaded
 */
const char* editor_get_filename(void);

/**
 * Get current filename being edited (menu system version).
 *
 * @return Filename string, or NULL if no file loaded
 */
const char* editor_get_current_filename(void);

/**
 * Create a new file in the editor.
 */
void editor_new_file(void);

/**
 * Execute the current editor content.
 */
void editor_execute_current(void);

/**
 * Update editor state and handle input (call in main loop when editor is active).
 */
void editor_update(void);

/**
 * Pass key input to editor when active (call from app's input loop).
 *
 * @param key Character code for printable keys
 * @param keycode Raw keycode for special keys (arrows, function keys, etc)
 * @param shift True if Shift key is pressed
 * @param ctrl True if Control key is pressed
 * @param alt True if Alt/Option key is pressed
 * @param cmd True if Command key is pressed
 */
void editor_key_pressed(int key, int keycode, bool shift, bool ctrl, bool alt, bool cmd);

/**
 * Update UI content - called every 60 frames (once per second).
 * Updates text content, cursor positions, etc.
 */
void ui_update_content(void);

/**
 * Draw UI overlay - called every frame to maintain persistence.
 * Draws all overlay UI elements including editor status bar.
 */
void ui_draw_overlay(void);

/**
 * Save current editor content to file.
 *
 * @param filename File to save to (in scripts/ folder)
 * @return true if save successful, false on error
 */
bool editor_save(const char* filename);

/**
 * Load file into editor.
 *
 * @param filename File to load from (in scripts/ folder)
 * @return true if load successful, false on error
 */
bool editor_load(const char* filename);

/**
 * Load file into editor by absolute path.
 *
 * @param filepath Full path to file to load
 * @return true if load successful, false on error
 */
bool editor_load_file(const char* filepath);

/**
 * Load content directly into editor buffer.
 *
 * @param content Script content string
 * @param filename Optional filename for display
 * @return true if load successful, false on error
 */
bool editor_load_content(const char* content, const char* filename);

/**
 * Run current editor content as Lua script.
 *
 * @return true if execution successful, false on error
 */
bool editor_run(void);

/**
 * Clear editor content.
 */
void editor_clear(void);

/**
 * Force editor to clear display and redraw content.
 * Called when text mode changes to refresh the display.
 */
void editor_force_redraw(void);

/**
 * Set editor status message.
 *
 * @param message Status message to display
 * @param duration Duration in frames (default 180 = 3 seconds at 60fps)
 */
void editor_set_status(const char* message, int duration);

/**
 * Get current editor content
 * @return Editor content as string (caller must free)
 */
char* editor_get_content(void);

/**
 * Set editor content from string
 * @param content Content to load into editor
 * @param filename Optional filename to display (can be NULL)
 */
void editor_set_content(const char* content, const char* filename);

/**
 * Set database metadata for current editor content (called after loading from DB)
 * @param script_name Script name in database (without .lua extension)
 * @param version Script version
 * @param author Script author
 */
void editor_set_database_metadata(const char* script_name, const char* version, const char* author);

/**
 * Check if editor content was loaded from database
 * @return true if loaded from database, false otherwise
 */
bool editor_is_loaded_from_database(void);

/**
 * Get database script name (without .lua extension)
 * @return Script name or NULL if not from database
 */
const char* editor_get_db_script_name(void);

/**
 * Get database script version
 * @return Version string or NULL if not set
 */
const char* editor_get_db_version(void);

/**
 * Get database script author
 * @return Author string or NULL if not set
 */
const char* editor_get_db_author(void);

/**
 * Format current editor content as Lua code.
 * Applies consistent indentation and spacing to Lua code in the editor.
 */
void editor_format_lua_code(void);

// MARK: - Interactive Editor Functions (Commodore 64 Style)

/**
 * Initialize the interactive editor system.
 */
void interactive_editor_init(void);

/**
 * Cleanup the interactive editor system.
 */
void interactive_editor_cleanup(void);

/**
 * Toggle interactive editor visibility (F1 handler).
 */
void interactive_editor_toggle(void);

/**
 * Update interactive editor (call from main loop).
 */
void interactive_editor_update(void);

/**
 * Handle key input for interactive editor.
 *
 * @param key Character code for printable keys
 * @param keycode Raw keycode for special keys
 * @param shift True if Shift key is pressed
 * @param ctrl True if Control key is pressed
 * @param alt True if Alt/Option key is pressed
 * @param cmd True if Command key is pressed
 */
void interactive_editor_handle_key(int key, int keycode, bool shift, bool ctrl, bool alt, bool cmd);

/**
 * Execute current Lua code in interactive editor (F2 handler).
 *
 * @return true if execution successful, false on error
 */
bool interactive_editor_execute(void);

/**
 * Save current file in interactive editor (F8 handler).
 *
 * @return true if save successful, false on error
 */
bool interactive_editor_save(void);

/**
 * Check if interactive editor is visible.
 *
 * @return true if editor is visible, false otherwise
 */
bool interactive_editor_is_visible(void);

/**
 * Load file into interactive editor.
 *
 * @param filename Path to file to load
 * @return true if successful, false on error
 */
bool interactive_editor_load_file(const char* filename);

/**
 * Get current interactive editor content.
 *
 * @return Editor content as string (valid until next call)
 */
const char* interactive_editor_get_content(void);

/**
 * Set interactive editor content.
 *
 * @param content Content to set in editor
 */
void interactive_editor_set_content(const char* content);

/**
 * Get current time in milliseconds.
 *
 * @return Current time in milliseconds
 */
uint64_t time_ms(void);

/**
 * Sleep for the specified number of milliseconds.
 *
 * @param ms Number of milliseconds to sleep
 */
void sleep_ms(uint64_t ms);

/**
 * Output a message to the terminal console.
 * This bypasses the SuperTerminal display and goes directly to stdout.
 *
 * @param message The message to output to console
 */
void console(const char* message);

// MARK: - Key Codes

// Common key codes for use with input functions
#define ST_KEY_ESCAPE    0x35
#define ST_KEY_RETURN    0x24
#define ST_KEY_TAB       0x30
#define ST_KEY_SPACE     0x31
#define ST_KEY_DELETE    0x33
#define ST_KEY_BACKSPACE 0x33

// Arrow keys
#define ST_KEY_UP        0x7E
#define ST_KEY_DOWN      0x7D
#define ST_KEY_LEFT      0x7B
#define ST_KEY_RIGHT     0x7C

// Function keys
#define ST_KEY_F1        0x7A
#define ST_KEY_F2        0x78
#define ST_KEY_F3        0x63
#define ST_KEY_F4        0x76
#define ST_KEY_F5        0x60
#define ST_KEY_F6        0x61
#define ST_KEY_F7        0x62

// Letters (A-Z)
#define ST_KEY_A         0x00
#define ST_KEY_B         0x0B
#define ST_KEY_C         0x08
#define ST_KEY_D         0x02
#define ST_KEY_E         0x0E
#define ST_KEY_F         0x03
#define ST_KEY_G         0x05
#define ST_KEY_H         0x04
#define ST_KEY_I         0x22
#define ST_KEY_J         0x26
#define ST_KEY_K         0x28
#define ST_KEY_L         0x25
#define ST_KEY_M         0x2E
#define ST_KEY_N         0x2D
#define ST_KEY_O         0x1F
#define ST_KEY_P         0x23
#define ST_KEY_Q         0x0C
#define ST_KEY_R         0x0F
#define ST_KEY_S         0x01
#define ST_KEY_T         0x11
#define ST_KEY_U         0x20
#define ST_KEY_V         0x09
#define ST_KEY_W         0x0D
#define ST_KEY_X         0x07
#define ST_KEY_Y         0x10
#define ST_KEY_Z         0x06

// Numbers (0-9)
#define ST_KEY_0         0x1D
#define ST_KEY_1         0x12
#define ST_KEY_2         0x13
#define ST_KEY_3         0x14
#define ST_KEY_4         0x15
#define ST_KEY_5         0x17
#define ST_KEY_6         0x16
#define ST_KEY_7         0x1A
#define ST_KEY_8         0x1C
#define ST_KEY_9         0x19

// MARK: - Mouse Input

// Mouse button codes
#define ST_MOUSE_LEFT    0
#define ST_MOUSE_RIGHT   1
#define ST_MOUSE_MIDDLE  2

/**
 * Get current mouse position.
 *
 * @param x Pointer to store X coordinate
 * @param y Pointer to store Y coordinate
 */
void mouse_get_position(float* x, float* y);

/**
 * Check if mouse button is pressed.
 *
 * @param button Button code (0=left, 1=right, 2=middle)
 * @return true if button is pressed, false otherwise
 */
bool mouse_is_pressed(int button);

/**
 * Wait for mouse click (blocking).
 * This function blocks until a mouse button is clicked.
 *
 * @param button Pointer to store button code that was clicked
 * @param x Pointer to store X coordinate of click
 * @param y Pointer to store Y coordinate of click
 * @return true if click received, false on error
 */
bool mouse_wait_click(int* button, float* x, float* y);

/**
 * Check if mouse cursor is over a sprite.
 *
 * @param sprite_id Sprite ID to check collision with
 * @return true if mouse is over the sprite, false otherwise
 */
bool sprite_mouse_over(uint16_t sprite_id);

/**
 * Convert screen coordinates to text grid coordinates.
 *
 * @param screen_x Screen X coordinate
 * @param screen_y Screen Y coordinate
 * @param text_x Pointer to store text grid X coordinate (0-79)
 * @param text_y Pointer to store text grid Y coordinate (0-24)
 */
void mouse_screen_to_text(float screen_x, float screen_y, int* text_x, int* text_y);

// MARK: - Sprite Menu System

/**
 * Initialize the sprite-based menu system.
 *
 * @return true if initialization successful, false on error
 */
bool sprite_menu_initialize(void);

/**
 * Shutdown the sprite menu system and release resources.
 */
void sprite_menu_shutdown(void);

/**
 * Check if the sprite menu system is active.
 *
 * @return true if active, false otherwise
 */
bool sprite_menu_is_active(void);

/**
 * Show the main menu at default position.
 */
void sprite_menu_show_main(void);

/**
 * Show the quit confirmation menu.
 */
void sprite_menu_show_quit(void);

/**
 * Hide all visible menus.
 */
void sprite_menu_hide_all(void);

/**
 * Handle mouse click for menu interaction.
 *
 * @param x Mouse X coordinate
 * @param y Mouse Y coordinate
 * @param button Mouse button (0=left, 1=right, 2=middle)
 */
void sprite_menu_handle_mouse_click(float x, float y, int button);

/**
 * Handle mouse movement for menu hover effects.
 *
 * @param x Mouse X coordinate
 * @param y Mouse Y coordinate
 */
void sprite_menu_handle_mouse_move(float x, float y);

/**
 * Handle keyboard input for menu navigation.
 *
 * @param keycode Key code from input system
 */
void sprite_menu_handle_key(int keycode);

/**
 * Update menu system (call in main loop).
 */
void sprite_menu_update(void);

// MARK: - UI Layer System

// MARK: - Overlay Graphics Layer Functions (Graphics Layer 2)

/**
 * Initialize the overlay graphics layer system.
 *
 * @param device Metal device pointer
 * @param width Canvas width
 * @param height Canvas height
 * @return true if initialization successful, false on error
 */
bool overlay_initialize(void* device, int width, int height);

/**
 * Shutdown the overlay graphics layer system and release resources.
 */
void overlay_shutdown(void);

/**
 * Check if the overlay graphics layer system is initialized.
 *
 * @return true if initialized, false otherwise
 */
bool overlay_is_initialized(void);

/**
 * Clear the overlay graphics layer with transparent color.
 */
void overlay_clear(void);

/**
 * Clear the overlay graphics layer with specified color.
 *
 * @param r Red component (0.0-1.0)
 * @param g Green component (0.0-1.0)
 * @param b Blue component (0.0-1.0)
 * @param a Alpha component (0.0-1.0)
 */
void overlay_clear_with_color(float r, float g, float b, float a);

/**
 * Set the ink color for drawing operations.
 *
 * @param r Red component (0.0-1.0)
 * @param g Green component (0.0-1.0)
 * @param b Blue component (0.0-1.0)
 * @param a Alpha component (0.0-1.0)
 */
void overlay_set_ink(float r, float g, float b, float a);

/**
 * Set the paper (background) color for overlay text drawing operations.
 *
 * @param r Red component (0.0-1.0)
 * @param g Green component (0.0-1.0)
 * @param b Blue component (0.0-1.0)
 * @param a Alpha component (0.0-1.0)
 */
void overlay_set_paper(float r, float g, float b, float a);

/**
 * Draw a line on the overlay layer.
 *
 * @param x1 Start X coordinate
 * @param y1 Start Y coordinate
 * @param x2 End X coordinate
 * @param y2 End Y coordinate
 */
void overlay_draw_line(float x1, float y1, float x2, float y2);

/**
 * Draw a rectangle outline on the overlay layer.
 *
 * @param x X position
 * @param y Y position
 * @param w Width
 * @param h Height
 */
void overlay_draw_rect(float x, float y, float w, float h);

/**
 * Fill a rectangle on the overlay layer.
 *
 * @param x X position
 * @param y Y position
 * @param w Width
 * @param h Height
 */
void overlay_fill_rect(float x, float y, float w, float h);

/**
 * Draw a circle outline on the overlay layer.
 *
 * @param x Center X coordinate
 * @param y Center Y coordinate
 * @param radius Circle radius
 */
void overlay_draw_circle(float x, float y, float radius);

/**
 * Fill a circle on the overlay layer.
 *
 * @param x Center X coordinate
 * @param y Center Y coordinate
 * @param radius Circle radius
 */
void overlay_fill_circle(float x, float y, float radius);

/**
 * Draw text on the overlay layer.
 *
 * @param x X position
 * @param y Y position
 * @param text Text to draw
 * @param fontSize Font size
 */
void overlay_draw_text(float x, float y, const char* text, float fontSize);

/**
 * Set blur filter effect for subsequent overlay drawing operations.
 *
 * @param radius Blur radius in pixels
 */
void overlay_set_blur_filter(float radius);

/**
 * Set drop shadow effect for subsequent overlay drawing operations.
 *
 * @param dx Shadow offset X
 * @param dy Shadow offset Y
 * @param blur Shadow blur radius
 * @param r Red component (0.0-1.0)
 * @param g Green component (0.0-1.0)
 * @param b Blue component (0.0-1.0)
 * @param a Alpha component (0.0-1.0)
 */
void overlay_set_drop_shadow(float dx, float dy, float blur, float r, float g, float b, float a);

/**
 * Clear all active overlay effects (blur, shadow, etc.).
 */
void overlay_clear_filters(void);

/**
 * Present/swap the overlay layer buffers.
 */
void overlay_present(void);

/**
 * Show the overlay graphics layer.
 */
void overlay_show(void);

/**
 * Hide the overlay graphics layer.
 */
void overlay_hide(void);

/**
 * Check if the overlay graphics layer is visible.
 *
 * @return true if visible, false if hidden
 */
bool overlay_is_visible(void);

// MARK: - Audio Functions

/**
 * Initialize audio system.
 *
 * @return true if initialization successful, false on error
 */
bool audio_initialize(void);

/**
 * Shutdown audio system.
 */
void audio_shutdown(void);

/**
 * Check if audio system is initialized.
 *
 * @return true if initialized, false otherwise
 */
bool audio_is_initialized(void);

/**
 * Load sound from file.
 *
 * @param filename Path to sound file
 * @return Sound ID, or 0 on error
 */
uint32_t audio_load_sound(const char* filename);

/**
 * Play sound effect.
 *
 * @param sound_id Sound ID from audio_load_sound
 * @param volume Volume (0.0 - 1.0)
 * @param pitch Pitch multiplier (0.5 - 2.0)
 * @param pan Stereo pan (-1.0 left, 0.0 center, 1.0 right)
 */
void audio_play_sound(uint32_t sound_id, float volume, float pitch, float pan);

/**
 * Initialize synthesis engine.
 *
 * @return true if initialization successful, false on error
 */
bool synth_initialize(void);

/**
 * Shutdown synthesis engine.
 */
void synth_shutdown(void);

/**
 * Generate beep sound.
 *
 * @param filename Output file path
 * @param frequency Frequency in Hz
 * @param duration Duration in seconds
 * @return true if generation successful, false on error
 */
bool synth_generate_beep(const char* filename, float frequency, float duration);

/**
 * Generate coin pickup sound.
 *
 * @param filename Output file path
 * @param pitch Pitch multiplier
 * @param duration Duration in seconds
 * @return true if generation successful, false on error
 */
bool synth_generate_coin(const char* filename, float pitch, float duration);

/**
 * Generate explosion sound.
 *
 * @param filename Output file path
 * @param size Explosion size (0.0 - 1.0)
 * @param duration Duration in seconds
 * @return true if generation successful, false on error
 */
bool synth_generate_explode(const char* filename, float size, float duration);

/**
 * Generate zap/laser sound.
 *
 * @param filename Output file path
 * @param frequency Base frequency in Hz
 * @param duration Duration in seconds
 * @return true if generation successful, false on error
 */
bool synth_generate_zap(const char* filename, float frequency, float duration);

// MARK: - MIDI Functions

/**
 * Send MIDI note on.
 *
 * @param channel MIDI channel (0-15)
 * @param note MIDI note number (0-127)
 * @param velocity Note velocity (0-127)
 */
void audio_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity);

/**
 * Send MIDI note off.
 *
 * @param channel MIDI channel (0-15)
 * @param note MIDI note number (0-127)
 */
void audio_midi_note_off(uint8_t channel, uint8_t note);

/**
 * Send MIDI program change.
 *
 * @param channel MIDI channel (0-15)
 * @param program Program number (0-127)
 */
void audio_midi_program_change(uint8_t channel, uint8_t program);

/**
 * Send MIDI control change.
 *
 * @param channel MIDI channel (0-15)
 * @param controller Controller number (0-127)
 * @param value Controller value (0-127)
 */
void audio_midi_control_change(uint8_t channel, uint8_t controller, uint8_t value);

// MARK: - High-Level Music Functions (ABC Notation)

/**
 * Play music from ABC notation string.
 *
 * @param abc_notation ABC notation string (e.g., "C D E F | G A B c |")
 * @param name Optional song name
 * @param tempo_bpm Tempo in beats per minute
 * @param instrument General MIDI instrument (0-127)
 * @return true if playback started successfully, false on error
 */
bool music_play(const char* abc_notation, const char* name, int tempo_bpm, int instrument);

/**
 * Queue music to play after current music finishes (slot-based API).
 *
 * @param abc_notation ABC notation string
 * @param name Optional song name
 * @param tempo_bpm Tempo in beats per minute
 * @param instrument General MIDI instrument (0-127)
 * @param loop Whether to loop this music
 * @return unique slot ID (>0) if queued successfully, 0 on error
 */
uint32_t queue_music_slot(const char* abc_notation, const char* name, int tempo_bpm, int instrument, bool loop);

/**
 * Queue music to play after current music finishes (legacy API).
 *
 * @param abc_notation ABC notation string
 * @param name Optional song name
 * @param tempo_bpm Tempo in beats per minute
 * @param instrument General MIDI instrument (0-127)
 * @param loop Whether to loop this music
 * @return true if queued successfully, false on error
 */
bool music_queue(const char* abc_notation, const char* name, int tempo_bpm, int instrument, bool loop);

/**
 * Stop all music playback.
 */
void music_stop(void);

/**
 * Pause music playback.
 */
void music_pause(void);

/**
 * Resume paused music playback.
 */
void music_resume(void);

/**
 * Clear music queue.
 */
void music_clear_queue(void);

/**
 * Check if music is currently playing.
 *
 * @return true if music is playing, false otherwise
 */
bool music_is_playing(void);

/**
 * Check if music is paused.
 *
 * @return true if music is paused, false otherwise
 */
bool music_is_paused(void);

/**
 * Get current music queue size.
 *
 * @return Number of songs in queue
 */
int music_get_queue_size(void);

/**
 * Get name of currently playing song.
 *
 * @return Song name, or empty string if none playing
 */
const char* music_get_current_song_name(void);

/**
 * Set music volume.
 *
 * @param volume Volume (0.0 - 1.0)
 */
void music_set_volume(float volume);

/**
 * Get current music volume.
 *
 * @return Current volume (0.0 - 1.0)
 */
float music_get_volume(void);

/**
 * Set music tempo multiplier.
 *
 * @param multiplier Tempo multiplier (0.1 - 4.0)
 */
void music_set_tempo(float multiplier);

/**
 * Get current music tempo multiplier.
 *
 * @return Current tempo multiplier
 */
float music_get_tempo(void);

/**
 * Play music with default settings (simple version).
 *
 * @param abc_notation ABC notation string
 * @return true if playback started successfully, false on error
 */
bool music_play_simple(const char* abc_notation);

/**
 * Queue music with default settings (simple version, slot-based).
 *
 * @param abc_notation ABC notation string
 * @return unique slot ID (>0) if queued successfully, 0 on error
 */
uint32_t queue_music_slot_simple(const char* abc_notation);

/**
 * Queue music with default settings (simple version, legacy).
 *
 * @param abc_notation ABC notation string
 * @return true if queued successfully, false on error
 */
bool music_queue_simple(const char* abc_notation);

/**
 * Remove a specific music slot from the queue.
 *
 * @param slot_id The slot ID to remove
 * @return true if removed successfully, false if not found
 */
bool remove_music_slot(uint32_t slot_id);

/**
 * Check if a music slot exists in the system.
 *
 * @param slot_id The slot ID to check
 * @return true if slot exists, false otherwise
 */
bool has_music_slot(uint32_t slot_id);

/**
 * Get the number of music slots currently in the system.
 *
 * @return number of music slots
 */
int get_music_slot_count(void);

/**
 * Shutdown the music system and clean up all resources.
 */
void music_shutdown(void);

/**
 * Get a test song in ABC notation.
 *
 * @param song_name Song name ("scale", "arpeggio", "twinkle", "happy_birthday", "mary_lamb")
 * @return ABC notation string for the test song
 */
const char* music_get_test_song(const char* song_name);

// MARK: - System Reset Functions

/**
 * Reset SuperTerminal to a clean state for running a new script.
 * This function shuts down and reinitializes all subsystems, clearing
 * all state including graphics, audio, sprites, particles, tiles, text,
 * and Lua runtime. Uses complete Lua reset for perfect isolation.
 */
void superterminal_reset_all(void);

/**
 * Quick reset for script iteration (visual systems with complete Lua reset).
 * Clears graphics, sprites, particles, tiles, text, and completely resets
 * Lua state. Provides full isolation while focusing on visual cleanup.
 */
void superterminal_reset_quick(void);

/**
 * Reset audio subsystems and Lua state.
 * Stops music, shuts down audio/synth engines, reinitializes them,
 * and performs complete Lua reset for consistency.
 */
void superterminal_reset_audio_only(void);

/**
 * Reset graphics subsystems and Lua state.
 * Clears all visual layers including sprites, particles, tiles, graphics,
 * and performs complete Lua reset for consistency.
 */
void superterminal_reset_graphics_only(void);

/**
 * Emergency reset for recovery from crashed or stuck states.
 * Forces cleanup of all systems without error checking. Use when
 * normal reset fails or system is in an unknown state.
 */
void superterminal_emergency_reset(void);

/**
 * Check if a reset operation is currently in progress.
 *
 * @return true if reset is running, false otherwise
 */
bool superterminal_is_reset_in_progress(void);

/**
 * Get the total number of resets performed since startup.
 *
 * @return Number of resets executed
 */
int superterminal_get_reset_count(void);

/**
 * Set external run script callback function.
 * This allows applications to register a custom callback for the Run Script menu.
 *
 * @param callback Function pointer to run script callback, or NULL to clear
 */
void set_external_run_script_callback(void (*callback)(void));

/* ========================================================================= */
/* ABC PLAYER CLIENT API                                                     */
/* ========================================================================= */

/**
 * Initialize ABC Player Client system.
 *
 * @return true if initialization succeeded, false otherwise
 */
bool abc_client_initialize(void);

/**
 * Shutdown ABC Player Client system.
 */
void abc_client_shutdown(void);

/**
 * Check if ABC Player Client is initialized.
 *
 * @return true if initialized, false otherwise
 */
bool abc_client_is_initialized(void);

/**
 * Play ABC notation string.
 *
 * @param abc_notation ABC notation string
 * @param name Optional name for the music (can be NULL)
 * @return true if queued successfully, false otherwise
 */
bool abc_client_play_abc(const char* abc_notation, const char* name);

/**
 * Play ABC notation from file.
 *
 * @param filename Path to ABC file
 * @return true if queued successfully, false otherwise
 */
bool abc_client_play_abc_file(const char* filename);

/**
 * Stop all music playback.
 *
 * @return true if stopped successfully, false otherwise
 */
bool abc_client_stop(void);

/**
 * Pause current music playback.
 *
 * @return true if paused successfully, false otherwise
 */
bool abc_client_pause(void);

/**
 * Resume paused music playback.
 *
 * @return true if resumed successfully, false otherwise
 */
bool abc_client_resume(void);

/**
 * Clear the music queue.
 *
 * @return true if cleared successfully, false otherwise
 */
bool abc_client_clear_queue(void);

/**
 * Set playback volume.
 *
 * @param volume Volume level (0.0 to 1.0)
 * @return true if set successfully, false otherwise
 */
bool abc_client_set_volume(float volume);

/**
 * Check if music is currently playing.
 *
 * @return true if playing, false otherwise
 */
bool abc_client_is_playing(void);

/**
 * Check if music is currently paused.
 *
 * @return true if paused, false otherwise
 */
bool abc_client_is_paused(void);

/**
 * Get current queue size.
 *
 * @return Number of items in queue
 */
int abc_client_get_queue_size(void);

/**
 * Get current volume level.
 *
 * @return Volume level (0.0 to 1.0)
 */
float abc_client_get_volume(void);

/**
 * Set whether to auto-start server if needed.
 *
 * @param auto_start true to enable auto-start, false to disable
 */
void abc_client_set_auto_start_server(bool auto_start);

/**
 * Set debug output mode.
 *
 * @param debug true to enable debug output, false to disable
 */
void abc_client_set_debug_output(bool debug);

// MARK: - Text Grid and Video Mode Functions

/**
 * Video mode constants for different text grid sizes.
 */
#define VIDEO_MODE_C64      0  // 40x25 - Classic Commodore 64
#define VIDEO_MODE_STANDARD 1  // 80x25 - Standard terminal
#define VIDEO_MODE_WIDE     2  // 100x30 - Widescreen
#define VIDEO_MODE_ULTRAWIDE 3 // 120x35 - Ultra-wide monitor
#define VIDEO_MODE_RETRO    4  // 32x16 - Ultra-retro chunky text
#define VIDEO_MODE_DENSE    5  // 160x50 - Maximum density
#define VIDEO_MODE_AUTO     6  // Auto-fit to window size

/**
 * Text scaling constants.
 */
#define TEXT_SCALE_AUTO 0  // Auto-calculate best fit
#define TEXT_SCALE_1X   1  // Native pixel size
#define TEXT_SCALE_2X   2  // 2x scaling
#define TEXT_SCALE_3X   3  // 3x scaling
#define TEXT_SCALE_4X   4  // 4x scaling

/**
 * Set the video mode (text grid size).
 *
 * @param mode Video mode constant (VIDEO_MODE_*)
 */
void setVideoMode(int mode);

/**
 * Get the current video mode.
 *
 * @return Current video mode constant
 */
int getVideoMode(void);

/**
 * Set the text scaling mode.
 *
 * @param scale Text scale constant (TEXT_SCALE_*)
 */
void setTextScale(int scale);

/**
 * Get the current text scaling mode.
 *
 * @return Current text scale constant
 */
int getTextScale(void);

/**
 * Cycle through available video modes.
 * Keyboard shortcut: F10
 */
void cycleVideoMode(void);

/**
 * Cycle through available text scaling modes.
 * Keyboard shortcut: F11
 */
void cycleTextScale(void);

/**
 * Auto-fit text grid to current window size.
 * Keyboard shortcut: F12
 */
void autoFitText(void);

/**
 * Get current text grid dimensions.
 *
 * @param width Pointer to store grid width (columns)
 * @param height Pointer to store grid height (rows)
 */
void getTextGridSize(int* width, int* height);

/**
 * Get current text cell size in points.
 *
 * @param width Pointer to store cell width
 * @param height Pointer to store cell height
 */
void getTextCellSize(float* width, float* height);

/**
 * Find the best video mode for given viewport dimensions.
 *
 * @param width Viewport width in points
 * @param height Viewport height in points
 * @return Best video mode constant for the viewport
 */
int findBestVideoMode(float width, float height);

/**
 * Force refresh of text grid layout.
 * Useful after manual window resizing or display changes.
 */
void refreshTextGrid(void);

// MARK: - Chunky Pixel Graphics Functions

/**
 * Get chunky pixel resolution based on current text mode.
 * Chunky mode uses sextant patterns (2x3 pixels per character cell).
 * For example, 80x25 text mode gives 160x75 chunky pixel resolution.
 *
 * @param width Pointer to store pixel width (columns * 2)
 * @param height Pointer to store pixel height (rows * 3)
 */
void chunky_get_resolution(int* width, int* height);

/**
 * Set a single chunky pixel on or off.
 * Uses current ink/paper colors set at the character cell level.
 *
 * @param pixel_x X coordinate in chunky pixel space
 * @param pixel_y Y coordinate in chunky pixel space
 * @param on true to set pixel on (ink color), false for off (paper color)
 */
void chunky_pixel(int pixel_x, int pixel_y, bool on);

/**
 * Draw a line in chunky pixel mode using Bresenham's algorithm.
 *
 * @param x1 Starting X coordinate
 * @param y1 Starting Y coordinate
 * @param x2 Ending X coordinate
 * @param y2 Ending Y coordinate
 */
void chunky_line(int x1, int y1, int x2, int y2);

/**
 * Clear all chunky pixels (set all to off/paper color).
 */
void chunky_clear(void);

/**
 * Draw a rectangle in chunky pixel mode.
 *
 * @param x X coordinate of top-left corner
 * @param y Y coordinate of top-left corner
 * @param width Rectangle width in pixels
 * @param height Rectangle height in pixels
 * @param filled true to fill, false for outline only
 */
void chunky_rect(int x, int y, int width, int height, bool filled);

#ifdef __cplusplus
}
#endif

#endif /* SUPERTERMINAL_H */
