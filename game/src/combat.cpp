// Turn-based combat slice. See combat.h.
#include "game/src/combat.h"

#include <cstdio>
#include <cstring>

#include "engine/anim/anim.h"
#include "engine/assets/assets.h"
#include "engine/debug/debug.h"
#include "engine/input/input.h"
#include "engine/math/fixed.h"
#include "engine/platform/platform.h"
#include "game/src/fx.h"

// ------------------------------------------------------------------ actors

struct Fighter {
    const char*     name;
    const Rig*      rig;
    AnimState       anim;
    const AnimClip* idle;
    const AnimClip* attack;
    const AnimClip* defend;   // may be null
    const AnimClip* hit;
    LVec            pos;
    SVec            rot;
    int             hp, max_hp;
    bool            defending;
    i32             sink;     // death sink offset, engine units (grows down)
};

enum Phase : u8 {
    PH_OFF = 0, PH_INTRO, PH_MENU, PH_PLAYER_ACT, PH_RESOLVE_P,
    PH_ENEMY_WAIT, PH_ENEMY_ACT, PH_RESOLVE_E, PH_VICTORY, PH_DEFEAT,
};

struct FloatNum {
    int  val;
    int  ticks;    // remaining; 0 = free slot
    int  x, y;     // internal-resolution pixels
    bool halved;
};

static bool     s_active = false;
static bool     s_auto = false;        // test hook: auto-pick ATTACK each turn
static Phase    s_phase = PH_OFF;
static i32      s_timer = 0;
static int      s_cursor = 0;          // 0 = ATTACK, 1 = DEFEND
static u32      s_seed = 0x1234567;
static Fighter  s_player, s_enemy;
static FloatNum s_floats[4];

// Screen anchors for floating damage (internal 320x180, camera is fixed
// in combat_stage.json — see level file).
static const int kPlayerAnchorX = 86,  kPlayerAnchorY = 108;
static const int kEnemyAnchorX  = 214, kEnemyAnchorY  = 104;

void Combat_SetActive(bool on) { s_active = on; }
void Combat_SetAuto(bool on) { s_auto = on; }
bool Combat_Active() { return s_active; }

static u32 Rnd() { s_seed = s_seed * 1103515245u + 12345u; return s_seed >> 16; }

static void StartClip(Fighter* f, const AnimClip* c) { Anim_Start(&f->anim, c); }

static void AddFloat(int val, int x, int y, bool halved) {
    for (int i = 0; i < 4; i++) {
        if (s_floats[i].ticks > 0) continue;
        s_floats[i] = { val, 75, x, y, halved };
        return;
    }
}

void Combat_Init() {
    if (!s_active) return;
    Debug_AllowFreeCam(false);   // TAB belongs to the combat menu
    s_player = {};
    s_player.name    = "UNIT-7";
    s_player.rig     = Rig_Find("robot");
    s_player.idle    = Anim_Find("robot_idle");
    s_player.attack  = Anim_Find("robot_attack");
    s_player.defend  = Anim_Find("robot_defend");
    s_player.hit     = Anim_Find("robot_hit");
    s_player.pos     = { (i32)(-2.3 * 256), 0, (i32)(0.5 * 256) };
    s_player.rot     = { 0, (i16)1024, 0 };          // face +x (the enemy)
    s_player.max_hp  = s_player.hp = 80;

    s_enemy = {};
    s_enemy.name     = "SHARD";
    s_enemy.rig      = Rig_Find("shard");
    s_enemy.idle     = Anim_Find("shard_idle");
    s_enemy.attack   = Anim_Find("shard_attack");
    s_enemy.defend   = nullptr;
    s_enemy.hit      = Anim_Find("shard_hit");
    s_enemy.pos      = { (i32)(2.3 * 256), 0, (i32)(0.5 * 256) };
    s_enemy.rot      = { 0, (i16)3072, 0 };          // face -x (the player)
    s_enemy.max_hp   = s_enemy.hp = 70;

    StartClip(&s_player, s_player.idle);
    StartClip(&s_enemy, s_enemy.idle);
    Fx_Init();
    memset(s_floats, 0, sizeof(s_floats));
    s_phase = PH_INTRO;
    s_timer = 100;
    s_cursor = 0;

    if (!s_player.rig || !s_enemy.rig)
        fprintf(stderr, "combat: missing rigs (build local assets first)\n");
}

// --------------------------------------------------------------- turn logic

static void DealDamage(Fighter* from, Fighter* to, int base, int spread,
                       int anchor_x, int anchor_y) {
    (void)from;
    int dmg = base + (int)(Rnd() % (u32)spread);
    bool halved = to->defending;
    if (halved) { dmg = (dmg + 1) / 2; to->defending = false; }
    to->hp -= dmg;
    if (to->hp < 0) to->hp = 0;
    if (to->hit) StartClip(to, to->hit);
    AddFloat(dmg, anchor_x, anchor_y, halved);

    LVec chest = to->pos;
    chest.vy -= 280;                          // ~1.1 m up (y down)
    if (halved)
        Fx_Burst(chest, 12, 11, 150, 215, 255); // blue block sparks
    else
        Fx_Burst(chest, 12, 14, 255, 210, 130); // impact sparks
    Fx_Shake(14 + dmg, 12);
}

void Combat_Update() {
    if (!s_active || s_phase == PH_OFF) return;

    Anim_Update(&s_player.anim, 16);
    Anim_Update(&s_enemy.anim, 16);
    for (int i = 0; i < 4; i++)
        if (s_floats[i].ticks > 0) { s_floats[i].ticks--; s_floats[i].y--; }

    switch (s_phase) {
    case PH_INTRO:
        if (--s_timer <= 0) s_phase = PH_MENU;
        break;

    case PH_MENU:
        if (Pad_Pressed(PAD_UP) || Pad_Pressed(PAD_DOWN) ||
            Plat_KeyPressed(PK_TAB))
            s_cursor ^= 1;
        if (s_auto) s_cursor = 0;
        if (s_auto || Pad_Pressed(PAD_CROSS) || Pad_Pressed(PAD_START)) {
            if (s_cursor == 0) {
                StartClip(&s_player, s_player.attack);
            } else {
                s_player.defending = true;
                StartClip(&s_player, s_player.defend);
            }
            s_phase = PH_PLAYER_ACT;
        }
        break;

    case PH_PLAYER_ACT:
        if (s_player.anim.done) {
            StartClip(&s_player, s_player.idle);
            if (s_cursor == 0) {
                DealDamage(&s_player, &s_enemy, 14, 9,
                           kEnemyAnchorX, kEnemyAnchorY);
                s_phase = PH_RESOLVE_P;
            } else {
                s_phase = PH_ENEMY_WAIT;
                s_timer = 50;
            }
        }
        break;

    case PH_RESOLVE_P:
        if (s_enemy.anim.done || s_enemy.anim.clip == s_enemy.idle) {
            if (s_enemy.anim.done) StartClip(&s_enemy, s_enemy.idle);
            if (s_enemy.hp <= 0) {
                s_phase = PH_VICTORY;
            } else {
                s_phase = PH_ENEMY_WAIT;
                s_timer = 50;
            }
        }
        break;

    case PH_ENEMY_WAIT:
        if (--s_timer <= 0) {
            StartClip(&s_enemy, s_enemy.attack);
            s_phase = PH_ENEMY_ACT;
        }
        break;

    case PH_ENEMY_ACT:
        if (s_enemy.anim.done) {
            StartClip(&s_enemy, s_enemy.idle);
            DealDamage(&s_enemy, &s_player, 10, 8,
                       kPlayerAnchorX, kPlayerAnchorY);
            s_phase = PH_RESOLVE_E;
        }
        break;

    case PH_RESOLVE_E:
        if (s_player.anim.done || s_player.anim.clip == s_player.idle) {
            if (s_player.anim.done) StartClip(&s_player, s_player.idle);
            s_phase = (s_player.hp <= 0) ? PH_DEFEAT : PH_MENU;
        }
        break;

    case PH_VICTORY:
    case PH_DEFEAT: {
        Fighter* loser = (s_phase == PH_VICTORY) ? &s_enemy : &s_player;
        if (loser->sink < 300) {
            if (loser->sink == 0) {              // breaking the surface
                Fx_Splash(loser->pos, 20);
                Fx_Shake(26, 16);
            } else if (loser->sink % 15 == 0) {  // churn while going under
                Fx_Splash(loser->pos, 9);
            }
            loser->sink += 3;
        }
        if (Pad_Pressed(PAD_CROSS) || Pad_Pressed(PAD_START)) Combat_Init();
        break;
    }
    default:
        break;
    }
}

// ------------------------------------------------------------------ render

static void DrawFighter(RenderContext* rc, Fighter* f) {
    if (!f->rig || f->sink >= 300) return;
    Mat m;
    Gte_RotMatrix(&f->rot, &m);
    m.t[0] = f->pos.vx;
    m.t[1] = f->pos.vy + f->sink;   // y grows down
    m.t[2] = f->pos.vz;
    Anim_Draw(rc, f->rig, &f->anim, &m);
}

void Combat_Render(RenderContext* rc) {
    if (!s_active || s_phase == PH_OFF) return;
    DrawFighter(rc, &s_player);
    DrawFighter(rc, &s_enemy);
}

// --------------------------------------------------------------------- HUD

static void FillRect(Framebuffer* fb, int x, int y, int w, int h,
                     u8 r, u8 g, u8 b) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= fb->h) continue;
        for (int i = x; i < x + w; i++) {
            if (i < 0 || i >= fb->w) continue;
            Fb_Put(fb, i, j, r, g, b);
        }
    }
}

static void DrawHpBar(Framebuffer* fb, int x, int y, const Fighter* f) {
    const int W = 96, H = 7;
    Debug_Text(fb, x, y, 220, 235, 235, "%s", f->name);
    int by = y + 10;
    FillRect(fb, x - 1, by - 1, W + 2, H + 2, 15, 25, 25);       // backdrop
    FillRect(fb, x, by, W, H, 40, 52, 52);                        // empty
    int fill = (f->hp * W) / f->max_hp;
    // teal when healthy, amber under half, red under quarter
    u8 r = 70, g = 220, b = 180;
    if (f->hp * 4 <= f->max_hp)      { r = 230; g = 60;  b = 50; }
    else if (f->hp * 2 <= f->max_hp) { r = 230; g = 180; b = 60; }
    if (fill > 0) FillRect(fb, x, by, fill, H, r, g, b);
    Debug_Text(fb, x + W + 5, by - 1, 180, 200, 200, "%d", f->hp);
}

static void DrawCentered(Framebuffer* fb, int y, u8 r, u8 g, u8 b,
                         const char* s) {
    int x = (fb->w - (int)strlen(s) * 8) / 2;
    Debug_Text(fb, x, y, r, g, b, "%s", s);
}

void Combat_DrawUI(Framebuffer* fb) {
    if (!s_active || s_phase == PH_OFF) return;

    DrawHpBar(fb, 8, 6, &s_player);
    DrawHpBar(fb, 216 - 8, 6, &s_enemy);

    if (s_phase == PH_INTRO)
        DrawCentered(fb, 80, 120, 235, 255, "RIFT BREACH // ENGAGE");

    if (s_phase == PH_MENU) {
        FillRect(fb, 6, 146, 76, 28, 10, 20, 22);
        Debug_Text(fb, 12, 150, s_cursor == 0 ? 255 : 140,
                   s_cursor == 0 ? 240 : 160, 120, "%cATTACK",
                   s_cursor == 0 ? '>' : ' ');
        Debug_Text(fb, 12, 161, s_cursor == 1 ? 255 : 140,
                   s_cursor == 1 ? 240 : 160, 120, "%cDEFEND",
                   s_cursor == 1 ? '>' : ' ');
    }

    if (s_phase == PH_ENEMY_WAIT)
        DrawCentered(fb, 80, 255, 150, 130, "SHARD ATTACKS");
    if (s_phase == PH_VICTORY) {
        DrawCentered(fb, 74, 140, 255, 190, "RIFT SEALED");
        DrawCentered(fb, 96, 160, 180, 180, "[SPACE] RE-ENGAGE");
    }
    if (s_phase == PH_DEFEAT) {
        DrawCentered(fb, 74, 255, 90, 80, "UNIT-7 DOWN");
        DrawCentered(fb, 96, 160, 180, 180, "[SPACE] RETRY");
    }

    for (int i = 0; i < 4; i++) {
        const FloatNum* fn = &s_floats[i];
        if (fn->ticks <= 0) continue;
        u8 fade = fn->ticks < 20 ? (u8)(80 + fn->ticks * 8) : 240;
        if (fn->halved)
            Debug_Text(fb, fn->x - 16, fn->y, (u8)(fade / 2), (u8)(fade * 4 / 5),
                       255, "%d BLOCKED", fn->val);
        else
            Debug_Text(fb, fn->x, fn->y, 255, fade, (u8)(fade / 3),
                       "%d", fn->val);
    }
}
