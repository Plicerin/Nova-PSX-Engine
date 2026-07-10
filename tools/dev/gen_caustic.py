#!/usr/bin/env python3
"""Generate a seamless PS1-style water caustic texture.

Caustics = the bright interlocking web you see on a pool floor. Built from an
interference field of several 2-D sine gratings (integer spatial frequencies =>
tiles seamlessly), domain-warped for organic wobble, then the near-zero
crossings are lifted into thin bright ridges and colour-mapped blue->cyan->white.
"""
import sys

import numpy as np
from PIL import Image

W = H = 128


def field():
    xs = np.linspace(0, 2 * np.pi, W, endpoint=False)
    ys = np.linspace(0, 2 * np.pi, H, endpoint=False)
    X, Y = np.meshgrid(xs, ys)
    # domain warp (periodic -> stays seamless)
    wx = 0.55 * np.sin(2 * Y + 1.0) + 0.35 * np.sin(3 * X + 0.5)
    wy = 0.55 * np.sin(2 * X + 2.0) + 0.35 * np.sin(3 * Y + 1.7)
    Xa, Ya = X + wx, Y + wy

    def g(fx, fy, ph):
        return np.sin(fx * Xa + fy * Ya + ph)

    v = (g(3, 1, 0.0) + g(1, 3, 1.3) + g(4, 2, 2.1) +
         g(2, 4, 0.7) + g(5, 3, 3.0)) / 5.0
    # thin bright ridges where the field crosses zero
    ridge = np.clip(1.0 - np.abs(v) * 2.2, 0.0, 1.0) ** 1.6
    return ridge


def colorize(ridge):
    base = np.array([18, 52, 78])       # deep water
    mid = np.array([48, 132, 170])      # ripple
    peak = np.array([190, 235, 250])    # bright caustic
    t = ridge[..., None]
    col = np.where(t < 0.5, base + (mid - base) * (t / 0.5),
                   mid + (peak - mid) * ((t - 0.5) / 0.5))
    return np.clip(col, 0, 255).astype(np.uint8)


def main():
    img = Image.fromarray(colorize(field()), "RGB")
    out = "source_assets/textures/arena_water.png"
    img.save(out)
    print("wrote", out)
    # 2x2 tiled + upscaled preview so seamlessness is visible
    prev = Image.new("RGB", (W * 2, H * 2))
    for i in range(2):
        for j in range(2):
            prev.paste(img, (i * W, j * H))
    prev.resize((W * 2 * 3, H * 2 * 3), Image.NEAREST).save("build/bin/water_preview.png")
    print("wrote build/bin/water_preview.png")


if __name__ == "__main__":
    sys.exit(main())
