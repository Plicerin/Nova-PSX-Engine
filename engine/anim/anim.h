// Rigid-parts skeletal animation (PS1 style, spec addendum in
// docs/file_formats.md: .rigbin / .animbin).
//
// A rig is a bone hierarchy; each bone optionally draws one mesh segment.
// Clips hold uniformly-timed keyframes for every bone. All math is fixed
// point: rotations in PS1 angle units interpolated along the shortest path,
// positions in engine units, matrices composed with the truncating 4.12 GTE
// pipeline (inter-segment cracks and drift are authentic).
#pragma once
#include "engine/assets/assets.h"
#include "engine/renderer/render.h"

struct AnimState {
    const AnimClip* clip;     // null = rig renders its bind pose
    i32             time_ms;  // position within the clip
    bool            done;     // one-shot reached its end (looped: never)
};

// Start playing a clip from t=0 (clip may be null to show the bind pose).
void Anim_Start(AnimState* st, const AnimClip* clip);

// Advance time; loops wrap, one-shots clamp to the last key and set `done`.
void Anim_Update(AnimState* st, i32 dt_ms);

// Evaluate the pose at st->time_ms and draw every bone segment.
// `model` is the object's model-to-world matrix (rotation 4.12 + translation).
void Anim_Draw(RenderContext* rc, const Rig* rig, const AnimState* st,
               const Mat* model);
