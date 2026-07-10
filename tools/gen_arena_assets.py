#!/usr/bin/env python3
"""Generate the aquatic-lab combat arena: wet dark-metal textures + room/prop
geometry + a cryo-fluid water plane. Deterministic (seeded RNG).

Coordinate convention matches the engine importer: y-up, meters. Front faces
CCW from the visible side; walls/props are flagged double-sided in the asset
config so winding is not load-bearing here.
"""
import math
import os
import random

import numpy as np
from PIL import Image, ImageDraw

TEX = "source_assets/textures"
MOD = "source_assets/models"


# ----------------------------------------------------------------- textures
def _speckle(img, seed, amt, n):
    rng = random.Random(seed)
    px = img.load()
    w, h = img.size
    for _ in range(n):
        x, y = rng.randint(0, w - 1), rng.randint(0, h - 1)
        d = rng.randint(-amt, amt)
        c = px[x, y]
        px[x, y] = tuple(max(0, min(255, v + d)) for v in c[:3])


def tex_metal_floor():
    """Clean dark metal deck: one large plate per tile, clear grout + bevel."""
    W = H = 64                      # one texture = one 2x2 m plate (no tiling noise)
    base = (48, 58, 60)
    im = Image.new("RGB", (W, H), base)
    d = ImageDraw.Draw(im)
    # subtle top-left lit bevel, bottom-right shadow -> reads as a raised plate
    d.rectangle([0, 0, W - 1, H - 1], outline=(66, 78, 80))       # bright top edge
    d.line([(0, H - 1), (W - 1, H - 1)], fill=(24, 30, 32))        # dark bottom
    d.line([(W - 1, 0), (W - 1, H - 1)], fill=(24, 30, 32))        # dark right
    d.rectangle([2, 2, W - 3, H - 3], outline=(28, 36, 38))        # groove
    for (x, y) in [(6, 6), (W - 7, 6), (6, H - 7), (W - 7, H - 7)]:  # corner bolts
        d.ellipse([x - 1, y - 1, x + 1, y + 1], fill=(78, 92, 88))
    _speckle(im, 101, 4, 60)         # very light grain
    im.save(f"{TEX}/arena_floor.png")


def tex_water():
    """Seamless PS1 water caustics: interference field of 2-D sine gratings
    (integer freqs => tiles), domain-warped, near-zero crossings lifted into
    bright cyan ridges over deep blue. Blended 50/50 over the deck."""
    W = H = 128
    xs = np.linspace(0, 2 * np.pi, W, endpoint=False)
    ys = np.linspace(0, 2 * np.pi, H, endpoint=False)
    X, Y = np.meshgrid(xs, ys)
    wx = 0.55 * np.sin(2 * Y + 1.0) + 0.35 * np.sin(3 * X + 0.5)
    wy = 0.55 * np.sin(2 * X + 2.0) + 0.35 * np.sin(3 * Y + 1.7)
    Xa, Ya = X + wx, Y + wy

    def g(fx, fy, ph):
        return np.sin(fx * Xa + fy * Ya + ph)

    v = (g(2, 1, 0.0) + g(1, 2, 1.3) + g(3, 2, 2.1) + g(2, 3, 0.7)) / 4.0
    ridge = np.clip(1.0 - np.abs(v) * 2.0, 0.0, 1.0) ** 1.6
    base = np.array([16, 46, 70]); mid = np.array([44, 122, 160])
    peak = np.array([180, 230, 248])
    t = ridge[..., None]
    col = np.where(t < 0.5, base + (mid - base) * (t / 0.5),
                   mid + (peak - mid) * ((t - 0.5) / 0.5))
    Image.fromarray(np.clip(col, 0, 255).astype(np.uint8), "RGB").save(
        f"{TEX}/arena_water.png")


def tex_metal_wall():
    """Readable industrial wall: brighter than the floor, vertical panels,
    riveted frames, one clean hazard band along the top. UV: u tiles, v=bottom..top."""
    W = H = 128
    im = Image.new("RGB", (W, H), (60, 70, 72))       # clearly lighter than deck
    d = ImageDraw.Draw(im)
    # two vertical panels with recessed centers + riveted frame
    for px0 in (6, 68):
        d.rectangle([px0, 16, px0 + 54, H - 6], fill=(50, 60, 62), outline=(30, 38, 40))
        d.rectangle([px0 + 6, 24, px0 + 48, H - 14], fill=(44, 54, 56), outline=(70, 82, 82))
        for ry in range(22, H - 8, 16):                # rivets down the frame
            d.ellipse([px0 + 1, ry, px0 + 3, ry + 2], fill=(84, 96, 94))
            d.ellipse([px0 + 51, ry, px0 + 53, ry + 2], fill=(84, 96, 94))
    # hazard band across the very top (v = 1)
    for x in range(-8, W, 16):
        d.polygon([(x, 0), (x + 8, 0), (x + 16, 14), (x + 8, 14)], fill=(170, 150, 44))
    d.rectangle([0, 14, W, 16], fill=(22, 26, 26))
    _speckle(im, 202, 5, 120)
    im.save(f"{TEX}/arena_wall.png")


def tex_metal_plate():
    """Generic dark metal for crates/pipes/pod."""
    W = H = 64
    im = Image.new("RGB", (W, H), (34, 42, 44))
    d = ImageDraw.Draw(im)
    d.rectangle([2, 2, W - 3, H - 3], outline=(20, 26, 28))
    d.rectangle([4, 4, W - 5, H - 5], outline=(48, 60, 60))
    d.line([(6, 6), (W - 7, H - 7)], fill=(24, 30, 32))   # brace
    for (x, y) in [(8, 8), (W - 9, 8), (8, H - 9), (W - 9, H - 9)]:
        d.ellipse([x - 1, y - 1, x + 1, y + 1], fill=(60, 74, 72))
    _speckle(im, 303, 10, 200)
    im.save(f"{TEX}/arena_plate.png")


def tex_screen():
    """Dark console screen with green readouts (emissive-ish)."""
    W = H = 64
    im = Image.new("RGB", (W, H), (8, 16, 12))
    d = ImageDraw.Draw(im)
    rng = random.Random(404)
    for y in range(6, H - 6, 6):
        w = rng.randint(10, 44)
        d.line([(8, y), (8 + w, y)], fill=(110, 235, 150))   # bright: blooms
    d.rectangle([0, 0, W - 1, H - 1], outline=(60, 140, 90))
    for _ in range(14):
        x, y = rng.randint(6, W - 8), rng.randint(4, H - 6)
        d.rectangle([x, y, x + 2, y + 2], fill=(180, 255, 210))
    im.save(f"{TEX}/arena_screen.png")




# ------------------------------------------------------------------ meshes
class Obj:
    def __init__(self):
        self.v = []; self.vt = []; self.vn = []
        self.faces = []            # (mtl, [(vi,ti,ni)...])
        self._nmap = {}
    def _n(self, n):
        L = math.sqrt(sum(c * c for c in n)) or 1.0
        n = (n[0] / L, n[1] / L, n[2] / L)
        key = tuple(round(c, 3) for c in n)
        if key not in self._nmap:
            self.vn.append(n); self._nmap[key] = len(self.vn)
        return self._nmap[key]
    def quad(self, mtl, pts, uvs, n):
        ni = self._n(n)
        idx = []
        for p, uv in zip(pts, uvs):
            self.v.append(p); self.vt.append(uv)
            idx.append((len(self.v), len(self.vt), ni))
        self.faces.append((mtl, idx))
    def box(self, mtl, cx, cy, cz, sx, sy, sz, uv=1.0):
        x0, x1 = cx - sx / 2, cx + sx / 2
        y0, y1 = cy - sy / 2, cy + sy / 2
        z0, z1 = cz - sz / 2, cz + sz / 2
        U = uv
        # +Y top, -Y bottom, +X, -X, +Z, -Z (winding not critical: double-sided)
        self.quad(mtl, [(x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0)],
                  [(0, 0), (0, U), (U, U), (U, 0)], (0, 1, 0))
        self.quad(mtl, [(x0, y0, z1), (x0, y0, z0), (x1, y0, z0), (x1, y0, z1)],
                  [(0, 0), (0, U), (U, U), (U, 0)], (0, -1, 0))
        self.quad(mtl, [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)],
                  [(0, 0), (0, U), (U, U), (U, 0)], (1, 0, 0))
        self.quad(mtl, [(x0, y0, z1), (x0, y1, z1), (x0, y1, z0), (x0, y0, z0)],
                  [(0, 0), (0, U), (U, U), (U, 0)], (-1, 0, 0))
        self.quad(mtl, [(x0, y0, z1), (x0, y1, z1), (x1, y1, z1), (x1, y0, z1)],
                  [(0, 0), (0, U), (U, U), (U, 0)], (0, 0, 1))
        self.quad(mtl, [(x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z0)],
                  [(0, 0), (0, U), (U, U), (U, 0)], (0, 0, -1))
    def grid(self, mtl, cx, cz, size, n_cells, y, uv_tiles, normal=(0, 1, 0)):
        """Subdivided horizontal plane (near-clip + affine + specular gradient)."""
        h = size / 2; step = size / n_cells
        for j in range(n_cells):
            for i in range(n_cells):
                x0 = cx - h + i * step; x1 = x0 + step
                z0 = cz - h + j * step; z1 = z0 + step
                u0 = i / n_cells * uv_tiles; u1 = (i + 1) / n_cells * uv_tiles
                v0 = j / n_cells * uv_tiles; v1 = (j + 1) / n_cells * uv_tiles
                self.quad(mtl, [(x0, y, z0), (x0, y, z1), (x1, y, z1), (x1, y, z0)],
                          [(u0, v0), (u0, v1), (u1, v1), (u1, v0)], normal)
    def save(self, path, mtllib):
        with open(path, "w") as f:
            f.write(f"mtllib {mtllib}\n")
            for p in self.v:  f.write("v %.4f %.4f %.4f\n" % p)
            for t in self.vt: f.write("vt %.4f %.4f\n" % t)
            for n in self.vn: f.write("vn %.4f %.4f %.4f\n" % n)
            cur = None
            for mtl, idx in self.faces:
                if mtl != cur:
                    f.write(f"usemtl {mtl}\n"); cur = mtl
                f.write("f " + " ".join("%d/%d/%d" % c for c in idx) + "\n")


def build_arena():
    o = Obj()
    R = 8.0            # room half-extent (16x16 m)
    Hh = 4.5           # wall height
    o.grid("floor", 0, 0, 2 * R, 24, 0.0, 6.0)                 # deck (6 plates/side)
    # walls (double-sided). NO south wall: the camera looks in from the open
    # near (-z) side at a 3/4 angle, like the concept.
    for (a, b, c, d_, nx, nz) in [
        (-R, R, R, R, 0, -1),      # north (+z), faces -z
        (R, R, -R, R, -1, 0),      # east (+x)
        (-R, -R, -R, R, 1, 0),     # west (-x)
    ]:
        # split each wall into 3 bands x 4 segments: less affine swim, and one
        # texture repeat per face (u8 texel coords cap spans at 255 texels)
        for k in range(3):
            y0 = Hh * k / 3; y1 = Hh * (k + 1) / 3
            for s in range(4):
                f0 = s / 4.0; f1 = (s + 1) / 4.0
                xa = a + (b - a) * f0; za = c + (d_ - c) * f0
                xb = a + (b - a) * f1; zb = c + (d_ - c) * f1
                o.quad("wall",
                       [(xa, y0, za), (xa, y1, za), (xb, y1, zb), (xb, y0, zb)],
                       [(0, 1 - k / 3), (0, 1 - (k + 1) / 3),
                        (1, 1 - (k + 1) / 3), (1, 1 - k / 3)],
                       (nx, 0, nz))
    # cryo pod against the north wall (keeps the combat lane clear)
    o.box("plate", 0, 0.5, 6.6, 1.6, 1.0, 1.6, 1)
    o.box("screen", 0, 1.4, 6.6, 1.1, 0.8, 1.1, 1)
    o.box("plate", 0, 2.0, 6.6, 1.4, 0.3, 1.4, 1)
    # scattered crates
    for (x, z, s) in [(-5, -4, 1.0), (-4.2, -4.6, 1.0), (-4.6, -3.4, 1.0),
                      (5.2, 3.5, 1.2), (4.6, -5.0, 0.9)]:
        o.box("plate", x, s / 2, z, s, s, s, 1)
    # wall pipes near the ceiling
    for z in (-6, -2, 2, 6):
        o.box("plate", -R + 0.2, 3.6, z, 0.35, 0.35, 3.4, 2)
    o.box("plate", 3, 3.9, R - 0.2, 6.0, 0.4, 0.35, 3)
    # wall consoles
    o.box("screen", R - 0.15, 1.4, -3, 0.1, 1.0, 1.6, 1)
    o.box("screen", -R + 0.15, 1.6, 4, 0.1, 1.1, 1.8, 1)
    o.save(f"{MOD}/arena.obj", "arena.mtl")

    # shallow water film just above the deck (~2 m caustic period).
    # Single treatment: teal (chosen from the 4-quadrant comparison).
    w = Obj()
    # 24 cells matches the floor grid so painter's-sort buckets align (no holes)
    w.grid("water_teal", 0, 0, 2 * R, 24, 0.05, 8.0)
    w.save(f"{MOD}/arena_water.obj", "arena_water.mtl")


def build_robot():
    """Hover-drone companion, rigid segments. Each OBJ's origin is its bone
    pivot; the rig's bind_pos values (game/anims/robot.json) do the placement.
    """
    # chassis: pivot at hover center
    c = Obj()
    c.box("plate", 0, 0.0, 0, 0.55, 0.34, 0.42, 1)       # main hull
    c.box("plate", 0, -0.24, 0, 0.34, 0.16, 0.28, 1)     # thruster skirt
    c.box("screen", 0, -0.02, 0.215, 0.20, 0.10, 0.02, 1)  # chest light strip
    c.box("plate", 0, 0.20, 0, 0.10, 0.10, 0.10, 1)        # neck post
    c.save(f"{MOD}/robot_chassis.obj", "robot.mtl")

    # head: pivot at the neck (bottom of head)
    h = Obj()
    h.box("plate", 0, 0.17, 0, 0.30, 0.24, 0.30, 1)
    h.box("screen", 0, 0.17, 0.16, 0.22, 0.12, 0.02, 1)  # face visor
    h.box("plate", 0, 0.36, 0, 0.04, 0.14, 0.04, 1)      # antenna
    h.save(f"{MOD}/robot_head.obj", "robot.mtl")

    # arm: pivot at the shoulder, hangs down; shared by both sides
    a = Obj()
    a.box("plate", 0, -0.21, 0, 0.10, 0.40, 0.10, 1)     # upper+forearm
    a.box("plate", 0, -0.46, 0, 0.15, 0.13, 0.15, 1)     # fist
    a.save(f"{MOD}/robot_arm.obj", "robot.mtl")


def main():
    os.makedirs(TEX, exist_ok=True)
    os.makedirs(MOD, exist_ok=True)
    tex_metal_floor(); tex_metal_wall(); tex_metal_plate()
    tex_screen(); tex_water()
    build_arena()
    build_robot()
    print("arena assets generated")


if __name__ == "__main__":
    main()
