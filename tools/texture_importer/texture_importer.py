#!/usr/bin/env python3
"""Texture importer: PNG -> .texbin (+ companion .json metadata).

Pipeline (docs/file_formats.md + spec 6):
  load PNG as RGBA -> validate/resize to power-of-two [8,256] (warn)
  -> transparency: alpha < 128 -> palette index 0 == halfword 0x0000
     (slot 0 reserved only with --transparent)
  -> quantize (Pillow MEDIANCUT) or map to a shared .pal by nearest color
  -> no opaque palette entry may encode to 0x0000 (bump r5 to 1)
  -> --semitrans sets STP bit15 on all opaque entries/texels
  -> pack payload halfwords, allocate in simulated VRAM, emit .texbin + .json

Usable both as a CLI (single texture, fresh allocator) and as a batch API:
    import_textures(entries, allocator, out_dir)  # shared allocator
"""

import argparse
import json
import os
import struct
import sys

_TOOLS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _TOOLS_DIR not in sys.path:
    sys.path.insert(0, _TOOLS_DIR)

from PIL import Image

from common import psx_formats as pf

try:                                   # Pillow >= 9.1
    _MEDIANCUT = Image.Quantize.MEDIANCUT
    _DITHER_NONE = Image.Dither.NONE
    _NEAREST = Image.Resampling.NEAREST
except AttributeError:                 # older Pillow
    _MEDIANCUT = Image.MEDIANCUT
    _DITHER_NONE = Image.NONE
    _NEAREST = Image.NEAREST


# --- helpers -------------------------------------------------------------------

def _pow2_dim(n):
    """Nearest power of two, clamped to [8, 256]."""
    if n <= 8:
        return 8
    if n >= 256:
        return 256
    lower = 1 << (n.bit_length() - 1)
    upper = lower << 1
    if lower == n:
        return n
    best = lower if (n - lower) < (upper - n) else upper
    return max(8, min(256, best))


def load_pal(path):
    """Load a raw .pal file: little-endian u16 halfwords, 16 or 256 entries."""
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) % 2 != 0:
        raise pf.PackError("palette %s: odd byte count %d" % (path, len(raw)))
    n = len(raw) // 2
    if n not in (16, 256):
        raise pf.PackError("palette %s: %d entries (must be 16 or 256)" % (path, n))
    return list(struct.unpack("<%dH" % n, raw))


def quantize_palette_mediancut(rgb_pixels, ncolors):
    """Median-cut (Pillow MEDIANCUT) a list of (r,g,b) into exactly `ncolors`
    entries (zero-padded). Deterministic for identical input."""
    if not rgb_pixels:
        return [(0, 0, 0)] * ncolors
    img = Image.new("RGB", (len(rgb_pixels), 1))
    img.putdata(rgb_pixels)
    q = img.quantize(colors=ncolors, method=_MEDIANCUT, dither=_DITHER_NONE)
    pal = q.getpalette() or []
    entries = [tuple(pal[i * 3:i * 3 + 3]) for i in range(len(pal) // 3)]
    entries = entries[:ncolors]
    while len(entries) < ncolors:
        entries.append((0, 0, 0))
    return entries


def _nearest_index(pal_rgb, cache, rgb):
    """Nearest palette index by squared RGB distance; ties -> lowest index."""
    hit = cache.get(rgb)
    if hit is not None:
        return hit
    r, g, b = rgb
    best, best_d = 0, None
    for i, (pr, pg, pb) in enumerate(pal_rgb):
        d = (pr - r) * (pr - r) + (pg - g) * (pg - g) + (pb - b) * (pb - b)
        if best_d is None or d < best_d:
            best_d, best = d, i
    cache[rgb] = best
    return best


def _finalize_entry(c, semitrans):
    """Apply STP + guarantee an opaque entry never encodes to 0x0000."""
    if semitrans:
        c |= 0x8000
    if c == 0x0000:
        c = 0x0001                     # bump r5 to 1
    return c


def _pack_indices_4(indices, w, h):
    """4 pixels per halfword, pixel x in bits (x&3)*4."""
    hws = []
    for y in range(h):
        base = y * w
        for x in range(0, w, 4):
            hw = 0
            for k in range(4):
                hw |= (indices[base + x + k] & 0xF) << (k * 4)
            hws.append(hw)
    return hws


def _pack_indices_8(indices, w, h):
    """2 pixels per halfword, pixel x in bits (x&1)*8."""
    hws = []
    for y in range(h):
        base = y * w
        for x in range(0, w, 2):
            hws.append((indices[base + x] & 0xFF)
                       | ((indices[base + x + 1] & 0xFF) << 8))
    return hws


# --- core conversion --------------------------------------------------------------

def prepare_texture(entry, source_root="."):
    """Load + quantize + pack one texture (no VRAM allocation yet).

    entry: {name, source, format, transparent?, semitrans?, palette?}
    Returns a dict consumed by _allocate_and_write().
    """
    name = entry["name"]
    src = entry["source"]
    fmt_name = entry["format"]
    if fmt_name not in pf.FORMAT_IDS:
        raise pf.PackError("texture '%s': unknown format %r" % (name, fmt_name))
    fmt = pf.FORMAT_IDS[fmt_name]
    transparent = bool(entry.get("transparent", False))
    semitrans = bool(entry.get("semitrans", False))
    pal_path = entry.get("palette")

    path = os.path.join(source_root, src)
    if not os.path.isfile(path):
        raise pf.PackError("texture '%s': source not found: %s" % (name, path))
    img = Image.open(path).convert("RGBA")
    w0, h0 = img.size
    w, h = _pow2_dim(w0), _pow2_dim(h0)
    if (w, h) != (w0, h0):
        print("warning: texture '%s': %dx%d is not a power-of-two size in "
              "[8,256]; resizing to %dx%d" % (name, w0, h0, w, h))
        img = img.resize((w, h), _NEAREST)
    px = list(img.getdata())

    palette_name = None
    if fmt == pf.TEX_15BIT:
        hws = []
        for (r, g, b, a) in px:
            if transparent and a < 128:
                hws.append(0x0000)
            else:
                hws.append(_finalize_entry(pf.rgb888_to_15(r, g, b), semitrans))
        payload = struct.pack("<%dH" % len(hws), *hws)
        clut, clut_len = [], 0
    else:
        colors = 16 if fmt == pf.TEX_4BIT else 256
        reserve = 1 if transparent else 0
        if transparent:
            mask = [a < 128 for (_r, _g, _b, a) in px]
        else:
            mask = [False] * len(px)
        opaque_rgb = [(r, g, b) for (r, g, b, _a), m in zip(px, mask) if not m]

        if pal_path:
            pal_file = os.path.join(source_root, pal_path)
            if not os.path.isfile(pal_file):
                raise pf.PackError("texture '%s': palette not found: %s"
                                   % (name, pal_file))
            hw_pal = load_pal(pal_file)
            if len(hw_pal) != colors:
                raise pf.PackError(
                    "texture '%s': palette %s has %d entries, format %s needs %d"
                    % (name, pal_path, len(hw_pal), fmt_name, colors))
            pal_rgb_all = [pf.rgb15_to_888(c) for c in hw_pal]
            # opaque pixels map to entries [reserve, colors); index 0 stays
            # transparent when reserved.
            cand_rgb = pal_rgb_all[reserve:]
            cache = {}
            indices = []
            for (r, g, b, _a), m in zip(px, mask):
                if m:
                    indices.append(0)
                else:
                    indices.append(reserve + _nearest_index(cand_rgb, cache,
                                                            (r, g, b)))
            clut = []
            for i, c in enumerate(hw_pal):
                if transparent and i == 0:
                    clut.append(0x0000)
                else:
                    clut.append(_finalize_entry(c, semitrans))
            palette_name = os.path.splitext(os.path.basename(pal_path))[0]
        else:
            pal_rgb = quantize_palette_mediancut(opaque_rgb, colors - reserve)
            cache = {}
            indices = []
            for (r, g, b, _a), m in zip(px, mask):
                if m:
                    indices.append(0)
                else:
                    indices.append(reserve + _nearest_index(pal_rgb, cache,
                                                            (r, g, b)))
            clut = [0x0000] * colors
            for i, (r, g, b) in enumerate(pal_rgb):
                clut[reserve + i] = _finalize_entry(pf.rgb888_to_15(r, g, b),
                                                    semitrans)
        clut_len = colors
        if fmt == pf.TEX_4BIT:
            hws = _pack_indices_4(indices, w, h)
        else:
            hws = _pack_indices_8(indices, w, h)
        payload = struct.pack("<%dH" % len(hws), *hws)

    return {
        "name": name,
        "source": src,
        "format": fmt,
        "format_name": fmt_name,
        "width": w,
        "height": h,
        "w_hw": pf.storage_width_hw(fmt, w),
        "payload": payload,
        "clut": clut,
        "clut_len": clut_len,
        "palette_name": palette_name,
    }


def _allocate_and_write(prep, allocator, out_dir):
    """Allocate VRAM for a prepared texture and write .texbin + .json."""
    vx, vy = allocator.alloc_texture(prep["w_hw"], prep["height"])
    if prep["clut_len"]:
        cx, cy = allocator.alloc_clut(prep["clut_len"])
    else:
        cx, cy = pf.NO_CLUT, pf.NO_CLUT
    cost = len(prep["payload"]) + 2 * prep["clut_len"]

    out_bin = os.path.join(out_dir, prep["name"] + ".texbin")
    pf.write_texbin(out_bin, name=prep["name"], fmt=prep["format"],
                    width=prep["width"], height=prep["height"],
                    vram_x=vx, vram_y=vy, clut_x=cx, clut_y=cy,
                    clut_len=prep["clut_len"], vram_cost_bytes=cost,
                    clut=prep["clut"], payload=prep["payload"])

    meta = {
        "name": prep["name"],
        "source": prep["source"].replace("\\", "/"),
        "format": prep["format_name"],
        "width": prep["width"],
        "height": prep["height"],
        "palette": prep["palette_name"],
        "vram_x": vx,
        "vram_y": vy,
        "clut_x": cx,
        "clut_y": cy,
        "clut_len": prep["clut_len"],
        "texture_page": (vy // 256) * 16 + vx // 64,
        "vram_cost_bytes": cost,
    }
    out_json = os.path.join(out_dir, prep["name"] + ".json")
    with open(out_json, "w", encoding="utf-8", newline="\n") as f:
        json.dump(meta, f, indent=2, sort_keys=True)
        f.write("\n")
    meta["file"] = out_bin
    meta["file_bytes"] = os.path.getsize(out_bin)
    return meta


# --- batch API (used by build_assets) -------------------------------------------------

def import_textures(entries, allocator, out_dir, source_root="."):
    """Convert + pack a batch of textures through one shared VramAllocator.

    Allocation order is height-descending (stable) for good shelf packing;
    the returned metadata list matches the input order.
    """
    os.makedirs(out_dir, exist_ok=True)
    prepared = [prepare_texture(e, source_root) for e in entries]
    order = sorted(range(len(prepared)), key=lambda i: -prepared[i]["height"])
    metas = [None] * len(prepared)
    for i in order:
        metas[i] = _allocate_and_write(prepared[i], allocator, out_dir)
    return metas


def import_texture(entry, allocator, out_dir, source_root="."):
    """Single-texture convenience wrapper."""
    return import_textures([entry], allocator, out_dir, source_root)[0]


# --- CLI --------------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Convert a PNG into an engine .texbin (+ .json metadata). "
                    "Standalone runs use a fresh VRAM allocator; the full "
                    "build (tools/build_assets.py) shares one allocator "
                    "across all textures.")
    ap.add_argument("--in", dest="src", required=True, help="source PNG file")
    ap.add_argument("--name", required=True, help="asset name (max 31 chars)")
    ap.add_argument("--format", required=True,
                    choices=["indexed_4", "indexed_8", "direct_15"],
                    help="pixel format")
    ap.add_argument("--palette", help="shared .pal file (raw LE u16 halfwords)"
                                      " to map to instead of quantizing")
    ap.add_argument("--transparent", action="store_true",
                    help="alpha<128 pixels become transparent (index 0 / "
                         "halfword 0x0000); reserves palette slot 0")
    ap.add_argument("--semitrans", action="store_true",
                    help="set STP bit15 on all opaque texels")
    ap.add_argument("--out-dir", default="build/assets/textures",
                    help="output directory (default: %(default)s)")
    args = ap.parse_args(argv)

    entry = {"name": args.name, "source": args.src, "format": args.format,
             "transparent": args.transparent, "semitrans": args.semitrans}
    if args.palette:
        entry["palette"] = args.palette
    try:
        allocator = pf.VramAllocator()
        meta = import_texture(entry, allocator, args.out_dir)
    except pf.PackError as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    print("wrote %s (%d bytes) %dx%d %s vram=(%d,%d) clut=(%d,%d,len %d) "
          "page=%d cost=%d bytes"
          % (meta["file"], meta["file_bytes"], meta["width"], meta["height"],
             meta["format"], meta["vram_x"], meta["vram_y"], meta["clut_x"],
             meta["clut_y"], meta["clut_len"], meta["texture_page"],
             meta["vram_cost_bytes"]))
    print("note: standalone runs allocate from a fresh VRAM; coordinates are "
          "final only when built via tools/build_assets.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
