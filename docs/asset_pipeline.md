# Asset pipeline

Source art → engine-native binaries. Tools are Python 3 + Pillow (spec §12).
The binary layouts they emit are pinned in [file_formats.md](file_formats.md);
the C++ loaders in `engine/assets/assets.cpp` read the same contract.

## One-shot build

```powershell
python tools/build_assets.py
# or: .\build.ps1 -Assets
```

`build_assets.py` is the orchestrator. It:

1. Runs `tools/gen_source_assets.py` to (re)generate the procedural source
   textures/models/audio under `source_assets/`.
2. Reads `tools/assets_config.json` (the asset list).
3. Packs all textures through a single shared VRAM allocator, converts meshes
   and levels, copies audio, and writes `build/assets/manifest.bin`.
4. Prints a summary table with **VRAM usage / 704 KiB** and **audio / 512 KiB**.
   Any budget overflow or bad input fails the build (exit 1).

Output tree: `build/assets/{textures,meshes,levels,audio}/` + `manifest.bin`.

## Tools

| Tool | Role |
|---|---|
| `tools/common/psx_formats.py` | shared binary writers + `VramAllocator` (shelf packer, enforces the 704 KiB budget and the `x ≥ 320` texture region) |
| `texture_importer` | PNG → `.texbin` (4/8-bit CLUT or 15-bit direct), packs into VRAM pages, emits `.json` metadata |
| `palette_tool` | build/show shared `.pal` CLUT files (median-cut over one or more PNGs) |
| `mesh_importer` | OBJ → `.meshbin` (tris + quads; y-flip to engine space; UV → texel coords; optional per-face normals for lighting) |
| `level_packer` | level JSON → `.lvlbin` (meters→units, degrees→PS1 angles, y-up→y-down) |
| `asset_manifest_builder` | asset list → `manifest.bin` (runtime load order: textures, meshes, sounds, levels) |

## `tools/assets_config.json`

```jsonc
{
  "textures": [
    { "name": "floor_tiles_01", "source": "source_assets/textures/floor_tiles_01.png",
      "format": "indexed_8" },
    { "name": "orb_glow", "source": "source_assets/textures/orb_glow.png",
      "format": "indexed_4", "transparent": true, "semitrans": true }
  ],
  "meshes": [
    { "name": "room", "source": "source_assets/models/room.obj", "lit": true,
      "materials": { "floor": "floor_tiles_01", "wall": "wall_panel_01" } },
    { "name": "crate", "source": "source_assets/models/crate.obj", "lit": true,
      "texture": "crate_01" }
  ],
  "sounds": [
    { "name": "blip",     "source": "source_assets/audio/blip.wav" },
    { "name": "ambience", "source": "source_assets/audio/ambience_loop.wav",
      "music": true, "loop": true }
  ],
  "levels": [
    { "name": "test_chamber", "source": "game/levels/test_chamber.json" }
  ]
}
```

- `format`: `indexed_4` (16-color CLUT), `indexed_8` (256-color), `direct_15`.
- `transparent`: reserve palette index 0 as the fully-transparent texel for
  pixels with alpha < 128.
- `semitrans`: set the STP bit so the texture blends when drawn on a
  semi-transparent primitive.
- Meshes use either a single `texture` or a `materials` map from OBJ `usemtl`
  names to texture names. `lit: true` keeps per-face normals for vertex lighting.
- Sounds: `music: true` exempts a sample from the 512 KiB SFX budget (it stands
  in for CD streaming); `loop: true` loops from the start.

## Budgets (enforced, spec §6.1 / §9)

- **VRAM**: 704 KiB usable (1024×512 halfwords minus the reserved `x < 320`
  display columns). Textures must be power-of-two, ≤ 256×256.
- **Audio**: 512 KiB simulated sound RAM for non-music samples. Accepted WAV
  rates are 11025 / 22050 / 44100 Hz, PCM 8/16-bit, mono or stereo (downmixed).

## Adding an asset

1. Drop the source file under `source_assets/` (or add it to
   `gen_source_assets.py` to keep it reproducible).
2. Add an entry to `tools/assets_config.json`.
3. `python tools/build_assets.py` — check it fits the budget in the summary.
4. Reference it by `name` from a level JSON (`game/levels/*.json`) or from code.
