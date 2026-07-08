#!/usr/bin/env python3
"""Convert engine BMP screenshots to PNG for inspection.

Usage: python tools/dev/convert_shots.py [dir]
Defaults to build/bin/screenshots. Writes .png next to each .bmp.
"""
import sys
from pathlib import Path

from PIL import Image


def main():
    d = Path(sys.argv[1] if len(sys.argv) > 1 else "build/bin/screenshots")
    if not d.is_dir():
        print(f"no screenshot dir: {d}", file=sys.stderr)
        return 1
    n = 0
    for bmp in sorted(d.glob("*.bmp")):
        png = bmp.with_suffix(".png")
        Image.open(bmp).convert("RGB").save(png)
        print(f"{bmp.name} -> {png.name}")
        n += 1
    print(f"{n} converted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
