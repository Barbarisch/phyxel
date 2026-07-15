#!/usr/bin/env python3
"""gen_heartwood.py — procedural raw cut-wood (heartwood) faces for the Heartwood material.

The inside of a chopped trunk: what an axe kerf exposes. No CC0 photo is needed —
freshly cut oak is tonally simple and the texture mostly shows at microcube scale
(1/9 of a face), so what matters is a believable warm sapwood tone with visible
grain. Faces (all seamless, wrap in x and y):

  top / bottom — END GRAIN: pale sapwood ring, darker heart center, concentric
                 growth rings with slight wobble, radial hairline checks.
  sides        — LONG GRAIN: straight longitudinal grain streaks (vertical),
                 subtle tonal bands, occasional darker grain line.

Deterministic. Writes resources/textures/source/heartwood_{face}.png (512px).
Restart the engine or call reload_atlas afterwards.
"""
import os

import numpy as np
from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_DIR = os.path.join(REPO, "resources", "textures", "source")

SIZE = 1024

# Fresh-cut oak palette (sRGB): pale sapwood -> warm heartwood.
SAPWOOD = np.array([214.0, 184.0, 138.0])
HEART = np.array([172.0, 132.0, 86.0])
RING = np.array([128.0, 92.0, 55.0])
CHECK = np.array([96.0, 68.0, 40.0])


def smooth_noise(rng, size, cells):
    """Seamless low-frequency noise in [0,1] (tiled bicubic upsample)."""
    g = rng.random((cells, cells))
    big = np.tile(g, (3, 3))
    img = Image.fromarray((big * 255).astype(np.uint8), "L").resize(
        (size * 3, size * 3), Image.BICUBIC)
    return np.asarray(img, dtype=float)[size:2 * size, size:2 * size] / 255.0


def end_grain(seed):
    """Concentric growth rings around the tile center."""
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:SIZE, 0:SIZE].astype(float)
    cx = cy = SIZE / 2.0
    r = np.hypot(xx - cx, yy - cy) / (SIZE / 2.0)          # 0 center -> ~1.4 corners
    ang = np.arctan2(yy - cy, xx - cx)

    # ring wobble so growth rings are not perfect circles
    wob = (0.045 * np.sin(3 * ang + rng.uniform(0, 6.28))
           + 0.03 * np.sin(7 * ang + rng.uniform(0, 6.28)))
    rw = np.clip(r + wob, 0, None)

    # base tone: heart center blending out to sapwood
    t = np.clip(rw / 1.05, 0, 1)[..., None]
    base = HEART * (1 - t) + SAPWOOD * t

    # concentric rings: darker line where ring phase crests (denser near center)
    phase = np.sin(rw * rw * 90.0 + rw * 24.0)
    ringmask = np.clip((phase - 0.55) / 0.45, 0, 1) ** 1.6
    base = base * (1 - 0.55 * ringmask[..., None]) + RING * (0.55 * ringmask[..., None])

    # radial hairline checks (drying cracks) — a few thin darker rays
    for _ in range(7):
        a0 = rng.uniform(0, 2 * np.pi)
        width = rng.uniform(0.006, 0.014)
        d = np.abs(((ang - a0 + np.pi) % (2 * np.pi)) - np.pi)
        ray = np.clip(1.0 - d / width, 0, 1) * np.clip(rw - 0.15, 0, 1)
        base = base * (1 - 0.5 * ray[..., None]) + CHECK * (0.5 * ray[..., None])

    # gentle luminance noise so flat areas aren't dead
    base *= (0.94 + 0.12 * smooth_noise(rng, SIZE, 9))[..., None]
    return np.clip(base, 0, 255).astype(np.uint8)


def long_grain(seed):
    """Straight longitudinal grain (vertical streaks)."""
    rng = np.random.default_rng(seed)
    # tonal bands across x: stretched noise 1:16 vertically
    gsmall = rng.random((SIZE // 16, SIZE))
    bands = np.asarray(Image.fromarray((np.tile(gsmall, (3, 3)) * 255).astype(np.uint8), "L")
                       .resize((SIZE * 3, SIZE * 3), Image.BILINEAR), dtype=float)
    bands = bands[SIZE:2 * SIZE, SIZE:2 * SIZE] / 255.0

    t = (0.35 + 0.5 * bands)[..., None]
    base = HEART * (1 - t) + SAPWOOD * t

    # sharp grain lines: high-freq noise stretched hard in y, thresholded
    lsmall = rng.random((SIZE // 32, SIZE))
    lines = np.asarray(Image.fromarray((np.tile(lsmall, (3, 3)) * 255).astype(np.uint8), "L")
                       .resize((SIZE * 3, SIZE * 3), Image.BICUBIC), dtype=float)
    lines = lines[SIZE:2 * SIZE, SIZE:2 * SIZE] / 255.0
    mask = np.clip((lines - 0.72) / 0.28, 0, 1) ** 1.4
    base = base * (1 - 0.5 * mask[..., None]) + RING * (0.5 * mask[..., None])

    base *= (0.95 + 0.10 * smooth_noise(rng, SIZE, 13))[..., None]
    return np.clip(base, 0, 255).astype(np.uint8)


def main():
    os.makedirs(SOURCE_DIR, exist_ok=True)
    faces = {
        "top": end_grain(101),
        "bottom": end_grain(202),
        "side_n": long_grain(303),
        "side_s": long_grain(404),
        "side_e": long_grain(505),
        "side_w": long_grain(606),
    }
    for face, arr in faces.items():
        path = os.path.join(SOURCE_DIR, f"heartwood_{face}.png")
        Image.fromarray(arr, "RGB").save(path)
        print("wrote", path)


if __name__ == "__main__":
    main()
