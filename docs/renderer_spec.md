# Renderer implementation notes

How this engine reproduces each PS1-era rendering rule. Pipeline (spec §4.1):

```
scene objects -> GTE-style transform -> primitive packets
             -> ordering table (depth buckets) -> software rasterizer
             -> 320x240 framebuffer (dither + 15-bit quantize)
             -> nearest-neighbor upscale -> window
```

## Fixed-point transform (GTE-style) — `engine/math/`

- Model vertices are `i16` (`SVec`, PS1 `SVECTOR`). Rotation matrices are 3×3 of
  `i16` in 4.12 fixed point; translation is `i32` in engine units (1.0m = 256).
- Every matrix multiply accumulates in `i64` then **truncates `>>12`** — no
  rounding. This deliberate precision loss is the source of the authentic
  transform wobble (spec §5.6, Risk 2).
- `Gte_RotMatrix` composes `Rz·Ry·Rx` (column-vector convention). Sine/cosine
  come from a 4096-entry table (4096 units = one turn), built once at startup;
  `sinf` is used only to fill the table, never per-vertex.
- `Gte_RotTransPers` projects: `sx = ofx + (vx·h)/vz`, integer-divided by
  view-space `z`. The result carries `EffectiveSubpix()` fractional bits, so the
  divide **is** the vertex snapping (0 subpixel bits = snap to whole pixels).
  Vertices with `vz <= 0` are flagged; the caller rejects the whole polygon
  (crude PS1-style near clipping — no vertex-level clip).

## Primitives & ordering table — `engine/renderer/prim.h`, `render.cpp`

- The renderer is **primitive-driven**: `Rc_DrawMesh` transforms a mesh, culls,
  lights, fogs, and emits `Prim` packets into a per-frame arena.
- **Quads split into two triangles** at emission — vertices `(0,1,2)` and
  `(1,3,2)` — exactly as the PS1 GPU did. The diagonal seam on warped/gouraud
  quads is an intended artifact (spec §7.3).
- **Culling**: backface via `Gte_NClip` (screen-space cross product; `> 0` is
  front-facing with our y-down convention) unless the face is double-sided;
  plus near/far rejection and a `>1023×511` screen-size reject (GPU limit).
- **Ordering table**: 4096 buckets. Depth key `otz = (avg view-space z) >> 4`,
  plus a per-primitive `sort_bias`, clamped to `[0, 4095]`. Insertion is
  push-front within a bucket (PS1 `AddPrim` order). `Rc_Flush` walks buckets
  far→near and rasterizes — painter's algorithm, **no z-buffer by default**.
  Intersecting geometry mis-sorts on purpose (spec §5.5).
- A debug **z-buffer** (F6) and **draw-order visualization** (F9, tints
  primitives blue→red by draw index) are available but off by default.

## Rasterizer — `engine/renderer/raster.cpp`

- Scanline edge-walking. Coords arrive in subpixel units, converted to 16.16.
  Fill rule: a pixel is covered when its **center** lies in `[xl, xr)` on a
  scanline and the row in `[y_top, y_bot)` — no double-drawn shared edges, no
  gaps between adjacent triangles; deterministic draw order.
- **Affine UV**: `u`, `v` (and `r,g,b`, `z`) interpolate linearly in screen
  space. Perspective-correct interpolation (F5) is a debug path only.
- **Sampling**: nearest-neighbor; UVs wrap by power-of-two mask (texture sizes
  are enforced pow2 ≤ 256). Bilinear (F8) is debug only.
- **Texture transparency**: a texel value of `0x0000` is fully transparent and
  skipped. Texels with the STP bit (`0x8000`) blend when the primitive is
  semi-transparent; otherwise they draw opaque.
- **Modulation**: `out = (texel8 · vertexColor8) >> 7` per channel (128 = 1.0),
  matching PS1 texture-blend.
- **Semi-transparency** modes 0–3 (`B/2+F/2`, `B+F`, `B-F`, `B+F/4`) read the
  current framebuffer pixel and blend before the quantize step.
- Sprites (`SPRT`) draw 1:1 texel-to-pixel (no scaling — PS1 sprites weren't
  scaled; billboards are textured quads instead). Tiles are flat rects. Lines
  are Bresenham. Wireframe mode draws triangle edges as lines.

## Color: VRAM, quantization, dither — `vram.h`, `framebuffer.h`

- **Simulated VRAM** is 1024×512 halfwords (1 MiB). Columns `x < 320` are
  reserved (notional display buffers, like a real PS1); textures and CLUTs live
  at `x ≥ 320`, giving a 704 KiB texture budget that the importer enforces.
- Colors are 15-bit `BGR555` + STP bit. `Vram_Texel` handles 4-bit and 8-bit
  CLUT indirection and 15-bit direct reads.
- The framebuffer stores XRGB8888 for cheap presentation, but every write goes
  through the PS1 pipeline: **ordered dither** (the psx-spx 4×4 kernel, scaled
  by `dither_strength`) → **quantize to 5 bits/channel** → expand back to 8.
  So the *content* is genuine 15-bit color with visible banding/dither. Both
  steps are individually toggleable (F3/F4).

## Fog & lighting — `render.cpp`

- **Fog** (spec §5.9) blends vertex color toward the fog color by the GTE depth
  factor `p` (0 at fog-start, 4096 at fog-end), computed at the low internal
  resolution and dithered with everything else.
- **Lighting** (spec §5.8) is vertex-based: one directional light plus ambient,
  `I = clamp(-dot(normal, lightDir))`, applied per vertex before modulation.
  No PBR, no shadow maps, no real-time GI. Gouraud interpolates the result
  across the triangle; flat uses the first vertex.

## Presentation — `platform_sdl3.cpp`

The internal framebuffer uploads to one streaming SDL texture with
`SDL_SCALEMODE_NEAREST`, presented per `scale_mode`: integer (largest whole
multiple, pillar/letterboxed), fit (aspect-preserving), or stretch. The 3D
scene is never rendered at desktop resolution and pixelated afterward.
