#!/usr/bin/env python3
"""UV/seam-aware mesh decimation via pymeshlab (quadric edge collapse with
texture), then a plain-numpy transform pass (scale / recenter-x / floor-y) that
preserves texcoords and normals. Keeps clothing seams, face, and hair far
better than a position-only collapse.

Usage:
  py tools/dev/decimate_tex.py --in IN.obj --out OUT.obj --faces 6000
     [--scale 0.01] [--recenter-x] [--floor-y]
"""
import argparse
import os
import sys

import pymeshlab as ml


def transform_obj(src, dst, scale, recenter_x, floor_y):
    verts = []
    lines = open(src, "r", errors="ignore").read().splitlines()
    for ln in lines:
        if ln.startswith("v "):
            verts.append([float(x) for x in ln.split()[1:4]])
    xs = [v[0] for v in verts]; ys = [v[1] for v in verts]
    tx = -(max(xs) + min(xs)) * 0.5 if recenter_x else 0.0
    ty = -min(ys) * scale if floor_y else 0.0
    with open(dst, "w") as f:
        f.write("mtllib girl.mtl\n")
        for ln in lines:
            if ln.startswith("v "):
                p = ln.split()
                x = float(p[1]) * scale + tx * scale
                y = float(p[2]) * scale + ty
                z = float(p[3]) * scale
                f.write(f"v {x:.5f} {y:.5f} {z:.5f}\n")
            elif ln.startswith(("vt ", "vn ", "f ", "g ", "usemtl ", "o ")):
                f.write(ln + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="inp", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--faces", type=int, default=6000)
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--recenter-x", action="store_true")
    ap.add_argument("--floor-y", action="store_true")
    args = ap.parse_args()

    ms = ml.MeshSet()
    ms.load_new_mesh(args.inp)
    m = ms.current_mesh()
    print(f"in: {m.vertex_number()} verts, {m.face_number()} faces", file=sys.stderr)

    ms.meshing_decimation_quadric_edge_collapse_with_texture(
        targetfacenum=args.faces,
        preserveboundary=True,
        boundaryweight=2.0,
        preservenormal=True,
        planarquadric=True,
        optimalplacement=True,
        extratcoordw=2.0,
        qualitythr=0.4,
    )
    m = ms.current_mesh()
    print(f"out: {m.vertex_number()} verts, {m.face_number()} faces", file=sys.stderr)
    ms.compute_normal_per_vertex()
    # Move per-wedge UVs onto vertices, then write the OBJ ourselves from the
    # numpy arrays (pymeshlab's OBJ texcoord exporter crashes in this build).
    ms.compute_texcoord_transfer_wedge_to_vertex()
    m = ms.current_mesh()

    V = m.vertex_matrix().astype(float)          # (N,3)
    N = m.vertex_normal_matrix().astype(float)    # (N,3)
    T = m.vertex_tex_coord_matrix().astype(float) # (N,2)
    F = m.face_matrix()                           # (M,3) 0-based

    tx = -(V[:, 0].max() + V[:, 0].min()) * 0.5 if args.recenter_x else 0.0
    V[:, 0] += tx
    V *= args.scale
    if args.floor_y:
        V[:, 1] -= V[:, 1].min()

    with open(args.out, "w") as f:
        f.write("mtllib girl.mtl\ng girl\nusemtl girl\n")
        for p in V:
            f.write(f"v {p[0]:.5f} {p[1]:.5f} {p[2]:.5f}\n")
        for t in T:
            f.write(f"vt {t[0]:.5f} {t[1]:.5f}\n")
        for n in N:
            f.write(f"vn {n[0]:.5f} {n[1]:.5f} {n[2]:.5f}\n")
        for tri in F:
            a, b, c = int(tri[0]) + 1, int(tri[1]) + 1, int(tri[2]) + 1
            f.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")
    print(f"wrote {args.out}: {len(V)} verts, {len(F)} faces", file=sys.stderr)


if __name__ == "__main__":
    main()
