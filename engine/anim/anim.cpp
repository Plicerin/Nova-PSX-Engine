// Rigid-parts skeletal animation runtime. See anim.h.
#include "engine/anim/anim.h"

#include <cstdio>
#include <cstring>

#include "engine/math/fixed.h"

void Anim_Start(AnimState* st, const AnimClip* clip) {
    st->clip = clip;
    st->time_ms = 0;
    st->done = false;
}

void Anim_Update(AnimState* st, i32 dt_ms) {
    const AnimClip* c = st->clip;
    if (!c) return;
    st->time_ms += dt_ms;
    // Loops wrap over nkeys intervals (last key blends back into key 0);
    // one-shots clamp at the last key.
    const i32 span = c->loop ? (i32)c->nkeys * c->key_ms
                             : (i32)(c->nkeys - 1) * c->key_ms;
    if (span <= 0) { st->time_ms = 0; st->done = !c->loop; return; }
    if (c->loop) {
        st->time_ms %= span;
    } else if (st->time_ms >= span) {
        st->time_ms = span;
        st->done = true;
    }
}

// Shortest-path angle interpolation in PS1 units (wrap-aware).
static inline i16 LerpAngle(i16 a, i16 b, fx12 t) {
    i32 d = ((i32)(b - a) + ANGLE_FULL / 2) & (ANGLE_FULL - 1);
    d -= ANGLE_FULL / 2;
    return (i16)((i32)a + ((d * t) >> FX_SHIFT));
}

static inline i16 LerpI16(i16 a, i16 b, fx12 t) {
    return (i16)((i32)a + (((i32)(b - a) * t) >> FX_SHIFT));
}

void Anim_Draw(RenderContext* rc, const Rig* rig, const AnimState* st,
               const Mat* model) {
    if (!rc || !rig || !model) return;

    // Pick the bracketing keys and blend factor.
    const AnimClip* c = st ? st->clip : nullptr;
    u32 kA = 0, kB = 0;
    fx12 t = 0;
    if (c && c->nkeys > 1) {
        u32 idx = (u32)(st->time_ms / c->key_ms);
        u32 last = (u32)c->nkeys - 1;
        if (c->loop) {
            kA = idx % c->nkeys;
            kB = (kA + 1) % c->nkeys;
        } else {
            kA = idx > last ? last : idx;
            kB = kA < last ? kA + 1 : last;
        }
        t = (fx12)(((i32)(st->time_ms - (i32)idx * c->key_ms) << FX_SHIFT)
                   / c->key_ms);
        if (t < 0) t = 0;
        if (t > FX_ONE) t = FX_ONE;
    }

    Mat world[32];  // rig loader caps nbones at 32, parents precede children
    for (u32 i = 0; i < rig->nbones; i++) {
        const RigBone* b = &rig->bones[i];

        SVec rot = { 0, 0, 0 };
        LVec off = { b->bind_pos.vx, b->bind_pos.vy, b->bind_pos.vz };
        if (c) {
            const AnimKey* a = &c->keys[kA * c->nbones + i];
            const AnimKey* d = &c->keys[kB * c->nbones + i];
            rot.vx = LerpAngle(a->rot.vx, d->rot.vx, t);
            rot.vy = LerpAngle(a->rot.vy, d->rot.vy, t);
            rot.vz = LerpAngle(a->rot.vz, d->rot.vz, t);
            off.vx += LerpI16(a->pos.vx, d->pos.vx, t);
            off.vy += LerpI16(a->pos.vy, d->pos.vy, t);
            off.vz += LerpI16(a->pos.vz, d->pos.vz, t);
        }

        Mat local;
        Gte_RotMatrix(&rot, &local);
        if (b->bind_rot.vx | b->bind_rot.vy | b->bind_rot.vz) {
            // Hinge frame: keys rotate about the bind_rot-tilted axes while
            // the segment mesh stays authored in the parent-aligned frame:
            //   local = Rb * Rkey * RbT
            Mat rb, rbt, tmp;
            Gte_RotMatrix(&b->bind_rot, &rb);
            Gte_TransposeRot(&rb, &rbt);
            Gte_CompMatrix(&local, &rbt, &tmp);   // Rkey * RbT
            Gte_CompMatrix(&rb, &tmp, &local);    // Rb * (Rkey * RbT)
        }
        local.t[0] = off.vx;
        local.t[1] = off.vy;
        local.t[2] = off.vz;

        const Mat* parent = (b->parent < 0) ? model : &world[b->parent];
        Gte_CompMatrix(parent, &local, &world[i]);

        if (b->mesh) Rc_DrawMesh(rc, b->mesh, &world[i]);
    }
}

// Pose evaluation shared by Anim_Draw/Anim_BoneWorld: fills world[] for every
// bone. Returns the blend keys via out-params; caller need not use them.
static void EvalWorld(const Rig* rig, const AnimState* st, const Mat* model,
                      Mat world[32]) {
    const AnimClip* c = st ? st->clip : nullptr;
    u32 kA = 0, kB = 0;
    fx12 t = 0;
    if (c && c->nkeys > 1) {
        u32 idx = (u32)(st->time_ms / c->key_ms);
        u32 last = (u32)c->nkeys - 1;
        if (c->loop) { kA = idx % c->nkeys; kB = (kA + 1) % c->nkeys; }
        else { kA = idx > last ? last : idx; kB = kA < last ? kA + 1 : last; }
        t = (fx12)(((i32)(st->time_ms - (i32)idx * c->key_ms) << FX_SHIFT)
                   / c->key_ms);
        if (t < 0) t = 0;
        if (t > FX_ONE) t = FX_ONE;
    }
    for (u32 i = 0; i < rig->nbones; i++) {
        const RigBone* b = &rig->bones[i];
        SVec rot = { 0, 0, 0 };
        LVec off = { b->bind_pos.vx, b->bind_pos.vy, b->bind_pos.vz };
        if (c) {
            const AnimKey* a = &c->keys[kA * c->nbones + i];
            const AnimKey* d = &c->keys[kB * c->nbones + i];
            rot.vx = LerpAngle(a->rot.vx, d->rot.vx, t);
            rot.vy = LerpAngle(a->rot.vy, d->rot.vy, t);
            rot.vz = LerpAngle(a->rot.vz, d->rot.vz, t);
            off.vx += LerpI16(a->pos.vx, d->pos.vx, t);
            off.vy += LerpI16(a->pos.vy, d->pos.vy, t);
            off.vz += LerpI16(a->pos.vz, d->pos.vz, t);
        }
        Mat local;
        Gte_RotMatrix(&rot, &local);
        if (b->bind_rot.vx | b->bind_rot.vy | b->bind_rot.vz) {
            Mat rb, rbt, tmp;
            Gte_RotMatrix(&b->bind_rot, &rb);
            Gte_TransposeRot(&rb, &rbt);
            Gte_CompMatrix(&local, &rbt, &tmp);
            Gte_CompMatrix(&rb, &tmp, &local);
        }
        local.t[0] = off.vx;
        local.t[1] = off.vy;
        local.t[2] = off.vz;
        const Mat* parent = (b->parent < 0) ? model : &world[b->parent];
        Gte_CompMatrix(parent, &local, &world[i]);
    }
}

bool Anim_BoneWorld(const Rig* rig, const AnimState* st, const Mat* model,
                    const char* bone, LVec* out) {
    if (!rig || !model || !bone || !out || rig->nbones > 32) return false;
    Mat world[32];
    EvalWorld(rig, st, model, world);
    for (u32 i = 0; i < rig->nbones; i++) {
        if (strncmp(rig->bones[i].name, bone, sizeof(rig->bones[i].name)) != 0)
            continue;
        out->vx = world[i].t[0];
        out->vy = world[i].t[1];
        out->vz = world[i].t[2];
        return true;
    }
    return false;
}
