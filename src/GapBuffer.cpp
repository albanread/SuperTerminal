//
//  GapBuffer.cpp
//  SuperTerminal Framework - Gap Buffer Document Storage
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Efficient text buffer using gap buffer technique for fast editing
//

#include "GapBuffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// Default initial capacity
#define DEFAULT_CAPACITY (64 * 1024)  // 64 KB
#define DEFAULT_GAP_SIZE (16 * 1024)  // 16 KB gap
#define MAX_LINES 100000               // Support up to 100K lines

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

static void ensure_line_index_capacity(GapBuffer* gb, int min_lines) {
    if (gb->max_lines >= min_lines) return;
    
    int new_max = gb->max_lines * 2;
    if (new_max < min_lines) new_max = min_lines;
    if (new_max > MAX_LINES) new_max = MAX_LINES;
    
    int* new_starts = (int*)realloc(gb->line_starts, new_max * sizeof(int));
    if (new_starts) {
        gb->line_starts = new_starts;
        gb->max_lines = new_max;
    }
}

static int actual_pos(const GapBuffer* gb, int pos) {
    // Convert logical position to physical position (accounting for gap)
    if (pos < gb->gap_start) {
        return pos;
    } else {
        return pos + (gb->gap_end - gb->gap_start);
    }
}

// ============================================================================
// LIFECYCLE
// ============================================================================

GapBuffer* gap_buffer_create(int initial_capacity) {
    if (initial_capacity < DEFAULT_GAP_SIZE) {
        initial_capacity = DEFAULT_CAPACITY;
    }
    
    GapBuffer* gb = (GapBuffer*)calloc(1, sizeof(GapBuffer));
    if (!gb) return NULL;
    
    gb->buffer = (char*)malloc(initial_capacity);
    if (!gb->buffer) {
        free(gb);
        return NULL;
    }
    
    gb->capacity = initial_capacity;
    gb->gap_start = 0;
    gb->gap_end = initial_capacity;
    
    // Initialize line index
    gb->line_starts = (int*)malloc(1024 * sizeof(int));
    if (!gb->line_starts) {
        free(gb->buffer);
        free(gb);
        return NULL;
    }
    gb->line_starts[0] = 0;
    gb->num_lines = 1;
    gb->max_lines = 1024;
    
    gb->modified = false;
    gb->filename = NULL;
    
    return gb;
}

GapBuffer* gap_buffer_load_file(const char* filepath) {
    if (!filepath) return NULL;
    
    // Open and get file size
    FILE* f = fopen(filepath, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size < 0) {
        fclose(f);
        return NULL;
    }
    
    // Create buffer with extra space for editing (2× file size + gap)
    int capacity = (file_size * 2) + DEFAULT_GAP_SIZE;
    GapBuffer* gb = gap_buffer_create(capacity);
    if (!gb) {
        fclose(f);
        return NULL;
    }
    
    // Read entire file into buffer (after gap)
    size_t bytes_read = fread(gb->buffer + gb->gap_end, 1, file_size, f);
    fclose(f);
    
    if ((long)bytes_read != file_size) {
        gap_buffer_free(gb);
        return NULL;
    }
    
    // Move content to beginning, leaving gap at start
    // Actually, let's put gap at end for now
    gb->gap_end = gb->capacity;
    gb->gap_start = file_size;
    memmove(gb->buffer, gb->buffer + (gb->capacity - file_size), file_size);
    
    // Store filename
    gb->filename = strdup(filepath);
    gb->modified = false;
    
    // Build line index
    gap_buffer_rebuild_line_index(gb);
    
    return gb;
}

GapBuffer* gap_buffer_load_memory(const char* content, int length) {
    if (!content || length < 0) return NULL;
    
    int capacity = (length * 2) + DEFAULT_GAP_SIZE;
    GapBuffer* gb = gap_buffer_create(capacity);
    if (!gb) return NULL;
    
    // Copy content to buffer (gap at end initially)
    memcpy(gb->buffer, content, length);
    gb->gap_start = length;
    gb->gap_end = gb->capacity;
    
    gb->modified = false;
    
    // Build line index
    gap_buffer_rebuild_line_index(gb);
    
    return gb;
}

bool gap_buffer_save_file(GapBuffer* gb, const char* filepath) {
    if (!gb || !filepath) return false;
    
    FILE* f = fopen(filepath, "wb");
    if (!f) return false;
    
    // Compact buffer first
    gap_buffer_compact(gb);
    
    // Write content
    int content_size = gap_buffer_size(gb);
    size_t written = fwrite(gb->buffer, 1, content_size, f);
    fclose(f);
    
    if ((int)written != content_size) {
        return false;
    }
    
    // Recreate gap at end
    gb->gap_start = content_size;
    gb->gap_end = gb->capacity;
    
    // Update filename and mark as unmodified
    if (gb->filename) free(gb->filename);
    gb->filename = strdup(filepath);
    gb->modified = false;
    
    return true;
}

void gap_buffer_free(GapBuffer* gb) {
    if (!gb) return;
    
    if (gb->buffer) free(gb->buffer);
    if (gb->line_starts) free(gb->line_starts);
    if (gb->filename) free(gb->filename);
    free(gb);
}

// ============================================================================
// QUERY
// ============================================================================

int gap_buffer_size(const GapBuffer* gb) {
    if (!gb) return 0;
    return gb->capacity - (gb->gap_end - gb->gap_start);
}

int gap_buffer_gap_size(const GapBuffer* gb) {
    if (!gb) return 0;
    return gb->gap_end - gb->gap_start;
}

int gap_buffer_line_count(const GapBuffer* gb) {
    if (!gb) return 0;
    return gb->num_lines;
}

char gap_buffer_get_char(const GapBuffer* gb, int pos) {
    if (!gb || pos < 0 || pos >= gap_buffer_size(gb)) {
        return '\0';
    }
    
    int physical_pos = actual_pos(gb, pos);
    return gb->buffer[physical_pos];
}

char gap_buffer_get_char_at(const GapBuffer* gb, int line, int col) {
    if (!gb || line < 0 || line >= gb->num_lines) {
        return '\0';
    }
    
    int line_start = gb->line_starts[line];
    int line_end;
    
    if (line + 1 < gb->num_lines) {
        line_end = gb->line_starts[line + 1];
    } else {
        line_end = gap_buffer_size(gb);
    }
    
    int pos = line_start + col;
    if (pos >= line_end) {
        return '\0';
    }
    
    return gap_buffer_get_char(gb, pos);
}

int gap_buffer_line_length(const GapBuffer* gb, int line) {
    if (!gb || line < 0 || line >= gb->num_lines) {
        return 0;
    }
    
    int line_start = gb->line_starts[line];
    int line_end;
    
    if (line + 1 < gb->num_lines) {
        line_end = gb->line_starts[line + 1] - 1;  // Exclude newline
    } else {
        line_end = gap_buffer_size(gb);
    }
    
    int length = line_end - line_start;
    return (length > 0) ? length : 0;
}

const char* gap_buffer_get_line(const GapBuffer* gb, int line, int* out_length) {
    if (!gb || line < 0 || line >= gb->num_lines) {
        if (out_length) *out_length = 0;
        return NULL;
    }
    
    int line_start = gb->line_starts[line];
    int physical_start = actual_pos(gb, line_start);
    
    if (out_length) {
        *out_length = gap_buffer_line_length(gb, line);
    }
    
    // WARNING: This returns pointer into buffer, NOT null-terminated
    // If line spans gap, this won't work perfectly - would need temp buffer
    return &gb->buffer[physical_start];
}

bool gap_buffer_is_valid_pos(const GapBuffer* gb, int pos) {
    return gb && pos >= 0 && pos <= gap_buffer_size(gb);
}

int gap_buffer_line_col_to_pos(const GapBuffer* gb, int line, int col) {
    if (!gb || line < 0 || line >= gb->num_lines) {
        return -1;
    }
    
    return gb->line_starts[line] + col;
}

void gap_buffer_pos_to_line_col(const GapBuffer* gb, int pos, int* out_line, int* out_col) {
    if (!gb || pos < 0) {
        if (out_line) *out_line = 0;
        if (out_col) *out_col = 0;
        return;
    }
    
    // Binary search for line
    int left = 0;
    int right = gb->num_lines - 1;
    int line = 0;
    
    while (left <= right) {
        int mid = (left + right) / 2;
        if (gb->line_starts[mid] <= pos) {
            line = mid;
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    
    if (out_line) *out_line = line;
    if (out_col) *out_col = pos - gb->line_starts[line];
}

// ============================================================================
// EDITING (GAP BUFFER OPERATIONS)
// ============================================================================

void gap_buffer_move_gap(GapBuffer* gb, int pos) {
    if (!gb || pos < 0 || pos > gap_buffer_size(gb)) return;
    
    if (pos == gb->gap_start) {
        return;  // Gap already at position
    }
    
    if (pos < gb->gap_start) {
        // Move gap left
        int move_size = gb->gap_start - pos;
        memmove(&gb->buffer[gb->gap_end - move_size],
                &gb->buffer[pos],
                move_size);
        gb->gap_end -= move_size;
        gb->gap_start = pos;
    } else {
        // Move gap right
        int move_size = pos - gb->gap_start;
        memmove(&gb->buffer[gb->gap_start],
                &gb->buffer[gb->gap_end],
                move_size);
        gb->gap_start += move_size;
        gb->gap_end += move_size;
    }
}

bool gap_buffer_grow(GapBuffer* gb, int min_new_capacity) {
    if (!gb) return false;
    
    int new_capacity = gb->capacity * 2;
    if (new_capacity < min_new_capacity) {
        new_capacity = min_new_capacity;
    }
    
    char* new_buffer = (char*)malloc(new_capacity);
    if (!new_buffer) return false;
    
    // Copy content before gap
    memcpy(new_buffer, gb->buffer, gb->gap_start);
    
    // Copy content after gap to end of new buffer
    int content_after_gap = gb->capacity - gb->gap_end;
    memcpy(new_buffer + new_capacity - content_after_gap,
           gb->buffer + gb->gap_end,
           content_after_gap);
    
    free(gb->buffer);
    gb->buffer = new_buffer;
    gb->gap_end = new_capacity - content_after_gap;
    gb->capacity = new_capacity;
    
    return true;
}

bool gap_buffer_insert_char(GapBuffer* gb, char c) {
    if (!gb) return false;
    
    // Grow if gap is full
    if (gb->gap_start >= gb->gap_end) {
        if (!gap_buffer_grow(gb, gb->capacity + DEFAULT_GAP_SIZE)) {
            return false;
        }
    }
    
    gb->buffer[gb->gap_start++] = c;
    gb->modified = true;
    
    return true;
}

bool gap_buffer_insert_string(GapBuffer* gb, const char* str, int length) {
    if (!gb || !str || length <= 0) return false;
    
    // Ensure gap has enough space
    int gap_size = gb->gap_end - gb->gap_start;
    if (gap_size < length) {
        int needed = gb->capacity + (length - gap_size) + DEFAULT_GAP_SIZE;
        if (!gap_buffer_grow(gb, needed)) {
            return false;
        }
    }
    
    memcpy(&gb->buffer[gb->gap_start], str, length);
    gb->gap_start += length;
    gb->modified = true;
    
    return true;
}

bool gap_buffer_delete_before(GapBuffer* gb) {
    if (!gb || gb->gap_start <= 0) return false;
    
    gb->gap_start--;
    gb->modified = true;
    
    return true;
}

bool gap_buffer_delete_after(GapBuffer* gb) {
    if (!gb || gb->gap_end >= gb->capacity) return false;
    
    gb->gap_end++;
    gb->modified = true;
    
    return true;
}

bool gap_buffer_delete_range(GapBuffer* gb, int start, int end) {
    if (!gb || start < 0 || end > gap_buffer_size(gb) || start >= end) {
        return false;
    }
    
    // Move gap to start position
    gap_buffer_move_gap(gb, start);
    
    // Expand gap to cover range
    int delete_size = end - start;
    gb->gap_end += delete_size;
    gb->modified = true;
    
    return true;
}

bool gap_buffer_insert_newline(GapBuffer* gb) {
    if (!gap_buffer_insert_char(gb, '\n')) {
        return false;
    }
    
    // Update line index
    ensure_line_index_capacity(gb, gb->num_lines + 1);
    
    // Find which line the gap is in
    int pos = gb->gap_start - 1;  // Position of newline we just inserted
    int line = 0;
    for (int i = 0; i < gb->num_lines; i++) {
        if (gb->line_starts[i] > pos) break;
        line = i;
    }
    
    // Insert new line start
    if (line + 1 < gb->num_lines) {
        memmove(&gb->line_starts[line + 2],
                &gb->line_starts[line + 1],
                (gb->num_lines - line - 1) * sizeof(int));
    }
    
    gb->line_starts[line + 1] = pos + 1;
    gb->num_lines++;
    
    return true;
}

bool gap_buffer_delete_line(GapBuffer* gb, int line) {
    if (!gb || line < 0 || line >= gb->num_lines) {
        return false;
    }
    
    int start = gb->line_starts[line];
    int end;
    
    if (line + 1 < gb->num_lines) {
        end = gb->line_starts[line + 1];
    } else {
        end = gap_buffer_size(gb);
    }
    
    if (!gap_buffer_delete_range(gb, start, end)) {
        return false;
    }
    
    // Remove line from index
    if (line + 1 < gb->num_lines) {
        memmove(&gb->line_starts[line],
                &gb->line_starts[line + 1],
                (gb->num_lines - line - 1) * sizeof(int));
    }
    gb->num_lines--;
    
    return true;
}

// ============================================================================
// VIEWPORT RENDERING
// ============================================================================

void gap_buffer_render_viewport(const GapBuffer* gb,
                                int scroll_x, int scroll_y,
                                int width, int height,
                                GapBufferRenderCallback callback,
                                void* userdata) {
    if (!gb || !callback) return;
    
    for (int row = 0; row < height; row++) {
        int line = scroll_y + row;
        if (line >= gb->num_lines) break;
        
        for (int col = 0; col < width; col++) {
            char c = gap_buffer_get_char_at(gb, line, scroll_x + col);
            callback(row, col, c ? c : ' ', userdata);
        }
    }
}

int gap_buffer_copy_viewport(const GapBuffer* gb,
                             int scroll_x, int scroll_y,
                             int width, int height,
                             char* out_buffer) {
    if (!gb || !out_buffer) return 0;
    
    int chars_copied = 0;
    
    for (int row = 0; row < height; row++) {
        int line = scroll_y + row;
        if (line >= gb->num_lines) {
            // Fill rest with spaces
            for (int col = 0; col < width; col++) {
                out_buffer[chars_copied++] = ' ';
            }
            continue;
        }
        
        for (int col = 0; col < width; col++) {
            char c = gap_buffer_get_char_at(gb, line, scroll_x + col);
            out_buffer[chars_copied++] = c ? c : ' ';
        }
    }
    
    return chars_copied;
}

// ============================================================================
// LINE INDEX MANAGEMENT
// ============================================================================

void gap_buffer_rebuild_line_index(GapBuffer* gb) {
    if (!gb) return;
    
    gb->num_lines = 1;
    gb->line_starts[0] = 0;
    
    int content_size = gap_buffer_size(gb);
    
    for (int i = 0; i < content_size; i++) {
        char c = gap_buffer_get_char(gb, i);
        if (c == '\n') {
            ensure_line_index_capacity(gb, gb->num_lines + 1);
            gb->line_starts[gb->num_lines++] = i + 1;
        }
    }
}

void gap_buffer_update_line_index_insert(GapBuffer* gb, int pos, int inserted_length) {
    if (!gb) return;
    
    // Shift all line starts after insertion point
    for (int i = 0; i < gb->num_lines; i++) {
        if (gb->line_starts[i] > pos) {
            gb->line_starts[i] += inserted_length;
        }
    }
}

void gap_buffer_update_line_index_delete(GapBuffer* gb, int pos, int deleted_length) {
    if (!gb) return;
    
    // Shift all line starts after deletion point
    for (int i = 0; i < gb->num_lines; i++) {
        if (gb->line_starts[i] > pos) {
            gb->line_starts[i] -= deleted_length;
        }
    }
}

// ============================================================================
// UTILITIES
// ============================================================================

char* gap_buffer_to_string(const GapBuffer* gb) {
    if (!gb) return NULL;
    
    int size = gap_buffer_size(gb);
    char* result = (char*)malloc(size + 1);
    if (!result) return NULL;
    
    // Copy content before gap
    memcpy(result, gb->buffer, gb->gap_start);
    
    // Copy content after gap
    memcpy(result + gb->gap_start,
           gb->buffer + gb->gap_end,
           gb->capacity - gb->gap_end);
    
    result[size] = '\0';
    return result;
}

void gap_buffer_clear(GapBuffer* gb) {
    if (!gb) return;
    
    gb->gap_start = 0;
    gb->gap_end = gb->capacity;
    gb->num_lines = 1;
    gb->line_starts[0] = 0;
    gb->modified = true;
}

void gap_buffer_compact(GapBuffer* gb) {
    if (!gb) return;
    
    // Move all content after gap to immediately after content before gap
    int content_after_gap = gb->capacity - gb->gap_end;
    memmove(&gb->buffer[gb->gap_start],
            &gb->buffer[gb->gap_end],
            content_after_gap);
    
    // Update gap to be at end
    int content_size = gb->gap_start + content_after_gap;
    gb->gap_start = content_size;
    gb->gap_end = gb->capacity;
}

void gap_buffer_get_stats(const GapBuffer* gb, GapBufferStats* stats) {
    if (!gb || !stats) return;
    
    stats->total_capacity = gb->capacity;
    stats->content_size = gap_buffer_size(gb);
    stats->gap_size = gap_buffer_gap_size(gb);
    stats->num_lines = gb->num_lines;
    stats->gap_start_pos = gb->gap_start;
    stats->gap_end_pos = gb->gap_end;
    stats->utilization = (float)stats->content_size / (float)stats->total_capacity;
}

void gap_buffer_debug_print(const GapBuffer* gb) {
    if (!gb) return;
    
    printf("=== Gap Buffer Debug ===\n");
    printf("Capacity: %d bytes\n", gb->capacity);
    printf("Content size: %d bytes\n", gap_buffer_size(gb));
    printf("Gap: [%d, %d) = %d bytes\n", 
           gb->gap_start, gb->gap_end, gap_buffer_gap_size(gb));
    printf("Lines: %d\n", gb->num_lines);
    printf("Modified: %s\n", gb->modified ? "yes" : "no");
    printf("Filename: %s\n", gb->filename ? gb->filename : "(none)");
    
    printf("\nFirst few line starts:\n");
    for (int i = 0; i < 10 && i < gb->num_lines; i++) {
        printf("  Line %d: starts at byte %d\n", i, gb->line_starts[i]);
    }
}