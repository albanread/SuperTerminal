//
// ReplConsole.cpp
// SuperTerminal - Interactive REPL Console System (Text-Based UI)
//
// Implementation of dedicated REPL (Read-Eval-Print Loop) console
// Uses the bottom 6 lines of the editor text layer with PETSCII box drawing
//

#include "ReplConsole.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>

// Forward declarations for SuperTerminal integration
extern "C" {
    // Editor text layer functions (Layer 6)
    void editor_cls();
    void editor_print_at(int x, int y, const char* text);
    void editor_set_color(uint32_t ink, uint32_t paper);
    void editor_background_color(uint32_t color);
    
    // Layer management
    void layer_set_enabled(int layer, bool enabled);
    bool layer_is_enabled(int layer);
    
    // Editor viewport control
    void coretext_editor_set_viewport(int startRow, int rowCount);
    
    // Text mode query
    int text_mode_get_rows(void);
    
    // Adaptive grid dimensions
    void text_grid_get_dimensions(int* width, int* height);
    
    // Lua GCD Runtime execution
    bool lua_gcd_exec_repl(const char* lua_code);
    const char* lua_gcd_get_last_error();
    void lua_gcd_reset_repl();
}

// Global instance
static ReplConsole* g_repl_instance = nullptr;

ReplConsole::ReplConsole()
    : m_initialized(false)
    , m_active(false)
    , m_screen_saved(false)
    , m_current_input("")
    , m_cursor_pos(0)
    , m_prompt("lua> ")
    , m_history_index(-1)
    , m_history_temp_input("")
    , m_scroll_offset(0)
    , m_cursor_blink_timer(0.0f)
    , m_cursor_visible(true)
    , m_needs_redraw(true)
{
    std::memset(m_saved_screen, 0, sizeof(m_saved_screen));
}

ReplConsole::~ReplConsole() {
    if (m_initialized) {
        shutdown();
    }
}



int ReplConsole::get_repl_start_row() const {
    // Get current grid height from TextGridManager (adaptive)
    int gridWidth, gridHeight;
    text_grid_get_dimensions(&gridWidth, &gridHeight);
    
    // REPL occupies bottom 6 lines plus 1 for border
    return gridHeight - REPL_LINES - 1;
}

bool ReplConsole::initialize() {
    if (m_initialized) {
        return true;
    }
    
    std::cout << "ReplConsole: Initializing text-based REPL system..." << std::endl;
    int replStartRow = get_repl_start_row();
    std::cout << "ReplConsole: Using bottom 6 lines (starting at row " << replStartRow << ") of adaptive grid" << std::endl;
    
    // Initialize with minimal status
    set_status_message("Ready");
    
    m_initialized = true;
    m_needs_redraw = true;
    
    std::cout << "ReplConsole: Initialization complete" << std::endl;
    return true;
}

void ReplConsole::shutdown() {
    if (!m_initialized) {
        return;
    }
    
    std::cout << "ReplConsole: Shutting down..." << std::endl;
    
    deactivate();
    clear_history();
    clear_output();
    
    m_initialized = false;
    std::cout << "ReplConsole: Shutdown complete" << std::endl;
}

void ReplConsole::activate() {
    if (!m_initialized) {
        if (!initialize()) {
            return;
        }
    }
    
    if (m_active) {
        return;
    }
    
    std::cout << "ReplConsole: Activating text-based REPL console" << std::endl;
    
    m_active = true;
    m_needs_redraw = true;
    m_cursor_blink_timer = 0.0f;
    m_cursor_visible = true;
    
    // Enable Layer 6 (editor text layer) for REPL display
    std::cout << "ReplConsole: Enabling Layer 6..." << std::endl;
    layer_set_enabled(6, true);
    std::cout << "ReplConsole: Layer 6 enabled, checking status: " 
              << (layer_is_enabled(6) ? "ENABLED" : "DISABLED") << std::endl;
    
    // Calculate REPL start row dynamically based on current text mode
    int totalRows = text_mode_get_rows();
    int replStartRow = totalRows - REPL_LINES;
    int replEndRow = totalRows - 1;
    
    std::cout << "ReplConsole: Setting editor viewport to rows " << replStartRow << "-" << replEndRow << std::endl;
    coretext_editor_set_viewport(replStartRow, REPL_LINES);
    
    // Clear the REPL area
    std::cout << "ReplConsole: Clearing REPL area..." << std::endl;
    editor_cls();
    uint32_t transparent = make_color(0, 0, 0, 0);  // Fully transparent
    editor_background_color(transparent);
    
    // Save the current screen content in the REPL area
    save_screen_area();
    
    // Draw the REPL UI
    render();
    
    std::cout << "ReplConsole: REPL activation complete" << std::endl;
}

void ReplConsole::deactivate() {
    if (!m_active) {
        return;
    }
    
    std::cout << "ReplConsole: Deactivating REPL console" << std::endl;
    
    // Clear the REPL area with transparent background
    int totalRows = text_mode_get_rows();
    int replStartRow = totalRows - REPL_LINES;
    int replEndRow = totalRows - 1;
    
    uint32_t transparent = make_color(0, 0, 0, 0);
    for (int y = replStartRow; y <= replEndRow; y++) {
        clear_line(y, transparent);
    }
    
    // Restore the saved screen content
    restore_screen_area();
    
    // Reset viewport to full screen using current text mode
    totalRows = text_mode_get_rows();
    std::cout << "ReplConsole: Resetting editor viewport to full screen (0-" << totalRows << ")" << std::endl;
    coretext_editor_set_viewport(0, totalRows);
    
    // Disable Layer 6 (editor text layer)
    std::cout << "ReplConsole: Disabling Layer 6..." << std::endl;
    layer_set_enabled(6, false);
    
    m_active = false;
    m_needs_redraw = false;
    
    // Clear any pending input
    clear_current_input();
    m_history_index = -1;
    m_history_temp_input.clear();
    
    std::cout << "ReplConsole: REPL deactivation complete" << std::endl;
}

void ReplConsole::toggle() {
    if (m_active) {
        deactivate();
    } else {
        activate();
    }
}

void ReplConsole::handle_key_input(int key, int modifiers) {
    if (!m_active) {
        return;
    }
    
    std::cout << "ReplConsole::handle_key_input - key=0x" << std::hex << key << std::dec 
              << " modifiers=0x" << std::hex << modifiers << std::dec << std::endl;
    
    // Key codes (macOS hex keycodes)
    const int KEY_RETURN = 0x24;      // Return/Enter
    const int KEY_BACKSPACE = 0x33;   // Backspace
    const int KEY_DELETE = 0x75;      // Forward Delete
    const int KEY_LEFT = 0x7B;        // Left Arrow
    const int KEY_RIGHT = 0x7C;       // Right Arrow
    const int KEY_UP = 0x7E;          // Up Arrow
    const int KEY_DOWN = 0x7D;        // Down Arrow
    const int KEY_HOME = 0x73;        // Home
    const int KEY_END = 0x77;         // End
    const int KEY_ESCAPE = 0x35;      // Escape
    const int KEY_TAB = 0x30;         // Tab
    
    bool ctrl = (modifiers & 0x40000) != 0;   // Control
    bool shift = (modifiers & 0x20000) != 0;  // Shift
    bool alt = (modifiers & 0x80000) != 0;    // Alt/Option
    bool cmd = (modifiers & 0x100000) != 0;   // Command
    
    switch (key) {
        case KEY_RETURN:
            if (ctrl) {
                // Ctrl+Return executes the command
                std::cout << "ReplConsole: Ctrl+RETURN detected - executing command" << std::endl;
                execute_current_command();
            } else {
                // Plain Return adds a newline for multi-line input
                std::cout << "ReplConsole: RETURN detected - adding newline" << std::endl;
                insert_character('\n');
            }
            break;
            
        case KEY_BACKSPACE:
            backspace();
            break;
            
        case KEY_DELETE:
            delete_character();
            break;
            
        case KEY_LEFT:
            move_cursor_left();
            break;
            
        case KEY_RIGHT:
            move_cursor_right();
            break;
            
        case KEY_UP:
            history_previous();
            break;
            
        case KEY_DOWN:
            history_next();
            break;
            
        case KEY_HOME:
            move_cursor_home();
            break;
            
        case KEY_END:
            move_cursor_end();
            break;
            
        case KEY_ESCAPE:
            if (ctrl) {
                clear_current_input();
            }
            break;
            
        default:
            // Ignore other special keys
            break;
    }
}

void ReplConsole::handle_character_input(char ch) {
    if (!m_active) {
        return;
    }
    
    std::cout << "ReplConsole::handle_character_input - ch='" << ch << "' (0x" 
              << std::hex << (int)(unsigned char)ch << std::dec << ")" << std::endl;
    
    // Only accept printable characters
    if (ch >= 32 && ch < 127) {
        insert_character(ch);
    }
}

void ReplConsole::update(float delta_time) {
    if (!m_initialized || !m_active) {
        return;
    }
    
    update_cursor_blink(delta_time);
    
    if (m_needs_redraw) {
        render();
    }
}

void ReplConsole::render() {
    std::cout << "ReplConsole::render() called - initialized=" << m_initialized 
              << " active=" << m_active << " needs_redraw=" << m_needs_redraw << std::endl;
    
    if (!m_initialized || !m_active) {
        std::cout << "ReplConsole::render() - skipping (not initialized or not active)" << std::endl;
        return;
    }
    
    if (!m_needs_redraw) {
        std::cout << "ReplConsole::render() - skipping (no redraw needed)" << std::endl;
        return;
    }
    
    std::cout << "ReplConsole::render() - rendering REPL components..." << std::endl;
    
    // Render REPL components
    render_box();
    std::cout << "ReplConsole::render() - box rendered" << std::endl;
    
    render_status_line();
    std::cout << "ReplConsole::render() - status line rendered" << std::endl;
    
    render_content_area();
    std::cout << "ReplConsole::render() - content area rendered" << std::endl;
    
    render_input_line();
    std::cout << "ReplConsole::render() - input line rendered" << std::endl;
    
    render_cursor();
    std::cout << "ReplConsole::render() - cursor rendered" << std::endl;
    
    m_needs_redraw = false;
    std::cout << "ReplConsole::render() - complete" << std::endl;
}

void ReplConsole::execute_current_command() {
    std::cout << "ReplConsole::execute_current_command() called" << std::endl;
    std::cout << "  m_current_input: '" << m_current_input << "'" << std::endl;
    std::cout << "  input length: " << m_current_input.length() << std::endl;
    
    if (m_current_input.empty()) {
        std::cout << "ReplConsole: Cannot execute - input is empty" << std::endl;
        return;
    }
    
    std::cout << "ReplConsole: Executing command: '" << m_current_input << "'" << std::endl;
    
    std::string command = m_current_input;
    
    // Add command to output display
    add_output_line(m_prompt + command);
    
    // Add to history if it's not empty and different from last command
    if (!command.empty() && 
        (m_command_history.empty() || m_command_history.back() != command)) {
        add_to_history(command);
    }
    
    // Execute the command
    execute_command(command);
    
    // Clear input
    clear_current_input();
    m_history_index = -1;
    m_history_temp_input.clear();
    
    m_needs_redraw = true;
}

void ReplConsole::execute_command(const std::string& command) {
    // Wrap command with start_of_repl and end_of_repl for persistent state
    std::string wrapped_command = "start_of_repl(\"interactive\")\n" + command + "\nend_of_repl(\"interactive\")";
    
    std::cout << "ReplConsole: Executing REPL command" << std::endl;
    
    bool success = lua_gcd_exec_repl(wrapped_command.c_str());
    
    if (success) {
        set_status_message("OK");
        std::cout << "ReplConsole: Command executed successfully" << std::endl;
    } else {
        const char* error = lua_gcd_get_last_error();
        if (error && strlen(error) > 0) {
            set_status_message("Error: " + std::string(error));
            std::cout << "ReplConsole: Lua error: " << error << std::endl;
        } else {
            set_status_message("Error");
            std::cout << "ReplConsole: Command execution failed - no error message" << std::endl;
        }
    }
}

void ReplConsole::add_to_history(const std::string& command) {
    // Remove duplicates
    auto it = std::find(m_command_history.begin(), m_command_history.end(), command);
    if (it != m_command_history.end()) {
        m_command_history.erase(it);
    }
    
    // Add to end
    m_command_history.push_back(command);
    
    // Limit size
    if (m_command_history.size() > MAX_HISTORY_SIZE) {
        m_command_history.erase(m_command_history.begin());
    }
}

void ReplConsole::history_previous() {
    if (m_command_history.empty()) {
        return;
    }
    
    // Save current input if we're not already browsing
    if (m_history_index == -1) {
        m_history_temp_input = m_current_input;
        m_history_index = m_command_history.size() - 1;
    } else if (m_history_index > 0) {
        m_history_index--;
    }
    
    m_current_input = m_command_history[m_history_index];
    m_cursor_pos = m_current_input.length();
    m_needs_redraw = true;
}

void ReplConsole::history_next() {
    if (m_history_index == -1) {
        return; // Not browsing history
    }
    
    if (m_history_index < (int)m_command_history.size() - 1) {
        m_history_index++;
        m_current_input = m_command_history[m_history_index];
    } else {
        // Restore original input
        m_current_input = m_history_temp_input;
        m_history_index = -1;
    }
    
    m_cursor_pos = m_current_input.length();
    m_needs_redraw = true;
}

void ReplConsole::add_output_line(const std::string& line) {
    wrap_and_add_line(line);
    
    // Limit buffer size
    while (m_output_lines.size() > MAX_OUTPUT_LINES) {
        m_output_lines.pop_front();
    }
    
    // Auto-scroll to bottom unless user has scrolled up
    if (m_scroll_offset == 0) {
        clamp_scroll_offset();
    }
    
    m_needs_redraw = true;
}

void ReplConsole::clear_output() {
    m_output_lines.clear();
    m_scroll_offset = 0;
    m_needs_redraw = true;
}

void ReplConsole::scroll_up() {
    m_scroll_offset++;
    clamp_scroll_offset();
    m_needs_redraw = true;
}

void ReplConsole::scroll_down() {
    m_scroll_offset--;
    clamp_scroll_offset();
    m_needs_redraw = true;
}

void ReplConsole::clear_history() {
    m_command_history.clear();
    m_history_index = -1;
    m_history_temp_input.clear();
}

void ReplConsole::update_cursor_blink(float delta_time) {
    m_cursor_blink_timer += delta_time;
    if (m_cursor_blink_timer >= CURSOR_BLINK_RATE) {
        m_cursor_visible = !m_cursor_visible;
        m_cursor_blink_timer = 0.0f;
        m_needs_redraw = true;
    }
}

void ReplConsole::insert_character(char ch) {
    m_current_input.insert(m_cursor_pos, 1, ch);
    m_cursor_pos++;
    m_cursor_visible = true;
    m_cursor_blink_timer = 0.0f;
    m_needs_redraw = true;
}

void ReplConsole::delete_character() {
    if (m_cursor_pos < (int)m_current_input.length()) {
        m_current_input.erase(m_cursor_pos, 1);
        m_cursor_visible = true;
        m_cursor_blink_timer = 0.0f;
        m_needs_redraw = true;
    }
}

void ReplConsole::backspace() {
    if (m_cursor_pos > 0) {
        m_current_input.erase(m_cursor_pos - 1, 1);
        m_cursor_pos--;
        m_cursor_visible = true;
        m_cursor_blink_timer = 0.0f;
        m_needs_redraw = true;
    }
}

void ReplConsole::move_cursor_left() {
    if (m_cursor_pos > 0) {
        m_cursor_pos--;
        m_cursor_visible = true;
        m_cursor_blink_timer = 0.0f;
        m_needs_redraw = true;
    }
}

void ReplConsole::move_cursor_right() {
    if (m_cursor_pos < (int)m_current_input.length()) {
        m_cursor_pos++;
        m_cursor_visible = true;
        m_cursor_blink_timer = 0.0f;
        m_needs_redraw = true;
    }
}

void ReplConsole::move_cursor_home() {
    if (m_cursor_pos != 0) {
        m_cursor_pos = 0;
        m_cursor_visible = true;
        m_cursor_blink_timer = 0.0f;
        m_needs_redraw = true;
    }
}

void ReplConsole::move_cursor_end() {
    int new_pos = m_current_input.length();
    if (m_cursor_pos != new_pos) {
        m_cursor_pos = new_pos;
        m_cursor_visible = true;
        m_cursor_blink_timer = 0.0f;
        m_needs_redraw = true;
    }
}

void ReplConsole::clear_current_input() {
    m_current_input.clear();
    m_cursor_pos = 0;
    m_needs_redraw = true;
}

// ============================================================================
// Screen Management
// ============================================================================

void ReplConsole::save_screen_area() {
    // Note: In a real implementation, we would read the editor text buffer
    // For now, we just mark that we've saved (assuming the area will be restored)
    m_screen_saved = true;
    std::cout << "ReplConsole: Screen area saved" << std::endl;
}

void ReplConsole::restore_screen_area() {
    if (!m_screen_saved) {
        return;
    }
    
    // Clear the REPL area
    uint32_t bg_color = make_color(0, 0, 0, 255);
    int replStartRow = get_repl_start_row();
    
    for (int row = replStartRow; row <= replStartRow + REPL_LINES; row++) {
        clear_line(row, bg_color);
    }
    
    m_screen_saved = false;
    std::cout << "ReplConsole: Screen area restored" << std::endl;
}

// ============================================================================
// Rendering
// ============================================================================

void ReplConsole::render_box() {
    std::cout << "ReplConsole::render_box() - drawing box at row " << get_row_top_border() << std::endl;
    
    // Draw PETSCII box border
    uint32_t box_color = make_color(0, 255, 0, 255);  // Green
    uint32_t bg_color = make_color(0, 0, 50, 255);    // Dark blue background
    
    std::cout << "ReplConsole::render_box() - box_color=" << std::hex << box_color 
              << " bg_color=" << bg_color << std::dec << std::endl;
    
    // Top border (row 19)
    int screen_cols, screen_rows;
    text_grid_get_dimensions(&screen_cols, &screen_rows);
    draw_char(0, get_row_top_border(), BOX_TOP_LEFT, box_color, bg_color);
    for (int x = 1; x < screen_cols - 1; x++) {
        draw_char(x, get_row_top_border(), BOX_HORIZONTAL, box_color, bg_color);
    }
    draw_char(screen_cols - 1, get_row_top_border(), BOX_TOP_RIGHT, box_color, bg_color);
    
    // Side borders (rows 20-23)
    for (int y = get_row_first_line(); y < get_row_bottom_border(); y++) {
        draw_char(0, y, BOX_VERTICAL, box_color, bg_color);
        draw_char(screen_cols - 1, y, BOX_VERTICAL, box_color, bg_color);
        
        // Clear interior
        for (int x = 1; x < screen_cols - 1; x++) {
            draw_char(x, y, " ", make_color(255, 255, 255, 255), bg_color);
        }
    }
    
    // Draw bottom border
    draw_char(0, get_row_bottom_border(), BOX_BOTTOM_LEFT, box_color, bg_color);
    for (int x = 1; x < screen_cols - 1; x++) {
        draw_char(x, get_row_bottom_border(), BOX_HORIZONTAL, box_color, bg_color);
    }
    draw_char(screen_cols - 1, get_row_bottom_border(), BOX_BOTTOM_RIGHT, box_color, bg_color);
}

void ReplConsole::render_status_line() {
    // Status appears in the top border (row 19) after the left corner
    uint32_t status_color = make_color(255, 255, 100, 255); // Yellow
    uint32_t bg_color = make_color(0, 0, 50, 255);
    
    int screen_cols, screen_rows;
    text_grid_get_dimensions(&screen_cols, &screen_rows);
    
    // Truncate status to fit in the border
    std::string display_status = " " + m_status_message + " ";
    if (display_status.length() > (size_t)(screen_cols - 4)) {
        display_status = display_status.substr(0, screen_cols - 4);
    }
    
    // Draw status in the top border
    int status_x = 2;
    draw_text(status_x, get_row_top_border(), display_status.c_str(), status_color, bg_color);
}

void ReplConsole::render_content_area() {
    // Content area shows output OR multi-line input
    // If input has newlines, show it across multiple rows
    // Otherwise show recent output
    
    uint32_t text_color = make_color(200, 200, 200, 255);
    uint32_t prompt_color = make_color(100, 255, 100, 255);
    uint32_t input_color = make_color(255, 255, 255, 255);
    uint32_t bg_color = make_color(0, 0, 50, 255);
    
    // Check if we have multi-line input
    bool has_multiline = m_current_input.find('\n') != std::string::npos;
    
    if (has_multiline) {
        // Display multi-line input across all 4 content rows
        std::vector<std::string> input_lines;
        std::istringstream stream(m_current_input);
        std::string line;
        while (std::getline(stream, line)) {
            input_lines.push_back(line);
        }
        
        // Show last 4 lines (or fewer if less than 4)
        int start_idx = std::max(0, (int)input_lines.size() - CONTENT_LINES);
        for (int i = 0; i < CONTENT_LINES; i++) {
            int line_idx = start_idx + i;
            int row = get_row_first_line() + i;
            
            if (line_idx < (int)input_lines.size()) {
                std::string display_line = input_lines[line_idx];
                
                // First line gets prompt
                if (line_idx == 0 && i == 0) {
                    draw_text(2, row, m_prompt.c_str(), prompt_color, bg_color);
                    int screen_cols, screen_rows;
                    text_grid_get_dimensions(&screen_cols, &screen_rows);
                    if (display_line.length() > (size_t)(screen_cols - 8)) {
                        display_line = display_line.substr(0, screen_cols - 8);
                    }
                    draw_text(2 + m_prompt.length(), row, display_line.c_str(), input_color, bg_color);
                } else {
                    // Continuation lines get "... " prefix
                    draw_text(2, row, "... ", prompt_color, bg_color);
                    int screen_cols, screen_rows;
                    text_grid_get_dimensions(&screen_cols, &screen_rows);
                    if (display_line.length() > (size_t)(screen_cols - 8)) {
                        display_line = display_line.substr(0, screen_cols - 8);
                    }
                    draw_text(6, row, display_line.c_str(), input_color, bg_color);
                }
            }
        }
    } else {
        // Show recent output in rows 20-22, reserve row 23 for single-line input
        int available_lines = CONTENT_LINES - 1;
        int start_line = std::max(0, (int)m_output_lines.size() - available_lines);
        
        for (int i = 0; i < available_lines; i++) {
            int line_idx = start_line + i;
            int row = get_row_first_line() + i;
            
            if (line_idx < (int)m_output_lines.size()) {
                std::string line = m_output_lines[line_idx];
                
                int screen_cols, screen_rows;
                text_grid_get_dimensions(&screen_cols, &screen_rows);
                if (line.length() > (size_t)(screen_cols - 3)) {
                    line = line.substr(0, screen_cols - 3);
                }
                
                draw_text(2, row, line.c_str(), text_color, bg_color);
            }
        }
        
        // Draw single-line input on row 23
        int input_row = get_row_bottom_border() - 1;
        draw_text(2, input_row, m_prompt.c_str(), prompt_color, bg_color);
        
        int screen_cols, screen_rows;
        text_grid_get_dimensions(&screen_cols, &screen_rows);
        int input_x = 2 + m_prompt.length();
        int available_width = screen_cols - 3 - m_prompt.length();
        
        std::string display_input = m_current_input;
        if (display_input.length() > (size_t)available_width) {
            int scroll_offset = std::max(0, m_cursor_pos - available_width + 5);
            display_input = display_input.substr(scroll_offset, available_width);
        }
        
        draw_text(input_x, input_row, display_input.c_str(), input_color, bg_color);
    }
}

void ReplConsole::render_input_line() {
    // Input rendering is now handled in render_content_area()
    // This function is kept for compatibility but does nothing
}

void ReplConsole::render_cursor() {
    if (!m_cursor_visible) {
        return;
    }
    
    // Calculate cursor position considering newlines
    int chars_before_cursor = 0;
    int cursor_line = 0;
    int cursor_col = 0;
    
    for (int i = 0; i < m_cursor_pos && i < (int)m_current_input.length(); i++) {
        if (m_current_input[i] == '\n') {
            cursor_line++;
            cursor_col = 0;
        } else {
            cursor_col++;
        }
    }
    
    // Determine which row to draw cursor on
    bool has_multiline = m_current_input.find('\n') != std::string::npos;
    int cursor_row;
    int cursor_x;
    
    if (has_multiline) {
        // Multi-line mode: cursor can be on any of the 4 content rows
        std::vector<std::string> input_lines;
        std::istringstream stream(m_current_input);
        std::string line;
        while (std::getline(stream, line)) {
            input_lines.push_back(line);
        }
        
        int start_idx = std::max(0, (int)input_lines.size() - CONTENT_LINES);
        int visible_line = cursor_line - start_idx;
        
        if (visible_line >= 0 && visible_line < CONTENT_LINES) {
            cursor_row = get_row_first_line() + visible_line;
            
            if (cursor_line == 0) {
                cursor_x = 2 + m_prompt.length() + cursor_col;
            } else {
                cursor_x = 6 + cursor_col; // After "... "
            }
        } else {
            return; // Cursor not visible in current view
        }
    } else {
        // Single-line mode: cursor on row 23
        cursor_row = get_row_bottom_border() - 1;
        cursor_x = 2 + m_prompt.length() + m_cursor_pos;
    }
    
    // Make sure cursor is visible
    int screen_cols, screen_rows;
    text_grid_get_dimensions(&screen_cols, &screen_rows);
    if (cursor_x >= screen_cols - 1) {
        cursor_x = screen_cols - 2;
    }
    
    uint32_t cursor_color = make_color(255, 255, 255, 255);
    uint32_t bg_color = make_color(100, 255, 100, 255); // Inverse
    
    // Draw a solid block cursor
    draw_char(cursor_x, cursor_row, "█", cursor_color, bg_color);
}

// ============================================================================
// Text Drawing Utilities
// ============================================================================

void ReplConsole::draw_text(int x, int y, const char* text, uint32_t ink, uint32_t paper) {
    int screen_cols, screen_rows;
    text_grid_get_dimensions(&screen_cols, &screen_rows);
    if (y < 0 || y >= screen_rows) {
        return;
    }
    
    editor_set_color(ink, paper);
    editor_print_at(x, y, text);
}

void ReplConsole::draw_char(int x, int y, const char* ch, uint32_t ink, uint32_t paper) {
    int screen_cols, screen_rows;
    text_grid_get_dimensions(&screen_cols, &screen_rows);
    if (x < 0 || x >= screen_cols || y < 0 || y >= screen_rows) {
        std::cout << "ReplConsole::draw_char() - out of bounds: x=" << x << " y=" << y 
                  << " (screen=" << screen_cols << "x" << screen_rows << ")" << std::endl;
        return;
    }
    
    static int call_count = 0;
    if (call_count < 5) {
        std::cout << "ReplConsole::draw_char() - x=" << x << " y=" << y 
                  << " ch='" << ch << "' ink=" << std::hex << ink 
                  << " paper=" << paper << std::dec << std::endl;
        call_count++;
    }
    
    editor_set_color(ink, paper);
    editor_print_at(x, y, ch);
}

void ReplConsole::clear_line(int y, uint32_t paper) {
    int screen_cols, screen_rows;
    text_grid_get_dimensions(&screen_cols, &screen_rows);
    if (y < 0 || y >= screen_rows) {
        return;
    }
    
    uint32_t ink = make_color(0, 0, 0, 255);
    editor_set_color(ink, paper);
    
    std::string spaces(screen_cols, ' ');
    editor_print_at(0, y, spaces.c_str());
}

// ============================================================================
// Line Management
// ============================================================================

void ReplConsole::wrap_and_add_line(const std::string& line) {
    // For now, just add the line without wrapping
    // In the future, could wrap long lines
    m_output_lines.push_back(line);
}

std::vector<std::string> ReplConsole::wrap_text(const std::string& text, int max_width) {
    std::vector<std::string> lines;
    
    if (text.length() <= (size_t)max_width) {
        lines.push_back(text);
        return lines;
    }
    
    // Simple word wrapping
    size_t pos = 0;
    while (pos < text.length()) {
        size_t end = std::min(pos + max_width, text.length());
        
        // Try to break at a space
        if (end < text.length()) {
            size_t last_space = text.rfind(' ', end);
            if (last_space != std::string::npos && last_space > pos) {
                end = last_space;
            }
        }
        
        lines.push_back(text.substr(pos, end - pos));
        pos = end;
        
        // Skip leading space on next line
        if (pos < text.length() && text[pos] == ' ') {
            pos++;
        }
    }
    
    return lines;
}

void ReplConsole::clamp_scroll_offset() {
    int max_scroll = std::max(0, (int)m_output_lines.size() - (CONTENT_LINES - 1));
    m_scroll_offset = std::max(0, std::min(m_scroll_offset, max_scroll));
}

void ReplConsole::set_status_message(const std::string& message) {
    m_status_message = message;
    m_needs_redraw = true;
}

void ReplConsole::ensure_cursor_in_bounds() {
    if (m_cursor_pos < 0) {
        m_cursor_pos = 0;
    }
    if (m_cursor_pos > (int)m_current_input.length()) {
        m_cursor_pos = m_current_input.length();
    }
}

bool ReplConsole::has_incomplete_input() const {
    // Simple heuristic: check for unclosed brackets
    int paren_count = 0;
    int bracket_count = 0;
    int brace_count = 0;
    
    for (char ch : m_current_input) {
        if (ch == '(') paren_count++;
        else if (ch == ')') paren_count--;
        else if (ch == '[') bracket_count++;
        else if (ch == ']') bracket_count--;
        else if (ch == '{') brace_count++;
        else if (ch == '}') brace_count--;
    }
    
    return (paren_count > 0 || bracket_count > 0 || brace_count > 0);
}

std::string ReplConsole::format_output_line(const std::string& line, bool is_command) {
    // Simple formatting for now
    return line;
}

uint32_t ReplConsole::make_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    // Pack RGBA into uint32_t (AARRGGBB format as expected by editor_set_color)
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// ============================================================================
// C Interface
// ============================================================================

bool repl_initialize() {
    if (!g_repl_instance) {
        g_repl_instance = new ReplConsole();
    }
    return g_repl_instance->initialize();
}

void repl_shutdown() {
    if (g_repl_instance) {
        g_repl_instance->shutdown();
        delete g_repl_instance;
        g_repl_instance = nullptr;
    }
}

bool repl_is_initialized() {
    return g_repl_instance && g_repl_instance->is_initialized();
}

void repl_activate() {
    if (g_repl_instance) {
        g_repl_instance->activate();
    }
}

void repl_deactivate() {
    if (g_repl_instance) {
        g_repl_instance->deactivate();
    }
}

void repl_toggle() {
    if (!g_repl_instance) {
        if (!repl_initialize()) {
            return;
        }
    }
    g_repl_instance->toggle();
}

bool repl_is_active() {
    return g_repl_instance && g_repl_instance->is_active();
}

void repl_handle_key(int key, int modifiers) {
    if (g_repl_instance) {
        g_repl_instance->handle_key_input(key, modifiers);
    }
}

void repl_handle_character(char ch) {
    if (g_repl_instance) {
        g_repl_instance->handle_character_input(ch);
    }
}

void repl_update(float delta_time) {
    if (g_repl_instance) {
        g_repl_instance->update(delta_time);
    }
}

void repl_render() {
    if (g_repl_instance) {
        g_repl_instance->render();
    }
}

void repl_execute_command(const char* command) {
    if (g_repl_instance && command) {
        g_repl_instance->execute_command(command);
    }
}

void repl_add_output(const char* text) {
    if (g_repl_instance && text) {
        g_repl_instance->add_output_line(text);
    }
}

void repl_clear_output() {
    if (g_repl_instance) {
        g_repl_instance->clear_output();
    }
}

void repl_set_prompt(const char* prompt) {
    if (g_repl_instance && prompt) {
        g_repl_instance->set_prompt(prompt);
    }
}

void repl_notify_state_reset() {
    if (g_repl_instance) {
        g_repl_instance->add_output_line("--- Lua state reset ---");
        g_repl_instance->set_status_message("State Reset");
    }
}

// Stub implementations for backward compatibility (no longer used in text-based UI)
void repl_set_background_alpha(float alpha) {
    // No-op: text-based REPL doesn't use overlay background alpha
    (void)alpha;
}

float repl_get_background_alpha() {
    // Return 0 to indicate fully transparent (text-based mode)
    return 0.0f;
}