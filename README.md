# SuperTerminal

A retro-inspired terminal framework for macOS Apple Silicon that brings 1980s computer graphics capabilities to modern hardware through hardware-accelerated compositing. SuperTerminal provides a complete application framework with LuaJIT as the primary development language and Commodore 64-style text handling.

## Overview

SuperTerminal recreates the experience of 1980s computer programming with modern performance. It features a 7-layer Metal-composited graphics system, real-time code editing, and authentic retro aesthetics while leveraging Apple Silicon's unified memory architecture and hardware acceleration.

### Key Features

- **7-Layer Hardware Compositing**: Background, dual tile layers, graphics, dual text layers, and sprites
- **LuaJIT Integration**: Modern Lua development with retro aesthetics
- **C64-Style Text Handling**: Direct character positioning, simple commands (PRINT, CLS, HOME)
- **Built-in Editor**: Full-screen Lua editor with syntax highlighting (toggle with F1)
- **High-Performance Graphics**: Skia-based 2D graphics with Metal acceleration
- **Retro Assets**: 128×128 PNG tiles and sprites with smooth scrolling and positioning
- **Live Coding**: Edit and run Lua code with immediate visual feedback

## Architecture

### 7-Layer Compositing System

```
Layer 7: Sprites       (256 × 128×128 sprites, smooth positioning)
Layer 6: Text Layer 2  (80×25 grid - Full-screen editor, toggleable)
Layer 5: Text Layer 1  (80×25 grid - Terminal output, C64-style)
Layer 4: Graphics      (Skia-based 2D drawing, lines, shapes, patterns)
Layer 3: Tile Layer 2  (Secondary tilemap with seamless scrolling)
Layer 2: Tile Layer 1  (Primary tilemap with seamless scrolling)
Layer 1: Background    (Solid color foundation)
```

### Development Workflow

1. **Launch**: `luarunner mygame.lua`
2. **Edit**: Press F1 to show/hide full-screen editor overlay
3. **Code**: Write Lua with C64-style commands and modern graphics
4. **Test**: Code runs immediately with live visual feedback
5. **Debug**: Errors displayed in terminal layer
6. **Iterate**: Seamless edit-test-debug cycle

## Requirements

- **macOS**: 12.0 or later
- **Hardware**: Apple Silicon (M1/M2/M3) recommended
- **Dependencies**:
  - LuaJIT 2.1+
  - Skia Graphics Library
  - Xcode Command Line Tools

## Installation

### Option 1: Homebrew (Recommended)

```bash
# Install dependencies
brew install luajit
brew install skia

# Clone and build SuperTerminal
git clone https://github.com/superterminal/superterminal.git
cd superterminal
mkdir build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
sudo make install
```

### Option 2: Manual Build

```bash
# Install LuaJIT
brew install luajit

# Build Skia (if not available via brew)
# Follow Skia build instructions for macOS

# Build SuperTerminal
git clone https://github.com/superterminal/superterminal.git
cd superterminal
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(sysctl -n hw.ncpu)
```

## Quick Start

### Hello World

Create `hello.lua`:

```lua
-- Clear screen and set colors
cls()
set_color(rgba(255, 255, 255, 255), rgba(0, 0, 0, 0))

-- Print greeting
print("HELLO, WORLD!")
print("WELCOME TO SUPERTERMINAL")

-- Wait for input
waitKey()
```

Run it:

```bash
luarunner hello.lua
```

### Graphics Example

```lua
-- Graphics demonstration
cls()
graphics_clear()
background_color(rgba(20, 20, 40, 255))

-- Draw colorful shapes
for i = 1, 10 do
    local x = 50 + i * 40
    local y = 200
    local color = rgba(255, i * 25, 128, 255)
    fill_circle(x, y, 20, color)
end

-- Animation loop
for frame = 1, 100 do
    graphics_clear()
    local x = 100 + frame * 5
    draw_line(x, 100, x + 50, 200, rgba(255, 255, 0, 255))
    sleep_ms(50)
end

waitKey()
```

### Sprite Example

```lua
-- Load and display sprites
sprite_load(1, "assets/player.png")  -- 128x128 PNG
sprite_load(2, "assets/enemy.png")

-- Show sprites
sprite_show(1, 100, 200)  -- Player at (100, 200)
sprite_show(2, 300, 150)  -- Enemy at (300, 150)

-- Animate sprite movement
for i = 1, 50 do
    sprite_move(1, 100 + i * 2, 200)
    sleep_ms(100)
end
```

## API Reference

### Text Output (Commodore 64 Style)

```lua
print(text)               -- Print at cursor position
print_at(x, y, text)      -- Print at specific position (0-79, 0-24)
cls()                     -- Clear screen
home()                    -- Move cursor to (0,0)

-- Cursor control
cursor_show()             -- Show cursor
cursor_hide()             -- Hide cursor
cursor_move(x, y)         -- Move cursor to position

-- Colors (high color RGBA)
set_color(ink, paper)     -- Set foreground and background
set_ink(color)            -- Set foreground only
set_paper(color)          -- Set background only
rgba(r, g, b, a)          -- Create color value (0-255 each)
```

### Input Functions

```lua
key()                     -- Get current key (non-blocking)
waitKey()                 -- Wait for key press (blocking)
isKeyPressed(keycode)     -- Check if key is pressed
accept_at(x, y)           -- Line input at position (blocking)
accept_ar()               -- Line input at cursor (blocking)
```

### Graphics Operations

```lua
-- Basic shapes
draw_line(x1, y1, x2, y2, color)
draw_rect(x, y, w, h, color)
draw_circle(x, y, radius, color)

-- Filled shapes
fill_rect(x, y, w, h, color)
fill_circle(x, y, radius, color)

-- Layer control
graphics_clear()          -- Clear graphics layer
background_color(color)   -- Set background color
```

### Sprite System

```lua
-- Loading and management
sprite_load(id, filename)     -- Load 128x128 PNG sprite
sprite_show(id, x, y)         -- Show sprite at position
sprite_hide(id)               -- Hide sprite
sprite_move(id, x, y)         -- Move sprite (smooth positioning)

-- Transformations
sprite_scale(id, scale)       -- Scale sprite (1.0 = normal)
sprite_rotate(id, angle)      -- Rotate sprite (radians)
sprite_alpha(id, alpha)       -- Set transparency (0.0-1.0)
```

### Tile System

```lua
-- Tile management
tile_load(id, filename)           -- Load 128x128 PNG tile
tile_set(layer, x, y, tile_id)    -- Place tile in map
tile_get(layer, x, y)             -- Get tile at position

-- Viewport control
tile_scroll(layer, dx, dy)        -- Smooth scrolling
tile_set_viewport(layer, x, y)    -- Jump to map position
```

### Editor Control

```lua
editor_toggle()           -- Toggle full-screen editor
editor_visible()          -- Check if editor is visible
```

### Utility Functions

```lua
time_ms()                 -- Current time in milliseconds
sleep_ms(ms)              -- Sleep for milliseconds
```

## Key Codes

Common key codes for input functions:

```lua
ST_KEY_ESCAPE    -- ESC key
ST_KEY_RETURN    -- Enter key
ST_KEY_SPACE     -- Space bar
ST_KEY_F1        -- F1 (toggles editor)
ST_KEY_UP        -- Arrow keys
ST_KEY_DOWN
ST_KEY_LEFT
ST_KEY_RIGHT
ST_KEY_A         -- Letter keys
ST_KEY_B
-- ... (see SuperTerminal.h for complete list)
```

## Examples

The `examples/` directory contains several demonstration programs:

- `hello.lua` - Basic text output and C64-style commands
- `graphics_demo.lua` - Comprehensive graphics capabilities
- `sprites_demo.lua` - Sprite loading, animation, and effects
- `tiles_demo.lua` - Tile maps and scrolling systems
- `game_example.lua` - Simple game combining all systems

Run any example:

```bash
luarunner examples/graphics_demo.lua
```

## Controls

- **F1** - Toggle full-screen Lua editor
- **F2** - Save current editor content (when editor is visible)
- **F3** - Load file into editor
- **F4** - Execute current editor buffer
- **ESC** - Exit SuperTerminal

## Development

### Building from Source

```bash
# Clone repository
git clone https://github.com/superterminal/superterminal.git
cd superterminal

# Create build directory
mkdir build && cd build

# Configure build
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Build framework and tools
make -j$(sysctl -n hw.ncpu)

# Run development build
./luarunner ../examples/hello.lua
```

### Project Structure

```
superterminal/
├── SuperTerminal.framework/    # Framework headers and resources
│   ├── Headers/               # Public API headers
│   └── Resources/             # Shaders, fonts, assets
├── src/                       # Framework implementation
├── luarunner/                 # Lua script launcher
├── examples/                  # Example Lua programs
├── assets/                    # Sample sprites and tiles
└── docs/                      # Documentation
```

### Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests for new functionality
5. Submit a pull request

## Troubleshooting

### Common Issues

**LuaJIT not found**:
```bash
brew install luajit
```

**Skia not found**:
```bash
brew install skia
# Or build from source following Skia documentation
```

**Metal compilation errors**:
- Ensure Xcode Command Line Tools are installed
- Check that you're running on macOS 12.0+

**Performance issues**:
- SuperTerminal is optimized for Apple Silicon
- Intel Macs may experience reduced performance
- Close other GPU-intensive applications

### Debug Mode

Build in debug mode for additional logging:

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

## License

SuperTerminal is released under the MIT License. See LICENSE file for details.

## Credits

- **LuaJIT**: Mike Pall and contributors
- **Skia Graphics**: Google and contributors
- **Metal**: Apple Inc.
- **Inspiration**: Commodore 64, Apple II, and other 1980s computers

---

*SuperTerminal: Bringing the golden age of computing to Apple Silicon*# SuperTerminal
