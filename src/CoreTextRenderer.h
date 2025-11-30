//
//  CoreTextRenderer.h
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  CoreText-based text renderer - replaces STB TrueType with native macOS rendering
//

#ifndef CORETEXTRENDERER_H
#define CORETEXTRENDERER_H

#include <stdbool.h>
#include <stdint.h>
#include "TextCommon.h"

// Text mode definitions (C64-inspired display modes)
typedef enum {
    TEXT_MODE_20x25 = 0,   // Giant - accessibility/presentation (48pt)
    TEXT_MODE_40x25 = 1,   // Large - C64 classic (24pt)
    TEXT_MODE_40x50 = 2,   // Medium - more rows (24pt)
    TEXT_MODE_64x44 = 3,   // Standard - current default (16pt)
    TEXT_MODE_80x25 = 4,   // Compact - wide code (12pt)
    TEXT_MODE_80x50 = 5,   // Dense - maximum visibility (12pt)
    TEXT_MODE_120x60 = 6   // UltraWide - maximum screen real estate (8pt)
} TextMode;

typedef struct {
    TextMode mode;
    int columns;
    int rows;
    float fontSize;
    const char* name;
    const char* description;
} TextModeConfig;

#ifdef __cplusplus
extern "C" {
#endif

// Text mode management
bool text_mode_set(TextMode mode);
TextMode text_mode_get(void);
int text_mode_get_columns(void);
int text_mode_get_rows(void);
float text_mode_get_font_size(void);
const char* text_mode_get_name(void);
const char* text_mode_get_description(void);
TextModeConfig text_mode_get_config(TextMode mode);

// Layer-specific state management
bool text_mode_set_for_layer(TextMode mode, int layer);
TextMode text_mode_get_for_layer(int layer);
void text_mode_save_layer_state(int layer);
void text_mode_restore_layer_state(int layer);

// Initialization and cleanup
void truetype_text_layers_init(void* device); // Legacy single-parameter version
bool coretext_text_layers_init(void* device, const char* fontPath, float fontSize);
void coretext_text_layers_cleanup(void);

// Terminal layer (Layer 5) functions
void coretext_terminal_print(const char* text);
void coretext_terminal_print_at(int x, int y, const char* text);
void coretext_terminal_clear(void);
void coretext_terminal_home(void);
void coretext_terminal_render(void* encoder, float width, float height);

// Editor layer viewport control
void coretext_editor_set_viewport(int startRow, int rowCount);
void coretext_editor_set_viewport_with_layout(int startRow, int rowCount, 
                                             float offsetX, float offsetY,
                                             float cellWidth, float cellHeight);

// Override cell sizes with TextGridManager values
void coretext_set_cell_size(float cellWidth, float cellHeight);
void coretext_terminal_set_cell_size(float cellWidth, float cellHeight);
void coretext_editor_set_cell_size(float cellWidth, float cellHeight);

// Font reloading for dynamic resizing
bool coretext_terminal_reload_font(float fontSize, float windowWidth, float windowHeight);
bool coretext_editor_reload_font(float fontSize, float windowWidth, float windowHeight);

// Editor layer (Layer 6) functions
void coretext_editor_print(const char* text);
void coretext_editor_print_at(int x, int y, const char* text);
void coretext_editor_clear(void);
void coretext_editor_home(void);
void coretext_editor_render(void* encoder, float width, float height);
void coretext_status(const char* text);

// Global screen cursor API (used by all modes: editor, REPL, terminal)
void screen_cursor_set_position(int x, int y);
void screen_cursor_set_visible(bool visible);
void screen_cursor_set_blink_phase(float phase);
void screen_cursor_set_color(float r, float g, float b, float a);
void screen_cursor_render(void* encoder, float width, float height);

// Legacy editor cursor API (now redirects to screen cursor)
void coretext_editor_set_cursor_position(int x, int y, bool visible);
void coretext_editor_update_cursor_blink(float phase);

// Color management
void coretext_terminal_set_color(float ink_r, float ink_g, float ink_b, float ink_a,
                                 float paper_r, float paper_g, float paper_b, float paper_a);
void coretext_terminal_set_ink(float r, float g, float b, float a);
void coretext_terminal_set_paper(float r, float g, float b, float a);

// Direct grid manipulation
void coretext_poke_colour(int layer, int x, int y, uint32_t ink_colour, uint32_t paper_colour);
void coretext_poke_ink(int layer, int x, int y, uint32_t ink_colour);
void coretext_poke_paper(int layer, int x, int y, uint32_t paper_colour);

// Direct grid access API - exposes contiguous memory for fast operations
struct TextCell* coretext_get_grid_buffer(int layer);
int coretext_get_grid_width(void);
int coretext_get_grid_height(void);

// Chunky pixel graphics API (sextant mode - 2x3 pixels per character cell)
// Uses Unicode sextant range U+1FB00-U+1FB3F for patterns
// Provides retro-style chunky graphics with cell-based ink/paper colors
void chunky_pixel(int pixel_x, int pixel_y, bool on);
void chunky_line(int x1, int y1, int x2, int y2);
void chunky_clear(void);
void chunky_rect(int x, int y, int width, int height, bool filled);
void chunky_get_resolution(int* width, int* height);
int coretext_get_grid_stride(void);  // Returns GRID_WIDTH (bytes per row)

// Bulk operations for fast rendering
void coretext_clear_region(int layer, int x, int y, int width, int height, 
                          uint32_t character, uint32_t ink, uint32_t paper);
void coretext_fill_rect(int layer, int x, int y, int width, int height, 
                        uint32_t ink, uint32_t paper);

// CRT Visual Effects
void coretext_set_crt_glow(bool enabled);
bool coretext_get_crt_glow(void);
void coretext_set_crt_glow_intensity(float intensity);
float coretext_get_crt_glow_intensity(void);

void coretext_set_crt_scanlines(bool enabled);
bool coretext_get_crt_scanlines(void);
void coretext_set_crt_scanline_intensity(float intensity);
float coretext_get_crt_scanline_intensity(void);

// Legacy font overdraw control (now maps to CRT glow)
void coretext_set_font_overdraw(bool enabled);
bool coretext_get_font_overdraw(void);

// Editor buffer access
struct TextCell* coretext_editor_get_text_buffer(void);
void coretext_editor_set_cursor_colors(float ink_r, float ink_g, float ink_b, float ink_a,
                                       float paper_r, float paper_g, float paper_b, float paper_a);

// Compatibility aliases (map to CoreText functions)
// These allow existing code to work without changes
// Note: truetype_text_layers_init is a real function, not a macro
#define truetype_text_layers_cleanup coretext_text_layers_cleanup
#define truetype_terminal_print coretext_terminal_print
#define truetype_terminal_print_at coretext_terminal_print_at
#define truetype_terminal_clear coretext_terminal_clear
#define truetype_terminal_home coretext_terminal_home
#define truetype_terminal_render coretext_terminal_render
#define truetype_editor_print coretext_editor_print
#define truetype_editor_print_at coretext_editor_print_at
#define truetype_editor_clear coretext_editor_clear
#define truetype_editor_home coretext_editor_home
#define truetype_editor_render coretext_editor_render
#define truetype_status coretext_status
#define truetype_terminal_set_color coretext_terminal_set_color
#define truetype_terminal_set_ink coretext_terminal_set_ink
#define truetype_terminal_set_paper coretext_terminal_set_paper
#define truetype_poke_colour coretext_poke_colour
#define truetype_poke_ink coretext_poke_ink
#define truetype_poke_paper coretext_poke_paper
#define truetype_set_font_overdraw coretext_set_font_overdraw
#define truetype_get_font_overdraw coretext_get_font_overdraw
#define truetype_set_crt_glow coretext_set_crt_glow
#define truetype_get_crt_glow coretext_get_crt_glow
#define truetype_set_crt_scanlines coretext_set_crt_scanlines
#define truetype_get_crt_scanlines coretext_get_crt_scanlines
#define editor_get_text_buffer coretext_editor_get_text_buffer
#define editor_set_cursor_colors coretext_editor_set_cursor_colors

// Legacy compatibility functions
#define truetype_text_layer_print coretext_terminal_print
#define truetype_text_layer_print_at coretext_terminal_print_at
#define truetype_text_layer_clear coretext_terminal_clear
#define truetype_text_layer_home coretext_terminal_home
#define truetype_text_layer_render coretext_terminal_render

#ifdef __cplusplus
}
#endif

#endif /* CORETEXTRENDERER_H */