// Demo scene implementation (spec 19).
#include "game/src/demo_scene.h"

#include <cstdio>
#include <cstring>

#include "engine/core/config.h"
#include "engine/math/fixed.h"
#include "engine/math/gte.h"
#include "engine/audio/audio.h"
#include "engine/input/input.h"
#include "engine/debug/debug.h"

static Level*         s_level     = nullptr;
static LevelObject*   s_gem       = nullptr;
static i32            s_gem_base_y = 0;
static Sample*        s_blip      = nullptr;
static const TexInfo* s_orb       = nullptr;

static LVec s_pos;          // player eye position, engine units (y DOWN)
static i32  s_yaw;          // PS1 angle units
static i32  s_pitch;        // PS1 angle units
static u32  s_tick;
static i32  s_bob;          // gem bob offset this tick, engine units
static i32  s_spin;         // turntable deg/tick for a "girl" object (0 = off)
static LevelObject* s_spin_obj = nullptr;

static constexpr i32 kRoomBound = 1400 - 128;  // room half-extent minus wall margin
static constexpr i32 kSpeed     = 13;          // units/tick ~= 3 m/s at 60 Hz
static constexpr i32 kTurnRate  = 30;          // angle units/tick

// Turntable rate for a showcase character (set from CLI before Demo_Init).
void Demo_SetSpin(i32 deg_per_tick) { s_spin = deg_per_tick; }

void Demo_Init(Level* level) {
    s_level = level;
    s_gem   = nullptr;
    s_tick  = 0;
    s_bob   = 0;
    if (!level) {
        fprintf(stderr, "demo: Demo_Init called with null level\n");
        return;
    }

    s_pos = level->cam_pos;                 // honor the level camera fully
    s_yaw   = level->cam_rot.vy & (ANGLE_FULL - 1);
    s_pitch = level->cam_rot.vx;

    s_spin_obj = nullptr;
    for (u32 i = 0; i < level->nobjects; i++) {
        if (strcmp(level->objects[i].mesh, "gem") == 0) {
            s_gem = &level->objects[i];
            s_gem_base_y = s_gem->pos.vy;
        }
        if (strcmp(level->objects[i].mesh, "girl") == 0)
            s_spin_obj = &level->objects[i];
    }

    s_orb = Tex_Find("orb_glow");
    if (!s_orb) fprintf(stderr, "demo: texture 'orb_glow' missing\n");
    s_blip = Sound_Find("blip");
    if (!s_blip) fprintf(stderr, "demo: sound 'blip' missing\n");

    Sample* amb = Sound_Find("ambience");
    if (amb) Audio_Play(amb, 36, 0, 4096);
    else fprintf(stderr, "demo: sound 'ambience' missing\n");
}

void Demo_Update() {
    if (!s_level) return;
    s_tick++;

    if (Pad_Held(PAD_L1)) s_yaw -= kTurnRate;
    if (Pad_Held(PAD_R1)) s_yaw += kTurnRate;
    s_yaw &= ANGLE_FULL - 1;

    // Stick Y up (negative) = forward; forward = +z rotated by yaw.
    i32 fwd = -(i32)Pad_StickY();
    i32 str = (i32)Pad_StickX();
    i32 mf = (fwd * kSpeed) / 128;
    i32 ms = (str * kSpeed) / 128;
    s_pos.vx += (Csin(s_yaw) * mf + Ccos(s_yaw) * ms) >> 12;
    s_pos.vz += (Ccos(s_yaw) * mf - Csin(s_yaw) * ms) >> 12;
    s_pos.vx = ClampI32(s_pos.vx, -kRoomBound, kRoomBound);
    s_pos.vz = ClampI32(s_pos.vz, -kRoomBound, kRoomBound);

    // Gem showcase: spin + 0.12 m bob from the deterministic sine table.
    s_bob = (Csin((i32)((s_tick * 40u) & 4095u)) * 30) >> 12;
    if (s_gem) {
        s_gem->rot.vy = (i16)((s_gem->rot.vy + 12) & (ANGLE_FULL - 1));
        s_gem->rot.vx = (i16)((s_gem->rot.vx + 5) & (ANGLE_FULL - 1));
        s_gem->pos.vy = s_gem_base_y - s_bob;
    }
    if (s_spin_obj && s_spin)
        s_spin_obj->rot.vy = (i16)((s_spin_obj->rot.vy + s_spin) & (ANGLE_FULL - 1));

    if (s_blip && Pad_Pressed(PAD_CROSS))
        Audio_Play(s_blip, 100, 0, 4096);
}

void Demo_Render(RenderContext* rc, Framebuffer* fb) {
    if (!s_level) return;

    // Level fog/light copied every frame so debug toggles can't go stale.
    rc->fog   = s_level->fog;
    rc->light = s_level->light;

    Camera scene_cam;
    scene_cam.pos    = s_pos;
    scene_cam.rot    = { (i16)s_pitch, (i16)s_yaw, 0 };
    scene_cam.near_z = 40;                // ~16 cm
    scene_cam.far_z  = 20 * WORLD_SCALE;  // 20 m

    // Seed the free cam while inactive so toggling starts from the current view.
    if (!Debug_FreeCamActive()) Debug_SyncFreeCam(&scene_cam);
    const Camera* cam = Debug_FreeCamActive() ? Debug_FreeCam() : &scene_cam;

    Rc_Begin(rc, cam);
    Fb_Clear(fb, s_level->clear_r, s_level->clear_g, s_level->clear_b);
    if (g_config.zbuffer) Fb_ClearZ();

    for (u32 i = 0; i < s_level->nobjects; i++) {
        LevelObject* o = &s_level->objects[i];
        if (!o->mesh_ptr) continue;
        Mat m;
        Gte_RotMatrix(&o->rot, &m);
        Gte_ScaleMatrix(&m, o->scale[0], o->scale[1], o->scale[2]);
        m.t[0] = o->pos.vx;
        m.t[1] = o->pos.vy;
        m.t[2] = o->pos.vz;
        Rc_DrawMesh(rc, o->mesh_ptr, &m);
    }

    // Glow billboard 0.8 m above the gem, opposite bob phase, additive.
    if (s_gem && s_orb) {
        LVec bp;
        bp.vx = s_gem->pos.vx;
        bp.vy = s_gem_base_y - 205 + s_bob;
        bp.vz = s_gem->pos.vz;
        Rc_DrawBillboard(rc, s_orb, bp, 115, 115, 128, 128, 128, true, 1);
    }

    Rc_Flush(rc, fb);
}

void Demo_DrawUI(Framebuffer* fb) {
    const char* title = "PSX-AUTHENTIC ENGINE";
    const char* help  = "[SPACE] BLIP  [TAB] FREECAM  [F1] DEBUG  [WASD] MOVE";
    int tx = (fb->w - (int)strlen(title) * 8) / 2;
    int hx = (fb->w - (int)strlen(help) * 8) / 2;
    if (tx < 2) tx = 2;
    if (hx < 2) hx = 2;
    Debug_Text(fb, tx, 6, 255, 255, 255, "%s", title);
    Debug_Text(fb, hx, fb->h - 12, 180, 180, 180, "%s", help);
}
