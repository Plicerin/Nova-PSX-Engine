// Primitive packets + ordering table (spec 5.2, 5.5).
// The renderer is primitive-driven: everything that reaches the rasterizer is
// one of these packets, allocated from a per-frame arena and linked into an
// ordering table bucket by depth key, PS1 style.
//
// Quads are split into two triangles (v0v1v2, v1v3v2) at *emission* time —
// exactly what the PS1 GPU did internally — so the rasterizer only ever sees
// triangles, lines, sprites and tiles. The diagonal seam this produces on
// gouraud/textured quads is an authentic artifact.
#pragma once
#include "engine/core/types.h"
#include "engine/renderer/vram.h"

enum PrimType : u8 {
    PRIM_TRI_F = 0,   // flat-colored triangle (color in v[0])
    PRIM_TRI_G,       // gouraud triangle (per-vertex color)
    PRIM_TRI_FT,      // flat-colored textured triangle (modulated by v[0] color)
    PRIM_TRI_GT,      // gouraud textured triangle
    PRIM_LINE,        // flat line, v[0]..v[1]
    PRIM_SPRT,        // screen-space sprite: v[0].x/y top-left, uses w,h, uv base
    PRIM_TILE,        // flat rectangle: v[0].x/y top-left, w,h, color v[0]
};

enum PrimFlags : u8 {
    PF_SEMITRANS   = 1 << 0,  // semi-transparent; blend mode in semi_mode
    PF_NO_TEXBLEND = 1 << 1,  // sprite/texture drawn raw (no color modulation)
};

struct PrimVert {
    i32 x, y;      // screen coords in subpixel units (see g_config.EffectiveSubpix())
    i32 z;         // view-space z (for debug z-buffer / perspective-correct path)
    u8  r, g, b;   // vertex color (PS1 convention: 128 = 1.0 for texture modulation)
    u8  u, v;      // texel coords within the texture (pixels)
};

struct Prim {
    Prim*          next;      // OT chain
    u8             type;      // PrimType
    u8             flags;     // PrimFlags
    u8             semi_mode; // 0..3, valid if PF_SEMITRANS
    u8             _pad;
    const TexInfo* tex;       // null for untextured
    u16            w, h;      // sprite/tile size in pixels
    PrimVert       v[3];
};

// --- ordering table ---
constexpr int OT_SIZE = 4096;
// Depth key: otz = average view-space z >> OTZ_SHIFT, clamped to [0, OT_SIZE-1].
// With WORLD_SCALE=256 and shift 4 the OT covers 0..65535 units = 256 m.
constexpr int OTZ_SHIFT = 4;

struct OrderingTable {
    Prim* bucket[OT_SIZE];
};

constexpr int PRIM_ARENA_MAX = 65536;

struct PrimArena {
    Prim  prims[PRIM_ARENA_MAX];
    int   used;
};

inline void Ot_Clear(OrderingTable* ot) {
    for (int i = 0; i < OT_SIZE; i++) ot->bucket[i] = nullptr;
}

inline Prim* Arena_Alloc(PrimArena* a) {
    if (a->used >= PRIM_ARENA_MAX) return nullptr;
    return &a->prims[a->used++];
}

// Push-front, like AddPrim: within a bucket, later insertions draw FIRST.
inline void Ot_Add(OrderingTable* ot, Prim* p, int otz) {
    if (otz < 0) otz = 0;
    if (otz >= OT_SIZE) otz = OT_SIZE - 1;
    p->next = ot->bucket[otz];
    ot->bucket[otz] = p;
}
