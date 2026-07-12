// Renderer frontend (spec 4.1): GTE transform -> primitive packets -> OT.
#include "engine/renderer/render.h"
#include "engine/renderer/raster.h"
#include "engine/core/config.h"
#include <cmath>
#include <cstdio>
#include <cstring>

bool g_debug_draworder = false;
SpecularParams g_specular = { false, 190, 220, 240, 2 };

// Per-mesh transform scratch. u16 vertex indices can never exceed this cap,
// so scratch reads are inherently in-bounds even on corrupt index data.
constexpr int RC_MAX_VERTS = 65536;
static i32 s_sx[RC_MAX_VERTS];
static i32 s_sy[RC_MAX_VERTS];
static i32 s_sz[RC_MAX_VERTS];
static i32 s_p[RC_MAX_VERTS];
static u8  s_lit[RC_MAX_VERTS][3];
static u8  s_spec[RC_MAX_VERTS][3];   // additive specular highlight, per vertex
static u8  s_plit[RC_MAX_VERTS][3];   // additive coloured point-light pools

void Rc_Init(RenderContext* rc) {
    if (!rc) return;
    memset(rc, 0, sizeof(*rc)); // fog/light disabled = all-zero defaults
}

void Rc_Begin(RenderContext* rc, const Camera* cam) {
    rc->cam = *cam;
    memset(&rc->stats, 0, sizeof(rc->stats));
    Ot_Clear(&rc->ot);
    rc->arena.used = 0;

    // View = inverse camera transform: transpose(R) rotation, -transpose(R)*pos.
    Mat camrot;
    Gte_RotMatrix(&cam->rot, &camrot);
    Gte_TransposeRot(&camrot, &rc->view);
    LVec negpos = { -cam->pos.vx, -cam->pos.vy, -cam->pos.vz };
    LVec t;
    Gte_ApplyRotL(&rc->view, &negpos, &t);
    rc->view.t[0] = t.vx;
    rc->view.t[1] = t.vy;
    rc->view.t[2] = t.vz;

    // float allowed: per-frame constant, never per-vertex.
    rc->proj_h = (i32)((g_config.InternalW() / 2) /
                       tan((double)g_config.fov_deg * 3.14159265358979323846 / 360.0));
    Gte_SetGeom(g_config.InternalW() / 2, g_config.InternalH() / 2,
                rc->proj_h, g_config.EffectiveSubpix());
    if (rc->fog.enabled)
        Gte_SetFogRange(rc->fog.start_z, rc->fog.end_z);
    else
        Gte_SetFogRange(0, 0);
}

void Rc_DrawMesh(RenderContext* rc, const Mesh* mesh, const Mat* model) {
    if (!rc || !mesh || !model) return;
    if (mesh->nverts > (u32)RC_MAX_VERTS) {
        fprintf(stderr, "Rc_DrawMesh: mesh '%s' has %u verts (cap %d)\n",
                mesh->name, mesh->nverts, RC_MAX_VERTS);
        return;
    }

    Mat mv;
    Gte_CompMatrix(&rc->view, model, &mv);
    Gte_SetRotTrans(&mv);

    for (u32 i = 0; i < mesh->nverts; i++)
        Gte_RotTransPers(&mesh->verts[i], &s_sx[i], &s_sy[i], &s_sz[i], &s_p[i]);

    const bool lit = rc->light.enabled && mesh->norms != nullptr;
    const bool spec_on = lit && g_specular.enabled;
    // Half-vector for infinite-viewer Blinn specular, in VIEW space (constant
    // per object): H = normalize((-lightDir_view) + (0,0,-1)).  Viewer looks +z,
    // so surface->camera is (0,0,-1) in view space.
    i32 Hx = 0, Hy = 0, Hz = 0;
    if (spec_on) {
        LVec ldv;
        Gte_ApplyRot(&rc->view, &rc->light.dir, &ldv);   // incoming light, view space
        i32 hx = -ldv.vx, hy = -ldv.vy, hz = -ldv.vz - FX_ONE;
        u32 hl2 = (u32)((i64)hx * hx + (i64)hy * hy + (i64)hz * hz);
        u32 hl = IsqrtU32(hl2);
        if (hl) { Hx = (i32)(((i64)hx << FX_SHIFT) / hl);
                  Hy = (i32)(((i64)hy << FX_SHIFT) / hl);
                  Hz = (i32)(((i64)hz << FX_SHIFT) / hl); }
    }
    if (lit) {
        // Light dir into MODEL space so normals need no per-vertex rotation.
        Mat mrot_t;
        Gte_TransposeRot(model, &mrot_t);
        LVec ld;
        Gte_ApplyRot(&mrot_t, &rc->light.dir, &ld);
        const int amb[3] = { rc->light.amb_r, rc->light.amb_g, rc->light.amb_b };
        const int dif[3] = { rc->light.dif_r, rc->light.dif_g, rc->light.dif_b };
        const int spc[3] = { g_specular.r, g_specular.g, g_specular.b };
        // Optional second, differently-coloured directional fill (two-tone).
        const bool fill_on = rc->light.fil_en != 0;
        const int fil[3] = { rc->light.fil_r, rc->light.fil_g, rc->light.fil_b };
        LVec lf = { 0, 0, 0 };
        if (fill_on) Gte_ApplyRot(&mrot_t, &rc->light.fdir, &lf);

        // Coloured point lights, brought into MODEL space so the per-vertex
        // maths matches the normals. Assumes the object's rotation part is
        // orthonormal (level objects are authored at unit scale); a scaled
        // object would need a true inverse rather than a transpose.
        const int npl = (int)rc->light.npoints;
        LVec plp[MAX_POINT_LIGHTS];
        i32  plrad[MAX_POINT_LIGHTS];
        int  plcol[MAX_POINT_LIGHTS][3];
        for (int L = 0; L < npl; L++) {
            const LevelPoint* lp = &rc->light.points[L];
            LVec d = { lp->pos.vx - model->t[0],
                       lp->pos.vy - model->t[1],
                       lp->pos.vz - model->t[2] };
            Gte_ApplyRotL(&mrot_t, &d, &plp[L]);
            plrad[L]   = lp->radius;
            plcol[L][0] = lp->r; plcol[L][1] = lp->g; plcol[L][2] = lp->b;
        }

        for (u32 i = 0; i < mesh->nverts; i++) {
            const SVec* n = &mesh->norms[i];
            i32 dot = (i32)n->vx * ld.vx + (i32)n->vy * ld.vy + (i32)n->vz * ld.vz;
            i32 I = ClampI32((-dot) >> 12, 0, 4096);
            i32 If = 0;
            if (fill_on) {
                i32 df = (i32)n->vx * lf.vx + (i32)n->vy * lf.vy + (i32)n->vz * lf.vz;
                If = ClampI32((-df) >> 12, 0, 4096);
            }
            // Per-point-light factor: linear distance falloff * N.L (4.12).
            i32 pfac[MAX_POINT_LIGHTS];
            for (int L = 0; L < npl; L++) {
                const SVec* v = &mesh->verts[i];
                i32 dx = plp[L].vx - v->vx;
                i32 dy = plp[L].vy - v->vy;
                i32 dz = plp[L].vz - v->vz;
                i64 d2 = (i64)dx * dx + (i64)dy * dy + (i64)dz * dz;
                i64 rr = (i64)plrad[L];
                if (d2 >= rr * rr) { pfac[L] = 0; continue; }   // out of range
                u32 dist  = IsqrtU32((u32)d2);
                i32 atten = (i32)((((rr - (i64)dist) << 12)) / rr);  // 0..4096
                i64 nd    = (i64)n->vx * dx + (i64)n->vy * dy + (i64)n->vz * dz;
                i32 ndl   = 0;
                if (nd > 0)
                    ndl = (dist == 0) ? FX_ONE
                                      : ClampI32((i32)(nd / (i64)dist), 0, FX_ONE);
                pfac[L] = (atten * ndl) >> 12;
            }
            for (int c = 0; c < 3; c++) {
                int L = amb[c] + ((dif[c] * I) >> 12);
                if (fill_on) L += (fil[c] * If) >> 12;
                s_lit[i][c] = (u8)(L > 255 ? 255 : L);
                // Point lights are ADDITIVE (applied after texture modulation,
                // like the specular term). Folding them into the multiplier
                // instead would tint nothing: a saturated texture has no red
                // channel for an orange lamp to scale, and the u8 clamp turns
                // a strong lamp white. Adding light shows its true hue.
                int P = 0;
                for (int k = 0; k < npl; k++)
                    P += (plcol[k][c] * pfac[k]) >> 12;
                s_plit[i][c] = (u8)(P > 255 ? 255 : P);
            }
            if (spec_on) {
                LVec nv;
                Gte_ApplyRot(&mv, n, &nv);               // normal into view space
                u32 nl2 = (u32)((i64)nv.vx * nv.vx + (i64)nv.vy * nv.vy +
                                (i64)nv.vz * nv.vz);
                u32 nl = IsqrtU32(nl2);
                i32 s = 0;
                if (nl) {
                    i64 raw = (i64)nv.vx * Hx + (i64)nv.vy * Hy + (i64)nv.vz * Hz;
                    s = (i32)(raw / (i64)nl);            // cos(N,H) in 4.12
                    s = ClampI32(s, 0, FX_ONE);
                    for (int k = 0; k < g_specular.shininess; k++)
                        s = (s * s) >> FX_SHIFT;          // ^(2^shininess)
                }
                for (int c = 0; c < 3; c++)
                    s_spec[i][c] = (u8)((spc[c] * s) >> FX_SHIFT);
            } else {
                s_spec[i][0] = s_spec[i][1] = s_spec[i][2] = 0;
            }
        }
    }

    const int  sub        = g_config.EffectiveSubpix();
    const int  fogc[3]    = { rc->fog.r, rc->fog.g, rc->fog.b };
    const bool fog_on     = rc->fog.enabled != 0;
    static const u8 kTriMap[2][3] = { { 0, 1, 2 }, { 1, 3, 2 } };

    for (u32 pi = 0; pi < mesh->nprims; pi++) {
        const MeshPrim* mp = &mesh->prims[pi];
        if (!g_config.draw_semitrans && (mp->flags & MPF_SEMITRANS)) continue;
        const int nv = (mp->type >= MP_F4) ? 4 : 3;

        u16 idx[4] = { 0, 0, 0, 0 };
        bool near_rej = false, all_far = true;
        i64 zsum = 0;
        for (int k = 0; k < nv; k++) {
            idx[k] = mp->vi[k];
            i32 z = s_sz[idx[k]];
            if (z < rc->cam.near_z) near_rej = true;   // PS1-style whole-poly reject
            if (z <= rc->cam.far_z) all_far = false;
            zsum += z;
        }
        if (near_rej || all_far) { rc->stats.faces_culled++; continue; }

        if (!(mp->flags & MPF_DOUBLESIDED)) {
            i64 nc = Gte_NClip(s_sx[idx[0]], s_sy[idx[0]],
                               s_sx[idx[1]], s_sy[idx[1]],
                               s_sx[idx[2]], s_sy[idx[2]]);
            if (nc <= 0) { rc->stats.faces_culled++; continue; }
        }

        i32 minx = s_sx[idx[0]], maxx = minx;
        i32 miny = s_sy[idx[0]], maxy = miny;
        for (int k = 1; k < nv; k++) {
            i32 x = s_sx[idx[k]], y = s_sy[idx[k]];
            if (x < minx) minx = x; else if (x > maxx) maxx = x;
            if (y < miny) miny = y; else if (y > maxy) maxy = y;
        }
        // Authentic GPU size reject (1023x511 pixels); also raster overflow guard.
        if (((maxx - minx) >> sub) > 1023 || ((maxy - miny) >> sub) > 511) {
            rc->stats.faces_culled++;
            continue;
        }

        u8 col[4][3];
        for (int k = 0; k < nv; k++) {
            for (int c = 0; c < 3; c++) {
                int v = mp->rgb[k][c];
                if (lit) {
                    v = (v * s_lit[idx[k]][c]) >> 7;
                    if (v > 255) v = 255;
                }
                if (fog_on)
                    v += ((fogc[c] - v) * s_p[idx[k]]) >> 12;
                if (lit) {
                    v += s_plit[idx[k]][c];   // coloured lamp pools, on top of fog
                    if (v > 255) v = 255;
                }
                if (lit && !(mp->flags & MPF_MATTE)) {
                    v += s_spec[idx[k]][c];   // additive wet highlight, on top of fog
                    if (v > 255) v = 255;
                }
                col[k][c] = (u8)v;
            }
        }

        int otz = (int)((zsum / nv) >> OTZ_SHIFT) + mp->sort_bias;
        otz = ClampI32(otz, 0, OT_SIZE - 1);

        const TexInfo* tex = nullptr;
        if (mp->tex_index != 0xFFFF && mp->tex_index < mesh->ntex)
            tex = mesh->tex[mp->tex_index];
        u8 ptype = (u8)(mp->type & 3);            // F3..GT3 order matches PrimType
        if ((ptype & 2) && !tex) ptype &= 1;      // textured with no texture -> flat/gouraud
        u8 flags = (mp->flags & MPF_SEMITRANS) ? PF_SEMITRANS : 0;
        u8 semi_mode = (u8)((mp->flags >> MPF_SEMIMODE_SHIFT) & 3);

        // UV scroll (water etc.): shift texel coords, then rebase the prim's
        // span to keep it ascending — the raster wraps per-sample via the
        // power-of-two texture mask, so values above tex->w are fine.
        i32 suv[4][2];
        for (int k = 0; k < nv; k++) {
            suv[k][0] = mp->uv[k][0];
            suv[k][1] = mp->uv[k][1];
        }
        if ((mp->flags & MPF_UVSCROLL) && tex) {
            i32 minu = 0x7FFFFFFF, minv = 0x7FFFFFFF;
            for (int k = 0; k < nv; k++) {
                suv[k][0] += rc->uvscroll_u;
                suv[k][1] += rc->uvscroll_v;
                if (suv[k][0] < minu) minu = suv[k][0];
                if (suv[k][1] < minv) minv = suv[k][1];
            }
            const i32 bu = minu & ~(i32)(tex->w - 1);  // floor to tile multiple
            const i32 bv = minv & ~(i32)(tex->h - 1);
            for (int k = 0; k < nv; k++) { suv[k][0] -= bu; suv[k][1] -= bv; }
        }

        const int ntri = (nv == 4) ? 2 : 1;       // quad -> two tris, PS1 GPU split
        for (int ti = 0; ti < ntri; ti++) {
            Prim* p = Arena_Alloc(&rc->arena);
            if (!p) continue; // arena full: drop silently
            p->type = ptype;
            p->flags = flags;
            p->semi_mode = semi_mode;
            p->tex = tex;
            p->w = 0;
            p->h = 0;
            for (int k = 0; k < 3; k++) {
                const int mk = kTriMap[ti][k];
                const u16 vi = idx[mk];
                p->v[k].x = s_sx[vi];
                p->v[k].y = s_sy[vi];
                p->v[k].z = s_sz[vi];
                p->v[k].r = col[mk][0];
                p->v[k].g = col[mk][1];
                p->v[k].b = col[mk][2];
                p->v[k].u = (u8)suv[mk][0];
                p->v[k].v = (u8)suv[mk][1];
            }
            Ot_Add(&rc->ot, p, otz);
            rc->stats.prims_emitted++;
            rc->stats.tris_emitted++;
        }
    }
    rc->stats.objects_drawn++;
}

void Rc_DrawBillboard(RenderContext* rc, const TexInfo* tex, LVec world_pos,
                      i32 w_units, i32 h_units, u8 r, u8 g, u8 b,
                      bool semitrans, u8 semi_mode) {
    if (!rc || !tex) return;

    LVec vp;
    Gte_ApplyRotL(&rc->view, &world_pos, &vp);
    vp.vx += rc->view.t[0];
    vp.vy += rc->view.t[1];
    vp.vz += rc->view.t[2];
    if (vp.vz < rc->cam.near_z || vp.vz <= 0) return;

    const int sub = g_config.EffectiveSubpix();
    // Project center like the GTE: multiply-then-divide with subpix bits.
    i64 cx = ((i64)(g_config.InternalW() / 2) << sub) +
             (((i64)vp.vx * rc->proj_h << sub) / vp.vz);
    i64 cy = ((i64)(g_config.InternalH() / 2) << sub) +
             (((i64)vp.vy * rc->proj_h << sub) / vp.vz);
    const i64 hw = (i64)(((i64)w_units * rc->proj_h / vp.vz) >> 1) << sub;
    const i64 hh = (i64)(((i64)h_units * rc->proj_h / vp.vz) >> 1) << sub;

    const i32 xs[4] = { (i32)(cx - hw), (i32)(cx + hw), (i32)(cx - hw), (i32)(cx + hw) };
    const i32 ys[4] = { (i32)(cy - hh), (i32)(cy - hh), (i32)(cy + hh), (i32)(cy + hh) };
    const u8  us[4] = { 0, (u8)(tex->w - 1), 0, (u8)(tex->w - 1) };
    const u8  vs[4] = { 0, 0, (u8)(tex->h - 1), (u8)(tex->h - 1) };

    int otz = ClampI32(vp.vz >> OTZ_SHIFT, 0, OT_SIZE - 1);
    static const u8 kTriMap[2][3] = { { 0, 1, 2 }, { 1, 3, 2 } };

    // PS1 had no scaled sprites: a billboard is just a camera-facing tex quad.
    for (int ti = 0; ti < 2; ti++) {
        Prim* p = Arena_Alloc(&rc->arena);
        if (!p) return;
        p->type = PRIM_TRI_FT;
        p->flags = semitrans ? PF_SEMITRANS : 0;
        p->semi_mode = semi_mode;
        p->tex = tex;
        p->w = 0;
        p->h = 0;
        for (int k = 0; k < 3; k++) {
            const int c = kTriMap[ti][k];
            p->v[k].x = xs[c];
            p->v[k].y = ys[c];
            p->v[k].z = vp.vz;
            p->v[k].r = r;
            p->v[k].g = g;
            p->v[k].b = b;
            p->v[k].u = us[c];
            p->v[k].v = vs[c];
        }
        Ot_Add(&rc->ot, p, otz);
        rc->stats.prims_emitted++;
        rc->stats.tris_emitted++;
    }
}

void Rc_AddTile(RenderContext* rc, int x, int y, int w, int h,
                u8 r, u8 g, u8 b, int otz, bool semitrans, u8 semi_mode) {
    Prim* p = Arena_Alloc(&rc->arena);
    if (!p) return;
    const int sub = g_config.EffectiveSubpix();
    p->type = PRIM_TILE;
    p->flags = semitrans ? PF_SEMITRANS : 0;
    p->semi_mode = semi_mode;
    p->tex = nullptr;
    p->w = (u16)w;
    p->h = (u16)h;
    p->v[0].x = x << sub;
    p->v[0].y = y << sub;
    p->v[0].z = 0;
    p->v[0].r = r; p->v[0].g = g; p->v[0].b = b;
    p->v[0].u = 0; p->v[0].v = 0;
    p->v[1] = p->v[0];
    p->v[2] = p->v[0];
    Ot_Add(&rc->ot, p, otz);
    rc->stats.prims_emitted++;
}

void Rc_AddSprite(RenderContext* rc, const TexInfo* tex, int x, int y,
                  int w, int h, u8 u0, u8 v0, u8 r, u8 g, u8 b, int otz) {
    if (!tex) return;
    Prim* p = Arena_Alloc(&rc->arena);
    if (!p) return;
    const int sub = g_config.EffectiveSubpix();
    p->type = PRIM_SPRT;
    p->flags = 0;
    p->semi_mode = 0;
    p->tex = tex;
    p->w = (u16)w;
    p->h = (u16)h;
    p->v[0].x = x << sub;
    p->v[0].y = y << sub;
    p->v[0].z = 0;
    p->v[0].r = r; p->v[0].g = g; p->v[0].b = b;
    p->v[0].u = u0; p->v[0].v = v0;
    p->v[1] = p->v[0];
    p->v[2] = p->v[0];
    Ot_Add(&rc->ot, p, otz);
    rc->stats.prims_emitted++;
}

void Rc_AddLine(RenderContext* rc, int x0, int y0, int x1, int y1,
                u8 r, u8 g, u8 b, int otz) {
    Prim* p = Arena_Alloc(&rc->arena);
    if (!p) return;
    const int sub = g_config.EffectiveSubpix();
    p->type = PRIM_LINE;
    p->flags = 0;
    p->semi_mode = 0;
    p->tex = nullptr;
    p->w = 0;
    p->h = 0;
    p->v[0].x = x0 << sub;
    p->v[0].y = y0 << sub;
    p->v[0].z = 0;
    p->v[0].r = r; p->v[0].g = g; p->v[0].b = b;
    p->v[0].u = 0; p->v[0].v = 0;
    p->v[1] = p->v[0];
    p->v[1].x = x1 << sub;
    p->v[1].y = y1 << sub;
    p->v[2] = p->v[0];
    Ot_Add(&rc->ot, p, otz);
    rc->stats.prims_emitted++;
}

void Rc_Flush(RenderContext* rc, Framebuffer* fb) {
    Raster_ResetStats();
    if (g_config.zbuffer) Fb_ClearZ();

    int total = 0;
    if (g_debug_draworder) {
        for (int i = OT_SIZE - 1; i >= 0; i--)
            for (const Prim* p = rc->ot.bucket[i]; p; p = p->next)
                total++;
    }

    int drawn = 0;
    for (int i = OT_SIZE - 1; i >= 0; i--) {
        for (Prim* p = rc->ot.bucket[i]; p; p = p->next) {
            if (g_debug_draworder) {
                Prim copy = *p;
                copy.tex = nullptr;
                switch (copy.type) { // strip texture, flatten shading for the tint
                case PRIM_TRI_F:
                case PRIM_TRI_G:
                case PRIM_TRI_FT:
                case PRIM_TRI_GT: copy.type = PRIM_TRI_F; break;
                case PRIM_SPRT:   copy.type = PRIM_TILE;  break;
                default: break;
                }
                int t = (total > 1) ? (drawn * 255) / (total - 1) : 0;
                for (int k = 0; k < 3; k++) { // blue early -> red late
                    copy.v[k].r = (u8)t;
                    copy.v[k].g = 0;
                    copy.v[k].b = (u8)(255 - t);
                }
                Raster_DrawPrim(fb, &copy);
            } else {
                Raster_DrawPrim(fb, p);
            }
            drawn++;
        }
    }
}
