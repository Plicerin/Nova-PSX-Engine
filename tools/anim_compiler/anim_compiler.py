#!/usr/bin/env python3
"""Anim compiler: one rig JSON -> <name>.rigbin + one .animbin per clip.

Input JSON (floats/degrees/meters/y-up like level JSON; spec 12.4 conventions):

{
  "rig": {
    "name": "robot",
    "bones": [
      {"name": "root",  "parent": -1, "mesh": "robot_chassis", "pos": [0, 0.9, 0]},
      {"name": "head",  "parent": 0,  "mesh": "robot_head",    "pos": [0, 0.5, 0]}
    ]
  },
  "clips": [
    {"name": "robot_idle", "loop": true, "key_ms": 400,
     "keys": [
       {},                                        # all bones at bind pose
       {"head": {"rot": [0, 25, 0]}},             # sparse overrides per key
       {"head": {"rot": [0, -25, 0], "pos": [0, 0.05, 0]}}
     ]}
  ]
}

Units: pos meters -> engine units (x256); rot degrees -> PS1 angle units
(x4096/360). Y is flipped (JSON y-up -> engine y-down), and rotations are
conjugated (rx,ry,rz) -> (-rx, ry, -rz) exactly like level_packer does for
objects, so a positive JSON yaw turns the same way in both.
"""

import argparse
import json
import os
import sys

_TOOLS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _TOOLS_DIR not in sys.path:
    sys.path.insert(0, _TOOLS_DIR)

from common import psx_formats as pf

WORLD_SCALE = 256


def _units_pos(p):
    """JSON meters y-up -> engine units y-down."""
    x, y, z = (float(v) for v in p)
    return (x * WORLD_SCALE, -y * WORLD_SCALE, z * WORLD_SCALE)


def _units_rot(r):
    """JSON degrees -> PS1 angle units, conjugated like level_packer objects."""
    rx, ry, rz = (float(v) * 4096.0 / 360.0 for v in r)
    return (-rx, ry, -rz)


def compile_rig(entry, rig_dir, anim_dir, source_root="."):
    """entry: {name, source}. Returns a summary dict with 'clips' list."""
    src = os.path.join(source_root, entry["source"])
    if not os.path.isfile(src):
        raise pf.PackError("rig '%s': source not found: %s"
                           % (entry["name"], src))
    with open(src, "r", encoding="utf-8") as f:
        data = json.load(f)

    rig = data.get("rig")
    if not rig or "bones" not in rig:
        raise pf.PackError("%s: missing rig.bones" % src)
    rig_name = rig.get("name", entry["name"])
    if rig_name != entry["name"]:
        raise pf.PackError("%s: rig name '%s' != config entry name '%s'"
                           % (src, rig_name, entry["name"]))

    bone_index = {}
    bones_out = []
    for i, b in enumerate(rig["bones"]):
        if b["name"] in bone_index:
            raise pf.PackError("%s: duplicate bone '%s'" % (src, b["name"]))
        parent = b.get("parent", -1)
        if isinstance(parent, str):
            if parent not in bone_index:
                raise pf.PackError("%s: bone '%s' parent '%s' not defined "
                                   "before it" % (src, b["name"], parent))
            parent = bone_index[parent]
        bone_index[b["name"]] = i
        bones_out.append({
            "name": b["name"],
            "mesh": b.get("mesh", ""),
            "parent": parent,
            "bind_pos": _units_pos(b.get("pos", (0, 0, 0))),
            "bind_rot": _units_rot(b.get("rot", (0, 0, 0))),
        })

    os.makedirs(rig_dir, exist_ok=True)
    os.makedirs(anim_dir, exist_ok=True)
    rig_path = os.path.join(rig_dir, rig_name + ".rigbin")
    pf.write_rigbin(rig_path, name=rig_name, bones=bones_out)

    nbones = len(bones_out)
    clips_out = []
    seen_clips = set()
    for clip in data.get("clips", []):
        cname = clip["name"]
        if cname in seen_clips:
            raise pf.PackError("%s: duplicate clip '%s'" % (src, cname))
        seen_clips.add(cname)
        key_ms = int(clip.get("key_ms", 250))
        if not 1 <= key_ms <= 65535:
            raise pf.PackError("%s: clip '%s' bad key_ms %d" % (src, cname, key_ms))
        poses = clip.get("keys")
        if not poses:
            raise pf.PackError("%s: clip '%s' has no keys" % (src, cname))

        flat = []
        for ki, pose in enumerate(poses):
            for bname in pose:
                if bname not in bone_index:
                    raise pf.PackError("%s: clip '%s' key %d references "
                                       "unknown bone '%s'"
                                       % (src, cname, ki, bname))
            for bi, b in enumerate(rig["bones"]):
                ch = pose.get(b["name"], {})
                flat.append((_units_rot(ch.get("rot", (0, 0, 0))),
                             _units_pos(ch.get("pos", (0, 0, 0)))))

        anim_path = os.path.join(anim_dir, cname + ".animbin")
        pf.write_animbin(anim_path, name=cname, rig_name=rig_name,
                         nbones=nbones, nkeys=len(poses), key_ms=key_ms,
                         loop=bool(clip.get("loop", False)), keys=flat)
        clips_out.append({"name": cname, "file": anim_path,
                          "file_bytes": os.path.getsize(anim_path),
                          "nkeys": len(poses), "key_ms": key_ms,
                          "loop": bool(clip.get("loop", False))})

    return {"name": rig_name, "file": rig_path,
            "file_bytes": os.path.getsize(rig_path),
            "nbones": nbones, "clips": clips_out}


def rig_clip_names(entry, source_root="."):
    """Clip names listed in a rig JSON (for the manifest builder)."""
    src = os.path.join(source_root, entry["source"])
    with open(src, "r", encoding="utf-8") as f:
        data = json.load(f)
    return [c["name"] for c in data.get("clips", [])]


def main(argv=None):
    ap = argparse.ArgumentParser(description="Compile a rig JSON to "
                                             ".rigbin + .animbin files.")
    ap.add_argument("--in", dest="src", required=True, help="rig JSON")
    ap.add_argument("--name", required=True, help="rig asset name")
    ap.add_argument("--rig-dir", default="build/assets/rigs")
    ap.add_argument("--anim-dir", default="build/assets/anims")
    ap.add_argument("--root", default=".")
    args = ap.parse_args(argv)
    try:
        s = compile_rig({"name": args.name, "source": args.src},
                        args.rig_dir, args.anim_dir, source_root=args.root)
    except pf.PackError as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    print("rig %s: %d bones -> %s" % (s["name"], s["nbones"], s["file"]))
    for c in s["clips"]:
        print("  clip %-24s %2d keys @ %d ms%s" %
              (c["name"], c["nkeys"], c["key_ms"],
               " loop" if c["loop"] else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
