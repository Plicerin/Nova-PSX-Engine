// Renderer frontend (spec 4.1): scene objects -> GTE transform -> primitive
// packets -> ordering table -> rasterizer.
#pragma once
#include "engine/core/types.h"
#include "engine/math/gte.h"
#include "engine/renderer/prim.h"
#include "engine/renderer/framebuffer.h"
#include "engine/assets/assets.h"

struct Camera {
    LVec pos;          // engine units
    SVec rot;          // PS1 angle units (pitch=vx, yaw=vy, roll=vz)
    i32  near_z;       // engine units; faces with any vertex closer are dropped
    i32  far_z;        // engine units; faces entirely beyond are dropped
};

struct RenderStats {
    u32 prims_emitted;
    u32 tris_emitted;
    u32 faces_culled;    // backface or near/far rejected
    u32 objects_drawn;
};

struct RenderContext {
    OrderingTable ot;
    PrimArena     arena;
    RenderStats   stats;
    LevelFog      fog;
    LevelLight    light;
    Mat           view;      // world -> view (camera) matrix, built by Rc_Begin
    Camera        cam;
    i32           proj_h;    // projection distance, from g_config.fov_deg
};

void Rc_Init(RenderContext* rc);

// Start a frame: clears OT/arena/stats, builds the view matrix from cam,
// programs the GTE projection (center of current internal resolution, fov),
// and the fog range from rc->fog.
void Rc_Begin(RenderContext* rc, const Camera* cam);

// Transform + light + fog + emit all primitives of a mesh instance.
// model = model-to-world matrix (rotation 4.12 + translation).
// Applies: backface culling via NClip (unless double-sided), PS1-style whole-
// poly near rejection, far rejection, directional+ambient vertex lighting
// (only for prims whose mesh has normals and light.enabled), fog blend toward
// fog color by GTE p factor, quad split into two tris (v0v1v2, v1v3v2).
void Rc_DrawMesh(RenderContext* rc, const Mesh* mesh, const Mat* model);

// Camera-facing textured quad at a world position (spec 19 billboard).
// w,h in engine units; scaled by projection (size on screen = dim * h / z).
// Emitted as PRIM_SPRT with computed screen size, semi-transparent optional.
void Rc_DrawBillboard(RenderContext* rc, const TexInfo* tex, LVec world_pos,
                      i32 w_units, i32 h_units, u8 r, u8 g, u8 b,
                      bool semitrans, u8 semi_mode);

// Screen-space primitives (UI/debug drawn through the OT if desired).
// Coords in pixels; otz picks the bucket (0 = drawn last / nearest).
void Rc_AddTile(RenderContext* rc, int x, int y, int w, int h,
                u8 r, u8 g, u8 b, int otz, bool semitrans, u8 semi_mode);
void Rc_AddSprite(RenderContext* rc, const TexInfo* tex, int x, int y,
                  int w, int h, u8 u0, u8 v0, u8 r, u8 g, u8 b, int otz);
void Rc_AddLine(RenderContext* rc, int x0, int y0, int x1, int y1,
                u8 r, u8 g, u8 b, int otz);

// Walk the OT far -> near and rasterize everything into fb.
void Rc_Flush(RenderContext* rc, Framebuffer* fb);

// Draw-order debug: when set, Rc_Flush tints primitives by draw index
// (early=blue -> late=red) instead of their real color.
extern bool g_debug_draworder;
