//
//  InteractiveEditor.h
//  SuperTerminal Framework - Commodore 64-Style Interactive Editor
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  This system provides a full-screen interactive Lua editor overlay
//  inspired by the Commodore 64's immediate programming environment.
//
//  Key bindings:
//  F1  - Toggle editor on/off
//  F2  - Execute current Lua code
//  F8  - Save current text to file
//  ESC - Exit editor without saving
//

#ifndef INTERACTIVE_EDITOR_H
#define INTERACTIVE_EDITOR_H

#include <string>
#include <vector>
#include <memory>

namespace SuperTerminal {

/**
 * Interactive editor state and configuration
 */
struct EditorConfig {
    bool show_line_numbers = true;
    bool auto_indent = true;
    bool syntax_highlighting = true;
    int tab_size = 4;
    std::string default_filename = "scratch.lua";
    std::string script_directory = "~/.superterminal/scripts/";
};

/**
 * Editor cursor position and selection
 */
struct EditorCursor {
    int line = 0;          // Current line (0-based)
    int column = 0;        // Current column (0-based)
    int selection_start_line = -1;    // Selection start (-1 = no selection)
    int selection_start_column = -1;
    int selection_end_line = -1;
    int selection_end_column = -1;
    
    bool has_selection() const {
        return selection_start_line >= 0 && selection_end_line >= 0;
    }
    
    void clear_selection() {
        selection_start_line = selection_start_column = -1;
        selection_end_line = selection_end_column = -1;
    }
};

/**
 * Editor undo/redo operation
 */
struct EditorOperation {
    enum Type {
        INSERT_CHAR,
        DELETE_CHAR,
        INSERT_LINE,
        DELETE_LINE,
        REPLACE_TEXT
    };
    
    Type type;
    int line;
    int column;
    std::string text_before;
    std::string text_after;
    EditorCursor cursor_before;
    EditorCursor cursor_after;
};

/**
 * Main interactive editor class
 * 
 * Provides a full-screen Lua code editor with:
 * - Syntax highlighting
 * - Line numbers
 * - Auto-indentation
 * - Undo/redo
 * - File operations
 * - Immediate Lua execution
 */
class InteractiveEditor {
private:
    // Editor state
    bool m_visible = false;
    bool m_modified = false;
    EditorConfig m_config;
    EditorCursor m_cursor;
    
    // Text content
    std::vector<std::string> m_lines;
    std::string m_filename;
    
    // Undo/redo system
    std::vector<EditorOperation> m_undo_stack;
    std::vector<EditorOperation> m_redo_stack;
    static const size_t MAX_UNDO_OPERATIONS = 1000;
    
    // Display properties
    int m_viewport_top = 0;     // First visible line
    int m_viewport_left = 0;    // First visible column
    int m_display_width = 80;   // Available display width
    int m_display_height = 25;  // Available display height
    
    // Status and messaging
    std::string m_status_message;
    int m_status_message_timeout = 0;
    
    // Lua execution state
    bool m_lua_initialized = false;
    std::string m_last_lua_error;

public:
    /**
     * Constructor - initializes empty editor
     */
    InteractiveEditor();
    
    /**
     * Destructor - cleanup resources
     */
    ~InteractiveEditor();
    
    // MARK: - Core Editor Operations
    
    /**
     * Toggle editor visibility (F1 key handler)
     */
    void toggle();
    
    /**
     * Show the editor overlay
     */
    void show();
    
    /**
     * Hide the editor overlay
     */
    void hide();
    
    /**
     * Check if editor is currently visible
     */
    bool is_visible() const { return m_visible; }
    
    /**
     * Update editor state and handle input
     * Should be called every frame when editor is active
     */
    void update();
    
    /**
     * Render the editor to the text layer
     * Called automatically by update() when visible
     */
    void render();
    
    // MARK: - Key Input Handling
    
    /**
     * Handle key press events
     * @param key ASCII key code
     * @param keycode Platform-specific key code  
     * @param shift Shift key pressed
     * @param ctrl Control key pressed
     * @param alt Alt key pressed
     * @param cmd Command key pressed (macOS)
     */
    void handle_key(int key, int keycode, bool shift, bool ctrl, bool alt, bool cmd);
    
    /**
     * Handle special function keys
     */
    void handle_function_key(int keycode, bool shift, bool ctrl, bool alt, bool cmd);
    
    // MARK: - Text Editing Operations
    
    /**
     * Insert character at cursor position
     */
    void insert_char(char c);
    
    /**
     * Insert text at cursor position
     */
    void insert_text(const std::string& text);
    
    /**
     * Delete character at cursor (backspace)
     */
    void delete_char_backward();
    
    /**
     * Delete character after cursor (delete key)
     */
    void delete_char_forward();
    
    /**
     * Insert new line at cursor
     */
    void insert_line();
    
    /**
     * Delete current line
     */
    void delete_line();
    
    /**
     * Move cursor in specified direction
     */
    void move_cursor(int delta_line, int delta_column, bool extend_selection = false);
    
    /**
     * Move cursor to specific position
     */
    void set_cursor_position(int line, int column, bool extend_selection = false);
    
    /**
     * Select all text
     */
    void select_all();
    
    /**
     * Copy selected text to clipboard
     */
    std::string copy_selection();
    
    /**
     * Cut selected text to clipboard
     */
    std::string cut_selection();
    
    /**
     * Paste text from clipboard
     */
    void paste_text(const std::string& text);
    
    // MARK: - File Operations
    
    /**
     * Create new file (clear editor)
     */
    void new_file();
    
    /**
     * Load file into editor
     * @param filename Path to file to load
     * @return true if successful, false on error
     */
    bool load_file(const std::string& filename);
    
    /**
     * Save current content to file (F8 key handler)
     * @param filename Optional filename (uses current if empty)
     * @return true if successful, false on error
     */
    bool save_file(const std::string& filename = "");
    
    /**
     * Auto-save to temporary file
     */
    void auto_save();
    
    /**
     * Get current filename
     */
    const std::string& get_filename() const { return m_filename; }
    
    /**
     * Check if content has been modified
     */
    bool is_modified() const { return m_modified; }
    
    // MARK: - Lua Execution
    
    /**
     * Execute current editor content as Lua code (F2 key handler)
     * @return true if execution successful, false on error
     */
    bool execute_lua();
    
    /**
     * Execute selected text as Lua code
     * @return true if execution successful, false on error
     */
    bool execute_lua_selection();
    
    /**
     * Get last Lua execution error
     */
    const std::string& get_lua_error() const { return m_last_lua_error; }
    
    // MARK: - Undo/Redo System
    
    /**
     * Undo last operation
     */
    void undo();
    
    /**
     * Redo last undone operation
     */
    void redo();
    
    /**
     * Check if undo is available
     */
    bool can_undo() const { return !m_undo_stack.empty(); }
    
    /**
     * Check if redo is available
     */
    bool can_redo() const { return !m_redo_stack.empty(); }
    
    // MARK: - Content Access
    
    /**
     * Get all editor content as single string
     */
    std::string get_content() const;
    
    /**
     * Set editor content from string
     */
    void set_content(const std::string& content);
    
    /**
     * Get number of lines
     */
    size_t get_line_count() const { return m_lines.size(); }
    
    /**
     * Get specific line content
     */
    const std::string& get_line(size_t line_index) const;
    
    // MARK: - Display and UI
    
    /**
     * Set display dimensions
     */
    void set_display_size(int width, int height);
    
    /**
     * Show status message temporarily
     */
    void show_status(const std::string& message, int timeout_frames = 180);
    
    /**
     * Get configuration reference for modification
     */
    EditorConfig& config() { return m_config; }
    
private:
    // MARK: - Internal Helper Methods
    
    /**
     * Ensure cursor is within valid bounds
     */
    void clamp_cursor();
    
    /**
     * Ensure viewport shows cursor
     */
    void ensure_cursor_visible();
    
    /**
     * Calculate auto-indentation for new line
     */
    int calculate_auto_indent(int line_index) const;
    
    /**
     * Get syntax highlighting color for character at position
     */
    uint32_t get_syntax_color(int line, int column) const;
    
    /**
     * Record operation for undo system
     */
    void push_undo_operation(const EditorOperation& op);
    
    /**
     * Apply syntax highlighting to text
     */
    void render_line_with_syntax(int line_index, int screen_y, int start_column, int end_column);
    
    /**
     * Render line numbers margin
     */
    void render_line_numbers();
    
    /**
     * Render status bar
     */
    void render_status_bar();
    
    /**
     * Render cursor
     */
    void render_cursor();
    
    /**
     * Initialize Lua runtime if needed
     */
    bool ensure_lua_initialized();
    
    /**
     * Get expanded path (handle ~ and relative paths)
     */
    std::string expand_path(const std::string& path) const;
};

// MARK: - Global Editor Instance

/**
 * Global interactive editor instance
 * Accessible from C API functions
 */
extern std::unique_ptr<InteractiveEditor> g_interactive_editor;

// MARK: - C API Functions (for SuperTerminal.h)

extern "C" {
    /**
     * Initialize the interactive editor system
     */
    void interactive_editor_init();
    
    /**
     * Cleanup the interactive editor system
     */
    void interactive_editor_cleanup();
    
    /**
     * Toggle editor visibility (F1 handler)
     */
    void interactive_editor_toggle();
    
    /**
     * Update editor (call from main loop)
     */
    void interactive_editor_update();
    
    /**
     * Handle key input (call from input system)
     */
    void interactive_editor_handle_key(int key, int keycode, bool shift, bool ctrl, bool alt, bool cmd);
    
    /**
     * Execute current Lua code (F2 handler)
     */
    bool interactive_editor_execute();
    
    /**
     * Save current file (F8 handler)
     */
    bool interactive_editor_save();
    
    /**
     * Check if editor is visible
     */
    bool interactive_editor_is_visible();
    
    /**
     * Load file into editor
     */
    bool interactive_editor_load_file(const char* filename);
    
    /**
     * Get current editor content
     */
    const char* interactive_editor_get_content();
    
    /**
     * Set editor content
     */
    void interactive_editor_set_content(const char* content);
}

} // namespace SuperTerminal

#endif // INTERACTIVE_EDITOR_H