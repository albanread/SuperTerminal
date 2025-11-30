//
//  InteractiveEditor.cpp
//  SuperTerminal Framework - Commodore 64-Style Interactive Editor
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Implementation of the interactive Lua editor system inspired by
//  the immediate programming environment of the Commodore 64.
//

#include "InteractiveEditor.h"
#include "SuperTerminal.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <cstdlib>

namespace SuperTerminal {

// MARK: - Global Editor Instance

std::unique_ptr<InteractiveEditor> g_interactive_editor = nullptr;

// MARK: - Color Constants

static const uint32_t COLOR_BACKGROUND     = 0x001122FF;  // Dark blue background
static const uint32_t COLOR_TEXT_NORMAL    = 0xC0C0C0FF;  // Light gray text
static const uint32_t COLOR_TEXT_KEYWORD   = 0x00FF00FF;  // Green keywords
static const uint32_t COLOR_TEXT_STRING    = 0xFFFF00FF;  // Yellow strings
static const uint32_t COLOR_TEXT_COMMENT   = 0x80FF80FF;  // Light green comments
static const uint32_t COLOR_TEXT_NUMBER    = 0xFF8080FF;  // Light red numbers
static const uint32_t COLOR_LINE_NUMBERS   = 0x808080FF;  // Gray line numbers
static const uint32_t COLOR_STATUS_BAR     = 0x404040FF;  // Dark gray status
static const uint32_t COLOR_CURSOR         = 0xFFFFFFFF;  // White cursor
static const uint32_t COLOR_SELECTION      = 0x0080FFFF;  // Blue selection

// MARK: - InteractiveEditor Implementation

InteractiveEditor::InteractiveEditor() {
    // Initialize with empty content
    m_lines.push_back("");
    m_filename = m_config.default_filename;
    
    // Set default display size (will be updated by terminal)
    m_display_width = 80;
    m_display_height = 25;
    
    std::cout << "InteractiveEditor: Initialized with C64-style interface" << std::endl;
}

InteractiveEditor::~InteractiveEditor() {
    if (m_modified) {
        auto_save();
    }
}

void InteractiveEditor::toggle() {
    if (m_visible) {
        hide();
    } else {
        show();
    }
}

void InteractiveEditor::show() {
    if (!m_visible) {
        m_visible = true;
        ensure_lua_initialized();
        show_status("Interactive Editor - F2:Run F8:Save ESC:Exit", 300);
        std::cout << "InteractiveEditor: Activated (C64 mode ON)" << std::endl;
    }
}

void InteractiveEditor::hide() {
    if (m_visible) {
        m_visible = false;
        std::cout << "InteractiveEditor: Deactivated" << std::endl;
    }
}

void InteractiveEditor::update() {
    if (!m_visible) return;
    
    // Update display dimensions from terminal
    // TODO: Get actual terminal dimensions
    
    // Decrease status message timeout
    if (m_status_message_timeout > 0) {
        m_status_message_timeout--;
        if (m_status_message_timeout == 0) {
            m_status_message.clear();
        }
    }
    
    // Render the editor
    render();
}

void InteractiveEditor::render() {
    if (!m_visible) return;
    
    std::cout << "DEBUG: Starting editor render, display_size=" << m_display_width << "x" << m_display_height 
              << " lines=" << m_lines.size() << std::endl;
    
    // Clear the editor layer
    std::cout << "DEBUG: Calling editor_cls()" << std::endl;
    editor_cls();
    
    std::cout << "DEBUG: Setting background color" << std::endl;
    editor_background_color(COLOR_BACKGROUND);
    
    // Calculate usable area (minus status bar and line numbers)
    int line_number_width = m_config.show_line_numbers ? 5 : 0;
    int text_start_x = line_number_width;
    int text_width = m_display_width - line_number_width;
    int text_height = m_display_height - 1; // Reserve bottom line for status
    
    std::cout << "DEBUG: Calculated dimensions - line_width=" << line_number_width 
              << " text_width=" << text_width << " text_height=" << text_height << std::endl;
    
    // Render line numbers
    if (m_config.show_line_numbers) {
        std::cout << "DEBUG: About to render line numbers" << std::endl;
        render_line_numbers();
        std::cout << "DEBUG: Line numbers rendered successfully" << std::endl;
    }
    
    // Render text content
    int visible_lines = std::min(text_height, (int)m_lines.size() - m_viewport_top);
    std::cout << "DEBUG: About to render " << visible_lines << " text lines" << std::endl;
    
    for (int screen_y = 0; screen_y < visible_lines; screen_y++) {
        int line_index = m_viewport_top + screen_y;
        if (line_index < m_lines.size()) {
            std::cout << "DEBUG: About to render text line " << line_index << std::endl;
            render_line_with_syntax(line_index, screen_y, 
                                  m_viewport_left, 
                                  m_viewport_left + text_width);
            std::cout << "DEBUG: Text line " << line_index << " rendered successfully" << std::endl;
        }
    }
    
    std::cout << "DEBUG: About to render cursor" << std::endl;
    // Render cursor
    render_cursor();
    
    std::cout << "DEBUG: About to render status bar" << std::endl;
    // Render status bar
    render_status_bar();
    
    std::cout << "DEBUG: Editor render completed successfully" << std::endl;
}

void InteractiveEditor::handle_key(int key, int keycode, bool shift, bool ctrl, bool alt, bool cmd) {
    if (!m_visible) return;
    
    // Handle function keys first
    if (keycode >= 0x7A && keycode <= 0x87) { // F1-F12
        handle_function_key(keycode, shift, ctrl, alt, cmd);
        return;
    }
    
    // Handle special keys
    switch (keycode) {
        case 0x35: // ESC
            hide();
            return;
            
        case 0x24: // Return/Enter
            insert_line();
            return;
            
        case 0x33: // Backspace
            delete_char_backward();
            return;
            
        case 0x75: // Delete
            delete_char_forward();
            return;
            
        case 0x7B: // Left arrow
            move_cursor(0, -1, shift);
            return;
            
        case 0x7C: // Right arrow
            move_cursor(0, 1, shift);
            return;
            
        case 0x7E: // Up arrow
            move_cursor(-1, 0, shift);
            return;
            
        case 0x7D: // Down arrow
            move_cursor(1, 0, shift);
            return;
            
        case 0x30: // Tab
            if (!shift) {
                for (int i = 0; i < m_config.tab_size; i++) {
                    insert_char(' ');
                }
            }
            return;
    }
    
    // Handle control key combinations
    if (ctrl) {
        switch (key) {
            case 'n': case 'N':
                new_file();
                return;
            case 'o': case 'O':
                // TODO: Show file open dialog
                show_status("File open not yet implemented", 120);
                return;
            case 's': case 'S':
                save_file();
                return;
            case 'a': case 'A':
                select_all();
                return;
            case 'z': case 'Z':
                if (shift) {
                    redo();
                } else {
                    undo();
                }
                return;
            case 'c': case 'C':
                copy_selection();
                return;
            case 'x': case 'X':
                cut_selection();
                return;
            case 'v': case 'V':
                // TODO: Get clipboard content
                show_status("Paste not yet implemented", 120);
                return;
        }
    }
    
    // Handle printable characters
    if (key >= 32 && key <= 126) {
        insert_char((char)key);
    }
}

void InteractiveEditor::handle_function_key(int keycode, bool shift, bool ctrl, bool alt, bool cmd) {
    switch (keycode) {
        case 0x7A: // F1
            toggle();
            break;
            
        case 0x78: // F2
            execute_lua();
            break;
            
        case 0x63: // F3
            // Reserved for find
            show_status("Find not yet implemented", 120);
            break;
            
        case 0x76: // F4
            // Reserved for replace
            show_status("Replace not yet implemented", 120);
            break;
            
        case 0x60: // F5
            // Reserved for run/debug
            show_status("Debug mode not yet implemented", 120);
            break;
            
        case 0x61: // F6
            // Reserved for step
            break;
            
        case 0x62: // F7
            // Reserved for breakpoint
            break;
            
        case 0x64: // F8
            save_file();
            break;
            
        case 0x65: // F9
            // Reserved for compile
            break;
            
        case 0x6D: // F10
            // Reserved for menu
            break;
            
        case 0x67: // F11
            // Reserved for full screen
            break;
            
        case 0x6F: // F12
            // Reserved for help
            show_status("F1:Toggle F2:Run F8:Save ESC:Exit Ctrl+N:New Ctrl+S:Save", 300);
            break;
    }
}

void InteractiveEditor::insert_char(char c) {
    if (m_cursor.line >= m_lines.size()) {
        m_lines.resize(m_cursor.line + 1);
    }
    
    std::string& line = m_lines[m_cursor.line];
    if (m_cursor.column > line.length()) {
        m_cursor.column = line.length();
    }
    
    // Record undo operation
    EditorOperation op;
    op.type = EditorOperation::INSERT_CHAR;
    op.line = m_cursor.line;
    op.column = m_cursor.column;
    op.text_before = "";
    op.text_after = std::string(1, c);
    op.cursor_before = m_cursor;
    
    line.insert(m_cursor.column, 1, c);
    m_cursor.column++;
    m_modified = true;
    
    op.cursor_after = m_cursor;
    push_undo_operation(op);
    
    ensure_cursor_visible();
}

void InteractiveEditor::insert_text(const std::string& text) {
    for (char c : text) {
        if (c == '\n') {
            insert_line();
        } else {
            insert_char(c);
        }
    }
}

void InteractiveEditor::delete_char_backward() {
    if (m_cursor.column > 0) {
        // Delete character in current line
        std::string& line = m_lines[m_cursor.line];
        
        EditorOperation op;
        op.type = EditorOperation::DELETE_CHAR;
        op.line = m_cursor.line;
        op.column = m_cursor.column - 1;
        op.text_before = std::string(1, line[m_cursor.column - 1]);
        op.text_after = "";
        op.cursor_before = m_cursor;
        
        line.erase(m_cursor.column - 1, 1);
        m_cursor.column--;
        m_modified = true;
        
        op.cursor_after = m_cursor;
        push_undo_operation(op);
        
    } else if (m_cursor.line > 0) {
        // Join with previous line
        std::string current_line = m_lines[m_cursor.line];
        std::string& prev_line = m_lines[m_cursor.line - 1];
        
        EditorOperation op;
        op.type = EditorOperation::DELETE_LINE;
        op.line = m_cursor.line;
        op.column = 0;
        op.text_before = current_line;
        op.text_after = "";
        op.cursor_before = m_cursor;
        
        m_cursor.column = prev_line.length();
        prev_line += current_line;
        m_lines.erase(m_lines.begin() + m_cursor.line);
        m_cursor.line--;
        m_modified = true;
        
        op.cursor_after = m_cursor;
        push_undo_operation(op);
    }
    
    ensure_cursor_visible();
}

void InteractiveEditor::delete_char_forward() {
    if (m_cursor.line >= m_lines.size()) return;
    
    std::string& line = m_lines[m_cursor.line];
    if (m_cursor.column < line.length()) {
        // Delete character in current line
        EditorOperation op;
        op.type = EditorOperation::DELETE_CHAR;
        op.line = m_cursor.line;
        op.column = m_cursor.column;
        op.text_before = std::string(1, line[m_cursor.column]);
        op.text_after = "";
        op.cursor_before = m_cursor;
        
        line.erase(m_cursor.column, 1);
        m_modified = true;
        
        op.cursor_after = m_cursor;
        push_undo_operation(op);
        
    } else if (m_cursor.line < m_lines.size() - 1) {
        // Join with next line
        std::string next_line = m_lines[m_cursor.line + 1];
        
        EditorOperation op;
        op.type = EditorOperation::DELETE_LINE;
        op.line = m_cursor.line + 1;
        op.column = 0;
        op.text_before = next_line;
        op.text_after = "";
        op.cursor_before = m_cursor;
        
        line += next_line;
        m_lines.erase(m_lines.begin() + m_cursor.line + 1);
        m_modified = true;
        
        op.cursor_after = m_cursor;
        push_undo_operation(op);
    }
}

void InteractiveEditor::insert_line() {
    if (m_cursor.line >= m_lines.size()) {
        m_lines.resize(m_cursor.line + 1);
    }
    
    std::string& current_line = m_lines[m_cursor.line];
    std::string remaining_text = current_line.substr(m_cursor.column);
    current_line = current_line.substr(0, m_cursor.column);
    
    // Calculate auto-indentation
    int indent = 0;
    if (m_config.auto_indent) {
        indent = calculate_auto_indent(m_cursor.line);
    }
    
    std::string new_line = std::string(indent, ' ') + remaining_text;
    
    EditorOperation op;
    op.type = EditorOperation::INSERT_LINE;
    op.line = m_cursor.line + 1;
    op.column = 0;
    op.text_before = "";
    op.text_after = new_line;
    op.cursor_before = m_cursor;
    
    m_lines.insert(m_lines.begin() + m_cursor.line + 1, new_line);
    m_cursor.line++;
    m_cursor.column = indent;
    m_modified = true;
    
    op.cursor_after = m_cursor;
    push_undo_operation(op);
    
    ensure_cursor_visible();
}

void InteractiveEditor::move_cursor(int delta_line, int delta_column, bool extend_selection) {
    if (!extend_selection) {
        m_cursor.clear_selection();
    } else if (!m_cursor.has_selection()) {
        // Start selection
        m_cursor.selection_start_line = m_cursor.line;
        m_cursor.selection_start_column = m_cursor.column;
    }
    
    m_cursor.line += delta_line;
    m_cursor.column += delta_column;
    
    clamp_cursor();
    
    if (extend_selection) {
        m_cursor.selection_end_line = m_cursor.line;
        m_cursor.selection_end_column = m_cursor.column;
    }
    
    ensure_cursor_visible();
}

void InteractiveEditor::set_cursor_position(int line, int column, bool extend_selection) {
    if (!extend_selection) {
        m_cursor.clear_selection();
    } else if (!m_cursor.has_selection()) {
        m_cursor.selection_start_line = m_cursor.line;
        m_cursor.selection_start_column = m_cursor.column;
    }
    
    m_cursor.line = line;
    m_cursor.column = column;
    
    clamp_cursor();
    
    if (extend_selection) {
        m_cursor.selection_end_line = m_cursor.line;
        m_cursor.selection_end_column = m_cursor.column;
    }
    
    ensure_cursor_visible();
}

bool InteractiveEditor::execute_lua() {
    if (!ensure_lua_initialized()) {
        show_status("Failed to initialize Lua runtime!", 180);
        return false;
    }
    
    std::string code = get_content();
    if (code.empty()) {
        show_status("Nothing to execute", 120);
        return true;
    }
    
    std::cout << "InteractiveEditor: Executing Lua code (" << code.length() << " chars)" << std::endl;
    
    // Execute the Lua code
    bool success = exec_lua(code.c_str());
    
    if (success) {
        show_status("Lua code executed successfully! Check terminal output.", 180);
        std::cout << "InteractiveEditor: Lua execution completed successfully" << std::endl;
    } else {
        const char* error = lua_get_error();
        if (error) {
            m_last_lua_error = error;
            show_status(std::string("Lua Error: ") + error, 300);
            std::cout << "InteractiveEditor: Lua execution failed: " << error << std::endl;
        } else {
            show_status("Lua execution failed (unknown error)", 180);
            std::cout << "InteractiveEditor: Lua execution failed with unknown error" << std::endl;
        }
    }
    
    return success;
}

bool InteractiveEditor::save_file(const std::string& filename) {
    std::string save_filename = filename.empty() ? m_filename : filename;
    if (save_filename.empty()) {
        save_filename = m_config.default_filename;
    }
    
    // Expand path
    save_filename = expand_path(save_filename);
    
    try {
        std::ofstream file(save_filename);
        if (!file.is_open()) {
            show_status("Failed to open file for writing: " + save_filename, 180);
            return false;
        }
        
        for (size_t i = 0; i < m_lines.size(); i++) {
            file << m_lines[i];
            if (i < m_lines.size() - 1) {
                file << "\n";
            }
        }
        
        file.close();
        
        m_filename = save_filename;
        m_modified = false;
        show_status("Saved: " + m_filename, 120);
        std::cout << "InteractiveEditor: Saved file: " << m_filename << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        show_status("Save error: " + std::string(e.what()), 180);
        std::cout << "InteractiveEditor: Save failed: " << e.what() << std::endl;
        return false;
    }
}

bool InteractiveEditor::load_file(const std::string& filename) {
    std::string load_filename = expand_path(filename);
    
    try {
        std::ifstream file(load_filename);
        if (!file.is_open()) {
            show_status("Failed to open file: " + load_filename, 180);
            return false;
        }
        
        m_lines.clear();
        std::string line;
        while (std::getline(file, line)) {
            m_lines.push_back(line);
        }
        
        if (m_lines.empty()) {
            m_lines.push_back("");
        }
        
        file.close();
        
        m_filename = load_filename;
        m_modified = false;
        m_cursor.line = 0;
        m_cursor.column = 0;
        m_cursor.clear_selection();
        m_viewport_top = 0;
        m_viewport_left = 0;
        
        // Clear undo/redo stacks
        m_undo_stack.clear();
        m_redo_stack.clear();
        
        show_status("Loaded: " + m_filename, 120);
        std::cout << "InteractiveEditor: Loaded file: " << m_filename << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        show_status("Load error: " + std::string(e.what()), 180);
        std::cout << "InteractiveEditor: Load failed: " << e.what() << std::endl;
        return false;
    }
}

void InteractiveEditor::new_file() {
    if (m_modified) {
        // TODO: Ask user if they want to save changes
        auto_save();
    }
    
    m_lines.clear();
    m_lines.push_back("");
    m_filename = m_config.default_filename;
    m_modified = false;
    m_cursor.line = 0;
    m_cursor.column = 0;
    m_cursor.clear_selection();
    m_viewport_top = 0;
    m_viewport_left = 0;
    
    // Clear undo/redo stacks
    m_undo_stack.clear();
    m_redo_stack.clear();
    
    show_status("New file created", 120);
    std::cout << "InteractiveEditor: Created new file" << std::endl;
}

std::string InteractiveEditor::get_content() const {
    std::stringstream ss;
    for (size_t i = 0; i < m_lines.size(); i++) {
        ss << m_lines[i];
        if (i < m_lines.size() - 1) {
            ss << "\n";
        }
    }
    return ss.str();
}

void InteractiveEditor::set_content(const std::string& content) {
    m_lines.clear();
    
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        m_lines.push_back(line);
    }
    
    if (m_lines.empty()) {
        m_lines.push_back("");
    }
    
    m_cursor.line = 0;
    m_cursor.column = 0;
    m_cursor.clear_selection();
    m_viewport_top = 0;
    m_viewport_left = 0;
    m_modified = true;
    
    // Clear undo/redo stacks
    m_undo_stack.clear();
    m_redo_stack.clear();
}

void InteractiveEditor::show_status(const std::string& message, int timeout_frames) {
    m_status_message = message;
    m_status_message_timeout = timeout_frames;
    std::cout << "InteractiveEditor: " << message << std::endl;
}

// MARK: - Private Helper Methods

void InteractiveEditor::clamp_cursor() {
    if (m_cursor.line < 0) {
        m_cursor.line = 0;
    }
    if (m_cursor.line >= m_lines.size()) {
        m_cursor.line = m_lines.size() - 1;
    }
    
    if (m_cursor.line < m_lines.size()) {
        const std::string& line = m_lines[m_cursor.line];
        if (m_cursor.column < 0) {
            m_cursor.column = 0;
        }
        if (m_cursor.column > line.length()) {
            m_cursor.column = line.length();
        }
    }
}

void InteractiveEditor::ensure_cursor_visible() {
    // Vertical scrolling
    int text_height = m_display_height - 1; // Reserve bottom line for status
    
    if (m_cursor.line < m_viewport_top) {
        m_viewport_top = m_cursor.line;
    }
    if (m_cursor.line >= m_viewport_top + text_height) {
        m_viewport_top = m_cursor.line - text_height + 1;
    }
    
    // Horizontal scrolling
    int line_number_width = m_config.show_line_numbers ? 5 : 0;
    int text_width = m_display_width - line_number_width;
    
    if (m_cursor.column < m_viewport_left) {
        m_viewport_left = m_cursor.column;
    }
    if (m_cursor.column >= m_viewport_left + text_width) {
        m_viewport_left = m_cursor.column - text_width + 1;
    }
}

int InteractiveEditor::calculate_auto_indent(int line_index) const {
    if (line_index < 0 || line_index >= m_lines.size()) {
        return 0;
    }
    
    const std::string& line = m_lines[line_index];
    int indent = 0;
    
    // Count leading whitespace
    for (char c : line) {
        if (c == ' ') {
            indent++;
        } else if (c == '\t') {
            indent += m_config.tab_size;
        } else {
            break;
        }
    }
    
    // Check for Lua block keywords that increase indentation
    std::string trimmed = line;
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    
    if (trimmed.find("function") == 0 ||
        trimmed.find("if") == 0 ||
        trimmed.find("for") == 0 ||
        trimmed.find("while") == 0 ||
        trimmed.find("repeat") == 0 ||
        trimmed.find("do") == 0) {
        indent += m_config.tab_size;
    }
    
    return indent;
}

uint32_t InteractiveEditor::get_syntax_color(int line, int column) const {
    if (!m_config.syntax_highlighting) {
        return COLOR_TEXT_NORMAL;
    }
    
    if (line < 0 || line >= m_lines.size() || column < 0) {
        return COLOR_TEXT_NORMAL;
    }
    
    const std::string& line_text = m_lines[line];
    if (column >= line_text.length()) {
        return COLOR_TEXT_NORMAL;
    }
    
    // Simple syntax highlighting
    char c = line_text[column];
    
    // Check for comments
    if (column > 0 && line_text.substr(0, column + 1).find("--") != std::string::npos) {
        size_t comment_pos = line_text.find("--");
        if (column >= comment_pos) {
            return COLOR_TEXT_COMMENT;
        }
    }
    
    // Check for strings
    // TODO: Implement proper string detection
    
    // Check for numbers
    if (isdigit(c)) {
        return COLOR_TEXT_NUMBER;
    }
    
    // Check for Lua keywords
    // TODO: Implement proper keyword detection
    
    return COLOR_TEXT_NORMAL;
}

void InteractiveEditor::push_undo_operation(const EditorOperation& op) {
    m_undo_stack.push_back(op);
    if (m_undo_stack.size() > MAX_UNDO_OPERATIONS) {
        m_undo_stack.erase(m_undo_stack.begin());
    }
    
    // Clear redo stack on new operation
    m_redo_stack.clear();
}

void InteractiveEditor::render_line_with_syntax(int line_index, int screen_y, int start_column, int end_column) {
    // Safety checks
    if (line_index < 0 || line_index >= (int)m_lines.size()) return;
    if (screen_y < 0 || screen_y >= m_display_height) return;
    if (start_column < 0 || end_column < 0) return;
    
    std::cout << "DEBUG: render_line_with_syntax line=" << line_index << " screen_y=" << screen_y 
              << " start_col=" << start_column << " end_col=" << end_column << std::endl;
    
    const std::string& line = m_lines[line_index];
    int line_number_width = m_config.show_line_numbers ? 5 : 0;
    
    std::cout << "DEBUG: line content (first 50 chars): '" << line.substr(0, 50) << "'" << std::endl;
    std::cout << "DEBUG: line length=" << line.length() << " line_number_width=" << line_number_width << std::endl;
    
    // Render visible portion of line
    std::string visible_text;
    int line_len = (int)line.length();
    int actual_end = std::min(end_column, line_len);
    
    if (start_column < line_len && start_column < actual_end) {
        int substr_len = actual_end - start_column;
        if (substr_len > 0 && substr_len <= line_len) {
            visible_text = line.substr(start_column, substr_len);
        }
    }
    
    std::cout << "DEBUG: About to call editor_set_color" << std::endl;
    // For now, use simple coloring
    editor_set_color(COLOR_TEXT_NORMAL, 0x00000000);
    
    std::cout << "DEBUG: About to call editor_print_at with text: '" << visible_text.substr(0, 30) << "'" << std::endl;
    editor_print_at(line_number_width, screen_y, visible_text.c_str());
    std::cout << "DEBUG: editor_print_at completed" << std::endl;
        editor_print_at(print_x, screen_y, visible_text.c_str());
    }
}

void InteractiveEditor::render_line_numbers() {
    if (!m_config.show_line_numbers) return;
    
    int text_height = m_display_height - 1;
    if (text_height <= 0) return;
    
    editor_set_color(COLOR_LINE_NUMBERS, 0x00000000);
    
    for (int screen_y = 0; screen_y < text_height; screen_y++) {
        int line_index = m_viewport_top + screen_y;
        if (line_index >= 0 && line_index < (int)m_lines.size()) {
            char line_num[8];
            snprintf(line_num, sizeof(line_num), "%4d ", line_index + 1);
            if (screen_y >= 0 && screen_y < m_display_height) {
                editor_print_at(0, screen_y, line_num);
            }
        }
    }
}

void InteractiveEditor::render_status_bar() {
    int status_y = m_display_height - 1;
    if (status_y < 0 || status_y >= m_display_height) return;
    if (m_display_width <= 0) return;
    
    // Clear status line with bounds check
    editor_set_color(COLOR_TEXT_NORMAL, COLOR_STATUS_BAR);
    int safe_width = std::max(1, std::min(m_display_width, 200)); // Cap at reasonable size
    std::string status_line(safe_width, ' ');
    editor_print_at(0, status_y, status_line.c_str());
    
    // Show status message or default info
    std::string status_text;
    if (!m_status_message.empty()) {
        status_text = m_status_message;
    } else {
        char pos_info[64];
        int safe_line = std::max(0, m_cursor.line);
        int safe_col = std::max(0, m_cursor.column);
        snprintf(pos_info, sizeof(pos_info), "Line %d, Col %d", 
                safe_line + 1, safe_col + 1);
        
        status_text = m_filename;
        if (m_modified) {
            status_text += " *";
        }
        status_text += " | ";
        status_text += pos_info;
        status_text += " | F2:Run F8:Save";
    }
    
    // Truncate if too long with bounds check
    if ((int)status_text.length() > safe_width && safe_width > 3) {
        status_text = status_text.substr(0, safe_width - 3) + "...";
    } else if ((int)status_text.length() > safe_width) {
        status_text = status_text.substr(0, safe_width);
    }
    
    editor_set_color(COLOR_TEXT_NORMAL, COLOR_STATUS_BAR);
    editor_print_at(0, status_y, status_text.c_str());
}

void InteractiveEditor::render_cursor() {
    if (!m_visible) return;
    
    // Calculate cursor screen position
    int line_number_width = m_config.show_line_numbers ? 5 : 0;
    int cursor_screen_x = line_number_width + (m_cursor.column - m_viewport_left);
    int cursor_screen_y = m_cursor.line - m_viewport_top;
    
    // Check if cursor is visible
    int text_width = m_display_width - line_number_width;
    int text_height = m_display_height - 1;
    
    if (cursor_screen_x >= line_number_width && 
        cursor_screen_x < m_display_width &&
        cursor_screen_y >= 0 && 
        cursor_screen_y < text_height) {
        
        // Render cursor as inverse character
        char cursor_char = ' ';
        if (m_cursor.line < m_lines.size() && 
            m_cursor.column < m_lines[m_cursor.line].length()) {
            cursor_char = m_lines[m_cursor.line][m_cursor.column];
        }
        
        editor_set_color(COLOR_BACKGROUND, COLOR_CURSOR);
        editor_print_at(cursor_screen_x, cursor_screen_y, std::string(1, cursor_char).c_str());
    }
}

bool InteractiveEditor::ensure_lua_initialized() {
    if (!m_lua_initialized) {
        m_lua_initialized = lua_init();
        if (m_lua_initialized) {
            std::cout << "InteractiveEditor: Lua runtime initialized" << std::endl;
        } else {
            std::cout << "InteractiveEditor: Failed to initialize Lua runtime" << std::endl;
        }
    }
    return m_lua_initialized;
}

std::string InteractiveEditor::expand_path(const std::string& path) const {
    if (path.empty()) {
        return path;
    }
    
    std::string expanded = path;
    
    // Handle ~ expansion
    if (expanded[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            expanded = std::string(home) + expanded.substr(1);
        }
    }
    
    return expanded;
}

void InteractiveEditor::undo() {
    // TODO: Implement undo functionality
    show_status("Undo not yet implemented", 120);
}

void InteractiveEditor::redo() {
    // TODO: Implement redo functionality
    show_status("Redo not yet implemented", 120);
}

void InteractiveEditor::select_all() {
    if (m_lines.empty()) return;
    
    m_cursor.selection_start_line = 0;
    m_cursor.selection_start_column = 0;
    m_cursor.selection_end_line = m_lines.size() - 1;
    m_cursor.selection_end_column = m_lines.back().length();
    
    show_status("All text selected", 120);
}

std::string InteractiveEditor::copy_selection() {
    // TODO: Implement selection copy
    show_status("Copy not yet implemented", 120);
    return "";
}

std::string InteractiveEditor::cut_selection() {
    // TODO: Implement selection cut
    show_status("Cut not yet implemented", 120);
    return "";
}

void InteractiveEditor::paste_text(const std::string& text) {
    // TODO: Implement paste
    show_status("Paste not yet implemented", 120);
}

void InteractiveEditor::auto_save() {
    if (!m_modified) return;
    
    std::string auto_save_path = expand_path("~/.superterminal_autosave.lua");
    
    try {
        std::ofstream file(auto_save_path);
        if (file.is_open()) {
            for (const auto& line : m_lines) {
                file << line << "\n";
            }
            file.close();
            std::cout << "InteractiveEditor: Auto-saved to " << auto_save_path << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "InteractiveEditor: Auto-save failed: " << e.what() << std::endl;
    }
}

const std::string& InteractiveEditor::get_line(size_t line_index) const {
    static const std::string empty_line = "";
    if (line_index >= m_lines.size()) {
        return empty_line;
    }
    return m_lines[line_index];
}

void InteractiveEditor::set_display_size(int width, int height) {
    m_display_width = width;
    m_display_height = height;
    ensure_cursor_visible();
}

bool InteractiveEditor::execute_lua_selection() {
    // TODO: Implement selection execution
    show_status("Execute selection not yet implemented", 120);
    return false;
}

void InteractiveEditor::delete_line() {
    // TODO: Implement line deletion
    show_status("Delete line not yet implemented", 120);
}

// MARK: - C API Functions

extern "C" {

void interactive_editor_init() {
    if (!g_interactive_editor) {
        g_interactive_editor = std::make_unique<InteractiveEditor>();
        std::cout << "InteractiveEditor: System initialized" << std::endl;
    }
}

void interactive_editor_cleanup() {
    if (g_interactive_editor) {
        g_interactive_editor.reset();
        std::cout << "InteractiveEditor: System cleaned up" << std::endl;
    }
}

void interactive_editor_toggle() {
    if (g_interactive_editor) {
        g_interactive_editor->toggle();
    }
}

void interactive_editor_update() {
    if (g_interactive_editor) {
        g_interactive_editor->update();
    }
}

void interactive_editor_handle_key(int key, int keycode, bool shift, bool ctrl, bool alt, bool cmd) {
    if (g_interactive_editor) {
        g_interactive_editor->handle_key(key, keycode, shift, ctrl, alt, cmd);
    }
}

bool interactive_editor_execute() {
    if (g_interactive_editor) {
        return g_interactive_editor->execute_lua();
    }
    return false;
}

bool interactive_editor_save() {
    if (g_interactive_editor) {
        return g_interactive_editor->save_file();
    }
    return false;
}

bool interactive_editor_is_visible() {
    if (g_interactive_editor) {
        return g_interactive_editor->is_visible();
    }
    return false;
}

bool interactive_editor_load_file(const char* filename) {
    if (g_interactive_editor && filename) {
        return g_interactive_editor->load_file(filename);
    }
    return false;
}

const char* interactive_editor_get_content() {
    if (g_interactive_editor) {
        static std::string content;
        content = g_interactive_editor->get_content();
        return content.c_str();
    }
    return "";
}

void interactive_editor_set_content(const char* content) {
    if (g_interactive_editor && content) {
        g_interactive_editor->set_content(content);
    }
}

} // extern "C"

} // namespace SuperTerminal