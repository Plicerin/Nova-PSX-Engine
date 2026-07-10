#!/usr/bin/env python3
"""Manifest builder: assets_config.json -> build/assets/manifest.bin ('PXMF').

Record order matches the runtime load order: all textures, then meshes, then
sounds, then levels (levels are enumerated only; loaded on demand). Paths are
relative to the project root with forward slashes. file_bytes is the size of
the already-built output file, so the pipeline must run before this tool.
"""

import argparse
import json
import os
import sys

_TOOLS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _TOOLS_DIR not in sys.path:
    sys.path.insert(0, _TOOLS_DIR)

from common import psx_formats as pf
from anim_compiler.anim_compiler import rig_clip_names

TEX_DIR = "build/assets/textures"
MESH_DIR = "build/assets/meshes"
AUDIO_DIR = "build/assets/audio"
LEVEL_DIR = "build/assets/levels"
RIG_DIR = "build/assets/rigs"
ANIM_DIR = "build/assets/anims"


def manifest_records(config, root):
    """Build the record list (types 0..3 in textures/meshes/sounds/levels
    order) from the asset config, verifying built files exist."""
    records = []

    def add(atype, name, relpath, is_music=0, loop_whole=0):
        full = os.path.join(root, relpath.replace("/", os.sep))
        if not os.path.isfile(full):
            raise pf.PackError("manifest: built asset missing: %s "
                               "(run tools/build_assets.py first)" % full)
        records.append({
            "type": atype,
            "is_music": is_music,
            "loop_whole": loop_whole,
            "name": name,
            "path": relpath,
            "file_bytes": os.path.getsize(full),
        })

    for t in config.get("textures", []):
        add(pf.ASSET_TEXTURE, t["name"], "%s/%s.texbin" % (TEX_DIR, t["name"]))
    for m in config.get("meshes", []):
        add(pf.ASSET_MESH, m["name"], "%s/%s.meshbin" % (MESH_DIR, m["name"]))
    for s in config.get("sounds", []):
        add(pf.ASSET_SOUND, s["name"], "%s/%s.wav" % (AUDIO_DIR, s["name"]),
            is_music=1 if s.get("music", False) else 0,
            loop_whole=1 if s.get("loop", False) else 0)
    for l in config.get("levels", []):
        add(pf.ASSET_LEVEL, l["name"], "%s/%s.lvlbin" % (LEVEL_DIR, l["name"]))
    for r in config.get("rigs", []):
        add(pf.ASSET_RIG, r["name"], "%s/%s.rigbin" % (RIG_DIR, r["name"]))
        for cname in rig_clip_names(r, source_root=root):
            add(pf.ASSET_ANIM, cname, "%s/%s.animbin" % (ANIM_DIR, cname))
    return records


def build_manifest(config, root, out_path):
    """Write manifest.bin; returns the record list."""
    records = manifest_records(config, root)
    out_dir = os.path.dirname(os.path.abspath(out_path))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    pf.write_manifest(out_path, records)
    return records


def main(argv=None):
    default_root = os.path.dirname(_TOOLS_DIR)
    ap = argparse.ArgumentParser(
        description="Build manifest.bin from the asset config (run after "
                    "the asset pipeline has produced the binaries).")
    ap.add_argument("--config", default=os.path.join(_TOOLS_DIR,
                                                     "assets_config.json"),
                    help="asset config JSON (default: %(default)s)")
    ap.add_argument("--root", default=default_root,
                    help="project root the manifest paths are relative to "
                         "(default: %(default)s)")
    ap.add_argument("--out", default=None,
                    help="output path (default: <root>/build/assets/"
                         "manifest.bin)")
    args = ap.parse_args(argv)

    out_path = args.out or os.path.join(args.root, "build", "assets",
                                        "manifest.bin")
    try:
        with open(args.config, "r", encoding="utf-8") as f:
            config = json.load(f)
        records = build_manifest(config, args.root, out_path)
    except (pf.PackError, OSError, ValueError, KeyError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    print("wrote %s: %d records" % (out_path, len(records)))
    for r in records:
        print("  type %d  %-24s %-44s %8d bytes"
              % (r["type"], r["name"], r["path"], r["file_bytes"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
