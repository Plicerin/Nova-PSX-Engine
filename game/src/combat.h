// Turn-based combat slice (roadmap steps 6+7, vertical slice).
// Robot companion vs shard creature inside the arena: menu-driven turns,
// clip-synced actions, HP bars, floating damage, victory/defeat + restart.
#pragma once
#include "engine/core/types.h"
#include "engine/renderer/render.h"
#include "engine/renderer/framebuffer.h"

void Combat_SetActive(bool on);   // set from --combat before Demo_Init
void Combat_SetAuto(bool on);     // test hook: auto-attack every turn
bool Combat_Active();

// Evolving enemy: one species, five tiers that gain rings of petals and grow
// in size as the game progresses. Scale is fixed-point 4.12 (4096 = 1.0).
int         Evolver_TierForBattle(int battles_won);   // -> tier 1..5
i32         Evolver_TierScale(int tier);              // fx12 draw scale
const char* Evolver_TierName(int tier);
const char* Evolver_RigName(int tier);                // "evolver_t<n>"

void Combat_Init();               // resolve rigs/clips, reset the battle
void Combat_Update();             // one fixed 60 Hz tick (input+logic+anims)
void Combat_Render(RenderContext* rc);
void Combat_DrawUI(Framebuffer* fb);

// Status/inspect screen ([I] in the action menu): freezes the battle and
// shows the player character on a turntable with a stats panel.
bool Combat_InspectActive();
const Camera* Combat_InspectCam();
void Combat_ToggleInspect();      // test hook (same as pressing I)
