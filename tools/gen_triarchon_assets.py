#!/usr/bin/env python3
"""TRIARCHON enemy (see triarchon-design memory). Black triangle predator,
built from fixed 0.42 m equilateral triangles via tri_build.

Stage 1 (this file, first pass): FODDER crawler -- a low arachnid: an
octahedron thorax, six splayed antiprism-tube legs, a small head, and the
signature red triangular eye.

Emits per stage: triar<N>.obj (black body) + triar<N>_eye.obj (red eye) and a
viewer rig game/anims/triar<N>.json.
"""

import json
import math
import os

from tri_build import TriBuild, _add, _mul, _norm, _sub, _cross

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, "source_assets", "models")
ANIM = os.path.join(ROOT, "game", "anims")

E = 0.42


def eye_obj(path, center, forward, size):
    """One red equilateral triangle standing upright, facing `forward`."""
    fwd = _norm(forward)
    up = (0.0, 1.0, 0.0)
    right = _norm(_cross(up, fwd))
    h = size * math.sqrt(3) / 2.0
    a = _add(center, _add(_mul(right, -size / 2), _mul(up, -h / 3)))
    b = _add(center, _add(_mul(right, size / 2), _mul(up, -h / 3)))
    c = _add(center, _mul(up, 2 * h / 3))
    n = _norm(_cross(_sub(b, a), _sub(c, a)))
    with open(path, "w") as f:
        f.write("mtllib triar.mtl\nusemtl eye\n")
        for p in (a, b, c):
            f.write("v %.4f %.4f %.4f\n" % p)
        f.write("vn %.4f %.4f %.4f\n" % n)
        f.write("f 1//1 2//1 3//1\n")


def build_stage1():
    b = TriBuild(E=E)
    body_y = 0.62
    b.octahedron((0.0, body_y, 0.0))                 # thorax
    b.octahedron((0.0, body_y + 0.10, 0.34), a=E / math.sqrt(2) * 0.8)  # head

    # Six legs: splayed out and down to the floor (skip the front for the head).
    for az in (35, 90, 145, 215, 270, 325):
        a = math.radians(az)
        axis = _norm((math.cos(a) * 0.85, -0.62, math.sin(a) * 0.85))
        start = (math.cos(a) * 0.18, body_y - 0.05, math.sin(a) * 0.18)
        b.tube(start, axis, 3, twist0=az)

    # rest on the floor, centre x/z
    (mnx, mny, mnz), (mxx, mxy, mxz) = b.bounds()
    b.tris = [tuple((p[0] - 0.5 * (mnx + mxx), p[1] - mny, p[2] - 0.5 * (mnz + mxz))
                    for p in t) for t in b.tris]
    return b, body_y - mny


def write_rig(stage, eye_bone_pos):
    rig = {
        "rig": {"name": "triar%d" % stage, "bones": [
            {"name": "body", "parent": -1, "mesh": "triar%d" % stage,
             "pos": [0, 0.02, 0]},
            {"name": "eye", "parent": "body", "mesh": "triar%d_eye" % stage,
             "pos": [0, 0, 0]},
        ]},
        "clips": [{
            "name": "triar%d_idle" % stage, "loop": True, "key_ms": 480,
            "keys": [
                {"body": {"pos": [0, 0, 0]}, "eye": {"pos": [0, 0, 0]}},
                {"body": {"pos": [0, 0.03, 0]}, "eye": {"pos": [0, 0, 0]}},
                {"body": {"pos": [0, 0, 0]}, "eye": {"pos": [0, 0, 0]}},
                {"body": {"pos": [0, 0.03, 0]}, "eye": {"pos": [0, 0, 0]}},
            ],
        }],
    }
    with open(os.path.join(ANIM, "triar%d.json" % stage), "w",
              encoding="utf-8") as f:
        json.dump(rig, f, indent=1)


def main():
    os.makedirs(MOD, exist_ok=True)
    os.makedirs(ANIM, exist_ok=True)
    b, _ = build_stage1()
    b.save(os.path.join(MOD, "triar1.obj"), "triar.mtl", "body")
    (mnx, mny, mnz), (mxx, mxy, mxz) = b.bounds()
    # eye on the head front (head was near +z, top of the stack)
    eye_obj(os.path.join(MOD, "triar1_eye.obj"),
            (0.0, mxy - 0.16, mxz - 0.02), (0, -0.2, 1.0), 0.16)
    write_rig(1, None)
    print("triar1: %d triangles, extent %.2f x %.2f x %.2f m"
          % (b.count(), mxx - mnx, mxy - mny, mxz - mnz))


if __name__ == "__main__":
    main()
