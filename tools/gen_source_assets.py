#!/usr/bin/env python3
"""
gen_source_assets.py -- deterministic procedural source assets for the nova engine.

Generates (overwriting) the PS1-style source assets:

  source_assets/textures/*.png   RGBA PNG (quantized to 4/8-bit indexed by the
                                 texture importer later)
  source_assets/models/*.obj     y-up, meters, front faces wound CCW when viewed
                                 from the VISIBLE side (outside for props /
                                 inside the room for interior walls)
  source_assets/audio/*.wav      PCM16 mono via the stdlib wave module

Everything derives from fixed random.Random seeds and pure math -- reruns are
byte-identical. Style target: gritty 1997 PS1 industrial chamber.

Every emitted face is machine-checked: the geometric normal computed from the
emitted vertex order must agree with the declared `vn`, must point at the
declared visible side (seen_from= an interior point / out_from= the solid's
center), and quads must be planar. A winding bug aborts generation.
"""

import math
import os
import random
import struct
import wave

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEX_DIR = os.path.join(ROOT, "source_assets", "textures")
MDL_DIR = os.path.join(ROOT, "source_assets", "models")
AUD_DIR = os.path.join(ROOT, "source_assets", "audio")


# ---------------------------------------------------------------------------
# small helpers
# ---------------------------------------------------------------------------

def clamp8(v):
    return max(0, min(255, int(round(v))))


def lerp3(a, b, t):
    return [a[k] + (b[k] - a[k]) * t for k in range(3)]


def blend(dst, src, a):
    """Blend 3-component color dst toward src by factor a (0..1)."""
    return [dst[k] * (1.0 - a) + src[k] * a for k in range(3)]


def buf_to_image(buf, alpha=None):
    """buf: H rows of W [r,g,b]. alpha: None (opaque) or H x W of 0..255."""
    h = len(buf)
    w = len(buf[0])
    img = Image.new("RGBA", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            r, g, b = buf[y][x]
            a = 255 if alpha is None else clamp8(alpha[y][x])
            px[x, y] = (clamp8(r), clamp8(g), clamp8(b), a)
    return img


# ---------------------------------------------------------------------------
# textures
# ---------------------------------------------------------------------------

def tex_floor_tiles():
    """64x64 dark concrete: 2x2 tiles, grout, grime speckle, per-tile shade."""
    rng = random.Random(1001)
    W = H = 64
    # per-tile brightness variation (2x2 grid of 32px tiles)
    tile_off = [[rng.randint(-6, 7) for _ in range(2)] for _ in range(2)]
    buf = [[None] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            gx, gy = x % 32, y % 32
            if gx < 2 or gy < 2:                     # grout (wraps when tiled)
                v = 33 + rng.randint(-2, 2)
                if gx < 2 and gy < 2:
                    v -= 3                           # grout intersections darker
                buf[y][x] = [v, v, v + 3]
            else:
                v = 55 + tile_off[y // 32][x // 32] + rng.randint(-4, 4)
                # low-frequency concrete mottle, phase-shifted per tile
                v += 4.0 * math.sin(x * 0.37 + (y // 32) * 2.1) \
                         * math.sin(y * 0.29 + (x // 32) * 1.3)
                r = rng.random()
                if r < 0.045:                        # dark grime speck
                    v -= rng.randint(8, 16)
                elif r < 0.065:                      # light aggregate fleck
                    v += rng.randint(6, 12)
                buf[y][x] = [v, v, v + 3]
    # a few soft grime stains
    for _ in range(4):
        cx, cy = rng.uniform(4, 60), rng.uniform(4, 60)
        rad, s = rng.uniform(5, 11), rng.uniform(0.10, 0.22)
        for y in range(H):
            for x in range(W):
                d = math.hypot(x - cx, y - cy)
                if d < rad:
                    a = s * (1.0 - d / rad)
                    buf[y][x] = [c * (1.0 - a) for c in buf[y][x]]
    return buf_to_image(buf)


def tex_wall_panel():
    """64x64 industrial panel: hazard stripe, seams, rivets, rust streaks."""
    rng = random.Random(1002)
    W = H = 64
    STRIPE_H = 8
    SEAMS = (8, 32, 56)
    col_off = [rng.randint(-3, 3) for _ in range(W)]   # brushed-metal columns
    buf = [[None] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            if y < STRIPE_H:                         # diagonal warning stripe
                band = ((x - y) // 4) % 2            # period 8 -> tiles at x=64
                c = [196, 166, 44] if band == 0 else [38, 36, 32]
                n = rng.randint(-14, 6)
                c = [c[0] + n, c[1] + n, c[2] + max(-6, n // 2)]
                if y == STRIPE_H - 1:
                    c = [v - 18 for v in c]          # shadow under the stripe
                buf[y][x] = c
            else:
                v = 76 + col_off[x] + rng.randint(-4, 4)
                if y < 32:
                    v += 3                           # subtle per-panel tint
                elif y >= 56:
                    v -= 3
                buf[y][x] = [v - 2, v, v + 3]
    # horizontal seams: dark groove + highlight lip below
    for sy in SEAMS:
        for x in range(W):
            buf[sy][x] = [40 + rng.randint(-2, 2)] * 3
            if sy + 1 < H:
                buf[sy + 1][x] = [c + 16 for c in buf[sy + 1][x]]
    # rivet grid positions (rendered after streaks so heads stay clean)
    rivets = [(rx, sy + 4) for sy in SEAMS for rx in (4, 20, 36, 52)]
    rust = [112, 62, 30]
    for rx, ry in rivets:                            # rust streaks run down
        if rng.random() < 0.7:
            length = rng.randint(6, 22)
            width = rng.choice((1, 1, 2))
            for dy in range(2, length):
                yy = ry + dy
                if yy >= H:
                    break
                a = 0.38 * (1.0 - dy / length)
                for dx in range(width):
                    xx = rx + dx
                    if xx < W:
                        buf[yy][xx] = blend(buf[yy][xx], rust, a)
    for _ in range(3):                               # extra faint grime runs
        sx = rng.randrange(2, 62)
        sy = rng.choice((9, 33))
        length = rng.randint(10, 26)
        for dy in range(length):
            yy = sy + dy
            if yy >= H:
                break
            buf[yy][sx] = blend(buf[yy][sx], [58, 48, 38], 0.25 * (1 - dy / length))
    for rx, ry in rivets:                            # rivet heads: 2x2 + rim
        if ry + 1 < H and rx + 1 < W:
            base = buf[ry][rx][1]
            buf[ry][rx] = [base + 42] * 3
            buf[ry][rx + 1] = [base + 22] * 3
            buf[ry + 1][rx] = [base + 12] * 3
            buf[ry + 1][rx + 1] = [base - 24] * 3    # shadowed corner
            if ry + 2 < H:
                buf[ry + 2][rx] = [c - 14 for c in buf[ry + 2][rx]]
    return buf_to_image(buf)


def tex_crate():
    """64x64 wooden crate face: planks, frame, diagonal brace, scuffs."""
    rng = random.Random(1003)
    W = H = 64
    FR = 6                                            # frame border width
    seps = (19, 32, 45)                               # plank separation rows
    # per-plank shade + grain phase (4 planks inside the frame)
    plank_off = [rng.randint(-8, 8) for _ in range(4)]
    plank_ph = [rng.uniform(0, math.tau) for _ in range(4)]

    def plank_index(y):
        for i, s in enumerate(seps):
            if y < s:
                return i
        return 3

    buf = [[None] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            pi = plank_index(y)
            v = plank_off[pi]
            v += 5.0 * math.sin(x * 0.55 + plank_ph[pi] + y * 0.06)  # grain
            v += rng.randint(-4, 4)
            buf[y][x] = [126 + v, 90 + v * 0.8, 52 + v * 0.6]
    for s in seps:                                    # plank gaps
        for x in range(W):
            buf[s][x] = blend(buf[s][x], [58, 38, 20], 0.7)
            buf[s + 1][x] = [c + 10 for c in buf[s + 1][x]]
    # diagonal brace (corner to corner) over the planks
    for y in range(H):
        for x in range(W):
            d = abs(x - y)
            if d <= 4:
                g = 4.0 * math.sin((x + y) * 0.5 + 1.7)
                c = [104 + g, 72 + g * 0.8, 40 + g * 0.6]
                if d == 4:
                    c = [v - 22 for v in c]           # brace edge shadow
                buf[y][x] = c
    # frame border on top of everything at the rim
    for y in range(H):
        for x in range(W):
            b = min(x, y, W - 1 - x, H - 1 - y)
            if b < FR:
                g = 4.0 * math.sin((x if y < x else y) * 0.7 + 0.4)
                c = [98 + g, 66 + g * 0.8, 36 + g * 0.6]
                if b == 0:
                    c = [v - 20 for v in c]           # outer edge dark
                elif b == FR - 1:
                    c = [v + 14 for v in c]           # inner bevel highlight
                buf[y][x] = [c[0] + rng.randint(-3, 3)] + c[1:]
    for cx, cy in ((3, 3), (60, 3), (3, 60), (60, 60)):   # corner bolts
        buf[cy][cx] = [52, 34, 18]
        buf[cy][cx + 1 if cx < 32 else cx - 1] = [150, 112, 70]
    for _ in range(7):                                # scuffs / scratches
        sx, sy = rng.randint(4, 52), rng.randint(4, 58)
        length = rng.randint(3, 9)
        dy = rng.choice((-1, 0, 0, 1))
        col = [176, 146, 104] if rng.random() < 0.6 else [66, 44, 26]
        yy = float(sy)
        for i in range(length):
            xx = sx + i
            iy = int(yy)
            if 0 <= xx < W and 0 <= iy < H:
                buf[iy][xx] = blend(buf[iy][xx], col, 0.55)
            yy += dy * 0.5
    return buf_to_image(buf)


def tex_gem():
    """64x64 faceted crystal: teal->violet gradient, specular streaks."""
    rng = random.Random(1004)
    W = H = 64
    top, bot = (52, 200, 196), (134, 66, 210)
    buf = [[None] * W for _ in range(H)]
    for y in range(H):
        t = y / (H - 1)
        base = lerp3(top, bot, t)
        for x in range(W):
            # angular facet plates (two interfering step gratings)
            band = (((2 * x + y) // 15) % 3 - 1) * 9 + (((x - 2 * y + 192) // 19) % 2) * 7
            n = rng.randint(-4, 4)
            buf[y][x] = [base[0] + band + n, base[1] + band + n, base[2] + band + n]
    spec = [238, 248, 252]
    for c0, width, strength in ((34, 5.0, 0.85), (62, 3.0, 0.60), (96, 7.0, 0.45)):
        for y in range(H):                            # anti-diagonal streaks
            for x in range(W):
                d = abs(x + y - c0)
                if d < width:
                    a = strength * (1.0 - d / width) ** 2
                    buf[y][x] = blend(buf[y][x], spec, a)
    for y in range(H):                                # one faint counter streak
        for x in range(W):
            d = abs((x - y) + 10)
            if d < 4.0:
                buf[y][x] = blend(buf[y][x], spec, 0.35 * (1.0 - d / 4.0) ** 2)
    return buf_to_image(buf)


def tex_orb_glow():
    """32x32 soft radial glow: warm amber core -> transparent edge."""
    W = H = 32
    cx = cy = 15.5
    R = 15.5
    buf = [[None] * W for _ in range(H)]
    alpha = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            d = math.hypot(x - cx, y - cy)
            if d > R:
                buf[y][x] = [0, 0, 0]                # fully transparent outside
                alpha[y][x] = 0
                continue
            v = (1.0 - d / R) ** 1.5
            buf[y][x] = [255 * v, 205 * v ** 1.3, 90 * v ** 1.9]
            alpha[y][x] = 255 * v
    return buf_to_image(buf, alpha)


# ---------------------------------------------------------------------------
# OBJ models
# ---------------------------------------------------------------------------

def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _norm(v):
    l = math.sqrt(_dot(v, v))
    return (v[0] / l, v[1] / l, v[2] / l)


class ObjBuilder:
    """Collects faces, verifies their winding, writes a v/vt/vn OBJ file."""

    def __init__(self, name, header_note):
        self.header = [
            "# Generated by tools/gen_source_assets.py -- DO NOT EDIT",
            "# y-up, meters. Front faces CCW viewed from the visible side.",
            "# " + header_note,
            "o " + name,
        ]
        self.vs, self.vts, self.vns = [], [], []
        self.vn_idx = {}
        self.body = []
        self.nfaces = 0

    def usemtl(self, m):
        self.body.append("usemtl " + m)

    def comment(self, c):
        self.body.append("# " + c)

    def face(self, pts, uvs, n=None, seen_from=None, out_from=None):
        """Emit a tri/quad in perimeter order; verify winding & visibility."""
        gn = _norm(_cross(_sub(pts[1], pts[0]), _sub(pts[2], pts[0])))
        if n is None:
            n = gn
        assert _dot(gn, n) > 0.999, \
            "winding disagrees with declared normal: %r vs %r" % (gn, n)
        if len(pts) == 4:                              # quads must be planar
            assert abs(_dot(n, _sub(pts[3], pts[0]))) < 1e-6, "non-planar quad"
        cen = tuple(sum(p[k] for p in pts) / len(pts) for k in range(3))
        if seen_from is not None:
            assert _dot(n, _sub(seen_from, cen)) > 1e-6, \
                "face at %r not visible from %r" % (cen, seen_from)
        if out_from is not None:
            assert _dot(n, _sub(cen, out_from)) > 1e-6, \
                "face at %r does not point away from %r" % (cen, out_from)
        key = (round(n[0], 6), round(n[1], 6), round(n[2], 6))
        if key not in self.vn_idx:
            self.vns.append(key)
            self.vn_idx[key] = len(self.vns)
        ni = self.vn_idx[key]
        refs = []
        for p, uv in zip(pts, uvs):
            self.vs.append(tuple(float(c) for c in p))
            self.vts.append(tuple(float(c) for c in uv))
            refs.append("%d/%d/%d" % (len(self.vs), len(self.vts), ni))
        self.body.append("f " + " ".join(refs))
        self.nfaces += 1

    def write(self, path):
        lines = list(self.header)
        for p in self.vs:
            lines.append("v %.4f %.4f %.4f" % p)
        for t in self.vts:
            lines.append("vt %.6f %.6f" % t)
        for nn in self.vns:
            lines.append("vn %.6f %.6f %.6f" % nn)
        lines += self.body
        with open(path, "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")


def build_room():
    o = ObjBuilder("room", "12x12 m chamber, walls 3 m, pedestal at (0,0,4). "
                           "Tiled UVs (importer wraps).")
    inside = (0.0, 1.5, 0.0)                          # interior reference point

    o.comment("floor: 3x3 grid of 4x4 m quads, +y visible, UV 0..3 across floor")
    o.usemtl("floor")
    for j in range(3):            # z rows
        for i in range(3):        # x columns
            x0, z0 = -6.0 + 4 * i, -6.0 + 4 * j
            x1, z1 = x0 + 4, z0 + 4
            u0, u1, v0, v1 = float(i), i + 1.0, float(j), j + 1.0
            o.face([(x0, 0, z0), (x0, 0, z1), (x1, 0, z1), (x1, 0, z0)],
                   [(u0, v0), (u0, v1), (u1, v1), (u1, v0)],
                   n=(0, 1, 0), seen_from=inside)

    o.comment("walls: 3 m high, 3 sections of 4 m, visible from inside;")
    o.comment("u 0..3 per wall, v 0 = bottom .. 1 = top (warning stripe at top)")
    o.usemtl("wall")
    for i in range(3):
        u0, u1 = float(i), i + 1.0
        uvw = [(u0, 0), (u0, 1), (u1, 1), (u1, 0)]    # BL TL TR BR of section
        # north wall z=+6, faces -z (into room)
        x0, x1 = -6.0 + 4 * i, -2.0 + 4 * i
        o.face([(x0, 0, 6), (x0, 3, 6), (x1, 3, 6), (x1, 0, 6)],
               uvw, n=(0, 0, -1), seen_from=inside)
        # south wall z=-6, faces +z
        xa, xb = 6.0 - 4 * i, 2.0 - 4 * i
        o.face([(xa, 0, -6), (xa, 3, -6), (xb, 3, -6), (xb, 0, -6)],
               uvw, n=(0, 0, 1), seen_from=inside)
        # west wall x=-6, faces +x
        z0, z1 = -6.0 + 4 * i, -2.0 + 4 * i
        o.face([(-6, 0, z0), (-6, 3, z0), (-6, 3, z1), (-6, 0, z1)],
               uvw, n=(1, 0, 0), seen_from=inside)
        # east wall x=+6, faces -x
        za, zb = 6.0 - 4 * i, 2.0 - 4 * i
        o.face([(6, 0, za), (6, 3, za), (6, 3, zb), (6, 0, zb)],
               uvw, n=(-1, 0, 0), seen_from=inside)

    o.comment("pedestal: 0.6x0.6 m x 0.8 m tall at (0, 0..0.8, 4), outside-visible")
    px0, px1, pz0, pz1, py1 = -0.3, 0.3, 3.7, 4.3, 0.8
    pc = (0.0, 0.4, 4.0)                              # pedestal center
    uv = [(0, 0), (0, 1), (1, 1), (1, 0)]
    o.face([(px1, 0, pz1), (px1, py1, pz1), (px0, py1, pz1), (px0, 0, pz1)],
           uv, n=(0, 0, 1), out_from=pc)
    o.face([(px0, 0, pz0), (px0, py1, pz0), (px1, py1, pz0), (px1, 0, pz0)],
           uv, n=(0, 0, -1), out_from=pc)
    o.face([(px1, 0, pz0), (px1, py1, pz0), (px1, py1, pz1), (px1, 0, pz1)],
           uv, n=(1, 0, 0), out_from=pc)
    o.face([(px0, 0, pz1), (px0, py1, pz1), (px0, py1, pz0), (px0, 0, pz0)],
           uv, n=(-1, 0, 0), out_from=pc)
    o.face([(px0, py1, pz0), (px0, py1, pz1), (px1, py1, pz1), (px1, py1, pz0)],
           uv, n=(0, 1, 0), out_from=pc)              # top cap (no bottom: on floor)
    return o


def build_crate():
    o = ObjBuilder("crate", "1x1x1 m cube centered at origin (y -0.5..0.5).")
    o.usemtl("crate")
    h = 0.5
    org = (0.0, 0.0, 0.0)
    uv = [(0, 0), (0, 1), (1, 1), (1, 0)]
    o.face([(h, -h, h), (h, h, h), (-h, h, h), (-h, -h, h)],
           uv, n=(0, 0, 1), out_from=org)
    o.face([(-h, -h, -h), (-h, h, -h), (h, h, -h), (h, -h, -h)],
           uv, n=(0, 0, -1), out_from=org)
    o.face([(h, -h, -h), (h, h, -h), (h, h, h), (h, -h, h)],
           uv, n=(1, 0, 0), out_from=org)
    o.face([(-h, -h, h), (-h, h, h), (-h, h, -h), (-h, -h, -h)],
           uv, n=(-1, 0, 0), out_from=org)
    o.face([(-h, h, -h), (-h, h, h), (h, h, h), (h, h, -h)],
           uv, n=(0, 1, 0), out_from=org)
    o.face([(-h, -h, h), (-h, -h, -h), (h, -h, -h), (h, -h, h)],
           uv, n=(0, -1, 0), out_from=org)
    return o


def build_gem():
    o = ObjBuilder("gem", "0.9 m tall 6-sided bipyramid + prism belt, "
                          "flat facet normals (faceted gouraud).")
    o.usemtl("gem")
    R = 0.22                                          # hexagon radius
    apex_t, apex_b = (0.0, 0.45, 0.0), (0.0, -0.45, 0.0)
    ring_t, ring_b = [], []
    for k in range(6):
        th = k * math.pi / 3.0
        ring_t.append((R * math.cos(th), 0.13, R * math.sin(th)))
        ring_b.append((R * math.cos(th), -0.13, R * math.sin(th)))
    org = (0.0, 0.0, 0.0)
    for k in range(6):
        k1 = (k + 1) % 6
        u0, u1 = k / 6.0, (k + 1) / 6.0               # each facet: one UV wedge
        um = 0.5 * (u0 + u1)
        # top pyramid triangle (CCW from outside: apex, next, current)
        o.face([apex_t, ring_t[k1], ring_t[k]],
               [(um, 0.02), (u1, 0.36), (u0, 0.36)], out_from=org)
        # belt quad
        o.face([ring_t[k], ring_t[k1], ring_b[k1], ring_b[k]],
               [(u0, 0.36), (u1, 0.36), (u1, 0.64), (u0, 0.64)], out_from=org)
        # bottom pyramid triangle
        o.face([apex_b, ring_b[k], ring_b[k1]],
               [(um, 0.98), (u0, 0.64), (u1, 0.64)], out_from=org)
    return o


# ---------------------------------------------------------------------------
# audio
# ---------------------------------------------------------------------------

def write_wav(path, rate, samples):
    data = struct.pack("<%dh" % len(samples), *samples)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(data)


def gen_blip():
    """0.12 s square-wave blip, 880->440 Hz exponential sweep, ~-6 dBFS."""
    sr = 22050
    dur = 0.12
    n = int(dur * sr)                                 # 2646 samples
    out = []
    phase = 0.0
    for i in range(n):
        t = i / sr
        f = 880.0 * (0.5 ** (t / dur))                # one octave down over dur
        phase += f / sr
        sq = 1.0 if (phase % 1.0) < 0.5 else -1.0
        env = math.exp(-t * 28.0)                     # exponential decay
        att = min(1.0, i / 16.0)                      # declick attack
        fade = min(1.0, (n - 1 - i) / 64.0)           # declick tail
        s = 0.5 * sq * env * att * fade               # 0.5 peak = -6.02 dBFS
        out.append(max(-32768, min(32767, int(round(s * 32767.0)))))
    return sr, out


def gen_ambience():
    """Exactly 4.0 s seamless drone loop @ 11025 Hz, ~-18 dBFS peak.

    Seamless because every component completes an integer number of cycles
    in 4 s: 55.0 Hz -> 220, 55.25 Hz -> 221, LFO 0.25 Hz -> 1.
    """
    sr = 11025
    n = sr * 4                                        # 44100 samples
    w1 = 2.0 * math.pi * 55.0
    w2 = 2.0 * math.pi * 55.25
    wl = 2.0 * math.pi * 0.25
    raw = []
    for i in range(n):
        t = i / sr
        lfo = 0.7 + 0.3 * math.sin(wl * t - math.pi / 2.0)   # slow swell
        raw.append(lfo * (0.5 * math.sin(w1 * t) + 0.5 * math.sin(w2 * t)))
    # normalize to exactly -18 dBFS peak (uniform gain keeps the loop seamless)
    gain = (10.0 ** (-18.0 / 20.0)) / max(abs(s) for s in raw)
    out = [max(-32768, min(32767, int(round(s * gain * 32767.0)))) for s in raw]
    return sr, out


# ---------------------------------------------------------------------------
# generation + verification
# ---------------------------------------------------------------------------

def main():
    for d in (TEX_DIR, MDL_DIR, AUD_DIR):
        os.makedirs(d, exist_ok=True)

    textures = {
        "floor_tiles_01.png": tex_floor_tiles,
        "wall_panel_01.png": tex_wall_panel,
        "crate_01.png": tex_crate,
        "gem_01.png": tex_gem,
        "orb_glow.png": tex_orb_glow,
    }
    for fname, fn in textures.items():
        path = os.path.join(TEX_DIR, fname)
        fn().save(path)
        print("wrote", os.path.relpath(path, ROOT))

    for fname, builder in (("room.obj", build_room),
                           ("crate.obj", build_crate),
                           ("gem.obj", build_gem)):
        path = os.path.join(MDL_DIR, fname)
        b = builder()
        b.write(path)
        print("wrote %s (%d faces, %d verts)"
              % (os.path.relpath(path, ROOT), b.nfaces, len(b.vs)))

    for fname, gen in (("blip.wav", gen_blip),
                       ("ambience_loop.wav", gen_ambience)):
        path = os.path.join(AUD_DIR, fname)
        rate, samples = gen()
        write_wav(path, rate, samples)
        peak = max(abs(s) for s in samples) / 32767.0
        print("wrote %s (%d Hz, %d samples, peak %.1f dBFS)"
              % (os.path.relpath(path, ROOT), rate, len(samples),
                 20.0 * math.log10(peak)))

    verify()
    print("all chamber assets generated and verified OK")
    gen_game_assets()


def gen_game_assets():
    """Run the other procedural generators (arena, fold-creatures, crew).

    /source_assets/ is gitignored, so a fresh clone has none of this art. It is
    all procedural and these generators are committed, so regenerate it here
    rather than making the asset build fail on a missing .obj. Assets derived
    from third-party GLB drops are NOT regenerable and stay missing; the asset
    build skips those with a warning.
    """
    import importlib
    for mod in ("gen_arena_assets", "gen_shard_assets", "gen_prism_assets",
                "gen_crew_assets"):
        try:
            m = importlib.import_module(mod)
        except ImportError as e:
            print("  skip %s (%s)" % (mod, e))
            continue
        fn = getattr(m, "main", None)
        if fn is None:
            print("  skip %s (no main())" % mod)
            continue
        print("== %s ==" % mod)
        fn()


def verify():
    # PNGs reopen with the right size/mode
    for fname, size in (("floor_tiles_01.png", (64, 64)),
                        ("wall_panel_01.png", (64, 64)),
                        ("crate_01.png", (64, 64)),
                        ("gem_01.png", (64, 64)),
                        ("orb_glow.png", (32, 32))):
        img = Image.open(os.path.join(TEX_DIR, fname))
        img.load()
        assert img.size == size and img.mode == "RGBA", fname
    # orb corners must be fully transparent (outside the glow radius)
    orb = Image.open(os.path.join(TEX_DIR, "orb_glow.png")).load()
    for cx, cy in ((0, 0), (31, 0), (0, 31), (31, 31)):
        assert orb[cx, cy][3] == 0, "orb corner not transparent"
    # OBJ face counts (quads = 1 face line here)
    expect = {"room.obj": 26, "crate.obj": 6, "gem.obj": 18}
    for fname, nf in expect.items():
        with open(os.path.join(MDL_DIR, fname)) as f:
            lines = f.read().splitlines()
        got = sum(1 for l in lines if l.startswith("f "))
        assert got == nf, "%s: %d faces, expected %d" % (fname, got, nf)
        assert any(l.startswith("vn ") for l in lines), fname + ": no normals"
    # WAV params
    for fname, rate, frames in (("blip.wav", 22050, 2646),
                                ("ambience_loop.wav", 11025, 44100)):
        with wave.open(os.path.join(AUD_DIR, fname), "rb") as w:
            assert w.getnchannels() == 1 and w.getsampwidth() == 2, fname
            assert w.getframerate() == rate, fname
            assert w.getnframes() == frames, fname


if __name__ == "__main__":
    main()
