#!/usr/bin/env python3
"""Author the `enchanted_log` textures: the ordinary oak-bark PNGs overlaid with branching, glowing
green cracks. The crack colour IS the shader glow colour (masked emission reads the albedo's bright
pixels — docs/MaskedEmissiveSpec.md). Run from the repo root:  python tools/gen_enchanted_log.py

Deterministic (fixed seeds). Cracks are drawn at low res with a BOUNDED queue (no unbounded fork
recursion) then upscaled, so it runs in ~a second even at 1024px.
"""

import os
import random

import numpy as np
from PIL import Image

SRC = "resources/textures/source"
GLOW = np.array([90, 255, 130], dtype=np.float32)   # crack colour == shader glow colour
MASK_RES = 256                                       # build the crack mask small, upscale to the PNG


def crack_mask(seed):
    """Bounded queue-based glowing cracks on a MASK_RES grid. Returns a 0..1 intensity mask."""
    rng = random.Random(seed)
    mask = np.zeros((MASK_RES, MASK_RES), dtype=np.float32)
    M = MASK_RES
    q = []
    for _ in range(rng.randint(3, 5)):                # a few trunk-following cracks
        q.append((rng.uniform(M * 0.15, M * 0.85), rng.uniform(0, M * 0.15),
                  np.pi / 2 + rng.uniform(-0.3, 0.3),
                  rng.randint(int(M * 0.6), int(M * 0.95)), 1.6))
    forks = 0
    while q and forks < 40:                            # hard cap on total segments
        x, y, ang, length, w = q.pop()
        for _ in range(length):
            ang += rng.uniform(-0.32, 0.32)
            x += np.cos(ang); y += np.sin(ang)
            xi, yi = int(round(x)), int(round(y))
            if not (0 <= xi < M and 0 <= yi < M):
                break
            r = max(1, int(round(w)))
            y0, y1 = max(0, yi - r), min(M, yi + r + 1)
            x0, x1 = max(0, xi - r), min(M, xi + r + 1)
            yy, xx = np.mgrid[y0:y1, x0:x1]
            g = np.exp(-((xx - xi) ** 2 + (yy - yi) ** 2) / (r * r * 0.7))
            mask[y0:y1, x0:x1] = np.maximum(mask[y0:y1, x0:x1], g)
            if rng.random() < 0.03 and forks < 40 and length > 8:
                forks += 1
                q.append((x, y, ang + rng.choice([-1, 1]) * rng.uniform(0.6, 1.1),
                          int(length * 0.45), max(1.0, w - 0.6)))
    return np.clip(mask, 0, 1)


def enchant(face, seed):
    img = Image.open(f"{SRC}/log_{face}.png").convert("RGB")
    a = np.asarray(img, dtype=np.float32)
    H, W = a.shape[:2]
    m = crack_mask(seed)
    m = np.asarray(Image.fromarray((m * 255).astype(np.uint8)).resize((W, H), Image.BILINEAR),
                   dtype=np.float32)[..., None] / 255.0
    out = a * (1.0 - 0.55 * m) * (1.0 - m) + GLOW[None, None, :] * m   # darken bark under crack, glow core
    Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).save(f"{SRC}/enchanted_log_{face}.png")
    nr = f"{SRC}/log_{face}_nr.png"                      # reuse the bark's normal+roughness relief
    if os.path.exists(nr):
        Image.open(nr).save(f"{SRC}/enchanted_log_{face}_nr.png")


if __name__ == "__main__":
    for i, f in enumerate(["side_n", "side_s", "side_e", "side_w", "top", "bottom"]):
        enchant(f, 1337 + i * 7)
        print(f"  enchanted_log_{f}.png")
    print("done")
