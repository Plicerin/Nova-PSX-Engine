// Low-resolution internal framebuffer (spec 5.1, 5.7).
// Stored as XRGB8888 for cheap presentation, but every write goes through the
// PS1 color pipeline: optional 4x4 ordered dither -> quantize to 5 bits per
// channel -> expand back to 8. So the *content* is 15-bit color.
// The optional debug z-buffer lives here too (16-bit, view-space z >> 4).
#pragma once
#include "engine/core/types.h"
#include "engine/core/config.h"

struct Framebuffer {
    int w, h;
    u32 pixels[FB_MAX_W * FB_MAX_H]; // XRGB8888
};

extern u16 g_zbuffer[FB_MAX_W * FB_MAX_H]; // debug z-buffer (0xFFFF = far)

// The PS1 GPU dither kernel (psx-spx), added to 8-bit channels before >>3.
constexpr i8 kDither4[4][4] = {
    { -4,  0, -3,  1 },
    {  2, -2,  3, -1 },
    { -3,  1, -4,  0 },
    {  3, -1,  2, -2 },
};

void Fb_Init(Framebuffer* fb, int w, int h);
void Fb_Clear(Framebuffer* fb, u8 r, u8 g, u8 b);   // clear color also quantized
void Fb_ClearZ();                                    // reset z-buffer to 0xFFFF

// PS1.5 hybrid bloom: extract pixels brighter than `threshold`, blur, and add
// back at `strength`/256. Operates on the low-res framebuffer before upscale.
void Fb_Bloom(Framebuffer* fb, int threshold, int strength);

inline u8 ClampU8i(int v) { return (u8)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// Quantize one 8-bit channel with dither offset d (raw kernel value).
inline u8 QuantChannel(int c, int d) {
    if (!g_config.color_quantization) return ClampU8i(c);
    if (g_config.dithering) c += (d * g_config.dither_strength_256) >> 8;
    int q = ClampU8i(c) >> 3;
    return (u8)((q << 3) | (q >> 2));
}

// Write one pixel through the PS1 color pipeline. No bounds check: caller
// guarantees 0<=x<w, 0<=y<h.
inline void Fb_Put(Framebuffer* fb, int x, int y, int r, int g, int b) {
    int d = kDither4[y & 3][x & 3];
    u32 pr = QuantChannel(r, d), pg = QuantChannel(g, d), pb = QuantChannel(b, d);
    fb->pixels[y * fb->w + x] = 0xFF000000u | (pr << 16) | (pg << 8) | pb;
}

inline u32 Fb_Get(const Framebuffer* fb, int x, int y) {
    return fb->pixels[y * fb->w + x];
}

// Semi-transparency blend modes (PS1): 0: B/2+F/2, 1: B+F, 2: B-F, 3: B+F/4.
// Operates on 8-bit channels, clamped; result then goes through Fb_Put.
inline int BlendChannel(int back, int fore, int mode) {
    switch (mode) {
    case 0:  return (back + fore) >> 1;
    case 1:  return back + fore;
    case 2:  return back - fore;
    default: return back + (fore >> 2);
    }
}
