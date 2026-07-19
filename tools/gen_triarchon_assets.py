#!/usr/bin/env python3
"""TRIARCHON enemy (see triarchon-design / triarchon-animation-model memory).
Black triangle predator, fixed 0.18 m equilateral triangles via tri_build.

Stage 1: FODDER crawler, now an ARTICULATED CLUSTER RIG. Each cluster (thorax,
head, and per-leg thigh/shin) is a rigid patch of unit triangles = one bone.
Legs fold at horizontal hip/knee hinges (bind_rot [0, az+/-90, 0], the proven
petal-hinge pattern). The red eye is one real head face (edge-to-edge). A glow
rides the 'eye' bone at runtime for visibility.

Rest pose: the engine's rig leaves each bone at the parent's orientation, so
every cluster mesh is authored in its final WORLD-REST orientation with its
pivot at the origin; bind_pos then places it. Key rotations fold the joints.
"""

import json
import math
import os

from tri_build import TriBuild, _add, _mul, _norm, _sub, _cross

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, "source_assets", "models")
ANIM = os.path.join(ROOT, "game", "anims")

E = 0.18                                   # ~7 inches, the fixed unit edge
BODY_Y = 0.30
# Three clean L/R pairs (front / mid / rear), front cone clear for the head.
# Order: front-R, front-L, mid-R, mid-L, rear-R, rear-L.
LEG_AZ = (40, 140, 0, 180, 220, 320)
# Gait phase per leg (from the real spider walk cycle: contralateral pairs a
# half-cycle apart, metachronal wave front->rear down each side).
LEG_PHASE = (0.0, 0.5, 0.17, 0.67, 0.33, 0.83)


def tube_cluster(axis, nbands, twist):
    """A limb-segment mesh at the origin, along `axis` (world-rest). Returns
    (TriBuild, end_offset)."""
    t = TriBuild(E=E)
    end = t.tube((0.0, 0.0, 0.0), _norm(axis), nbands, twist0=twist)
    return t, end


def head_cluster():
    """Head block minus one front-upper face (pulled out to become the eye)."""
    t = TriBuild(E=E)
    t.octahedron((0.0, 0.0, 0.0), a=E / math.sqrt(2))

    def score(i):
        A, B, C = t.tris[i]
        return (A[2] + B[2] + C[2]) / 3 + 0.5 * (A[1] + B[1] + C[1]) / 3
    eye_i = max(range(t.count()), key=score)
    eye_face = t.tris[eye_i]
    del t.tris[eye_i]
    return t, eye_face


def save_cluster(tb, path, mtl):
    tb.save(path, "triar.mtl", mtl)


def build_rigged():
    """Returns (bones, meshes, eye_rel). bones: list of dicts for the rig JSON;
    meshes: {name: TriBuild} to save; eye_rel: the eye face verts vs centroid."""
    bones, meshes = [], {}

    # --- thorax (root) ---
    tho, tho_end = tube_cluster((0, 0, 1), 3, 0)
    meshes["triar1_thorax"] = tho
    thorax_piv = (0.0, BODY_Y, -0.22)
    bones.append({"name": "thorax", "parent": -1, "mesh": "triar1_thorax",
                  "pos": list(thorax_piv), "rot": [0, 0, 0]})

    # --- head + eye ---
    head_piv = (0.0, BODY_Y + 0.05, thorax_piv[2] + tho_end[2] + 0.08)
    hc, eye_face = head_cluster()
    meshes["triar1_head"] = hc
    bones.append({"name": "head", "parent": "thorax", "mesh": "triar1_head",
                  "pos": list(_sub(head_piv, thorax_piv)), "rot": [0, 0, 0]})
    eyeC = _mul(_add(_add(eye_face[0], eye_face[1]), eye_face[2]), 1.0 / 3)
    eye_rel = [_sub(v, eyeC) for v in eye_face]
    bones.append({"name": "eye", "parent": "head", "mesh": "triar1_eye",
                  "pos": [round(c, 4) for c in eyeC], "rot": [0, 0, 0]})

    # --- six two-segment legs ---
    for i, az in enumerate(LEG_AZ):
        a = math.radians(az)
        outx, outz = math.cos(a), math.sin(a)
        hip = (outx * 0.14, BODY_Y + 0.02, outz * 0.14)
        thigh_axis = _norm((outx * 0.9, 0.45, outz * 0.9))
        th, th_end = tube_cluster(thigh_axis, 2, az)
        meshes["triar1_thigh%d" % i] = th
        bones.append({"name": "thigh%d" % i, "parent": "thorax",
                      "mesh": "triar1_thigh%d" % i,
                      "pos": [round(c, 4) for c in _sub(hip, thorax_piv)],
                      "rot": [0, az + 90, 0]})       # horizontal hip hinge
        shin_axis = _norm((outx * 0.55, -1.0, outz * 0.55))
        sh, _ = tube_cluster(shin_axis, 3, az + 30)
        meshes["triar1_shin%d" % i] = sh
        bones.append({"name": "shin%d" % i, "parent": "thigh%d" % i,
                      "mesh": "triar1_shin%d" % i,
                      "pos": [round(c, 4) for c in th_end],   # knee, in thigh frame
                      "rot": [0, az + 90, 0]})       # horizontal knee hinge

    return bones, meshes, eye_rel


def _gait_pose(u):
    """Per-leg pose at cycle position u in [0,1). Recovery (swing forward,
    lifted) for the first `duty` of the cycle, then a planted stance sweeping
    the foot back (the power stroke). Returns (thigh_lift_rx, thigh_swing_ry,
    shin_rx). Amplitudes follow the spider: big-ish lift, moderate fore/aft."""
    duty = 0.45
    swr, lifta = 26.0, 30.0
    u %= 1.0
    if u < duty:                         # recovery: lift + swing back->front
        t = u / duty
        lift = math.sin(math.pi * t) * lifta
        swing = -swr + 2 * swr * t
    else:                               # stance: planted, sweep front->back
        t = (u - duty) / (1 - duty)
        lift = 0.0
        swing = swr - 2 * swr * t
    shin = -(8.0 + 0.7 * lift)
    return lift, swing, shin


def walk_clip():
    """Phased spider gait ported from hi-fi-spider.glb: each leg runs the same
    stride offset by LEG_PHASE (contralateral pairs half a cycle apart, a
    metachronal wave front->rear). ~0.68 s cycle, matching the reference."""
    K = 8
    keys = []
    for k in range(K):
        p = k / float(K)
        fr = {}
        for i in range(6):
            lift, swing, shin = _gait_pose(p - LEG_PHASE[i])
            fr["thigh%d" % i] = {"rot": [round(lift, 1), round(swing, 1), 0]}
            fr["shin%d" % i] = {"rot": [round(shin, 1), 0, 0]}
        fr["thorax"] = {"pos": [0, round(0.02 * math.sin(4 * math.pi * p), 3), 0]}
        keys.append(fr)
    return {"name": "triar1_walk", "loop": True, "key_ms": 85, "keys": keys}


def idle_clip():
    keys = []
    for k in range(4):
        s = [0, 6, 0, -6][k]
        fr = {"thorax": {"pos": [0, [0, 0.012, 0, 0.012][k], 0]}}
        for i in range(6):
            fr["shin%d" % i] = {"rot": [s, 0, 0]}
        keys.append(fr)
    return {"name": "triar1_idle", "loop": True, "key_ms": 460, "keys": keys}


# --------------------------------------------------------- serpent form
SNAKE_SEGS = 9


def build_snake(name):
    """A serpent: a chain of tube segments (each a bone hinged to the previous)
    that undulates via a travelling wave. Head + red eye at the front. Same
    fixed-triangle clusters as the crawler, arranged as a spine instead of a
    body+legs -- this is how the species can read as a snake."""
    bones, meshes = [], {}
    seglen = E * math.sqrt(2.0 / 3.0) * 2      # a 2-band tube's length
    for s in range(SNAKE_SEGS):
        seg, _ = tube_cluster((0, 0, -1), 2, s * 24)   # extend -z (behind head)
        meshes["%s_seg%d" % (name, s)] = seg
        if s == 0:
            bones.append({"name": "seg0", "parent": -1, "mesh": "%s_seg0" % name,
                          "pos": [0, BODY_Y, 0], "rot": [0, 0, 0]})
        else:
            bones.append({"name": "seg%d" % s, "parent": "seg%d" % (s - 1),
                          "mesh": "%s_seg%d" % (name, s),
                          "pos": [0, 0, round(-seglen, 4)], "rot": [0, 0, 0]})
    # head at the front (+z) of seg0
    hc, eye_face = head_cluster()
    meshes["%s_head" % name] = hc
    head_piv = (0.0, 0.03, 0.14)
    bones.append({"name": "head", "parent": "seg0", "mesh": "%s_head" % name,
                  "pos": [round(c, 4) for c in head_piv], "rot": [0, 0, 0]})
    eyeC = _mul(_add(_add(eye_face[0], eye_face[1]), eye_face[2]), 1.0 / 3)
    eye_rel = [_sub(v, eyeC) for v in eye_face]
    bones.append({"name": "eye", "parent": "head", "mesh": "%s_eye" % name,
                  "pos": [round(c, 4) for c in eyeC], "rot": [0, 0, 0]})
    return bones, meshes, eye_rel


def slither_clip(name):
    """Horizontal S-wave travelling head->tail (key ry bends each segment about
    the vertical). Amplitude grows toward the tail for a whip."""
    K = 8
    keys = []
    for k in range(K):
        p = k / float(K)
        fr = {}
        for s in range(SNAKE_SEGS):
            amp = 7.0 + s * 1.6
            ry = amp * math.sin(2 * math.pi * p - s * 0.9)
            fr["seg%d" % s] = {"rot": [0, round(ry, 1), 0]}
        keys.append(fr)
    return {"name": "%s_walk" % name, "loop": True, "key_ms": 90, "keys": keys}


def write_creature(name, bones, meshes, eye_rel, clips):
    for mn, tb in meshes.items():
        save_cluster(tb, os.path.join(MOD, mn + ".obj"), "body")
    eye = ("mtllib triar.mtl\nusemtl eye\n"
           + "".join("v %.4f %.4f %.4f\n" % v for v in eye_rel))
    n = _norm(_cross(_sub(eye_rel[1], eye_rel[0]), _sub(eye_rel[2], eye_rel[0])))
    eye += "vn %.4f %.4f %.4f\nf 1//1 2//1 3//1\n" % n
    with open(os.path.join(MOD, name + "_eye.obj"), "w") as f:
        f.write(eye)
    rig = {"rig": {"name": name, "bones": bones}, "clips": clips}
    with open(os.path.join(ANIM, name + ".json"), "w", encoding="utf-8") as f:
        json.dump(rig, f, indent=1)
    print("%s: %d bones, %d meshes" % (name, len(bones), len(meshes) + 1))
    print("MESHES:", ",".join(sorted(meshes.keys())))


def main():
    os.makedirs(MOD, exist_ok=True)
    os.makedirs(ANIM, exist_ok=True)
    bones, meshes, eye_rel = build_rigged()
    write_creature("triar1", bones, meshes, eye_rel, [idle_clip(), walk_clip()])
    sb, sm, se = build_snake("triarsnake")
    write_creature("triarsnake", sb, sm, se, [slither_clip("triarsnake")])


if __name__ == "__main__":
    main()
