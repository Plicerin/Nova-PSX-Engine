#!/usr/bin/env python3
"""Split the imported astronaut (mecha_*.obj, world-space meters y-up) into
rig segments and write game/anims/astro.json (rig + clips reusing the crew
move set). Segments are re-centered on their joint pivots.

Source parts (from untitled.glb):
  Cube        head/helmet          Cube_001  neck
  Cube_003    chest                Cube_004  abdomen/pelvis
  Cube_006    hands (both)         Cube_005  feet (both)
  Cylinder_001 upper arms (both)   Cylinder_002 forearms (both)
  Cylinder    legs (both, hip..ankle -- split at the knee)
"""

import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, "source_assets", "models")

KNEE_Y = 0.58

# pivots (world meters, y-up). bind_pos in astro.json = pivot - parent pivot.
PIVOTS = {
    "pelvis":   (0.00, 1.00, -0.03),
    "torso":    (0.00, 1.26, -0.03),
    "head":     (0.00, 1.66, -0.02),
    "arm_l_up": (-0.16, 1.56, -0.04),
    "arm_l_lo": (-0.20, 1.33, -0.04),
    "arm_r_up": (0.16, 1.56, -0.04),
    "arm_r_lo": (0.20, 1.33, -0.04),
    "leg_l_up": (-0.09, 1.00, -0.03),
    "leg_l_lo": (-0.09, KNEE_Y, -0.03),
    "leg_r_up": (0.09, 1.00, -0.03),
    "leg_r_lo": (0.09, KNEE_Y, -0.03),
}


def read_obj(path):
    v, vn, faces = [], [], []
    for line in open(path):
        p = line.split()
        if not p:
            continue
        if p[0] == 'v':
            v.append(tuple(float(x) for x in p[1:4]))
        elif p[0] == 'vn':
            vn.append(tuple(float(x) for x in p[1:4]))
        elif p[0] == 'f':
            face = []
            for tok in p[1:]:
                sp = tok.split('/')
                vi = int(sp[0]) - 1
                ni = int(sp[2]) - 1 if len(sp) > 2 and sp[2] else None
                face.append((vi, ni))
            faces.append(face)
    return v, vn, faces


def face_centroid(face, v):
    xs = [v[i][0] for (i, _) in face]
    ys = [v[i][1] for (i, _) in face]
    zs = [v[i][2] for (i, _) in face]
    n = float(len(face))
    return (sum(xs) / n, sum(ys) / n, sum(zs) / n)


class SegWriter:
    def __init__(self, pivot):
        self.pivot = pivot
        self.v = []; self.vn = []
        self.vmap = {}; self.nmap = {}
        self.faces = []

    def add(self, verts, norms, faces, keep):
        for face in faces:
            if not keep(face_centroid(face, verts)):
                continue
            out = []
            for (vi, ni) in face:
                key = ('v', id(verts), vi)
                if key not in self.vmap:
                    p = verts[vi]
                    self.v.append((p[0] - self.pivot[0], p[1] - self.pivot[1],
                                   p[2] - self.pivot[2]))
                    self.vmap[key] = len(self.v)
                nkey = ('n', id(norms), ni)
                if ni is not None and nkey not in self.nmap:
                    self.nmap[nkey] = len(self.vn) + 1
                    self.vn.append(norms[ni])
                out.append((self.vmap[key],
                            self.nmap[nkey] if ni is not None else None))
            self.faces.append(out)

    def save(self, path):
        with open(path, 'w') as f:
            f.write('mtllib astro.mtl\nusemtl astro\n')
            for p in self.v:  f.write('v %.5f %.5f %.5f\n' % p)
            for n in self.vn: f.write('vn %.4f %.4f %.4f\n' % n)
            for face in self.faces:
                refs = ['%d//%d' % (vi, ni) if ni else str(vi)
                        for (vi, ni) in face]
                f.write('f ' + ' '.join(refs) + '\n')
        return len(self.faces)


def main():
    parts = {name: read_obj(os.path.join(MOD, 'mecha_%s.obj' % name))
             for name in ['Cube', 'Cube_001', 'Cube_003', 'Cube_004',
                          'Cube_005', 'Cube_006', 'Cylinder',
                          'Cylinder_001', 'Cylinder_002']}

    ALL = lambda c: True
    L = lambda c: c[0] < 0
    R = lambda c: c[0] >= 0

    segs = {
        'astro_head':     [('Cube', ALL)],
        'astro_torso':    [('Cube_003', ALL), ('Cube_001', ALL)],
        'astro_pelvis':   [('Cube_004', ALL)],
        'astro_arm_up_l': [('Cylinder_001', L)],
        'astro_arm_up_r': [('Cylinder_001', R)],
        'astro_arm_lo_l': [('Cylinder_002', L), ('Cube_006', L)],
        'astro_arm_lo_r': [('Cylinder_002', R), ('Cube_006', R)],
        'astro_leg_up_l': [('Cylinder', lambda c: c[0] < 0 and c[1] > KNEE_Y)],
        'astro_leg_up_r': [('Cylinder', lambda c: c[0] >= 0 and c[1] > KNEE_Y)],
        'astro_leg_lo_l': [('Cylinder', lambda c: c[0] < 0 and c[1] <= KNEE_Y),
                           ('Cube_005', L)],
        'astro_leg_lo_r': [('Cylinder', lambda c: c[0] >= 0 and c[1] <= KNEE_Y),
                           ('Cube_005', R)],
    }
    seg_pivot = {
        'astro_head': 'head', 'astro_torso': 'torso', 'astro_pelvis': 'pelvis',
        'astro_arm_up_l': 'arm_l_up', 'astro_arm_up_r': 'arm_r_up',
        'astro_arm_lo_l': 'arm_l_lo', 'astro_arm_lo_r': 'arm_r_lo',
        'astro_leg_up_l': 'leg_l_up', 'astro_leg_up_r': 'leg_r_up',
        'astro_leg_lo_l': 'leg_l_lo', 'astro_leg_lo_r': 'leg_r_lo',
    }

    for name, sources in segs.items():
        w = SegWriter(PIVOTS[seg_pivot[name]])
        for (part, keep) in sources:
            verts, norms, faces = parts[part]
            w.add(verts, norms, faces, keep)
        n = w.save(os.path.join(MOD, name + '.obj'))
        print('%-18s %3d faces' % (name, n))

    # --- rig json: bone order matches the crew rig so clips transfer 1:1
    def bind(child, parent):
        c, p = PIVOTS[child], PIVOTS[parent]
        return [round(c[0] - p[0], 3), round(c[1] - p[1], 3),
                round(c[2] - p[2], 3)]

    bones = [
        {"name": "pelvis", "parent": -1, "mesh": "astro_pelvis",
         "pos": [PIVOTS['pelvis'][0], PIVOTS['pelvis'][1], PIVOTS['pelvis'][2]]},
        {"name": "torso", "parent": "pelvis", "mesh": "astro_torso",
         "pos": bind('torso', 'pelvis')},
        {"name": "head", "parent": "torso", "mesh": "astro_head",
         "pos": bind('head', 'torso')},
        {"name": "arm_l_up", "parent": "torso", "mesh": "astro_arm_up_l",
         "pos": bind('arm_l_up', 'torso')},
        {"name": "arm_l_lo", "parent": "arm_l_up", "mesh": "astro_arm_lo_l",
         "pos": bind('arm_l_lo', 'arm_l_up')},
        {"name": "arm_r_up", "parent": "torso", "mesh": "astro_arm_up_r",
         "pos": bind('arm_r_up', 'torso')},
        {"name": "arm_r_lo", "parent": "arm_r_up", "mesh": "astro_arm_lo_r",
         "pos": bind('arm_r_lo', 'arm_r_up')},
        {"name": "leg_l_up", "parent": "pelvis", "mesh": "astro_leg_up_l",
         "pos": bind('leg_l_up', 'pelvis')},
        {"name": "leg_l_lo", "parent": "leg_l_up", "mesh": "astro_leg_lo_l",
         "pos": bind('leg_l_lo', 'leg_l_up')},
        {"name": "leg_r_up", "parent": "pelvis", "mesh": "astro_leg_up_r",
         "pos": bind('leg_r_up', 'pelvis')},
        {"name": "leg_r_lo", "parent": "leg_r_up", "mesh": "astro_leg_lo_r",
         "pos": bind('leg_r_lo', 'leg_r_up')},
    ]

    crew = json.load(open(os.path.join(ROOT, 'game', 'anims', 'crew.json')))
    clips = []
    for c in crew['clips']:
        c2 = dict(c)
        c2['name'] = c['name'].replace('crew_', 'astro_')
        clips.append(c2)

    astro = {"rig": {"name": "astro", "bones": bones}, "clips": clips}
    out = os.path.join(ROOT, 'game', 'anims', 'astro.json')
    json.dump(astro, open(out, 'w'), indent=1)
    print('rig ->', out)


if __name__ == '__main__':
    main()
