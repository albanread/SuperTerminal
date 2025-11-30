//
// ReplConsole.h
// SuperTerminal - Interactive REPL Console System (Text-Based UI)
//
// A dedicated REPL (Read-Eval-Print Loop) console for interactive Lua execution
// Uses the bottom 6 lines of the editor text layer (Layer 6) with PETSCII box drawing
// Captures and restores screen content when toggled
//

#ifndef REPL_CONSOLE_H
#define REPL_CONSOLE_H

#include <string>
#include <vector>
#include <deque>
#include <cstdint>

// Text screen dimensions (80x25 character grid)
constexpr int SCREEN_COLS = 80;
constexpr int SCREEN_ROWS = 25;

// PETSCII box drawing characters (Unicode)
constexpr const char* BOX_TOP_LEFT = "┌";
constexpr const char* BOX_TOP_RIGHT = "┐";
constexpr const char* BOX_BOTTOM_LEFT = "└";
constexpr const char* BOX_BOTTOM_RIGHT = "┘";
constexpr const char* BOX_HORIZONTAL = "─";
constexpr const char* BOX_VERTICAL = "│";

class ReplConsole {
public:
    // Constructor/Destructor
    ReplConsole();
    ~ReplConsole();
    
    // Core REPL functionality
    bool initialize();
    void shutdown();
    bool is_initialized() const { return m_initialized; }
    
    // Activation/Visibility
    void activate();
    void deactivate();
    bool is_active() const { return m_active; }
    void toggle();
    
    // Input handling
    void handle_key_input(int key, int modifiers);
    void handle_character_input(char ch);
    
    // Rendering
    void update(float delta_time);
    void render();
    
    // Execution
    void execute_current_command();
    void execute_command(const std::string& command);
    
    // History management
    void add_to_history(const std::string& command);
    void history_previous();
    void history_next();
    void clear_history();
    
    // Display management
    void add_output_line(const std::string& line);
    void clear_output();
    void scroll_up();
    void scroll_down();
    
    // Configuration
    void set_prompt(const std::string& prompt) { m_prompt = prompt; }
    void set_status_message(const std::string& message);
    
    // State queries
    std::string get_current_input() const { return m_current_input; }
    bool has_incomplete_input() const;
    int get_cursor_position() const { return m_cursor_pos; }
    
private:
    // Configuration constants
    static const int REPL_LINES = 6;              // REPL occupies bottom 6 lines
    static const int MAX_HISTORY_SIZE = 100;
    static const int MAX_OUTPUT_LINES = 1000;
    
    // Calculate REPL start row based on current grid height (adaptive)
    int get_repl_start_row() const;
    static constexpr float CURSOR_BLINK_RATE = 0.5f; // seconds
    
    // REPL line layout (dynamic based on adaptive grid)
    int get_row_top_border() const { return get_repl_start_row(); }
    int get_row_first_line() const { return get_repl_start_row() + 1; }
    int get_row_bottom_border() const { return get_repl_start_row() + REPL_LINES; }
    static const int CONTENT_LINES = 4;           // Lines for content
    
    // Screen buffer for save/restore
    struct TextCell {
        char ch;
        uint32_t ink;
        uint32_t paper;
    };
    
    // Core state
    bool m_initialized;
    bool m_active;
    
    // Screen capture for restoration
    TextCell m_saved_screen[SCREEN_COLS * REPL_LINES];
    bool m_screen_saved;
    
    // Input state
    std::string m_current_input;
    int m_cursor_pos;
    std::string m_prompt;
    std::string m_status_message;
    
    // Command history
    std::vector<std::string> m_command_history;
    int m_history_index; // -1 means not browsing history
    std::string m_history_temp_input; // Temporary storage when browsing history
    
    // Output buffer (viewport over larger text)
    std::deque<std::string> m_output_lines;
    int m_scroll_offset; // How many lines scrolled up from bottom
    
    // Visual state
    float m_cursor_blink_timer;
    bool m_cursor_visible;
    bool m_needs_redraw;
    
    // Internal methods
    void update_cursor_blink(float delta_time);
    void insert_character(char ch);
    void delete_character();
    void backspace();
    void move_cursor_left();
    void move_cursor_right();
    void move_cursor_home();
    void move_cursor_end();
    void clear_current_input();
    
    // Screen management
    void save_screen_area();
    void restore_screen_area();
    
    // Rendering helpers
    void render_box();
    void render_status_line();
    void render_content_area();
    void render_input_line();
    void render_cursor();
    
    // Text drawing utilities
    void draw_text(int x, int y, const char* text, uint32_t ink, uint32_t paper);
    void draw_char(int x, int y, const char* ch, uint32_t ink, uint32_t paper);
    void clear_line(int y, uint32_t paper);
    
    // Line management
    void wrap_and_add_line(const std::string& line);
    std::vector<std::string> wrap_text(const std::string& text, int max_width);
    
    // Lua integration
    bool execute_lua_command(const std::string& command, std::string& result, std::string& error);
    bool is_lua_command_complete(const std::string& command);
    
    // Utility
    void clamp_scroll_offset();
    void ensure_cursor_in_bounds();
    std::string format_output_line(const std::string& line, bool is_command = false);
    
    // Color helpers (RGBA to uint32_t)
    uint32_t make_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
};

// C interface for integration with existing SuperTerminal systems
extern "C" {
    // Global REPL instance management
    bool repl_initialize();
    void repl_shutdown();
    bool repl_is_initialized();
    
    // Activation
    void repl_activate();
    void repl_deactivate();
    void repl_toggle();
    bool repl_is_active();
    
    // Input handling (called from main input system)
    void repl_handle_key(int key, int modifiers);
    void repl_handle_character(char ch);
    
    // Update/Render (called from main loop)
    void repl_update(float delta_time);
    void repl_render();
    
    // Utility
    void repl_execute_command(const char* command);
    void repl_add_output(const char* text);
    void repl_clear_output();
    void repl_set_prompt(const char* prompt);
    void repl_notify_state_reset();
    
    // Backward compatibility stubs (no longer used in text-based UI)
    void repl_set_background_alpha(float alpha);
    float repl_get_background_alpha();
}

#endif // REPL_CONSOLE_H