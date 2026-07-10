#!/usr/bin/env python3
"""GLB -> OBJ extractor for the asset pipeline (pure python).

Parses a binary glTF, applies node world transforms, and writes:
  --merge:    one OBJ with a `usemtl` group per material
  --per-node: one OBJ per mesh-carrying node (origin kept = rig pivots stay)
Embedded PNG textures are saved next to the OBJs as <name>_tex<N>.png.
Skins/animations are ignored (meshes come out in bind pose).

Usage:
  py tools/glb_import.py --in file.glb --name robo --out-dir source_assets/models [--merge]
"""

import argparse
import json
import os
import struct
import sys

_CTYPE = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2),
          5125: ('I', 4), 5126: ('f', 4)}
_CCOUNT = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def load_glb(path):
    with open(path, 'rb') as f:
        magic, _ver, _total = struct.unpack('<4sII', f.read(12))
        if magic != b'glTF':
            raise SystemExit('%s: not a GLB' % path)
        gltf = None
        bin_chunk = b''
        while True:
            hdr = f.read(8)
            if len(hdr) < 8:
                break
            clen, ctype = struct.unpack('<I4s', hdr)
            data = f.read(clen)
            if ctype == b'JSON':
                gltf = json.loads(data)
            elif ctype == b'BIN\x00':
                bin_chunk = data
    return gltf, bin_chunk


def read_accessor(gltf, binc, idx):
    acc = gltf['accessors'][idx]
    bv = gltf['bufferViews'][acc['bufferView']]
    fmt, csize = _CTYPE[acc['componentType']]
    ncomp = _CCOUNT[acc['type']]
    count = acc['count']
    base = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    stride = bv.get('byteStride', csize * ncomp)
    out = []
    for i in range(count):
        off = base + i * stride
        vals = struct.unpack_from('<%d%s' % (ncomp, fmt), binc, off)
        out.append(vals if ncomp > 1 else vals[0])
    return out


def mat_identity():
    return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def mat_mul(a, b):  # column-major 4x4 (glTF convention)
    o = [0.0] * 16
    for c in range(4):
        for r in range(4):
            o[c * 4 + r] = sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4))
    return o


def node_local(n):
    if 'matrix' in n:
        return list(n['matrix'])
    t = n.get('translation', [0, 0, 0])
    q = n.get('rotation', [0, 0, 0, 1])
    s = n.get('scale', [1, 1, 1])
    x, y, z, w = q
    rot = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
           2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
           2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]
    m = mat_identity()
    for c in range(3):
        for r in range(3):
            m[c * 4 + r] = rot[c * 3 + r] * s[c]
    m[12], m[13], m[14] = t
    return m


def xform_point(m, p):
    return (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
            m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14])


def xform_dir(m, p):
    return (m[0] * p[0] + m[4] * p[1] + m[8] * p[2],
            m[1] * p[0] + m[5] * p[1] + m[9] * p[2],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2])


def world_transforms(gltf):
    nodes = gltf.get('nodes', [])
    world = [None] * len(nodes)

    def walk(idx, parent):
        m = mat_mul(parent, node_local(nodes[idx]))
        world[idx] = m
        for c in nodes[idx].get('children', []):
            walk(c, m)

    scene = gltf.get('scenes', [{}])[gltf.get('scene', 0)]
    for root in scene.get('nodes', []):
        walk(root, mat_identity())
    for i in range(len(nodes)):          # orphans: local only
        if world[i] is None:
            world[i] = node_local(nodes[i])
    return world


def emit_obj(f, prims, name):
    """prims: list of (mtl, verts, norms, uvs, tris). Indices are local."""
    f.write('# extracted by glb_import.py\n')
    vbase = tbase = nbase = 1
    for (mtl, verts, norms, uvs, tris) in prims:
        f.write('o %s\nusemtl %s\n' % (name, mtl))
        for v in verts:
            f.write('v %.5f %.5f %.5f\n' % v)
        for t in uvs:
            f.write('vt %.5f %.5f\n' % (t[0], 1.0 - t[1]))  # glTF v-down
        for n in norms:
            f.write('vn %.4f %.4f %.4f\n' % n)
        has_uv, has_n = len(uvs) > 0, len(norms) > 0
        for (a, b, c) in tris:
            def ref(i):
                s = str(vbase + i)
                if has_uv and has_n:
                    return '%d/%d/%d' % (vbase + i, tbase + i, nbase + i)
                if has_n:
                    return '%d//%d' % (vbase + i, nbase + i)
                if has_uv:
                    return '%d/%d' % (vbase + i, tbase + i)
                return s
            f.write('f %s %s %s\n' % (ref(a), ref(b), ref(c)))
        vbase += len(verts); tbase += len(uvs); nbase += len(norms)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--in', dest='src', required=True)
    ap.add_argument('--name', required=True)
    ap.add_argument('--out-dir', default='source_assets/models')
    ap.add_argument('--tex-dir', default='source_assets/textures')
    ap.add_argument('--merge', action='store_true',
                    help='single OBJ (default: one OBJ per mesh node)')
    args = ap.parse_args()

    gltf, binc = load_glb(args.src)
    os.makedirs(args.out_dir, exist_ok=True)
    os.makedirs(args.tex_dir, exist_ok=True)

    # textures
    for i, img in enumerate(gltf.get('images', [])):
        if 'bufferView' not in img:
            continue
        bv = gltf['bufferViews'][img['bufferView']]
        data = binc[bv.get('byteOffset', 0):bv.get('byteOffset', 0) + bv['byteLength']]
        ext = 'png' if img.get('mimeType', '').endswith('png') else 'jpg'
        tp = os.path.join(args.tex_dir, '%s_tex%d.%s' % (args.name, i, ext))
        with open(tp, 'wb') as f:
            f.write(data)
        print('texture ->', tp)

    world = world_transforms(gltf)
    mats = gltf.get('materials', [])

    def prim_data(prim, m):
        attrs = prim['attributes']
        verts = [xform_point(m, p) for p in read_accessor(gltf, binc, attrs['POSITION'])]
        norms = [xform_dir(m, n) for n in read_accessor(gltf, binc, attrs['NORMAL'])] \
            if 'NORMAL' in attrs else []
        uvs = read_accessor(gltf, binc, attrs['TEXCOORD_0']) \
            if 'TEXCOORD_0' in attrs else []
        idx = read_accessor(gltf, binc, prim['indices']) if 'indices' in prim \
            else list(range(len(verts)))
        tris = [(idx[i], idx[i + 1], idx[i + 2]) for i in range(0, len(idx), 3)]
        mi = prim.get('material')
        mtl = mats[mi].get('name', 'mat%d' % mi) if mi is not None else 'default'
        return (mtl.replace(' ', '_'), verts, norms, uvs, tris)

    lo = [1e9] * 3; hi = [-1e9] * 3
    all_prims = []
    per_node = []
    for ni, node in enumerate(gltf.get('nodes', [])):
        if 'mesh' not in node:
            continue
        mesh = gltf['meshes'][node['mesh']]
        prims = [prim_data(p, world[ni]) for p in mesh['primitives']]
        nname = (node.get('name') or 'node%d' % ni).replace(' ', '_').replace('.', '_')
        per_node.append((nname, prims))
        all_prims += prims
        for (_, verts, _, _, _) in prims:
            for v in verts:
                for c in range(3):
                    lo[c] = min(lo[c], v[c]); hi[c] = max(hi[c], v[c])

    if args.merge:
        path = os.path.join(args.out_dir, args.name + '.obj')
        with open(path, 'w') as f:
            emit_obj(f, all_prims, args.name)
        print('obj ->', path)
    else:
        for (nname, prims) in per_node:
            path = os.path.join(args.out_dir, '%s_%s.obj' % (args.name, nname))
            with open(path, 'w') as f:
                emit_obj(f, prims, nname)
            print('obj ->', path)

    print('bounds: x[%.3f..%.3f] y[%.3f..%.3f] z[%.3f..%.3f]'
          % (lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
    print('size:   %.3f x %.3f x %.3f' % (hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]))


if __name__ == '__main__':
    sys.exit(main())
