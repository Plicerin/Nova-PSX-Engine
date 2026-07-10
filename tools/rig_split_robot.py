#!/usr/bin/env python3
"""Split the imported PS1 robot (ps1__robot.glb) into rigid rig segments.

The GLB's skin has no vertex weights (Sketchfab export), so triangles are
assigned to the nearest bone segment (line between a joint and its child)
instead — robust for humanoids in bind pose. Writes unit7_*.obj segments
(scaled to meters, recentered on joint pivots, UVs kept) and
game/anims/unit7.json with the crew clip set transferred.
"""

import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from glb_import import load_glb, world_transforms, read_accessor, xform_point, xform_dir

SRC = r'C:\Users\admin\Downloads\ps1__robot.glb'
MOD = os.path.join(ROOT, 'source_assets', 'models')
SCALE = 0.15

# bone -> (pivot joint, segment end joint) by armature names (prefix match)
SEGS = {
    'pelvis':   ('Hips', 'Spine01'),
    'torso':    ('Spine01', 'Neck'),
    'head':     ('Neck', 'Head_LookAt'),      # up through the head
    'arm_l_up': ('UpperArm.l', 'LowerArm.l'),
    'arm_l_lo': ('LowerArm.l', 'Hand.l'),
    'arm_r_up': ('UpperArm.r', 'LowerArm.r'),
    'arm_r_lo': ('LowerArm.r', 'Hand.r'),
    'leg_l_up': ('UpperLeg.l', 'LowerLeg.l'),
    'leg_l_lo': ('LowerLeg.l', 'Foot.l'),
    'leg_r_up': ('UpperLeg.r', 'LowerLeg.r'),
    'leg_r_lo': ('LowerLeg.r', 'Foot.r'),
}
PARENT = {
    'pelvis': None, 'torso': 'pelvis', 'head': 'torso',
    'arm_l_up': 'torso', 'arm_l_lo': 'arm_l_up',
    'arm_r_up': 'torso', 'arm_r_lo': 'arm_r_up',
    'leg_l_up': 'pelvis', 'leg_l_lo': 'leg_l_up',
    'leg_r_up': 'pelvis', 'leg_r_lo': 'leg_r_up',
}
# feet/toes/hands hang below their segment end: extend those capsules
EXTEND = {'arm_l_lo': 0.8, 'arm_r_lo': 0.8, 'leg_l_lo': 1.2, 'leg_r_lo': 1.2,
          'head': 0.0, 'pelvis': 0.6}


def seg_dist(p, a, b):
    ab = tuple(b[i] - a[i] for i in range(3))
    ap = tuple(p[i] - a[i] for i in range(3))
    denom = sum(c * c for c in ab) or 1e-9
    t = max(0.0, min(1.0, sum(ap[i] * ab[i] for i in range(3)) / denom))
    d = tuple(ap[i] - ab[i] * t for i in range(3))
    return sum(c * c for c in d)


def main():
    gltf, binc = load_glb(SRC)
    nodes = gltf['nodes']
    world = world_transforms(gltf)

    def joint_pos(prefix):
        for ni, n in enumerate(nodes):
            if n.get('name', '').startswith(prefix):
                m = world[ni]
                return (m[12], m[13], m[14])
        raise SystemExit('joint not found: ' + prefix)

    piv = {}
    ends = {}
    for bone, (ja, jb) in SEGS.items():
        a, b = joint_pos(ja), joint_pos(jb)
        ext = EXTEND.get(bone, 0.0)
        if ext:                                # push the far end outward
            b = tuple(b[i] + (b[i] - a[i]) * ext for i in range(3))
        piv[bone] = a
        ends[bone] = (a, b)

    # mesh geometry (world space)
    ni = next(i for i, n in enumerate(nodes) if 'mesh' in n)
    prim = gltf['meshes'][nodes[ni]['mesh']]['primitives'][0]
    m = world[ni]
    attrs = prim['attributes']
    verts = [xform_point(m, p) for p in read_accessor(gltf, binc, attrs['POSITION'])]
    norms = [xform_dir(m, n) for n in read_accessor(gltf, binc, attrs['NORMAL'])]
    uvs = read_accessor(gltf, binc, attrs['TEXCOORD_0'])
    idx = read_accessor(gltf, binc, prim['indices'])
    tris = [(idx[i], idx[i + 1], idx[i + 2]) for i in range(0, len(idx), 3)]

    buckets = {b: [] for b in SEGS}
    for tri in tris:
        c = tuple(sum(verts[v][i] for v in tri) / 3.0 for i in range(3))
        best = min(SEGS, key=lambda bone: seg_dist(c, *ends[bone]))
        buckets[best].append(tri)

    for bone, faces in buckets.items():
        path = os.path.join(MOD, 'unit7_%s.obj' % bone)
        vmap = {}
        with open(path, 'w') as f:
            f.write('mtllib unit7.mtl\nusemtl unit7\n')
            order = []
            for tri in faces:
                for v in tri:
                    if v not in vmap:
                        vmap[v] = len(vmap) + 1
                        order.append(v)
            p0 = piv[bone]
            for v in order:
                p = verts[v]
                f.write('v %.5f %.5f %.5f\n' % ((p[0] - p0[0]) * SCALE,
                                                (p[1] - p0[1]) * SCALE,
                                                (p[2] - p0[2]) * SCALE))
            for v in order:
                f.write('vt %.5f %.5f\n' % (uvs[v][0], 1.0 - uvs[v][1]))
            for v in order:
                f.write('vn %.4f %.4f %.4f\n' % norms[v])
            for tri in faces:
                f.write('f ' + ' '.join('%d/%d/%d' % ((vmap[v],) * 3)
                                        for v in tri) + '\n')
        print('unit7_%-9s %4d tris' % (bone, len(faces)))

    # rig json (crew bone order; positions y-up meters)
    def P(bone):
        p = piv[bone]
        return [round(p[0] * SCALE, 3), round(p[1] * SCALE, 3),
                round(p[2] * SCALE, 3)]

    def bind(bone):
        par = PARENT[bone]
        if par is None:
            return P(bone)
        a, b = P(bone), P(par)
        return [round(a[i] - b[i], 3) for i in range(3)]

    order = ['pelvis', 'torso', 'head', 'arm_l_up', 'arm_l_lo',
             'arm_r_up', 'arm_r_lo', 'leg_l_up', 'leg_l_lo',
             'leg_r_up', 'leg_r_lo']
    bones = []
    for bone in order:
        bones.append({'name': bone,
                      'parent': PARENT[bone] if PARENT[bone] else -1,
                      'mesh': 'unit7_%s' % bone,
                      'pos': bind(bone)})

    crew = json.load(open(os.path.join(ROOT, 'game', 'anims', 'crew.json')))
    clips = []
    for c in crew['clips']:
        c2 = dict(c)
        c2['name'] = c['name'].replace('crew_', 'unit7_')
        clips.append(c2)
    out = os.path.join(ROOT, 'game', 'anims', 'unit7.json')
    json.dump({'rig': {'name': 'unit7', 'bones': bones}, 'clips': clips},
              open(out, 'w'), indent=1)
    print('rig ->', out)


if __name__ == '__main__':
    main()
