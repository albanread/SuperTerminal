//
//  OverlayGraphicsLayer.h
//  SuperTerminal Framework - Overlay Graphics Layer (Graphics Layer 2)
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  A transparent overlay graphics layer that renders on top of all other layers
//  Based on the working MinimalGraphicsLayer
//

#ifndef OverlayGraphicsLayer_h
#define OverlayGraphicsLayer_h

#ifdef __cplusplus
extern "C" {
#endif

// C API for overlay graphics layer (Graphics Layer 2)
// This renders on top of all other layers as a transparent overlay

// Initialization
bool overlay_graphics_layer_initialize(void* device, int width, int height);
void overlay_graphics_layer_shutdown(void);
bool overlay_graphics_layer_is_initialized(void);

// Clear operations
void overlay_graphics_layer_clear(void);
void overlay_graphics_layer_clear_with_color(float r, float g, float b, float a);

// Drawing operations (same API as main graphics layer)
void overlay_graphics_layer_set_ink(float r, float g, float b, float a);
void overlay_graphics_layer_set_paper(float r, float g, float b, float a);
void overlay_graphics_layer_draw_line(float x1, float y1, float x2, float y2);
void overlay_graphics_layer_draw_rect(float x, float y, float w, float h);
void overlay_graphics_layer_fill_rect(float x, float y, float w, float h);
void overlay_graphics_layer_draw_circle(float x, float y, float radius);
void overlay_graphics_layer_fill_circle(float x, float y, float radius);
void overlay_graphics_layer_draw_text(float x, float y, const char* text, float fontSize);

// Visibility operations
void overlay_graphics_layer_show(void);
void overlay_graphics_layer_hide(void);
bool overlay_graphics_layer_is_visible(void);

// Render operations
void overlay_graphics_layer_present(void);
void overlay_graphics_layer_render_overlay(void* encoder, void* projectionMatrix);

#ifdef __cplusplus
}
#endif

#endif /* OverlayGraphicsLayer_h */