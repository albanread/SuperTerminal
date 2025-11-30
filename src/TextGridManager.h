//
//  TextGridManager.h
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#ifndef TextGridManager_h
#define TextGridManager_h

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <simd/simd.h>
#include "CoreTextRenderer.h"

#ifdef __cplusplus
extern "C" {
#endif

// Use TextMode from CoreTextRenderer.h
typedef TextMode TextVideoMode;

// Define TEXT_MODE_AUTO as an extension
#define TEXT_MODE_AUTO 7

// Text scaling options
typedef enum {
    TEXT_SCALE_MODE_AUTO = 0,   // Auto-calculate best fit
    TEXT_SCALE_MODE_1X = 1,     // Native pixel size
    TEXT_SCALE_MODE_2X = 2,     // 2x scaling
    TEXT_SCALE_MODE_3X = 3,     // 3x scaling
    TEXT_SCALE_MODE_4X = 4      // 4x scaling
} TextScaleMode;

// Grid calculation result
typedef struct {
    int gridWidth;              // Calculated grid width (columns)
    int gridHeight;             // Calculated grid height (rows)
    float cellWidth;            // Cell width in points
    float cellHeight;           // Cell height in points
    float scaleFactorX;         // Horizontal scale factor
    float scaleFactorY;         // Vertical scale factor
    float offsetX;              // Horizontal centering offset
    float offsetY;              // Vertical centering offset
    BOOL isRetina;              // Whether display is Retina/high-DPI
    float devicePixelRatio;     // Device pixel ratio
    TextMode actualMode;   // Actual mode used (for AUTO mode)
} TextGridLayout;

// Font metrics for different base sizes
typedef struct {
    float baseCellWidth;        // Base character width in points
    float baseCellHeight;       // Base character height in points
    float aspectRatio;          // Width/height aspect ratio
    float atlasWidth;           // Font atlas texture width
    float atlasHeight;          // Font atlas texture height
    float charTexSizeX;         // Character texture coordinate width
    float charTexSizeY;         // Character texture coordinate height
} FontMetrics;

// Callback function type for grid recalculation notifications
typedef void (*TextGridRecalcCallback)(TextGridLayout layout, void* userData);

// Text grid manager interface
@interface TextGridManager : NSObject

@property (nonatomic, assign) TextMode currentMode;
@property (nonatomic, assign) TextScaleMode scaleMode;
@property (nonatomic, assign) BOOL maintainAspectRatio;
@property (nonatomic, assign) BOOL centerGrid;
@property (nonatomic, assign) CGSize lastViewportSize;
@property (nonatomic, assign) TextGridLayout currentLayout;
@property (nonatomic, assign) FontMetrics fontMetrics;

// Initialization
+ (instancetype)sharedManager;
- (instancetype)initWithFontMetrics:(FontMetrics)metrics;

// Main layout calculation
- (TextGridLayout)calculateLayoutForViewport:(CGSize)viewportSize
                                        mode:(TextMode)mode
                                   scaleMode:(TextScaleMode)scaleMode;

// Mode-specific calculations
- (CGSize)getTargetGridSizeForMode:(TextMode)mode;
- (TextMode)findBestAutoModeForViewport:(CGSize)viewportSize;

// Scaling calculations
- (float)calculateOptimalScaleForGrid:(CGSize)gridSize 
                            viewport:(CGSize)viewportSize
                         fontMetrics:(FontMetrics)metrics;

- (TextGridLayout)fitGridToViewport:(CGSize)gridSize
                           viewport:(CGSize)viewportSize
                        fontMetrics:(FontMetrics)metrics
                          scaleMode:(TextScaleMode)scaleMode;

// Retina/High-DPI handling
- (BOOL)isRetinaDisplay:(CGSize)viewportSize;
- (float)getDevicePixelRatio;
- (CGSize)adjustForRetina:(CGSize)size;

// Grid adjustment algorithms
- (CGSize)adjustGridForBetterFit:(CGSize)originalGrid
                        viewport:(CGSize)viewportSize
                     fontMetrics:(FontMetrics)metrics
                       tolerance:(float)tolerance;

// Utility functions
- (NSString*)descriptionForMode:(TextMode)mode;
- (FontMetrics)getDefaultFontMetrics;
- (BOOL)isValidGridSize:(CGSize)gridSize;

// Debug and info
- (void)logLayoutInfo:(TextGridLayout)layout;
- (NSDictionary*)getLayoutDictionary:(TextGridLayout)layout;

@end

// C API for integration with existing code
TextGridLayout text_grid_calculate_layout(float viewportWidth, float viewportHeight, 
                                         TextMode mode, TextScaleMode scaleMode);

void text_grid_set_mode(TextMode mode);
TextMode text_grid_get_mode(void);

void text_grid_set_scale_mode(TextScaleMode scaleMode);
TextScaleMode text_grid_get_scale_mode(void);

// Viewport resize handling
void text_grid_recalculate_for_viewport(float viewportWidth, float viewportHeight);
void text_grid_force_update(void);

// Callback registration for grid changes
void text_grid_register_recalc_callback(TextGridRecalcCallback callback, void* userData);
void text_grid_unregister_recalc_callback(TextGridRecalcCallback callback);

// Get current grid dimensions
void text_grid_get_dimensions(int* width, int* height);
void text_grid_get_cell_size(float* width, float* height);
void text_grid_get_scale_factors(float* scaleX, float* scaleY);
void text_grid_get_offsets(float* offsetX, float* offsetY);

// Mode switching
void text_grid_cycle_mode(void);
void text_grid_cycle_scale(void);

// Auto-fitting
TextMode text_grid_find_best_mode(float viewportWidth, float viewportHeight);
void text_grid_auto_fit(float viewportWidth, float viewportHeight);

// Coordinate conversion
void text_grid_screen_to_grid(float screenX, float screenY, int* gridX, int* gridY);
void text_grid_grid_to_screen(int gridX, int gridY, float* screenX, float* screenY);

// Validation
BOOL text_grid_validate_layout(TextGridLayout layout);
void text_grid_clamp_cursor(int* x, int* y);

#ifdef __cplusplus
}
#endif

#endif /* TextGridManager_h */