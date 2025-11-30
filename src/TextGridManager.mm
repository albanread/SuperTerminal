//
//  TextGridManager.mm
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import "TextGridManager.h"
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <math.h>

// Global instance
static TextGridManager* g_sharedManager = nil;

// Callback system for grid recalculation notifications
static NSMutableArray<NSValue*>* g_callbacks = nil;
static NSMutableArray<NSValue*>* g_callbackUserData = nil;

// Default font metrics for 8x8 bitmap font
static const FontMetrics kDefaultFontMetrics = {
    .baseCellWidth = 8.0f,
    .baseCellHeight = 8.0f,
    .aspectRatio = 1.0f,
    .atlasWidth = 128.0f,
    .atlasHeight = 48.0f,
    .charTexSizeX = 8.0f / 128.0f,   // 0.0625
    .charTexSizeY = 8.0f / 48.0f     // 0.1667
};

@implementation TextGridManager

#pragma mark - Initialization

+ (instancetype)sharedManager {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        g_sharedManager = [[TextGridManager alloc] initWithFontMetrics:kDefaultFontMetrics];
        g_callbacks = [[NSMutableArray alloc] init];
        g_callbackUserData = [[NSMutableArray alloc] init];
    });
    return g_sharedManager;
}

- (instancetype)initWithFontMetrics:(FontMetrics)metrics {
    self = [super init];
    if (self) {
        _fontMetrics = metrics;
        _currentMode = TEXT_MODE_64x44;
        _scaleMode = TEXT_SCALE_MODE_AUTO;
        _maintainAspectRatio = YES;
        _centerGrid = YES;
        _lastViewportSize = CGSizeZero;

        // Initialize with default layout
        _currentLayout = [self calculateLayoutForViewport:CGSizeMake(1024, 768)
                                                    mode:TEXT_MODE_64x44
                                               scaleMode:TEXT_SCALE_MODE_AUTO];
    }
    return self;
}

#pragma mark - Main Layout Calculation

- (TextGridLayout)calculateLayoutForViewport:(CGSize)viewportSize
                                        mode:(TextMode)mode
                                   scaleMode:(TextScaleMode)scaleMode {

    TextGridLayout layout = {0};

    // Handle auto mode
    TextMode actualMode = mode;
    if (mode == TEXT_MODE_AUTO) {
        actualMode = [self findBestAutoModeForViewport:viewportSize];
    }

    // Get target grid size for this mode
    CGSize targetGrid = [self getTargetGridSizeForMode:actualMode];

    // Calculate optimal layout
    layout = [self fitGridToViewport:targetGrid
                           viewport:viewportSize
                        fontMetrics:_fontMetrics
                          scaleMode:scaleMode];

    // Set additional properties
    layout.actualMode = actualMode;
    layout.isRetina = [self isRetinaDisplay:viewportSize];
    layout.devicePixelRatio = [self getDevicePixelRatio];

    // Adjust for better fit if needed
    if (scaleMode == TEXT_SCALE_MODE_AUTO) {
        CGSize adjustedGrid = [self adjustGridForBetterFit:targetGrid
                                                  viewport:viewportSize
                                               fontMetrics:_fontMetrics
                                                 tolerance:0.1f];

        if (!CGSizeEqualToSize(adjustedGrid, targetGrid)) {
            layout = [self fitGridToViewport:adjustedGrid
                                   viewport:viewportSize
                                fontMetrics:_fontMetrics
                                  scaleMode:scaleMode];
            layout.actualMode = actualMode;
        }
    }

    // Update cached values
    _currentLayout = layout;
    _lastViewportSize = viewportSize;

    // Notify callbacks of layout change
    [self notifyCallbacks:layout];

    return layout;
}

#pragma mark - Mode-Specific Calculations

- (CGSize)getTargetGridSizeForMode:(TextMode)mode {
    switch (mode) {
        case TEXT_MODE_20x25:
            return CGSizeMake(20, 25);
        case TEXT_MODE_40x25:
            return CGSizeMake(40, 25);
        case TEXT_MODE_40x50:
            return CGSizeMake(40, 50);
        case TEXT_MODE_64x44:
            return CGSizeMake(64, 44);
        case TEXT_MODE_80x25:
            return CGSizeMake(80, 25);
        case TEXT_MODE_80x50:
            return CGSizeMake(80, 50);
        case TEXT_MODE_120x60:
            return CGSizeMake(120, 60);
        default:
            return CGSizeMake(64, 44); // Fallback to balanced default
    }
}

- (TextMode)findBestAutoModeForViewport:(CGSize)viewportSize {
    // Calculate aspect ratio
    float aspectRatio = viewportSize.width / viewportSize.height;

    // Determine best mode based on viewport size and aspect ratio
    if (viewportSize.width < 640 || viewportSize.height < 480) {
        return TEXT_MODE_20x25;  // Small window - use chunky text
    }

    if (aspectRatio > 2.5f) {
        return TEXT_MODE_120x60;  // Very wide display
    } else if (aspectRatio > 1.8f) {
        return TEXT_MODE_80x50;   // Wide display
    } else if (viewportSize.width > 1920 && viewportSize.height > 1080) {
        return TEXT_MODE_80x50;   // High resolution - use dense mode
    } else {
        return TEXT_MODE_64x44;   // Standard mode for most cases
    }
}

#pragma mark - Scaling Calculations

- (float)calculateOptimalScaleForGrid:(CGSize)gridSize
                            viewport:(CGSize)viewportSize
                         fontMetrics:(FontMetrics)metrics {

    // Calculate scale needed to fit grid in viewport
    float scaleX = viewportSize.width / (gridSize.width * metrics.baseCellWidth);
    float scaleY = viewportSize.height / (gridSize.height * metrics.baseCellHeight);

    float scale;
    if (self.maintainAspectRatio) {
        // Use smaller scale to ensure both dimensions fit
        scale = fminf(scaleX, scaleY);
    } else {
        // Use average of both scales
        scale = (scaleX + scaleY) * 0.5f;
    }

    // Ensure minimum readability
    if (scale < 0.5f) scale = 0.5f;

    // For Retina displays, prefer integer scales when possible
    if ([self isRetinaDisplay:viewportSize]) {
        float deviceRatio = [self getDevicePixelRatio];
        scale *= deviceRatio;

        // Round to nearest integer for crisp pixels
        if (scale >= 1.0f) {
            scale = roundf(scale);
        }

        scale /= deviceRatio;
    }

    return scale;
}

- (TextGridLayout)fitGridToViewport:(CGSize)gridSize
                           viewport:(CGSize)viewportSize
                        fontMetrics:(FontMetrics)metrics
                          scaleMode:(TextScaleMode)scaleMode {

    TextGridLayout layout = {0};

    // Start with target grid dimensions
    int targetGridWidth = (int)gridSize.width;
    int targetGridHeight = (int)gridSize.height;

    // STEP 1: Calculate maximum scale that fits TARGET grid in viewport while maintaining proportions
    float scaleX = viewportSize.width / (targetGridWidth * metrics.baseCellWidth);
    float scaleY = viewportSize.height / (targetGridHeight * metrics.baseCellHeight);
    float scale = fminf(scaleX, scaleY);  // Use smaller scale to ensure both dimensions fit

    // STEP 2: Calculate scaled cell size (maintains font proportions)
    float scaledCellWidth = metrics.baseCellWidth * scale;
    float scaledCellHeight = metrics.baseCellHeight * scale;

    // STEP 3: Calculate how much space the TARGET grid occupies
    float targetGridPixelWidth = targetGridWidth * scaledCellWidth;
    float targetGridPixelHeight = targetGridHeight * scaledCellHeight;

    // STEP 4: Calculate leftover space after target grid
    float leftoverWidth = viewportSize.width - targetGridPixelWidth;
    float leftoverHeight = viewportSize.height - targetGridPixelHeight;

    // STEP 5: Add extra columns/rows if full cells fit in leftover space
    int extraColumns = (int)(leftoverWidth / scaledCellWidth);
    int extraRows = (int)(leftoverHeight / scaledCellHeight);

    // STEP 6: Final grid dimensions
    int finalGridWidth = targetGridWidth + extraColumns;
    int finalGridHeight = targetGridHeight + extraRows;

    // Set layout values
    layout.gridWidth = finalGridWidth;
    layout.gridHeight = finalGridHeight;
    layout.cellWidth = scaledCellWidth;
    layout.cellHeight = scaledCellHeight;
    layout.scaleFactorX = scale;
    layout.scaleFactorY = scale;
    layout.offsetX = 0.0f;
    layout.offsetY = 0.0f;

    // Write debug to file
    FILE *debugFile = fopen("/tmp/superterminal_debug.log", "a");
    if (debugFile) {
        fprintf(debugFile, "\n=== TEXT GRID MANAGER CALCULATION ===\n");
        fprintf(debugFile, "INPUT:\n");
        fprintf(debugFile, "  Viewport: %.0f x %.0f points\n", viewportSize.width, viewportSize.height);
        fprintf(debugFile, "  Target grid: %d x %d\n", targetGridWidth, targetGridHeight);
        fprintf(debugFile, "  Base cell: %.1f x %.1f points\n", metrics.baseCellWidth, metrics.baseCellHeight);
        fprintf(debugFile, "\nCALCULATION:\n");
        fprintf(debugFile, "  Scale X: %.3f, Scale Y: %.3f\n", scaleX, scaleY);
        fprintf(debugFile, "  Best-fit scale: %.3f (min of both)\n", scale);
        fprintf(debugFile, "  Scaled cell: %.1f x %.1f points\n", scaledCellWidth, scaledCellHeight);
        fprintf(debugFile, "  Target coverage: %.0f x %.0f points\n", targetGridPixelWidth, targetGridPixelHeight);
        fprintf(debugFile, "  Leftover space: %.0f x %.0f points\n", leftoverWidth, leftoverHeight);
        fprintf(debugFile, "  Extra cells: +%d cols, +%d rows\n", extraColumns, extraRows);
        fprintf(debugFile, "\nRESULT:\n");
        fprintf(debugFile, "  Final grid: %d x %d\n", finalGridWidth, finalGridHeight);
        fprintf(debugFile, "  Final coverage: %.0f x %.0f points\n",
                finalGridWidth * scaledCellWidth, finalGridHeight * scaledCellHeight);
        fprintf(debugFile, "  Remaining gap: %.0f x %.0f points\n",
                viewportSize.width - (finalGridWidth * scaledCellWidth),
                viewportSize.height - (finalGridHeight * scaledCellHeight));
        fprintf(debugFile, "=====================================\n");
        fflush(debugFile);
        fclose(debugFile);
    }

    return layout;
}

#pragma mark - Retina/High-DPI Handling

- (BOOL)isRetinaDisplay:(CGSize)viewportSize {
    NSScreen* mainScreen = [NSScreen mainScreen];
    if (mainScreen) {
        return mainScreen.backingScaleFactor > 1.0;
    }

    // Fallback: assume Retina if viewport is very large
    return (viewportSize.width > 1440 && viewportSize.height > 900);
}

- (float)getDevicePixelRatio {
    NSScreen* mainScreen = [NSScreen mainScreen];
    if (mainScreen) {
        return mainScreen.backingScaleFactor;
    }
    return 1.0f;
}

- (CGSize)adjustForRetina:(CGSize)size {
    float ratio = [self getDevicePixelRatio];
    return CGSizeMake(size.width * ratio, size.height * ratio);
}

#pragma mark - Grid Adjustment Algorithms

- (CGSize)adjustGridForBetterFit:(CGSize)originalGrid
                        viewport:(CGSize)viewportSize
                     fontMetrics:(FontMetrics)metrics
                       tolerance:(float)tolerance {

    CGSize bestGrid = originalGrid;
    float bestFillRatio = [self calculateFillRatio:originalGrid viewport:viewportSize fontMetrics:metrics];

    // Try variations around the original grid size
    int maxVariation = 10;

    for (int dw = -maxVariation; dw <= maxVariation; dw++) {
        for (int dh = -maxVariation; dh <= maxVariation; dh++) {
            CGSize testGrid = CGSizeMake(originalGrid.width + dw, originalGrid.height + dh);

            // Ensure minimum grid size
            if (testGrid.width < 20 || testGrid.height < 10) continue;

            float fillRatio = [self calculateFillRatio:testGrid viewport:viewportSize fontMetrics:metrics];

            // Prefer grids that fill more of the viewport
            if (fillRatio > bestFillRatio + tolerance) {
                bestGrid = testGrid;
                bestFillRatio = fillRatio;
            }
        }
    }

    return bestGrid;
}

- (float)calculateFillRatio:(CGSize)gridSize viewport:(CGSize)viewportSize fontMetrics:(FontMetrics)metrics {
    float scale = [self calculateOptimalScaleForGrid:gridSize viewport:viewportSize fontMetrics:metrics];

    float usedWidth = gridSize.width * metrics.baseCellWidth * scale;
    float usedHeight = gridSize.height * metrics.baseCellHeight * scale;

    float fillRatioX = usedWidth / viewportSize.width;
    float fillRatioY = usedHeight / viewportSize.height;

    // Return the minimum to ensure both dimensions fit
    return fminf(fillRatioX, fillRatioY);
}

#pragma mark - Utility Functions

- (NSString*)descriptionForMode:(TextMode)mode {
    switch (mode) {
        case TEXT_MODE_20x25: return @"Giant (20×25)";
        case TEXT_MODE_40x25: return @"Large (40×25)";
        case TEXT_MODE_40x50: return @"Medium (40×50)";
        case TEXT_MODE_64x44: return @"Standard (64×44)";
        case TEXT_MODE_80x25: return @"Compact (80×25)";
        case TEXT_MODE_80x50: return @"Dense (80×50)";
        case TEXT_MODE_120x60: return @"UltraWide (120×60)";
        default: return @"Unknown";
    }
}

- (FontMetrics)getDefaultFontMetrics {
    return _fontMetrics;
}

- (BOOL)isValidGridSize:(CGSize)gridSize {
    return gridSize.width >= 10 && gridSize.width <= 200 &&
           gridSize.height >= 5 && gridSize.height <= 100;
}

#pragma mark - Debug and Info

- (void)logLayoutInfo:(TextGridLayout)layout {
    NSLog(@"Text Grid Layout:");
    NSLog(@"  Mode: %@", [self descriptionForMode:layout.actualMode]);
    NSLog(@"  Grid: %d×%d", layout.gridWidth, layout.gridHeight);
    NSLog(@"  Cell size: %.1f×%.1f pts", layout.cellWidth, layout.cellHeight);
    NSLog(@"  Scale factors: %.2f×%.2f", layout.scaleFactorX, layout.scaleFactorY);
    NSLog(@"  Offset: %.1f,%.1f", layout.offsetX, layout.offsetY);
    NSLog(@"  Retina: %@, Device ratio: %.1f", layout.isRetina ? @"YES" : @"NO", layout.devicePixelRatio);

    float totalWidth = layout.gridWidth * layout.cellWidth;
    float totalHeight = layout.gridHeight * layout.cellHeight;
    NSLog(@"  Total grid size: %.1f×%.1f pts", totalWidth, totalHeight);
}

- (NSDictionary*)getLayoutDictionary:(TextGridLayout)layout {
    return @{
        @"mode": @(layout.actualMode),
        @"gridWidth": @(layout.gridWidth),
        @"gridHeight": @(layout.gridHeight),
        @"cellWidth": @(layout.cellWidth),
        @"cellHeight": @(layout.cellHeight),
        @"scaleFactorX": @(layout.scaleFactorX),
        @"scaleFactorY": @(layout.scaleFactorY),
        @"offsetX": @(layout.offsetX),
        @"offsetY": @(layout.offsetY),
        @"isRetina": @(layout.isRetina),
        @"devicePixelRatio": @(layout.devicePixelRatio)
    };
}

- (void)notifyCallbacks:(TextGridLayout)layout {
    // Call all registered callbacks
    for (NSUInteger i = 0; i < g_callbacks.count; i++) {
        NSValue* callbackValue = g_callbacks[i];
        NSValue* userDataValue = g_callbackUserData[i];

        TextGridRecalcCallback callback = (TextGridRecalcCallback)[callbackValue pointerValue];
        void* userData = [userDataValue pointerValue];

        if (callback) {
            callback(layout, userData);
        }
    }
}

@end

#pragma mark - C API Implementation

static TextGridManager* getManager() {
    return [TextGridManager sharedManager];
}

TextGridLayout text_grid_calculate_layout(float viewportWidth, float viewportHeight,
                                         TextMode mode, TextScaleMode scaleMode) {
    TextGridManager* manager = getManager();
    return [manager calculateLayoutForViewport:CGSizeMake(viewportWidth, viewportHeight)
                                          mode:mode
                                     scaleMode:scaleMode];
}

void text_grid_set_mode(TextMode mode) {
    getManager().currentMode = mode;
}

TextMode text_grid_get_mode(void) {
    return getManager().currentMode;
}

void text_grid_set_scale_mode(TextScaleMode scaleMode) {
    getManager().scaleMode = scaleMode;
}

TextScaleMode text_grid_get_scale_mode(void) {
    return getManager().scaleMode;
}

void text_grid_get_dimensions(int* width, int* height) {
    TextGridLayout layout = getManager().currentLayout;
    if (width) *width = layout.gridWidth;
    if (height) *height = layout.gridHeight;
}

void text_grid_get_cell_size(float* width, float* height) {
    TextGridLayout layout = getManager().currentLayout;
    if (width) *width = layout.cellWidth;
    if (height) *height = layout.cellHeight;
}

void text_grid_get_scale_factors(float* scaleX, float* scaleY) {
    TextGridLayout layout = getManager().currentLayout;
    if (scaleX) *scaleX = layout.scaleFactorX;
    if (scaleY) *scaleY = layout.scaleFactorY;
}

void text_grid_get_offsets(float* offsetX, float* offsetY) {
    TextGridLayout layout = getManager().currentLayout;
    if (offsetX) *offsetX = layout.offsetX;
    if (offsetY) *offsetY = layout.offsetY;
}

void text_grid_cycle_mode(void) {
    TextGridManager* manager = getManager();
    TextVideoMode currentMode = manager.currentMode;

    FILE *debugFile = fopen("/tmp/superterminal_debug.log", "a");
    if (debugFile) {
        fprintf(debugFile, "\n=== TEXT_GRID_CYCLE_MODE CALLED ===\n");
        fprintf(debugFile, "CURRENT MODE: %d\n", (int)currentMode);
        fflush(debugFile);
        fclose(debugFile);
    }
    TextVideoMode nextMode = (TextVideoMode)((currentMode + 1) % 7); // 7 total modes
    manager.currentMode = nextMode;
}

void text_grid_cycle_scale(void) {
    TextGridManager* manager = getManager();
    TextScaleMode currentScale = manager.scaleMode;
    TextScaleMode nextScale = (TextScaleMode)((currentScale + 1) % 5); // 5 total scale modes
    manager.scaleMode = nextScale;
}

TextMode text_grid_find_best_mode(float viewportWidth, float viewportHeight) {
    TextGridManager* manager = getManager();
    return [manager findBestAutoModeForViewport:CGSizeMake(viewportWidth, viewportHeight)];
}

void text_grid_auto_fit(float viewportWidth, float viewportHeight) {
    TextGridManager* manager = getManager();
    TextVideoMode bestMode = [manager findBestAutoModeForViewport:CGSizeMake(viewportWidth, viewportHeight)];
    manager.currentMode = bestMode;
    manager.scaleMode = TEXT_SCALE_MODE_AUTO;
}

void text_grid_screen_to_grid(float screenX, float screenY, int* gridX, int* gridY) {
    TextGridLayout layout = getManager().currentLayout;

    float adjustedX = screenX - layout.offsetX;
    float adjustedY = screenY - layout.offsetY;

    if (gridX) *gridX = (int)(adjustedX / layout.cellWidth);
    if (gridY) *gridY = (int)(adjustedY / layout.cellHeight);
}

void text_grid_grid_to_screen(int gridX, int gridY, float* screenX, float* screenY) {
    TextGridLayout layout = getManager().currentLayout;

    if (screenX) *screenX = gridX * layout.cellWidth + layout.offsetX;
    if (screenY) *screenY = gridY * layout.cellHeight + layout.offsetY;
}

BOOL text_grid_validate_layout(TextGridLayout layout) {
    return layout.gridWidth > 0 && layout.gridHeight > 0 &&
           layout.cellWidth > 0 && layout.cellHeight > 0 &&
           layout.scaleFactorX > 0 && layout.scaleFactorY > 0;
}

void text_grid_recalculate_for_viewport(float viewportWidth, float viewportHeight) {
    TextGridManager* manager = getManager();

    // Recalculate layout for new viewport size
    TextGridLayout newLayout = [manager calculateLayoutForViewport:CGSizeMake(viewportWidth, viewportHeight)
                                                             mode:manager.currentMode
                                                        scaleMode:manager.scaleMode];

    // Update cached layout
    manager.currentLayout = newLayout;
    manager.lastViewportSize = CGSizeMake(viewportWidth, viewportHeight);

    // Log the recalculation for debugging
    NSLog(@"TextGridManager: Recalculated for viewport %.0fx%.0f -> grid %dx%d, cells %.1fx%.1f",
          viewportWidth, viewportHeight, newLayout.gridWidth, newLayout.gridHeight,
          newLayout.cellWidth, newLayout.cellHeight);
}

void text_grid_register_recalc_callback(TextGridRecalcCallback callback, void* userData) {
    if (callback && g_callbacks && g_callbackUserData) {
        NSValue* callbackValue = [NSValue valueWithPointer:(void*)callback];
        NSValue* userDataValue = [NSValue valueWithPointer:userData];

        [g_callbacks addObject:callbackValue];
        [g_callbackUserData addObject:userDataValue];
    }
}

void text_grid_unregister_recalc_callback(TextGridRecalcCallback callback) {
    if (callback && g_callbacks && g_callbackUserData) {
        for (NSUInteger i = 0; i < g_callbacks.count; i++) {
            NSValue* callbackValue = g_callbacks[i];
            if ([callbackValue pointerValue] == (void*)callback) {
                [g_callbacks removeObjectAtIndex:i];
                [g_callbackUserData removeObjectAtIndex:i];
                break;
            }
        }
    }
}

void text_grid_force_update(void) {
    TextGridManager* manager = getManager();

    // Force immediate callback notifications with current layout
    [manager notifyCallbacks:manager.currentLayout];

    NSLog(@"TextGridManager: Forced update - notified all callbacks");
}

void text_grid_clamp_cursor(int* x, int* y) {
    TextGridLayout layout = getManager().currentLayout;

    if (x) {
        *x = MAX(0, MIN(*x, layout.gridWidth - 1));
    }
    if (y) {
        *y = MAX(0, MIN(*y, layout.gridHeight - 1));
    }
}
