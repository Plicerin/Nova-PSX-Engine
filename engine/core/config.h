// Global engine configuration and authenticity toggles.
// Defaults follow the PSX-Authentic spec: affine mapping ON, z-buffer OFF,
// nearest sampling, quantization + dithering ON, integer vertex snapping.
#pragma once
#include "engine/core/types.h"

enum ScaleMode : u8 {
    SCALE_INTEGER = 0,   // largest integer multiple, centered (pillarbox/letterbox)
    SCALE_FIT     = 1,   // fit window preserving aspect (fractional, still nearest)
    SCALE_STRETCH = 2,   // fill window ignoring aspect
    SCALE_MODE_COUNT
};

// Supported internal resolutions (spec 5.1). Index into kResModes.
struct ResMode { i16 w, h; };
constexpr ResMode kResModes[] = {
    {320, 240}, {320, 224}, {368, 240}, {512, 240}, {640, 480}, {320, 180}
};
constexpr int kResModeCount = 6;   // last entry (320x180) is 16:9, non-PS1
constexpr int FB_MAX_W = 640;
constexpr int FB_MAX_H = 480;

struct EngineConfig {
    // --- internal resolution / presentation ---
    int  res_mode        = 0;      // index into kResModes (default 320x240)
    ScaleMode scale_mode = SCALE_INTEGER;
    bool fullscreen      = false;

    // --- authenticity toggles (debug) ---
    bool perspective_correct = false; // OFF = affine texture mapping (authentic)
    bool bilinear_filter     = false; // OFF = nearest-neighbor (authentic)
    bool zbuffer             = false; // OFF = painter's / ordering table (authentic)
    bool color_quantization  = true;  // quantize to 5 bits/channel on framebuffer write
    bool dithering           = true;  // PS1 4x4 ordered dither before quantization
    int  dither_strength_256 = 256;   // 256 = exact PS1 dither amplitude
    bool vertex_snapping     = true;  // ON = snap to subpixel_bits grid (default integer)
    int  subpixel_bits       = 0;     // 0, 1, 2 or 4 -> 1, 1/2, 1/4, 1/16 pixel
    bool wireframe           = false;

    // --- camera defaults (spec 8) ---
    i32  fov_deg = 70;                // horizontal FOV in degrees

    // Effective subpixel bits: vertex snapping OFF forces max precision (1/16).
    int EffectiveSubpix() const { return vertex_snapping ? subpixel_bits : 4; }
    int InternalW() const { return kResModes[res_mode].w; }
    int InternalH() const { return kResModes[res_mode].h; }
};

extern EngineConfig g_config;

// World scale convention: 1.0 units in source assets/level JSON = 256 engine
// units. Meshes store vertices as raw i16 engine units (PS1 SVECTOR style).
constexpr i32 WORLD_SCALE = 256;
