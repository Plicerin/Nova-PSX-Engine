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

void Combat_Init();               // resolve rigs/clips, reset the battle
void Combat_Update();             // one fixed 60 Hz tick (input+logic+anims)
void Combat_Render(RenderContext* rc);
void Combat_DrawUI(Framebuffer* fb);
