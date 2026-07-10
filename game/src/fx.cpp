// Combat FX implementation. See fx.h.
#include "game/src/fx.h"

#include <cstdio>
#include <cstring>

#include "engine/assets/assets.h"
#include "engine/math/fixed.h"

enum { MAX_PARTICLES = 96 };

struct Particle {
    LVec pos, vel;         // engine units (y down), units/tick
    i32  life, max_life;   // ticks
    i32  size0, size1;     // billboard size, engine units (lerped over life)
    u8   r, g, b;
    u8   gravity;          // 0/1: apply per-tick downward pull
};

static Particle       s_pool[MAX_PARTICLES];
static const TexInfo* s_glow = nullptr;
static u32            s_seed = 0xC0FFEE;
static i32            s_shake_amp = 0, s_shake_ticks = 0, s_shake_total = 1;

static u32 Rnd() { s_seed = s_seed * 1103515245u + 12345u; return s_seed >> 16; }
static i32 RndRange(i32 lo, i32 hi) {          // inclusive lo, exclusive hi
    return lo + (i32)(Rnd() % (u32)(hi - lo));
}

void Fx_Init() {
    memset(s_pool, 0, sizeof(s_pool));
    s_glow = Tex_Find("orb_glow");
    s_shake_amp = s_shake_ticks = 0;
    s_shake_total = 1;
}

static Particle* Alloc() {
    for (int i = 0; i < MAX_PARTICLES; i++)
        if (s_pool[i].life <= 0) return &s_pool[i];
    return nullptr;                            // pool full: drop
}

void Fx_Burst(LVec pos, int count, i32 speed, u8 r, u8 g, u8 b) {
    for (int i = 0; i < count; i++) {
        Particle* p = Alloc();
        if (!p) return;
        const i32 yaw = RndRange(0, ANGLE_FULL);
        // biased upward hemisphere so sparks arc over the target
        const i32 up = RndRange(speed / 4, speed);
        const i32 hs = RndRange(speed / 3, speed);
        p->pos = pos;
        p->vel.vx = (Csin(yaw) * hs) >> FX_SHIFT;
        p->vel.vz = (Ccos(yaw) * hs) >> FX_SHIFT;
        p->vel.vy = -up;                       // y down: negative = rising
        p->life = p->max_life = RndRange(24, 44);
        p->size0 = 100; p->size1 = 24;
        p->r = r; p->g = g; p->b = b;
        p->gravity = 1;
    }
    // one big short-lived core flash
    Particle* f = Alloc();
    if (f) {
        f->pos = pos; f->vel = { 0, 0, 0 };
        f->life = f->max_life = 6;
        f->size0 = 420; f->size1 = 160;
        f->r = 255; f->g = 255; f->b = 255;
        f->gravity = 0;
    }
}

void Fx_Splash(LVec pos, int count) {
    pos.vy = -12;                              // at the water film
    for (int i = 0; i < count; i++) {
        Particle* p = Alloc();
        if (!p) return;
        const i32 yaw = RndRange(0, ANGLE_FULL);
        const i32 hs = RndRange(6, 15);        // strong outward ring
        p->pos = pos;
        p->pos.vx += (Csin(yaw) * RndRange(20, 70)) >> FX_SHIFT;
        p->pos.vz += (Ccos(yaw) * RndRange(20, 70)) >> FX_SHIFT;
        p->vel.vx = (Csin(yaw) * hs) >> FX_SHIFT;
        p->vel.vz = (Ccos(yaw) * hs) >> FX_SHIFT;
        p->vel.vy = -RndRange(10, 22);
        p->life = p->max_life = RndRange(24, 46);
        p->size0 = 130; p->size1 = 30;
        p->r = 180; p->g = 255; p->b = 235;    // bright against the teal water
        p->gravity = 1;
    }
}

void Fx_Shake(i32 amplitude, i32 ticks) {
    if (amplitude > s_shake_amp) s_shake_amp = amplitude;
    if (ticks > s_shake_ticks) { s_shake_ticks = ticks; s_shake_total = ticks; }
}

LVec Fx_CamOffset() {
    LVec o = { 0, 0, 0 };
    if (s_shake_ticks <= 0 || s_shake_amp <= 0) return o;
    const i32 a = (s_shake_amp * s_shake_ticks) / s_shake_total; // decay
    o.vx = RndRange(-a, a + 1);
    o.vy = RndRange(-a, a + 1);
    return o;
}

void Fx_Update() {
    if (s_shake_ticks > 0) s_shake_ticks--;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle* p = &s_pool[i];
        if (p->life <= 0) continue;
        p->life--;
        p->pos.vx += p->vel.vx;
        p->pos.vy += p->vel.vy;
        p->pos.vz += p->vel.vz;
        if (p->gravity) p->vel.vy += 1;        // y down: gravity pulls +y
        // splash droplets die at the water surface on the way down
        if (p->gravity && p->vel.vy > 0 && p->pos.vy > -8) p->life = 0;
    }
}

void Fx_Render(RenderContext* rc) {
    if (!rc || !s_glow) return;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        const Particle* p = &s_pool[i];
        if (p->life <= 0) continue;
        const i32 t = (p->life << FX_SHIFT) / p->max_life;         // 1 -> 0
        const i32 size = p->size1 + (((p->size0 - p->size1) * t) >> FX_SHIFT);
        // fade color with life (additive: darker = more transparent)
        const u8 r = (u8)((p->r * t) >> FX_SHIFT);
        const u8 g = (u8)((p->g * t) >> FX_SHIFT);
        const u8 b = (u8)((p->b * t) >> FX_SHIFT);
        Rc_DrawBillboard(rc, s_glow, p->pos, size, size, r, g, b,
                         true, 1);                                  // additive
    }
}
