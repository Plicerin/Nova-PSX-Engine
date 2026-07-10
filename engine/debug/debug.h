// Debug & developer tools (spec 13): overlay, toggles, free camera,
// pause/step, screenshots, bitmap text (8x8 public-domain font).
//
// Key map (keyboard only, function keys so game input stays clean):
//   F1  overlay page cycle (off -> stats -> stats+OT viz -> stats+VRAM map)
//   F2  wireframe            F3  dithering        F4  color quantization
//   F5  affine/perspective   F6  z-buffer          F7  vertex snapping
//   F8  texture filter (nearest/bilinear)          F9  draw-order viz
//   F10 cycle internal resolution                  F11 cycle scale mode
//   F12 screenshots (raw fb + upscaled + overlay)  Alt+Enter fullscreen (in platform)
//   TAB free camera on/off (WASD+QE+arrows fly)    P pause sim   O step one frame
#pragma once
#include "engine/core/types.h"
#include "engine/renderer/framebuffer.h"
#include "engine/renderer/render.h"

void Debug_Init();

// Force the overlay page (0=off, 1=stats, 2=+OT, 3=+VRAM). For startup config
// and automated overlay screenshots; F1 cycles it at runtime.
void Debug_SetOverlayPage(int page);

// Handle debug keys; call once per frame. May mutate g_config, request
// screenshots, toggle pause, drive the free camera (dt_ms for fly speed).
void Debug_Update(RenderContext* rc, i32 dt_ms);

bool Debug_Paused();          // sim paused (render keeps running)
bool Debug_StepFrame();       // true exactly once after O is pressed while paused

// Free camera: when active, call Debug_GetCamera to override the scene camera.
bool Debug_FreeCamActive();
void Debug_ToggleFreeCam(RenderContext* rc);   // same as pressing TAB
// Games may claim TAB for their own UI (e.g. combat menus): when disallowed,
// the TAB freecam toggle is ignored (an active freecam is forced off).
void Debug_AllowFreeCam(bool allow);
const Camera* Debug_FreeCam();
void Debug_SyncFreeCam(const Camera* scene_cam); // seed free cam from scene cam

// Bitmap text straight into the framebuffer (goes through the quantize path).
// 8x8 font, printf-style. Returns pixel width drawn.
int  Debug_Text(Framebuffer* fb, int x, int y, u8 r, u8 g, u8 b, const char* fmt, ...);

// Draw the overlay (stats text, OT histogram, VRAM page map) per current page.
// fps/frame_ms measured by the main loop; rc supplies stats after Rc_Flush.
void Debug_DrawOverlay(Framebuffer* fb, const RenderContext* rc, i32 fps_x10, i32 frame_ms_x10);

// Screenshots into build/bin/screenshots/: raw internal fb ("raw_NNN.bmp"),
// nearest-upscaled x3 ("up_NNN.bmp"). If with_overlay, capture after overlay.
void Debug_RequestScreenshot();
bool Debug_ScreenshotPending();
void Debug_TakeScreenshot(const Framebuffer* fb);  // call at end of frame if pending

// BMP writer used for screenshots (24-bit BGR, bottom-up).
bool Debug_WriteBMP(const char* path, const u32* xrgb, int w, int h, int scale_n);
