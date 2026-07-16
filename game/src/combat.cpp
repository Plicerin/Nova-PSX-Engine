// Turn-based combat slice. See combat.h.
#include "game/src/combat.h"

#include <cstdio>
#include <cstdlib>
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
    int             charge;    // 0..kChargeMax; only the player builds it
    bool            defending;
    i32             sink;      // death sink offset, engine units (grows down)
    i32             bob;       // transient "acting" hop, engine units (up)
    i32             scale;     // fx12 draw scale (4096 = 1.0); evolving enemy grows
};

enum Phase : u8 {
    PH_OFF = 0, PH_INTRO, PH_MENU, PH_PLAYER_ACT, PH_RESOLVE_P,
    PH_COMPANION_ACT, PH_RESOLVE_C,
    PH_ENEMY_WAIT, PH_ENEMY_ACT, PH_RESOLVE_E, PH_VICTORY, PH_DEFEAT,
};

// Menu actions.
enum Action : int { ACT_ATTACK = 0, ACT_DEFEND = 1, ACT_SPECIAL = 2, ACT_COUNT };

struct FloatNum {
    int  val;
    int  ticks;    // remaining; 0 = free slot
    int  x, y;     // internal-resolution pixels
    u8   kind;     // 0 dmg, 1 blocked, 2 heal, 3 crit
};

static const int kChargeMax   = 100;   // full meter enables SPECIAL
static const int kSpecialCost = 100;

static bool     s_active = false;
static bool     s_auto = false;        // test hook: auto-pick ATTACK each turn
static Phase    s_phase = PH_OFF;
static i32      s_timer = 0;
static int      s_cursor = ACT_ATTACK;
static u32      s_seed = 0x1234567;
static Fighter  s_player, s_enemy, s_companion;
static FloatNum s_floats[6];
static bool     s_inspect = false;     // status screen (battle frozen)
static i32      s_inspect_yaw = 0;     // turntable angle, PS1 units
static Camera   s_inspect_cam;

// Enemy telegraph: the creature spends one turn folding up ("winding"), then
// unleashes a heavy strike the following turn -- the window in which DEFEND
// actually pays off.
static bool     s_enemy_wind = false;  // charged, heavy strike is imminent
static bool     s_enemy_heavy = false; // this turn's strike is the heavy one
static int      s_pending;             // action locked in from the menu
static int      s_comp_cd = 0;         // companion repair cooldown, turns
static bool     s_comp_heal = false;   // this companion turn is a repair

// Screen anchors for floating damage (internal 320x180, camera is fixed
// in combat_stage.json -- see level file).
static const int kPlayerAnchorX = 86,  kPlayerAnchorY = 108;
static const int kEnemyAnchorX  = 214, kEnemyAnchorY  = 104;

void Combat_SetActive(bool on) { s_active = on; }
void Combat_SetAuto(bool on) { s_auto = on; }
bool Combat_Active() { return s_active; }

// --- evolving enemy tiers ---------------------------------------------------
// Draw scale grows so tier 5 reads ~10x tier 1 (footprint also grows with the
// added rings). Names track the creature's escalating menace.
static const i32   kTierScale[6] = { 0, 4096, 6349, 9830, 14336, 20480 };
static const char* kTierName[6]  = { "", "SLIVER", "FACET", "PRISM",
                                     "LATTICE", "RIFT-BLOOM" };
static int ClampTier(int t) { return t < 1 ? 1 : (t > 5 ? 5 : t); }
int Evolver_TierForBattle(int won) { return ClampTier(1 + won); }
i32 Evolver_TierScale(int tier) { return kTierScale[ClampTier(tier)]; }
const char* Evolver_TierName(int tier) { return kTierName[ClampTier(tier)]; }
const char* Evolver_RigName(int tier) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "evolver_t%d", ClampTier(tier));
    return buf;
}

static u32 Rnd() { s_seed = s_seed * 1103515245u + 12345u; return s_seed >> 16; }

static void StartClip(Fighter* f, const AnimClip* c) { Anim_Start(&f->anim, c); }

static void AddCharge(Fighter* f, int amt) {
    f->charge += amt;
    if (f->charge > kChargeMax) f->charge = kChargeMax;
}

static void AddFloat(int val, int x, int y, u8 kind) {
    for (int i = 0; i < (int)(sizeof(s_floats) / sizeof(s_floats[0])); i++) {
        if (s_floats[i].ticks > 0) continue;
        s_floats[i] = { val, 75, x, y, kind };
        return;
    }
}

void Combat_Init() {
    if (!s_active) return;
    Debug_AllowFreeCam(false);   // TAB belongs to the combat menu
    s_player = {};
    s_player.name    = "ASTRA";
    s_player.rig     = Rig_Find("astro");
    s_player.idle    = Anim_Find("astro_idle");
    s_player.attack  = Anim_Find("astro_attack");
    s_player.defend  = Anim_Find("astro_defend");
    s_player.hit     = Anim_Find("astro_hit");
    s_player.pos     = { (i32)(-2.3 * 256), 0, (i32)(0.5 * 256) };
    // Verified on screen: astro's forward is +z and engine yaw is clockwise
    // from above, so +x (the enemy) is yaw 1024. unit7 came from a different
    // GLB and is authored facing -z, so its +x is 3072 -- do NOT assume the
    // two rigs share a forward axis.
    s_player.rot     = { 0, (i16)1024, 0 };          // face +x (the enemy)
    s_player.max_hp  = s_player.hp = 80;
    s_player.charge  = 0;

    // Robot companion: fights alongside ASTRA on her side of the arena.
    s_companion = {};
    s_companion.name = "UNIT-7";
    s_companion.rig  = Rig_Find("unit7");
    s_companion.idle = Anim_Find("unit7_idle");
    s_companion.pos  = { (i32)(-3.6 * 256), 0, (i32)(1.8 * 256) };
    s_companion.rot  = { 0, (i16)3213, 0 };   // angled at the enemy from behind-left
    StartClip(&s_companion, s_companion.idle);

    // Enemy roster: pick one of the fold-creatures each battle.
    static u32 s_roster = 1;   // first battle shows PRISM, then alternates
    s_roster++;
    s_enemy = {};
    if (s_roster & 1) {
        s_enemy.name   = "SHARD";
        s_enemy.rig    = Rig_Find("shard");
        s_enemy.idle   = Anim_Find("shard_idle");
        s_enemy.attack = Anim_Find("shard_attack");
        s_enemy.hit    = Anim_Find("shard_hit");
        s_enemy.max_hp = s_enemy.hp = 70;
    } else {
        s_enemy.name   = "PRISM";
        s_enemy.rig    = Rig_Find("prism");
        s_enemy.idle   = Anim_Find("prism_idle");
        s_enemy.attack = Anim_Find("prism_attack");
        s_enemy.hit    = Anim_Find("prism_hit");
        s_enemy.max_hp = s_enemy.hp = 64;
    }
    s_enemy.defend   = nullptr;
    s_enemy.pos      = { (i32)(2.3 * 256), 0, (i32)(0.5 * 256) };
    s_enemy.rot      = { 0, (i16)3072, 0 };          // prism authored +z; -x is 3072

    StartClip(&s_player, s_player.idle);
    StartClip(&s_enemy, s_enemy.idle);
    Fx_Init();
    memset(s_floats, 0, sizeof(s_floats));
    s_enemy_wind = s_enemy_heavy = false;
    s_comp_cd = 0;
    s_phase = PH_INTRO;
    s_timer = 100;
    s_cursor = ACT_ATTACK;

    if (!s_player.rig || !s_enemy.rig)
        fprintf(stderr, "combat: missing rigs (build local assets first)\n");
}

// --------------------------------------------------------------- turn logic

// kind: 0 impact, 3 crit (heavy/special) -> bigger, hotter FX.
static void DealDamage(Fighter* to, int base, int spread, u8 kind) {
    int dmg = base + (int)(Rnd() % (u32)spread);
    bool blocked = to->defending;
    if (blocked) { dmg = (dmg + 1) / 2; to->defending = false; }
    to->hp -= dmg;
    if (to->hp < 0) to->hp = 0;
    if (to->hit) StartClip(to, to->hit);
    // Taking a hit stokes the player's meter a little (rage).
    if (to == &s_player) AddCharge(&s_player, 10);

    int ax = (to == &s_enemy) ? kEnemyAnchorX : kPlayerAnchorX;
    int ay = (to == &s_enemy) ? kEnemyAnchorY : kPlayerAnchorY;
    AddFloat(dmg, ax, ay, blocked ? 1 : kind);

    LVec chest = to->pos;
    chest.vy -= 280;                          // ~1.1 m up (y down)
    if (blocked) {
        Fx_Burst(chest, 12, 11, 150, 215, 255);          // blue block sparks
        Fx_AddLight(chest, 90, 180, 255, 3 * 256, 14);   // cold block flash
    } else if (kind == 3) {
        Fx_Burst(chest, 22, 20, 255, 240, 170);          // white-hot crit
        Fx_AddLight(chest, 255, 235, 170, 6 * 256, 22);  // big hot flash
    } else {
        Fx_Burst(chest, 12, 14, 255, 210, 130);          // impact sparks
        Fx_AddLight(chest, 255, 170, 90, 4 * 256, 14);   // warm impact flash
    }
    Fx_Shake((kind == 3 ? 24 : 14) + dmg, kind == 3 ? 16 : 12);
}

bool Combat_InspectActive() { return s_active && s_inspect; }

void Combat_ToggleInspect() {
    s_inspect = !s_inspect;
    if (s_inspect) s_inspect_yaw = 0;
    else s_player.rot = { 0, (i16)1024, 0 };
}

const Camera* Combat_InspectCam() {
    // In front of the player at chest height, looking straight at them.
    s_inspect_cam.pos = { s_player.pos.vx, -235,
                          s_player.pos.vz - (i32)(3.4 * 256) };
    s_inspect_cam.rot = { 0, 0, 0 };
    s_inspect_cam.near_z = 40;
    s_inspect_cam.far_z = 20 * 256;
    return &s_inspect_cam;
}

// True once the enemy's reaction to a player/companion hit has settled.
static bool EnemySettled() {
    return s_enemy.anim.done || s_enemy.anim.clip == s_enemy.idle;
}

void Combat_Update() {
    if (!s_active || s_phase == PH_OFF) return;

    // Status screen: freeze the battle, spin the character.
    if (s_inspect) {
        s_inspect_yaw = (s_inspect_yaw + 14) & (ANGLE_FULL - 1);
        s_player.rot.vy = (i16)((2048 + s_inspect_yaw) & (ANGLE_FULL - 1));
        Anim_Update(&s_player.anim, 16);
        if (Plat_KeyPressed(PK_I) || Pad_Pressed(PAD_CIRCLE)) {
            s_inspect = false;
            s_player.rot = { 0, (i16)1024, 0 };   // back to facing the enemy
        }
        return;
    }
    if (s_phase == PH_MENU && Plat_KeyPressed(PK_I)) {
        s_inspect = true;
        s_inspect_yaw = 0;
        return;
    }

    Anim_Update(&s_player.anim, 16);
    Anim_Update(&s_enemy.anim, 16);
    Anim_Update(&s_companion.anim, 16);
    for (int i = 0; i < (int)(sizeof(s_floats) / sizeof(s_floats[0])); i++)
        if (s_floats[i].ticks > 0) { s_floats[i].ticks--; s_floats[i].y--; }

    switch (s_phase) {
    case PH_INTRO:
        if (--s_timer <= 0) s_phase = PH_MENU;
        break;

    case PH_MENU: {
        bool special_ok = s_player.charge >= kSpecialCost;
        if (Pad_Pressed(PAD_UP))
            s_cursor = (s_cursor + ACT_COUNT - 1) % ACT_COUNT;
        if (Pad_Pressed(PAD_DOWN) || Plat_KeyPressed(PK_TAB))
            s_cursor = (s_cursor + 1) % ACT_COUNT;
        if (s_auto) s_cursor = ACT_ATTACK;
        // Triangle/E fires SPECIAL directly when it's ready.
        bool confirm = s_auto || Pad_Pressed(PAD_CROSS) || Pad_Pressed(PAD_START);
        if (Pad_Pressed(PAD_TRIANGLE) && special_ok) { s_cursor = ACT_SPECIAL; confirm = true; }
        if (confirm) {
            if (s_cursor == ACT_SPECIAL && !special_ok) break;  // locked
            s_pending = s_cursor;
            if (s_pending == ACT_DEFEND) {
                s_player.defending = true;
                AddCharge(&s_player, 20);
                if (s_player.defend) StartClip(&s_player, s_player.defend);
            } else {
                StartClip(&s_player, s_player.attack);
                if (s_pending == ACT_SPECIAL) s_player.charge -= kSpecialCost;
            }
            s_phase = PH_PLAYER_ACT;
        }
        break;
    }

    case PH_PLAYER_ACT:
        if (s_player.anim.done) {
            StartClip(&s_player, s_player.idle);
            if (s_pending == ACT_ATTACK) {
                AddCharge(&s_player, 30);
                DealDamage(&s_enemy, 14, 9, 0);
                s_phase = PH_RESOLVE_P;
            } else if (s_pending == ACT_SPECIAL) {
                // OVERLOAD: ASTRA's morph-strike. Big, hot, lights the room.
                LVec c = s_enemy.pos; c.vy -= 200;
                Fx_Burst(c, 16, 26, 180, 245, 255);
                Fx_AddLight(c, 140, 240, 255, 9 * 256, 26);   // cyan blast light
                LVec self = s_player.pos; self.vy -= 260;
                Fx_AddLight(self, 120, 220, 255, 5 * 256, 18); // muzzle-side glow
                DealDamage(&s_enemy, 30, 16, 3);
                s_phase = PH_RESOLVE_P;
            } else {  // DEFEND: no enemy reaction, straight to the ally turn
                s_phase = PH_COMPANION_ACT;
                s_timer = 40;
                s_comp_cd = s_comp_cd > 0 ? s_comp_cd - 1 : 0;
            }
        }
        break;

    case PH_RESOLVE_P:
        if (EnemySettled()) {
            if (s_enemy.anim.done) StartClip(&s_enemy, s_enemy.idle);
            if (s_enemy.hp <= 0) {
                s_phase = PH_VICTORY;
            } else {
                s_phase = PH_COMPANION_ACT;
                s_timer = 40;
                s_comp_cd = s_comp_cd > 0 ? s_comp_cd - 1 : 0;
            }
        }
        break;

    case PH_COMPANION_ACT: {
        // Decide the ally's move once, at the top of its turn.
        if (s_timer == 40) {
            s_comp_heal = (s_player.hp * 100 <= s_player.max_hp * 45) &&
                          s_comp_cd == 0 && s_player.hp > 0;
        }
        // Hop up and back down over the turn -- reads as "acting".
        int t = 40 - s_timer;                 // 0..40
        int up = t < 20 ? t : 40 - t;         // 0..20..0
        s_companion.bob = up * 6;
        if (s_timer == 20) {                  // apex: apply the effect
            if (s_comp_heal) {
                int heal = 12 + (int)(Rnd() % 6);
                s_player.hp += heal;
                if (s_player.hp > s_player.max_hp) s_player.hp = s_player.max_hp;
                AddFloat(heal, kPlayerAnchorX, kPlayerAnchorY, 2);
                LVec c = s_player.pos; c.vy -= 260;
                Fx_Burst(c, 14, 12, 120, 255, 190);       // green repair motes
                Fx_AddLight(c, 90, 255, 165, 4 * 256, 20);
                s_comp_cd = 3;
            } else {
                LVec c = s_enemy.pos; c.vy -= 240;
                Fx_Burst(c, 10, 15, 255, 190, 120);       // arc-zap
                Fx_AddLight(c, 255, 180, 100, 3 * 256, 12);
                DealDamage(&s_enemy, 7, 5, 0);
            }
        }
        if (--s_timer <= 0) { s_companion.bob = 0; s_phase = PH_RESOLVE_C; }
        break;
    }

    case PH_RESOLVE_C:
        if (EnemySettled()) {
            if (s_enemy.anim.done) StartClip(&s_enemy, s_enemy.idle);
            if (s_enemy.hp <= 0) {
                s_phase = PH_VICTORY;
            } else {
                s_phase = PH_ENEMY_WAIT;
                s_timer = 45;
            }
        }
        break;

    case PH_ENEMY_WAIT:
        if (--s_timer <= 0) {
            if (s_enemy_wind) {
                // The fold releases: this strike is the heavy one.
                s_enemy_wind = false;
                s_enemy_heavy = true;
                StartClip(&s_enemy, s_enemy.attack);
                s_phase = PH_ENEMY_ACT;
            } else if ((Rnd() % 100) < 42 && s_enemy.hp * 2 > s_enemy.max_hp) {
                // Begin folding up -- telegraph a heavy strike for next turn.
                s_enemy_wind = true;
                LVec c = s_enemy.pos; c.vy -= 220;
                Fx_Burst(c, 10, 8, 120, 200, 255);        // cold gather-glow
                Fx_AddLight(c, 110, 190, 255, 5 * 256, 34);   // menacing charge light
                s_phase = PH_RESOLVE_C;                    // skip a real attack
                s_timer = 20;
            } else {
                s_enemy_heavy = false;
                StartClip(&s_enemy, s_enemy.attack);
                s_phase = PH_ENEMY_ACT;
            }
        }
        break;

    case PH_ENEMY_ACT:
        if (s_enemy.anim.done) {
            StartClip(&s_enemy, s_enemy.idle);
            if (s_enemy_heavy) {
                s_enemy_heavy = false;
                DealDamage(&s_player, 20, 10, 3);
            } else {
                DealDamage(&s_player, 10, 8, 0);
            }
            s_phase = PH_RESOLVE_E;
        }
        break;

    case PH_RESOLVE_E:
        if (s_player.anim.done || s_player.anim.clip == s_player.idle) {
            if (s_player.anim.done) StartClip(&s_player, s_player.idle);
            s_phase = (s_player.hp <= 0) ? PH_DEFEAT : PH_MENU;
            s_cursor = ACT_ATTACK;
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
    if (f->scale && f->scale != FX_ONE)
        Gte_ScaleMatrix(&m, f->scale, f->scale, f->scale);
    m.t[0] = f->pos.vx;
    m.t[1] = f->pos.vy + f->sink - f->bob;   // y grows down; bob lifts up
    m.t[2] = f->pos.vz;
    Anim_Draw(rc, f->rig, &f->anim, &m);
}

void Combat_Render(RenderContext* rc) {
    if (!s_active || s_phase == PH_OFF) return;
    if (s_inspect) {                    // status screen: character only
        DrawFighter(rc, &s_player);
        return;
    }
    DrawFighter(rc, &s_player);
    DrawFighter(rc, &s_enemy);
    DrawFighter(rc, &s_companion);
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

static void DrawMenuRow(Framebuffer* fb, int y, int act, const char* label,
                        bool locked) {
    bool sel = s_cursor == act;
    u8 r = sel ? 255 : 140, g = sel ? 240 : 160, b = 120;
    if (locked) { r = 80; g = 90; b = 95; }
    Debug_Text(fb, 12, y, r, g, b, "%c%s", sel ? '>' : ' ', label);
}

void Combat_DrawUI(Framebuffer* fb) {
    if (!s_active || s_phase == PH_OFF) return;

    if (s_inspect) {                            // status screen panel
        FillRect(fb, 6, 6, 116, 86, 8, 18, 20);
        Debug_Text(fb, 12, 12, 150, 240, 235, "ASTRA");
        Debug_Text(fb, 12, 26, 190, 200, 200, "FLIGHT CREW");
        Debug_Text(fb, 12, 42, 190, 200, 200, "HP  %d/%d",
                   s_player.hp, s_player.max_hp);
        Debug_Text(fb, 12, 54, 190, 200, 200, "ATK 14-22");
        Debug_Text(fb, 12, 66, 190, 200, 200, "SPC OVERLOAD 30-45");
        Debug_Text(fb, 12, 78, 190, 200, 200, "CHG %d/%d",
                   s_player.charge, kChargeMax);
        Debug_Text(fb, 12, fb->h - 14, 140, 160, 160, "[I] BACK");
        return;
    }

    DrawHpBar(fb, 8, 6, &s_player);
    DrawHpBar(fb, 216 - 8, 6, &s_enemy);

    if (s_phase == PH_INTRO)
        DrawCentered(fb, 80, 120, 235, 255, "RIFT BREACH // ENGAGE");

    // Charge meter (always visible so the player can watch it fill). Sits
    // below the ASTRA HP row so its label clears the HP bar and number.
    {
        const int cx = 8, cy = 42, W = 96, H = 5;
        bool full = s_player.charge >= kSpecialCost;
        Debug_Text(fb, cx, cy - 9, full ? 120 : 90, full ? 240 : 150,
                   full ? 255 : 200, full ? "CHARGE READY" : "CHARGE");
        FillRect(fb, cx - 1, cy - 1, W + 2, H + 2, 15, 25, 30);
        FillRect(fb, cx, cy, W, H, 30, 40, 52);
        int fill = (s_player.charge * W) / kChargeMax;
        if (fill > 0) FillRect(fb, cx, cy, fill, H,
                               full ? 150 : 70, full ? 245 : 180,
                               full ? 255 : 235);
    }

    if (s_phase == PH_MENU) {
        if (s_enemy_wind)
            DrawCentered(fb, 62, 255, 120, 110, "! ENEMY CHARGING - BRACE");
        // Bottom-left: the fighters own the middle of the top-down 3/4 frame.
        Debug_Text(fb, 8, 126, 120, 140, 140, "[I] STATUS");
        FillRect(fb, 6, 136, 132, 40, 10, 20, 22);
        bool special_ok = s_player.charge >= kSpecialCost;
        DrawMenuRow(fb, 140, ACT_ATTACK,  "ATTACK", false);
        DrawMenuRow(fb, 151, ACT_DEFEND,  "DEFEND", false);
        DrawMenuRow(fb, 162, ACT_SPECIAL,
                    special_ok ? "OVERLOAD" : "OVERLOAD [LOCK]", !special_ok);
    }

    if (s_phase == PH_COMPANION_ACT)
        DrawCentered(fb, 80, 150, 235, 255,
                     s_comp_heal ? "UNIT-7 REPAIRS" : "UNIT-7 SUPPORTS");
    if ((s_phase == PH_ENEMY_WAIT || s_phase == PH_ENEMY_ACT) && s_enemy.name)
        DrawCentered(fb, 80, 255, 150, 130,
                     s_enemy_heavy ? "!! HEAVY STRIKE" :
                     s_enemy_wind  ? "ENEMY FOLDS UP" : s_enemy.name);
    if (s_phase == PH_VICTORY) {
        DrawCentered(fb, 74, 140, 255, 190, "RIFT SEALED");
        DrawCentered(fb, 96, 160, 180, 180, "[SPACE] RE-ENGAGE");
    }
    if (s_phase == PH_DEFEAT) {
        DrawCentered(fb, 74, 255, 90, 80, "ASTRA DOWN");
        DrawCentered(fb, 96, 160, 180, 180, "[SPACE] RETRY");
    }

    for (int i = 0; i < (int)(sizeof(s_floats) / sizeof(s_floats[0])); i++) {
        const FloatNum* fn = &s_floats[i];
        if (fn->ticks <= 0) continue;
        u8 fade = fn->ticks < 20 ? (u8)(80 + fn->ticks * 8) : 240;
        if (fn->kind == 1)        // blocked
            Debug_Text(fb, fn->x - 16, fn->y, (u8)(fade / 2), (u8)(fade * 4 / 5),
                       255, "%d BLOCKED", fn->val);
        else if (fn->kind == 2)   // heal
            Debug_Text(fb, fn->x - 8, fn->y, (u8)(fade / 3), 255,
                       (u8)(fade / 2), "+%d", fn->val);
        else if (fn->kind == 3)   // crit / heavy
            Debug_Text(fb, fn->x - 4, fn->y, 255, fade, (u8)(fade / 4),
                       "%d!", fn->val);
        else
            Debug_Text(fb, fn->x, fn->y, 255, fade, (u8)(fade / 3),
                       "%d", fn->val);
    }
}
