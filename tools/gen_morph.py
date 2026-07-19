#!/usr/bin/env python3
"""TRIARCHON transformation: ONE fixed pool of N unit triangles arranged two
ways (spider, snake) and morphed between them. The snake IS the spider's
triangles rearranged -- same count, same pool. See triarchon-animation-model.

Each triangle is transported RIGIDLY (fixed size preserved every frame): we
interpolate its centroid and orientation from the spider slot to the snake
slot. Baked to a set of frame meshes (triarmorph_NN) the viewer plays back --
a PS1-authentic baked transformation.
"""

import math
import os

import numpy as np

from tri_build import TriBuild

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, "source_assets", "models")

E = 0.18
BODY_Y = 0.30
LEG_AZ = (40, 140, 0, 180, 220, 320)
FRAMES = 14                    # morph frames (spider -> snake)


# ---- the two arrangements of the SAME pool ----
def flat_spider():
    b = TriBuild(E=E)
    thorax_end = b.tube((0, BODY_Y, -0.22), (0, 0, 1), 3)        # 18
    b.octahedron((0, BODY_Y + 0.06, thorax_end[2] + 0.08), a=E / math.sqrt(2))  # 8
    for az in LEG_AZ:
        a = math.radians(az)
        ox, oz = math.cos(a), math.sin(a)
        hip = (ox * 0.14, BODY_Y + 0.02, oz * 0.14)
        knee = b.tube(hip, (ox * 0.9, 0.45, oz * 0.9), 2, twist0=az)     # 12
        b.tube(knee, (ox * 0.55, -1.0, oz * 0.55), 3, twist0=az + 30)    # 18
    return b


def flat_snake():
    """A reared, slithering serpent: the head periscopes up, the neck curves
    down to the deck, and the body S-waves back. Same 11 segments (198 tris) +
    head (8) = 206, just laid along a curved centreline instead of a stick."""
    b = TriBuild(E=E)
    seglen = E * math.sqrt(2.0 / 3.0) * 3
    pos = np.array([0.0, 0.86, 0.55])          # raised head, forward
    pitch = -62.0                              # neck descends from the head
    head_pos, head_dir = pos.copy(), None
    for s in range(11):
        yaw = 38.0 * math.sin((s + 1) * 0.85)  # horizontal S-wave (slither)
        cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
        cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        d = np.array([sy * cp, sp, -cy * cp])
        d /= (np.linalg.norm(d) or 1)
        if s == 0:
            head_dir = d
        b.tube(tuple(pos), tuple(d), 3, twist0=s * 20)          # 18 tris
        pos = pos + d * seglen
        pitch = min(0.0, pitch + 31.0)         # rear -> level over 2 segments
    b.octahedron((head_pos[0], head_pos[1] + 0.12, head_pos[2] + 0.10),
                 a=E / math.sqrt(2))                            # 8
    return b


# ---- rigid frame extraction / interpolation ----
CANON = None


def frame_of(V):
    """(centroid, R) with R mapping the canonical unit triangle to this one."""
    V = np.asarray(V, float)
    c = V.mean(0)
    o = V - c
    u = o[1] - o[0]; u = u / (np.linalg.norm(u) or 1)
    n = np.cross(o[1] - o[0], o[2] - o[0]); n = n / (np.linalg.norm(n) or 1)
    v = np.cross(n, u)
    return c, np.stack([u, v, n], 1)          # basis as columns


def ortho(R):
    u = R[:, 0]; u = u / (np.linalg.norm(u) or 1)
    v = R[:, 1] - u * (u @ R[:, 1]); v = v / (np.linalg.norm(v) or 1)
    n = np.cross(u, v)
    return np.stack([u, v, n], 1)


def main():
    global CANON
    os.makedirs(MOD, exist_ok=True)
    A = [np.asarray(t, float) for t in flat_spider().tris]
    B = [np.asarray(t, float) for t in flat_snake().tris]
    print("spider tris=%d  snake tris=%d" % (len(A), len(B)))
    n = min(len(A), len(B))
    A, B = A[:n], B[:n]

    # canonical local triangle = the spider's first, framed to its own basis
    cA0, RA0 = frame_of(A[0])
    CANON = (np.linalg.inv(RA0) @ (A[0] - cA0).T).T     # local offsets, centroid 0

    # pair by a stable spatial order so the morph flows rather than scrambles:
    # sort both by (height, angle-around-body).
    def key(V):
        c = np.asarray(V, float).mean(0)
        return (round(c[1], 2), math.atan2(c[2], c[0]))
    A.sort(key=key)
    B.sort(key=lambda V: (round(np.asarray(V, float).mean(0)[1], 2),
                          math.atan2(np.asarray(V, float).mean(0)[2],
                                     np.asarray(V, float).mean(0)[0])))

    frames = [(frame_of(a), frame_of(b)) for a, b in zip(A, B)]

    for k in range(FRAMES):
        t = k / (FRAMES - 1)
        tris = []
        for (cA, RA), (cB, RB) in frames:
            c = cA * (1 - t) + cB * t
            R = ortho(RA * (1 - t) + RB * t)
            verts = [tuple((c + R @ off).tolist()) for off in CANON]
            tris.append(verts)
        tb = TriBuild(E=E); tb.tris = tris
        tb.save(os.path.join(MOD, "triarmorph_%02d.obj" % k), "triar.mtl", "body")
    print("baked %d morph frames (%d triangles each)" % (FRAMES, n))


if __name__ == "__main__":
    main()
