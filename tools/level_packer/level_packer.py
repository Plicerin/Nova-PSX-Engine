#!/usr/bin/env python3
"""Level packer: level .json (spec 12.4) -> .lvlbin ('PXLV').

Source JSON is authored in float units / degrees / y-up; the engine wants
integer engine units / PS1 angle units / y-down. Conversions:
  - positions * 256 -> i32, with Y NEGATED (JSON y-up -> engine y-down)
  - rotations: degrees -> PS1 units (deg * 4096 / 360) with the y-flip
    conjugation (rx, ry, rz) -> (-rx, ry, -rz)
  - fog start/end * 256 -> i32
  - light direction: y negated, normalized to 4.12
  - scale floats -> 4.12 (4096 = 1.0); scalar or [sx,sy,sz]
Defaults when fields are missing: fog disabled, light disabled, clear color
black, camera at origin, scale 1. Colors are 0..255 ints.

Accepted JSON shape:
{
  "name": "level01",
  "camera_start": {"position": [x,y,z], "rotation": [rx,ry,rz]},   // degrees
  "fog":   {"enabled": true, "color": [r,g,b], "start": f, "end": f},
  "light": {"enabled": true, "ambient": [r,g,b], "diffuse": [r,g,b],
            "direction": [x,y,z]},
  "clear_color": [r,g,b],
  "objects": [{"mesh": "name", "position": [x,y,z],
               "rotation": [rx,ry,rz], "scale": 1.0 | [sx,sy,sz]}]
}
"""

import argparse
import json
import math
import os
import sys

_TOOLS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _TOOLS_DIR not in sys.path:
    sys.path.insert(0, _TOOLS_DIR)

from common import psx_formats as pf


# --- conversions -----------------------------------------------------------------

def _pos_to_engine(p, what):
    """[x,y,z] floats -> engine i32 (units * 256, y negated)."""
    return (pf.check_i32(int(round(p[0] * pf.WORLD_SCALE)), what + ".x"),
            pf.check_i32(int(round(-p[1] * pf.WORLD_SCALE)), what + ".y"),
            pf.check_i32(int(round(p[2] * pf.WORLD_SCALE)), what + ".z"))


def _deg_to_psx(deg):
    """Degrees -> PS1 angle units in [0, 4096)."""
    return int(round(deg * pf.PSX_FULL_TURN / 360.0)) % pf.PSX_FULL_TURN


def _rot_to_engine(r):
    """Degrees (rx,ry,rz) -> PS1 units with y-flip conjugation (-rx,ry,-rz)."""
    return (_deg_to_psx(-r[0]), _deg_to_psx(r[1]), _deg_to_psx(-r[2]))


def _col3(v, default=(0, 0, 0)):
    if v is None:
        v = default
    return (pf.clamp_u8(int(round(v[0]))), pf.clamp_u8(int(round(v[1]))),
            pf.clamp_u8(int(round(v[2]))))


def _dir_to_fx12(d):
    """Light direction: y negated, normalized to 4.12."""
    x, y, z = float(d[0]), -float(d[1]), float(d[2])
    length = math.sqrt(x * x + y * y + z * z)
    if length == 0.0:
        return (0, pf.FX12_ONE, 0)     # straight down in engine space
    return (int(round(x * pf.FX12_ONE / length)),
            int(round(y * pf.FX12_ONE / length)),
            int(round(z * pf.FX12_ONE / length)))


def _scale_to_fx12(s):
    """Scalar or [sx,sy,sz] floats -> fx12 i32[3]."""
    if s is None:
        s = 1.0
    if isinstance(s, (int, float)):
        s = [s, s, s]
    return tuple(pf.check_i32(int(round(float(c) * pf.FX12_ONE)), "scale")
                 for c in s)


# --- packing ----------------------------------------------------------------------

def pack_level(json_path, out_path):
    """Pack one level JSON; returns a small summary dict."""
    if not os.path.isfile(json_path):
        raise pf.PackError("level source not found: %s" % json_path)
    with open(json_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    name = data.get("name") or os.path.splitext(os.path.basename(json_path))[0]

    cam = data.get("camera_start") or {}
    cam_pos = _pos_to_engine(cam.get("position", [0.0, 0.0, 0.0]),
                             "camera.position")
    cam_rot = _rot_to_engine(cam.get("rotation", [0.0, 0.0, 0.0]))

    fog_src = data.get("fog") or {}
    fog_col = _col3(fog_src.get("color"))
    fog_start = fog_src.get("start", fog_src.get("start_z", 0.0))
    fog_end = fog_src.get("end", fog_src.get("end_z", 0.0))
    fog = {
        "enabled": 1 if fog_src.get("enabled", False) else 0,
        "r": fog_col[0], "g": fog_col[1], "b": fog_col[2],
        "start": pf.check_i32(int(round(float(fog_start) * pf.WORLD_SCALE)),
                              "fog.start"),
        "end": pf.check_i32(int(round(float(fog_end) * pf.WORLD_SCALE)),
                            "fog.end"),
    }

    light_src = data.get("light") or {}
    light = {
        "enabled": 1 if light_src.get("enabled", False) else 0,
        "ambient": _col3(light_src.get("ambient")),
        "diffuse": _col3(light_src.get("diffuse")),
        "dir": _dir_to_fx12(light_src.get("direction", [0.0, -1.0, 0.0])),
    }

    clear_color = _col3(data.get("clear_color"))

    objects = []
    for i, o in enumerate(data.get("objects") or []):
        if "mesh" not in o:
            raise pf.PackError("level '%s': objects[%d] missing \"mesh\""
                               % (name, i))
        objects.append({
            "mesh": o["mesh"],
            "pos": _pos_to_engine(o.get("position", [0.0, 0.0, 0.0]),
                                  "objects[%d].position" % i),
            "rot": _rot_to_engine(o.get("rotation", [0.0, 0.0, 0.0])),
            "scale": _scale_to_fx12(o.get("scale")),
        })

    out_dir = os.path.dirname(os.path.abspath(out_path))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    pf.write_lvlbin(out_path, name=name, cam_pos=cam_pos, cam_rot=cam_rot,
                    fog=fog, light=light, clear_color=clear_color,
                    objects=objects)
    return {
        "name": name,
        "source": json_path,
        "file": out_path,
        "file_bytes": os.path.getsize(out_path),
        "nobjects": len(objects),
        "fog": bool(fog["enabled"]),
        "light": bool(light["enabled"]),
    }


# --- CLI ------------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Pack a level .json (floats/degrees/y-up, spec 12.4) "
                    "into an engine .lvlbin.")
    ap.add_argument("--in", dest="src", required=True, help="source level.json")
    ap.add_argument("--out", required=True, help="output .lvlbin path")
    args = ap.parse_args(argv)
    try:
        s = pack_level(args.src, args.out)
    except pf.PackError as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    print("wrote %s (%d bytes): %d objects, fog %s, light %s"
          % (s["file"], s["file_bytes"], s["nobjects"],
             "on" if s["fog"] else "off", "on" if s["light"] else "off"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
