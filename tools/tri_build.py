#!/usr/bin/env python3
"""Assemble creatures from FIXED-SIZE equilateral triangles, edge-to-edge.

The three alien rules are enforced by construction:
  1. fixed size  -- every triangle has the same edge length E.
  2. edge-to-edge -- a new triangle is always attached to a full edge of an
     existing one, sharing both its endpoints.
  3. connected    -- every triangle is attached (transitively) to the seed.

attach() places a new equilateral triangle on an edge of an existing one,
rotated out of that triangle's plane by a fold angle (0 = coplanar/flat,
+90 = folded straight up toward the parent's normal). This is a developable
fold: the strip/patch stays rigid unit triangles no matter how it curls.
"""

import math


def _sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def _add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def _mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def _dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])
def _norm(a):
    L = math.sqrt(_dot(a, a)) or 1.0
    return _mul(a, 1.0 / L)


class TriBuild:
    def __init__(self, E=0.45):
        self.E = E
        self.tris = []              # each: (A, B, C) world points

    def normal(self, i):
        A, B, C = self.tris[i]
        return _norm(_cross(_sub(B, A), _sub(C, A)))

    def seed(self, A, B, C):
        self.tris.append((A, B, C))
        return len(self.tris) - 1

    def seed_flat(self, cx=0.0, cy=0.0, cz=0.0):
        """A single equilateral triangle lying flat (in the y-plane), pointing +z."""
        E, h = self.E, self.E * math.sqrt(3) / 2.0
        A = (cx - E / 2, cy, cz - h / 3)
        B = (cx + E / 2, cy, cz - h / 3)
        C = (cx, cy, cz + 2 * h / 3)
        return self.seed(A, B, C)

    def attach(self, i, e0, e1, fold_deg):
        """New equilateral triangle on edge (local verts e0,e1) of triangle i.
        Returns its index; its verts are (A, B, C) = (edge0, edge1, new apex)."""
        T = self.tris[i]
        A, B = T[e0], T[e1]
        P = T[({0, 1, 2} - {e0, e1}).pop()]        # parent apex
        M = _mul(_add(A, B), 0.5)
        u = _norm(_sub(B, A))
        n = self.normal(i)
        toP = _sub(P, M)
        d0 = _mul(_norm(_sub(toP, _mul(u, _dot(toP, u)))), -1.0)  # outward, in-plane
        h = self.E * math.sqrt(3) / 2.0
        phi = math.radians(fold_deg)
        C = _add(M, _add(_mul(d0, h * math.cos(phi)), _mul(n, h * math.sin(phi))))
        self.tris.append((A, B, C))
        return len(self.tris) - 1

    def strip(self, start, count, fold_deg):
        """Chain `count` triangles off triangle `start`, each attached to the
        previous one's trailing edge -- a folded ribbon (constant fold => arc)."""
        i = start
        last = i
        f = fold_deg
        for k in range(count):
            i = self.attach(i, 1, 2, f(k) if callable(f) else f)
            last = i
        return last

    # --- primitives assembled from the same unit triangle --------------------
    def _frame(self, axis):
        axis = _norm(axis)
        ref = (0.0, 0.0, 1.0) if abs(axis[2]) < 0.9 else (1.0, 0.0, 0.0)
        e1 = _norm(_cross(axis, ref))
        e2 = _cross(axis, e1)
        return axis, e1, e2

    def tube(self, c0, axis, nbands, twist0=0.0):
        """A straight limb: a triangular ANTIPRISM tube of unit equilateral
        triangles along `axis`. Cross-section edge = E (circumradius E/sqrt3),
        band height E*sqrt(2/3). 6 triangles per band. Returns the end centre."""
        ax, e1, e2 = self._frame(axis)
        R = self.E / math.sqrt(3.0)
        d = self.E * math.sqrt(2.0 / 3.0)

        def ring(k):
            base = _add(c0, _mul(ax, d * k))
            out = []
            for j in range(3):
                a = math.radians(twist0 + 60.0 * k + 120.0 * j)
                out.append(_add(base, _add(_mul(e1, R * math.cos(a)),
                                           _mul(e2, R * math.sin(a)))))
            return out

        prev = ring(0)
        for k in range(1, nbands + 1):
            cur = ring(k)
            for j in range(3):
                jn = (j + 1) % 3
                self.tris.append((prev[j], prev[jn], cur[j]))       # up
                self.tris.append((prev[jn], cur[jn], cur[j]))       # down
            prev = cur
        return _add(c0, _mul(ax, d * nbands))

    def octahedron(self, center, a=None):
        """A body block: regular octahedron (8 unit equilateral faces).
        Edge = a*sqrt2; pass a = E/sqrt2 to keep the unit edge."""
        if a is None:
            a = self.E / math.sqrt(2.0)
        cx, cy, cz = center
        vp = [(cx + a, cy, cz), (cx - a, cy, cz),
              (cx, cy + a, cz), (cx, cy - a, cz),
              (cx, cy, cz + a), (cx, cy, cz - a)]
        xp, xm, yp, ym, zp, zm = vp
        for (u, v, w) in [(xp, yp, zp), (yp, xm, zp), (xm, ym, zp), (ym, xp, zp),
                          (yp, xp, zm), (xm, yp, zm), (ym, xm, zm), (xp, ym, zm)]:
            self.tris.append((u, v, w))

    def count(self):
        return len(self.tris)

    def bounds(self):
        xs = [p[0] for t in self.tris for p in t]
        ys = [p[1] for t in self.tris for p in t]
        zs = [p[2] for t in self.tris for p in t]
        return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))

    def save(self, path, mtllib, mtl):
        with open(path, "w") as f:
            f.write("mtllib %s\nusemtl %s\n" % (mtllib, mtl))
            for (A, B, C) in self.tris:
                for p in (A, B, C):
                    f.write("v %.4f %.4f %.4f\n" % p)
                n = _norm(_cross(_sub(B, A), _sub(C, A)))
                f.write("vn %.4f %.4f %.4f\n" % n)
            for k in range(len(self.tris)):
                a, b, c = 3 * k + 1, 3 * k + 2, 3 * k + 3
                f.write("f %d//%d %d//%d %d//%d\n" % (a, k + 1, b, k + 1, c, k + 1))
