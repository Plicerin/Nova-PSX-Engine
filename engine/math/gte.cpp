// GTE-style transform stage. Every rotation multiply truncates >>12 into i16;
// this precision loss is the authentic PS1 transform wobble — do not round.
#include "engine/math/gte.h"

static Mat g_rt = { { {4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096} }, {0, 0, 0} };
static i32 g_ofx = 0;
static i32 g_ofy = 0;
static i32 g_h = 0;
static int g_subpix = 0;
static i32 g_fog_near = 0;
static i32 g_fog_far = 0;   // far <= near -> fog disabled (p = 0)

void Gte_Identity(Mat* out) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) out->m[i][j] = (i == j) ? (i16)4096 : (i16)0;
        out->t[i] = 0;
    }
}

void Gte_CompMatrix(const Mat* a, const Mat* b, Mat* out) {
    Mat tmp;   // allows out to alias a or b
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            i64 acc = 0;
            for (int k = 0; k < 3; ++k) acc += (i64)a->m[i][k] * (i64)b->m[k][j];
            tmp.m[i][j] = (i16)(acc >> 12);   // truncating cast: authentic loss
        }
        i64 tacc = 0;
        for (int k = 0; k < 3; ++k) tacc += (i64)a->m[i][k] * (i64)b->t[k];
        tmp.t[i] = (i32)(tacc >> 12) + a->t[i];
    }
    *out = tmp;
}

void Gte_RotMatrix(const SVec* angles, Mat* out) {
    fx12 sx = Csin(angles->vx), cx = Ccos(angles->vx);
    fx12 sy = Csin(angles->vy), cy = Ccos(angles->vy);
    fx12 sz = Csin(angles->vz), cz = Ccos(angles->vz);

    // Column-vector elementary rotations (v' = M * v).
    Mat rx = { { {4096, 0, 0},
                 {0, (i16)cx, (i16)-sx},
                 {0, (i16)sx, (i16)cx} }, {0, 0, 0} };
    Mat ry = { { {(i16)cy, 0, (i16)sy},
                 {0, 4096, 0},
                 {(i16)-sy, 0, (i16)cy} }, {0, 0, 0} };
    Mat rz = { { {(i16)cz, (i16)-sz, 0},
                 {(i16)sz, (i16)cz, 0},
                 {0, 0, 4096} }, {0, 0, 0} };

    Mat zy;
    Gte_CompMatrix(&rz, &ry, &zy);
    Gte_CompMatrix(&zy, &rx, out);
}

void Gte_ScaleMatrix(Mat* m, fx12 sx, fx12 sy, fx12 sz) {
    const fx12 s[3] = { sx, sy, sz };
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
            m->m[i][j] = (i16)(((i64)m->m[i][j] * (i64)s[j]) >> 12);
}

void Gte_ApplyRot(const Mat* m, const SVec* v, LVec* out) {
    i64 x = v->vx, y = v->vy, z = v->vz;
    out->vx = (i32)(((i64)m->m[0][0] * x + (i64)m->m[0][1] * y + (i64)m->m[0][2] * z) >> 12);
    out->vy = (i32)(((i64)m->m[1][0] * x + (i64)m->m[1][1] * y + (i64)m->m[1][2] * z) >> 12);
    out->vz = (i32)(((i64)m->m[2][0] * x + (i64)m->m[2][1] * y + (i64)m->m[2][2] * z) >> 12);
}

void Gte_ApplyRotL(const Mat* m, const LVec* v, LVec* out) {
    i64 x = v->vx, y = v->vy, z = v->vz;
    i32 rx = (i32)(((i64)m->m[0][0] * x + (i64)m->m[0][1] * y + (i64)m->m[0][2] * z) >> 12);
    i32 ry = (i32)(((i64)m->m[1][0] * x + (i64)m->m[1][1] * y + (i64)m->m[1][2] * z) >> 12);
    i32 rz = (i32)(((i64)m->m[2][0] * x + (i64)m->m[2][1] * y + (i64)m->m[2][2] * z) >> 12);
    out->vx = rx; out->vy = ry; out->vz = rz;   // v may alias out
}

void Gte_TransposeRot(const Mat* m, Mat* out) {
    Mat tmp;   // allows out to alias m
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            tmp.m[i][j] = m->m[j][i];
    tmp.t[0] = 0; tmp.t[1] = 0; tmp.t[2] = 0;
    *out = tmp;
}

void Gte_SetRotTrans(const Mat* m) { g_rt = *m; }

void Gte_SetGeom(i32 ofx, i32 ofy, i32 h, int subpix_bits) {
    g_ofx = ofx; g_ofy = ofy; g_h = h; g_subpix = subpix_bits;
}

void Gte_SetFogRange(i32 fog_near, i32 fog_far) {
    g_fog_near = fog_near; g_fog_far = fog_far;
}

i32 Gte_RotTransPers(const SVec* v, i32* sx, i32* sy, i32* sz, i32* p) {
    i64 x = v->vx, y = v->vy, z = v->vz;
    i32 view_x = (i32)(((i64)g_rt.m[0][0] * x + (i64)g_rt.m[0][1] * y + (i64)g_rt.m[0][2] * z) >> 12) + g_rt.t[0];
    i32 view_y = (i32)(((i64)g_rt.m[1][0] * x + (i64)g_rt.m[1][1] * y + (i64)g_rt.m[1][2] * z) >> 12) + g_rt.t[1];
    i32 view_z = (i32)(((i64)g_rt.m[2][0] * x + (i64)g_rt.m[2][1] * y + (i64)g_rt.m[2][2] * z) >> 12) + g_rt.t[2];

    *sz = view_z;
    if (view_z <= 0) {
        // Behind the camera: PS1-style whole-poly reject, no clip plane.
        *sx = 0; *sy = 0; *p = 0;
        return view_z;
    }

    // Integer division by z IS the vertex snapping; sx/sy carry subpix bits.
    i64 hs = (i64)g_h << g_subpix;
    i32 lo = -(4096 << g_subpix);
    i32 hi = 4095 << g_subpix;
    *sx = ClampI32((i32)(((i64)(g_ofx << g_subpix)) + ((i64)view_x * hs) / view_z), lo, hi);
    *sy = ClampI32((i32)(((i64)(g_ofy << g_subpix)) + ((i64)view_y * hs) / view_z), lo, hi);

    if (g_fog_far > g_fog_near) {
        *p = ClampI32((i32)(((i64)(view_z - g_fog_near) * 4096) / (g_fog_far - g_fog_near)), 0, 4096);
    } else {
        *p = 0;
    }
    return view_z;
}

i64 Gte_NClip(i32 x0, i32 y0, i32 x1, i32 y1, i32 x2, i32 y2) {
    return (i64)(x1 - x0) * (i64)(y2 - y0) - (i64)(y1 - y0) * (i64)(x2 - x0);
}
