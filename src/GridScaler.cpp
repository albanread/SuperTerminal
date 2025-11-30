//
//  GridScaler.cpp
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Simple grid scaling - ONE function to rule them all
//

#include "GridScaler.h"
#include <algorithm>
#include <cmath>

GridScaleResult calculateGridScale(int targetWidth, int targetHeight, 
                                  float viewportWidth, float viewportHeight,
                                  float baseCellWidth, float baseCellHeight) {
    
    GridScaleResult result = {0};
    
    // Calculate font aspect ratio to maintain proportions
    float fontAspectRatio = baseCellWidth / baseCellHeight;
    
    // Calculate maximum scale that fits the target grid in viewport
    float scaleX = viewportWidth / (targetWidth * baseCellWidth);
    float scaleY = viewportHeight / (targetHeight * baseCellHeight);
    
    // Use the smaller scale to ensure everything fits and maintain proportions
    float baseScale = std::min(scaleX, scaleY);
    
    // Calculate actual cell size with this scale
    float scaledCellWidth = baseCellWidth * baseScale;
    float scaledCellHeight = baseCellHeight * baseScale;
    
    // Now calculate how many cells actually fit in the viewport
    int actualGridWidth = (int)std::floor(viewportWidth / scaledCellWidth);
    int actualGridHeight = (int)std::floor(viewportHeight / scaledCellHeight);
    
    // Ensure we don't go below the target (but we can go above to fill gaps)
    if (actualGridWidth < targetWidth) actualGridWidth = targetWidth;
    if (actualGridHeight < targetHeight) actualGridHeight = targetHeight;
    
    // If we went below target, recalculate scale to fit
    if (actualGridWidth == targetWidth && actualGridHeight == targetHeight) {
        // Use the original scale
        result.cellScale = baseScale;
    } else {
        // Recalculate scale for the actual grid size
        float newScaleX = viewportWidth / (actualGridWidth * baseCellWidth);
        float newScaleY = viewportHeight / (actualGridHeight * baseCellHeight);
        result.cellScale = std::min(newScaleX, newScaleY);
    }
    
    // Fill in the result
    result.gridWidth = actualGridWidth;
    result.gridHeight = actualGridHeight;
    result.totalWidth = actualGridWidth * baseCellWidth * result.cellScale;
    result.totalHeight = actualGridHeight * baseCellHeight * result.cellScale;
    
    return result;
}