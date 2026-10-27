// Direct3D 11 backend for the OpenGL ES 1.x subset in compat/GLES/gl.h.
//
// All GL calls must come from one thread (the game thread). Rendering goes to
// an offscreen target that keeps its contents between frames, because the
// game relies on EGL_BUFFER_PRESERVED and only redraws what changed.
#pragma once

#include <d3d11_1.h>
#include <dxgi1_3.h>

// Creates the device, the swap chain for the window and the offscreen target.
// width and height are the size of the game's frame in physical pixels.
bool gles_init(IUnknown *core_window, int width, int height);

// The window's size in physical pixels and how it is turned relative to the
// display's native orientation, a DXGI_MODE_ROTATION. May be called from any
// thread; set it before gles_init and whenever the window changes.
void gles_set_window(int width, int height, int rotation);

// A point of the window in physical pixels as a point of the game's frame.
// The frame stays landscape and is shown turned in a portrait window.
void gles_window_to_frame(float x, float y, float *frame_x, float *frame_y);

// Copies the offscreen target to the swap chain, turned to the display's
// native orientation, and presents it.
void gles_present();

// Releases what the GPU driver can rebuild, as Windows asks on suspend.
void gles_trim();

void gles_get_size(int *width, int *height);
