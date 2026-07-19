# Nova — Production Readiness Audit

_Last updated: 2026-07-19. A snapshot of what exists, what's missing, and the effort
to get from "vertical slice" to "we can build the actual game." Effort scale:
**S** ≈ hours–1 day, **M** ≈ several days, **L** ≈ 1–2+ weeks._

## TL;DR verdict

The **engine core and two gameplay slices (dialogue + turn-based combat) are real and
reasonably polished**, and the input layer is production-grade. We are **not** blocked by
broken tech — every gap below is an *unbuilt feature*, not a bug.

The distance to "making the actual game" falls into three buckets:

1. **Game shell — the biggest structural gap.** There is no title screen, menu, pause,
   save/load, or top-level state machine. The whole project is launched by CLI flags
   (`--combat`, `--dialog`, `--show-rig`…) and one hardcoded dialog→combat cut. Almost
   everything else (menus, transitions, encounter flow, endings) hangs off this.
2. **Presentation polish.** No shadows of any kind; the game is effectively **silent**
   (one blip + one drone; combat and dialog make no sound); no screen transitions.
3. **Content depth & pipeline scale.** The asset library is thin and almost entirely
   bespoke one-offs; there is no artist/DCC workflow and no reusable prop kit.

We have enough to **prototype the game's flow now** (the slices work), but a real
production needs the shell + audio + a content pipeline before scaling up.

## Current inventory ("what we have")

| Kind | Count | Notes |
|---|---|---|
| Levels | 4 | `test_chamber`, `crew_room`, `combat_arena`, `combat_stage` (the real arena) |
| Playable/allies | 2 | ASTRA (human, from GLB), UNIT-7 (robot companion, from GLB) |
| Other characters | 2 | `crew` humanoid, `girl` (both unused in combat) |
| Enemies | — | `shard`, `prism`, `evolver` (5 defined tiers, **unused**), **TRIARCHON** (spider→snake morph, wired into combat) |
| Meshes | 97 | ~42 are the TRIARCHON family (spider 15, snake 13, 14 morph frames) |
| Textures | 14 | ~all procedurally generated; 2 third-party (`girl_diffuse`, `psrobot_tex`) |
| Rigs | 14 | ≤32 bones each, rigid-part (no skinning) |
| Anim clips | 47 | idle/attack/hit/defend sets + morph/walk extras |
| Sounds | **2** | `blip` (SFX), `ambience` (looping drone). That's the entire audio library. |

**Reuse story:** meshes are essentially all one-offs. Real reuse exists only in (a) shared
textures, and (b) `tools/tri_build.py` — a procedural triangle-assembly library (`tube`,
`octahedron`, `strip`, `attach`) that generates every alien creature. There is no asset
browser, prop kit, or reusable static-mesh library.

## Subsystem status

### Renderer — strong, PS1-authentic
Affine texture mapping, ordering-table painter's sort (no Z by default), per-vertex
lighting (directional + colored fill + up to 4 attenuated colored point lights), depth
fog, 4×4 ordered dither + 15-bit quantize, 4 semi-transparency blend modes, camera-facing
billboards, tiles/sprites/lines, wireframe. PS1.5 opt-ins: additive Blinn specular,
low-res bloom. Multiple internal resolutions with nearest integer/fit/stretch upscale.
Perspective-correct mapping, bilinear, and Z-buffer exist but are **debug-only** (by design).

### FX — good for combat
Deterministic zero-alloc particle pool: impact **bursts**, water **splash**, **screen
shake**, **ambient motes**, and **transient colored lights** (impacts throw real light on
the room). All additive billboards via `orb_glow`. Used in combat + the dialog breach.

### Audio — solid mixer, **effectively unused**
24-voice software mixer, 44.1 kHz stereo, per-voice volume/pan/pitch, one-shot + looping,
mutex-safe, 512 KB SFX budget enforced. WAV/PCM only (no ADPCM). But: **combat.cpp and
dialog.cpp make zero audio calls.** The only in-game sound is looping ambience + a blip on
Cross. No music streaming, no crossfades, no combat/UI SFX. The engine is ready; the
content and event-wiring are not there.

### Art pipeline — deterministic and budget-safe, but code-only
One-command build (`build_assets.py`) from a single JSON manifest, with a self-verifying
procedural source generator. Full texture stack (4/8-bit CLUT + 15-bit direct, median-cut
quantize, VRAM shelf-packer with a hard **704 KiB** budget). Mesh import (OBJ + one-way
GLB→OBJ), rich per-mesh flags, rig/anim compiler, level packer (fog + lights + objects).
**No dithering in the texture importer** (hard-coded off — gradients band). **All art is
code-generated**; there is no hand-paint/DCC workflow in practice, and GLB import discards
skins + animations (no Blender animation round-trip).

### Dialogue — works, hardcoded & linear
Typewriter text, speakers/nameplates, per-line camera shots, one world event (the breach),
seamless hand-off into combat. But the script is a **hardcoded C++ array** — no data
format, **no branching/choices**, no variables/flags.

### Combat — real single-encounter slice, hardcoded
Turn state machine (intro→menu→player→companion→enemy→resolve + morph/victory/defeat),
3 actions (ATTACK/DEFEND/OVERLOAD with a charge meter), auto companion (repair/zap), enemy
telegraph→heavy mechanic, the **spider→snake morph at half HP**, HP bars, floating damage,
inspect screen. All fighters/stats/damage are **hardcoded**; the 5-tier `Evolver` system
is built but **never called**; victory just restarts the same fight.

### Input — production-grade
Full PS1 pad abstraction, keyboard+gamepad merge, stick-as-dpad, edge handling across
multi-tick frames, and a rebinding API + config-file parser (the file loader just isn't
called yet). The most finished system in the project.

### UI / text — ad-hoc, no framework
One 8×8 bitmap font (mixed case supported; UPPERCASE is a style choice), printf-style
`Debug_Text`, and hand-rolled `FillRect`. Every screen (HP bars, menus, letterbox) is
bespoke. No widget/menu/layout system, no font scaling.

### Game shell — **absent**
No title, main menu, pause menu, options menu, save/load, or top-level game-state machine.
`main.cpp` is an arg-parser + 60 Hz loop; `demo_scene.cpp` is an `if/else` dispatcher that
picks one mode at launch and never changes it (except the one dialog→combat cut).

## Gap register

| Gap | Area | Status | Effort |
|---|---|---|---|
| Top-level game-state machine (boot→title→menu→scene→combat→…) | Shell | none | **L** |
| Title screen + main menu (New/Continue/Options/Quit) | Shell | none | **M** |
| Save/load (progress, settings) | Shell | none | **M** |
| Pause menu (game-level, not debug `P`) | Shell | none | **S–M** |
| Options/settings menu (surface config + input rebinding) | Shell | CLI-only | **M** |
| Encounter/progression flow (wire the built `Evolver` tiers; victory → next) | Shell | restarts same fight | **M** |
| Level/scene transitions (load next level in-process) | Shell | fixed at launch | **M** |
| Screen transitions (fade/wipe/crossfade) | Render | none | **S** |
| **Shadows** (blob/projected) — characters float | Render | none | **S** (blob) / **M** (projected) |
| Camera framework (lerp/easing/cuts/sequencer; only hard cuts + shake now) | Render | none | **M** |
| Decals (scorch/marks) | Render | none | **M** |
| Post-FX beyond bloom (flash, vignette, grade) | Render | bloom only | **S–M** |
| Combat + dialog/UI **SFX** | Audio | silent | **S–M** |
| Music system (streaming, crossfade, per-scene) | Audio | drone only | **M–L** |
| Reusable UI/menu framework (widgets, focus) | UI | none | **M** |
| Data-driven combat (enemy/move/stat tables) | Combat | hardcoded | **L** |
| Data-driven dialogue + branching/choices | Dialogue | hardcoded/linear | **M** (branch) / **L** (full) |
| Texture dithering | Pipeline | off | **S** |
| Prune stale build outputs; enforce tri budget | Pipeline | manual | **S** |
| Third-party chars regen on clean clone (GLB paths hardcoded) | Pipeline | silent-skip | **S–M** |
| Asset browser / manifest inspector | Pipeline | none | **M** |
| Blender animation/skin round-trip | Pipeline | GLB import is one-way, no anim | **L** |

## Recommended path to "start making the game"

**Phase 0 — the shell (unblocks everything).** Build the top-level state machine and the
frame it needs: title/menu, pause, save/load, screen transitions, and an encounter/scene
flow that actually chains scenes (and finally uses the `Evolver` tiers). Target: launch to
title, start a run, fight → win → next → end, and save/quit. _Effort: ~L + a couple of Ms._

**Phase 1 — make it feel alive.** Combat/dialog/UI **SFX** (the mixer is ready), basic
**music** per scene, **blob shadows** (small, huge grounding payoff), and a light **camera
lerp**. This is the cheapest large jump in perceived quality. _Effort: mostly S–M._

**Phase 2 — scale content.** Data-driven combat (enemy/move tables) and data-driven
dialogue with branching, so designers add content without C++ edits; texture dithering;
an asset/manifest inspector; more TRIARCHON forms (caterpillar/bull) and the raptor/titan
stages. _Effort: the Ls live here._

## Known risks / cleanup

- **Fresh-clone gap:** ASTRA, UNIT-7, and `girl` are third-party GLB-derived and are **not
  regenerated** by the build (the `rig_split_*` scripts have hardcoded `Downloads` paths and
  aren't orchestrated). A clean clone silently skips ~24 meshes + their textures/rigs.
  Everything procedural (arena, chamber, all `tri_build` creatures) rebuilds fine.
- **Stale outputs:** `build/assets/` holds orphaned dev artifacts (5 textures, 13 levels,
  1 mesh) never pruned except by `build.ps1 -Clean`.
- **Content is bespoke:** almost every mesh is a one-off; scaling needs a reuse strategy.
