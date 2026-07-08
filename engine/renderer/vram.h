// Simulated PS1 VRAM: 1024x512 halfwords = 1 MiB (spec 6).
// Layout convention (enforced by the texture importer, mirrored here for the
// debug view): the region x < 320 is reserved for notional display buffers,
// exactly like a real PS1 with two 320x240 16-bit framebuffers at (0,0)/(0,240).
// Textures and CLUTs live at x >= 320. Usable texture budget is therefore
// (1024-320)*512*2 = 704 KiB, inside the spec's 512 KB - 1 MB window.
//
// Colors are PS1 15-bit: bit15 = STP (semi-transparency flag),
// bits 0-4 = R, 5-9 = G, 10-14 = B. Value 0x0000 = fully transparent texel.
#pragma once
#include "engine/core/types.h"

constexpr int VRAM_W = 1024;          // halfwords per row
constexpr int VRAM_H = 512;
constexpr int VRAM_TEX_X0 = 320;      // first halfword column usable by textures
constexpr u32 VRAM_TEX_BUDGET = (VRAM_W - VRAM_TEX_X0) * VRAM_H * 2; // bytes

extern u16 g_vram[VRAM_W * VRAM_H];

enum TexFormat : u8 { TEX_4BIT = 0, TEX_8BIT = 1, TEX_15BIT = 2 };

// Runtime descriptor of an uploaded texture (built from .texbin metadata).
struct TexInfo {
    char  name[32];
    u8    format;        // TexFormat
    u16   w, h;          // size in pixels (powers of two, <= 256)
    u16   vx, vy;        // top-left in VRAM, vx in HALFWORD columns
    u16   clut_x, clut_y;// CLUT row start (halfword coords); 0xFFFF,0xFFFF = none
    u16   clut_len;      // 0, 16 or 256
    u32   vram_bytes;    // accounted cost (pixels + clut)
    u16   page;          // texture page id for debug display (vy/256*11 + vx/64 style)
};

void Vram_Clear();
// Write w_hw x h halfwords at (vx, vy). Data row-major, tightly packed.
void Vram_WriteRect(int vx, int vy, int w_hw, int h, const u16* data);
inline u16 Vram_Read(int vx, int vy) { return g_vram[vy * VRAM_W + vx]; }

// Track an allocation for the budget counter + debug page map. Called by the
// asset loader after uploading. rect coords in halfwords.
void Vram_TrackAlloc(int vx, int vy, int w_hw, int h, u32 bytes);
u32  Vram_UsedBytes();
// Debug: 16x2 grid of 64x256-halfword page cells; returns occupancy 0..255.
u8   Vram_PageOccupancy(int page_col, int page_row); // col 0..15, row 0..1

// Expand 15-bit VRAM color to 8-bit channels (r,g,b in 0..255).
inline void Rgb15To24(u16 c, u8* r, u8* g, u8* b) {
    u8 r5 = c & 31, g5 = (c >> 5) & 31, b5 = (c >> 10) & 31;
    *r = (u8)((r5 << 3) | (r5 >> 2));
    *g = (u8)((g5 << 3) | (g5 >> 2));
    *b = (u8)((b5 << 3) | (b5 >> 2));
}

// Fetch one texel (15-bit value, including STP bit) for texture t at pixel
// (u,v). u,v MUST already be wrapped to [0,w) / [0,h). Handles 4/8-bit CLUT
// indirection and 15-bit direct.
inline u16 Vram_Texel(const TexInfo* t, u32 u, u32 v) {
    switch (t->format) {
    case TEX_4BIT: {
        u16 hw = g_vram[(t->vy + v) * VRAM_W + t->vx + (u >> 2)];
        u16 idx = (hw >> ((u & 3) * 4)) & 0xF;
        return g_vram[t->clut_y * VRAM_W + t->clut_x + idx];
    }
    case TEX_8BIT: {
        u16 hw = g_vram[(t->vy + v) * VRAM_W + t->vx + (u >> 1)];
        u16 idx = (hw >> ((u & 1) * 8)) & 0xFF;
        return g_vram[t->clut_y * VRAM_W + t->clut_x + idx];
    }
    default: // TEX_15BIT
        return g_vram[(t->vy + v) * VRAM_W + t->vx + u];
    }
}
