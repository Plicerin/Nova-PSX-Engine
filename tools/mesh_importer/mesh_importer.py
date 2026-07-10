#!/usr/bin/env python3
"""Mesh importer: Wavefront OBJ -> .meshbin (+ .json summary).

Engine-space conversion (docs/file_formats.md):
  - y = -y for positions AND normals (OBJ is y-up, engine is y-down);
    vertex order is KEPT (winding is not reversed).
  - Positions: * scale * 256 (WORLD_SCALE), rounded, must fit i16.
  - Normals: normalized 4.12 (4096 = 1.0), per-vertex (dedup by (v,vn) pair).
  - Quads: OBJ ring (r0,r1,r2,r3) is stored Z-pattern (r0,r1,r3,r2) =
    top-left, top-right, bottom-left, bottom-right; runtime splits
    (0,1,2)+(1,3,2).
  - UVs -> texel coords: v = 1 - v_obj; if the face's max |uv| <= 1.001 the
    face maps edge-to-edge (scale by w-1/h-1), else it tiles (scale by w/h,
    wrap & 255). Texture sizes come from the texture's .json metadata in
    --textures-dir.
  - Prim types: textured + --lit + normals -> GT3/GT4, textured -> FT3/FT4,
    untextured -> G3/G4; all vertex colors 128 (neutral gray).
"""

import argparse
import json
import math
import os
import sys

_TOOLS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _TOOLS_DIR not in sys.path:
    sys.path.insert(0, _TOOLS_DIR)

from common import psx_formats as pf

TRI_BUDGET_WARN = 800      # spec 7.2 prop budget


# --- OBJ parsing -----------------------------------------------------------------

def parse_obj(path):
    """Returns (verts, texcoords, normals, faces).
    faces: list of (mtl_name_or_None, [(vi, ti, ni), ...]) with 0-based
    indices already resolved (negative OBJ indices handled); ti/ni may be None.
    """
    vs, vts, vns, faces = [], [], [], []
    cur_mtl = None

    def _resolve(idx, count, what, lineno):
        if idx > 0:
            i = idx - 1
        elif idx < 0:
            i = count + idx
        else:
            raise pf.PackError("%s: line %d: OBJ index 0 is invalid"
                               % (path, lineno))
        if not 0 <= i < count:
            raise pf.PackError("%s: line %d: %s index %d out of range (%d)"
                               % (path, lineno, what, idx, count))
        return i

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for lineno, line in enumerate(f, 1):
            parts = line.split()
            if not parts or parts[0].startswith("#"):
                continue
            tag = parts[0]
            if tag == "v":
                vs.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif tag == "vt":
                u = float(parts[1])
                v = float(parts[2]) if len(parts) > 2 else 0.0
                vts.append((u, v))
            elif tag == "vn":
                vns.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif tag == "usemtl":
                cur_mtl = parts[1] if len(parts) > 1 else None
            elif tag == "f":
                corners = []
                for tok in parts[1:]:
                    comps = tok.split("/")
                    vi = _resolve(int(comps[0]), len(vs), "vertex", lineno)
                    ti = ni = None
                    if len(comps) > 1 and comps[1]:
                        ti = _resolve(int(comps[1]), len(vts), "texcoord", lineno)
                    if len(comps) > 2 and comps[2]:
                        ni = _resolve(int(comps[2]), len(vns), "normal", lineno)
                    corners.append((vi, ti, ni))
                faces.append((cur_mtl, corners))
    return vs, vts, vns, faces


# --- conversion helpers --------------------------------------------------------------

def _engine_pos(p, scale, name):
    """Source units -> engine i16: * scale * 256, y negated, rounded."""
    e = (int(round(p[0] * scale * pf.WORLD_SCALE)),
         int(round(-p[1] * scale * pf.WORLD_SCALE)),
         int(round(p[2] * scale * pf.WORLD_SCALE)))
    for c in e:
        if not -32768 <= c <= 32767:
            raise pf.PackError(
                "mesh '%s': vertex %s (scaled) out of i16 range; reduce "
                "--scale or shrink the model" % (name, (e,)))
    return e


_DEFAULT_NORMAL = (0, -4096, 0)        # engine-space "up" (y-down world)


def _engine_normal(n):
    """OBJ normal -> engine 4.12 normalized (y negated)."""
    x, y, z = n[0], -n[1], n[2]
    length = math.sqrt(x * x + y * y + z * z)
    if length == 0.0:
        return _DEFAULT_NORMAL
    return (pf.check_i16(int(round(x * pf.FX12_ONE / length)), "normal.x"),
            pf.check_i16(int(round(y * pf.FX12_ONE / length)), "normal.y"),
            pf.check_i16(int(round(z * pf.FX12_ONE / length)), "normal.z"))


def _load_texture_size(textures_dir, tex_name, cache):
    """Read width/height from the texture's companion .json metadata."""
    if tex_name in cache:
        return cache[tex_name]
    meta_path = os.path.join(textures_dir, tex_name + ".json")
    if not os.path.isfile(meta_path):
        raise pf.PackError(
            "texture metadata not found: %s (import textures before meshes, "
            "or fix the --texture/--materials name)" % meta_path)
    with open(meta_path, "r", encoding="utf-8") as f:
        meta = json.load(f)
    size = (int(meta["width"]), int(meta["height"]))
    cache[tex_name] = size
    return size


def _face_uv_to_texels(uvs_src, w, h):
    """Apply v-flip + scale rule for one face's UVs (list of (u, v_obj))."""
    max_uv = max(max(abs(u), abs(v)) for (u, v) in uvs_src)
    out = []
    if max_uv <= 1.001:                # edge-to-edge mapping
        for (u, v) in uvs_src:
            tu = int(round(u * (w - 1)))
            tv = int(round((1.0 - v) * (h - 1)))
            out.append((max(0, min(255, tu)), max(0, min(255, tv))))
    else:
        # Tiling: scale to texels, then rebase the whole face into its first
        # tile so spans stay ascending. Wrapping corners independently (&255)
        # breaks faces that cross a 256-texel boundary: their span becomes
        # descending and the rasterizer smears the whole texture backwards
        # across the face (dotted-artifact columns). The engine samples with
        # a power-of-two mask, so texel coords may exceed w/h — only u8 range
        # (255) limits us, which holds for textures up to 128 px.
        tus = [int(round(u * w)) for (u, v) in uvs_src]
        tvs = [int(round((1.0 - v) * h)) for (u, v) in uvs_src]
        bu = (min(tus) // w) * w
        bv = (min(tvs) // h) * h
        tus = [t - bu for t in tus]
        tvs = [t - bv for t in tvs]
        if max(tus) > 255 or max(tvs) > 255:
            raise ValueError(
                "face UV span does not fit u8 texel coords after rebase "
                "(texture too wide for tiling, or face spans >1 repeat): "
                "u=%s v=%s (tex %dx%d)" % (tus, tvs, w, h))
        out = list(zip(tus, tvs))
    return out


# --- import -------------------------------------------------------------------------------

def import_mesh(entry, textures_dir="build/assets/textures",
                out_dir="build/assets/meshes", source_root="."):
    """entry: {name, source, lit?, scale?, texture?, materials?}.
    Writes <out_dir>/<name>.meshbin + .json; returns the summary dict."""
    name = entry["name"]
    src = entry["source"]
    lit = bool(entry.get("lit", False))
    scale = float(entry.get("scale", 1.0))
    texture = entry.get("texture")
    materials = entry.get("materials")
    # Optional per-mesh primitive flags (applied to all faces).
    semitrans = bool(entry.get("semitrans", False))
    semi_mode = int(entry.get("semi_mode", 0)) & 3
    doublesided = bool(entry.get("doublesided", False))
    sort_bias = int(entry.get("sort_bias", 0))
    # Optional flat vertex color (default 128 = neutral; texture modulation
    # treats 128 as 1.0, untextured prims show it directly).
    color = tuple(pf.clamp_u8(c) for c in entry.get("color", (128, 128, 128)))
    if len(color) != 3:
        raise pf.PackError("mesh '%s': color must be [r,g,b]" % name)
    prim_flags = 0
    if semitrans:
        prim_flags |= pf.MPF_SEMITRANS | (semi_mode << pf.MPF_SEMIMODE_SHIFT)
    if doublesided:
        prim_flags |= pf.MPF_DOUBLESIDED
    if bool(entry.get("uv_scroll", False)):
        prim_flags |= pf.MPF_UVSCROLL
    if bool(entry.get("matte", False)):
        prim_flags |= pf.MPF_MATTE

    path = os.path.join(source_root, src)
    if not os.path.isfile(path):
        raise pf.PackError("mesh '%s': source not found: %s" % (name, path))
    vs, vts, vns, faces = parse_obj(path)
    if not faces:
        raise pf.PackError("mesh '%s': OBJ has no faces" % name)

    has_normal_refs = any(ni is not None
                          for _mtl, corners in faces
                          for (_vi, _ti, ni) in corners)
    use_lit = lit and has_normal_refs
    warnings = []
    if lit and not has_normal_refs:
        warnings.append("--lit requested but OBJ has no vn data; "
                        "emitting unlit (flat FT/G) prims")

    # --- split polygons: tris + quads kept, >4-gons fanned to tris ---
    polys = []                          # (mtl, [corner x3or4], is_quad)
    for mtl, corners in faces:
        if len(corners) < 3:
            warnings.append("degenerate face with %d vertices skipped"
                            % len(corners))
            continue
        if len(corners) == 3:
            polys.append((mtl, corners, False))
        elif len(corners) == 4:
            polys.append((mtl, corners, True))
        else:
            for i in range(1, len(corners) - 1):
                polys.append((mtl, [corners[0], corners[i], corners[i + 1]],
                              False))

    # --- vertex dedup by (v, vn) pair; uv lives on the prim -----------------
    vert_map = {}
    verts, norms = [], []
    missing_normal_warned = [False]

    def get_vert(vi, ni):
        key = (vi, ni if use_lit else -1)
        idx = vert_map.get(key)
        if idx is not None:
            return idx
        idx = len(verts)
        verts.append(_engine_pos(vs[vi], scale, name))
        if use_lit:
            if ni is None:
                if not missing_normal_warned[0]:
                    warnings.append("some lit faces have no vn; defaulting "
                                    "their normals to engine up (0,-1,0)")
                    missing_normal_warned[0] = True
                norms.append(_DEFAULT_NORMAL)
            else:
                norms.append(_engine_normal(vns[ni]))
        vert_map[key] = idx
        return idx

    # --- build prims ----------------------------------------------------------
    tex_names, tex_index_map = [], {}
    size_cache = {}
    prims = []
    tri_count = 0
    uv_missing_warned = False

    for mtl, corners, is_quad in polys:
        if materials is not None:
            tex_name = materials.get(mtl)
        elif texture:
            tex_name = texture
        else:
            tex_name = None

        textured = tex_name is not None
        if textured and any(c[1] is None for c in corners):
            if not uv_missing_warned:
                warnings.append("faces reference texture '%s' but lack vt; "
                                "emitting them untextured" % tex_name)
                uv_missing_warned = True
            textured = False

        # Z-pattern reorder for quads: ring (r0,r1,r2,r3) -> (r0,r1,r3,r2)
        ring = [corners[0], corners[1], corners[3], corners[2]] if is_quad \
            else list(corners)

        vi = [get_vert(c[0], c[2]) for c in ring]
        if not is_quad:
            vi.append(0)               # vi[3] = 0 for tris per contract

        if textured:
            w, h = _load_texture_size(textures_dir, tex_name, size_cache)
            if tex_name not in tex_index_map:
                tex_index_map[tex_name] = len(tex_names)
                tex_names.append(tex_name)
            tex_index = tex_index_map[tex_name]
            uvs = _face_uv_to_texels([vts[c[1]] for c in ring], w, h)
            if not is_quad:
                uvs.append((0, 0))
            if use_lit:
                ptype = pf.MP_GT4 if is_quad else pf.MP_GT3
            else:
                ptype = pf.MP_FT4 if is_quad else pf.MP_FT3
        else:
            tex_index = 0xFFFF
            uvs = [(0, 0)] * 4
            ptype = pf.MP_G4 if is_quad else pf.MP_G3

        prims.append({
            "type": ptype,
            "flags": prim_flags,
            "tex_index": tex_index,
            "vi": vi,
            "uv": uvs,
            "rgb": [color] * 4,
            "sort_bias": sort_bias,
        })
        tri_count += 2 if is_quad else 1

    radius = 0
    for (x, y, z) in verts:
        r = math.sqrt(x * x + y * y + z * z)
        if r > radius:
            radius = r
    radius = int(round(radius))

    os.makedirs(out_dir, exist_ok=True)
    out_bin = os.path.join(out_dir, name + ".meshbin")
    pf.write_meshbin(out_bin, name=name, verts=verts, norms=norms,
                     tex_names=tex_names, prims=prims, tri_count=tri_count,
                     radius=radius)

    if tri_count > TRI_BUDGET_WARN:
        warnings.append("%d tris exceeds the %d-tri prop budget (spec 7.2)"
                        % (tri_count, TRI_BUDGET_WARN))

    summary = {
        "name": name,
        "source": src.replace("\\", "/"),
        "nverts": len(verts),
        "nnorms": len(norms),
        "nprims": len(prims),
        "ntex": len(tex_names),
        "textures": tex_names,
        "tri_count": tri_count,
        "radius": radius,
        "lit": use_lit,
        "scale": scale,
        "warnings": warnings,
    }
    out_json = os.path.join(out_dir, name + ".json")
    with open(out_json, "w", encoding="utf-8", newline="\n") as f:
        json.dump(summary, f, indent=2, sort_keys=True)
        f.write("\n")

    for wmsg in warnings:
        print("warning: mesh '%s': %s" % (name, wmsg))
    summary["file"] = out_bin
    summary["file_bytes"] = os.path.getsize(out_bin)
    return summary


# --- CLI -----------------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Convert a Wavefront OBJ into an engine .meshbin "
                    "(+ .json summary).")
    ap.add_argument("--in", dest="src", required=True, help="source OBJ file")
    ap.add_argument("--name", required=True, help="asset name (max 31 chars)")
    ap.add_argument("--lit", action="store_true",
                    help="emit Gouraud-textured (GT) prims when normals exist")
    grp = ap.add_mutually_exclusive_group()
    grp.add_argument("--texture", help="texture asset name for all faces")
    grp.add_argument("--materials",
                     help="JSON file mapping OBJ usemtl name -> texture name")
    ap.add_argument("--scale", type=float, default=1.0,
                    help="uniform pre-scale (default: %(default)s)")
    ap.add_argument("--textures-dir", default="build/assets/textures",
                    help="where texture .json metadata lives "
                         "(default: %(default)s)")
    ap.add_argument("--out-dir", default="build/assets/meshes",
                    help="output directory (default: %(default)s)")
    args = ap.parse_args(argv)

    entry = {"name": args.name, "source": args.src, "lit": args.lit,
             "scale": args.scale}
    if args.texture:
        entry["texture"] = args.texture
    if args.materials:
        with open(args.materials, "r", encoding="utf-8") as f:
            entry["materials"] = json.load(f)
    try:
        s = import_mesh(entry, textures_dir=args.textures_dir,
                        out_dir=args.out_dir)
    except pf.PackError as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    print("wrote %s (%d bytes): %d verts, %d norms, %d prims, %d tris, "
          "radius %d, textures %s"
          % (s["file"], s["file_bytes"], s["nverts"], s["nnorms"],
             s["nprims"], s["tri_count"], s["radius"],
             s["textures"] or "none"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
