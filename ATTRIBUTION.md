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

- **Water reference textures.** Each engine texture derives from one source
  image (matched by luminance-structure correlation):

  | Engine texture | Source image | In use? |
  |---|---|---|
  | `arena_water_teal` | **`water.jpg`** | **yes — this is the arena's water** |
  | `arena_water_clouds` | `water_128px.gif` | no |
  | `arena_water_light` | `light_water.jpg` | no |
  | `arena_water_dark` | `dark_water.jpg` | no |

  The originals were dropped in from outside the repo and no author or licence
  could be established for any of them. `water.jpg` is the one that matters:
  it became the arena's water surface, so a fresh clone renders the arena
  without its intended look. Closing that gap means generating the water
  procedurally (everything else in the arena already is — see
  `tools/gen_arena_assets.py`, which has a `tex_water()` caustics generator
  the mesh does not currently use) or swapping in art with a known licence.

- `girl` / `girl_diffuse`, `grid_floor`, `floor` — origin not established.

If you know the source of any of these, add it above and remove it from this
section. If you intend to publish this project, either replace them or clear
their licensing first.
