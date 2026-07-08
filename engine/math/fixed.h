// Fixed-point math, PS1/GTE conventions.
//   - fx12: 4.12 fixed point (4096 = 1.0), matches GTE rotation matrix format.
//   - Angles: 4096 units = one full turn (PS1 libgte convention).
// Precision loss from the >>12 truncation in every multiply is deliberate:
// it is the source of the authentic transform wobble.
#pragma once
#include "engine/core/types.h"

typedef i32 fx12;                 // 20.12 signed fixed point
constexpr fx12 FX_ONE   = 4096;
constexpr int  FX_SHIFT = 12;
constexpr i32  ANGLE_FULL = 4096; // 4096 = 360 degrees

inline fx12 FxMul(fx12 a, fx12 b) { return (fx12)(((i64)a * (i64)b) >> FX_SHIFT); }
inline fx12 FxDiv(fx12 a, fx12 b) { return b ? (fx12)(((i64)a << FX_SHIFT) / b) : 0; }
inline fx12 FxFromFloat(float f)  { return (fx12)(f * 4096.0f); }
inline float FxToFloat(fx12 v)    { return (float)v / 4096.0f; }

// Must be called once at startup; builds the 4096-entry sine table.
void Fixed_Init();

// Sine/cosine, input angle in PS1 units (wraps), output 4.12 in [-4096, 4096].
fx12 Csin(i32 angle);
fx12 Ccos(i32 angle);

// Integer square root (for vector lengths / bounding radii).
u32 IsqrtU32(u32 v);

inline i32 ClampI32(i32 v, i32 lo, i32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
