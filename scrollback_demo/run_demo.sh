#!/bin/bash

# Scrollback Buffer Demo Runner
# Runs the scrollback_demo.lua script in SuperTerminal

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEMO_SCRIPT="$SCRIPT_DIR/scrollback_demo.lua"

# Find SuperTerminal executable
SUPERTERMINAL_APP="$SCRIPT_DIR/../build/SuperTerminal.app/Contents/MacOS/SuperTerminal"

# Check if executable exists
if [ ! -f "$SUPERTERMINAL_APP" ]; then
    echo "Error: SuperTerminal executable not found at: $SUPERTERMINAL_APP"
    echo "Please build SuperTerminal first using: cd .. && ./build.sh"
    exit 1
fi

# Check if demo script exists
if [ ! -f "$DEMO_SCRIPT" ]; then
    echo "Error: Demo script not found at: $DEMO_SCRIPT"
    exit 1
fi

echo "Starting Scrollback Buffer Demo..."
echo "Script: $DEMO_SCRIPT"
echo ""
echo "Controls:"
echo "  Page Up       - Scroll up one page"
echo "  Page Down     - Scroll down one page"
echo "  Cmd+Home      - Jump to top of buffer"
echo "  Cmd+End       - Jump to bottom (re-enable auto-scroll)"
echo "  ESC           - Exit demo"
echo ""

# Run SuperTerminal with the demo script
"$SUPERTERMINAL_APP" "$DEMO_SCRIPT"

echo ""
echo "Demo finished."
