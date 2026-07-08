# Coding standards

Conventions for the engine. Kept short; the code is the reference.

## Language & build

- **C++17**, compiled with MinGW-w64 g++ under `-Wall -Wextra
  -Wno-unused-parameter`. Keep it warning-clean.
- No exceptions and no RTTI in the hot path. Errors are reported to `stderr`
  with `fprintf` and surfaced as `false` / `nullptr` return values — never
  `throw` or `abort()` in engine code.
- Only `engine/platform/platform_sdl3.cpp` includes SDL headers. The rest of the
  engine talks to the platform through `engine/platform/platform.h`.

## Headers as contract

- Public headers are the frozen interface: types, function contracts, and the
  authenticity rationale live there. Implementations follow the header exactly.
- Include paths are project-root-relative: `#include "engine/renderer/prim.h"`.

## Fixed-point & math conventions

- Fixed point is **4.12** (`FX_ONE = 4096`). Angles use **4096 = one full turn**.
- World scale: **1.0 m = 256 engine units** (`WORLD_SCALE`).
- Coordinate system: **x right, y DOWN, z into the screen**. Screen y grows
  down. Keep this in mind for lighting, culling winding, and camera math.
- Matrix/vector multiplies accumulate in `i64` and truncate `>>12`. The
  truncation is intentional (authentic precision loss) — do not "fix" it with
  rounding.

## Numeric discipline

- **No `float`/`double` in per-vertex or per-pixel paths.** Floating point is
  allowed only for one-time setup (e.g. building the sine table, computing the
  projection distance from FOV once per frame) and in the explicitly
  debug-only perspective-correct / bilinear paths.
- Everything gameplay- and render-visible must be **deterministic**: no
  `rand()`, no wall-clock in logic. The PS1 wobble comes from quantization, not
  randomness.
- Bounds-check every index derived from file data or screen coordinates before
  use. Loaders validate sizes before reading; rasterization clips to the
  framebuffer.

## Style

- Comments are sparse and explain *why* (usually a PS1 authenticity constraint),
  not *what*. Match the surrounding density.
- Types: `u8/u16/u32/u64` and `i8/i16/i32/i64` (from `engine/core/types.h`).
- Prefer plain structs and free functions (`Module_Verb`) over class hierarchies;
  the renderer is data-oriented and primitive-driven.
- Large per-frame buffers (framebuffer, render context/arena) are static/global,
  never stack-allocated.
