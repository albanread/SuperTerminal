//
//  TextCommon.h
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#ifndef TEXTCOMMON_H
#define TEXTCOMMON_H

#import <simd/simd.h>
#include <stdint.h>

// Video mode constants for API compatibility
#define VIDEO_MODE_C64      0
#define VIDEO_MODE_STANDARD 1  
#define VIDEO_MODE_WIDE     2
#define VIDEO_MODE_ULTRAWIDE 3
#define VIDEO_MODE_RETRO    4
#define VIDEO_MODE_DENSE    5
#define VIDEO_MODE_AUTO     6

// Text scale constants (for public API)
#define TEXT_SCALE_AUTO 0
#define TEXT_SCALE_1X   1
#define TEXT_SCALE_2X   2
#define TEXT_SCALE_3X   3
#define TEXT_SCALE_4X   4

// Text grid constants
// Maximum grid capacity for text buffers (supports up to 90 columns as requested)
// Actual rendered grid is dynamic based on window size
static const int GRID_WIDTH = 160;  // Maximum 160 columns (supports long lines with horizontal scrolling)
static const int GRID_HEIGHT = 60; // Maximum 60 rows (supports taller windows)

// Text cell structure (may be defined elsewhere, e.g., SuperTerminal.h)
#ifndef TEXTCELL_DEFINED
#define TEXTCELL_DEFINED
struct TextCell {
    uint32_t character;    // Unicode codepoint
    simd_float4 inkColor;  // Foreground color
    simd_float4 paperColor; // Background color
};
#endif

// Vertex structure for text rendering
struct TextVertex {
    simd_float2 position;
    simd_float2 texCoord;
    simd_float4 inkColor;
    simd_float4 paperColor;
    uint32_t unicode;
    simd_float2 gridPos;  // Grid position (x, y) for cursor rendering
};

#endif /* TEXTCOMMON_H */