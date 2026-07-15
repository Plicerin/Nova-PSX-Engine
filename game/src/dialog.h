// Staged dialogue scene: a directed, pre-combat story beat.
//
// A script is a list of lines; each line names a speaker, some text, a camera
// SHOT to cut to, and an optional world EVENT (the rift tearing open, the
// creature unfolding). Text types on; the player advances with Cross/Start.
// When the script runs out, the scene hands off directly into combat.
#pragma once
#include "engine/core/types.h"
#include "engine/renderer/render.h"
#include "engine/renderer/framebuffer.h"

void Dialog_SetActive(bool on);   // set from --dialog before Demo_Init
void Dialog_SetAuto(bool on);     // test hook: advance lines without input
void Dialog_SetHoldLine(int n);   // test hook: jump to line n and freeze there
bool Dialog_Active();

void Dialog_Init();
void Dialog_Update();             // one fixed 60 Hz tick (input + typing)
void Dialog_Render(RenderContext* rc);
void Dialog_DrawUI(Framebuffer* fb);
const Camera* Dialog_Cam();       // the current shot's camera
