# Text Layer Scrollback Buffer Guide

## Overview

The SuperTerminal text layer (Layer 5) now supports a **2000-line scrollback buffer** with viewport-based rendering and full programmatic control. This allows you to:

- Print thousands of lines of text that persist in memory
- Scroll back through printed output using Page Up/Down
- Build multi-page documents programmatically
- Control auto-scroll behavior (follow cursor or stay fixed)
- Position the write cursor anywhere in the 2000-line buffer

## Performance

The scrollback buffer is designed for high performance:

- **Memory**: 2000 lines × 160 columns × ~20 bytes = ~6.4 MB (negligible)
- **Rendering**: Only the visible viewport is rendered (typically 60 lines)
- **Scrolling**: Viewport adjustment is instant (no buffer copying)
- **Writing**: When buffer fills, only 100 lines are shifted at once
- **60 FPS**: All operations maintain smooth frame rates

## Architecture

```
┌─────────────────────────────────────┐
│  2000-Line Scrollback Buffer        │
│  (Lines 0-1999)                      │
│                                      │
│  Line 0:  "First line..."            │
│  Line 1:  "Second line..."           │
│  Line 2:  "..."                      │
│  ...                                 │
│  Line 100: "Current viewport top"    │ ← viewportStartLine
│  Line 101: "Visible line 2"          │
│  Line 102: "Visible line 3"          │
│  ...                                 │
│  Line 159: "Visible line 60"         │ ← viewportStartLine + viewportHeight
│  Line 160: "Below viewport"          │
│  ...                                 │
│  Line 500: "Cursor writes here" ←    │ ← cursorY (write position)
│  Line 501: "..."                     │
│  ...                                 │
│  Line 1999: "Last line"              │
└─────────────────────────────────────┘
```

### Key Concepts

- **Buffer**: 2000 lines of persistent text storage
- **Viewport**: The visible window (e.g., lines 100-159 shown on screen)
- **Cursor**: Write position for `print()` (can be anywhere 0-1999)
- **Auto-scroll**: When enabled, viewport follows cursor automatically

## API Reference

### Basic Printing

```lua
print(text)              -- Print at cursor, auto-wrap/scroll
print_at(x, y, text)     -- Print at viewport-relative position
                         -- y=0 is top of visible screen
                         -- y=59 is bottom of visible screen (60-line viewport)
home()                   -- Cursor to (0,0)
cls()                    -- Clear entire buffer
```

### Cursor Positioning

```lua
text_locate_line(line)   -- Set cursor Y to absolute buffer line (0-1999)
                         -- Also resets cursor X to 0
                         -- Example: text_locate_line(500)

text_get_cursor_line()   -- Get cursor Y position (0-1999)
text_get_cursor_column() -- Get cursor X position (0-159)
```

### Viewport Control

```lua
text_scroll_to_line(line)  -- Show specific buffer line at top of viewport
                           -- Disables auto-scroll
                           -- Example: text_scroll_to_line(0) shows top

text_scroll_up(lines)      -- Scroll viewport up N lines
text_scroll_down(lines)    -- Scroll viewport down N lines

text_page_up()             -- Scroll up one viewport height
text_page_down()           -- Scroll down one viewport height

text_scroll_to_top()       -- Jump to line 0 (top of buffer)
text_scroll_to_bottom()    -- Jump to cursor position and re-enable auto-scroll

text_get_viewport_line()   -- Get top visible line number
text_get_viewport_height() -- Get number of visible lines (e.g., 60)
```

### Auto-Scroll Control

```lua
text_set_autoscroll(enabled)  -- Enable/disable auto-scroll
                              -- true: viewport follows cursor
                              -- false: viewport stays fixed

text_get_autoscroll()         -- Query current auto-scroll state
                              -- Returns true or false
```

## Keyboard Shortcuts

When running scripts, these keys control the text viewport:

| Key            | Action                                    |
|----------------|-------------------------------------------|
| **Page Up**    | Scroll up one page                        |
| **Page Down**  | Scroll down one page                      |
| **Cmd+Home**   | Jump to top of buffer (line 0)            |
| **Cmd+End**    | Jump to bottom and re-enable auto-scroll  |

## Usage Examples

### Example 1: Simple Scrolling Log Viewer

```lua
cls()
text_set_autoscroll(true)  -- Follow cursor

-- Generate 500 lines of log output
for i = 1, 500 do
    print(string.format("[%04d] Log entry: System event %d", i, i))
    if i % 50 == 0 then
        wait(0.1)  -- Brief pause to see scrolling
    end
end

print("")
print("=== LOG COMPLETE ===")
print("Use Page Up to scroll back and review logs")
wait(5)
```

### Example 2: Multi-Page Document

```lua
cls()
text_set_autoscroll(false)  -- Manual control

-- Build table of contents at top
text_locate_line(0)
print("TABLE OF CONTENTS")
print("=================")
print("")
print("Chapter 1 ........ Line 20")
print("Chapter 2 ........ Line 100")
print("Chapter 3 ........ Line 200")

-- Write chapters
text_locate_line(20)
print("")
print("CHAPTER 1: Introduction")
print("=======================")
for i = 1, 50 do
    print("Chapter 1, paragraph " .. i)
end

text_locate_line(100)
print("")
print("CHAPTER 2: Details")
print("==================")
for i = 1, 50 do
    print("Chapter 2, paragraph " .. i)
end

-- Show table of contents
text_scroll_to_line(0)
print_at(0, 10, ">>> Use Page Up/Down to navigate <<<")
```

### Example 3: Status Display with Scrolling Data

```lua
cls()
text_set_autoscroll(false)

local data_start_line = 10
local counter = 0

while true do
    -- Fixed header at top of viewport
    text_scroll_to_line(0)
    print_at(0, 0, "=== SYSTEM MONITOR ===")
    print_at(0, 1, "Time: " .. time_ms())
    print_at(0, 2, "Data lines: " .. counter)
    print_at(0, 3, "Viewport: " .. text_get_viewport_line())
    print_at(0, 4, string.rep("-", 40))
    
    -- Add data at cursor position
    text_locate_line(data_start_line + counter)
    print(string.format("Data point %d: value=%d", counter, math.random(100)))
    
    counter = counter + 1
    
    -- Auto-scroll when data grows beyond viewport
    if counter > 50 then
        text_scroll_to_line(data_start_line + counter - 50)
    end
    
    wait(0.5)
    
    if keyImmediate() == 0x35 then break end  -- ESC to exit
end
```

### Example 4: Reviewing History While Printing

```lua
cls()
text_set_autoscroll(true)

-- Start generating data
for i = 1, 1000 do
    print(string.format("Line %04d: Data stream continues...", i))
    
    -- User can press Page Up at any time to review history
    -- Auto-scroll is automatically disabled when they do
    -- Press Cmd+End to return to live view
    
    wait(0.05)
end
```

### Example 5: Search Through Buffer

```lua
-- Generate content
cls()
text_set_autoscroll(false)
for i = 1, 300 do
    if i % 50 == 0 then
        print("*** MARKER " .. (i/50) .. " ***")
    else
        print("Line " .. i)
    end
end

-- Search for markers
function jump_to_marker(marker_num)
    local target_line = marker_num * 50
    text_scroll_to_line(math.max(0, target_line - 5))
    print_at(0, 10, ">>> Found marker " .. marker_num .. " <<<")
end

-- Jump to markers
text_scroll_to_top()
wait(1)
jump_to_marker(1)
wait(2)
jump_to_marker(3)
wait(2)
jump_to_marker(5)
```

## How `print_at()` Works with Scrollback

**Important**: `print_at(x, y, text)` uses **viewport-relative** coordinates:

```lua
text_scroll_to_line(100)  -- Show lines 100-159

-- y is relative to VIEWPORT, not buffer
print_at(0, 0, "Top of screen")      -- Writes to buffer line 100
print_at(0, 1, "Second line")        -- Writes to buffer line 101
print_at(0, 59, "Bottom of screen")  -- Writes to buffer line 159
```

This makes `print_at()` natural for updating the visible screen, while `text_locate_line()` handles absolute positioning.

## Best Practices

### 1. Choose Auto-Scroll Mode Appropriately

```lua
-- For live logs/data: enable auto-scroll
text_set_autoscroll(true)
for i = 1, 1000 do
    print("Log entry " .. i)
end

-- For documents: disable auto-scroll
text_set_autoscroll(false)
build_document()
text_scroll_to_line(0)
```

### 2. Separate Data Generation from Display

```lua
-- Phase 1: Build data (fast, no auto-scroll)
text_set_autoscroll(false)
for i = 1, 500 do
    text_locate_line(i)
    print("Data line " .. i)
end

-- Phase 2: Interactive viewing
text_scroll_to_line(0)
print_at(0, 10, ">>> Use Page Up/Down to browse >>>")
```

### 3. Use viewport-relative `print_at()` for UI

```lua
-- Update status bar at bottom of screen
local viewport_height = text_get_viewport_height()
print_at(0, viewport_height - 1, "Status: Ready")
```

### 4. Save/Restore Viewport Position

```lua
-- Save position
local saved_viewport = text_get_viewport_line()
local saved_autoscroll = text_get_autoscroll()

-- Do something that changes view
text_scroll_to_line(100)
-- ... operations ...

-- Restore
text_scroll_to_line(saved_viewport)
text_set_autoscroll(saved_autoscroll)
```

## Limitations and Edge Cases

### Buffer Overflow

When the cursor reaches line 2000, the buffer automatically scrolls:

- Lines 0-99 are deleted
- Lines 100-1999 shift to 0-1899
- Cursor moves to line 1900
- Viewport adjusts to maintain relative position

This is transparent to scripts but means very old data is lost.

### Viewport Boundaries

```lua
text_scroll_to_line(-10)   -- Clamped to 0
text_scroll_to_line(5000)  -- Clamped to (2000 - viewport_height)
```

### Cursor Boundaries

```lua
text_locate_line(-5)    -- Ignored (cursor unchanged)
text_locate_line(2500)  -- Ignored (cursor unchanged)
```

## Performance Tips

1. **Batch writes**: Multiple `print()` calls are efficient
2. **Avoid excessive scrolling**: Changing viewport every frame is wasteful
3. **Use `print()` for bulk output**: Faster than multiple `print_at()` calls
4. **Clear when done**: Call `cls()` to reset buffer between scripts

## Debugging

```lua
-- Print diagnostic info
print("Cursor: (" .. text_get_cursor_column() .. ", " .. text_get_cursor_line() .. ")")
print("Viewport: line " .. text_get_viewport_line() .. ", height " .. text_get_viewport_height())
print("Auto-scroll: " .. tostring(text_get_autoscroll()))
```

## Demo Script

Run the included demo:

```bash
cd superterminal/scrollback_demo
./run_demo.sh
```

Or load in SuperTerminal: **File > Load Script** → `scrollback_demo.lua`

## Summary

The 2000-line scrollback buffer turns SuperTerminal's text layer into a powerful document viewer and log browser, while maintaining the simplicity of `print()` for output. Combine auto-scroll for live data with manual navigation for reviewing history, all with zero performance impact.