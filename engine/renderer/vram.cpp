// Simulated PS1 VRAM storage + allocation tracking (spec 6).
#include "engine/renderer/vram.h"
#include <cstdio>
#include <cstring>

u16 g_vram[VRAM_W * VRAM_H];

struct VramRect { i16 x, y, w, h; };
constexpr int VRAM_MAX_ALLOCS = 1024;
static VramRect s_allocs[VRAM_MAX_ALLOCS];
static int      s_nallocs = 0;
static u32      s_used_bytes = 0;

void Vram_Clear() {
    memset(g_vram, 0, sizeof(g_vram));
    s_nallocs = 0;
    s_used_bytes = 0;
}

void Vram_WriteRect(int vx, int vy, int w_hw, int h, const u16* data) {
    if (!data || w_hw <= 0 || h <= 0) return;
    int x0 = vx, y0 = vy, x1 = vx + w_hw, y1 = vy + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > VRAM_W) x1 = VRAM_W;
    if (y1 > VRAM_H) y1 = VRAM_H;
    if (x0 >= x1 || y0 >= y1) return;
    for (int y = y0; y < y1; y++) {
        const u16* src = data + (size_t)(y - vy) * (size_t)w_hw + (size_t)(x0 - vx);
        memcpy(&g_vram[y * VRAM_W + x0], src, (size_t)(x1 - x0) * sizeof(u16));
    }
}

void Vram_TrackAlloc(int vx, int vy, int w_hw, int h, u32 bytes) {
    if (s_nallocs < VRAM_MAX_ALLOCS) {
        s_allocs[s_nallocs].x = (i16)vx;
        s_allocs[s_nallocs].y = (i16)vy;
        s_allocs[s_nallocs].w = (i16)w_hw;
        s_allocs[s_nallocs].h = (i16)h;
        s_nallocs++;
    } else {
        fprintf(stderr, "Vram_TrackAlloc: rect list full (%d), budget still counted\n",
                VRAM_MAX_ALLOCS);
    }
    s_used_bytes += bytes;
}

u32 Vram_UsedBytes() {
    return s_used_bytes;
}

u8 Vram_PageOccupancy(int page_col, int page_row) {
    const int cx0 = page_col * 64, cy0 = page_row * 256;
    const int cx1 = cx0 + 64,      cy1 = cy0 + 256;
    u64 area = 0; // clipped halfword area covered inside this page cell
    for (int i = 0; i < s_nallocs; i++) {
        int x0 = s_allocs[i].x, y0 = s_allocs[i].y;
        int x1 = x0 + s_allocs[i].w, y1 = y0 + s_allocs[i].h;
        if (x0 < cx0) x0 = cx0;
        if (y0 < cy0) y0 = cy0;
        if (x1 > cx1) x1 = cx1;
        if (y1 > cy1) y1 = cy1;
        if (x0 < x1 && y0 < y1)
            area += (u64)(x1 - x0) * (u64)(y1 - y0);
    }
    u64 occ = (area * 255u) / (64u * 256u); // overlapping rects can exceed 255
    if (occ > 255) occ = 255;
    return (u8)occ;
}
