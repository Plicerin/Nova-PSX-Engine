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

// Bright-pass + separable box blur + additive combine. Scratch buffers hold the
// isolated bright pixels (RGB) at full res; a 3-tap box blur run twice ~= a
// small gaussian, cheap at 320x240.
static u8 s_bloom_a[FB_MAX_W * FB_MAX_H * 3];
static u8 s_bloom_b[FB_MAX_W * FB_MAX_H * 3];

void Fb_Bloom(Framebuffer* fb, int threshold, int strength) {
    if (!fb) return;
    const int w = fb->w, h = fb->h;
    // Bright pass: keep (channel - threshold) for pixels over the luma cutoff.
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            u32 px = fb->pixels[y * w + x];
            int r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
            int luma = (r * 77 + g * 150 + b * 29) >> 8;
            int i = (y * w + x) * 3;
            if (luma > threshold) {
                s_bloom_a[i]     = (u8)(r > threshold ? r - threshold : 0);
                s_bloom_a[i + 1] = (u8)(g > threshold ? g - threshold : 0);
                s_bloom_a[i + 2] = (u8)(b > threshold ? b - threshold : 0);
            } else {
                s_bloom_a[i] = s_bloom_a[i + 1] = s_bloom_a[i + 2] = 0;
            }
        }
    }
    // Two blur passes (H then V each), radius 2, src a -> dst b -> a ...
    u8* src = s_bloom_a; u8* dst = s_bloom_b;
    for (int pass = 0; pass < 2; pass++) {
        // horizontal
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int acc[3] = { 0, 0, 0 }, n = 0;
                for (int d = -2; d <= 2; d++) {
                    int xx = x + d; if (xx < 0 || xx >= w) continue;
                    int i = (y * w + xx) * 3;
                    acc[0] += src[i]; acc[1] += src[i + 1]; acc[2] += src[i + 2]; n++;
                }
                int o = (y * w + x) * 3;
                dst[o] = (u8)(acc[0] / n); dst[o + 1] = (u8)(acc[1] / n); dst[o + 2] = (u8)(acc[2] / n);
            }
        }
        u8* t = src; src = dst; dst = t;
        // vertical
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int acc[3] = { 0, 0, 0 }, n = 0;
                for (int d = -2; d <= 2; d++) {
                    int yy = y + d; if (yy < 0 || yy >= h) continue;
                    int i = ((yy) * w + x) * 3;
                    acc[0] += src[i]; acc[1] += src[i + 1]; acc[2] += src[i + 2]; n++;
                }
                int o = (y * w + x) * 3;
                dst[o] = (u8)(acc[0] / n); dst[o + 1] = (u8)(acc[1] / n); dst[o + 2] = (u8)(acc[2] / n);
            }
        }
        u8* t2 = src; src = dst; dst = t2;
    }
    // Additive combine (post-quantize glow; not dithered on purpose).
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = (y * w + x) * 3;
            u32 px = fb->pixels[y * w + x];
            int r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
            r += (src[i]     * strength) >> 8;
            g += (src[i + 1] * strength) >> 8;
            b += (src[i + 2] * strength) >> 8;
            fb->pixels[y * w + x] = 0xFF000000u | (ClampU8i(r) << 16) |
                                    (ClampU8i(g) << 8) | ClampU8i(b);
        }
    }
}
