# Nova Engine — Assessment

_Last updated: 2026-07-19. Engine-focused companion to `production_status.md` (which
covers game systems/content). Scope: the ~4,500-LOC engine under `engine/` — graphics,
sound, art pipeline, performance, features, strengths/weaknesses, easy wins, docs._

## Feature list (what the engine can do)

**Graphics / renderer** (`engine/renderer/`, `engine/math/`)
- Fixed-point GTE-style transform (4.12, i64-accumulate-truncate → authentic wobble); 4096-entry sin/cos table; no float in the vertex path.
- Primitive-driven pipeline: `Rc_DrawMesh` → cull → light → fog → `Prim` packets → 4096-bucket ordering table → scanline rasterizer.
- Affine texture mapping, nearest sampling, pow2 UV wrap; painter's sort (no Z by default).
- Per-vertex lighting: 1 directional + ambient, an optional colored **fill** light, and up to 4 **colored point lights** with linear falloff (bounding-sphere culled, applied additively).
- Depth fog; 4×4 ordered dither + 15-bit BGR555 quantize; 4 semi-transparency blend modes; STP-bit transparency.
- Primitives: tris/quads (auto-split), flat tiles, 1:1 sprites, Bresenham lines, camera-facing billboards, wireframe.
- PS1.5 opt-ins: additive Blinn specular, low-res bloom.
- Backface + near/far + 1023×511 size rejection.
- Internal resolutions (320×240 default … 640×480, plus a 320×180 16:9) with nearest integer/fit/stretch upscale.
- Debug/authenticity toggles (F-keys): dither, quantize, snap, perspective-correct, bilinear, z-buffer, draw-order viz — perspective/bilinear/Z are intentionally debug-only.

**FX** (`game/src/fx.cpp`) — deterministic zero-alloc pool: impact bursts, water splash, screen shake, ambient motes, transient colored lights. Bone-attachable (`Anim_BoneWorld`).

**Animation** (`engine/anim/`) — rigid-part skeletal (no vertex skinning): ≤32 bones, hierarchical, per-bone hinge frame (`bind_rot`) for arbitrary fold axes; keyframe clips with shortest-path angle lerp; `Anim_BoneWorld` for attaching FX to bones.

**Audio** (`engine/audio/`) — 24-voice software mixer, 44.1 kHz stereo, per-voice volume/pan/pitch, one-shot + looping, mutex-safe, 512 KB SFX budget. WAV/PCM (8/16-bit, mono/stereo→mono, 11.025/22.05/44.1 kHz).

**Assets / pipeline** (`engine/assets/`, `tools/`) — one-command deterministic build from a single JSON manifest; texture importer (4/8-bit CLUT + 15-bit direct, median-cut quantize, VRAM shelf-packer, 704 KB budget); OBJ mesh import + one-way GLB→OBJ; rig/anim compiler; level packer (fog + lights + objects); self-verifying procedural source generators.

**Input** (`engine/input/`) — PS1 pad abstraction, keyboard+gamepad merge, stick-as-dpad, edge handling across multi-tick frames, rebinding API + config-file parser.

**Debug/tooling** (`engine/debug/`) — 8×8 bitmap text, in-engine **profiler overlay** (FPS, frame ms, prim/tri counts, arena/VRAM/page usage, resolution), free-fly camera, frame step/pause, screenshots, draw-order viz. CLI demo flags act as runnable examples (`--combat`, `--dialog`, `--show-rig`, `--show-morph`, `--evolver-tier`, `--combat-cam`, …).

## Performance (measured, via the debug overlay)

Render time is the engine's `pump→present` cost; the on-screen FPS is capped by the
**remote-desktop screen encoder**, not the engine (locally it's VSync 60 with headroom).

| Scene | Frame time | Prims | Tris | Notes |
|---|---|---|---|---|
| Morph creature (void) | **0.3 ms** | 219 | 219 | ~3000 fps of headroom |
| Big creature (evolver-5) in arena | **1.8 ms** | 2,773 | 2,745 | full arena + water |
| Full combat (arena + animated water + 3 fighters + FX) | **4.1 ms** | 5,060 | 5,060 | ~240 fps-equivalent |

Utilization at the heaviest scene: **prim arena 5,060 / 65,536 (8%)**, **VRAM 191 KB / 704 KB
(27%)**, **texture pages 18 / 32**, culled 0. The engine is **far from any limit** — there's
room for several times more geometry, textures, and effects before optimization matters.

**Takeaway:** performance is a strength, not a risk. Any perceived slowness is the RDP
display path. No profiling/optimization work is warranted now; the headroom should be
spent on content and effects, not micro-optimization.

## Strengths

- **Authentic and complete PS1 rendering** — affine warp, OT painter's sort, dither+15-bit, blend modes, vertex lighting incl. colored point lights, specular + bloom as opt-in PS1.5.
- **Clean, disciplined codebase** — ~4,500 LOC, **zero TODO/FIXME**, warning-clean C++17, no exceptions/RTTI in hot paths, no float in vertex/pixel paths, deterministic (no `rand()`/wall-clock in logic), headers-as-contract.
- **Fast with large headroom** (see above).
- **Solid asset pipeline** — one command, single manifest, hard-enforced VRAM/audio budgets, deterministic + self-verifying source generation, clean binary-format contract mirrored by C++ loaders.
- **Production-grade input** and a genuinely useful **debug/profiler overlay** built in.
- **Procedural geometry library** (`tri_build`) that already generates whole creature families; **GLB import** for external art.
- **Good documentation for what's documented** — `renderer_spec`, `asset_pipeline`, `file_formats`, `coding_standards` are concise and accurate.

## Weaknesses

- **No shadows** of any kind — characters float; the single biggest visual gap.
- **Audio effectively unused** — the mixer is solid but the game ships one blip + one drone; **no music streaming**, no crossfade, no combat/UI SFX.
- **No texture dithering in the importer** (hard-coded off) — gradients band; ironic given the runtime dithers.
- **GLB import is one-way and lossy** — discards skins and animations; no Blender/DCC round-trip.
- **No reusable UI framework** — every screen is hand-rolled `FillRect` + `Debug_Text`; one font, one size.
- **Content is bespoke one-offs** — no prop kit / reusable static-mesh library / asset browser.
- **Third-party assets don't regenerate on a clean clone** — `rig_split_*` scripts have hardcoded `Downloads` paths and aren't orchestrated; a clone silently skips ASTRA/UNIT-7/girl.
- **Docs reference a master "spec §…"** that isn't in the repo — the numbered design spec the docs cite is missing.

## Easy improvements / added features (low effort, high value)

Ordered by value-for-effort. All are **S** (hours–1 day) unless noted.

| Improvement | Why it matters | Effort |
|---|---|---|
| **Blob shadows** (dark billboard/ground quad under actors) | Biggest visual win; grounds every character; uses existing `Rc_DrawBillboard`/`Rc_AddTile` | S |
| **Texture dithering** in the importer (Bayer/Floyd–Steinberg pre-quantize) | Kills gradient banding; a config flag + a few lines | S |
| **Screen transitions** (fade-to-black via a full-screen semi-transparent tile) | Needed for every scene change; ~1 function + timer | S |
| **Combat + UI SFX hookup** | Mixer is ready; game is silent; add a small sound catalog + `Audio_Play` at event sites | S–M |
| **Master volume / mute** | Basic expectation; localized change in `audio.cpp` | S |
| **Call `Input_LoadBindings`** (rebind file loader exists but is never invoked) | Unlocks key rebinding for free | S |
| **Prune stale build outputs** (clean target dirs or manifest-diff) | 5 stale textures / 13 stale levels linger in `build/assets/` | S |
| **Enforce tri budget + per-level VRAM report** | Currently only a warning; catch overruns early | S |
| **Fix third-party regen** (parameterize `rig_split_*` paths; orchestrate or commit derived OBJs) | Clean-clone reproducibility | S–M |
| **Camera lerp helper** (fixed-point pos/rot ease between shots) | Turns hard cuts into moves; reusable by dialog/combat | M |
| **Add the missing master design spec** (or renumber docs to be self-contained) | Docs cite sections that don't exist in-repo | S |
| **Positional/attenuated SFX** (`Audio_Play` at a world point) | Cheap depth for combat audio; mixer already has pan/vol | S–M |

## Documentation & examples

- **Design docs (4):** `renderer_spec.md`, `asset_pipeline.md`, `file_formats.md`,
  `coding_standards.md` — concise, accurate, up to date. Gap: they reference an external
  master "spec §…" not present in the repo.
- **Status docs (2):** this file + `production_status.md`.
- **Examples:** the CLI demo flags are the de-facto examples/tutorials and cover most
  systems (combat, dialogue, rig/morph viewers, evolver tiers). Gap: no "using the engine
  as a library" sample or a documented index of the demo flags; no annotated minimal scene.
- **In-engine reference:** the F-key toggles + profiler overlay are excellent live
  documentation of the renderer's authenticity features.
