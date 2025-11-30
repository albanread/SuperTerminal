//
//  GridScaler.h
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Simple grid scaling - ONE function to rule them all
//

#ifndef GridScaler_h
#define GridScaler_h

#ifdef __cplusplus
extern "C" {
#endif

// Grid scaling result
typedef struct {
    int gridWidth;          // Actual grid width (may be larger than target)
    int gridHeight;         // Actual grid height (may be larger than target)
    float cellScale;        // Scale factor to apply to base font size
    float totalWidth;       // Total width covered by grid
    float totalHeight;      // Total height covered by grid
} GridScaleResult;

// The ONE function that does it all:
// - Takes target grid size and viewport size
// - Returns actual grid size and scale factor
// - Maintains font proportions
// - Fills gaps with extra cells if possible
GridScaleResult calculateGridScale(int targetWidth, int targetHeight, 
                                  float viewportWidth, float viewportHeight,
                                  float baseCellWidth, float baseCellHeight);

#ifdef __cplusplus
}
#endif

#endif /* GridScaler_h */