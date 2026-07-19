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
#include "engine/anim/anim.h"
#include "game/src/combat.h"
#include "game/src/dialog.h"
#include "game/src/fx.h"

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

// Robot companion showcase (Step 2 animation proof).
static const Rig*      s_robot_rig = nullptr;
static AnimState       s_robot_anim;
static const AnimClip* s_robot_idle = nullptr;
static const AnimClip* s_robot_attack = nullptr;
static const AnimClip* s_robot_defend = nullptr;
static LVec            s_robot_pos = { (i32)(1.8 * 256), 0, (i32)(-2.5 * 256) };
static SVec            s_robot_rot = { 0, (i16)(2048 + 300), 0 }; // face camera-ish

// Evolver tier showcase (--evolver-tier N): one tier idling in the arena at
// its real draw scale, slowly turning, to inspect shape + size.
static int             s_ev_tier = 0;
static const Rig*      s_ev_rig  = nullptr;
static AnimState       s_ev_anim;
void Demo_SetEvolverTier(int t) { s_ev_tier = t; }

// Generic creature viewer (--show-rig NAME): idles any rig at true scale
// (fixed-size triangles => no scaling), slowly turning, framed close.
static char            s_show_rig[40] = { 0 };
static const Rig*      s_show_r = nullptr;
static AnimState       s_show_anim;
void Demo_SetShowRig(const char* n) {
    strncpy(s_show_rig, n, sizeof(s_show_rig) - 1);
}

// Transformation viewer (--show-morph): plays the baked spider<->snake morph
// frames (one shared triangle pool rearranging), ping-ponging with end holds.
enum { MORPH_FRAMES = 14 };
static bool         s_morph_on = false;
static int          s_morph_hold = -1;     // test hook: freeze on one frame
static const Mesh*  s_morph[MORPH_FRAMES];
static bool         s_combatcam = false;   // view the creature in the arena, combat cam
void Demo_SetShowMorph(bool on) { s_morph_on = on; }
void Demo_SetCombatCam(bool on) { s_combatcam = on; }
void Demo_SetMorphFrame(int f) { s_morph_hold = f; }
static int MorphFrame() {
    if (s_morph_hold >= 0)
        return s_morph_hold >= MORPH_FRAMES ? MORPH_FRAMES - 1 : s_morph_hold;
    const int step = 4, hold = 26, up = (MORPH_FRAMES - 1) * step;
    int total = 2 * up + 2 * hold;
    int u = (int)(s_tick % (u32)total);
    int f;
    if (u < hold) f = 0;                             // hold spider
    else if (u < hold + up) f = (u - hold) / step;   // spider -> snake
    else if (u < hold + up + hold) f = MORPH_FRAMES - 1;  // hold snake
    else f = (MORPH_FRAMES - 1) - (u - hold - up - hold) / step;  // back
    return f < 0 ? 0 : (f >= MORPH_FRAMES ? MORPH_FRAMES - 1 : f);
}

// Shard fold-creature showcase (triangle enemy prototype).
static const Rig*      s_shard_rig = nullptr;
static AnimState       s_shard_anim;
static const AnimClip* s_shard_idle = nullptr;
static const AnimClip* s_shard_attack = nullptr;
static const AnimClip* s_shard_morph = nullptr;
static LVec            s_shard_pos = { (i32)(-1.6 * 256), 0, (i32)(-2.5 * 256) };
static SVec            s_shard_rot = { 0, (i16)2048, 0 };  // face the -z side

static constexpr i32 kRoomBound = 2048 - 128;  // arena half-extent minus wall margin
static constexpr i32 kSpeed     = 13;          // units/tick ~= 3 m/s at 60 Hz
static constexpr i32 kTurnRate  = 30;          // angle units/tick

// Turntable rate for a showcase character (set from CLI before Demo_Init).
void Demo_SetSpin(i32 deg_per_tick) { s_spin = deg_per_tick; }

// Start a named clip on the robot (CLI/test hook; no-op if unknown).
void Demo_PlayClip(const char* name) {
    const AnimClip* c = Anim_Find(name);
    if (c) Anim_Start(&s_robot_anim, c);
    else fprintf(stderr, "demo: clip '%s' not found\n", name);
}

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

    Fx_Init();
    // Ambient mote haze filling the play volume (aquatic-lab atmosphere).
    // The combat arena is a much larger room than the test chamber. The
    // creature viewer wants a clean empty background, so no motes there.
    if (s_show_rig[0])
        Fx_AmbientClear();
    else if (Combat_Active() || Dialog_Active())
        Fx_AmbientInit(LVec{ -1900, -1600, -1500 }, LVec{ 1900, -30, 2400 });
    else
        Fx_AmbientInit(LVec{ -900, -1050, -300 }, LVec{ 900, -30, 1700 });
    if (s_ev_tier) {
        s_ev_rig = Rig_Find(Evolver_RigName(s_ev_tier));
        char cn[24];
        snprintf(cn, sizeof(cn), "evolver_t%d_idle", s_ev_tier);
        Anim_Start(&s_ev_anim, Anim_Find(cn));
    }
    if (s_morph_on) {
        for (int k = 0; k < MORPH_FRAMES; k++) {
            char n[24];
            snprintf(n, sizeof(n), "triarmorph_%02d", k);
            s_morph[k] = Mesh_Find(n);
        }
        Fx_AmbientClear();
    }
    if (s_show_rig[0]) {
        s_show_r = Rig_Find(s_show_rig);
        char cn[48];
        // Prefer the walk clip in the viewer (motion is more useful to inspect
        // than idle); fall back to idle for creatures without one.
        snprintf(cn, sizeof(cn), "%s_walk", s_show_rig);
        const AnimClip* clip = Anim_Find(cn);
        if (!clip) { snprintf(cn, sizeof(cn), "%s_idle", s_show_rig);
                     clip = Anim_Find(cn); }
        Anim_Start(&s_show_anim, clip);
        if (!s_show_r) fprintf(stderr, "demo: --show-rig '%s' not found\n", s_show_rig);
    }
    if (Dialog_Active()) Dialog_Init();
    else if (Combat_Active()) Combat_Init();

    s_robot_rig    = Rig_Find("robot");
    s_robot_idle   = Anim_Find("robot_idle");
    s_robot_attack = Anim_Find("robot_attack");
    s_robot_defend = Anim_Find("robot_defend");
    Anim_Start(&s_robot_anim, s_robot_idle);   // clip may be null: bind pose

    s_shard_rig    = Rig_Find("shard");
    s_shard_idle   = Anim_Find("shard_idle");
    s_shard_attack = Anim_Find("shard_attack");
    s_shard_morph  = Anim_Find("shard_morph");
    Anim_Start(&s_shard_anim, s_shard_idle);

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
    Fx_Update();
    Fx_AmbientUpdate();

    // The scene, then combat, own all input in their modes (no walk-camera
    // bleed-through). Dialog is checked first: it hands off into combat.
    if (s_morph_on) { s_tick++; return; }
    if (s_ev_tier) { s_tick++; Anim_Update(&s_ev_anim, 16); return; }
    if (s_show_rig[0]) { s_tick++; Anim_Update(&s_show_anim, 16); return; }
    if (Dialog_Active()) { Dialog_Update(); return; }
    if (Combat_Active()) { Combat_Update(); return; }

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

    // Robot: E/Triangle = attack, Q/Square = defend, back to idle when done.
    if (s_robot_rig) {
        if (Pad_Pressed(PAD_TRIANGLE) && s_robot_attack)
            Anim_Start(&s_robot_anim, s_robot_attack);
        if (Pad_Pressed(PAD_SQUARE) && s_robot_defend)
            Anim_Start(&s_robot_anim, s_robot_defend);
        Anim_Update(&s_robot_anim, 16);        // fixed 60 Hz sim tick
        if (s_robot_anim.done && s_robot_idle)
            Anim_Start(&s_robot_anim, s_robot_idle);
    }

    // Shard creature: same keys (E = lunge, Q = fold-morph).
    if (s_shard_rig) {
        if (Pad_Pressed(PAD_TRIANGLE) && s_shard_attack)
            Anim_Start(&s_shard_anim, s_shard_attack);
        if (Pad_Pressed(PAD_SQUARE) && s_shard_morph)
            Anim_Start(&s_shard_anim, s_shard_morph);
        Anim_Update(&s_shard_anim, 16);
        if (s_shard_anim.done && s_shard_idle)
            Anim_Start(&s_shard_anim, s_shard_idle);
    }
}

void Demo_Render(RenderContext* rc, Framebuffer* fb) {
    if (!s_level) return;

    // Level fog/light copied every frame so debug toggles can't go stale.
    rc->fog   = s_level->fog;
    rc->light = s_level->light;

    // Lamps breathe (+/-10%), each on its own phase, so the room isn't static.
    for (u32 i = 0; i < rc->light.npoints; i++) {
        const LevelPoint* src = &s_level->light.points[i];
        i32 ph = (i32)((s_tick * 17u + i * 937u) & (ANGLE_FULL - 1));
        i32 k  = 3686 + ((Csin(ph) * 410) >> 12);        // ~0.90 .. 1.10 (4.12)
        LevelPoint* p = &rc->light.points[i];
        p->r = (u8)ClampI32(((i32)src->r * k) >> 12, 0, 255);
        p->g = (u8)ClampI32(((i32)src->g * k) >> 12, 0, 255);
        p->b = (u8)ClampI32(((i32)src->b * k) >> 12, 0, 255);
    }
    // Transient impact/special flashes take the remaining slots.
    Fx_LightsApply(rc);

    Camera scene_cam;
    scene_cam.pos    = s_pos;
    scene_cam.rot    = { (i16)s_pitch, (i16)s_yaw, 0 };
    scene_cam.near_z = 40;                // ~16 cm
    scene_cam.far_z  = 20 * WORLD_SCALE;  // 20 m

    // The staged scene directs its own camera; the status screen overrides the
    // scene camera with its portrait framing.
    if (Dialog_Active())            scene_cam = *Dialog_Cam();
    else if (Combat_InspectActive()) scene_cam = *Combat_InspectCam();
    else if (s_ev_tier) {
        // Pull back far enough that the biggest tier fits; smaller tiers then
        // read at their true relative size.
        scene_cam.pos = { 0, (i32)(-5.5 * 256), (i32)(-16.0 * 256) };
        scene_cam.rot = { (i16)-360, 0, 0 };       // look down ~32 deg
        scene_cam.near_z = 40;
        scene_cam.far_z = 40 * WORLD_SCALE;
    } else if (s_morph_on && !s_combatcam) {
        scene_cam.pos = { 0, (i32)(-2.0 * 256), (i32)(-4.2 * 256) };
        scene_cam.rot = { (i16)-360, 0, 0 };       // back far enough for the snake
        scene_cam.near_z = 20;
        scene_cam.far_z = 28 * WORLD_SCALE;
    } else if (s_show_rig[0] && !s_combatcam) {
        scene_cam.pos = { 0, (i32)(-1.7 * 256), (i32)(-2.9 * 256) };
        scene_cam.rot = { (i16)-430, 0, 0 };       // back + high, ~38 deg down
        scene_cam.near_z = 20;
        scene_cam.far_z = 24 * WORLD_SCALE;
    }

    // Seed the free cam while inactive so toggling starts from the current view.
    if (!Debug_FreeCamActive()) Debug_SyncFreeCam(&scene_cam);
    if (!Debug_FreeCamActive()) {              // combat screen shake
        LVec sh = Fx_CamOffset();
        scene_cam.pos.vx += sh.vx;
        scene_cam.pos.vy += sh.vy;
    }
    const Camera* cam = Debug_FreeCamActive() ? Debug_FreeCam() : &scene_cam;

    Rc_Begin(rc, cam);
    // Water animation: slow diagonal drift + sine wobble (texels; wraps).
    rc->uvscroll_u = (i32)((s_tick * 20u) >> 6);
    rc->uvscroll_v = (i32)((s_tick * 9u) >> 6)
                   + ((Csin((i32)((s_tick * 24u) & 4095u)) * 5) >> 12);
    Fb_Clear(fb, s_level->clear_r, s_level->clear_g, s_level->clear_b);
    if (g_config.zbuffer) Fb_ClearZ();

    // Creature viewer: hide the arena so the wireframe/silhouette is clean --
    // unless --combat-cam, which shows the creature in the arena.
    bool hide_arena = (s_show_rig[0] || s_morph_on) && !s_combatcam;
    for (u32 i = 0; i < s_level->nobjects && !hide_arena; i++) {
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

    if (s_morph_on) {
        const Mesh* fm = s_morph[MorphFrame()];
        if (fm) {
            Mat m;
            SVec rot = { 0, (i16)1024, 0 };   // broadside: see the S-wave/legs
            Gte_RotMatrix(&rot, &m);
            m.t[0] = 0;
            if (s_combatcam) {                // in the arena, under the combat cam
                m.t[1] = 0;
                m.t[2] = (i32)(0.5 * 256);
            } else {
                rc->light.npoints = 0;        // void viewer: clean black
                m.t[1] = 0;
                m.t[2] = (i32)(0.3 * 256);
            }
            Rc_DrawMesh(rc, fm, &m);
        }
    } else if (s_ev_tier && s_ev_rig) {
        Mat m;
        SVec rot = { 0, (i16)((s_tick * 9) & (ANGLE_FULL - 1)), 0 }; // slow spin
        Gte_RotMatrix(&rot, &m);
        i32 sc = Evolver_TierScale(s_ev_tier);
        Gte_ScaleMatrix(&m, sc, sc, sc);
        m.t[0] = 0;
        m.t[1] = (i32)(1.6 * 256);      // drop the hover so it rests in frame
        m.t[2] = (i32)(1.5 * 256);
        Anim_Draw(rc, s_ev_rig, &s_ev_anim, &m);
    } else if (s_show_rig[0] && s_show_r) {
        Mat m;
        // Frozen 3/4 view facing the camera (head toward us); TAB to orbit.
        SVec rot = { 0, (i16)1900, 0 };
        Gte_RotMatrix(&rot, &m);
        m.t[0] = 0;
        m.t[1] = 0;
        m.t[2] = (i32)(0.2 * 256);
        // Clean viewer lighting: drop the arena's coloured point lights (they
        // muddy the black facets around the eye). A single red point light AT
        // the eye, set BEFORE drawing, reddens the head geometrically.
        LVec eyep;
        if (Anim_BoneWorld(s_show_r, &s_show_anim, &m, "eye", &eyep)) {
            rc->light.npoints = 1;
            LevelPoint* p = &rc->light.points[0];
            p->r = 255; p->g = 30; p->b = 22;
            p->pos = eyep;
            p->radius = (i32)(0.75 * 256);
        } else {
            rc->light.npoints = 0;
        }
        Anim_Draw(rc, s_show_r, &s_show_anim, &m);
    } else if (Dialog_Active()) {
        Dialog_Render(rc);
    } else if (Combat_Active()) {
        Combat_Render(rc);
    } else if (s_robot_rig) {
        Mat rm;
        Gte_RotMatrix(&s_robot_rot, &rm);
        rm.t[0] = s_robot_pos.vx;
        rm.t[1] = s_robot_pos.vy;
        rm.t[2] = s_robot_pos.vz;
        Anim_Draw(rc, s_robot_rig, &s_robot_anim, &rm);
    }
    if (!Combat_Active() && !Dialog_Active() && s_shard_rig) {
        Mat sm;
        Gte_RotMatrix(&s_shard_rot, &sm);
        sm.t[0] = s_shard_pos.vx;
        sm.t[1] = s_shard_pos.vy;
        sm.t[2] = s_shard_pos.vz;
        Anim_Draw(rc, s_shard_rig, &s_shard_anim, &sm);
    }

    Fx_AmbientRender(rc);
    Fx_Render(rc);

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
    if (Dialog_Active()) { Dialog_DrawUI(fb); return; }
    if (Combat_Active()) { Combat_DrawUI(fb); return; }

    const char* title = "PSX-AUTHENTIC ENGINE";
    const char* help  = "[SPACE] BLIP  [TAB] FREECAM  [F1] DEBUG  [WASD] MOVE";
    int tx = (fb->w - (int)strlen(title) * 8) / 2;
    int hx = (fb->w - (int)strlen(help) * 8) / 2;
    if (tx < 2) tx = 2;
    if (hx < 2) hx = 2;
    Debug_Text(fb, tx, 6, 255, 255, 255, "%s", title);
    Debug_Text(fb, hx, fb->h - 12, 180, 180, 180, "%s", help);
}
