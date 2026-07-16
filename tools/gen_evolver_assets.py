#!/usr/bin/env python3
"""EVOLVER: a fold-creature that grows in complexity across the game.

One species, five evolution tiers. Every tier is the same three meshes
(core / petal / crown) arranged into progressively more rings of petals, so
the creature literally gains structure as it evolves. Tier 1 is a three-petal
bud (~the original PRISM); tier 5 is a five-ring bloom. Physical size is a
per-tier scale applied at draw time (see combat.cpp Evolver_TierScale), so the
top tier reads ~10x the first.

Petals hinge exactly like PRISM: each petal is authored pointing -z and folds
about its local x; a bind_rot of [0, azimuth, 0] tilts that hinge onto the
core's edge at that azimuth (rig format v2). So an animation key only ever
writes a single fold angle [f,0,0] per petal, regardless of where it sits.

Emits, into source_assets/models and game/anims:
  evolver_core.obj  evolver_petal.obj  evolver_crown.obj   (shared)
  evolver_t1.json .. evolver_t5.json                        (rig + clips)
"""

import json
import math
import os

from gen_shard_assets import TriObj

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, "source_assets", "models")
ANIM = os.path.join(ROOT, "game", "anims")

# petals per ring, innermost first. Bones = 2 (core+crown) + sum(rings).
TIERS = {
    1: [3],
    2: [4, 3],
    3: [5, 4, 3],
    4: [6, 5, 4, 3],
    5: [6, 6, 5, 4, 3],
}


# ------------------------------------------------------------------ meshes
def build_meshes():
    os.makedirs(MOD, exist_ok=True)

    # core: a crystal bipyramid over a triangular base.
    R = 0.30
    A = [math.radians(90), math.radians(210), math.radians(330)]
    V = [(R * math.cos(a), 0.0, R * math.sin(a)) for a in A]
    up, dn = (0.0, 0.32, 0.0), (0.0, -0.18, 0.0)
    core = TriObj()
    for i in range(3):
        j = (i + 1) % 3
        core.tri(V[i], V[j], up)
        core.tri(V[j], V[i], dn)
    core.save(os.path.join(MOD, "evolver_core.obj"))

    # petal: a two-facet triangular blade, hinge along x at the origin,
    # extending -z, tip lifted slightly so a folded ring domes over.
    E = 0.62
    petal = TriObj()
    petal.tri((-E / 2, 0, 0), (E / 2, 0, 0), (0.08, 0.04, -0.30))
    petal.tri((-E / 2, 0, 0), (0.08, 0.04, -0.30), (-0.05, 0.02, -0.56))
    petal.save(os.path.join(MOD, "evolver_petal.obj"))

    # crown: a slender spinning spike.
    crown = TriObj()
    B = [(0.09 * math.cos(a), 0.0, 0.09 * math.sin(a)) for a in A]
    tip = (0.0, 0.40, 0.0)
    for i in range(3):
        j = (i + 1) % 3
        crown.tri(B[i], B[j], tip)
        crown.tri(B[j], B[i], (0.0, -0.03, 0.0))
    crown.save(os.path.join(MOD, "evolver_crown.obj"))


# --------------------------------------------------------------- rig + clips
# Rings stack into an ASCENDING spire: ring 0 is the wide base, each further
# ring sits higher and a little tighter. So each evolution adds a visible new
# layer of spikes climbing the creature, instead of all rings merging into one
# flat disc. Heights are core-local (core mesh sits near local 0).
def ring_geo(r):
    return 0.36 - 0.045 * r, -0.10 + 0.30 * r      # (radius, height)


def build_bones(rings):
    """Rig bones for one tier; returns (bones, ring_names) grouped by ring."""
    bones = [{"name": "core", "parent": -1, "mesh": "evolver_core",
              "pos": [0, 0.30, 0]}]
    ring_names = []
    for ri, n in enumerate(rings):
        rad, y = ring_geo(ri)
        off = (180.0 / n) if (ri % 2) else 0.0     # interleave alternate rings
        names = []
        for p in range(n):
            az = 360.0 * p / n + off
            a = math.radians(az)
            name = "p%d_%d" % (ri, p)
            bones.append({
                "name": name, "parent": "core", "mesh": "evolver_petal",
                "pos": [round(-rad * math.sin(a), 4), round(y, 4),
                        round(-rad * math.cos(a), 4)],
                "rot": [0, round(az, 2), 0],       # bind_rot: hinge onto edge
            })
            names.append(name)
        ring_names.append(names)
    # crown caps the spire, just above the topmost ring of this tier.
    _, top_y = ring_geo(len(rings) - 1)
    bones.append({"name": "crown", "parent": "core", "mesh": "evolver_crown",
                  "pos": [0, round(top_y + 0.30, 3), 0]})
    return bones, ring_names


def _fold(frame, ring_names, angle_fn):
    for ri, names in enumerate(ring_names):
        for n in names:
            frame[n] = {"rot": [round(angle_fn(ri), 1), 0, 0]}


# Rest fold: petals tips folded UP into a tight spire (negative = up).
REST = -52


def idle_clip(name, ring_names):
    keys, F = [], 4
    for k in range(F):
        fr = {}
        _fold(fr, ring_names,
              lambda ri, k=k: REST + 8 * math.sin(2 * math.pi * k / F + ri * 0.6))
        fr["crown"] = {"rot": [0, (90 * k) % 360, 0]}          # slow spin
        fr["core"] = {"pos": [0, round(0.03 * math.sin(2 * math.pi * k / F), 3), 0]}
        keys.append(fr)
    return {"name": name, "loop": True, "key_ms": 430, "keys": keys}


def attack_clip(name, ring_names):
    # spire -> coil tighter -> SLAM open outward with a lunge -> snap back.
    seq = [
        (REST, [0, 0.00, 0.00], 0),
        (-72,  [0, 0.12, -0.16], 40),     # coil, lean back
        (20,   [0, 0.02, 0.52], 200),     # fling wide open, lunge forward (+z)
        (14,   [0, -0.02, 0.44], 260),
        (-64,  [0, 0.03, 0.12], 60),      # recoil toward the spire
        (REST, [0, 0.00, 0.00], 0),
    ]
    keys = []
    for f, cpos, cy in seq:
        fr = {}
        # outer rings lag the inner ones so the bloom opens as a ripple.
        _fold(fr, ring_names, lambda ri, f=f: f - (7 * ri if f > -30 else 0))
        fr["core"] = {"pos": [round(v, 3) for v in cpos], "rot": [0, cy, 0]}
        keys.append(fr)
    return {"name": name, "loop": False, "key_ms": 105, "keys": keys}


def hit_clip(name, ring_names):
    keys = []
    poses = [(REST, [0, 0, 0]), (None, [0, -0.12, -0.16]),
             (None, [0, -0.04, -0.05]), (REST, [0, 0, 0])]
    for idx, (f, cpos) in enumerate(poses):
        fr = {}
        if f is None:
            # jittered flinch: each petal kicks a different amount
            flat = [n for ring in ring_names for n in ring]
            for i, n in enumerate(flat):
                base = -18 if idx == 1 else -36
                fr[n] = {"rot": [round(base + (i % 3) * 16, 1), 0, 0]}
        else:
            _fold(fr, ring_names, lambda ri, f=f: f)
        fr["core"] = {"pos": [round(v, 3) for v in cpos],
                      "rot": [(-10 if idx == 1 else 0), 0, 0]}
        keys.append(fr)
    return {"name": name, "loop": False, "key_ms": 90, "keys": keys}


def build_tier(tier, rings):
    bones, ring_names = build_bones(rings)
    rig = {
        "rig": {"name": "evolver_t%d" % tier, "bones": bones},
        "clips": [
            idle_clip("evolver_t%d_idle" % tier, ring_names),
            attack_clip("evolver_t%d_attack" % tier, ring_names),
            hit_clip("evolver_t%d_hit" % tier, ring_names),
        ],
    }
    os.makedirs(ANIM, exist_ok=True)
    path = os.path.join(ANIM, "evolver_t%d.json" % tier)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(rig, f, indent=1)
    return len(bones)


def main():
    build_meshes()
    for tier, rings in TIERS.items():
        nb = build_tier(tier, rings)
        print("evolver_t%d: %d rings %s -> %d bones"
              % (tier, len(rings), rings, nb))
    print("evolver meshes + 5 tier rigs generated")


if __name__ == "__main__":
    main()
