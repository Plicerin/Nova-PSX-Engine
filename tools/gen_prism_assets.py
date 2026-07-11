#!/usr/bin/env python3
"""PRISM: fold-enemy #2 — a hovering tetra-flower. Crystal bipyramid core;
three petal triangles hinged edge-to-edge on the core's base edges (bind_rot
ry 0/120/-120 tilts each hinge onto its edge — rig format v2 feature).
"""

import os
from gen_shard_assets import TriObj

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, "source_assets", "models")

R = 0.45          # core base triangle radius


def main():
    os.makedirs(MOD, exist_ok=True)
    import math
    A = [math.radians(90), math.radians(210), math.radians(330)]
    V = [(R * math.cos(a), 0.0, R * math.sin(a)) for a in A]
    apex_u = (0.0, 0.34, 0.0)
    apex_d = (0.0, -0.20, 0.0)

    core = TriObj()
    for i in range(3):
        j = (i + 1) % 3
        core.tri(V[i], V[j], apex_u)          # upper crystal faces
        core.tri(V[j], V[i], apex_d)          # lower crystal faces
    core.save(os.path.join(MOD, "prism_core.obj"))

    # petal: hinge edge along local x at the pivot, blade extends -z (outward
    # for the base petal at the south edge), slight kink for a crystal facet
    petal = TriObj()
    E = 0.78                                   # base edge length (R * sqrt(3))
    petal.tri((-E / 2, 0, 0), (E / 2, 0, 0), (0.10, 0, -0.34))
    petal.tri((-E / 2, 0, 0), (0.10, 0, -0.34), (-0.06, 0, -0.62))
    petal.save(os.path.join(MOD, "prism_petal.obj"))

    crown = TriObj()                           # spinning top spike
    B = [(0.10 * math.cos(a), 0.0, 0.10 * math.sin(a)) for a in A]
    tip = (0.0, 0.38, 0.0)
    for i in range(3):
        j = (i + 1) % 3
        crown.tri(B[i], B[j], tip)
        crown.tri(B[j], B[i], (0.0, -0.02, 0.0))
    crown.save(os.path.join(MOD, "prism_crown.obj"))

    print("prism meshes generated")


if __name__ == "__main__":
    main()
