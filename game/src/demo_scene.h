// Demo game scene (spec 14 M8 + 19): walkable test chamber with a rotating
// gem showcase object, additive glow billboard, ambience loop and blip SFX.
#pragma once
#include "engine/core/types.h"
#include "engine/assets/assets.h"
#include "engine/renderer/render.h"
#include "engine/renderer/framebuffer.h"

// Turntable rate (PS1 angle units/tick) for a "girl" showcase object; 0 = off.
// Call before Demo_Init.
void Demo_SetSpin(i32 deg_per_tick);
void Demo_SetEvolverTier(int tier);   // --evolver-tier N: showcase one tier
void Demo_SetShowRig(const char* name); // --show-rig NAME: showcase any rig
void Demo_SetShowMorph(bool on);        // --show-morph: play the spider<->snake morph
void Demo_SetMorphFrame(int f);         // --morph-frame N: hold one morph frame
void Demo_SetCombatCam(bool on);        // --combat-cam: view creature in arena, combat cam
// Start a named animation clip on the showcase robot (test/CLI hook).
void Demo_PlayClip(const char* name);
void Demo_Init(Level* level);
// One fixed 60 Hz sim tick (main loop owns the accumulator).
void Demo_Update();
// Rc_Begin (scene cam or debug free cam), Fb_Clear, draw level + billboard, Rc_Flush.
void Demo_Render(RenderContext* rc, Framebuffer* fb);
// Bitmap-text title + control hints; call after Demo_Render.
void Demo_DrawUI(Framebuffer* fb);
