#!/usr/bin/env python3
"""Shard creature: a fold-enemy prototype built purely from triangles
assembled edge to edge. Each mesh is one bone segment of the 'shard' rig
(game/anims/shard.json); every shared edge is axis-aligned (x or z) so the
euler rotation keys act as fold hinges.

Meshes (y-up meters, origin = the bone's hinge pivot):
  shard_core    kite body (2 tris) + dorsal fin (1 tri)
  shard_wing1_l inner left wing (2 tris), hinge along z at x=0
  shard_wing2_l outer left wing spike (1 tri), hinge along z at x=0
  shard_wing1_r / shard_wing2_r  mirrored
  shard_nose    front spike (1 tri), hinge along x at z=0
  shard_tail    rear spike (1 tri), hinge along x at z=0
"""

import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, "source_assets", "models")


class TriObj:
    def __init__(self):
        self.v = []; self.vn = []; self.faces = []

    def tri(self, a, b, c):
        """One triangle, flat face normal from winding (CCW seen from +n)."""
        u = tuple(b[i] - a[i] for i in range(3))
        w = tuple(c[i] - a[i] for i in range(3))
        n = (u[1] * w[2] - u[2] * w[1],
             u[2] * w[0] - u[0] * w[2],
             u[0] * w[1] - u[1] * w[0])
        L = math.sqrt(sum(x * x for x in n)) or 1.0
        n = tuple(x / L for x in n)
        base_v = len(self.v); base_n = len(self.vn)
        self.v += [a, b, c]; self.vn.append(n)
        self.faces.append(tuple((base_v + i + 1, base_n + 1) for i in range(3)))

    def save(self, path):
        with open(path, "w") as f:
            f.write("mtllib shard.mtl\nusemtl shard\n")
            for p in self.v:  f.write("v %.4f %.4f %.4f\n" % p)
            for n in self.vn: f.write("vn %.4f %.4f %.4f\n" % n)
            for fa in self.faces:
                f.write("f " + " ".join("%d//%d" % c for c in fa) + "\n")


def main():
    os.makedirs(MOD, exist_ok=True)

    # hexagonal core: side edges at x=+-0.25 are z-parallel (z in [-0.12,0.12])
    # so the wings hinge flush against them. 4 triangles + dorsal fin.
    core = TriObj()
    core.tri((0, 0, 0.30), (0.25, 0, 0.12), (-0.25, 0, 0.12))     # front tri
    core.tri((0.25, 0, 0.12), (0.25, 0, -0.12), (-0.25, 0, -0.12))
    core.tri((0.25, 0, 0.12), (-0.25, 0, -0.12), (-0.25, 0, 0.12))
    core.tri((0.25, 0, -0.12), (0, 0, -0.30), (-0.25, 0, -0.12))  # rear tri
    core.tri((0, 0.0, -0.26), (0, 0.28, -0.02), (0, 0.0, 0.14))   # dorsal fin
    core.save(os.path.join(MOD, "shard_core.obj"))

    w1l = TriObj()  # inner left wing: hinge edge z in [-0.12,0.12] at x=0
    w1l.tri((0, 0, 0.12), (-0.36, 0, 0.08), (0, 0, -0.12))
    w1l.tri((-0.36, 0, 0.08), (-0.36, 0, -0.06), (0, 0, -0.12))
    w1l.save(os.path.join(MOD, "shard_wing1_l.obj"))

    w2l = TriObj()  # outer left spike, hinge edge z in [-0.06,0.08] at x=0
    w2l.tri((0, 0, 0.08), (-0.44, 0, 0.02), (0, 0, -0.06))
    w2l.save(os.path.join(MOD, "shard_wing2_l.obj"))

    w1r = TriObj()  # mirrored right (winding swapped to keep the normal up)
    w1r.tri((0, 0, 0.12), (0, 0, -0.12), (0.36, 0, 0.08))
    w1r.tri((0.36, 0, 0.08), (0, 0, -0.12), (0.36, 0, -0.06))
    w1r.save(os.path.join(MOD, "shard_wing1_r.obj"))

    w2r = TriObj()
    w2r.tri((0, 0, 0.08), (0, 0, -0.06), (0.44, 0, 0.02))
    w2r.save(os.path.join(MOD, "shard_wing2_r.obj"))

    nose = TriObj()  # front spike, hinge edge on x axis at z=0
    nose.tri((0.14, 0, 0), (0, 0, 0.42), (-0.14, 0, 0))
    nose.save(os.path.join(MOD, "shard_nose.obj"))

    tail = TriObj()  # rear spike
    tail.tri((0.12, 0, 0), (-0.12, 0, 0), (0, 0, -0.38))
    tail.save(os.path.join(MOD, "shard_tail.obj"))

    print("shard meshes generated")


if __name__ == "__main__":
    main()
