#!/usr/bin/env python3
"""Palette tool: build/inspect shared .pal files.

A .pal file is raw little-endian u16 halfwords (16 or 256 entries) in PS1
15-bit layout: bit15 = STP, c = (b5<<10)|(g5<<5)|r5. Halfword 0x0000 reads as
a fully transparent texel, so opaque entries are bumped to r5=1 if they would
encode to zero.

Subcommands:
  build  --out shared.pal --colors {16,256} [--transparent] a.png [b.png ...]
         Median-cut quantization (Pillow MEDIANCUT) over the combined opaque
         pixels of all inputs. --transparent reserves entry 0 as 0x0000.
  show   shared.pal
         Print all entries (hex halfword, 5-bit channels, expanded RGB, STP).
"""

import argparse
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
except AttributeError:                 # older Pillow
    _MEDIANCUT = Image.MEDIANCUT
    _DITHER_NONE = Image.NONE


def _quantize(rgb_pixels, ncolors):
    """Median-cut a list of (r,g,b) into exactly ncolors entries (0-padded)."""
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


def cmd_build(args):
    pixels = []
    for path in args.pngs:
        if not os.path.isfile(path):
            print("error: input not found: %s" % path, file=sys.stderr)
            return 1
        img = Image.open(path).convert("RGBA")
        for (r, g, b, a) in img.getdata():
            if a >= 128:
                pixels.append((r, g, b))
    if not pixels:
        print("error: no opaque pixels in any input image", file=sys.stderr)
        return 1

    reserve = 1 if args.transparent else 0
    entries = _quantize(pixels, args.colors - reserve)

    halfwords = [0x0000] * args.colors
    for i, (r, g, b) in enumerate(entries):
        c = pf.rgb888_to_15(r, g, b)
        if c == 0x0000:
            c = 0x0001                 # opaque black must not read transparent
        halfwords[reserve + i] = c

    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(struct.pack("<%dH" % args.colors, *halfwords))
    print("wrote %s: %d entries from %d source pixels (%d image(s))%s"
          % (args.out, args.colors, len(pixels), len(args.pngs),
             ", entry 0 reserved transparent" if reserve else ""))
    return 0


def cmd_show(args):
    if not os.path.isfile(args.pal):
        print("error: palette not found: %s" % args.pal, file=sys.stderr)
        return 1
    with open(args.pal, "rb") as f:
        raw = f.read()
    if len(raw) % 2 != 0:
        print("error: %s has odd byte count %d" % (args.pal, len(raw)),
              file=sys.stderr)
        return 1
    n = len(raw) // 2
    halfwords = struct.unpack("<%dH" % n, raw)
    print("%s: %d entries%s" % (args.pal, n,
          "" if n in (16, 256) else "  (WARNING: not 16 or 256)"))
    print("idx   hex    r5 g5 b5  stp   rgb888")
    for i, c in enumerate(halfwords):
        r5, g5, b5 = c & 31, (c >> 5) & 31, (c >> 10) & 31
        r, g, b = pf.rgb15_to_888(c)
        note = "  <- transparent (0x0000)" if c == 0x0000 else ""
        print("%3d  0x%04X  %2d %2d %2d   %d   (%3d,%3d,%3d)%s"
              % (i, c, r5, g5, b5, (c >> 15) & 1, r, g, b, note))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Build or inspect shared .pal palettes (raw LE u16 "
                    "halfwords, PS1 15-bit color).")
    sub = ap.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("build", help="build a palette from one or more PNGs")
    b.add_argument("--out", required=True, help="output .pal path")
    b.add_argument("--colors", type=int, choices=[16, 256], default=16,
                   help="palette size (default: %(default)s)")
    b.add_argument("--transparent", action="store_true",
                   help="reserve entry 0 as 0x0000 (transparent)")
    b.add_argument("pngs", nargs="+", metavar="PNG",
                   help="source images (combined pixel pool)")
    b.set_defaults(func=cmd_build)

    s = sub.add_parser("show", help="print the entries of a .pal file")
    s.add_argument("pal", help=".pal file to display")
    s.set_defaults(func=cmd_show)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
