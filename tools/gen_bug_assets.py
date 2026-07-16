#!/usr/bin/env python3
"""Fodder tier prototype: an aquatic LARVA/grub built from fixed-size
equilateral triangles (see tri_build + alien-triangle-rules memory).

Anatomy, all one connected edge-to-edge assembly:
  - an arched segmented body (a folded triangle strip = a grub's humped back)
  - stubby legs (triangles folded down off the body's side edges)
  - a raised head plate + two forward antennae

Emits source_assets/models/bug_larva.obj and a 1-bone viewer rig
game/anims/bug_larva.json (idle bob) for --show-rig bug_larva.
"""

import json
import math
import os

from tri_build import TriBuild, _sub

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, "source_assets", "models")
ANIM = os.path.join(ROOT, "game", "anims")


def build_larva():
    b = TriBuild(E=0.42)
    seed = b.seed_flat(0.0, 0.0, -0.55)          # head end toward -z

    # Body: a strip that arches up and curls at the tail (a grub's segments).
    body = [seed]
    i = seed
    for k in range(7):
        fold = 20 + 3 * k                        # curls tighter toward the tail
        i = b.attach(i, 1, 2, fold)
        body.append(i)

    # Legs: short triangles folded steeply down off alternating side edges.
    for n, idx in enumerate((1, 2, 3, 4, 5)):
        edge = (0, 1) if (n % 2 == 0) else (0, 2)
        b.attach(body[idx], edge[0], edge[1], -74)

    # Head: a raised plate over the seed, then two antennae poking forward.
    head = b.attach(seed, 0, 1, 58)
    b.attach(head, 1, 2, 12)
    b.attach(head, 0, 2, 12)

    # Rest on the floor and centre on x.
    (mnx, mny, mnz), (mxx, mxy, mxz) = b.bounds()
    cx = 0.5 * (mnx + mxx)
    b.tris = [tuple((p[0] - cx, p[1] - mny, p[2]) for p in t) for t in b.tris]
    return b


def write_rig():
    rig = {
        "rig": {"name": "bug_larva",
                "bones": [{"name": "body", "parent": -1,
                           "mesh": "bug_larva", "pos": [0, 0.05, 0]}]},
        "clips": [{
            "name": "bug_larva_idle", "loop": True, "key_ms": 460,
            "keys": [
                {"body": {"pos": [0, 0, 0], "rot": [0, 0, 0]}},
                {"body": {"pos": [0, 0.03, 0], "rot": [0, 6, 0]}},
                {"body": {"pos": [0, 0, 0], "rot": [0, 0, 0]}},
                {"body": {"pos": [0, 0.03, 0], "rot": [0, -6, 0]}},
            ],
        }],
    }
    os.makedirs(ANIM, exist_ok=True)
    with open(os.path.join(ANIM, "bug_larva.json"), "w", encoding="utf-8") as f:
        json.dump(rig, f, indent=1)


def main():
    os.makedirs(MOD, exist_ok=True)
    b = build_larva()
    b.save(os.path.join(MOD, "bug_larva.obj"), "bug.mtl", "bug")
    write_rig()
    (mnx, mny, mnz), (mxx, mxy, mxz) = b.bounds()
    print("bug_larva: %d triangles, extent %.2f x %.2f x %.2f m"
          % (b.count(), mxx - mnx, mxy - mny, mxz - mnz))


if __name__ == "__main__":
    main()
