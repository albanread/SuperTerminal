//
//  TextEditor.cpp
//  SuperTerminal Framework - Full-Screen Text Editor
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Full-screen text editor with file operations and Lua script execution
//

#include "SuperTerminal.h"
#include "CoreTextRenderer.h"
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <sys/stat.h>
#include <cstring>
#include <set>
#include <cctype>
#include <cstdlib>
#include <mach-o/dyld.h>
#include <libgen.h>

// Forward declarations for Lua GCD Runtime
extern "C" {
    bool lua_gcd_exec(const char* lua_code, const char* script_name);
    const char* lua_gcd_get_last_error(void);
}

// Forward declarations for TextGridManager C API
extern "C" {
    void text_grid_get_dimensions(int* width, int* height);
    void text_grid_get_cell_size(float* width, float* height);
    void text_grid_get_offsets(float* offsetX, float* offsetY);
    void text_grid_get_scale_factors(float* scaleX, float* scaleY);
    void coretext_editor_set_viewport_with_layout(int startRow, int rowCount, 
                                                 float offsetX, float offsetY,
                                                 float cellWidth, float cellHeight);
    
    // CoreText cell size override functions
    void coretext_editor_set_cell_size(float cellWidth, float cellHeight);
    
    // TextGridManager callback registration
    typedef struct {
        int gridWidth;              // Calculated grid width (columns)
        int gridHeight;             // Calculated grid height (rows)
        float cellWidth;            // Cell width in points
        float cellHeight;           // Cell height in points
        float scaleFactorX;         // Horizontal scale factor
        float scaleFactorY;         // Vertical scale factor
        float offsetX;              // Horizontal centering offset
        float offsetY;              // Vertical centering offset
        bool isRetina;              // Whether display is Retina/high-DPI
        float devicePixelRatio;     // Device pixel ratio
        int actualMode;             // Actual mode used (for AUTO mode)
    } TextGridLayout;
    
    typedef void (*TextGridRecalcCallback)(TextGridLayout layout, void* userData);
    void text_grid_register_recalc_callback(TextGridRecalcCallback callback, void* userData);
    void text_grid_unregister_recalc_callback(TextGridRecalcCallback callback);
    
    // macOS Clipboard functions
    void macos_clipboard_set_text(const char* text);
    char* macos_clipboard_get_text(void);
    void macos_clipboard_free_text(char* text);
}

// Editor state
struct TextEditorState {
    std::vector<std::string> lines;
    int cursor_x;
    int cursor_y;
    int scroll_offset;
    int horizontal_offset;  // Horizontal scroll position for panning
    bool active;
    bool modified;
    std::string current_filename;
    
    // Database metadata (when loaded from database)
    bool loaded_from_database;
    std::string db_script_name;  // Name without .lua extension
    std::string db_version;
    std::string db_author;
    std::vector<std::string> db_tags;
    
    bool show_status;
    std::string status_message;
    int status_timer;
    bool cursor_visible;
    int cursor_blink_timer;
    // UI overlay system
    std::string status_bar_text;
    int ui_update_timer;
    bool show_status_bar;
    bool ui_needs_refresh;
    
    // Track last known dimensions to detect changes
    int last_known_columns;
    int last_known_rows;
    
    // Dirty line tracking for optimized rendering
    std::vector<bool> dirty_lines;
    bool full_redraw_needed;
    int last_scroll_offset;
    int last_horizontal_offset;  // Track horizontal scroll for redraw detection
    
    // Track previous cursor position to restore character when cursor moves
    int last_cursor_x;
    int last_cursor_y;
    
    // Clipboard for cut/copy/paste
    std::string clipboard;
    
    // Undo/Redo system
    struct EditorSnapshot {
        std::vector<std::string> lines;
        int cursor_x;
        int cursor_y;
        int scroll_offset;
        int horizontal_offset;  // Include horizontal scroll in undo/redo
        std::string description;
    };
    std::vector<EditorSnapshot> undo_stack;
    std::vector<EditorSnapshot> redo_stack;
    int max_undo_levels;
    
    // Search functionality
    std::string search_term;
    int search_start_x;
    int search_start_y;
    bool search_active;
    
    // Text selection (mouse drag)
    bool has_selection;
    int selection_start_x;
    int selection_start_y;
    int selection_end_x;
    int selection_end_y;
    bool is_selecting; // Currently dragging to select
};

static TextEditorState g_editor = {};

// Forward declaration for overlay functions
// Forward declarations for overlay system
extern "C" {
    bool overlay_is_initialized(void);
    void overlay_clear(void);
    void overlay_clear_with_color(float r, float g, float b, float a);
    void overlay_set_ink(float r, float g, float b, float a);
    void overlay_set_paper(float r, float g, float b, float a);
    void overlay_fill_rect(float x, float y, float w, float h);
    void overlay_draw_text(float x, float y, const char* text, float fontSize);
    void overlay_set_blur_filter(float radius);
    void overlay_set_drop_shadow(float dx, float dy, float blur, float r, float g, float b, float a);
    void overlay_clear_filters(void);
    void overlay_present(void);
    void overlay_show(void);
    void overlay_hide(void);
    bool overlay_is_visible(void);
    
    // REPL mode query function
    bool editor_is_repl_mode(void);
    
    // REPL console control functions
    void repl_deactivate(void);
    bool repl_is_active(void);
    
    // Editor viewport control
    void coretext_editor_set_viewport(int startRow, int rowCount);
    
    // Editor cursor control
    void coretext_editor_set_cursor_position(int x, int y, bool visible);
    void coretext_editor_update_cursor_blink(float phase);
    
    // Color poke functions are now handled by CoreTextRenderer.h macros
    // (removed forward declarations that were preventing macro expansion)
    
    // rgba() function already declared in SuperTerminal.h
}



// Helper functions for editor color poking
static void editor_poke_paper(int layer, int x, int y, uint32_t color) {
    truetype_poke_paper(layer, x, y, color);
}

// UI overlay configuration
const int STATUS_BAR_HEIGHT = 20;
const float STATUS_BAR_FONT_SIZE = 14.0f;
const int UI_UPDATE_INTERVAL = 30; // 30 frames = ~1 second at 30fps

// REPL mode configuration
const int REPL_MODE_LINES = 6;  // Number of lines for REPL mode

// Helper function to get lua-format binary path
static std::string get_lua_format_path() {
    // Get the path to the current executable
    char exe_path[1024];
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) == 0) {
        // Get directory containing the executable
        char* dir = dirname(exe_path);
        // lua-format is in the same directory as the executable (Contents/MacOS/)
        return std::string(dir) + "/lua-format";
    }
    // Fallback to relative path if we can't determine executable location
    return "external/LuaFormatter/lua-format";
}

// Helper functions for dynamic editor dimensions based on current text mode
static int get_editor_width() {
    int width, height;
    text_grid_get_dimensions(&width, &height);
    return width;
}

static int get_editor_height() {
    int width, height;
    text_grid_get_dimensions(&width, &height);
    if (editor_is_repl_mode()) {
        return REPL_MODE_LINES;
    }
    // Reserve rows: row 0 (unused), row 1 (blank), row 2 (top status), row 3 (ruler), row 4 (messages), last row (bottom status)
    // Subtract 6 to reserve space for status bars (top 5 rows + bottom 1 row)
    return height - 6;
}

static int get_editor_start_y() {
    if (editor_is_repl_mode()) {
        int width, height;
        text_grid_get_dimensions(&width, &height);
        return height - REPL_MODE_LINES - 1; // Start REPL area near bottom
    }
    return 5; // Start at row 5, below blank line, top status, ruler, and message line
}

static int get_content_lines() {
    int lines = get_editor_height(); // Use full editor height (status bars already excluded)
    return lines;
}

static int get_max_line_length() {
    return get_editor_width() - 1;
}

// Dirty line tracking implementation
static void mark_line_dirty(int line_index) {
    if (line_index >= 0 && line_index < (int)g_editor.dirty_lines.size()) {
        g_editor.dirty_lines[line_index] = true;
    }
}

static void mark_all_dirty() {
    g_editor.full_redraw_needed = true;
    std::fill(g_editor.dirty_lines.begin(), g_editor.dirty_lines.end(), true);
}

static void mark_range_dirty(int start_line, int end_line) {
    for (int i = start_line; i <= end_line && i < (int)g_editor.dirty_lines.size(); i++) {
        if (i >= 0) {
            g_editor.dirty_lines[i] = true;
        }
    }
}

// Forward declarations
void editor_set_database_metadata(const char* script_name, const char* version, const char* author);
static void editor_update_buffer();
static void editor_insert_char(char c);
static void editor_delete_char();
static void editor_handle_backspace();
static void editor_handle_forward_delete();
static void editor_new_line();
static void editor_move_cursor(int dx, int dy);
static void editor_scroll_if_needed();
static void editor_grid_recalc_callback(TextGridLayout layout, void* userData);
static void editor_delete_line();
static void editor_duplicate_line();
static void editor_copy_line();
static void editor_cut_line();
static void editor_paste_line();
static void editor_clear_selection();
static bool editor_has_selection();
static std::string editor_get_selected_text();
static void editor_delete_selection();
static bool is_position_in_selection(int x, int y);
static void editor_save_undo_state(const std::string& description);
static void editor_undo();
static void editor_redo();
static void editor_find();
static void editor_goto_line();
static void editor_page_up();
static void editor_page_down();
void editor_set_status(const char* message, int duration = 180); // 3 seconds at 60fps
static void editor_update_ui_content();
static void editor_refresh_overlay_ui();
static void render_line_with_syntax(const std::string& line, int screen_y);
static bool is_lua_keyword(const std::string& word);
static bool is_superterminal_function(const std::string& word);

// Dirty line tracking helpers
static void mark_line_dirty(int line_index);
static void mark_all_dirty();
static void mark_range_dirty(int start_line, int end_line);
static std::string editor_get_scripts_path();
static void editor_ensure_scripts_folder();

// UI Management System functions
extern "C" void ui_update_content(void);     // Update content every 60 frames
extern "C" void ui_draw_overlay(void);       // Draw UI every frame

// Static variables to track UI state changes
static std::string g_last_status_bar_text = "";
static bool g_ui_needs_redraw = false;
static int g_ui_frame_counter = 0;  // Force periodic redraws

// Selection helper functions
static void editor_clear_selection() {
    g_editor.has_selection = false;
    g_editor.is_selecting = false;
    mark_all_dirty(); // Redraw to remove selection highlighting
}

static bool editor_has_selection() {
    return g_editor.has_selection;
}

// Check if a position (x, y) is within the selection range
static bool is_position_in_selection(int x, int y) {
    if (!g_editor.has_selection) return false;
    
    int start_y = std::min(g_editor.selection_start_y, g_editor.selection_end_y);
    int end_y = std::max(g_editor.selection_start_y, g_editor.selection_end_y);
    int start_x = g_editor.selection_start_x;
    int end_x = g_editor.selection_end_x;
    
    // Normalize start/end if selection goes backwards
    if (g_editor.selection_start_y > g_editor.selection_end_y ||
        (g_editor.selection_start_y == g_editor.selection_end_y && 
         g_editor.selection_start_x > g_editor.selection_end_x)) {
        std::swap(start_x, end_x);
    }
    
    // Check if position is in selection range
    if (y < start_y || y > end_y) return false;
    
    if (start_y == end_y) {
        // Single line selection
        return y == start_y && x >= std::min(start_x, end_x) && x < std::max(start_x, end_x);
    } else {
        // Multi-line selection
        if (y == start_y) {
            return x >= start_x;
        } else if (y == end_y) {
            return x < end_x;
        } else {
            return true; // Middle lines are fully selected
        }
    }
}

// Get the selected text as a string
static std::string editor_get_selected_text() {
    if (!g_editor.has_selection) return "";
    
    int start_y = std::min(g_editor.selection_start_y, g_editor.selection_end_y);
    int end_y = std::max(g_editor.selection_start_y, g_editor.selection_end_y);
    int start_x = g_editor.selection_start_x;
    int end_x = g_editor.selection_end_x;
    
    // Normalize start/end if selection goes backwards
    if (g_editor.selection_start_y > g_editor.selection_end_y ||
        (g_editor.selection_start_y == g_editor.selection_end_y && 
         g_editor.selection_start_x > g_editor.selection_end_x)) {
        std::swap(start_x, end_x);
    }
    
    std::string result;
    
    if (start_y == end_y) {
        // Single line selection
        if (start_y < (int)g_editor.lines.size()) {
            const std::string& line = g_editor.lines[start_y];
            int actual_end_x = std::min(end_x, (int)line.length());
            int actual_start_x = std::min(start_x, (int)line.length());
            if (actual_end_x > actual_start_x) {
                result = line.substr(actual_start_x, actual_end_x - actual_start_x);
            }
        }
    } else {
        // Multi-line selection
        for (int y = start_y; y <= end_y && y < (int)g_editor.lines.size(); y++) {
            const std::string& line = g_editor.lines[y];
            
            if (y == start_y) {
                // First line: from start_x to end
                if (start_x < (int)line.length()) {
                    result += line.substr(start_x);
                }
                result += "\n";
            } else if (y == end_y) {
                // Last line: from beginning to end_x
                int actual_end_x = std::min(end_x, (int)line.length());
                if (actual_end_x > 0) {
                    result += line.substr(0, actual_end_x);
                }
            } else {
                // Middle lines: entire line
                result += line + "\n";
            }
        }
    }
    
    return result;
}

// Delete the selected text
static void editor_delete_selection() {
    if (!g_editor.has_selection) return;
    
    editor_save_undo_state("Delete selection");
    
    int start_y = std::min(g_editor.selection_start_y, g_editor.selection_end_y);
    int end_y = std::max(g_editor.selection_start_y, g_editor.selection_end_y);
    int start_x = g_editor.selection_start_x;
    int end_x = g_editor.selection_end_x;
    
    // Normalize start/end if selection goes backwards
    if (g_editor.selection_start_y > g_editor.selection_end_y ||
        (g_editor.selection_start_y == g_editor.selection_end_y && 
         g_editor.selection_start_x > g_editor.selection_end_x)) {
        std::swap(start_x, end_x);
    }
    
    if (start_y == end_y) {
        // Single line selection - delete characters
        if (start_y < (int)g_editor.lines.size()) {
            std::string& line = g_editor.lines[start_y];
            int actual_end_x = std::min(end_x, (int)line.length());
            int actual_start_x = std::min(start_x, (int)line.length());
            if (actual_end_x > actual_start_x) {
                line.erase(actual_start_x, actual_end_x - actual_start_x);
            }
            g_editor.cursor_x = actual_start_x;
            g_editor.cursor_y = start_y;
        }
    } else {
        // Multi-line selection - more complex
        std::string first_part, last_part;
        
        if (start_y < (int)g_editor.lines.size()) {
            first_part = g_editor.lines[start_y].substr(0, start_x);
        }
        
        if (end_y < (int)g_editor.lines.size()) {
            const std::string& last_line = g_editor.lines[end_y];
            if (end_x < (int)last_line.length()) {
                last_part = last_line.substr(end_x);
            }
        }
        
        // Delete all lines in selection
        for (int y = end_y; y >= start_y && y >= 0; y--) {
            if (y < (int)g_editor.lines.size()) {
                g_editor.lines.erase(g_editor.lines.begin() + y);
                g_editor.dirty_lines.erase(g_editor.dirty_lines.begin() + y);
            }
        }
        
        // Insert combined line
        g_editor.lines.insert(g_editor.lines.begin() + start_y, first_part + last_part);
        g_editor.dirty_lines.insert(g_editor.dirty_lines.begin() + start_y, true);
        
        g_editor.cursor_x = start_x;
        g_editor.cursor_y = start_y;
    }
    
    editor_clear_selection();
    g_editor.modified = true;
    mark_all_dirty();
}

// ============================================================================
// VIEWPORT RENDERING HELPERS - Direct Grid Access
// ============================================================================

// Helper to convert uint32_t color to simd_float4
static inline simd_float4 color_to_float4(uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    return simd_make_float4(r, g, b, a);
}

// Forward declarations for syntax highlighting
static bool is_lua_keyword(const std::string& word);
static bool is_superterminal_function(const std::string& word);

// Get syntax color for a character in Lua code
static uint32_t get_syntax_color_for_char(char c, char prev_char, char next_char, 
                                          const std::string& line, size_t pos,
                                          bool in_string, bool in_comment) {
    // Comments - light gray
    if (in_comment) {
        return rgba(180, 180, 180, 255);
    }
    
    // Strings - yellow
    if (in_string) {
        return rgba(255, 255, 0, 255);
    }
    
    // Numbers - magenta
    if (std::isdigit(c) || (c == '.' && pos > 0 && std::isdigit(prev_char))) {
        return rgba(255, 128, 255, 255);
    }
    
    // Operators - cyan
    if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
        c == '=' || c == '<' || c == '>' || c == '~' || c == '#') {
        return rgba(0, 255, 255, 255);
    }
    
    // Parentheses and brackets - light blue
    if (c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}') {
        return rgba(128, 200, 255, 255);
    }
    
    // Check if we're in a keyword or function
    if (std::isalpha(c) || c == '_') {
        // Extract word
        size_t word_start = pos;
        while (word_start > 0 && (std::isalnum(line[word_start - 1]) || line[word_start - 1] == '_')) {
            word_start--;
        }
        size_t word_end = pos;
        while (word_end < line.length() && (std::isalnum(line[word_end]) || line[word_end] == '_')) {
            word_end++;
        }
        
        std::string word = line.substr(word_start, word_end - word_start);
        
        // Keywords - light green
        if (is_lua_keyword(word)) {
            return rgba(144, 238, 144, 255);
        }
        
        // SuperTerminal functions - orange
        if (is_superterminal_function(word)) {
            return rgba(255, 165, 0, 255);
        }
    }
    
    // Default - white
    return rgba(255, 255, 255, 255);
}

// Fast viewport rendering with syntax highlighting
static void render_viewport_to_grid(struct TextCell* grid, int grid_width,
                                    int viewport_x, int viewport_y, 
                                    int viewport_width, int viewport_height,
                                    int scroll_x, int scroll_y,
                                    uint32_t default_ink, uint32_t default_paper) {
    if (!grid) return;
    
    simd_float4 paper_color = color_to_float4(default_paper);
    
    // Selection highlight color (lighter blue)
    uint32_t selection_paper = rgba(100, 150, 200, 255);
    simd_float4 selection_paper_color = color_to_float4(selection_paper);
    
    // Render visible portion of document with syntax highlighting
    for (int row = 0; row < viewport_height; row++) {
        int buffer_row = scroll_y + row;
        int grid_y = viewport_y + row;
        int grid_index = grid_y * grid_width + viewport_x;
        
        if (buffer_row >= 0 && buffer_row < (int)g_editor.lines.size()) {
            const std::string& line = g_editor.lines[buffer_row];
            
            // Track syntax state for this line
            bool in_string_double = false;
            bool in_string_single = false;
            bool in_comment = false;
            
            // Pre-scan line for comment start
            size_t comment_pos = line.find("--");
            if (comment_pos != std::string::npos) {
                in_comment = false; // Will be set true after this position
            }
            
            // Render visible columns with syntax highlighting
            for (int col = 0; col < viewport_width; col++) {
                int buffer_col = scroll_x + col;
                
                if (buffer_col < (int)line.length()) {
                    char c = line[buffer_col];
                    
                    // Update syntax state
                    if (buffer_col >= (int)comment_pos && comment_pos != std::string::npos) {
                        in_comment = true;
                    }
                    
                    if (!in_comment) {
                        if (c == '"' && (buffer_col == 0 || line[buffer_col - 1] != '\\')) {
                            in_string_double = !in_string_double;
                        }
                        if (c == '\'' && (buffer_col == 0 || line[buffer_col - 1] != '\\')) {
                            in_string_single = !in_string_single;
                        }
                    }
                    
                    bool in_string = in_string_double || in_string_single;
                    
                    // Get syntax color
                    char prev_char = (buffer_col > 0) ? line[buffer_col - 1] : ' ';
                    char next_char = (buffer_col + 1 < (int)line.length()) ? line[buffer_col + 1] : ' ';
                    uint32_t syntax_color = get_syntax_color_for_char(c, prev_char, next_char, 
                                                                       line, buffer_col,
                                                                       in_string, in_comment);
                    
                    // Check if this position is in selection
                    bool is_selected = is_position_in_selection(buffer_col, buffer_row);
                    
                    grid[grid_index].character = c;
                    grid[grid_index].inkColor = color_to_float4(syntax_color);
                    grid[grid_index].paperColor = is_selected ? selection_paper_color : paper_color;
                } else {
                    // Past end of line - check if in selection for highlighting empty space
                    bool is_selected = is_position_in_selection(buffer_col, buffer_row);
                    
                    grid[grid_index].character = ' ';
                    grid[grid_index].inkColor = color_to_float4(default_ink);
                    grid[grid_index].paperColor = is_selected ? selection_paper_color : paper_color;
                }
                grid_index++;
            }
        } else {
            // Empty line beyond document
            simd_float4 ink_color = color_to_float4(default_ink);
            for (int col = 0; col < viewport_width; col++) {
                grid[grid_index].character = ' ';
                grid[grid_index].inkColor = ink_color;
                grid[grid_index].paperColor = paper_color;
                grid_index++;
            }
        }
    }
}

// Fast clear region using direct grid access
static void clear_grid_region(struct TextCell* grid, int grid_width,
                              int x, int y, int width, int height,
                              uint32_t ink, uint32_t paper) {
    if (!grid) return;
    
    simd_float4 ink_color = color_to_float4(ink);
    simd_float4 paper_color = color_to_float4(paper);
    
    for (int row = y; row < y + height; row++) {
        int index = row * grid_width + x;
        for (int col = 0; col < width; col++) {
            grid[index].character = ' ';
            grid[index].inkColor = ink_color;
            grid[index].paperColor = paper_color;
            index++;
        }
    }
}

// Helper function to draw a ruler using sextant characters
static void draw_ruler(struct TextCell* grid, int grid_width, int y, int width, uint32_t ink, uint32_t paper) {
    if (!grid) return;
    
    // Sextant character base (Unicode block U+1FB00-U+1FB3F)
    const uint32_t SEXTANT_BASE = 0x1FB00;
    
    // Sextant patterns for ruler:
    // 0x1FB38 = bottom row pixels (bits 1,0 set) - short tick
    // 0x1FB3E = middle and bottom row pixels (bits 3,2,1,0 set) - long tick
    const uint32_t SHORT_TICK = SEXTANT_BASE + 0x38;  // Bottom row only
    const uint32_t LONG_TICK = SEXTANT_BASE + 0x3E;   // Bottom 2 rows
    
    simd_float4 ink_color = color_to_float4(ink);
    simd_float4 paper_color = color_to_float4(paper);
    
    for (int col = 0; col < width; col++) {
        int index = y * grid_width + col;
        
        if (col % 10 == 0) {
            // Every 10th column: long tick
            grid[index].character = LONG_TICK;
        } else if (col % 5 == 0) {
            // Every 5th column: short tick
            grid[index].character = SHORT_TICK;
        } else {
            // Other columns: blank space
            grid[index].character = ' ';
        }
        
        grid[index].inkColor = ink_color;
        grid[index].paperColor = paper_color;
    }
}

// Fast write text to grid
static void write_text_to_grid(struct TextCell* grid, int grid_width,
                               int x, int y, const char* text,
                               uint32_t ink, uint32_t paper) {
    if (!grid || !text) return;
    
    simd_float4 ink_color = color_to_float4(ink);
    simd_float4 paper_color = color_to_float4(paper);
    
    int index = y * grid_width + x;
    while (*text && x < grid_width) {
        grid[index].character = *text;
        grid[index].inkColor = ink_color;
        grid[index].paperColor = paper_color;
        text++;
        index++;
        x++;
    }
}

// C API implementations
extern "C" {

bool editor_toggle_impl(void) {
    if (!g_editor.active) {
        // Deactivate REPL if active (both use Layer 6 and conflict)
        if (repl_is_active()) {
            std::cout << "TextEditor: Deactivating REPL to enable full-screen editor" << std::endl;
            repl_deactivate();
            // CRITICAL: repl_deactivate() disables Layer 6, so we must re-enable it
            extern void layer_set_enabled(int layer, bool enabled);
            layer_set_enabled(6, true);
        }
        
        // Activate editor
        g_editor.active = true;
        g_editor.cursor_x = 0;
        g_editor.cursor_y = 0;
        g_editor.scroll_offset = 0;
        g_editor.horizontal_offset = 0;
        g_editor.show_status = true;
        g_editor.cursor_visible = true;
        g_editor.cursor_blink_timer = 0;
        g_editor.ui_update_timer = 0;
        g_editor.show_status_bar = true;
        g_editor.ui_needs_refresh = true;
        g_ui_needs_redraw = true;  // Force initial UI draw
        
        // Track initial dimensions and log them
        text_grid_get_dimensions(&g_editor.last_known_columns, &g_editor.last_known_rows);
        
        float cellWidth, cellHeight, offsetX, offsetY;
        text_grid_get_cell_size(&cellWidth, &cellHeight);
        text_grid_get_offsets(&offsetX, &offsetY);
        
        std::cout << "=== EDITOR ACTIVATING ===" << std::endl;
        std::cout << "Text Grid: " << g_editor.last_known_columns << "x" << g_editor.last_known_rows << std::endl;
        std::cout << "Cell Size: " << cellWidth << "x" << cellHeight << std::endl;
        std::cout << "Offsets: " << offsetX << "," << offsetY << std::endl;
        std::cout << "Editor Width: " << get_editor_width() << std::endl;
        std::cout << "Editor Height: " << get_editor_height() << std::endl;
        std::cout << "Content Start Y: " << get_editor_start_y() << std::endl;
        std::cout << "Content Lines: " << get_content_lines() << std::endl;
        
        // Initialize new fields
        g_editor.clipboard.clear();
        g_editor.undo_stack.clear();
        g_editor.redo_stack.clear();
        g_editor.max_undo_levels = 50;
        g_editor.search_term.clear();
        g_editor.search_start_x = 0;
        g_editor.search_start_y = 0;
        g_editor.search_active = false;
        
        // Initialize selection state
        g_editor.has_selection = false;
        g_editor.selection_start_x = 0;
        g_editor.selection_start_y = 0;
        g_editor.selection_end_x = 0;
        g_editor.selection_end_y = 0;
        g_editor.is_selecting = false;
        
        // Register for TextGridManager layout change notifications
        text_grid_register_recalc_callback(editor_grid_recalc_callback, nullptr);
        
        // No longer using overlay - UI is now in text grid
        
        // Only load Lua script content if editor is empty (no file was loaded)
        if (g_editor.lines.empty() || (g_editor.lines.size() == 1 && g_editor.lines[0].empty())) {
            extern const char* lua_get_current_script_content(void);
            extern const char* lua_get_current_script_filename(void);
            
            const char* script_content = lua_get_current_script_content();
            const char* script_filename = lua_get_current_script_filename();
            
            if (script_content && strlen(script_content) > 0) {
                // Clear existing content
                g_editor.lines.clear();
                g_editor.current_filename = script_filename ? script_filename : "untitled.lua";
                
                // Split script content into lines
                std::string content(script_content);
                std::stringstream ss(content);
                std::string line;
                
                while (std::getline(ss, line)) {
                    g_editor.lines.push_back(line);
                }
                
                // Ensure we have at least one line
                if (g_editor.lines.empty()) {
                    g_editor.lines.push_back("");
                }
                
                // Initialize dirty_lines to match lines size
                g_editor.dirty_lines.resize(g_editor.lines.size(), true);
                g_editor.full_redraw_needed = true;
                
                editor_set_status("Editing Lua script - F2:Execute F8:Save ESC:Exit");
            } else {
                // No script content available, start with empty editor
                g_editor.lines.push_back("");
                
                // Initialize dirty_lines to match lines size
                g_editor.dirty_lines.resize(g_editor.lines.size(), true);
                g_editor.full_redraw_needed = true;
            }
        } else {
            // Ensure dirty_lines is initialized if lines already exist
            if (g_editor.dirty_lines.size() != g_editor.lines.size()) {
                g_editor.dirty_lines.resize(g_editor.lines.size(), true);
                g_editor.full_redraw_needed = true;
            }
            editor_set_status("Editor ready - F2:Execute F8:Save ESC:Exit");
        }
        
        // In REPL mode, be very conservative - don't clear anything outside REPL area
        if (editor_is_repl_mode()) {
            // Set viewport to only render REPL area using TextGridManager dimensions
            int gridWidth, gridHeight;
            float cellWidth, cellHeight, offsetX, offsetY;
            text_grid_get_dimensions(&gridWidth, &gridHeight);
            text_grid_get_cell_size(&cellWidth, &cellHeight);
            text_grid_get_offsets(&offsetX, &offsetY);
            
            int replStartY = get_editor_start_y();
            
            // CRITICAL: Set CoreText to use TextGridManager's scaled cell sizes
            coretext_editor_set_cell_size(cellWidth, cellHeight);
            
            // Set viewport to render only the REPL area
            coretext_editor_set_viewport_with_layout(replStartY, REPL_MODE_LINES, offsetX, offsetY, cellWidth, cellHeight);
            
            // Don't call editor_cls() in REPL mode - preserve existing screen content
            // Only clear and prepare the specific REPL area
            editor_set_color(rgba(255, 255, 255, 255), rgba(0, 50, 100, 255)); // White on darker blue for REPL
            int editorWidth = get_editor_width();
            for (int y = replStartY; y < replStartY + REPL_MODE_LINES; y++) {
                for (int x = 0; x < editorWidth; x++) {
                    editor_print_at(x, y, " ");
                }
            }
            
            // Add visual border to distinguish REPL area (only if there's space above)
            int replBorderY = get_editor_start_y();
            if (replBorderY > 0) {
                editor_set_color(rgba(100, 200, 255, 255), rgba(0, 0, 0, 0)); // Light blue on transparent
                int editorWidth = get_editor_width();
                
                // Top border of REPL area
                for (int x = 0; x < editorWidth; x++) {
                    editor_print_at(x, replBorderY - 1, "─");
                }
                
                // Add corner markers
                editor_print_at(0, replBorderY - 1, "┌");
                editor_print_at(editorWidth - 1, replBorderY - 1, "┐");
                
                // Add "REPL" label in the border
                const char* repl_label = " REPL MODE ";
                int label_x = (editorWidth - strlen(repl_label)) / 2;
                if (label_x > 0 && label_x < editorWidth - (int)strlen(repl_label)) {
                    editor_print_at(label_x, replBorderY - 1, repl_label);
                }
            }
        } else {
            // Full screen mode - clear entire layer and take over screen
            editor_cls();
            editor_set_color(rgba(255, 255, 255, 255), rgba(0, 0, 150, 255)); // White on blue
            
            // Set viewport to render all rows in full screen mode using TextGridManager layout
            int gridWidth, gridHeight;
            float cellWidth, cellHeight, offsetX, offsetY;
            text_grid_get_dimensions(&gridWidth, &gridHeight);
            text_grid_get_cell_size(&cellWidth, &cellHeight);
            text_grid_get_offsets(&offsetX, &offsetY);
            
            std::cout << "Setting viewport to 0-" << gridHeight << " (full screen) with offsets (" 
                      << offsetX << "," << offsetY << ") and cell size " << cellWidth << "x" << cellHeight << std::endl;
            
            // CRITICAL: Set CoreText to use TextGridManager's scaled cell sizes
            coretext_editor_set_cell_size(cellWidth, cellHeight);
            
            coretext_editor_set_viewport_with_layout(0, gridHeight, offsetX, offsetY, cellWidth, cellHeight);
            mark_all_dirty();
            g_editor.last_cursor_x = -1;
            g_editor.last_cursor_y = -1;
        }
        
        // Update Layer 6 buffer with editor content
        editor_update_buffer();
        
        // Debug Layer 6 state
        return true;
    } else {
        // Editor already active - do nothing
        return true;
    }
}

extern "C" {

bool editor_is_active(void) {
    return g_editor.active;
}

void editor_deactivate(void) {
    if (g_editor.active) {
        g_editor.active = false;
        g_editor.show_status_bar = false;
        g_editor.ui_needs_refresh = false;
        
        // Unregister TextGridManager callback to prevent memory leaks
        text_grid_unregister_recalc_callback(editor_grid_recalc_callback);
    }
}

bool editor_save(const char* filename) {
    if (!filename || strlen(filename) == 0) {
        editor_set_status("ERROR: No filename specified");
        return false;
    }
    
    editor_ensure_scripts_folder();
    std::string filepath = editor_get_scripts_path() + "/" + std::string(filename);
    
    // Add .lua extension if not present
    if (filepath.length() < 4 || filepath.substr(filepath.length() - 4) != ".lua") {
        filepath += ".lua";
    }
    
    try {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            editor_set_status("ERROR: Could not create file");
            return false;
        }
        
        for (size_t i = 0; i < g_editor.lines.size(); i++) {
            file << g_editor.lines[i];
            if (i < g_editor.lines.size() - 1) {
                file << "\n";
            }
        }
        
        file.close();
        g_editor.modified = false;
        g_editor.current_filename = filename;
        
        char status[128];
        snprintf(status, sizeof(status), "SAVED: %s", filename);
        editor_set_status(status);
        
        return true;
        
    } catch (const std::exception& e) {
        editor_set_status("ERROR: Save failed");
        std::cerr << "TextEditor: Save error: " << e.what() << std::endl;
        return false;
    }
}

bool editor_load(const char* filename) {
    if (!filename || strlen(filename) == 0) {
        editor_set_status("ERROR: No filename specified");
        return false;
    }
    
    std::string filepath = editor_get_scripts_path() + "/" + std::string(filename);
    
    // Add .lua extension if not present
    if (filepath.length() < 4 || filepath.substr(filepath.length() - 4) != ".lua") {
        filepath += ".lua";
    }
    
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            editor_set_status("ERROR: File not found");
            return false;
        }
        
        g_editor.lines.clear();
        std::string line;
        
        while (std::getline(file, line)) {
            // Truncate lines that are too long
            int maxLen = get_max_line_length();
            if (line.length() > (size_t)maxLen) {
                line = line.substr(0, maxLen);
            }
            g_editor.lines.push_back(line);
        }
        
        if (g_editor.lines.empty()) {
            g_editor.lines.push_back("");
        }
        
        file.close();
        
        g_editor.cursor_x = 0;
        g_editor.cursor_y = 0;
        g_editor.scroll_offset = 0;
        g_editor.horizontal_offset = 0;
        g_editor.modified = false;
        g_editor.current_filename = filename;
        
        // Auto-format Lua files on load using LuaFormatter
        if (filepath.length() >= 4 && filepath.substr(filepath.length() - 4) == ".lua") {
            // Create temp file
            std::string temp_file = filepath + ".tmp";
            std::ofstream temp_out(temp_file);
            for (const auto& line : g_editor.lines) {
                temp_out << line << "\n";
            }
            temp_out.close();
            
            // Call lua-format binary from app bundle
            std::string cmd = get_lua_format_path() + " -i " + temp_file + " 2>/dev/null";
            int result = system(cmd.c_str());
            
            if (result == 0) {
                // Read formatted file back
                std::ifstream formatted_in(temp_file);
                g_editor.lines.clear();
                std::string line;
                while (std::getline(formatted_in, line)) {
                    g_editor.lines.push_back(line);
                }
                formatted_in.close();
                
                char status[128];
                snprintf(status, sizeof(status), "LOADED & FORMATTED: %s (%d lines)", filename, (int)g_editor.lines.size());
                editor_set_status(status);
            } else {
                char status[128];
                snprintf(status, sizeof(status), "LOADED: %s (%d lines) - Format failed", filename, (int)g_editor.lines.size());
                editor_set_status(status);
            }
            
            // Clean up temp file
            std::remove(temp_file.c_str());
        } else {
            char status[128];
            snprintf(status, sizeof(status), "LOADED: %s (%d lines)", filename, (int)g_editor.lines.size());
            editor_set_status(status);
        }
        
        editor_update_buffer();
        
        return true;
        
    } catch (const std::exception& e) {
        editor_set_status("ERROR: Load failed");
        std::cerr << "TextEditor: Load error: " << e.what() << std::endl;
        return false;
    }
}

bool editor_load_file(const char* filepath) {
    if (!filepath || strlen(filepath) == 0) {
        editor_set_status("ERROR: No filepath specified");
        return false;
    }

    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            editor_set_status("ERROR: File not found");
            return false;
        }
        
        // Clear current content
        g_editor.lines.clear();
        g_editor.cursor_x = 0;
        g_editor.cursor_y = 0;
        g_editor.scroll_offset = 0;
        g_editor.horizontal_offset = 0;

        // Read file line by line
        std::string line;
        int line_count = 0;
        while (std::getline(file, line)) {
            g_editor.lines.push_back(line);
            line_count++;
        }
        file.close();

        // Ensure at least one empty line
        if (g_editor.lines.empty()) {
            g_editor.lines.push_back("");
        }
        
        // Initialize dirty tracking
        g_editor.dirty_lines.resize(g_editor.lines.size(), true);
        g_editor.full_redraw_needed = true;
        g_editor.last_scroll_offset = 0;
        g_editor.last_horizontal_offset = 0;
        g_editor.last_cursor_x = -1;
        g_editor.last_cursor_y = -1;

        // Update editor state
        g_editor.modified = false;
        g_editor.current_filename = filepath;

        // Auto-format Lua files on load using LuaFormatter
        std::string fileStr(filepath);
        if (fileStr.length() >= 4 && fileStr.substr(fileStr.length() - 4) == ".lua") {
            // Create temp file
            std::string temp_file = std::string(filepath) + ".tmp";
            std::ofstream temp_out(temp_file);
            for (const auto& line : g_editor.lines) {
                temp_out << line << "\n";
            }
            temp_out.close();
            
            // Call lua-format binary from app bundle
            std::string cmd = get_lua_format_path() + " -i " + temp_file + " 2>/dev/null";
            int result = system(cmd.c_str());
            
            if (result == 0) {
                // Read formatted file back
                std::ifstream formatted_in(temp_file);
                g_editor.lines.clear();
                std::string line;
                while (std::getline(formatted_in, line)) {
                    g_editor.lines.push_back(line);
                }
                formatted_in.close();
                
                char status[256];
                snprintf(status, sizeof(status), "LOADED & FORMATTED: %s (%d lines)", filepath, (int)g_editor.lines.size());
                editor_set_status(status);
            } else {
                char status[256];
                snprintf(status, sizeof(status), "LOADED: %s (%d lines) - Format failed", filepath, (int)g_editor.lines.size());
                editor_set_status(status);
            }
            
            // Clean up temp file
            std::remove(temp_file.c_str());
        } else {
            char status[256];
            snprintf(status, sizeof(status), "LOADED: %s (%d lines)", filepath, (int)g_editor.lines.size());
            editor_set_status(status);
        }

        if (g_editor.active) {
            editor_update_buffer();
        }

        return true;

    } catch (const std::exception& e) {
        editor_set_status("ERROR: Load failed");
        std::cerr << "TextEditor: Load error: " << e.what() << std::endl;
        return false;
    }
}

void editor_set_content(const char* content, const char* filename) {
    if (!content) {
        std::cerr << "TextEditor: Cannot set null content" << std::endl;
        return;
    }
    
    // Clear current content
    g_editor.lines.clear();
    g_editor.cursor_x = 0;
    g_editor.cursor_y = 0;
    g_editor.scroll_offset = 0;
    g_editor.horizontal_offset = 0;
    
    // Parse content line by line
    std::string content_str(content);
    std::istringstream stream(content_str);
    std::string line;
    
    while (std::getline(stream, line)) {
        g_editor.lines.push_back(line);
    }
    
    // Ensure at least one empty line
    if (g_editor.lines.empty()) {
        g_editor.lines.push_back("");
    }
    
    // Initialize dirty tracking
    g_editor.dirty_lines.resize(g_editor.lines.size(), true);
    g_editor.full_redraw_needed = true;
    g_editor.modified = false;
    
    // Set filename if provided
    if (filename && filename[0] != '\0') {
        g_editor.current_filename = filename;
    } else {
        g_editor.current_filename.clear();
    }
    
    // Clear database metadata (this is a fresh set, not a DB load)
    g_editor.loaded_from_database = false;
    g_editor.db_script_name.clear();
    g_editor.db_version.clear();
    g_editor.db_author.clear();
    g_editor.db_tags.clear();
    
    // Mark all lines dirty for redraw
    mark_all_dirty();
}

void editor_set_database_metadata(const char* script_name, const char* version, const char* author) {
    g_editor.loaded_from_database = true;
    g_editor.db_script_name = script_name ? script_name : "";
    g_editor.db_version = version ? version : "";
    g_editor.db_author = author ? author : "";
    g_editor.db_tags.clear();
}

bool editor_load_content(const char* content, const char* filename) {
    if (!content) {
        editor_set_status("ERROR: No content provided");
        return false;
    }

    try {
        // Clear current content
        g_editor.lines.clear();
        g_editor.cursor_x = 0;
        g_editor.cursor_y = 0;
        g_editor.scroll_offset = 0;
        g_editor.horizontal_offset = 0;

        // Parse content line by line
        std::string content_str(content);
        std::istringstream stream(content_str);
        std::string line;
        int line_count = 0;
        
        while (std::getline(stream, line)) {
            g_editor.lines.push_back(line);
            line_count++;
        }

        // Ensure at least one empty line
        if (g_editor.lines.empty()) {
            g_editor.lines.push_back("");
        }
        
        // Initialize dirty tracking
        g_editor.dirty_lines.resize(g_editor.lines.size(), true);
        g_editor.full_redraw_needed = true;
        g_editor.last_scroll_offset = 0;
        g_editor.last_horizontal_offset = 0;
        g_editor.last_cursor_x = -1;
        g_editor.last_cursor_y = -1;

        // Update editor state
        g_editor.modified = false;
        g_editor.current_filename = filename ? filename : "";

        // Auto-format if it's Lua content (detect by filename or content)
        bool isLuaContent = false;
        if (filename) {
            std::string filenameStr(filename);
            isLuaContent = (filenameStr.length() >= 4 && filenameStr.substr(filenameStr.length() - 4) == ".lua");
        }
        // Also check if content looks like Lua (contains common Lua keywords)
        if (!isLuaContent && !g_editor.lines.empty()) {
            std::string firstLine = g_editor.lines[0];
            isLuaContent = (firstLine.find("function") != std::string::npos ||
                           firstLine.find("local") != std::string::npos ||
                           firstLine.find("start_of_script") != std::string::npos);
        }
        
        if (isLuaContent) {
            // Auto-format Lua files on load (for content-based detection)
            // Create temp file
            std::string temp_file = "/tmp/superterminal_format_tmp.lua";
            std::ofstream temp_out(temp_file);
            for (const auto& line : g_editor.lines) {
                temp_out << line << "\n";
            }
            temp_out.close();
        
            // Call lua-format binary from app bundle
            std::string cmd = get_lua_format_path() + " -i " + temp_file + " 2>/dev/null";
            int result = system(cmd.c_str());
        
            if (result == 0) {
                // Read formatted file back
                std::ifstream formatted_in(temp_file);
                g_editor.lines.clear();
                std::string line;
                while (std::getline(formatted_in, line)) {
                    g_editor.lines.push_back(line);
                }
                formatted_in.close();
            
                char status[256];
                snprintf(status, sizeof(status), "LOADED & FORMATTED: %s (%d lines)", filename ? filename : "content", (int)g_editor.lines.size());
                editor_set_status(status);
            } else {
                char status[256];
                snprintf(status, sizeof(status), "LOADED: %s (%d lines) - Format failed", filename ? filename : "content", (int)g_editor.lines.size());
                editor_set_status(status);
            }
        
            // Clean up temp file
            std::remove(temp_file.c_str());
        } else {
            char status[256];
            snprintf(status, sizeof(status), "LOADED: %s (%d lines)", filename ? filename : "content", (int)g_editor.lines.size());
            editor_set_status(status);
        }

        std::cout << "TextEditor: Editor state updated - lines in buffer: " << g_editor.lines.size() << std::endl;
        std::cout << "TextEditor: Editor active: " << (g_editor.active ? "YES" : "NO") << std::endl;

        if (g_editor.active) {
            std::cout << "TextEditor: Updating buffer since editor is active" << std::endl;
            editor_update_buffer();
        }

        std::cout << "TextEditor: Successfully loaded content with " << g_editor.lines.size() << " lines" << std::endl;
        return true;

    } catch (const std::exception& e) {
        editor_set_status("ERROR: Content load failed");
        std::cerr << "TextEditor: Content load error: " << e.what() << std::endl;
        return false;
    }
}

bool editor_run(void) {
    if (g_editor.lines.empty()) {
        editor_set_status("ERROR: No code to run");
        return false;
    }
    
    // Combine all lines into a single string
    std::string code;
    for (size_t i = 0; i < g_editor.lines.size(); i++) {
        code += g_editor.lines[i];
        if (i < g_editor.lines.size() - 1) {
            code += "\n";
        }
    }
    
    // Simple approach: execute if no script running
    editor_set_status("Executing editor content...");
    std::cout << "TextEditor: F2 - executing editor content" << std::endl;
    std::cout << "TextEditor: Executing editor script with " << code.length() << " bytes" << std::endl;
    
    bool success = lua_gcd_exec(code.c_str(), "editor_script");
    
    if (success) {
        std::cout << "TextEditor: Editor script queued successfully" << std::endl;
    } else {
        const char* error = lua_gcd_get_last_error();
        editor_set_status("Editor script execution failed");
        std::cout << "TextEditor: Editor script execution failed: " << (error ? error : "Unknown error") << std::endl;
    }
    
    return success;
}

void editor_clear(void) {
    g_editor.lines.clear();
    g_editor.lines.push_back("");
    g_editor.dirty_lines.clear();
    g_editor.dirty_lines.resize(1, true);
    g_editor.full_redraw_needed = true;
    g_editor.cursor_x = 0;
    g_editor.cursor_y = 0;
    g_editor.scroll_offset = 0;
    g_editor.horizontal_offset = 0;
    g_editor.modified = false;
    g_editor.current_filename.clear();
    
    // Clear database metadata
    g_editor.loaded_from_database = false;
    g_editor.db_script_name.clear();
    g_editor.db_version.clear();
    g_editor.db_author.clear();
    g_editor.db_tags.clear();
    
    // Clear undo/redo stacks and clipboard
    g_editor.undo_stack.clear();
    g_editor.redo_stack.clear();
    g_editor.clipboard.clear();
    g_editor.search_term.clear();
    g_editor.search_active = false;
    
    if (g_editor.active) {
        editor_set_status("Editor cleared");
    }
}

// Force editor to clear display and redraw (e.g., on text mode change)
void editor_force_redraw(void) {
    if (!g_editor.active) {
        return;
    }
    
    std::cout << "TextEditor: Force redraw requested (clearing and redisplaying)" << std::endl;
    
    // Clear the editor display layer
    coretext_editor_clear();
    
    // Mark all lines as dirty to force complete redraw
    g_editor.full_redraw_needed = true;
    mark_all_dirty();
    
    // Update dimensions to current grid size (in case they changed)
    text_grid_get_dimensions(&g_editor.last_known_columns, &g_editor.last_known_rows);
    
    // Immediately redraw the editor buffer with current content
    editor_update_buffer();
    
    std::cout << "TextEditor: Force redraw completed" << std::endl;
}

char* editor_get_content(void) {
    std::string content;
    for (size_t i = 0; i < g_editor.lines.size(); i++) {
        content += g_editor.lines[i];
        if (i < g_editor.lines.size() - 1) {
            content += "\n";
        }
    }
    
    char* result = (char*)malloc(content.length() + 1);
    if (result) {
        strcpy(result, content.c_str());
    }
    return result;
}

// Editor update function - call this regularly from main loop
void editor_update(void) {
    if (!g_editor.active) return;
    
    // Don't update during application termination to avoid crashes
    extern bool superterminal_is_terminating(void);
    if (superterminal_is_terminating()) {
        return;
    }
    
    // Check if text grid dimensions have changed (window resize or mode switch)
    int current_columns, current_rows;
    text_grid_get_dimensions(&current_columns, &current_rows);
    
    if (current_columns != g_editor.last_known_columns || 
        current_rows != g_editor.last_known_rows) {
        std::cout << "TextEditor: Grid dimensions changed from " 
                  << g_editor.last_known_columns << "x" << g_editor.last_known_rows
                  << " to " << current_columns << "x" << current_rows << std::endl;
        
        // Update tracked dimensions
        g_editor.last_known_columns = current_columns;
        g_editor.last_known_rows = current_rows;
        
        // Only manage viewport if editor is actually active (not just REPL)
        // REPL manages its own viewport when active
        float cellWidth, cellHeight, offsetX, offsetY;
        text_grid_get_cell_size(&cellWidth, &cellHeight);
        text_grid_get_offsets(&offsetX, &offsetY);
        
        // CRITICAL: Set CoreText to use TextGridManager's scaled cell sizes
        coretext_editor_set_cell_size(cellWidth, cellHeight);
        
        // Only set viewport if we're in full-screen editor mode
        // Don't interfere with REPL's viewport management
        coretext_editor_set_viewport_with_layout(0, current_rows, offsetX, offsetY, cellWidth, cellHeight);
        std::cout << "TextEditor: Reset viewport to 0-" << current_rows 
                  << " with layout offsets (" << offsetX << "," << offsetY << ") and cell size " 
                  << cellWidth << "x" << cellHeight << std::endl;
        
        // Adjust scroll offset to ensure cursor is visible in new viewport
        editor_scroll_if_needed();
        
        // Force buffer redraw to adapt to new dimensions
        editor_update_buffer();
        g_ui_needs_redraw = true;
    }
    
    // Update status timer
    if (g_editor.status_timer > 0) {
        g_editor.status_timer--;
        if (g_editor.status_timer <= 0) {
            g_editor.show_status = false;
        }
    }
    
    // Update cursor blink (30 fps = blink every 30 frames = ~0.5 seconds)
    g_editor.cursor_blink_timer++;
    if (g_editor.cursor_blink_timer >= 30) {
        g_editor.cursor_visible = !g_editor.cursor_visible;
        g_editor.cursor_blink_timer = 0;
        
        // Trigger overlay redraw for cursor blink
        g_ui_needs_redraw = true;
        
        // Update buffer to show cursor blink
        editor_update_buffer();
    }
    
    // UI updates are now handled by global UI system every 60 frames
}

void editor_key_pressed(int key, int keycode, bool shift, bool ctrl, bool alt, bool cmd) {
    if (!g_editor.active) return; // Only process keys when editor is active
    
    // Reset cursor to visible when any key is pressed
    g_editor.cursor_visible = true;
    g_editor.cursor_blink_timer = 0;
    
    // Handle Cmd key combinations (macOS) - for copy/cut/paste with system clipboard
    if (cmd) {
        switch (key) {
            case 'c':
            case 'C':
                editor_copy_line();
                if (g_editor.active) {
                    editor_update_buffer();
                }
                return;
            case 'x':
            case 'X':
                editor_cut_line();
                if (g_editor.active) {
                    editor_update_buffer();
                }
                return;
            case 'v':
            case 'V':
                // Clear selection before pasting
                if (g_editor.has_selection) {
                    editor_delete_selection();
                }
                editor_paste_line();
                if (g_editor.active) {
                    editor_update_buffer();
                }
                return;
            case 'a':
            case 'A':
                // Select all - future enhancement
                return;
            default:
                // Fall through to handle other cmd combinations
                break;
        }
    }
    
    // Handle control key combinations
    if (ctrl) {
        switch (key) {
            case 'k':
            case 'K':
                editor_delete_line();
                break;
            case 'd':
            case 'D':
                editor_duplicate_line();
                break;
            case 'z':
            case 'Z':
                editor_undo();
                break;
            case 'y':
            case 'Y':
                editor_redo();
                break;
            case 'f':
            case 'F':
                editor_find();
                break;
            case 'l':
            case 'L':
                editor_goto_line();
                break;
            default:
                // Fall through to handle other ctrl combinations
                break;
        }
        // Don't process other keys when ctrl is pressed (except fall-through cases)
        if (key == 'k' || key == 'K' || key == 'd' || key == 'D' || 
            key == 'z' || key == 'Z' || key == 'y' || key == 'Y' ||
            key == 'f' || key == 'F' || key == 'l' || key == 'L') {
            if (g_editor.active) {
                editor_update_buffer();
            }
            return;
        }
    }
    
    // Handle function keys and special keys by keycode
    switch (keycode) {
        case 0x78: // F2 - Execute
            editor_run();
            break;

        case 0x64: // F8 - Save
            {
                if (g_editor.current_filename.empty()) {
                    editor_set_status("Enter filename to save: ");
                    editor_update_buffer();
                    
                    char* filename = accept_at(26, get_editor_start_y() + get_editor_height() - 1);
                    if (filename && strlen(filename) > 0) {
                        editor_save(filename);
                    } else {
                        editor_set_status("Save cancelled");
                    }
                    if (filename) free(filename);
                } else {
                    editor_save(g_editor.current_filename.c_str());
                }
            }
            break;

        case 0x61: // F6 - Load
            {
                editor_set_status("Enter filename to load: ");
                editor_update_buffer();
                
                char* filename = accept_at(26, get_editor_start_y() + get_editor_height() - 1);
                if (filename && strlen(filename) > 0) {
                    editor_load(filename);
                } else {
                    editor_set_status("Load cancelled");
                }
                if (filename) free(filename);
            }
            break;
            
        case 0x62: // F7 - Save As
            {
                editor_set_status("Enter filename to save: ");
                editor_update_buffer();
                
                char* filename = accept_at(26, get_editor_start_y() + get_editor_height() - 1);
                if (filename && strlen(filename) > 0) {
                    editor_save(filename);
                } else {
                    editor_set_status("Save cancelled");
                }
                if (filename) free(filename);
            }
            break;
            
        case 0x65: // F9 - Format Lua Code
            {
                // Check if we have content that looks like Lua
                bool isLuaContent = false;
                if (!g_editor.current_filename.empty()) {
                    std::string filename = g_editor.current_filename;
                    isLuaContent = (filename.length() >= 4 && filename.substr(filename.length() - 4) == ".lua");
                }
                // Also check if content looks like Lua
                if (!isLuaContent && !g_editor.lines.empty()) {
                    for (const auto& line : g_editor.lines) {
                        if (line.find("function") != std::string::npos ||
                            line.find("local") != std::string::npos ||
                            line.find("start_of_script") != std::string::npos ||
                            line.find("end_of_script") != std::string::npos) {
                            isLuaContent = true;
                            break;
                        }
                    }
                }
                
                if (isLuaContent) {
                    size_t originalLines = g_editor.lines.size();
                    
                    // Create temp file
                    std::string temp_file = "/tmp/superterminal_format_tmp.lua";
                    std::ofstream temp_out(temp_file);
                    for (const auto& line : g_editor.lines) {
                        temp_out << line << "\n";
                    }
                    temp_out.close();
                    
                    // Call lua-format binary from app bundle
                    std::string cmd = get_lua_format_path() + " -i " + temp_file + " 2>/dev/null";
                    int result = system(cmd.c_str());
                    
                    if (result == 0) {
                        // Read formatted file back
                        std::ifstream formatted_in(temp_file);
                        g_editor.lines.clear();
                        std::string line;
                        while (std::getline(formatted_in, line)) {
                            g_editor.lines.push_back(line);
                        }
                        formatted_in.close();
                        
                        g_editor.modified = true;
                        editor_set_status("Lua code formatted successfully");
                        std::cout << "TextEditor: Applied Lua formatting (" << originalLines << " lines)" << std::endl;
                    } else {
                        editor_set_status("ERROR: Lua formatting failed");
                        std::cerr << "TextEditor: Lua formatting error" << std::endl;
                    }
                    
                    // Clean up temp file
                    std::remove(temp_file.c_str());
                } else {
                    editor_set_status("Not a Lua file - formatting skipped");
                }
            }
            break;
            
        case 0x63: // F3 - Exit editor
            layer_set_enabled(6, false);
            editor_deactivate(); // Properly deactivate editor and hide overlay
            std::cout << "TextEditor: F3 pressed - editor deactivated" << std::endl;
            break;
            
        case 0x7A: // F1 - Exit anyway (even with unsaved changes)
            layer_set_enabled(6, false);
            editor_deactivate(); // Properly deactivate editor and hide overlay
            std::cout << "TextEditor: F1 pressed - editor deactivated (unsaved changes discarded)" << std::endl;
            break;
            
        case 0x6D: // F10 - Run (alternative to F8)
            editor_run();
            break;
            
        case 0x35: // ESC
            if (g_editor.modified) {
                editor_set_status("Unsaved changes! F7 to save, F1 to exit anyway");
            } else {
                layer_set_enabled(6, false);
                editor_deactivate(); // Properly deactivate editor and hide overlay
            }
            break;
            
        case 0x33: // Backspace/Delete
            editor_save_undo_state("Backspace");
            editor_handle_backspace();
            break;
            
        case 0x75: // Forward Delete (Fn+Delete on Mac laptop)
            editor_save_undo_state("Forward Delete");
            editor_handle_forward_delete();
            break;
            
        case 0x24: // Return/Enter
            editor_new_line();
            break;
            
        case 0x7B: // Left arrow
            std::cout << "[ARROW KEY] LEFT pressed - moving cursor left" << std::endl;
            editor_clear_selection(); // Clear selection on arrow key
            editor_move_cursor(-1, 0);
            std::cout << "[ARROW KEY] After left: cursor_x=" << g_editor.cursor_x << " cursor_y=" << g_editor.cursor_y << std::endl;
            break;
            
        case 0x7C: // Right arrow
            std::cout << "[ARROW KEY] RIGHT pressed - moving cursor right" << std::endl;
            editor_clear_selection(); // Clear selection on arrow key
            editor_move_cursor(1, 0);
            std::cout << "[ARROW KEY] After right: cursor_x=" << g_editor.cursor_x << " cursor_y=" << g_editor.cursor_y << std::endl;
            break;
            
        case 0x7E: // Up arrow
            editor_clear_selection(); // Clear selection on arrow key
            editor_move_cursor(0, -1);
            break;
            
        case 0x7D: // Down arrow
            editor_clear_selection(); // Clear selection on arrow key
            editor_move_cursor(0, 1);
            break;
            
        case 0x74: // Page Up
            editor_page_up();
            break;
            
        case 0x79: // Page Down
            editor_page_down();
            break;
            
        default:
            // Regular character input
            if (key >= 32 && key <= 126) { // Printable ASCII
                editor_insert_char((char)key);
            }
            break;
    }
    
    // Update display after key processing
    if (g_editor.active) {
        editor_update_buffer();
    }
}

// Render editor content to the editor layer
// Update Layer 6 buffer with editor content - this is the ONLY rendering function
static void editor_update_buffer() {
    if (!g_editor.active) {
        return;
    }
    
    // Safety check: Don't render during application termination
    extern bool superterminal_is_terminating(void);
    if (superterminal_is_terminating()) {
        return;
    }
    
    try {
        // Get direct grid access for fast rendering
        struct TextCell* grid = coretext_get_grid_buffer(6);
        if (!grid) {
            std::cout << "TextEditor: ERROR - Failed to get grid pointer!" << std::endl;
            return;
        }
        
        int gridWidth = coretext_get_grid_width();
        int gridHeight = coretext_get_grid_height();
        
        // Safety check: validate grid dimensions during shutdown
        if (gridWidth <= 0 || gridHeight <= 0 || gridWidth > 1000 || gridHeight > 1000) {
            std::cout << "TextEditor: WARNING - Invalid grid dimensions (" << gridWidth << "x" << gridHeight << "), skipping render" << std::endl;
            return;
        }
        
        int editorWidth = get_editor_width();
        int totalRows = gridHeight;
        
        // Define colors
        uint32_t white = rgba(255, 255, 255, 255);
        uint32_t blue = rgba(0, 0, 150, 255);
        uint32_t lightBlue = rgba(176, 216, 230, 255);
        uint32_t black = rgba(0, 0, 0, 255);
        uint32_t darkBlue = rgba(0, 100, 200, 255);
        
        // Editor content area - use VIEWPORT MODEL (render FIRST, before status bars)
        int content_start_y = get_editor_start_y();
        int content_lines = get_content_lines();
        
        std::cout << "[VIEWPORT] Grid: " << gridWidth << "x" << gridHeight << " totalRows=" << totalRows << std::endl;
        std::cout << "[VIEWPORT] Content: start_y=" << content_start_y << " lines=" << content_lines 
                  << " end_y=" << (content_start_y + content_lines - 1) << std::endl;
        std::cout << "[VIEWPORT] Bottom status bar at row: " << (totalRows - 1) << std::endl;
        std::cout << "[VIEWPORT] Rendering content: scroll_y=" << g_editor.scroll_offset 
                  << " scroll_x=" << g_editor.horizontal_offset 
                  << " viewport=" << editorWidth << "x" << content_lines << std::endl;
        
        // Use fast viewport rendering - maps document to grid window
        render_viewport_to_grid(grid, gridWidth,
                               0, content_start_y,  // viewport position in grid
                               editorWidth, content_lines,  // viewport size
                               g_editor.horizontal_offset, g_editor.scroll_offset,  // scroll offsets
                               white, blue);  // colors
        
        // Update tracking variables
        g_editor.last_scroll_offset = g_editor.scroll_offset;
        g_editor.last_horizontal_offset = g_editor.horizontal_offset;
        g_editor.full_redraw_needed = false;
        
        // Clear dirty flags
        if (g_editor.dirty_lines.size() == g_editor.lines.size()) {
            std::fill(g_editor.dirty_lines.begin(), g_editor.dirty_lines.end(), false);
        }
        
        // =======================================================================
        // DRAW STATUS BARS LAST - They overlay the content
        // =======================================================================
        
        // Row 1: Blank light blue line (using direct grid access)
        clear_grid_region(grid, gridWidth, 0, 1, editorWidth, 1, lightBlue, lightBlue);
        
        // Row 2: Top status bar - filename and mode
        clear_grid_region(grid, gridWidth, 0, 2, editorWidth, 1, black, lightBlue);
        
        std::string modeText = "[EDIT] ";
        std::string filenameText = g_editor.current_filename.empty() ? "untitled" : g_editor.current_filename;
        std::string modifiedMarker = g_editor.modified ? "*" : " ";
        
        std::string topLeftStatus = modeText + modifiedMarker + filenameText;
        std::string topRightStatus = std::string(text_mode_get_name());
        
        // Truncate if too long
        if (topLeftStatus.length() + topRightStatus.length() + 2 > (size_t)editorWidth) {
            int maxLeft = editorWidth - topRightStatus.length() - 2;
            if (maxLeft > 10) {
                topLeftStatus = topLeftStatus.substr(0, maxLeft);
            }
        }
        
        write_text_to_grid(grid, gridWidth, 0, 2, topLeftStatus.c_str(), black, lightBlue);
        int topRightPos = editorWidth - topRightStatus.length();
        if (topRightPos > (int)topLeftStatus.length()) {
            write_text_to_grid(grid, gridWidth, topRightPos, 2, topRightStatus.c_str(), black, lightBlue);
        }
        
        // Row 3: Status message line
        // Row 3: Ruler with 5/10 char markers using sextants
        clear_grid_region(grid, gridWidth, 0, 3, editorWidth, 1, black, lightBlue);
        draw_ruler(grid, gridWidth, 3, editorWidth, black, lightBlue);
        
        // Row 4: Status message line (or blank blue line if no message)
        if (g_editor.show_status && !g_editor.status_message.empty()) {
            clear_grid_region(grid, gridWidth, 0, 4, editorWidth, 1, white, darkBlue);
            std::string statusMsg = g_editor.status_message;
            if (statusMsg.length() > (size_t)editorWidth) {
                statusMsg = statusMsg.substr(0, editorWidth);
            }
            write_text_to_grid(grid, gridWidth, 0, 4, statusMsg.c_str(), white, darkBlue);
        } else {
            clear_grid_region(grid, gridWidth, 0, 4, editorWidth, 1, white, blue);
        }
        
        // Bottom status bar - place at end of content area, not at absolute bottom row
        // This ensures it's within the viewport that gets rendered
        int bottomRow = content_start_y + content_lines;
        if (bottomRow >= totalRows) {
            bottomRow = totalRows - 1;
        }
        clear_grid_region(grid, gridWidth, 0, bottomRow, editorWidth, 1, black, lightBlue);
        
        std::string lineInfo = "Line " + std::to_string(g_editor.cursor_y + 1) + 
                               "/" + std::to_string(g_editor.lines.size());
        std::string colInfo = "Col " + std::to_string(g_editor.cursor_x + 1);
        
        write_text_to_grid(grid, gridWidth, 0, bottomRow, lineInfo.c_str(), black, lightBlue);
        int bottomRightPos = editorWidth - colInfo.length();
        if (bottomRightPos > (int)lineInfo.length()) {
            write_text_to_grid(grid, gridWidth, bottomRightPos, bottomRow, colInfo.c_str(), black, lightBlue);
        }
        
        // Update visual cursor position (rendered by Metal shader, doesn't modify grid)
        if (g_editor.cursor_visible && g_editor.cursor_y >= g_editor.scroll_offset) {
            int screen_y = (g_editor.cursor_y - g_editor.scroll_offset) + content_start_y;
            
            // Calculate screen cursor X position accounting for horizontal scroll
            int screen_x = g_editor.cursor_x - g_editor.horizontal_offset;
            
            // Prevent cursor from appearing on status bar rows (1, 2, 3, or last row)
            int bottomRow = totalRows - 1;
            bool isOnStatusBar = (screen_y == 1 || screen_y == 2 || screen_y == 3 || screen_y == bottomRow);
            
            // Cursor is only visible if horizontally within the display area
            bool cursor_in_horizontal_bounds = (screen_x >= 0 && screen_x < editorWidth);
            
            if (!isOnStatusBar && screen_y >= content_start_y && screen_y < content_start_y + content_lines && cursor_in_horizontal_bounds) {
                // Set visual cursor position (shader will render it without modifying textGrid)
                coretext_editor_set_cursor_position(screen_x, screen_y, true);
                
                // Update blink phase based on timer
                float blinkPhase = (g_editor.cursor_blink_timer % 60) / 60.0f;
                coretext_editor_update_cursor_blink(blinkPhase);
            } else {
                // Hide cursor if on status bar
                coretext_editor_set_cursor_position(0, 0, false);
            }
        } else {
            // Hide cursor when not visible
            coretext_editor_set_cursor_position(0, 0, false);
        }
        

        
        // std::cout << "TextEditor: editor_update_buffer() completed successfully" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "TextEditor: CRASH in editor_update_buffer(): " << e.what() << std::endl;
    } catch (...) {
        std::cout << "TextEditor: UNKNOWN CRASH in editor_update_buffer()" << std::endl;
    }
}

// TextGridManager layout change callback
static void editor_grid_recalc_callback(TextGridLayout layout, void* userData) {
    if (!g_editor.active) return;
    
    std::cout << "TextEditor: Grid layout changed - updating viewport to " 
              << layout.gridWidth << "x" << layout.gridHeight << " with offsets (" 
              << layout.offsetX << "," << layout.offsetY << ")" << std::endl;
    
    // Update cached dimensions
    g_editor.last_known_columns = layout.gridWidth;
    g_editor.last_known_rows = layout.gridHeight;
    
    // Only update viewport if editor is active
    // REPL manages its own viewport
    if (g_editor.active) {
        // CRITICAL: Set CoreText to use TextGridManager's scaled cell sizes
        coretext_editor_set_cell_size(layout.cellWidth, layout.cellHeight);
        
        // Set full-screen editor viewport
        coretext_editor_set_viewport_with_layout(0, layout.gridHeight, layout.offsetX, layout.offsetY, 
                                                 layout.cellWidth, layout.cellHeight);
    }
    
    // Force buffer update to reflect new layout
    editor_update_buffer();
}

int editor_get_cursor_line(void) {
    return g_editor.cursor_y + 1; // Convert to 1-based line numbering
}

int editor_get_cursor_column(void) {
    return g_editor.cursor_x + 1; // Convert to 1-based column numbering
}

} // extern "C"

bool editor_is_modified(void) {
    return g_editor.modified;
}

const char* editor_get_current_filename(void) {
    if (g_editor.current_filename.empty()) {
        return nullptr;
    }
    return g_editor.current_filename.c_str();
}

void editor_new_file(void) {
    g_editor.lines.clear();
    g_editor.lines.push_back("");
    g_editor.dirty_lines.clear();
    g_editor.dirty_lines.resize(1, true);
    g_editor.full_redraw_needed = true;
    g_editor.cursor_x = 0;
    g_editor.cursor_y = 0;
    g_editor.scroll_offset = 0;
    g_editor.horizontal_offset = 0;
    g_editor.modified = false;
    g_editor.current_filename.clear();
    
    // Clear undo/redo stacks
    g_editor.undo_stack.clear();
    g_editor.redo_stack.clear();
    
    if (g_editor.active) {
        editor_set_status("New file created");
    }
}

void editor_execute_current(void) {
    editor_run();
}

const char* editor_get_filename(void) {
    if (g_editor.current_filename.empty()) {
        return "untitled.lua";
    }
    return g_editor.current_filename.c_str();
}

} // extern "C"

// Internal implementation



static void editor_insert_char(char c) {
    // Delete selection first if one exists
    if (g_editor.has_selection) {
        editor_delete_selection();
    }
    
    // Ensure lines vector is large enough
    if (g_editor.cursor_y >= (int)g_editor.lines.size()) {
        g_editor.lines.resize(g_editor.cursor_y + 1);
        g_editor.dirty_lines.resize(g_editor.cursor_y + 1, true);
    }
    
    std::string& line = g_editor.lines[g_editor.cursor_y];
    int maxLen = get_max_line_length();
    
    if (g_editor.cursor_x <= (int)line.length() && line.length() < (size_t)maxLen) {
        line.insert(g_editor.cursor_x, 1, c);
        g_editor.cursor_x++;
        g_editor.modified = true;
        mark_line_dirty(g_editor.cursor_y);
        g_ui_needs_redraw = true;  // Trigger UI redraw for modified status
    }
}

// Handle backspace
static void editor_handle_backspace() {
    // Delete selection if one exists
    if (g_editor.has_selection) {
        editor_delete_selection();
        return;
    }
    
    if (g_editor.cursor_x > 0) {
        std::string& line = g_editor.lines[g_editor.cursor_y];
        line.erase(g_editor.cursor_x - 1, 1);
        g_editor.cursor_x--;
        g_editor.modified = true;
        mark_line_dirty(g_editor.cursor_y);
        g_ui_needs_redraw = true;  // Trigger UI redraw for modified status
    } else if (g_editor.cursor_x == 0 && g_editor.cursor_y > 0) {
        // Join with previous line
        std::string current_line = g_editor.lines[g_editor.cursor_y];
        g_editor.lines.erase(g_editor.lines.begin() + g_editor.cursor_y);
        g_editor.dirty_lines.erase(g_editor.dirty_lines.begin() + g_editor.cursor_y);
        g_editor.cursor_y--;
        g_editor.cursor_x = g_editor.lines[g_editor.cursor_y].length();
        g_editor.lines[g_editor.cursor_y] += current_line;
        g_editor.modified = true;
        mark_line_dirty(g_editor.cursor_y);
        mark_all_dirty(); // Line numbers changed
        g_ui_needs_redraw = true;  // Trigger UI redraw for modified status
    }
}

static void editor_handle_forward_delete() {
    // Delete selection if one exists
    if (g_editor.has_selection) {
        editor_delete_selection();
        return;
    }
    
    if (g_editor.cursor_y >= (int)g_editor.lines.size()) return;
    
    std::string& line = g_editor.lines[g_editor.cursor_y];
    if (g_editor.cursor_x < (int)line.length()) {
        // Delete character at cursor position
        line.erase(g_editor.cursor_x, 1);
        g_editor.modified = true;
        mark_line_dirty(g_editor.cursor_y);
        g_ui_needs_redraw = true;  // Trigger UI redraw for modified status
    } else if (g_editor.cursor_x == (int)line.length() && g_editor.cursor_y < (int)g_editor.lines.size() - 1) {
        // At end of line, join with next line
        std::string next_line = g_editor.lines[g_editor.cursor_y + 1];
        g_editor.lines[g_editor.cursor_y] += next_line;
        g_editor.lines.erase(g_editor.lines.begin() + g_editor.cursor_y + 1);
        g_editor.dirty_lines.erase(g_editor.dirty_lines.begin() + g_editor.cursor_y + 1);
        g_editor.modified = true;
        mark_line_dirty(g_editor.cursor_y);
        mark_all_dirty(); // Line numbers changed
        g_ui_needs_redraw = true;  // Trigger UI redraw for modified status
    }
}

static void editor_new_line() {
    // Delete selection first if one exists
    if (g_editor.has_selection) {
        editor_delete_selection();
    }
    
    editor_save_undo_state("New line");
    
    // Safety check: ensure dirty_lines is synchronized with lines
    if (g_editor.dirty_lines.size() != g_editor.lines.size()) {
        g_editor.dirty_lines.resize(g_editor.lines.size(), true);
    }
    
    std::string& current_line = g_editor.lines[g_editor.cursor_y];
    std::string new_line = current_line.substr(g_editor.cursor_x);
    current_line = current_line.substr(0, g_editor.cursor_x);
    
    g_editor.lines.insert(g_editor.lines.begin() + g_editor.cursor_y + 1, new_line);
    g_editor.dirty_lines.insert(g_editor.dirty_lines.begin() + g_editor.cursor_y + 1, true);
    mark_line_dirty(g_editor.cursor_y);
    mark_line_dirty(g_editor.cursor_y + 1);
    mark_all_dirty(); // Line numbers changed
    g_editor.cursor_y++;
    g_editor.cursor_x = 0;
    g_editor.modified = true;
    g_ui_needs_redraw = true;  // Trigger UI redraw for modified status
    
    editor_scroll_if_needed();
}

// Delete the current line (Ctrl+K)
static void editor_delete_line() {
    if (g_editor.lines.empty()) return;
    
    editor_save_undo_state("Delete line");
    
    // If there's only one line, clear it instead of deleting
    if (g_editor.lines.size() == 1) {
        g_editor.lines[0].clear();
        g_editor.cursor_x = 0;
    } else {
        // Delete the current line
        g_editor.lines.erase(g_editor.lines.begin() + g_editor.cursor_y);
        g_editor.dirty_lines.erase(g_editor.dirty_lines.begin() + g_editor.cursor_y);
        
        // Adjust cursor position
        if (g_editor.cursor_y >= (int)g_editor.lines.size()) {
            g_editor.cursor_y = (int)g_editor.lines.size() - 1;
        }
        if (g_editor.cursor_y < 0) {
            g_editor.cursor_y = 0;
        }
        
        // Ensure cursor_x is within bounds of the current line
        if (g_editor.cursor_y < (int)g_editor.lines.size()) {
            if (g_editor.cursor_x > (int)g_editor.lines[g_editor.cursor_y].length()) {
                g_editor.cursor_x = (int)g_editor.lines[g_editor.cursor_y].length();
            }
        }
    }
    
    mark_all_dirty(); // Line numbers changed
    g_editor.modified = true;
    g_ui_needs_redraw = true;
    editor_scroll_if_needed();
}

// Duplicate the current line below (Ctrl+D)
static void editor_duplicate_line() {
    // Ensure we have at least one line
    if (g_editor.lines.empty()) {
        g_editor.lines.push_back("");
        g_editor.dirty_lines.resize(1, true);
        g_editor.cursor_y = 0;
        g_editor.cursor_x = 0;
        return;
    }
    
    editor_save_undo_state("Duplicate line");
    
    // Ensure cursor_y is within bounds
    if (g_editor.cursor_y >= (int)g_editor.lines.size()) {
        g_editor.cursor_y = (int)g_editor.lines.size() - 1;
    }
    
    // Get the current line
    std::string current_line = g_editor.lines[g_editor.cursor_y];
    
    // Insert a copy of the current line below it
    g_editor.lines.insert(g_editor.lines.begin() + g_editor.cursor_y + 1, current_line);
    g_editor.dirty_lines.insert(g_editor.dirty_lines.begin() + g_editor.cursor_y + 1, true);
    
    // Move cursor to the duplicated line
    g_editor.cursor_y++;
    // Keep cursor_x position (it should be valid since we copied the line)
    
    g_editor.modified = true;
    g_ui_needs_redraw = true;
    editor_scroll_if_needed();
}

static void editor_move_cursor(int dx, int dy) {
    if (dy != 0) {
        int old_y = g_editor.cursor_y;
        g_editor.cursor_y = std::max(0, std::min((int)g_editor.lines.size() - 1, g_editor.cursor_y + dy));
        
        std::cout << "CURSOR DEBUG: dy=" << dy << " old_y=" << old_y << " new_y=" << g_editor.cursor_y << std::endl;
        
        // Adjust cursor_x to be within the new line
        if (g_editor.cursor_y < (int)g_editor.lines.size()) {
            int line_length = g_editor.lines[g_editor.cursor_y].length();
            std::cout << "CURSOR DEBUG: Line " << g_editor.cursor_y << " length=" << line_length << std::endl;
            g_editor.cursor_x = std::min(g_editor.cursor_x, line_length);
        }
        
        editor_scroll_if_needed();
        g_ui_needs_redraw = true;  // Trigger UI redraw for position change
    }
    
    if (dx != 0 && g_editor.cursor_y < (int)g_editor.lines.size()) {
        int max_x = g_editor.lines[g_editor.cursor_y].length();
        int old_x = g_editor.cursor_x;
        g_editor.cursor_x = std::max(0, std::min(max_x, g_editor.cursor_x + dx));
        
        std::cout << "CURSOR DEBUG: dx=" << dx << " line_y=" << g_editor.cursor_y 
                  << " max_x=" << max_x << " old_x=" << old_x << " new_x=" << g_editor.cursor_x << std::endl;
        if (g_editor.cursor_y > 0) {
            std::cout << "CURSOR DEBUG: Line above (" << (g_editor.cursor_y-1) << ") length=" 
                      << g_editor.lines[g_editor.cursor_y-1].length() << std::endl;
        }
        
        g_ui_needs_redraw = true;  // Trigger UI redraw for position change
    }
}

// MARK: - Lua Syntax Highlighting Functions

static void render_line_with_syntax(const std::string& line, int screen_y) {
    if (line.empty()) return;

    enum State { NORMAL, STRING_DOUBLE, STRING_SINGLE, COMMENT_LINE, COMMENT_BLOCK, NUMBER };
    State state = NORMAL;
    bool in_block_comment = false;
    int block_comment_depth = 0;
    
    // Cache background color to avoid repeated function calls
    uint32_t bg_color = rgba(0, 0, 150, 255);  // Blue background for full-screen editor
    
    // Batch rendering: accumulate consecutive characters with same color
    std::string batch_text;
    uint32_t batch_color = rgba(255, 255, 255, 255);
    int batch_start_x = 0;
    
    auto flush_batch = [&]() {
        if (!batch_text.empty()) {
            editor_set_color(batch_color, bg_color);
            editor_print_at(batch_start_x, screen_y, batch_text.c_str());
            batch_text.clear();
        }
    };

    for (size_t i = 0; i < line.length(); i++) {
        char c = line[i];
        uint32_t color = rgba(255, 255, 255, 255); // Default white
    
        // Handle different states
        switch (state) {
            case STRING_DOUBLE:
                color = rgba(255, 255, 0, 255); // Yellow for strings
                if (c == '"' && (i == 0 || line[i-1] != '\\')) {
                    state = NORMAL;
                }
                break;
            
            case STRING_SINGLE:
                color = rgba(255, 255, 0, 255); // Yellow for strings
                if (c == '\'' && (i == 0 || line[i-1] != '\\')) {
                    state = NORMAL;
                }
                break;
            
            case COMMENT_LINE:
                color = rgba(180, 180, 180, 255); // Light gray for comments
                break;
            
            case COMMENT_BLOCK:
                color = rgba(180, 180, 180, 255); // Light gray for comments
                if (c == ']' && i + 1 < line.length() && line[i+1] == ']') {
                    if (--block_comment_depth == 0) {
                        state = NORMAL;
                        i++; // Skip the second ]
                    }
                }
                break;
            
            case NUMBER:
                if (std::isdigit(c) || c == '.' || c == 'e' || c == 'E' || 
                    (i > 0 && (c == '+' || c == '-'))) {
                    color = rgba(255, 128, 255, 255); // Magenta for numbers
                } else {
                    state = NORMAL;
                    i--; // Reprocess this character
                    continue;
                }
                break;
            
            default: // NORMAL
                if (c == '"') {
                    state = STRING_DOUBLE;
                    color = rgba(255, 255, 0, 255);
                } else if (c == '\'') {
                    state = STRING_SINGLE;
                    color = rgba(255, 255, 0, 255);
                } else if (c == '-' && i + 1 < line.length() && line[i+1] == '-') {
                    if (i + 3 < line.length() && line[i+2] == '[' && line[i+3] == '[') {
                        state = COMMENT_BLOCK;
                        block_comment_depth = 1;
                    } else {
                        state = COMMENT_LINE;
                    }
                    color = rgba(180, 180, 180, 255);
                } else if (std::isdigit(c) || (c == '.' && i + 1 < line.length() && std::isdigit(line[i+1]))) {
                    state = NUMBER;
                    color = rgba(255, 128, 255, 255);
                } else if (std::isalpha(c) || c == '_') {
                    // Extract word for keyword checking
                    size_t word_start = i;
                    while (i < line.length() && (std::isalnum(line[i]) || line[i] == '_')) {
                        i++;
                    }
                    std::string word = line.substr(word_start, i - word_start);
                    i--; // Adjust for loop increment
                
                    if (is_lua_keyword(word)) {
                        color = rgba(100, 200, 255, 255); // Light blue for keywords
                    } else if (is_superterminal_function(word)) {
                        color = rgba(100, 255, 255, 255); // Cyan for SuperTerminal functions
                    } else {
                        color = rgba(255, 255, 255, 255); // White for identifiers
                    }
                
                    // Flush any pending batch before rendering word
                    flush_batch();
                    
                    // Render the entire word as a batch
                    editor_set_color(color, bg_color);
                    std::string word_text = line.substr(word_start, word.length());
                    editor_print_at(word_start, screen_y, word_text.c_str());
                    continue;
                } else {
                    color = rgba(255, 255, 255, 255); // Default white
                }
                break;
        }
    
        // Batch consecutive characters with same color
        if (batch_text.empty() || batch_color == color) {
            if (batch_text.empty()) {
                batch_start_x = i;
                batch_color = color;
            }
            batch_text += c;
        } else {
            // Color changed, flush previous batch
            flush_batch();
            batch_start_x = i;
            batch_color = color;
            batch_text = c;
        }
    }
    
    // Flush any remaining batch
    flush_batch();
}

static bool is_lua_keyword(const std::string& word) {
    static const std::set<std::string> keywords = {
        "and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "goto", "if", "in", "local", "nil", "not", "or",
        "repeat", "return", "then", "true", "until", "while"
    };
    return keywords.find(word) != keywords.end();
}

static bool is_superterminal_function(const std::string& word) {
    static const std::set<std::string> functions = {
        // Text functions
        "cls", "home", "print", "print_at", "accept", "accept_at", "set_color",
        "background_color", "rgba",
    
        // Graphics functions
        "draw_line", "draw_rect", "fill_rect", "draw_circle", "fill_circle",
        "line", "rect", "fillrect", "circle", "fillcircle",
    
        // Sprite functions
        "sprite_load", "sprite_show", "sprite_hide", "sprite_move", "sprite_scale",
        "sprite_rotate", "sprite_alpha", "sprite_release",
    
        // Tile functions
        "tile_load", "tile_set", "tile_get", "tile_scroll", "tile_create_map",
        "tile_clear_map", "tile_fill_map",
    
        // Input functions
        "waitKey", "isKeyPressed", "getKey",
    
        // Audio functions
        "audio_play", "audio_stop", "audio_pause", "beep", "play_note",
    
        // Lua execution (basic functions only)
        "lua_init", "lua_cleanup",
    
        // System functions
        "sleep_ms", "wait_frame", "superterminal_exit",
    
        // Layer functions
        "layer_set_enabled", "layer_is_enabled"
    };
    return functions.find(word) != functions.end();
}

static void editor_horizontal_scroll_if_needed() {
    int displayWidth = get_editor_width();
    const int SCROLL_MARGIN = 5;  // Buffer zone before auto-scrolling
    
    int cursor_x = g_editor.cursor_x;
    int offset = g_editor.horizontal_offset;
    
    std::cout << "[H-SCROLL] cursor_x=" << cursor_x << " offset=" << offset 
              << " displayWidth=" << displayWidth << " margin=" << SCROLL_MARGIN << std::endl;
    
    // Pan right if cursor moves near/beyond right edge
    if (cursor_x >= offset + displayWidth - SCROLL_MARGIN) {
        int old_offset = g_editor.horizontal_offset;
        g_editor.horizontal_offset = cursor_x - displayWidth + SCROLL_MARGIN + 1;
        std::cout << "[H-SCROLL] PAN RIGHT: offset " << old_offset << " -> " << g_editor.horizontal_offset << std::endl;
    }
    
    // Pan left if cursor moves near/before left edge
    if (cursor_x < offset + SCROLL_MARGIN) {
        int old_offset = g_editor.horizontal_offset;
        g_editor.horizontal_offset = cursor_x - SCROLL_MARGIN;
        std::cout << "[H-SCROLL] PAN LEFT: offset " << old_offset << " -> " << g_editor.horizontal_offset << std::endl;
    }
    
    // Keep offset reasonable (never negative, never too far right)
    g_editor.horizontal_offset = std::max(0, g_editor.horizontal_offset);
    
    // Don't scroll horizontally beyond what's necessary
    int maxOffset = std::max(0, (int)g_editor.lines[g_editor.cursor_y].length() - displayWidth + SCROLL_MARGIN);
    g_editor.horizontal_offset = std::min(g_editor.horizontal_offset, maxOffset);
    
    std::cout << "[H-SCROLL] FINAL offset=" << g_editor.horizontal_offset << " (max=" << maxOffset << ")" << std::endl;
}

static void editor_scroll_if_needed() {
    int display_lines = get_content_lines();
    
    std::cout << "TextEditor: scroll_if_needed() - display_lines=" << display_lines 
              << ", cursor_y=" << g_editor.cursor_y 
              << ", scroll_offset=" << g_editor.scroll_offset << std::endl;
    
    // Scroll down if cursor is below visible area
    if (g_editor.cursor_y >= g_editor.scroll_offset + display_lines) {
        g_editor.scroll_offset = g_editor.cursor_y - display_lines + 1;
        std::cout << "TextEditor: Scrolled down, new scroll_offset=" << g_editor.scroll_offset << std::endl;
    }
    
    // Scroll up if cursor is above visible area
    if (g_editor.cursor_y < g_editor.scroll_offset) {
        g_editor.scroll_offset = g_editor.cursor_y;
        std::cout << "TextEditor: Scrolled up, new scroll_offset=" << g_editor.scroll_offset << std::endl;
    }
    
    g_editor.scroll_offset = std::max(0, g_editor.scroll_offset);
    
    // Also check horizontal scrolling
    editor_horizontal_scroll_if_needed();
}

// Copy current line or selection to system clipboard (Cmd+C)
static void editor_copy_line() {
    if (g_editor.lines.empty()) return;
    
    std::string text_to_copy;
    
    // If there's a selection, copy selected text
    if (g_editor.has_selection) {
        text_to_copy = editor_get_selected_text();
        
        if (!text_to_copy.empty()) {
            g_editor.clipboard = text_to_copy;
            macos_clipboard_set_text(text_to_copy.c_str());
            editor_set_status("Selection copied to clipboard");
        }
    } else {
        // Copy current line to internal clipboard
        if (g_editor.cursor_y < (int)g_editor.lines.size()) {
            g_editor.clipboard = g_editor.lines[g_editor.cursor_y];
            
            // Also copy to macOS system clipboard
            macos_clipboard_set_text(g_editor.clipboard.c_str());
            editor_set_status("Line copied to clipboard");
        }
    }
}

// Cut current line or selection (Ctrl+X or Cmd+X)
static void editor_cut_line() {
    if (g_editor.lines.empty()) return;
    
    editor_save_undo_state("Cut");
    
    // If there's a selection, cut selected text
    if (g_editor.has_selection) {
        std::string text_to_cut = editor_get_selected_text();
        
        if (!text_to_cut.empty()) {
            g_editor.clipboard = text_to_cut;
            macos_clipboard_set_text(text_to_cut.c_str());
            editor_delete_selection();
            editor_set_status("Selection cut to clipboard");
        }
    } else {
        // Copy current line to clipboard
        if (g_editor.cursor_y < (int)g_editor.lines.size()) {
            g_editor.clipboard = g_editor.lines[g_editor.cursor_y];
            
            // Also copy to macOS system clipboard
            macos_clipboard_set_text(g_editor.clipboard.c_str());
        }
        
        // Delete the line (reuse existing delete logic)
        if (g_editor.lines.size() == 1) {
            g_editor.lines[0].clear();
            g_editor.cursor_x = 0;
        } else {
            g_editor.lines.erase(g_editor.lines.begin() + g_editor.cursor_y);
            g_editor.dirty_lines.erase(g_editor.dirty_lines.begin() + g_editor.cursor_y);
            
            if (g_editor.cursor_y >= (int)g_editor.lines.size()) {
                g_editor.cursor_y = (int)g_editor.lines.size() - 1;
            }
            if (g_editor.cursor_y < 0) {
                g_editor.cursor_y = 0;
            }
            
            if (g_editor.cursor_y < (int)g_editor.lines.size()) {
                if (g_editor.cursor_x > (int)g_editor.lines[g_editor.cursor_y].length()) {
                    g_editor.cursor_x = (int)g_editor.lines[g_editor.cursor_y].length();
                }
            }
        }
        
        mark_all_dirty();
        g_editor.modified = true;
        g_ui_needs_redraw = true;
        editor_scroll_if_needed();
        editor_set_status("Line cut to clipboard");
    }
}

// Paste line from clipboard (Ctrl+V or Cmd+V)
static void editor_paste_line() {
    editor_save_undo_state("Paste line");
    
    // Try to get text from macOS system clipboard first
    char* system_clipboard = macos_clipboard_get_text();
    std::string paste_text;
    
    if (system_clipboard) {
        paste_text = system_clipboard;
        macos_clipboard_free_text(system_clipboard);
        
        // Update internal clipboard with system clipboard content
        g_editor.clipboard = paste_text;
    } else if (!g_editor.clipboard.empty()) {
        // Fall back to internal clipboard if system clipboard is empty
        paste_text = g_editor.clipboard;
    } else {
        editor_set_status("Clipboard is empty");
        return;
    }
    
    // Handle multi-line paste (if clipboard contains newlines)
    std::istringstream stream(paste_text);
    std::string line;
    bool first_line = true;
    int lines_pasted = 0;
    
    while (std::getline(stream, line)) {
        if (first_line) {
            // Insert first line below current line
            g_editor.lines.insert(g_editor.lines.begin() + g_editor.cursor_y + 1, line);
            g_editor.dirty_lines.insert(g_editor.dirty_lines.begin() + g_editor.cursor_y + 1, true);
            g_editor.cursor_y++;
            first_line = false;
        } else {
            // Insert subsequent lines
            g_editor.lines.insert(g_editor.lines.begin() + g_editor.cursor_y + 1, line);
            g_editor.dirty_lines.insert(g_editor.dirty_lines.begin() + g_editor.cursor_y + 1, true);
            g_editor.cursor_y++;
        }
        lines_pasted++;
    }
    
    // If no newlines were found, treat as single line
    if (lines_pasted == 0) {
        g_editor.lines.insert(g_editor.lines.begin() + g_editor.cursor_y + 1, paste_text);
        g_editor.dirty_lines.insert(g_editor.dirty_lines.begin() + g_editor.cursor_y + 1, true);
        g_editor.cursor_y++;
        lines_pasted = 1;
    }
    
    g_editor.cursor_x = 0;
    mark_all_dirty();
    g_editor.modified = true;
    g_ui_needs_redraw = true;
    editor_scroll_if_needed();
    
    if (lines_pasted == 1) {
        editor_set_status("Line pasted from clipboard");
    } else {
        char status[64];
        snprintf(status, sizeof(status), "%d lines pasted from clipboard", lines_pasted);
        editor_set_status(status);
    }
}

// Save current editor state for undo
static void editor_save_undo_state(const std::string& description) {
    // Don't save undo state for undo/redo operations themselves
    if (description == "Undo" || description == "Redo") return;
    
    // Initialize max undo levels if not set
    if (g_editor.max_undo_levels == 0) {
        g_editor.max_undo_levels = 50;
    }
    
    TextEditorState::EditorSnapshot snapshot;
    snapshot.lines = g_editor.lines;
    snapshot.cursor_x = g_editor.cursor_x;
    snapshot.cursor_y = g_editor.cursor_y;
    snapshot.scroll_offset = g_editor.scroll_offset;
    snapshot.horizontal_offset = g_editor.horizontal_offset;
    snapshot.description = description;
    
    g_editor.undo_stack.push_back(snapshot);
    
    // Clear redo stack when new action is performed
    g_editor.redo_stack.clear();
    
    // Limit undo stack size
    if ((int)g_editor.undo_stack.size() > g_editor.max_undo_levels) {
        g_editor.undo_stack.erase(g_editor.undo_stack.begin());
    }
}

// Undo last operation (Ctrl+Z)
static void editor_undo() {
    if (g_editor.undo_stack.empty()) {
        editor_set_status("Nothing to undo");
        return;
    }
    
    // Save current state to redo stack
    TextEditorState::EditorSnapshot current_snapshot;
    current_snapshot.lines = g_editor.lines;
    current_snapshot.cursor_x = g_editor.cursor_x;
    current_snapshot.cursor_y = g_editor.cursor_y;
    current_snapshot.scroll_offset = g_editor.scroll_offset;
    current_snapshot.horizontal_offset = g_editor.horizontal_offset;
    current_snapshot.description = "Redo";
    g_editor.redo_stack.push_back(current_snapshot);
    
    // Restore previous state
    TextEditorState::EditorSnapshot& snapshot = g_editor.undo_stack.back();
    g_editor.lines = snapshot.lines;
    g_editor.cursor_x = snapshot.cursor_x;
    g_editor.cursor_y = snapshot.cursor_y;
    g_editor.scroll_offset = snapshot.scroll_offset;
    g_editor.horizontal_offset = snapshot.horizontal_offset;
    
    g_editor.undo_stack.pop_back();
    
    g_editor.modified = true;
    g_ui_needs_redraw = true;
    editor_scroll_if_needed();
    editor_set_status(("Undone: " + snapshot.description).c_str());
}

// Redo last undone operation (Ctrl+Y)
static void editor_redo() {
    if (g_editor.redo_stack.empty()) {
        editor_set_status("Nothing to redo");
        return;
    }
    
    // Save current state to undo stack
    editor_save_undo_state("Undo");
    
    // Restore next state
    TextEditorState::EditorSnapshot& snapshot = g_editor.redo_stack.back();
    g_editor.lines = snapshot.lines;
    g_editor.cursor_x = snapshot.cursor_x;
    g_editor.cursor_y = snapshot.cursor_y;
    g_editor.scroll_offset = snapshot.scroll_offset;
    
    g_editor.redo_stack.pop_back();
    
    g_editor.modified = true;
    g_ui_needs_redraw = true;
    editor_scroll_if_needed();
    editor_set_status("Redone");
}

// Native macOS dialog functions (implemented in Objective-C)
extern "C" {
    char* show_macos_input_dialog(const char* title, const char* message, const char* defaultText);
}

// Find text in editor (Ctrl+F)
static void editor_find() {
    const char* default_text = g_editor.search_term.empty() ? "" : g_editor.search_term.c_str();
    char* search_input = show_macos_input_dialog("Find Text", "Enter search term:", default_text);
    
    if (search_input && strlen(search_input) > 0) {
        g_editor.search_term = search_input;
        g_editor.search_start_x = g_editor.cursor_x;
        g_editor.search_start_y = g_editor.cursor_y;
        g_editor.search_active = true;
        
        // Search from current position
        bool found = false;
        for (int y = g_editor.cursor_y; y < (int)g_editor.lines.size() && !found; y++) {
            int start_x = (y == g_editor.cursor_y) ? g_editor.cursor_x + 1 : 0;
            size_t pos = g_editor.lines[y].find(g_editor.search_term, start_x);
            if (pos != std::string::npos) {
                g_editor.cursor_y = y;
                g_editor.cursor_x = (int)pos;
                editor_scroll_if_needed();
                found = true;
                editor_set_status(("Found: " + g_editor.search_term).c_str());
            }
        }
        
        if (!found) {
            // Search from beginning of file
            for (int y = 0; y <= g_editor.search_start_y && !found; y++) {
                int end_x = (y == g_editor.search_start_y) ? g_editor.search_start_x : g_editor.lines[y].length();
                size_t pos = g_editor.lines[y].find(g_editor.search_term, 0);
                if (pos != std::string::npos && (int)pos < end_x) {
                    g_editor.cursor_y = y;
                    g_editor.cursor_x = (int)pos;
                    editor_scroll_if_needed();
                    found = true;
                    editor_set_status(("Found: " + g_editor.search_term).c_str());
                }
            }
        }
        
        if (!found) {
            editor_set_status(("Not found: " + g_editor.search_term).c_str());
        }
    } else {
        editor_set_status("Search cancelled");
    }
    
    if (search_input) free(search_input);
}

// Go to line number (Ctrl+L)
static void editor_goto_line() {
    std::string current_line_str = std::to_string(g_editor.cursor_y + 1);
    std::string message = "Enter line number (1-" + std::to_string(g_editor.lines.size()) + "):";
    
    char* line_input = show_macos_input_dialog("Go to Line", message.c_str(), current_line_str.c_str());
    
    if (line_input && strlen(line_input) > 0) {
        int target_line = atoi(line_input);
        if (target_line > 0 && target_line <= (int)g_editor.lines.size()) {
            g_editor.cursor_y = target_line - 1; // Convert to 0-based
            g_editor.cursor_x = 0;
            editor_scroll_if_needed();
            editor_set_status(("Moved to line " + std::to_string(target_line)).c_str());
        } else {
            editor_set_status(("Invalid line number: " + std::string(line_input)).c_str());
        }
    } else {
        editor_set_status("Go to line cancelled");
    }
    
    if (line_input) free(line_input);
}

// Move cursor half screen up (Page Up)
static void editor_page_up() {
    int half_screen = get_content_lines() / 2;
    std::cout << "TextEditor: page_up() - content_lines=" << (half_screen * 2) 
              << ", half_screen=" << half_screen << std::endl;
    int new_y = std::max(0, g_editor.cursor_y - half_screen);
    g_editor.cursor_y = new_y;
    
    // Adjust cursor_x if needed
    if (g_editor.cursor_y < (int)g_editor.lines.size()) {
        if (g_editor.cursor_x > (int)g_editor.lines[g_editor.cursor_y].length()) {
            g_editor.cursor_x = (int)g_editor.lines[g_editor.cursor_y].length();
        }
    }
    
    editor_scroll_if_needed();
}

// Move cursor half screen down (Page Down)
static void editor_page_down() {
    int half_screen = get_content_lines() / 2;
    std::cout << "TextEditor: page_down() - content_lines=" << (half_screen * 2) 
              << ", half_screen=" << half_screen << std::endl;
    int new_y = std::min((int)g_editor.lines.size() - 1, g_editor.cursor_y + half_screen);
    g_editor.cursor_y = new_y;
    
    // Adjust cursor_x if needed
    if (g_editor.cursor_y < (int)g_editor.lines.size()) {
        if (g_editor.cursor_x > (int)g_editor.lines[g_editor.cursor_y].length()) {
            g_editor.cursor_x = (int)g_editor.lines[g_editor.cursor_y].length();
        }
    }
    
    editor_scroll_if_needed();
}

void editor_set_status(const char* message, int duration) {
    g_editor.status_message = message;
    g_editor.status_timer = duration;
    g_editor.show_status = true;
}

static void editor_update_ui_content() {
    if (!g_editor.active) {
        return;
    }
    
    // Generate status bar text with cursor position and other info
    char status_text[256];
    int line = editor_get_cursor_line();
    int col = editor_get_cursor_column();
    const char* filename = editor_get_filename();
    const char* modified_indicator = editor_is_modified() ? " [Modified]" : "";
    
    const char* mode_indicator = editor_is_repl_mode() ? " [REPL Mode]" : "";
    snprintf(status_text, sizeof(status_text), 
             " Line: %d, Col: %d | File: %s%s%s", 
             line, col, filename ? filename : "Untitled", modified_indicator, mode_indicator);
    
    // Only update if content has changed
    if (g_editor.status_bar_text != status_text) {
        g_editor.status_bar_text = status_text;
        g_ui_needs_redraw = true;  // Mark that UI needs to be redrawn
        // std::cout << "TextEditor: Status bar content changed, triggering redraw" << std::endl;
    }
    g_editor.show_status_bar = true;
}

static void editor_refresh_overlay_ui() {
    // No longer needed - using text grid instead of overlay
    // Just trigger buffer update
    editor_update_buffer();
}

// Update UI content - called every 60 frames
void ui_update_content(void) {
    if (!g_editor.active) {
        return;
    }
    
    // Text-based UI is now part of editor_update_buffer()
    // This function kept for compatibility
}

// Draw UI overlay - no longer used (text grid handles everything)
void ui_draw_overlay(void) {
    // Editor UI is now rendered entirely in the text grid (C64-style)
    // This function kept for compatibility but does nothing
}



static std::string editor_get_scripts_path() {
    return "scripts";
}

static void editor_ensure_scripts_folder() {
    std::string scripts_path = editor_get_scripts_path();
    struct stat st;
    memset(&st, 0, sizeof(st));
    if (stat(scripts_path.c_str(), &st) == -1) {
        if (mkdir(scripts_path.c_str(), 0700) == 0) {
            std::cout << "TextEditor: Created scripts folder: " << scripts_path << std::endl;
        } else {
            std::cerr << "TextEditor: Could not create scripts folder" << std::endl;
        }
    }
}

// Mouse support functions (extern C)
extern "C" {
    void editor_set_cursor_from_mouse(float x, float y);
    void editor_mouse_down(float x, float y);
    void editor_mouse_drag(float x, float y);
    void editor_mouse_up(float x, float y);
    void editor_scroll_vertical(int lines);
    void input_system_get_viewport_size(float* width, float* height);
}

void editor_set_cursor_from_mouse(float x, float y) {
    if (!g_editor.active) {
        return;
    }
    
    // Convert screen coordinates to character position - use actual grid dimensions
    int gridWidth, gridHeight;
    text_grid_get_dimensions(&gridWidth, &gridHeight);
    
    float cellWidth, cellHeight;
    text_grid_get_cell_size(&cellWidth, &cellHeight);
    
    // Convert screen coordinates to character grid position
    int screen_x = (int)(x / cellWidth);
    int screen_y = (int)(y / cellHeight);
    
    // Account for content start position (status bars at top)
    int content_start_y = get_editor_start_y();
    int content_lines = get_content_lines();
    
    std::cout << "[MOUSE] Raw: x=" << x << " y=" << y << std::endl;
    std::cout << "[MOUSE] Cell size: " << cellWidth << "x" << cellHeight << std::endl;
    std::cout << "[MOUSE] Grid: " << gridWidth << "x" << gridHeight << std::endl;
    std::cout << "[MOUSE] Screen pos: x=" << screen_x << " y=" << screen_y << std::endl;
    std::cout << "[MOUSE] Content: start_y=" << content_start_y << " lines=" << content_lines << std::endl;
    std::cout << "[MOUSE] Scroll: " << g_editor.scroll_offset << std::endl;
    
    // If click is outside content area (on status bars), ignore it
    if (screen_y < content_start_y || screen_y >= content_start_y + content_lines) {
        std::cout << "[MOUSE] Outside content area, ignoring" << std::endl;
        return;
    }
    
    // Convert screen row to document line
    // Visual rendering does: screen_y = (cursor_y - scroll) + content_start_y
    // So we need: cursor_y = (screen_y - content_start_y) + scroll
    int char_y = (screen_y - content_start_y) + g_editor.scroll_offset;
    
    std::cout << "[MOUSE] Calculated char_y: " << char_y << std::endl;
    
    // Apply horizontal offset to get actual file column position
    int char_x = screen_x + g_editor.horizontal_offset;
    
    // Clamp to valid ranges
    char_x = std::max(0, char_x);
    char_y = std::max(0, std::min(char_y, (int)g_editor.lines.size() - 1));
    
    // Ensure cursor_x is within the line length
    if (char_y < (int)g_editor.lines.size()) {
        int line_length = g_editor.lines[char_y].length();
        char_x = std::min(char_x, line_length);
    }
    
    // Update cursor position
    g_editor.cursor_x = char_x;
    g_editor.cursor_y = char_y;
    
    std::cout << "[MOUSE] Set cursor to: x=" << char_x << " y=" << char_y << std::endl;
    
    // Ensure cursor is visible (adjust scroll if needed)
    editor_scroll_if_needed();
}

// Mouse down - start selection
void editor_mouse_down(float x, float y) {
    if (!g_editor.active) {
        return;
    }
    
    // Convert screen coordinates to character position (same as cursor positioning)
    int gridWidth, gridHeight;
    text_grid_get_dimensions(&gridWidth, &gridHeight);
    
    float cellWidth, cellHeight;
    text_grid_get_cell_size(&cellWidth, &cellHeight);
    
    int screen_x = (int)(x / cellWidth);
    int screen_y = (int)(y / cellHeight);
    
    int content_start_y = get_editor_start_y();
    int content_lines = get_content_lines();
    
    // If click is outside content area, ignore
    if (screen_y < content_start_y || screen_y >= content_start_y + content_lines) {
        return;
    }
    
    // Convert to document position
    int char_y = (screen_y - content_start_y) + g_editor.scroll_offset;
    int char_x = screen_x + g_editor.horizontal_offset;
    
    // Clamp to valid ranges
    char_x = std::max(0, char_x);
    char_y = std::max(0, std::min(char_y, (int)g_editor.lines.size() - 1));
    
    if (char_y < (int)g_editor.lines.size()) {
        int line_length = g_editor.lines[char_y].length();
        char_x = std::min(char_x, line_length);
    }
    
    // Start new selection
    g_editor.has_selection = false;
    g_editor.is_selecting = true;
    g_editor.selection_start_x = char_x;
    g_editor.selection_start_y = char_y;
    g_editor.selection_end_x = char_x;
    g_editor.selection_end_y = char_y;
    
    // Also update cursor position
    g_editor.cursor_x = char_x;
    g_editor.cursor_y = char_y;
    
    std::cout << "[SELECTION] Mouse down at: x=" << char_x << " y=" << char_y << std::endl;
}

// Mouse drag - update selection
void editor_mouse_drag(float x, float y) {
    if (!g_editor.active || !g_editor.is_selecting) {
        return;
    }
    
    // Convert screen coordinates to character position
    int gridWidth, gridHeight;
    text_grid_get_dimensions(&gridWidth, &gridHeight);
    
    float cellWidth, cellHeight;
    text_grid_get_cell_size(&cellWidth, &cellHeight);
    
    int screen_x = (int)(x / cellWidth);
    int screen_y = (int)(y / cellHeight);
    
    int content_start_y = get_editor_start_y();
    int content_lines = get_content_lines();
    
    // If drag goes outside content area, clamp to edges
    if (screen_y < content_start_y) {
        screen_y = content_start_y;
    } else if (screen_y >= content_start_y + content_lines) {
        screen_y = content_start_y + content_lines - 1;
    }
    
    // Convert to document position
    int char_y = (screen_y - content_start_y) + g_editor.scroll_offset;
    int char_x = screen_x + g_editor.horizontal_offset;
    
    // Clamp to valid ranges
    char_x = std::max(0, char_x);
    char_y = std::max(0, std::min(char_y, (int)g_editor.lines.size() - 1));
    
    if (char_y < (int)g_editor.lines.size()) {
        int line_length = g_editor.lines[char_y].length();
        char_x = std::min(char_x, line_length);
    }
    
    // Update selection end
    g_editor.selection_end_x = char_x;
    g_editor.selection_end_y = char_y;
    
    // Mark as having selection if start != end
    if (g_editor.selection_start_x != g_editor.selection_end_x ||
        g_editor.selection_start_y != g_editor.selection_end_y) {
        g_editor.has_selection = true;
    }
    
    // Update cursor to follow drag
    g_editor.cursor_x = char_x;
    g_editor.cursor_y = char_y;
    
    // Force redraw to show selection
    mark_all_dirty();
    editor_update_buffer();
    
    std::cout << "[SELECTION] Drag to: x=" << char_x << " y=" << char_y << std::endl;
}

// Mouse up - finalize selection
void editor_mouse_up(float x, float y) {
    if (!g_editor.active) {
        return;
    }
    
    if (g_editor.is_selecting) {
        // Finalize selection
        g_editor.is_selecting = false;
        
        // If no actual selection (click without drag), clear selection
        if (g_editor.selection_start_x == g_editor.selection_end_x &&
            g_editor.selection_start_y == g_editor.selection_end_y) {
            g_editor.has_selection = false;
        }
        
        std::cout << "[SELECTION] Mouse up - selection " 
                  << (g_editor.has_selection ? "active" : "cleared") << std::endl;
        
        mark_all_dirty();
        editor_update_buffer();
    }
}

void editor_scroll_vertical(int lines) {
    if (!g_editor.active || lines == 0) {
        return;
    }
    
    int old_scroll = g_editor.scroll_offset;
    
    // Update scroll offset
    g_editor.scroll_offset += lines;
    
    // Clamp scroll to valid range
    int max_scroll = std::max(0, (int)g_editor.lines.size() - get_content_lines());
    g_editor.scroll_offset = std::max(0, std::min(g_editor.scroll_offset, max_scroll));
    
    // If we actually scrolled, update the display
    if (g_editor.scroll_offset != old_scroll) {
        std::cout << "Scrolled from " << old_scroll << " to " << g_editor.scroll_offset << std::endl;
        
        // Force UI redraw
        g_ui_needs_redraw = true;
        
        // Update the text buffer
        editor_update_buffer();
    }
}

bool editor_is_loaded_from_database(void) {
    return g_editor.loaded_from_database;
}

const char* editor_get_db_script_name(void) {
    return g_editor.db_script_name.empty() ? nullptr : g_editor.db_script_name.c_str();
}

const char* editor_get_db_version(void) {
    return g_editor.db_version.empty() ? nullptr : g_editor.db_version.c_str();
}

const char* editor_get_db_author(void) {
    return g_editor.db_author.empty() ? nullptr : g_editor.db_author.c_str();
}

void editor_format_lua_code() {
    std::cout << "editor_format_lua_code() called!" << std::endl;
    
    if (!g_editor.active) {
        std::cout << "Editor not active, returning" << std::endl;
        editor_set_status("ERROR: Editor not active", 180);
        return;
    }
    
    if (g_editor.lines.empty()) {
        std::cout << "No content to format, returning" << std::endl;
        editor_set_status("No content to format", 90);
        return;
    }
    
    // Check if we have content that looks like Lua (similar to F9 handler)
    bool isLuaContent = false;
    if (!g_editor.current_filename.empty()) {
        std::string filename = g_editor.current_filename;
        isLuaContent = (filename.length() >= 4 && filename.substr(filename.length() - 4) == ".lua");
    }
    // Also check if content looks like Lua
    if (!isLuaContent && !g_editor.lines.empty()) {
        for (const auto& line : g_editor.lines) {
            if (line.find("function") != std::string::npos ||
                line.find("local") != std::string::npos ||
                line.find("start_of_script") != std::string::npos ||
                line.find("end_of_script") != std::string::npos) {
                isLuaContent = true;
                break;
            }
        }
    }
    
    if (isLuaContent) {
        // Save current state for undo
        editor_save_undo_state("Format Lua Code");
        
        size_t originalLines = g_editor.lines.size();
        std::cout << "Formatting " << originalLines << " lines..." << std::endl;
        
        // Create temp file
        std::string temp_file = "/tmp/superterminal_format_tmp.lua";
        std::ofstream temp_out(temp_file);
        for (const auto& line : g_editor.lines) {
            temp_out << line << "\n";
        }
        temp_out.close();
        
        // Call lua-format binary from app bundle
        std::string cmd = get_lua_format_path() + " -i " + temp_file + " 2>/dev/null";
        int result = system(cmd.c_str());
        
        if (result == 0) {
            // Read formatted file back
            std::ifstream formatted_in(temp_file);
            g_editor.lines.clear();
            std::string line;
            while (std::getline(formatted_in, line)) {
                g_editor.lines.push_back(line);
            }
            formatted_in.close();
            
            // Mark as modified
            g_editor.modified = true;
            
            // Set success status
            editor_set_status("Lua code formatted successfully", 120);
            
            // Update buffer to refresh display
            editor_update_buffer();
            
            std::cout << "TextEditor: Applied Lua formatting (" << originalLines << " lines)" << std::endl;
        } else {
            std::cout << "Formatting failed with result: " << result << std::endl;
            editor_set_status("ERROR: Lua formatting failed", 180);
        }
        
        // Clean up temp file
        std::remove(temp_file.c_str());
    } else {
        editor_set_status("Not a Lua file - formatting skipped", 120);
        std::cout << "Not Lua content, skipping format" << std::endl;
    }
    
    std::cout << "editor_format_lua_code() finished" << std::endl;
}

