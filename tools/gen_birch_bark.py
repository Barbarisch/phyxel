#!/usr/bin/env python3
"""gen_birch_bark.py — turn the fetched LogBirch faces into photoreal paper birch.

No CC0 birch bark photo exists (ambientCG and Poly Haven both lack one), so LogBirch is
built procedurally ON TOP of the fetched Bark011 tile, which supplies real photographic
micro-relief. Layers, all seamless (wrap-around in x and y):

  1. large soft tonal patches   — chalk white / cream / pale grey (low-freq noise)
  2. horizontal paper grain     — mid-freq noise stretched 16:1 in x
  3. photographic micro-detail  — high-pass of the fetched Bark011 luminance, low opacity
  4. lenticels                  — thin ragged horizontal dashes, dark core + pale emboss
                                  line below, loosely clustered in horizontal bands
  5. peel scars                 — a few wide dark patches with noisy edges

Run AFTER `fetch_cc0_textures.py LogBirch` (fetching reverts the albedo to the grey-green
Bark011 base). Deterministic. Restart the engine or call reload_atlas afterwards.
"""
import os

import numpy as np
from PIL import Image, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_DIR = os.path.join(REPO, "resources", "textures", "source")

FACES = ["side_n", "side_s", "side_e", "side_w", "top", "bottom"]

CHALK = np.array([246.0, 243.0, 236.0])
CREAM = np.array([233.0, 227.0, 214.0])
GREY = np.array([204.0, 199.0, 189.0])
LENTICEL = np.array([52.0, 45.0, 38.0])
SCAR = np.array([88.0, 72.0, 58.0])


def smooth_noise(rng, size, cells):
    """Seamless low-frequency noise in [0,1]: random grid upsampled bicubically (tiled)."""
    g = rng.random((cells, cells))
    big = np.tile(g, (3, 3))                          # tile so the upsample wraps
    img = Image.fromarray((big * 255).astype(np.uint8), "L").resize(
        (size * 3, size * 3), Image.BICUBIC)
    a = np.asarray(img, dtype=float)[size:2 * size, size:2 * size] / 255.0
    return a


def stamp_dash(canvas_a, rng, size, cy, cx, w, h, strength):
    """Accumulate a ragged horizontal dash into an alpha canvas, wrapping at edges."""
    xs = (np.arange(-w, w + 1))
    taper = np.clip(1.15 - np.abs(xs) / w, 0.0, 1.0) ** 0.6          # blunt ragged ends
    ragged = 0.75 + 0.25 * rng.random(xs.size)
    for dy in range(-h, h + 1):
        fall = max(0.0, 1.0 - (dy / (h + 0.6)) ** 2)                  # soft vertical edge
        row = (cy + dy) % size
        cols = (cx + xs) % size
        canvas_a[row, cols] = np.maximum(canvas_a[row, cols],
                                         strength * fall * taper * ragged)


def build_face(base_img, seed):
    size = base_img.size[0]
    rng = np.random.default_rng(seed)

    # 1. tonal patches: two noise octaves pick between chalk/cream/grey
    n1 = smooth_noise(rng, size, 5)
    n2 = smooth_noise(rng, size, 11)
    t = np.clip(0.62 * n1 + 0.38 * n2, 0, 1)[..., None]
    base = GREY * (1 - t) + CHALK * t
    base = base * 0.72 + CREAM * 0.28

    # 2. horizontal paper grain (stretched 16:1)
    gsmall = rng.random((size, size // 16))
    grain = np.asarray(Image.fromarray((np.tile(gsmall, (3, 3)) * 255).astype(np.uint8), "L")
                       .resize((size * 3, size * 3), Image.BILINEAR), dtype=float)
    grain = grain[size:2 * size, size:2 * size] / 255.0 - 0.5
    base *= (1.0 + 0.09 * grain)[..., None]

    # 3. photographic micro-detail from the fetched Bark011 tile (high-pass, low opacity)
    lum = np.asarray(base_img.convert("L"), dtype=float)
    lo = np.asarray(Image.fromarray(lum.astype(np.uint8)).filter(
        ImageFilter.GaussianBlur(9)), dtype=float)
    base += ((lum - lo) * 0.55)[..., None]

    # 4. lenticels: alpha canvas of ragged dashes, clustered in loose horizontal bands
    a = np.zeros((size, size))
    bands = rng.uniform(0, size, 7)
    n_dash = int(size * size / 9000)
    for _ in range(n_dash):
        if rng.random() < 0.65:                       # 65% band-clustered, rest uniform
            cy = int(rng.choice(bands) + rng.normal(0, size * 0.045)) % size
        else:
            cy = int(rng.uniform(0, size))
        cx = int(rng.uniform(0, size))
        w = int(rng.uniform(size / 34, size / 9))
        h = max(1, int(abs(rng.normal(0, size / 400)) + size / 512))
        stamp_dash(a, rng, size, cy, cx, w, h, rng.uniform(0.55, 0.95))
    # emboss: pale line one row below each dash (paper pushed out under the slit)
    hl = np.roll(a, 2, axis=0) - a
    base += np.clip(hl, 0, 1)[..., None] * 14.0
    base = base * (1 - a[..., None]) + LENTICEL * a[..., None]

    # 5. peel scars: a few wide dark patches with noisy borders
    sa = np.zeros((size, size))
    for _ in range(rng.integers(2, 5)):
        cy, cx = int(rng.uniform(0, size)), int(rng.uniform(0, size))
        w = int(rng.uniform(size / 9, size / 4))
        h = int(rng.uniform(size / 60, size / 26))
        stamp_dash(sa, rng, size, cy, cx, w, h, rng.uniform(0.5, 0.8))
    sa = np.asarray(Image.fromarray((sa * 255).astype(np.uint8), "L").filter(
        ImageFilter.GaussianBlur(2)), dtype=float) / 255.0
    base = base * (1 - sa[..., None]) + SCAR * sa[..., None]

    return Image.fromarray(np.clip(base, 0, 255).astype(np.uint8), "RGB")


def main():
    for i, face in enumerate(FACES):
        path = os.path.join(SOURCE_DIR, f"log_birch_{face}.png")
        img = Image.open(path).convert("RGB")
        out = build_face(img, seed=4200 + i)          # distinct pattern per face
        out.save(path)
        print(f"  birch-ified {os.path.basename(path)} ({out.size[0]}px)")
    print("Done. Restart the engine (or reload_atlas) to see it.")


if __name__ == "__main__":
    main()
