// Staged dialogue scene. See dialog.h.
#include "game/src/dialog.h"

#include <cstdio>
#include <cstring>

#include "engine/anim/anim.h"
#include "engine/assets/assets.h"
#include "engine/debug/debug.h"
#include "engine/input/input.h"
#include "engine/math/fixed.h"
#include "engine/platform/platform.h"
#include "game/src/combat.h"
#include "game/src/fx.h"

// ------------------------------------------------------------------ staging

// Actors stand where they will fight, so the cut into combat does not teleport
// anyone. Engine units: 256 = 1 m, +x right, y DOWN, +z into the screen.
struct Actor {
    const Rig*      rig;
    AnimState       anim;
    const AnimClip* idle;
    LVec            pos;
    SVec            rot;
    bool            visible;
};

static Actor s_astra, s_unit7, s_enemy;

// Camera shots. Hand-authored rather than computed with a look-at: the actor
// marks are fixed, so the angles are constants and stay integer (no float in
// the engine). Engine pitch is NEGATIVE to look down, positive to look up --
// the level packer negates the JSON's rx, so a JSON pitch of +48 (down)
// arrives here as -546.
enum Shot : u8 {
    SHOT_WIDE = 0,   // establishing: the whole flooded lab
    SHOT_TWO,        // two-shot: ASTRA and UNIT-7 together
    SHOT_ASTRA,      // close on ASTRA
    SHOT_UNIT7,      // close on UNIT-7
    SHOT_ENEMY,      // low angle looking UP at the creature
    SHOT_COUNT
};

struct ShotDef { LVec pos; SVec rot; };

static const ShotDef kShots[SHOT_COUNT] = {
    // WIDE: the combat camera itself, so the hand-off is seamless.
    { { 0, -2176, -1920 }, { (i16)-546, 0, 0 } },
    // TWO: pulled back and wider so BOTH crew are in frame, yaw -34 deg.
    { { 640, -470, -1180 }, { 0, (i16)3709, 0 } },
    // ASTRA at (-589,128) faces +x, so the camera sits on her +x side and a
    // touch -z, looking back at her: dir (-1.7,+0.55) -> yaw ~3430.
    { { 435, -395, -20 }, { 0, (i16)3430, 0 } },
    // UNIT-7 at (-922,461); camera in front (its facing) looking back at it.
    { { -512, -360, 205 }, { 0, (i16)3110, 0 } },
    // ENEMY at (589,128): low (0.3 m up) and close on its -x/-z side, tilted
    // UP ~11 deg so the creature looms. Look-at dir (409,488) -> yaw ~455.
    { { 180, -78, -360 }, { (i16)125, (i16)455, 0 } },
};

// ------------------------------------------------------------------- script

enum Event : u8 {
    EV_NONE = 0,
    EV_BREACH,    // the rift tears: shake + cold light, creature appears
};

struct Line {
    const char* who;    // nullptr = caption card (no nameplate)
    const char* text;
    Shot        shot;
    Event       ev;
};

// The beat: two crew wade into a flooded sublevel, find the rift still open,
// and something folds its way out of it.
static const Line kScript[] = {
    { nullptr,  "HYBORNEA STATION  //  SUBLEVEL 3",
      SHOT_WIDE,  EV_NONE },
    { "ASTRA",  "Coolant's up to my knees. Whatever came through, it came "
                "through deep.",
      SHOT_TWO,   EV_NONE },
    { "UNIT-7", "Rift signature holding four hundred meters out. It is not "
                "closing on its own.",
      SHOT_UNIT7, EV_NONE },
    { "ASTRA",  "Then we close it.",
      SHOT_ASTRA, EV_NONE },
    { nullptr,  "//  RIFT BREACH",
      SHOT_WIDE,  EV_BREACH },
    { "UNIT-7", "Contact. It is... unfolding.",
      SHOT_ENEMY, EV_NONE },
    { "ASTRA",  "Stay on my shoulder, Seven.",
      SHOT_ASTRA, EV_NONE },
};
static const int kNumLines = (int)(sizeof(kScript) / sizeof(kScript[0]));

// --------------------------------------------------------------------- state

static bool   s_active = false;
static bool   s_auto = false;   // test hook: hold each finished line, then advance
static int    s_hold_line = -1; // test hook: jump to this line and never advance
static int    s_line = 0;
static i32    s_tick = 0;      // ticks on the current line (drives the typing)
static int    s_shown = 0;     // characters revealed
static Camera s_cam;

static const i32 kTicksPerChar = 2;   // ~30 chars/sec at 60 Hz

void Dialog_SetActive(bool on) { s_active = on; }
void Dialog_SetAuto(bool on) { s_auto = on; }
void Dialog_SetHoldLine(int n) { s_hold_line = n; }
bool Dialog_Active() { return s_active; }

static int LineLen(int i) { return (int)strlen(kScript[i].text); }

static void EnterLine(int i) {
    s_line = i;
    s_tick = 0;
    s_shown = 0;
    if (i >= kNumLines) return;
    // If we jumped past the breach (test hook), the creature should already be
    // present -- reflect any earlier world events without re-firing their FX.
    for (int j = 0; j < i; j++)
        if (kScript[j].ev == EV_BREACH) s_enemy.visible = true;
    if (kScript[i].ev == EV_BREACH) {
        s_enemy.visible = true;
        LVec c = s_enemy.pos;
        c.vy -= 270;                                   // creature's core
        Fx_Burst(c, 24, 22, 140, 220, 255);            // cold rift light
        Fx_AddLight(c, 120, 210, 255, 9 * 256, 40);
        Fx_Shake(30, 26);
    }
}

void Dialog_Init() {
    if (!s_active) return;
    Debug_AllowFreeCam(false);       // the scene owns the camera

    s_astra = {};
    s_astra.rig     = Rig_Find("astro");
    s_astra.idle    = Anim_Find("astro_idle");
    s_astra.pos     = { (i32)(-2.3 * 256), 0, (i32)(0.5 * 256) };
    // The rigs do NOT share a forward axis (different source GLBs): astro is
    // authored facing +z, so +x is yaw 1024; unit7 faces -z, so +x is 3072.
    s_astra.rot     = { 0, (i16)1024, 0 };            // facing the rift (+x)
    s_astra.visible = true;
    Anim_Start(&s_astra.anim, s_astra.idle);

    s_unit7 = {};
    s_unit7.rig     = Rig_Find("unit7");
    s_unit7.idle    = Anim_Find("unit7_idle");
    s_unit7.pos     = { (i32)(-3.6 * 256), 0, (i32)(1.8 * 256) };
    s_unit7.rot     = { 0, (i16)3213, 0 };
    s_unit7.visible = true;
    Anim_Start(&s_unit7.anim, s_unit7.idle);

    // The creature is staged but hidden until the rift actually breaches.
    s_enemy = {};
    s_enemy.rig     = Rig_Find("prism");
    s_enemy.idle    = Anim_Find("prism_idle");
    s_enemy.pos     = { (i32)(2.3 * 256), 0, (i32)(0.5 * 256) };
    s_enemy.rot     = { 0, (i16)3072, 0 };            // facing the crew (-x)
    s_enemy.visible = false;
    Anim_Start(&s_enemy.anim, s_enemy.idle);

    if (!s_astra.rig || !s_unit7.rig)
        fprintf(stderr, "dialog: missing rigs (build assets first)\n");

    EnterLine(s_hold_line >= 0 && s_hold_line < kNumLines ? s_hold_line : 0);
}

// Hand off to the fight. Combat re-inits its own actors on the same marks,
// so nothing visibly moves across the cut.
static void HandOffToCombat() {
    s_active = false;
    Combat_SetActive(true);
    Combat_Init();
}

void Dialog_Update() {
    if (!s_active) return;
    if (s_line >= kNumLines) { HandOffToCombat(); return; }

    Anim_Update(&s_astra.anim, 16);
    Anim_Update(&s_unit7.anim, 16);
    if (s_enemy.visible) Anim_Update(&s_enemy.anim, 16);

    const int len = LineLen(s_line);
    s_tick++;
    if (s_shown < len && (s_tick % kTicksPerChar) == 0) s_shown++;

    if (s_hold_line >= 0) { s_shown = len; return; }   // test hook: freeze here
    if (Pad_Pressed(PAD_CROSS) || Pad_Pressed(PAD_START)) {
        if (s_shown < len) s_shown = len;          // first press: finish typing
        else EnterLine(s_line + 1);                // second: next line
    } else if (s_auto && s_shown >= len &&
               s_tick > (i32)len * kTicksPerChar + 90) {
        EnterLine(s_line + 1);                     // test hook: read, then move on
    }
    if (s_line >= kNumLines) HandOffToCombat();
}

const Camera* Dialog_Cam() {
    const ShotDef* sd = &kShots[kScript[s_line < kNumLines ? s_line : kNumLines - 1].shot];
    s_cam.pos    = sd->pos;
    s_cam.rot    = sd->rot;
    s_cam.near_z = 40;
    s_cam.far_z  = 20 * 256;
    return &s_cam;
}

static void DrawActor(RenderContext* rc, Actor* a) {
    if (!a->rig || !a->visible) return;
    Mat m;
    Gte_RotMatrix(&a->rot, &m);
    m.t[0] = a->pos.vx;
    m.t[1] = a->pos.vy;
    m.t[2] = a->pos.vz;
    Anim_Draw(rc, a->rig, &a->anim, &m);
}

void Dialog_Render(RenderContext* rc) {
    if (!s_active) return;
    DrawActor(rc, &s_astra);
    DrawActor(rc, &s_unit7);
    DrawActor(rc, &s_enemy);
}

// ----------------------------------------------------------------------- UI

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

// Greedy word wrap of the revealed prefix into fixed-width rows.
// Returns the number of rows written. 8x8 font, so cols = width / 8.
static int WrapText(const char* s, int shown, int cols, char rows[3][48]) {
    int nrow = 0, col = 0;
    rows[0][0] = '\0';
    for (int i = 0; i < shown && nrow < 3;) {
        // measure the next word
        int wlen = 0;
        while (i + wlen < shown && s[i + wlen] != ' ') wlen++;
        if (col + wlen > cols && col > 0) {          // does not fit: new row
            rows[nrow][col] = '\0';
            if (++nrow >= 3) break;
            col = 0;
            rows[nrow][0] = '\0';
        }
        for (int k = 0; k < wlen && col < cols && col < 47; k++)
            rows[nrow][col++] = s[i + k];
        i += wlen;
        if (i < shown && s[i] == ' ') {              // the separating space
            if (col > 0 && col < cols && col < 47) rows[nrow][col++] = ' ';
            i++;
        }
    }
    if (nrow < 3) rows[nrow][col < 47 ? col : 47] = '\0';
    return nrow + 1;
}

void Dialog_DrawUI(Framebuffer* fb) {
    if (!s_active || s_line >= kNumLines) return;
    const Line* L = &kScript[s_line];

    // Cinematic letterbox.
    const int bar = 20;
    FillRect(fb, 0, 0, fb->w, bar, 0, 0, 0);
    FillRect(fb, 0, fb->h - bar, fb->w, bar, 0, 0, 0);

    if (!L->who) {
        // Caption card: centred in the frame, no box.
        char rows[3][48];
        int n = WrapText(L->text, s_shown, 36, rows);
        for (int i = 0; i < n; i++) {
            int w = (int)strlen(rows[i]) * 8;
            Debug_Text(fb, (fb->w - w) / 2, fb->h / 2 - 4 + i * 10,
                       150, 240, 235, "%s", rows[i]);
        }
    } else {
        // Speech box above the bottom bar.
        const int bx = 12, bw = fb->w - 24, bh = 44;
        const int by = fb->h - bar - bh - 4;
        FillRect(fb, bx, by, bw, bh, 8, 20, 24);              // panel
        FillRect(fb, bx, by, bw, 1, 60, 150, 160);            // top rule
        FillRect(fb, bx, by + bh - 1, bw, 1, 60, 150, 160);   // bottom rule

        // Nameplate sits on the panel's top edge.
        int nw = (int)strlen(L->who) * 8 + 8;
        FillRect(fb, bx + 6, by - 9, nw, 10, 20, 60, 70);
        Debug_Text(fb, bx + 10, by - 8, 150, 240, 235, "%s", L->who);

        char rows[3][48];
        int n = WrapText(L->text, s_shown, (bw - 16) / 8, rows);
        for (int i = 0; i < n; i++)
            Debug_Text(fb, bx + 8, by + 8 + i * 10, 220, 232, 232, "%s",
                       rows[i]);

        // Blinking advance caret, once the line has finished typing.
        if (s_shown >= LineLen(s_line) && ((s_tick >> 4) & 1))
            Debug_Text(fb, bx + bw - 14, by + bh - 12, 150, 240, 235, "\x1f");
    }
}
