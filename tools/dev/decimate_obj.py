#!/usr/bin/env python3
"""Decimate a textured OBJ to a PS1-budget triangle mesh.

Preserves UVs (averaged per position, nearest-neighbor transferred after
collapse) and recomputes smooth normals. Outputs a clean triangulated OBJ with
`f v/vt/vn`, one UV + one normal per vertex, ready for the engine mesh_importer.

Usage:
  py tools/dev/decimate_obj.py --in IN.obj --out OUT.obj --tris 2000
     [--scale 0.01] [--yup] [--recenter-x] [--floor-y] [--mtl NAME]

--scale multiplies positions (e.g. 0.01 for cm -> m). Coordinates stay Y-up;
the engine importer applies the Y-flip to engine space.
"""
import argparse
import sys

import numpy as np
import fast_simplification as fs


def parse_obj(path):
    pos, uv, faces = [], [], []   # faces: list of [(pi,ti),...] triangulated
    with open(path, "r", errors="ignore") as fh:
        for ln in fh:
            if ln.startswith("v "):
                p = ln.split()[1:4]
                pos.append((float(p[0]), float(p[1]), float(p[2])))
            elif ln.startswith("vt "):
                t = ln.split()[1:3]
                uv.append((float(t[0]), float(t[1])))
            elif ln.startswith("f "):
                corners = ln.split()[1:]
                idx = []
                for c in corners:
                    a = c.split("/")
                    pi = int(a[0])
                    ti = int(a[1]) if len(a) > 1 and a[1] else 0
                    idx.append((pi - 1, ti - 1))
                # fan triangulate
                for k in range(1, len(idx) - 1):
                    faces.append((idx[0], idx[k], idx[k + 1]))
    return pos, uv, faces


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="inp", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--tris", type=int, default=2000)
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--recenter-x", action="store_true")
    ap.add_argument("--floor-y", action="store_true", help="shift so min Y = 0")
    ap.add_argument("--mtl", default="girl")
    args = ap.parse_args()

    pos, uv, faces = parse_obj(args.inp)
    P = np.array(pos, dtype=np.float64)
    UV = np.array(uv, dtype=np.float64) if uv else np.zeros((1, 2))
    tris = np.array([[c[0] for c in f] for f in faces], dtype=np.int64)

    # Average UV per position vertex (positions with no UV get 0).
    uv_sum = np.zeros((len(P), 2))
    uv_cnt = np.zeros((len(P), 1))
    for f in faces:
        for pi, ti in f:
            if ti >= 0:
                uv_sum[pi] += UV[ti]
                uv_cnt[pi] += 1
    uv_cnt[uv_cnt == 0] = 1
    uv_pos = uv_sum / uv_cnt

    print(f"in: {len(P)} verts, {len(tris)} tris", file=sys.stderr)

    reduction = max(0.0, 1.0 - args.tris / len(tris))
    Vout, Fout = fs.simplify(P.astype(np.float32), tris.astype(np.int32),
                             target_reduction=reduction)
    Vout = Vout.astype(np.float64)
    print(f"out: {len(Vout)} verts, {len(Fout)} tris "
          f"(reduction {reduction:.3f})", file=sys.stderr)

    # Nearest original position -> transfer averaged UV (chunked argmin).
    new_uv = np.zeros((len(Vout), 2))
    CH = 512
    for i in range(0, len(Vout), CH):
        blk = Vout[i:i + CH]                      # (b,3)
        d = ((blk[:, None, :] - P[None, :, :]) ** 2).sum(-1)  # (b, Norig)
        nn = d.argmin(1)
        new_uv[i:i + CH] = uv_pos[nn]

    # Recompute area-weighted smooth normals.
    N = np.zeros((len(Vout), 3))
    v0 = Vout[Fout[:, 0]]; v1 = Vout[Fout[:, 1]]; v2 = Vout[Fout[:, 2]]
    fn = np.cross(v1 - v0, v2 - v0)              # area-weighted face normals
    for k in range(3):
        np.add.at(N, Fout[:, k], fn)
    ln = np.linalg.norm(N, axis=1, keepdims=True)
    ln[ln == 0] = 1
    N = N / ln

    # Transform positions.
    Vout = Vout * args.scale
    if args.recenter_x:
        Vout[:, 0] -= (Vout[:, 0].max() + Vout[:, 0].min()) * 0.5
    if args.floor_y:
        Vout[:, 1] -= Vout[:, 1].min()

    with open(args.out, "w") as fh:
        fh.write(f"# decimated to {len(Fout)} tris\n")
        fh.write(f"mtllib {args.mtl}.mtl\n")
        for v in Vout:
            fh.write(f"v {v[0]:.5f} {v[1]:.5f} {v[2]:.5f}\n")
        for t in new_uv:
            fh.write(f"vt {t[0]:.5f} {t[1]:.5f}\n")
        for n in N:
            fh.write(f"vn {n[0]:.5f} {n[1]:.5f} {n[2]:.5f}\n")
        fh.write(f"g {args.mtl}\nusemtl {args.mtl}\n")
        for f in Fout:
            a, b, c = f[0] + 1, f[1] + 1, f[2] + 1
            fh.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")
    print(f"wrote {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
