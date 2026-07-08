// PS1-style scanline software rasterizer (spec 5.2-5.7).
// All triangle attributes interpolate LINEARLY in screen space (affine mapping,
// authentic PS1). Fixed point throughout: coords/attributes in 16.16, i64 for
// products. Floats appear ONLY in the perspective-correct debug path.
#include "engine/renderer/raster.h"
#include <cstdio>

RasterStats g_raster_stats = {0, 0, 0};

void Raster_ResetStats() {
    g_raster_stats.prims_drawn  = 0;
    g_raster_stats.tris_drawn   = 0;
    g_raster_stats.pixels_filled = 0;
}

// ceil of 16.16 value to integer (gcc: arithmetic shift on negatives).
static inline int Ceil16(i64 v) { return (int)((v + 0xFFFF) >> 16); }

// PS1 texture blend: 128 = neutral, saturates at 255.
static inline int Modulate(int tex8, int vert8) {
    int c = (tex8 * vert8) >> 7;
    return c > 255 ? 255 : c;
}

// ---------------------------------------------------------------- lines ----

static void PlotLine(Framebuffer* fb, int x0, int y0, int x1, int y1,
                     int r, int g, int b, bool semitrans, int semi_mode) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    dy = -dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < fb->w && y0 >= 0 && y0 < fb->h) {
            int cr = r, cg = g, cb = b;
            if (semitrans) {
                u32 back = Fb_Get(fb, x0, y0);
                cr = ClampU8i(BlendChannel((int)((back >> 16) & 0xFF), cr, semi_mode));
                cg = ClampU8i(BlendChannel((int)((back >> 8) & 0xFF), cg, semi_mode));
                cb = ClampU8i(BlendChannel((int)(back & 0xFF), cb, semi_mode));
            }
            Fb_Put(fb, x0, y0, cr, cg, cb);
            g_raster_stats.pixels_filled++;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Raster_LinePx(Framebuffer* fb, int x0, int y0, int x1, int y1,
                   u8 r, u8 g, u8 b) {
    if (!fb) return;
    PlotLine(fb, x0, y0, x1, y1, r, g, b, false, 0);
}

// ------------------------------------------------------------ triangles ----

struct RVert {
    i32 x, y;            // screen, 16.16
    i64 z;               // z-buffer units (view z >> 4) in 16.16; i64 for headroom
    i32 u, v;            // texel coords, 16.16
    i32 r, g, b;         // color, 16.16
    float fuz, fvz, foz; // u/z, v/z, 1/z -- perspective debug path only
};

struct EdgePt {
    i64 x, z, u, v, r, g, b;
    float fuz, fvz, foz;
};

// Linear interpolation along edge a->b at scan Y. Caller guarantees b.y > a.y.
static void EvalEdge(const RVert& a, const RVert& b, i32 Y, bool persp, EdgePt* o) {
    i64 num = (i64)Y - a.y;
    i64 den = (i64)b.y - a.y;
    o->x = a.x + (i64)(b.x - a.x) * num / den;
    o->z = a.z + (b.z - a.z) * num / den;
    o->u = a.u + (i64)(b.u - a.u) * num / den;
    o->v = a.v + (i64)(b.v - a.v) * num / den;
    o->r = a.r + (i64)(b.r - a.r) * num / den;
    o->g = a.g + (i64)(b.g - a.g) * num / den;
    o->b = a.b + (i64)(b.b - a.b) * num / den;
    if (persp) {
        float t = (float)num / (float)den;
        o->fuz = a.fuz + (b.fuz - a.fuz) * t;
        o->fvz = a.fvz + (b.fvz - a.fvz) * t;
        o->foz = a.foz + (b.foz - a.foz) * t;
    } else {
        o->fuz = o->fvz = o->foz = 0.0f;
    }
}

// Bilinear debug sample at 16.16 texel coords. Base texel already known
// non-transparent; transparent neighbors just contribute their (black) color.
static void SampleBilinear(const TexInfo* t, i32 ufix, i32 vfix, u32 twm, u32 thm,
                           int* out_r, int* out_g, int* out_b) {
    u32 ui = (u32)(ufix >> 16) & twm, vi = (u32)(vfix >> 16) & thm;
    u32 ui1 = (ui + 1) & twm, vi1 = (vi + 1) & thm;
    u8 c[4][3];
    Rgb15To24(Vram_Texel(t, ui,  vi ), &c[0][0], &c[0][1], &c[0][2]);
    Rgb15To24(Vram_Texel(t, ui1, vi ), &c[1][0], &c[1][1], &c[1][2]);
    Rgb15To24(Vram_Texel(t, ui,  vi1), &c[2][0], &c[2][1], &c[2][2]);
    Rgb15To24(Vram_Texel(t, ui1, vi1), &c[3][0], &c[3][1], &c[3][2]);
    int fx = (ufix >> 8) & 0xFF, fy = (vfix >> 8) & 0xFF;
    int out[3];
    for (int ch = 0; ch < 3; ch++) {
        int top = c[0][ch] + (((c[1][ch] - c[0][ch]) * fx) >> 8);
        int bot = c[2][ch] + (((c[3][ch] - c[2][ch]) * fx) >> 8);
        out[ch] = top + (((bot - top) * fy) >> 8);
    }
    *out_r = out[0]; *out_g = out[1]; *out_b = out[2];
}

static void DrawTriangle(Framebuffer* fb, const Prim* p) {
    const int  sp       = g_config.EffectiveSubpix();
    const bool textured = (p->type == PRIM_TRI_FT || p->type == PRIM_TRI_GT);
    const bool gouraud  = (p->type == PRIM_TRI_G  || p->type == PRIM_TRI_GT);

    if (g_config.wireframe) {
        const int half = (1 << sp) >> 1;
        int xs[3], ys[3];
        for (int i = 0; i < 3; i++) {
            xs[i] = (p->v[i].x + half) >> sp;
            ys[i] = (p->v[i].y + half) >> sp;
        }
        g_raster_stats.tris_drawn++;
        Raster_LinePx(fb, xs[0], ys[0], xs[1], ys[1], p->v[0].r, p->v[0].g, p->v[0].b);
        Raster_LinePx(fb, xs[1], ys[1], xs[2], ys[2], p->v[0].r, p->v[0].g, p->v[0].b);
        Raster_LinePx(fb, xs[2], ys[2], xs[0], ys[0], p->v[0].r, p->v[0].g, p->v[0].b);
        return;
    }

    const TexInfo* tex = textured ? p->tex : nullptr;
    if (textured && (!tex || tex->w == 0 || tex->h == 0)) {
        fprintf(stderr, "raster: textured triangle with missing/empty texture\n");
        return;
    }

    const int shift = 16 - sp;
    RVert rv[3];
    for (int i = 0; i < 3; i++) {
        const PrimVert& s = p->v[i];
        rv[i].x = (i32)((i64)s.x << shift);
        rv[i].y = (i32)((i64)s.y << shift);
        i32 zc = s.z >> 4; // pre-shift to z-buffer units so 16.16 products fit i64
        if (zc < 0) zc = 0; else if (zc > 0xFFFF) zc = 0xFFFF;
        rv[i].z = (i64)zc << 16;
        rv[i].u = (i32)s.u << 16;
        rv[i].v = (i32)s.v << 16;
        const PrimVert& col = gouraud ? s : p->v[0];
        rv[i].r = (i32)col.r << 16;
        rv[i].g = (i32)col.g << 16;
        rv[i].b = (i32)col.b << 16;
        rv[i].fuz = rv[i].fvz = rv[i].foz = 0.0f;
    }

    bool persp = g_config.perspective_correct && textured;
    if (persp) {
        for (int i = 0; i < 3; i++)
            if (p->v[i].z < 1) { persp = false; break; } // can't divide: affine fallback
    }
    if (persp) {
        for (int i = 0; i < 3; i++) { // debug-only float setup
            float z = (float)p->v[i].z;
            rv[i].foz = 1.0f / z;
            rv[i].fuz = (float)p->v[i].u / z;
            rv[i].fvz = (float)p->v[i].v / z;
        }
    }

    i64 area2 = ((i64)rv[1].x - rv[0].x) * ((i64)rv[2].y - rv[0].y)
              - ((i64)rv[1].y - rv[0].y) * ((i64)rv[2].x - rv[0].x);
    if (area2 == 0) return; // degenerate; winding handled by per-row L/R compare

    // Stable sort by y (bubble network, strict compare keeps input order on ties).
    const RVert* va = &rv[0];
    const RVert* vb = &rv[1];
    const RVert* vc = &rv[2];
    const RVert* tswap;
    if (vb->y < va->y) { tswap = va; va = vb; vb = tswap; }
    if (vc->y < vb->y) { tswap = vb; vb = vc; vc = tswap; }
    if (vb->y < va->y) { tswap = va; va = vb; vb = tswap; }

    int pys = Ceil16((i64)va->y - 0x8000);
    int pye = Ceil16((i64)vc->y - 0x8000);
    if (pys < 0) pys = 0;
    if (pye > fb->h) pye = fb->h;
    if (pys >= pye) return;
    g_raster_stats.tris_drawn++;

    const bool zbuf     = g_config.zbuffer;
    const bool bilinear = g_config.bilinear_filter && textured;
    const u32  twm = tex ? (u32)(tex->w - 1) : 0;
    const u32  thm = tex ? (u32)(tex->h - 1) : 0;

    for (int py = pys; py < pye; py++) {
        const i32 Y = ((i32)py << 16) + 0x8000; // pixel-center sample line

        EdgePt e_long, e_short;
        EvalEdge(*va, *vc, Y, persp, &e_long);
        if (Y < vb->y) EvalEdge(*va, *vb, Y, persp, &e_short);
        else           EvalEdge(*vb, *vc, Y, persp, &e_short);

        EdgePt* L;
        EdgePt* R;
        if (e_long.x <= e_short.x) { L = &e_long;  R = &e_short; }
        else                       { L = &e_short; R = &e_long;  }

        const i64 xl = L->x, xr = R->x;
        int pxs = Ceil16(xl - 0x8000);
        int pxe = Ceil16(xr - 0x8000);
        if (pxs < 0) pxs = 0;
        if (pxe > fb->w) pxe = fb->w;
        if (pxs >= pxe) continue;
        const i64 dxs = xr - xl;
        if (dxs <= 0) continue;

        // Per-pixel steps (16.16). i64 keeps (delta<<16) exact.
        const i64 u_s = (R->u - L->u) * 65536 / dxs;
        const i64 v_s = (R->v - L->v) * 65536 / dxs;
        const i64 r_s = (R->r - L->r) * 65536 / dxs;
        const i64 g_s = (R->g - L->g) * 65536 / dxs;
        const i64 b_s = (R->b - L->b) * 65536 / dxs;
        const i64 z_s = (R->z - L->z) * 65536 / dxs;

        const i64 off = (((i64)pxs << 16) + 0x8000) - xl; // 0 <= off < dxs
        i64 u_a = L->u + ((u_s * off) >> 16);
        i64 v_a = L->v + ((v_s * off) >> 16);
        i64 r_a = L->r + ((r_s * off) >> 16);
        i64 g_a = L->g + ((g_s * off) >> 16);
        i64 b_a = L->b + ((b_s * off) >> 16);
        i64 z_a = L->z + ((z_s * off) >> 16);

        float fuz0 = 0.0f, fvz0 = 0.0f, foz0 = 0.0f;
        float fuz_s = 0.0f, fvz_s = 0.0f, foz_s = 0.0f;
        if (persp) {
            float t0 = (float)off / (float)dxs;
            fuz0 = L->fuz + (R->fuz - L->fuz) * t0;
            fvz0 = L->fvz + (R->fvz - L->fvz) * t0;
            foz0 = L->foz + (R->foz - L->foz) * t0;
            float inv = 65536.0f / (float)dxs;
            fuz_s = (R->fuz - L->fuz) * inv;
            fvz_s = (R->fvz - L->fvz) * inv;
            foz_s = (R->foz - L->foz) * inv;
        }

        for (int px = pxs; px < pxe;
             px++, u_a += u_s, v_a += v_s, r_a += r_s, g_a += g_s,
             b_a += b_s, z_a += z_s) {
            const int zidx = py * fb->w + px;
            u16 zval = 0;
            if (zbuf) {
                i64 zz = z_a >> 16;
                if (zz < 0) zz = 0; else if (zz > 0xFFFF) zz = 0xFFFF;
                zval = (u16)zz;
                if (zval >= g_zbuffer[zidx]) continue;
            }

            u16 texel = 0;
            int cr, cg, cb;
            if (textured) {
                i32 ufix, vfix;
                if (persp) {
                    float k = (float)(px - pxs);
                    float foz = foz0 + foz_s * k;
                    if (foz > 1e-12f) {
                        float inv = 65536.0f / foz;
                        ufix = (i32)((fuz0 + fuz_s * k) * inv);
                        vfix = (i32)((fvz0 + fvz_s * k) * inv);
                    } else {
                        ufix = (i32)u_a; vfix = (i32)v_a;
                    }
                } else {
                    ufix = (i32)u_a; vfix = (i32)v_a;
                }
                u32 ui = (u32)(ufix >> 16) & twm;
                u32 vi = (u32)(vfix >> 16) & thm;
                texel = Vram_Texel(tex, ui, vi);
                if (texel == 0x0000) continue; // fully transparent texel

                int tr, tg, tb;
                if (bilinear) {
                    SampleBilinear(tex, ufix, vfix, twm, thm, &tr, &tg, &tb);
                } else {
                    u8 r8, g8, b8;
                    Rgb15To24(texel, &r8, &g8, &b8);
                    tr = r8; tg = g8; tb = b8;
                }
                int vr, vg, vbl;
                if (gouraud) {
                    vr  = (int)(r_a >> 16);
                    vg  = (int)(g_a >> 16);
                    vbl = (int)(b_a >> 16);
                } else {
                    vr = p->v[0].r; vg = p->v[0].g; vbl = p->v[0].b;
                }
                if (p->flags & PF_NO_TEXBLEND) {
                    cr = tr; cg = tg; cb = tb;
                } else {
                    cr = Modulate(tr, vr);
                    cg = Modulate(tg, vg);
                    cb = Modulate(tb, vbl);
                }
            } else {
                if (gouraud) {
                    cr = (int)(r_a >> 16);
                    cg = (int)(g_a >> 16);
                    cb = (int)(b_a >> 16);
                } else {
                    cr = p->v[0].r; cg = p->v[0].g; cb = p->v[0].b;
                }
            }

            // STP rule: textured semi-trans applies only to texels with bit15.
            if ((p->flags & PF_SEMITRANS) && (!textured || (texel & 0x8000))) {
                u32 back = Fb_Get(fb, px, py);
                cr = ClampU8i(BlendChannel((int)((back >> 16) & 0xFF), cr, p->semi_mode));
                cg = ClampU8i(BlendChannel((int)((back >> 8) & 0xFF), cg, p->semi_mode));
                cb = ClampU8i(BlendChannel((int)(back & 0xFF), cb, p->semi_mode));
            }

            if (zbuf) g_zbuffer[zidx] = zval;
            Fb_Put(fb, px, py, cr, cg, cb);
            g_raster_stats.pixels_filled++;
        }
    }
}

// --------------------------------------------------------- sprite / tile ----

static void DrawSprite(Framebuffer* fb, const Prim* p) {
    const TexInfo* tex = p->tex;
    if (!tex || tex->w == 0 || tex->h == 0) {
        fprintf(stderr, "raster: SPRT with missing/empty texture\n");
        return;
    }
    if (p->w == 0 || p->h == 0) return;

    const int sp = g_config.EffectiveSubpix();
    const int half = (1 << sp) >> 1;
    const int x0 = (p->v[0].x + half) >> sp;
    const int y0 = (p->v[0].y + half) >> sp;

    int i0 = x0 < 0 ? -x0 : 0;
    int j0 = y0 < 0 ? -y0 : 0;
    int i1 = (x0 + (int)p->w > fb->w) ? fb->w - x0 : (int)p->w;
    int j1 = (y0 + (int)p->h > fb->h) ? fb->h - y0 : (int)p->h;
    if (i0 >= i1 || j0 >= j1) return;

    const u32 twm = (u32)(tex->w - 1), thm = (u32)(tex->h - 1);
    const bool mod = !(p->flags & PF_NO_TEXBLEND);
    const bool st  = (p->flags & PF_SEMITRANS) != 0;
    const int vr = p->v[0].r, vg = p->v[0].g, vbl = p->v[0].b;

    for (int j = j0; j < j1; j++) {
        const int py = y0 + j;
        const u32 vv = ((u32)p->v[0].v + (u32)j) & thm;
        for (int i = i0; i < i1; i++) {
            const u32 uu = ((u32)p->v[0].u + (u32)i) & twm;
            u16 texel = Vram_Texel(tex, uu, vv);
            if (texel == 0x0000) continue;
            u8 r8, g8, b8;
            Rgb15To24(texel, &r8, &g8, &b8);
            int cr, cg, cb;
            if (mod) {
                cr = Modulate(r8, vr); cg = Modulate(g8, vg); cb = Modulate(b8, vbl);
            } else {
                cr = r8; cg = g8; cb = b8;
            }
            const int px = x0 + i;
            if (st && (texel & 0x8000)) {
                u32 back = Fb_Get(fb, px, py);
                cr = ClampU8i(BlendChannel((int)((back >> 16) & 0xFF), cr, p->semi_mode));
                cg = ClampU8i(BlendChannel((int)((back >> 8) & 0xFF), cg, p->semi_mode));
                cb = ClampU8i(BlendChannel((int)(back & 0xFF), cb, p->semi_mode));
            }
            Fb_Put(fb, px, py, cr, cg, cb);
            g_raster_stats.pixels_filled++;
        }
    }
}

static void DrawTile(Framebuffer* fb, const Prim* p) {
    if (p->w == 0 || p->h == 0) return;
    const int sp = g_config.EffectiveSubpix();
    const int half = (1 << sp) >> 1;
    const int x0 = (p->v[0].x + half) >> sp;
    const int y0 = (p->v[0].y + half) >> sp;

    int xs = x0 < 0 ? 0 : x0;
    int ys = y0 < 0 ? 0 : y0;
    int xe = x0 + (int)p->w; if (xe > fb->w) xe = fb->w;
    int ye = y0 + (int)p->h; if (ye > fb->h) ye = fb->h;
    if (xs >= xe || ys >= ye) return;

    const bool st = (p->flags & PF_SEMITRANS) != 0;
    for (int y = ys; y < ye; y++) {
        for (int x = xs; x < xe; x++) {
            int cr = p->v[0].r, cg = p->v[0].g, cb = p->v[0].b;
            if (st) {
                u32 back = Fb_Get(fb, x, y);
                cr = ClampU8i(BlendChannel((int)((back >> 16) & 0xFF), cr, p->semi_mode));
                cg = ClampU8i(BlendChannel((int)((back >> 8) & 0xFF), cg, p->semi_mode));
                cb = ClampU8i(BlendChannel((int)(back & 0xFF), cb, p->semi_mode));
            }
            Fb_Put(fb, x, y, cr, cg, cb);
            g_raster_stats.pixels_filled++;
        }
    }
}

static void DrawLinePrim(Framebuffer* fb, const Prim* p) {
    const int sp = g_config.EffectiveSubpix();
    const int half = (1 << sp) >> 1;
    PlotLine(fb,
             (p->v[0].x + half) >> sp, (p->v[0].y + half) >> sp,
             (p->v[1].x + half) >> sp, (p->v[1].y + half) >> sp,
             p->v[0].r, p->v[0].g, p->v[0].b,
             (p->flags & PF_SEMITRANS) != 0, p->semi_mode);
}

// -------------------------------------------------------------- dispatch ----

void Raster_DrawPrim(Framebuffer* fb, const Prim* p) {
    if (!fb || !p) return;
    g_raster_stats.prims_drawn++;
    switch (p->type) {
    case PRIM_TRI_F:
    case PRIM_TRI_G:
    case PRIM_TRI_FT:
    case PRIM_TRI_GT:
        DrawTriangle(fb, p);
        break;
    case PRIM_LINE:
        DrawLinePrim(fb, p);
        break;
    case PRIM_SPRT:
        DrawSprite(fb, p);
        break;
    case PRIM_TILE:
        DrawTile(fb, p);
        break;
    default:
        fprintf(stderr, "raster: unknown prim type %d\n", (int)p->type);
        break;
    }
}
