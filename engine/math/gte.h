// GTE-style transform stage (spec 5.6).
// Not a GTE emulator: reproduces the same *precision profile*:
//   - model vertices are i16 (SVECTOR)
//   - rotation matrices are 3x3 of i16 in 4.12; every multiply truncates >>12
//   - translation is i32 in engine world units (1.0m = 256 units)
//   - projection divides by integer z and snaps to the configured subpixel grid
// All functions are single-threaded; current-state setters mirror the PS1
// SetRotMatrix/SetTransMatrix style.
#pragma once
#include "engine/core/types.h"
#include "engine/math/fixed.h"

struct SVec { i16 vx, vy, vz; };        // short vector: model vertex or 4.12 normal
struct LVec { i32 vx, vy, vz; };        // long vector: world/view position
struct Mat {
    i16 m[3][3];                        // rotation, 4.12
    i32 t[3];                           // translation, engine units
};

// --- matrix construction / composition ---
void Gte_Identity(Mat* out);
// Euler angles in PS1 units (4096 = full turn). Composition order matches
// libgte RotMatrix: M = RotZ(az) * RotY(ay) * RotX(ax), column-vector convention
// (vector is multiplied on the right: v' = M * v).
void Gte_RotMatrix(const SVec* angles, Mat* out);
// out = a * b for rotation (4.12 truncating), out.t = a.m * b.t >> 12 + a.t
void Gte_CompMatrix(const Mat* a, const Mat* b, Mat* out);
// Scale columns of the rotation by 4.12 factors (for per-object scale).
void Gte_ScaleMatrix(Mat* m, fx12 sx, fx12 sy, fx12 sz);
// out = m.rot * v >> 12 (no translation)
void Gte_ApplyRot(const Mat* m, const SVec* v, LVec* out);
// Same for a 32-bit vector (used for camera translation), i64 intermediate.
void Gte_ApplyRotL(const Mat* m, const LVec* v, LVec* out);
// Transpose of rotation part (inverse for pure rotations); t untouched (set 0).
void Gte_TransposeRot(const Mat* m, Mat* out);

// --- current transform state (model+view composed) ---
void Gte_SetRotTrans(const Mat* m);

// --- projection state ---
// ofx/ofy: screen center in pixels; h: projection distance in pixels
// (h = (internal_w/2) / tan(hfov/2)). subpix: fractional bits in output coords.
void Gte_SetGeom(i32 ofx, i32 ofy, i32 h, int subpix_bits);
// Depth-cue (fog) range in view-space z engine units. p output of
// Gte_RotTransPers is 0 at fog_near and 4096 at fog_far (clamped).
void Gte_SetFogRange(i32 fog_near, i32 fog_far);

// Transform one vertex by the current matrix and project.
//   sx, sy : screen coords with subpix fractional bits (already snapped)
//   sz     : view-space z in engine units (>=1 if in front; unclamped raw z returned)
//   p      : fog factor 0..4096 (0 = no fog)
// Returns view-space z (sz) so callers can near/far reject. If sz <= 0 the
// vertex is behind the camera: sx/sy are garbage and the caller must reject
// the primitive (PS1-style crude near clipping: whole-poly reject).
i32 Gte_RotTransPers(const SVec* v, i32* sx, i32* sy, i32* sz, i32* p);

// Screen-space winding test, like GTE NCLIP: cross product z of the three
// projected points. > 0 : counter-clockwise (front-facing with our convention),
// == 0 : degenerate, < 0 : clockwise. Coords in subpixel units.
i64 Gte_NClip(i32 x0, i32 y0, i32 x1, i32 y1, i32 x2, i32 y2);
