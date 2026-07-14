# Attribution

Third-party art used by this project. None of it is committed: it all lives
under `/source_assets/`, which is gitignored (see `.gitignore`). The engine,
tools, rigs, animation clips and level definitions in this repo are ours; the
models below are not.

If you clone this repo you will not get these assets. `tools/build_assets.py`
regenerates everything procedural and **skips** the rest with a report, so the
build still succeeds — you will just be missing the characters listed here.

## Models

| Asset | Author | Used for |
|---|---|---|
| `ps1__robot.glb` | **Kuptchi** | UNIT-7, the robot companion (`unit7_*` rig segments, `psrobot` mesh, `psrobot_tex`) |
| `untitled.glb` | **patriotprimeJR** | ASTRA, the player character (`astro_*` rig segments, `mecha_full` mesh) |

Both were imported with `tools/glb_import.py` and split into rigid rig segments
by `tools/rig_split_robot.py` / `tools/rig_split_mecha.py`.

## Unknown provenance — do not redistribute

The following have **no attribution we could establish**. Because the author
and licence are unknown, these must not be committed to this repo or shipped:

- `girl` / `girl_diffuse`, `grid_floor`, `floor` — origin not established.

## Resolved: the arena water

The arena's water used to come from an unattributed image. It no longer does —
`tools/gen_arena_assets.py::tex_water()` generates it procedurally, so it is
committed, reproducible, and licence-clean.

For the record, the retired textures each derived from one source image
(matched by luminance-structure correlation):

| Retired texture | Source image | Was it used? |
|---|---|---|
| `arena_water_teal` | `water.jpg` | yes — this was the arena's water |
| `arena_water_clouds` | `water_128px.gif` | no |
| `arena_water_light` | `light_water.jpg` | no |
| `arena_water_dark` | `dark_water.jpg` | no |

No author or licence could be established for any of them, so none are
committed and the three unused ones were dropped from the asset config
entirely (they were still costing ~32 KB of VRAM).

If you know the source of any of these, add it above and remove it from this
section. If you intend to publish this project, either replace them or clear
their licensing first.
