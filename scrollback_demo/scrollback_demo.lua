-- Scrollback Buffer Demo
-- Demonstrates the 2000-line scrollable text buffer with viewport control
-- Use Page Up/Page Down to scroll through the buffer

cls()
home()

-- Configuration
local num_lines = 500
local auto_scroll_demo = true

print("=== SCROLLBACK BUFFER DEMO ===")
print("")
print("This demo will generate " .. num_lines .. " lines of text")
print("to demonstrate the 2000-line scrollback buffer.")
print("")
print("Controls:")
print("  Page Up       - Scroll up one page")
print("  Page Down     - Scroll down one page")
print("  Cmd+Home      - Jump to top of buffer")
print("  Cmd+End       - Jump to bottom (re-enable auto-scroll)")
print("")
print("Press any key to start...")
waitKey()

cls()
home()

-- Enable auto-scroll so viewport follows as we write
text_set_autoscroll(true)

-- Phase 1: Generate lines with auto-scroll
print("=== PHASE 1: Generating lines with auto-scroll ===")
print("")

for i = 1, num_lines do
    print(string.format("Line %04d: The quick brown fox jumps over the lazy dog. [%d%%]",
        i, math.floor(i * 100 / num_lines)))

    -- Brief pause every 50 lines so you can see it scrolling
    if i % 50 == 0 then
        wait(0.05)
    end
end

print("")
print("=== GENERATION COMPLETE ===")
print("")
print("Cursor is at line: " .. text_get_cursor_line())
print("Viewport shows lines: " .. text_get_viewport_line() .. " to " ..
    (text_get_viewport_line() + text_get_viewport_height() - 1))
print("Auto-scroll enabled: " .. tostring(text_get_autoscroll()))
print("")
print("Press any key to continue...")
waitKey()

-- Phase 2: Demonstrate manual scrolling
print("")
print("=== PHASE 2: Manual Scrolling Demo ===")
print("")
print("Now we'll disable auto-scroll and demonstrate navigation.")
wait(1)

-- Disable auto-scroll
text_set_autoscroll(false)
print("Auto-scroll disabled.")
wait(0.5)

-- Scroll to top
print("Scrolling to top...")
wait(1)
text_scroll_to_top()
wait(1)

-- Write status at current viewport position (won't scroll)
print_at(0, 5, ">>> YOU ARE NOW AT THE TOP <<<")
wait(2)

-- Scroll to middle
local middle_line = math.floor(num_lines / 2)
print_at(0, 6, string.format("Jumping to line %d (middle)...", middle_line))
wait(1)
text_scroll_to_line(middle_line)
wait(1)

-- Scroll down a bit using scroll_down
print_at(0, 10, "Scrolling down 10 lines...")
wait(1)
text_scroll_down(10)
wait(1)

-- Scroll up using scroll_up
print_at(0, 10, "Scrolling up 20 lines...   ")
wait(1)
text_scroll_up(20)
wait(1)

-- Demonstrate page navigation
print_at(0, 10, "Simulating Page Down...    ")
wait(1)
text_page_down()
wait(1)

print_at(0, 10, "Simulating Page Up...      ")
wait(1)
text_page_up()
wait(1)

-- Phase 3: Demonstrate text_locate_line for absolute positioning
print_at(0, 15, "=== PHASE 3: Absolute Positioning ===")
wait(1)

-- Jump to specific line and write there
local target_line = 100
print_at(0, 16, string.format("Using text_locate_line(%d)...", target_line))
wait(1)

text_locate_line(target_line)
print("")
print("*** MARKER AT LINE " .. target_line .. " ***")
print("This text was written using text_locate_line()")
print("to position the cursor at an absolute buffer line.")

wait(2)

-- Show where we are
text_scroll_to_line(target_line - 5)
wait(2)

-- Phase 4: Build a multi-page document
cls()
home()
print("=== PHASE 4: Building Multi-Page Document ===")
print("")
print("Creating a structured 300-line document...")
print("")
wait(1)

-- Disable auto-scroll while building document
text_set_autoscroll(false)

-- Build table of contents
text_locate_line(0)
print("TABLE OF CONTENTS")
print("=================")
print("")
print("Chapter 1: Introduction ............. Line 10")
print("Chapter 2: Getting Started .......... Line 50")
print("Chapter 3: Advanced Features ........ Line 100")
print("Chapter 4: API Reference ............ Line 150")
print("Chapter 5: Examples ................. Line 200")
print("Chapter 6: Conclusion ............... Line 250")
print("")

-- Write chapters
local chapters = {
    { line = 10,  title = "Chapter 1: Introduction" },
    { line = 50,  title = "Chapter 2: Getting Started" },
    { line = 100, title = "Chapter 3: Advanced Features" },
    { line = 150, title = "Chapter 4: API Reference" },
    { line = 200, title = "Chapter 5: Examples" },
    { line = 250, title = "Chapter 6: Conclusion" }
}

for _, chapter in ipairs(chapters) do
    text_locate_line(chapter.line)
    print("")
    print(string.rep("=", 50))
    print(chapter.title)
    print(string.rep("=", 50))
    print("")
    print("This is the beginning of " .. chapter.title .. ".")
    print("")
    print("Lorem ipsum dolor sit amet, consectetur adipiscing elit.")
    print("Sed do eiusmod tempor incididunt ut labore et dolore magna")
    print("aliqua. Ut enim ad minim veniam, quis nostrud exercitation.")
    print("")

    -- Fill with content
    for i = 1, 30 do
        print(string.format("  Content line %d of %s", i, chapter.title))
    end
    print("")
end

-- Return to table of contents
text_scroll_to_line(0)
wait(1)

print_at(0, 10, ">>> Document created! Use Page Up/Down to browse <<<")
wait(3)

-- Phase 5: Interactive mode
print_at(0, 12, "=== INTERACTIVE MODE ===")
print_at(0, 13, "You can now use:")
print_at(0, 14, "  Page Up/Down  - Navigate the document")
print_at(0, 15, "  Cmd+Home      - Jump to top")
print_at(0, 16, "  Cmd+End       - Jump to bottom")
print_at(0, 18, "Press ESC when done...")

-- Wait for ESC
while true do
    local key = keyImmediate()
    if key == 0x35 then -- ESC
        break
    end
    wait(0.1)
end

-- Final summary
cls()
home()
text_set_autoscroll(true)
text_scroll_to_bottom()

print("=== DEMO COMPLETE ===")
print("")
print("Summary of features demonstrated:")
print("  1. 2000-line scrollback buffer")
print("  2. Auto-scroll mode (follows cursor)")
print("  3. Manual scrolling (Page Up/Down)")
print("  4. Absolute positioning with text_locate_line()")
print("  5. Viewport-relative print_at()")
print("  6. Building multi-page documents")
print("  7. Navigation shortcuts (Cmd+Home/End)")
print("")
print("API Functions used:")
print("  text_set_autoscroll(enabled)")
print("  text_get_autoscroll()")
print("  text_scroll_to_line(line)")
print("  text_scroll_up(lines)")
print("  text_scroll_down(lines)")
print("  text_page_up()")
print("  text_page_down()")
print("  text_scroll_to_top()")
print("  text_scroll_to_bottom()")
print("  text_locate_line(line)")
print("  text_get_cursor_line()")
print("  text_get_cursor_column()")
print("  text_get_viewport_line()")
print("  text_get_viewport_height()")
print("")
print("Press any key to exit...")
waitKey()
