// Runtime asset structures + binary loaders (spec 6, 7, 12).
// All formats are little-endian binaries produced by the Python tools in
// /tools. Exact byte layouts are pinned in docs/file_formats.md — the C++
// loaders and Python writers must both follow that document.
#pragma once
#include "engine/core/types.h"
#include "engine/math/gte.h"
#include "engine/renderer/vram.h"

// --- mesh (loaded from .meshbin, magic 'PXMS') ---

enum MeshPrimType : u8 {
    MP_F3 = 0, MP_G3 = 1, MP_FT3 = 2, MP_GT3 = 3,
    MP_F4 = 4, MP_G4 = 5, MP_FT4 = 6, MP_GT4 = 7,
};

enum MeshPrimFlags : u8 {
    MPF_SEMITRANS    = 1 << 0,
    MPF_SEMIMODE_SHIFT = 1,      // bits 1-2: semi-transparency mode 0..3
    MPF_DOUBLESIDED  = 1 << 3,   // skip backface cull
    MPF_UVSCROLL     = 1 << 4,   // add RenderContext uvscroll offset (water etc.)
    MPF_MATTE        = 1 << 5,   // no specular highlight (matte materials)
};

struct MeshPrim {           // 36 bytes on disk, see file_formats.md
    u8  type;               // MeshPrimType
    u8  flags;              // MeshPrimFlags
    u16 tex_index;          // into Mesh::tex_names, 0xFFFF = untextured
    u16 vi[4];              // vertex indices (vi[3] unused for tris)
    u8  uv[4][2];           // texel coords (pixels within texture)
    u8  rgb[4][3];          // vertex colors (128 = neutral for textured)
    i16 sort_bias;          // added to otz (negative = draw later/nearer)
};

struct Mesh {
    char      name[32];
    u32       nverts, nnorms, nprims, ntex;
    u32       tri_count;    // quads count as 2 (budget display)
    i32       radius;       // bounding radius, engine units
    SVec*     verts;
    SVec*     norms;        // 4.12 normals, may be null; per-VERTEX, indexed like verts
    MeshPrim* prims;
    char      (*tex_names)[32];
    const TexInfo** tex;    // resolved at load time, size ntex
};

// --- level (loaded from .lvlbin, magic 'PXLV') ---

struct LevelObject {
    char mesh[32];
    LVec pos;               // engine units
    SVec rot;               // PS1 angle units
    fx12 scale[3];          // 4.12
    const Mesh* mesh_ptr;   // resolved at load
};

struct LevelFog {
    u8  enabled;
    u8  r, g, b;
    i32 start_z, end_z;     // view-space, engine units
};

struct LevelLight {
    u8  enabled;
    u8  amb_r, amb_g, amb_b;
    u8  dif_r, dif_g, dif_b;
    SVec dir;               // 4.12, need not be normalized in file; loader normalizes
    // Second, independently-coloured directional "fill" (level format v2).
    // Zeroed (disabled) for v1 levels. Gives two-tone colored lighting.
    u8  fil_en;
    u8  fil_r, fil_g, fil_b;
    SVec fdir;              // 4.12, loader normalizes
};

struct Level {
    char         name[32];
    u32          nobjects;
    LevelObject* objects;
    LVec         cam_pos;
    SVec         cam_rot;
    LevelFog     fog;
    LevelLight   light;
    u8           clear_r, clear_g, clear_b;
};

// --- rig (loaded from .rigbin, magic 'PXRG') ---
// Rigid-parts skeleton: bones form a hierarchy (parents before children),
// each bone optionally draws one mesh segment. No vertex skinning (PS1 style).

struct RigBone {
    char        name[16];
    char        mesh_name[32];  // all-NUL = no geometry
    i16         parent;         // -1 for root (bone 0)
    SVec        bind_pos;       // rest offset from parent, engine units
    SVec        bind_rot;       // hinge frame, PS1 angle units (0 = parent axes)
    const Mesh* mesh;           // resolved at load, may be null
};

struct Rig {
    char     name[32];
    u32      nbones;
    RigBone* bones;
};

// --- animation clip (loaded from .animbin, magic 'PXAN') ---

struct AnimKey { SVec rot; SVec pos; };  // rot: PS1 angle units; pos: engine units

struct AnimClip {
    char       name[32];
    char       rig_name[32];
    u32        nbones;        // must equal rig's
    u16        nkeys;
    u16        key_ms;        // duration of one key interval
    u8         loop;          // 0 = hold last key, 1 = wrap
    AnimKey*   keys;          // nkeys * nbones, key-major bone-minor
    const Rig* rig;           // resolved at load
};

// --- manifest / registry ---

enum AssetType : u8 {
    ASSET_TEXTURE = 0, ASSET_MESH = 1, ASSET_SOUND = 2, ASSET_LEVEL = 3,
    ASSET_RIG = 4, ASSET_ANIM = 5,
};

// Load build/assets/manifest.bin, then every listed asset:
// textures -> uploaded into simulated VRAM + registered; meshes -> loaded +
// texture refs resolved; sounds -> loaded into the audio budget.
// Returns false (and logs) on any missing/corrupt file or budget violation.
bool Assets_LoadAll(const char* manifest_path);

const TexInfo* Tex_Find(const char* name);   // null if missing
const Mesh*    Mesh_Find(const char* name);
const Rig*      Rig_Find(const char* name);
const AnimClip* Anim_Find(const char* name);
struct Sample;                                // audio.h
Sample*        Sound_Find(const char* name);
Level*         Level_Load(const char* path);  // .lvlbin; caller owns nothing (static)

int  Assets_TextureCount();
const TexInfo* Assets_TextureByIndex(int i);
