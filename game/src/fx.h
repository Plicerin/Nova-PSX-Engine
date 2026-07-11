// Combat FX (roadmap step 5): fixed-pool particles drawn as additive
// billboards, plus a decaying camera shake. All fixed point, deterministic
// (seeded LCG), zero allocations.
#pragma once
#include "engine/core/types.h"
#include "engine/math/gte.h"
#include "engine/renderer/render.h"

void Fx_Init();                       // reset pool + shake
void Fx_Update();                     // one 60 Hz tick
void Fx_Render(RenderContext* rc);    // billboard pass (call inside the frame)

// Radial burst (impact sparks). pos in engine units; speed in units/tick.
void Fx_Burst(LVec pos, int count, i32 speed, u8 r, u8 g, u8 b);
// Upward splash ring at the water plane (teal droplets).
void Fx_Splash(LVec pos, int count);
// Screen shake: amplitude in engine units, duration in ticks.
void Fx_Shake(i32 amplitude, i32 ticks);
// Current camera offset (add to the scene camera position each frame).
LVec Fx_CamOffset();

// --- ambient particle drift (atmosphere) -----------------------------------
// A looping field of slow additive motes bounded to an AABB (engine units,
// y down: vmin is the upper/near corner, vmax the lower/far). Motes rise and
// sway, wrapping back to the floor -- steady mood haze, independent of the
// combat burst pool. Init enables it; Clear disables.
void Fx_AmbientInit(LVec vmin, LVec vmax);
void Fx_AmbientClear();
void Fx_AmbientUpdate();
void Fx_AmbientRender(RenderContext* rc);
