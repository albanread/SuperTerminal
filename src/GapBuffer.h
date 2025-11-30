//
//  GapBuffer.h
//  SuperTerminal Framework - Gap Buffer Document Storage
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Efficient text buffer using gap buffer technique for fast editing
//

#ifndef GAPBUFFER_H
#define GAPBUFFER_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Gap buffer structure - single contiguous memory block with a gap
typedef struct GapBuffer {
    char* buffer;           // Flat contiguous buffer
    int capacity;           // Total allocated size
    int gap_start;          // Start of gap (cursor position)
    int gap_end;            // End of gap
    
    // Line index for fast line access
    int* line_starts;       // Array of byte offsets to line starts
    int num_lines;          // Number of lines in document
    int max_lines;          // Capacity of line_starts array
    
    // Metadata
    bool modified;          // Has document been modified?
    char* filename;         // Associated filename
} GapBuffer;

// ============================================================================
// LIFECYCLE
// ============================================================================

// Create new gap buffer with initial capacity
GapBuffer* gap_buffer_create(int initial_capacity);

// Load document from file into gap buffer
GapBuffer* gap_buffer_load_file(const char* filepath);

// Load document from memory
GapBuffer* gap_buffer_load_memory(const char* content, int length);

// Save gap buffer to file
bool gap_buffer_save_file(GapBuffer* gb, const char* filepath);

// Free gap buffer
void gap_buffer_free(GapBuffer* gb);

// ============================================================================
// QUERY
// ============================================================================

// Get actual content size (excluding gap)
int gap_buffer_size(const GapBuffer* gb);

// Get gap size (unused space)
int gap_buffer_gap_size(const GapBuffer* gb);

// Get number of lines
int gap_buffer_line_count(const GapBuffer* gb);

// Get character at absolute position (handles gap internally)
char gap_buffer_get_char(const GapBuffer* gb, int pos);

// Get character at line/column
char gap_buffer_get_char_at(const GapBuffer* gb, int line, int col);

// Get line length (excluding newline)
int gap_buffer_line_length(const GapBuffer* gb, int line);

// Get entire line (returns pointer into buffer, NOT null-terminated)
const char* gap_buffer_get_line(const GapBuffer* gb, int line, int* out_length);

// Check if position is valid
bool gap_buffer_is_valid_pos(const GapBuffer* gb, int pos);

// Convert line/col to absolute position
int gap_buffer_line_col_to_pos(const GapBuffer* gb, int line, int col);

// Convert absolute position to line/col
void gap_buffer_pos_to_line_col(const GapBuffer* gb, int pos, int* out_line, int* out_col);

// ============================================================================
// EDITING (GAP BUFFER OPERATIONS)
// ============================================================================

// Move gap to position (call before insert/delete operations)
void gap_buffer_move_gap(GapBuffer* gb, int pos);

// Insert character at gap position (O(1) when gap is at cursor)
bool gap_buffer_insert_char(GapBuffer* gb, char c);

// Insert string at gap position
bool gap_buffer_insert_string(GapBuffer* gb, const char* str, int length);

// Delete character before gap (backspace)
bool gap_buffer_delete_before(GapBuffer* gb);

// Delete character after gap (delete key)
bool gap_buffer_delete_after(GapBuffer* gb);

// Delete range [start, end)
bool gap_buffer_delete_range(GapBuffer* gb, int start, int end);

// Insert newline (updates line index)
bool gap_buffer_insert_newline(GapBuffer* gb);

// Delete line
bool gap_buffer_delete_line(GapBuffer* gb, int line);

// ============================================================================
// VIEWPORT RENDERING
// ============================================================================

// Render viewport region to callback function
// Callback is called for each visible character with (line, col, char, userdata)
typedef void (*GapBufferRenderCallback)(int line, int col, char c, void* userdata);

void gap_buffer_render_viewport(const GapBuffer* gb,
                                int scroll_x, int scroll_y,
                                int width, int height,
                                GapBufferRenderCallback callback,
                                void* userdata);

// Copy viewport region to buffer (for direct rendering)
// Returns number of characters copied
int gap_buffer_copy_viewport(const GapBuffer* gb,
                             int scroll_x, int scroll_y,
                             int width, int height,
                             char* out_buffer);

// ============================================================================
// LINE INDEX MANAGEMENT
// ============================================================================

// Rebuild line index (call after bulk operations)
void gap_buffer_rebuild_line_index(GapBuffer* gb);

// Update line index after insertion at position
void gap_buffer_update_line_index_insert(GapBuffer* gb, int pos, int inserted_length);

// Update line index after deletion at position
void gap_buffer_update_line_index_delete(GapBuffer* gb, int pos, int deleted_length);

// ============================================================================
// UTILITIES
// ============================================================================

// Get entire content as continuous string (allocates memory, caller must free)
char* gap_buffer_to_string(const GapBuffer* gb);

// Clear all content
void gap_buffer_clear(GapBuffer* gb);

// Grow buffer capacity (internal use, but exposed for testing)
bool gap_buffer_grow(GapBuffer* gb, int min_new_capacity);

// Compact buffer (remove gap, useful before saving)
void gap_buffer_compact(GapBuffer* gb);

// Get buffer statistics (for debugging)
typedef struct {
    int total_capacity;
    int content_size;
    int gap_size;
    int num_lines;
    int gap_start_pos;
    int gap_end_pos;
    float utilization;  // content_size / total_capacity
} GapBufferStats;

void gap_buffer_get_stats(const GapBuffer* gb, GapBufferStats* stats);

// Print buffer state (for debugging)
void gap_buffer_debug_print(const GapBuffer* gb);

#ifdef __cplusplus
}
#endif

#endif /* GAPBUFFER_H */