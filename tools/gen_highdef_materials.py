#!/usr/bin/env python3
"""
gen_highdef_materials.py — High-resolution regen for materials whose stock
source PNGs were only 64x64 (upscaled to fill the 512/1024 BC7 atlas, so they
looked blurry). Tier-1 of the texture-quality pass (see docs/MaterialTextureNeeds.md).

Outputs all 6 per-face albedo PNGs per material with the EXACT filenames the
materials reference, into resources/textures/source/:
  1024px detail/hero metals: gold, metal, glass, mirror
   512px:                     leaf (+ birch/spruce/jungle/autumn), glow, thatch, default

Notes:
  * glow/glow_blue/glow_green share one white glow_*.png set — the colour comes
    from each material's colorTint, so we only emit a neutral bright set.
  * Mirror previously pointed at a missing 'Glass_side.png'; this emits a proper
    mirror_*.png set (materials.json is repointed alongside).
  * Albedo only — the atlas auto-generates flat normal/rough sidecars.

Run from repo root:  python tools/gen_highdef_materials.py
"""
import os, math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join("resources", "textures", "source")
FACES = ["side_n", "side_s", "side_e", "side_w", "top", "bottom"]


# ---------------------------------------------------------------- noise helpers
def _vnoise(size, cells_y, cells_x, seed):
    rng = np.random.default_rng(seed)
    small = (rng.random((cells_y, cells_x)) * 255).astype(np.uint8)
    im = Image.fromarray(small).resize((size, size), Image.BICUBIC)
    return np.asarray(im, np.float32) / 255.0


def fbm(size, seed, octaves=5, base=4):
    out = np.zeros((size, size), np.float32)
    amp, tot, c = 1.0, 0.0, base
    for o in range(octaves):
        out += amp * _vnoise(size, c, c, seed + o)
        tot += amp; amp *= 0.5; c *= 2
    return out / tot


def streaks(size, seed, vertical=True, fine=320, coarse=5):
    # Anisotropic noise -> brushed/strand streaks.
    cy, cx = (coarse, fine) if vertical else (fine, coarse)
    return _vnoise(size, cy, cx, seed)


def tint(field, dark, light):
    """Map a [0,1] field onto a dark->light colour ramp -> HxWx3 float."""
    d = np.array(dark, np.float32); l = np.array(light, np.float32)
    return d + (l - d) * field[..., None]


def save(rgb, name):
    arr = np.clip(rgb, 0, 255).astype(np.uint8)
    Image.fromarray(arr, "RGB").save(os.path.join(OUT, name + ".png"))


def save_set(base, maker, size, seed0):
    for i, f in enumerate(FACES):
        save(maker(size, seed0 + i * 17, f), f"{base}_{f}")


# ----------------------------------------------------------------- metals
def gold_face(size, seed, face):
    base = fbm(size, seed, octaves=4, base=6) * 0.18 + 0.82      # gentle mottle
    br = streaks(size, seed + 5, vertical=(face not in ("top", "bottom")))
    v = np.clip(base * (0.80 + 0.30 * br), 0, 1.2)
    rgb = tint(v, (150, 110, 18), (255, 226, 130))
    # Diagonal sheen highlight.
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32) / size
    sheen = np.exp(-((xx + yy - 1.0) ** 2) / 0.05)[..., None]
    rgb = rgb + sheen * np.array([45, 38, 12], np.float32)
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)                                       # a few fine bright scratches
    rng = np.random.default_rng(seed + 99)
    for _ in range(size // 12):
        x0, y0 = int(rng.integers(0, size)), int(rng.integers(0, size))
        ln = int(rng.integers(size // 8, size // 3))
        d.line([(x0, y0), (x0 + int(rng.integers(-8, 8)), y0 + ln)], fill=(255, 244, 190), width=1)
    return np.asarray(img, np.float32)


def metal_face(size, seed, face):
    base = fbm(size, seed, octaves=5, base=5) * 0.15 + 0.80
    br = streaks(size, seed + 3, vertical=(face not in ("top", "bottom")), fine=420)
    grain = fbm(size, seed + 8, octaves=6, base=64) * 0.10
    v = np.clip(base * (0.85 + 0.22 * br) + grain - 0.05, 0, 1.15)
    rgb = tint(v, (96, 100, 112), (214, 218, 230))
    return rgb


def glass_face(size, seed, face):
    base = fbm(size, seed, octaves=3, base=4) * 0.08 + 0.90
    br = streaks(size, seed + 2, vertical=True, fine=180, coarse=4) * 0.10
    v = np.clip(base + br - 0.04, 0, 1.1)
    rgb = tint(v, (176, 202, 224), (228, 242, 252))
    # Bright bevel near the edges.
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    e = np.minimum.reduce([xx, yy, size - 1 - xx, size - 1 - yy]) / (size * 0.12)
    edge = np.clip(1.0 - e, 0, 1)[..., None]
    rgb = rgb + edge * np.array([30, 34, 40], np.float32)
    return rgb


def mirror_face(size, seed, face):
    base = fbm(size, seed, octaves=3, base=4) * 0.05 + 0.93
    yy = (np.mgrid[0:size, 0:size][0].astype(np.float32) / size)   # vertical sheen
    v = np.clip(base * (0.9 + 0.18 * (1 - yy)), 0, 1.1)
    return tint(v, (150, 158, 170), (224, 230, 240))


# ----------------------------------------------------------------- foliage
LEAF_RAMPS = {
    "leaf":        ((18, 52, 12), (104, 196, 66)),
    "leaf_birch":  ((44, 84, 22), (158, 206, 96)),
    "leaf_spruce": ((8, 40, 26), (58, 118, 84)),
    "leaf_jungle": ((10, 70, 14), (74, 208, 54)),
    "leaf_autumn": ((92, 34, 8), (232, 142, 40)),
}


def leaf_maker(dark, light):
    def maker(size, seed, face):
        rng = np.random.default_rng(seed)
        # Dark gappy base so leaves read as a clump, not a flat slab.
        clump = fbm(size, seed, octaves=5, base=5)
        base = tint(clump * 0.5, dark, tuple(int((d + l) / 2) for d, l in zip(dark, light)))
        img = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))
        d = ImageDraw.Draw(img)
        mid = tuple(int((a + b) / 2) for a, b in zip(dark, light))
        for _ in range(size * size // 700):                      # scatter leaf blades
            cx, cy = rng.integers(0, size), rng.integers(0, size)
            r = rng.integers(size // 40, size // 18)
            t = rng.random()
            col = tuple(int(dark[k] + (light[k] - dark[k]) * (0.4 + 0.6 * t)) for k in range(3))
            ang = rng.random() * 360
            blade = Image.new("RGBA", (r * 2, int(r * 1.2)), (0, 0, 0, 0))
            bd = ImageDraw.Draw(blade)
            bd.ellipse([0, 0, r * 2 - 1, int(r * 1.2) - 1], fill=col + (255,))
            bd.line([(r, 2), (r, int(r * 1.2) - 2)], fill=mid + (255,), width=1)  # vein
            blade = blade.rotate(ang, expand=True)
            img.paste(blade, (cx - blade.width // 2, cy - blade.height // 2), blade)
        return np.asarray(img.convert("RGB"), np.float32)
    return maker


# ----------------------------------------------------------------- glow / thatch / default
def glow_face(size, seed, face):
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    cx = cy = size / 2
    dist = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / (size * 0.72)
    v = np.clip(1.05 - 0.30 * dist, 0.6, 1.0) + fbm(size, seed, octaves=4, base=16) * 0.04
    return tint(np.clip(v, 0, 1), (210, 200, 168), (255, 252, 240))


def thatch_face(size, seed, face):
    strand = streaks(size, seed, vertical=(face not in ("top", "bottom")), fine=size, coarse=3)
    fine = fbm(size, seed + 4, octaves=5, base=48) * 0.12
    v = np.clip(0.72 + 0.42 * strand + fine - 0.06, 0, 1.1)
    rgb = tint(v, (120, 92, 38), (208, 176, 96))
    # Darker course lines (overlapping bundles of thatch).
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    n = 5
    for i in range(1, n):
        y = int(size * i / n)
        d.line([(0, y), (size, y)], fill=(86, 64, 26), width=max(2, size // 110))
    return np.asarray(img.filter(ImageFilter.GaussianBlur(0.5)), np.float32)


def default_face(size, seed, face):
    img = Image.new("RGB", (size, size), (40, 40, 40))
    d = ImageDraw.Draw(img)
    c = size // 8
    for gy in range(8):
        for gx in range(8):
            if (gx + gy) % 2 == 0:
                d.rectangle([gx * c, gy * c, (gx + 1) * c, (gy + 1) * c], fill=(255, 0, 255))
    return np.asarray(img, np.float32)


def main():
    os.makedirs(OUT, exist_ok=True)
    save_set("gold", gold_face, 1024, 1000)
    save_set("metal", metal_face, 1024, 2000)
    save_set("glass", glass_face, 1024, 3000)
    save_set("mirror", mirror_face, 1024, 3500)
    for base, (dk, lt) in LEAF_RAMPS.items():
        save_set(base, leaf_maker(dk, lt), 512, hash(base) % 9000 + 100)
    save_set("glow", glow_face, 512, 8000)
    save_set("thatch", thatch_face, 512, 9000)
    save_set("default", default_face, 512, 50)
    print("High-def regen complete:",
          "gold/metal/glass/mirror @1024, leaves(5)/glow/thatch/default @512.")


if __name__ == "__main__":
    main()
