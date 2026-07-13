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

- **Water reference textures** — the source images behind `arena_water_teal`,
  `arena_water_dark`, `arena_water_clouds` and `arena_water_light`. These are
  the arena's water surface, so a fresh clone renders the arena without its
  intended water look. Replacing them with procedurally generated water (or
  art with a known licence) is the way to close that gap.
- `girl` / `girl_diffuse`, `grid_floor`, `floor` — origin not established.

If you know the source of any of these, add it above and remove it from this
section. If you intend to publish this project, either replace them or clear
their licensing first.
