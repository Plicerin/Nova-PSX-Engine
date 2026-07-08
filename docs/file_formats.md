# Engine-native binary formats (v1)

All files little-endian, tightly packed (no implicit alignment padding beyond
what is listed). Strings are UTF-8, NUL-padded to their fixed size.
Written by the Python tools in `/tools`, read by `engine/assets/assets.cpp`.
**Both sides must follow this document exactly.**

Conventions:
- World units: 1.0 in source assets / level JSON = **256 engine units** (`WORLD_SCALE`).
- Angles: PS1 units, 4096 = full turn. JSON degrees are converted: `a = round(deg * 4096 / 360)`.
- 4.12 fixed point: 4096 = 1.0.
- 15-bit color halfword: bit15 = STP flag, bits 0-4 = R, 5-9 = G, 10-14 = B
  (`c = (b5 << 10) | (g5 << 5) | r5`, PS1 layout). Halfword 0x0000 = transparent texel.

## VRAM layout rules (texture_importer must enforce)

- Simulated VRAM is 1024x512 halfwords. Columns x < 320 are reserved
  (display buffers); textures/CLUTs go at x >= 320.
- Texture width/height: powers of two, 8..256.
- Storage width in halfwords: 4-bit -> w/4, 8-bit -> w/2, 15-bit -> w.
- CLUTs are rows of 16 or 256 halfwords, x aligned to 16.
- The importer packs textures into 64x256-halfword page cells left-to-right,
  top-to-bottom starting at (320,0), CLUTs into a dedicated strip; it must
  never overlap allocations and must fail if the budget (704 KiB) is exceeded.

## .texbin — texture ('PXTX')

| offset | size | field |
|---|---|---|
| 0  | 4  | magic `PXTX` |
| 4  | 4  | u32 version = 1 |
| 8  | 32 | name |
| 40 | 1  | u8 format: 0=4-bit indexed, 1=8-bit indexed, 2=15-bit direct |
| 41 | 1  | pad (0) |
| 42 | 2  | u16 width (pixels) |
| 44 | 2  | u16 height |
| 46 | 2  | u16 vram_x (halfword column of top-left) |
| 48 | 2  | u16 vram_y |
| 50 | 2  | u16 clut_x (0xFFFF if none) |
| 52 | 2  | u16 clut_y (0xFFFF if none) |
| 54 | 2  | u16 clut_len (0, 16 or 256) |
| 56 | 4  | u32 vram_cost_bytes (pixel storage + clut storage) |
| 60 | 4  | u32 data_len (bytes of pixel payload) |
| 64 | 2*clut_len | CLUT halfwords |
| ...| data_len | pixel payload: row-major halfwords, storage-width per format |

Companion `.json` next to it mirrors the spec 6.3 metadata example (source,
format, size, palette name, texture_page, vram_cost_bytes) — for humans/tools.

## .meshbin — mesh ('PXMS')

Header:
| offset | size | field |
|---|---|---|
| 0  | 4  | magic `PXMS` |
| 4  | 4  | u32 version = 1 |
| 8  | 32 | name |
| 40 | 4  | u32 nverts |
| 44 | 4  | u32 nnorms (0 or == nverts) |
| 48 | 4  | u32 nprims |
| 52 | 4  | u32 ntex |
| 56 | 4  | u32 tri_count (quads count as 2) |
| 60 | 4  | i32 radius (engine units, bounding sphere from origin) |

Then, in order:
1. verts: nverts × `i16 x, i16 y, i16 z` (6 bytes each, packed)
2. norms: nnorms × `i16 x, i16 y, i16 z` (4.12, normalized)
3. tex names: ntex × char[32]
4. prims: nprims × 36-byte records:

| offset | size | field |
|---|---|---|
| 0  | 1 | u8 type: 0 F3, 1 G3, 2 FT3, 3 GT3, 4 F4, 5 G4, 6 FT4, 7 GT4 |
| 1  | 1 | u8 flags: bit0 semitrans, bits1-2 semi mode, bit3 double-sided |
| 2  | 2 | u16 tex_index (0xFFFF = none) |
| 4  | 8 | u16 vi[4] (vi[3] = 0 for tris) |
| 12 | 8 | u8 uv[4][2] (texel coords, pixels) |
| 20 | 12 | u8 rgb[4][3] (128 = neutral) |
| 32 | 2 | i16 sort_bias (added to otz) |
| 34 | 2 | pad (0) |

Quad vertex order: vi[0]=top-left, vi[1]=top-right, vi[2]=bottom-left,
vi[3]=bottom-right (PS1 convention; runtime splits as (0,1,2) + (1,3,2)).

## .lvlbin — level ('PXLV')

| offset | size | field |
|---|---|---|
| 0  | 4  | magic `PXLV` |
| 4  | 4  | u32 version = 1 |
| 8  | 32 | name |
| 40 | 4  | u32 nobjects |
| 44 | 12 | camera pos i32[3] (engine units) |
| 56 | 6  | camera rot i16[3] (PS1 angle units) |
| 62 | 2  | pad |
| 64 | 1  | fog enabled u8 |
| 65 | 3  | fog r,g,b u8 |
| 68 | 4  | i32 fog start_z (engine units) |
| 72 | 4  | i32 fog end_z |
| 76 | 1  | light enabled u8 |
| 77 | 3  | ambient r,g,b u8 |
| 80 | 3  | diffuse r,g,b u8 |
| 83 | 1  | pad |
| 84 | 6  | light dir i16[3] (4.12; loader normalizes) |
| 90 | 2  | pad |
| 92 | 3  | clear color r,g,b u8 |
| 95 | 1  | pad |

Then nobjects × 64-byte records:
| offset | size | field |
|---|---|---|
| 0  | 32 | mesh name |
| 32 | 12 | pos i32[3] |
| 44 | 6  | rot i16[3] |
| 50 | 2  | pad |
| 52 | 12 | scale fx12[3] (i32 4.12; 4096 = 1.0) — **only rotation-preserving
scales supported in v1: loader applies scale by scaling matrix columns** |

Source form is `game/levels/*.json` per spec 12.4 (floats/degrees), packed by
`level_packer`.

## manifest.bin ('PXMF')

| offset | size | field |
|---|---|---|
| 0 | 4 | magic `PXMF` |
| 4 | 4 | u32 version = 1 |
| 8 | 4 | u32 count |

Then count × 108-byte records:
| offset | size | field |
|---|---|---|
| 0   | 1  | u8 type: 0 texture, 1 mesh, 2 sound, 3 level |
| 1   | 1  | u8 is_music (sounds only) |
| 2   | 1  | u8 loop_whole (sounds only) |
| 3   | 1  | pad |
| 4   | 32 | asset name |
| 36  | 64 | path relative to project root (forward slashes) |
| 100 | 4  | u32 file_bytes |
| 104 | 4  | pad |

Load order at runtime: all textures, then meshes, then sounds. Levels are
listed for enumeration but loaded on demand via `Level_Load`.

## WAV input (source assets)

Standard RIFF PCM, 8 or 16-bit, mono or stereo (downmixed), rates 11025/22050/
44100 only. The audio tools do not resample; the mixer's zero-order-hold
resampling to 44100 is the intended authentic behavior.
