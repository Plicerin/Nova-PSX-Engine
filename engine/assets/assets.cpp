// Binary asset loaders + registries. Byte layouts are pinned in
// docs/file_formats.md; every field is read with explicit little-endian
// helpers from the raw Plat_ReadFile buffer (on-disk records are packed,
// C++ structs are not — never fread into them).
#include "engine/assets/assets.h"
#include "engine/audio/audio.h"
#include "engine/math/fixed.h"
#include "engine/platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ------------------------------------------------------------- LE readers
static u16 RdU16(const u8* p) { return (u16)((u16)p[0] | ((u16)p[1] << 8)); }
static u32 RdU32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static i16 RdI16(const u8* p) { return (i16)RdU16(p); }
static i32 RdI32(const u8* p) { return (i32)RdU32(p); }

static bool IsPow2(u32 v) { return v != 0 && (v & (v - 1)) == 0; }
static bool NameEq(const char* a, const char* b) { return strncmp(a, b, 32) == 0; }

// ------------------------------------------------------------- registries
enum { MAX_TEXTURES = 256, MAX_MESHES = 128, MAX_SOUNDS = 128 };

static TexInfo s_textures[MAX_TEXTURES];
static int     s_tex_count = 0;
static Mesh    s_meshes[MAX_MESHES];
static int     s_mesh_count = 0;
static Sample* s_sounds[MAX_SOUNDS];
static int     s_sound_count = 0;

const TexInfo* Tex_Find(const char* name) {
    for (int i = 0; i < s_tex_count; i++)
        if (NameEq(s_textures[i].name, name)) return &s_textures[i];
    return nullptr;
}

const Mesh* Mesh_Find(const char* name) {
    for (int i = 0; i < s_mesh_count; i++)
        if (NameEq(s_meshes[i].name, name)) return &s_meshes[i];
    return nullptr;
}

Sample* Sound_Find(const char* name) {
    for (int i = 0; i < s_sound_count; i++)
        if (NameEq(s_sounds[i]->name, name)) return s_sounds[i];
    return nullptr;
}

int Assets_TextureCount() { return s_tex_count; }

const TexInfo* Assets_TextureByIndex(int i) {
    if (i < 0 || i >= s_tex_count) return nullptr;
    return &s_textures[i];
}

// Frees mesh heap arrays and clears all registries (Sample memory is owned
// by the audio module; VRAM tracking is not rewound here).
static void ResetRegistries() {
    for (int i = 0; i < s_mesh_count; i++) {
        Mesh* m = &s_meshes[i];
        free(m->verts);
        free(m->norms);
        free(m->prims);
        free(m->tex_names);
        free((void*)m->tex);
    }
    memset(s_meshes, 0, sizeof(s_meshes));
    memset(s_textures, 0, sizeof(s_textures));
    memset(s_sounds, 0, sizeof(s_sounds));
    s_tex_count = s_mesh_count = s_sound_count = 0;
}

// ------------------------------------------------------------- .texbin
static bool ParseTexture(const u8* buf, u32 size, const char* path) {
    if (size < 64 || memcmp(buf, "PXTX", 4) != 0) {
        fprintf(stderr, "[assets] %s: not a PXTX file\n", path);
        return false;
    }
    u32 version = RdU32(buf + 4);
    if (version != 1) {
        fprintf(stderr, "[assets] %s: unsupported texture version %u\n", path, (unsigned)version);
        return false;
    }
    u8  format   = buf[40];
    u16 w        = RdU16(buf + 42);
    u16 h        = RdU16(buf + 44);
    u16 vx       = RdU16(buf + 46);
    u16 vy       = RdU16(buf + 48);
    u16 clut_x   = RdU16(buf + 50);
    u16 clut_y   = RdU16(buf + 52);
    u16 clut_len = RdU16(buf + 54);
    u32 cost     = RdU32(buf + 56);
    u32 data_len = RdU32(buf + 60);

    if (format > TEX_15BIT) {
        fprintf(stderr, "[assets] %s: bad format %u\n", path, (unsigned)format);
        return false;
    }
    if (!IsPow2(w) || !IsPow2(h) || w < 8 || w > 256 || h < 8 || h > 256) {
        fprintf(stderr, "[assets] %s: bad dimensions %ux%u (pow2 8..256 required)\n",
                path, (unsigned)w, (unsigned)h);
        return false;
    }
    u32 w_hw = (format == TEX_4BIT) ? w / 4u : (format == TEX_8BIT) ? w / 2u : w;
    if (clut_len != 0 && clut_len != 16 && clut_len != 256) {
        fprintf(stderr, "[assets] %s: bad clut_len %u\n", path, (unsigned)clut_len);
        return false;
    }
    // Indexed formats must ship a CLUT big enough for their index range,
    // or Vram_Texel would sample untracked/out-of-rect VRAM at draw time.
    if (format == TEX_4BIT && clut_len == 0) {
        fprintf(stderr, "[assets] %s: 4-bit texture without CLUT\n", path);
        return false;
    }
    if (format == TEX_8BIT && clut_len != 256) {
        fprintf(stderr, "[assets] %s: 8-bit texture needs a 256-entry CLUT\n", path);
        return false;
    }
    if (data_len != w_hw * (u32)h * 2u) {
        fprintf(stderr, "[assets] %s: data_len %u != expected %u\n",
                path, (unsigned)data_len, (unsigned)(w_hw * (u32)h * 2u));
        return false;
    }
    if (64ull + 2ull * clut_len + (u64)data_len > (u64)size) {
        fprintf(stderr, "[assets] %s: truncated file\n", path);
        return false;
    }
    if (vx < VRAM_TEX_X0) {
        fprintf(stderr, "[assets] %s: vram_x %u inside display region (< %d)\n",
                path, (unsigned)vx, VRAM_TEX_X0);
        return false;
    }
    if ((int)vx + (int)w_hw > VRAM_W || (int)vy + (int)h > VRAM_H) {
        fprintf(stderr, "[assets] %s: pixel rect out of VRAM bounds\n", path);
        return false;
    }
    if (clut_len) {
        if (clut_x == 0xFFFF || clut_y == 0xFFFF ||
            (int)clut_x + (int)clut_len > VRAM_W || clut_y >= VRAM_H) {
            fprintf(stderr, "[assets] %s: bad CLUT placement (%u,%u len %u)\n",
                    path, (unsigned)clut_x, (unsigned)clut_y, (unsigned)clut_len);
            return false;
        }
        if (clut_x < VRAM_TEX_X0) {
            fprintf(stderr, "[assets] %s: clut_x %u inside display region (< %d)\n",
                    path, (unsigned)clut_x, VRAM_TEX_X0);
            return false;
        }
    } else {
        clut_x = clut_y = 0xFFFF; // normalize "no CLUT" marker
    }
    if (s_tex_count >= MAX_TEXTURES) {
        fprintf(stderr, "[assets] texture registry full (%d)\n", MAX_TEXTURES);
        return false;
    }

    const u8* p = buf + 64;
    if (clut_len) {
        u16 clut[256];
        for (u32 i = 0; i < clut_len; i++) clut[i] = RdU16(p + i * 2);
        Vram_WriteRect(clut_x, clut_y, clut_len, 1, clut);
        p += 2u * clut_len;
    }
    u32 nhw = data_len / 2u;
    u16* px = (u16*)malloc(data_len);
    if (!px) {
        fprintf(stderr, "[assets] %s: out of memory (%u bytes)\n", path, (unsigned)data_len);
        return false;
    }
    for (u32 i = 0; i < nhw; i++) px[i] = RdU16(p + i * 2);
    Vram_WriteRect(vx, vy, (int)w_hw, h, px);
    free(px);

    Vram_TrackAlloc(vx, vy, (int)w_hw, h, cost);
    if (clut_len)
        Vram_TrackAlloc(clut_x, clut_y, clut_len, 1, 0); // cost already counted above

    TexInfo* t = &s_textures[s_tex_count++];
    memset(t, 0, sizeof(*t));
    memcpy(t->name, buf + 8, 32);
    t->format     = format;
    t->w          = w;
    t->h          = h;
    t->vx         = vx;
    t->vy         = vy;
    t->clut_x     = clut_x;
    t->clut_y     = clut_y;
    t->clut_len   = clut_len;
    t->vram_bytes = cost;
    t->page       = (u16)((vy / 256) * 16 + (vx / 64));
    return true;
}

static bool LoadTextureFile(const char* path) {
    u32 size = 0;
    u8* buf = Plat_ReadFile(path, &size);
    if (!buf) {
        fprintf(stderr, "[assets] cannot read texture: %s\n", path);
        return false;
    }
    bool ok = ParseTexture(buf, size, path);
    free(buf);
    return ok;
}

// ------------------------------------------------------------- .meshbin
static void FreeMeshArrays(SVec* v, SVec* n, MeshPrim* p, char (*tn)[32], const TexInfo** t) {
    free(v); free(n); free(p); free(tn); free((void*)t);
}

static bool ParseMesh(const u8* buf, u32 size, const char* path) {
    if (size < 64 || memcmp(buf, "PXMS", 4) != 0) {
        fprintf(stderr, "[assets] %s: not a PXMS file\n", path);
        return false;
    }
    u32 version = RdU32(buf + 4);
    if (version != 1) {
        fprintf(stderr, "[assets] %s: unsupported mesh version %u\n", path, (unsigned)version);
        return false;
    }
    u32 nverts = RdU32(buf + 40);
    u32 nnorms = RdU32(buf + 44);
    u32 nprims = RdU32(buf + 48);
    u32 ntex   = RdU32(buf + 52);
    if (nnorms != 0 && nnorms != nverts) {
        fprintf(stderr, "[assets] %s: nnorms %u must be 0 or nverts %u\n",
                path, (unsigned)nnorms, (unsigned)nverts);
        return false;
    }
    u64 need = 64ull + (u64)nverts * 6 + (u64)nnorms * 6 + (u64)ntex * 32 + (u64)nprims * 36;
    if (need > (u64)size) {
        fprintf(stderr, "[assets] %s: truncated file\n", path);
        return false;
    }
    if (s_mesh_count >= MAX_MESHES) {
        fprintf(stderr, "[assets] mesh registry full (%d)\n", MAX_MESHES);
        return false;
    }

    SVec*     verts = nullptr;
    SVec*     norms = nullptr;
    MeshPrim* prims = nullptr;
    char      (*tex_names)[32] = nullptr;
    const TexInfo** tex = nullptr;
    if (nverts) verts = (SVec*)malloc(nverts * sizeof(SVec));
    if (nnorms) norms = (SVec*)malloc(nnorms * sizeof(SVec));
    if (nprims) prims = (MeshPrim*)malloc(nprims * sizeof(MeshPrim));
    if (ntex) {
        tex_names = (char(*)[32])malloc((size_t)ntex * 32);
        tex       = (const TexInfo**)malloc(ntex * sizeof(const TexInfo*));
    }
    if ((nverts && !verts) || (nnorms && !norms) || (nprims && !prims) ||
        (ntex && (!tex_names || !tex))) {
        fprintf(stderr, "[assets] %s: out of memory\n", path);
        FreeMeshArrays(verts, norms, prims, tex_names, tex);
        return false;
    }

    const u8* p = buf + 64;
    for (u32 i = 0; i < nverts; i++, p += 6) {
        verts[i].vx = RdI16(p); verts[i].vy = RdI16(p + 2); verts[i].vz = RdI16(p + 4);
    }
    for (u32 i = 0; i < nnorms; i++, p += 6) {
        norms[i].vx = RdI16(p); norms[i].vy = RdI16(p + 2); norms[i].vz = RdI16(p + 4);
    }
    if (ntex) {
        memcpy(tex_names, p, (size_t)ntex * 32);
        p += (size_t)ntex * 32;
    }
    for (u32 i = 0; i < nprims; i++, p += 36) {
        MeshPrim* pr = &prims[i];
        pr->type      = p[0];
        pr->flags     = p[1];
        pr->tex_index = RdU16(p + 2);
        for (int k = 0; k < 4; k++) pr->vi[k] = RdU16(p + 4 + k * 2);
        memcpy(pr->uv, p + 12, 8);
        memcpy(pr->rgb, p + 20, 12);
        pr->sort_bias = RdI16(p + 32);
        if (pr->type > MP_GT4) {
            fprintf(stderr, "[assets] %s: prim %u bad type %u\n", path, (unsigned)i, (unsigned)pr->type);
            FreeMeshArrays(verts, norms, prims, tex_names, tex);
            return false;
        }
        if (pr->tex_index != 0xFFFF && pr->tex_index >= ntex) {
            fprintf(stderr, "[assets] %s: prim %u tex_index %u out of range\n",
                    path, (unsigned)i, (unsigned)pr->tex_index);
            FreeMeshArrays(verts, norms, prims, tex_names, tex);
            return false;
        }
        int nv = (pr->type >= MP_F4) ? 4 : 3;
        for (int k = 0; k < nv; k++) {
            if (pr->vi[k] >= nverts) {
                fprintf(stderr, "[assets] %s: prim %u vertex index %u out of range\n",
                        path, (unsigned)i, (unsigned)pr->vi[k]);
                FreeMeshArrays(verts, norms, prims, tex_names, tex);
                return false;
            }
        }
    }

    Mesh* m = &s_meshes[s_mesh_count++];
    memset(m, 0, sizeof(*m));
    memcpy(m->name, buf + 8, 32);
    m->nverts    = nverts;
    m->nnorms    = nnorms;
    m->nprims    = nprims;
    m->ntex      = ntex;
    m->tri_count = RdU32(buf + 56);
    m->radius    = RdI32(buf + 60);
    m->verts     = verts;
    m->norms     = norms;
    m->prims     = prims;
    m->tex_names = tex_names;
    m->tex       = tex;
    for (u32 i = 0; i < ntex; i++) {
        tex[i] = Tex_Find(tex_names[i]);
        if (!tex[i])
            fprintf(stderr, "[assets] %s: texture '%.32s' not found (drawn untextured)\n",
                    path, tex_names[i]);
    }
    return true;
}

static bool LoadMeshFile(const char* path) {
    u32 size = 0;
    u8* buf = Plat_ReadFile(path, &size);
    if (!buf) {
        fprintf(stderr, "[assets] cannot read mesh: %s\n", path);
        return false;
    }
    bool ok = ParseMesh(buf, size, path);
    free(buf);
    return ok;
}

// ------------------------------------------------------------- manifest
static bool LoadAllFromManifest(const u8* buf, u32 size, const char* manifest_path) {
    if (size < 12 || memcmp(buf, "PXMF", 4) != 0) {
        fprintf(stderr, "[assets] %s: not a PXMF manifest\n", manifest_path);
        return false;
    }
    u32 version = RdU32(buf + 4);
    if (version != 1) {
        fprintf(stderr, "[assets] %s: unsupported manifest version %u\n",
                manifest_path, (unsigned)version);
        return false;
    }
    u32 count = RdU32(buf + 8);
    if (12ull + (u64)count * 108 > (u64)size) {
        fprintf(stderr, "[assets] %s: truncated manifest (%u records)\n",
                manifest_path, (unsigned)count);
        return false;
    }
    for (u32 i = 0; i < count; i++) {
        u8 type = buf[12 + i * 108];
        if (type > ASSET_LEVEL) {
            fprintf(stderr, "[assets] %s: record %u unknown asset type %u\n",
                    manifest_path, (unsigned)i, (unsigned)type);
            return false;
        }
    }

    ResetRegistries();

    // Load order per contract: textures, then meshes (need Tex_Find), then
    // sounds. Levels are listed only for enumeration; Level_Load is on demand.
    static const u8 kPassType[3] = { ASSET_TEXTURE, ASSET_MESH, ASSET_SOUND };
    for (int pass = 0; pass < 3; pass++) {
        for (u32 i = 0; i < count; i++) {
            const u8* rec = buf + 12 + i * 108;
            if (rec[0] != kPassType[pass]) continue;
            char name[33]; memcpy(name, rec + 4, 32);  name[32] = 0;
            char path[65]; memcpy(path, rec + 36, 64); path[64] = 0;
            if (pass == 0) {
                if (!LoadTextureFile(path)) return false;
            } else if (pass == 1) {
                if (!LoadMeshFile(path)) return false;
            } else {
                if (s_sound_count >= MAX_SOUNDS) {
                    fprintf(stderr, "[assets] sound registry full (%d)\n", MAX_SOUNDS);
                    return false;
                }
                Sample* s = Audio_LoadWav(path, name, rec[1] != 0, rec[2] != 0);
                if (!s) {
                    fprintf(stderr, "[assets] sound load failed: %s\n", path);
                    return false;
                }
                s_sounds[s_sound_count++] = s;
            }
        }
        // Authenticity budget is HARD: reject the whole set if textures blow it.
        if (pass == 0 && Vram_UsedBytes() > VRAM_TEX_BUDGET) {
            fprintf(stderr, "[assets] VRAM texture budget exceeded: %u > %u bytes\n",
                    (unsigned)Vram_UsedBytes(), (unsigned)VRAM_TEX_BUDGET);
            return false;
        }
    }
    return true;
}

bool Assets_LoadAll(const char* manifest_path) {
    u32 size = 0;
    u8* buf = Plat_ReadFile(manifest_path, &size);
    if (!buf) {
        fprintf(stderr, "[assets] cannot read manifest: %s\n", manifest_path);
        return false;
    }
    bool ok = LoadAllFromManifest(buf, size, manifest_path);
    free(buf);
    return ok;
}

// ------------------------------------------------------------- .lvlbin
static Level s_level; // one level at a time; Level_Load owns its object array

// Object record: fields at 0/32/44/50/52 sum to 64 bytes (the doc's "60-byte"
// line contradicts its own offsets; explicit offsets are authoritative).
static const u32 kLvlHeaderSize = 96;
static const u32 kLvlObjSize    = 64;

static bool ParseLevel(const u8* buf, u32 size, const char* path) {
    if (size < kLvlHeaderSize || memcmp(buf, "PXLV", 4) != 0) {
        fprintf(stderr, "[assets] %s: not a PXLV file\n", path);
        return false;
    }
    u32 version = RdU32(buf + 4);
    if (version != 1) {
        fprintf(stderr, "[assets] %s: unsupported level version %u\n", path, (unsigned)version);
        return false;
    }
    u32 nobjects = RdU32(buf + 40);
    if ((u64)kLvlHeaderSize + (u64)nobjects * kLvlObjSize > (u64)size) {
        fprintf(stderr, "[assets] %s: truncated level (%u objects)\n", path, (unsigned)nobjects);
        return false;
    }

    free(s_level.objects);
    memset(&s_level, 0, sizeof(s_level));

    memcpy(s_level.name, buf + 8, 32);
    s_level.cam_pos.vx = RdI32(buf + 44);
    s_level.cam_pos.vy = RdI32(buf + 48);
    s_level.cam_pos.vz = RdI32(buf + 52);
    s_level.cam_rot.vx = RdI16(buf + 56);
    s_level.cam_rot.vy = RdI16(buf + 58);
    s_level.cam_rot.vz = RdI16(buf + 60);

    s_level.fog.enabled = buf[64];
    s_level.fog.r       = buf[65];
    s_level.fog.g       = buf[66];
    s_level.fog.b       = buf[67];
    s_level.fog.start_z = RdI32(buf + 68);
    s_level.fog.end_z   = RdI32(buf + 72);

    s_level.light.enabled = buf[76];
    s_level.light.amb_r   = buf[77];
    s_level.light.amb_g   = buf[78];
    s_level.light.amb_b   = buf[79];
    s_level.light.dif_r   = buf[80];
    s_level.light.dif_g   = buf[81];
    s_level.light.dif_b   = buf[82];
    s_level.light.dir.vx  = RdI16(buf + 84);
    s_level.light.dir.vy  = RdI16(buf + 86);
    s_level.light.dir.vz  = RdI16(buf + 88);

    s_level.clear_r = buf[92];
    s_level.clear_g = buf[93];
    s_level.clear_b = buf[94];

    // Normalize light dir to length 4096 (4.12). Squares fit u32: 3*32767^2.
    {
        i32 dx = s_level.light.dir.vx, dy = s_level.light.dir.vy, dz = s_level.light.dir.vz;
        u32 len2 = (u32)(dx * dx) + (u32)(dy * dy) + (u32)(dz * dz);
        u32 len  = IsqrtU32(len2);
        if (len == 0) {
            fprintf(stderr, "[assets] %s: zero light dir, defaulting to +z\n", path);
            s_level.light.dir.vx = 0;
            s_level.light.dir.vy = 0;
            s_level.light.dir.vz = (i16)FX_ONE;
        } else {
            s_level.light.dir.vx = (i16)(dx * FX_ONE / (i32)len);
            s_level.light.dir.vy = (i16)(dy * FX_ONE / (i32)len);
            s_level.light.dir.vz = (i16)(dz * FX_ONE / (i32)len);
        }
    }

    s_level.nobjects = nobjects;
    if (nobjects) {
        s_level.objects = (LevelObject*)malloc(nobjects * sizeof(LevelObject));
        if (!s_level.objects) {
            fprintf(stderr, "[assets] %s: out of memory (%u objects)\n", path, (unsigned)nobjects);
            memset(&s_level, 0, sizeof(s_level));
            return false;
        }
        for (u32 i = 0; i < nobjects; i++) {
            const u8* r = buf + kLvlHeaderSize + i * kLvlObjSize;
            LevelObject* o = &s_level.objects[i];
            memset(o, 0, sizeof(*o));
            memcpy(o->mesh, r, 32);
            o->pos.vx = RdI32(r + 32);
            o->pos.vy = RdI32(r + 36);
            o->pos.vz = RdI32(r + 40);
            o->rot.vx = RdI16(r + 44);
            o->rot.vy = RdI16(r + 46);
            o->rot.vz = RdI16(r + 48);
            o->scale[0] = RdI32(r + 52);
            o->scale[1] = RdI32(r + 56);
            o->scale[2] = RdI32(r + 60);
            o->mesh_ptr = Mesh_Find(o->mesh);
            if (!o->mesh_ptr)
                fprintf(stderr, "[assets] %s: object %u mesh '%.32s' not found\n",
                        path, (unsigned)i, o->mesh);
        }
    }
    return true;
}

Level* Level_Load(const char* path) {
    u32 size = 0;
    u8* buf = Plat_ReadFile(path, &size);
    if (!buf) {
        fprintf(stderr, "[assets] cannot read level: %s\n", path);
        return nullptr;
    }
    bool ok = ParseLevel(buf, size, path);
    free(buf);
    return ok ? &s_level : nullptr;
}
