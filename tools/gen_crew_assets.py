#!/usr/bin/env python3
"""Crew diver: segmented player character, lofted low-poly (MGS/Vagrant
Story style — tapered chamfered sections, not boxes). Each OBJ's origin is
its bone pivot; game/anims/crew.json places them.

A segment is a loft: chamfered-rectangle rings (w x d, corners cut) stacked
along y and skinned with quads, capped with triangle fans. Facet normals.
"""

import math
import os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, "source_assets", "models")
TEX = os.path.join(ROOT, "source_assets", "textures")


def ring(y, w, d, cut=0.32):
    """8-vertex chamfered rectangle centered on the y axis (CCW from +x+z)."""
    hw, hd = w / 2.0, d / 2.0
    cx, cz = hw * cut, hd * cut
    return [(hw, y, hd - cz), (hw - cx, y, hd), (-hw + cx, y, hd),
            (-hw, y, hd - cz), (-hw, y, -hd + cz), (-hw + cx, y, -hd),
            (hw - cx, y, -hd), (hw, y, -hd + cz)]


class Loft:
    def __init__(self):
        self.v = []; self.vt = []; self.vn = []
        self.faces = []                     # (idx tuples of (v,t,n))
        self._nmap = {}

    def _norm(self, n):
        L = math.sqrt(sum(c * c for c in n)) or 1.0
        n = (round(n[0] / L, 4), round(n[1] / L, 4), round(n[2] / L, 4))
        if n not in self._nmap:
            self.vn.append(n); self._nmap[n] = len(self.vn)
        return self._nmap[n]

    def _face(self, pts, uvs=None):
        """Planar face (3 or 4 verts), CCW seen from outside."""
        u = tuple(pts[1][i] - pts[0][i] for i in range(3))
        w = tuple(pts[2][i] - pts[0][i] for i in range(3))
        n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2],
             u[0] * w[1] - u[1] * w[0])
        ni = self._norm(n)
        if uvs is None:
            uvs = [(0, 0)] * len(pts)
        bv, bt = len(self.v), len(self.vt)
        self.v += list(pts); self.vt += list(uvs)
        self.faces.append(tuple((bv + i + 1, bt + i + 1, ni)
                                for i in range(len(pts))))

    def loft(self, rings, vspan=None):
        """Skin consecutive rings with quads. rings: list of 8-pt rings
        (bottom to top). vspan: (v0, v1) texture V range bottom->top."""
        for r in range(len(rings) - 1):
            lo, hi = rings[r], rings[r + 1]
            for i in range(8):
                j = (i + 1) % 8
                uv = [(0, 0)] * 4
                if vspan:
                    t0 = vspan[0] + (vspan[1] - vspan[0]) * r / (len(rings) - 1)
                    t1 = vspan[0] + (vspan[1] - vspan[0]) * (r + 1) / (len(rings) - 1)
                    u0, u1 = i / 8.0, (i + 1) / 8.0
                    uv = [(u0, t0), (u1, t0), (u1, t1), (u0, t1)]
                # outward winding: bottom edge left->right, then up
                self._face([lo[i], lo[j], hi[j], hi[i]], uv)

    def cap(self, rng, up=True, uv_at=None):
        cy = sum(p[1] for p in rng) / 8.0
        cx = sum(p[0] for p in rng) / 8.0
        cz = sum(p[2] for p in rng) / 8.0
        center = (cx, cy, cz)
        for i in range(8):
            j = (i + 1) % 8
            uvs = None
            if uv_at is not None:
                uvs = [uv_at] * 3
            if up:  self._face([rng[i], rng[j], center], uvs)
            else:   self._face([rng[j], rng[i], center], uvs)

    def save(self, path):
        with open(path, "w") as f:
            f.write("mtllib crew.mtl\nusemtl crew\n")
            for p in self.v:  f.write("v %.4f %.4f %.4f\n" % p)
            for t in self.vt: f.write("vt %.4f %.4f\n" % t)
            for n in self.vn: f.write("vn %.4f %.4f %.4f\n" % n)
            for fa in self.faces:
                f.write("f " + " ".join("%d/%d/%d" % c for c in fa) + "\n")


def offset(rng, dx=0.0, dz=0.0):
    return [(x + dx, y, z + dz) for (x, y, z) in rng]


def tex_helmet():
    """32x32 wrap (u = around the head, v = bottom->top): visor band on the
    front-facing third, shell elsewhere, crown vents."""
    im = Image.new("RGB", (32, 32), (92, 100, 105))
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 31, 2], fill=(66, 74, 78))            # rim (v=0 bottom)
    d.rectangle([0, 26, 31, 31], fill=(74, 82, 86))          # crown
    # front faces are u ~ [0.25..0.5] of the wrap (ring starts at +x, CCW,
    # +z = facing) -> visor slit across those columns, mid height
    d.rectangle([7, 12, 16, 19], fill=(26, 36, 40))          # recess
    d.rectangle([8, 13, 15, 18], fill=(90, 235, 240))        # cyan visor
    d.rectangle([8, 13, 15, 15], fill=(45, 160, 170))
    d.rectangle([20, 8, 23, 22], fill=(70, 78, 82))          # side channel
    d.rectangle([2, 8, 5, 22], fill=(70, 78, 82))
    im.save(os.path.join(TEX, "crew_helmet.png"))


def main():
    os.makedirs(MOD, exist_ok=True)
    os.makedirs(TEX, exist_ok=True)
    tex_helmet()

    # ---- torso: waist -> chest -> shoulders -> neck base (pivot at waist)
    t = Loft()
    rt = [ring(0.00, 0.26, 0.165),
          ring(0.14, 0.29, 0.185),
          ring(0.30, 0.335, 0.20),          # chest
          ring(0.42, 0.36, 0.205),          # shoulders
          ring(0.50, 0.155, 0.13)]          # neck base (steep slope in)
    t.loft(rt)
    t.cap(rt[-1], up=True)
    t.cap(rt[0], up=False)
    # backpack: small loft on the back
    rb = [offset(ring(0.10, 0.20, 0.075), dz=-0.135),
          offset(ring(0.24, 0.22, 0.085), dz=-0.14),
          offset(ring(0.38, 0.18, 0.07), dz=-0.135)]
    t.loft(rb); t.cap(rb[-1], up=True); t.cap(rb[0], up=False)
    t.save(os.path.join(MOD, "crew_torso.obj"))

    # ---- pelvis (pivot at hip center)
    p = Loft()
    rp = [ring(-0.10, 0.24, 0.16),
          ring(-0.02, 0.295, 0.185),
          ring(0.06, 0.27, 0.175)]
    p.loft(rp); p.cap(rp[-1], up=True); p.cap(rp[0], up=False)
    p.save(os.path.join(MOD, "crew_pelvis.obj"))

    # ---- head: helmet (pivot at neck), textured wrap
    h = Loft()
    rh = [ring(0.02, 0.145, 0.15),          # chin rim
          ring(0.06, 0.19, 0.20),
          ring(0.16, 0.20, 0.21),
          ring(0.24, 0.17, 0.18),
          ring(0.285, 0.10, 0.11)]          # crown
    h.loft(rh, vspan=(0.02, 0.95))
    h.cap(rh[-1], up=True, uv_at=(0.9, 0.97))
    h.cap(rh[0], up=False, uv_at=(0.9, 0.03))
    h.save(os.path.join(MOD, "crew_head.obj"))

    # ---- upper arm: deltoid flare -> elbow (pivot at shoulder)
    au = Loft()
    ra = [ring(-0.30, 0.075, 0.075),
          ring(-0.16, 0.095, 0.095),
          ring(-0.04, 0.12, 0.12),
          ring(0.02, 0.10, 0.10)]           # cap above pivot: shoulder ball
    au.loft(ra); au.cap(ra[-1], up=True); au.cap(ra[0], up=False)
    au.save(os.path.join(MOD, "crew_arm_up.obj"))

    # ---- forearm + glove (pivot at elbow)
    al = Loft()
    rf = [ring(-0.26, 0.06, 0.06),          # wrist
          ring(-0.10, 0.085, 0.085),        # forearm bulge
          ring(0.01, 0.075, 0.075)]
    al.loft(rf); al.cap(rf[-1], up=True); al.cap(rf[0], up=False)
    rg = [ring(-0.37, 0.075, 0.085),        # glove/fist
          ring(-0.27, 0.09, 0.10)]
    al.loft(rg); al.cap(rg[-1], up=True); al.cap(rg[0], up=False)
    al.save(os.path.join(MOD, "crew_arm_lo.obj"))

    # ---- thigh (pivot at hip)
    lu = Loft()
    rl = [ring(-0.39, 0.10, 0.105),         # knee
          ring(-0.20, 0.125, 0.135),
          ring(-0.02, 0.145, 0.155)]        # quad/hip
    lu.loft(rl); lu.cap(rl[-1], up=True); lu.cap(rl[0], up=False)
    lu.save(os.path.join(MOD, "crew_leg_up.obj"))

    # ---- shin + boot (pivot at knee)
    ll = Loft()
    rs = [ring(-0.36, 0.075, 0.08),         # ankle
          ring(-0.26, 0.10, 0.115),         # calf bulge
          ring(-0.10, 0.105, 0.11),
          ring(0.01, 0.095, 0.10)]
    ll.loft(rs); ll.cap(rs[-1], up=True); ll.cap(rs[0], up=False)
    rboot = [offset(ring(-0.45, 0.11, 0.21), dz=0.045),
             offset(ring(-0.36, 0.10, 0.16), dz=0.02)]
    ll.loft(rboot); ll.cap(rboot[-1], up=True); ll.cap(rboot[0], up=False)
    ll.save(os.path.join(MOD, "crew_leg_lo.obj"))

    print("crew meshes generated (lofted)")


if __name__ == "__main__":
    main()
