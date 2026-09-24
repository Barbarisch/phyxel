#!/usr/bin/env python3
"""Generate the Glass texture: a clean, 1024 px RGBA pane.

    python tools/gen_glass_texture.py            # writes resources/textures/source/glass_*.png
    python tools/gen_glass_texture.py --preview  # also writes a 4x4 tiled preview to stdout path

docs/GlassTransparency.md §2 — reviewer requirements R1 (not the old 64 px texture), R4 (no
baked distortion), R5 (much cleaner), R7 (never ship glass without alpha).

HOW GLASS IS DRAWN, which decides what this texture must contain. Glass is drawn by the OIT pass
only (GlassTransparency.md §1), which blends with

    alpha = max(textureAlpha, materialAlpha)

so the MATERIAL's alpha (materials.json) is the see-through floor — calibrated against measured
transmission to the reviewer's T ~ 0.80 — and the texture can only ADD coverage on top of it. So:

  * almost every texel has alpha 0: the pane is exactly as clear as the material says;
  * a few faint, soft streaks rise a little above the floor, so the pane reads as PRESENT (R3)
    without clouding it — the "you know it is there" detail, done in coverage, not in holes;
  * colour is a clean, very light blue-green, the tint thick glass has at its edges.

Every pattern is PERIODIC over the tile, so the texture repeats seamlessly across a voxel wall — a
seam at every voxel would read as a grid on a large window.

WHAT IT MUST NEVER BE AGAIN. Commit 2ea8b8d9 regenerated glass as RGB — no alpha channel — and that
alone made glass opaque for four months (GlassTransparency.md §11). This writes RGBA explicitly, asserts it, and
TransparencyTextureGuardTest fails the build if a future regen strips it.
"""

import argparse
import math
import os
import sys

from PIL import Image

SIZE = 1024
FACES = ["glass_top", "glass_bottom", "glass_side_n", "glass_side_s", "glass_side_e", "glass_side_w"]
OUT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "resources", "textures", "source")

# Clean glass: very light, faintly blue-green. Values are albedo (0..255).
BASE_RGB = (218, 236, 240)
# Smudge coverage ceiling. The material floor is ~0.2 (calibrated), so streaks must exceed it to
# show at all, and must stay faint to keep the pane clear. 0.34 peak = subtle.
STREAK_ALPHA_PEAK = 0.34


def periodic(u, v, fu, fv, phase=0.0):
    """A plane wave with integer frequencies on the tile -> tiles seamlessly."""
    return math.sin(2.0 * math.pi * (fu * u + fv * v) + phase)


# TILEABLE NOISE. The first version used two families of regular diagonal waves and produced a
# chain-link diamond lattice -- the opposite of "clean". This is a sum of many low-frequency waves
# at scattered integer frequency vectors with fixed pseudo-random phases: still exactly periodic on
# the tile (so it wraps with no seam), but irregular, so nothing lines up into a grid.
import random as _random
_rng = _random.Random(20260923)
_WAVES = []
for _ in range(14):
    fu = _rng.randint(-4, 4)
    fv = _rng.randint(-4, 4)
    if fu == 0 and fv == 0:
        fv = 1
    amp = 1.0 / math.sqrt(fu * fu + fv * fv)          # lower frequencies dominate: soft shapes
    _WAVES.append((fu, fv, _rng.uniform(0.0, 2.0 * math.pi), amp))
_NORM = sum(w[3] for w in _WAVES)


def noise(u, v):
    return sum(a * periodic(u, v, fu, fv, ph) for fu, fv, ph, a in _WAVES) / _NORM   # ~[-1, 1]


def smoothstep(e0, e1, x):
    k = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return k * k * (3.0 - 2.0 * k)


def texel(x, y):
    u = x / float(SIZE)
    v = y / float(SIZE)
    n = noise(u, v)

    # Only the highest crests of the noise become faint smudges -- sparse, soft-edged, irregular.
    smudge = smoothstep(0.38, 0.62, n)

    # A barely-there colour variation so the pane is not a flat fill.
    shade = 0.015 * n

    r = BASE_RGB[0] * (1.0 + shade)
    g = BASE_RGB[1] * (1.0 + shade)
    b = BASE_RGB[2] * (1.0 + shade)
    lift = 1.0 + 0.05 * smudge                       # smudges read very slightly brighter
    a = STREAK_ALPHA_PEAK * smudge

    clamp = lambda c: max(0, min(255, int(round(c))))
    return (clamp(r * lift), clamp(g * lift), clamp(b * lift), clamp(a * 255.0))


def build():
    im = Image.new("RGBA", (SIZE, SIZE))
    px = im.load()
    for y in range(SIZE):
        for x in range(SIZE):
            px[x, y] = texel(x, y)
    return im


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--preview", action="store_true", help="also write a 4x4 tiled preview PNG")
    args = ap.parse_args()

    im = build()
    # R7: never write glass without alpha. Assert, don't assume.
    assert im.mode == "RGBA", "glass texture must be RGBA"
    alphas = list(im.getchannel("A").getdata())
    below = sum(1 for a in alphas if a < 250) / float(len(alphas))
    assert below > 0.5, "glass must be mostly clear (got %.1f%% below 250)" % (below * 100)

    for name in FACES:
        path = os.path.join(OUT_DIR, name + ".png")
        im.save(path)
    print("wrote %d faces, %dx%d RGBA, %.1f%% of texels clear (alpha < 250), peak alpha %d/255"
          % (len(FACES), SIZE, SIZE, below * 100.0, max(alphas)))

    if args.preview:
        tiled = Image.new("RGBA", (SIZE, SIZE))
        small = im.resize((SIZE // 4, SIZE // 4))
        for ty in range(4):
            for tx in range(4):
                tiled.paste(small, (tx * SIZE // 4, ty * SIZE // 4))
        prev = os.path.join(OUT_DIR, "..", "..", "..", "screenshots", "glass_texture_preview.png")
        bg = Image.new("RGBA", tiled.size, (60, 90, 140, 255))
        Image.alpha_composite(bg, tiled).save(os.path.abspath(prev))
        print("preview (tiled 4x4 over a blue backdrop): %s" % os.path.abspath(prev))
    return 0


if __name__ == "__main__":
    sys.exit(main())
