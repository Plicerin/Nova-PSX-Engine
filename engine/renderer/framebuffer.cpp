// Framebuffer + debug z-buffer storage (spec 5.1, 5.7).
#include "engine/renderer/framebuffer.h"
#include <cstdio>

u16 g_zbuffer[FB_MAX_W * FB_MAX_H];

void Fb_Init(Framebuffer* fb, int w, int h) {
    if (!fb) return;
    if (w < 1 || h < 1 || w > FB_MAX_W || h > FB_MAX_H) {
        fprintf(stderr, "Fb_Init: bad size %dx%d, clamping\n", w, h);
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        if (w > FB_MAX_W) w = FB_MAX_W;
        if (h > FB_MAX_H) h = FB_MAX_H;
    }
    fb->w = w;
    fb->h = h;
    Fb_Clear(fb, 0, 0, 0);
}

void Fb_Clear(Framebuffer* fb, u8 r, u8 g, u8 b) {
    // Clear color goes through quantize+dither too, like any GPU fill.
    for (int y = 0; y < fb->h; y++)
        for (int x = 0; x < fb->w; x++)
            Fb_Put(fb, x, y, r, g, b);
}

void Fb_ClearZ() {
    for (int i = 0; i < FB_MAX_W * FB_MAX_H; i++)
        g_zbuffer[i] = 0xFFFF;
}
