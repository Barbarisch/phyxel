#!/usr/bin/env python3
"""
gen_roof_materials.py — the vernacular ROOF material family for Phyxel.

Produces per-face albedo PNGs (512px, into resources/textures/source/) for:
  Thatch      (redo)   dried straw
  ClayTile             terracotta plain tile
  WoodShingle          split wooden shakes
  Slate                thin blue-grey slate
  StoneSlab            heavy grey stone-slab roof (Yorkshire stone)

ALIGNMENT DESIGN (see the roofing discussion / docs/MaterialTextureNeeds.md).
Phyxel voxel roofs are *stepped* — a staircase of subcubes at the style pitch.
That geometry already draws one strong horizontal course line per step, so the
texture must NOT bake in a second, competing course rhythm (the v1 thatch bug:
5 hard mid-face lines that fought the grid and never aligned on subcubes).

Rules every maker here follows:
  * COURSES RUN IN IMAGE-Y and the course *shadow/overlap* sits only at the
    BOTTOM EDGE of each course band — so it lands on the step lip, never
    mid-face. COURSES=3 per full-cube tile => each 1/3 subcube slice shows
    exactly one clean course ("the step is the course").
  * Everything runs the SAME direction on every face (down-slope = image-Y),
    so straw/grain/seams continue over the step lip instead of switching
    material between top face and riser.
  * Horizontally SEAMLESS (unit width divides SIZE; stagger wraps over 2 rows)
    so a row of voxels reads as one continuous run. Materials are varied:false.

Albedo only — the atlas auto-generates flat normal/rough sidecars.

Run from repo root:  python tools/gen_roof_materials.py
"""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join("resources", "textures", "source")
FACES = ["side_n", "side_s", "side_e", "side_w", "top", "bottom"]
SIZE = 512
COURSES = 3                    # courses per full-cube tile == subcube steps


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


def strands(size, seed, fine=384, coarse=4):
    """Down-slope striations: many fine columns across X, smooth along image-Y,
    so the striations RUN vertically (down-slope) and continue over the step lip."""
    return _vnoise(size, coarse, fine, seed)


def tint(field, dark, light):
    d = np.array(dark, np.float32); l = np.array(light, np.float32)
    return d + (l - d) * np.clip(field, 0, 1)[..., None]


def save(rgb, name):
    arr = np.clip(rgb, 0, 255).astype(np.uint8)
    Image.fromarray(arr, "RGB").save(os.path.join(OUT, name + ".png"))


def save_set(base, maker, seed0):
    for i, f in enumerate(FACES):
        save(maker(SIZE, seed0 + i * 17, f), f"{base}_{f}")


# ---------------------------------------------------------------- course frame
def course_bounds(size, courses=COURSES):
    """Y-pixel [start, end) of each course band, top->bottom."""
    edges = [round(size * i / courses) for i in range(courses + 1)]
    return list(zip(edges[:-1], edges[1:]))


def lip_shadow(rgb, size, courses=COURSES, drop=0.55, feather=None):
    """Darken the bottom few rows of every course band (the overlap lip)."""
    feather = feather or max(3, size // 42)
    out = rgb.copy()
    for _, y1 in course_bounds(size, courses):
        for k in range(feather):
            y = y1 - 1 - k
            if 0 <= y < size:
                f = drop * (1.0 - k / feather)
                out[y, :, :] *= (1.0 - f)
    return out


def unit_seams(draw, size, courses, cols, stagger=True, color=(0, 0, 0), width=2):
    """Vertical seams between individual units, brick-bond staggered per course."""
    for ci, (y0, y1) in enumerate(course_bounds(size, courses)):
        off = (0.5 if (stagger and ci % 2) else 0.0)
        for j in range(cols):
            x = int(size * ((j + off) % cols) / cols)
            draw.line([(x, y0), (x, y1)], fill=color, width=width)


# ---------------------------------------------------------------- Thatch (redo)
def thatch_face(size, seed, face):
    # Soft down-slope straw. NO baked mid-face course lines — the step lip only.
    st = strands(size, seed, fine=size, coarse=3)
    fine = fbm(size, seed + 4, octaves=5, base=40) * 0.14
    undulate = fbm(size, seed + 9, octaves=3, base=6) * 0.12
    v = np.clip(0.70 + 0.40 * st + fine + undulate - 0.10, 0, 1.12)
    rgb = tint(v, (120, 92, 38), (210, 178, 96))
    if face != "bottom":
        rgb = lip_shadow(rgb, size, drop=0.42, feather=max(4, size // 30))
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    return np.asarray(img.filter(ImageFilter.GaussianBlur(0.6)), np.float32)


# ---------------------------------------------------------------- ClayTile
def clay_tile_face(size, seed, face):
    rng = np.random.default_rng(seed)
    mott = fbm(size, seed, octaves=4, base=8) * 0.22 + 0.80
    v = np.clip(mott, 0, 1.1)
    rgb = tint(v, (150, 66, 40), (206, 108, 66))          # terracotta ramp
    # per-tile colour jitter (kiln variation), 5 tiles across a course
    cols = 5
    for ci, (y0, y1) in enumerate(course_bounds(size)):
        off = 0.5 if ci % 2 else 0.0
        for j in range(cols + 1):
            x0 = int(size * ((j - 1 + off)) / cols)
            x1 = int(size * ((j + off)) / cols)
            jt = 0.86 + 0.28 * rng.random()
            rgb[y0:y1, max(0, x0):max(0, x1)] *= jt
    rgb = lip_shadow(rgb, size, drop=0.5)
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    unit_seams(ImageDraw.Draw(img), size, COURSES, cols, color=(70, 28, 16),
               width=max(2, size // 200))
    return np.asarray(img.filter(ImageFilter.GaussianBlur(0.4)), np.float32)


# ---------------------------------------------------------------- WoodShingle
def wood_shingle_face(size, seed, face):
    rng = np.random.default_rng(seed)
    grain = strands(size, seed, fine=size, coarse=2)      # vertical wood grain
    weather = fbm(size, seed + 3, octaves=5, base=6) * 0.30
    v = np.clip(0.55 + 0.42 * grain + weather - 0.12, 0, 1.1)
    rgb = tint(v, (74, 52, 30), (150, 116, 78))           # brown shake ramp
    cols = 7
    for ci, (y0, y1) in enumerate(course_bounds(size)):
        off = 0.5 if ci % 2 else 0.0
        for j in range(cols + 1):
            x0 = int(size * (j - 1 + off) / cols)
            x1 = int(size * (j + off) / cols)
            jt = 0.80 + 0.36 * rng.random()               # each shake weathers apart
            rgb[y0:y1, max(0, x0):max(0, x1)] *= jt
    rgb = lip_shadow(rgb, size, drop=0.55)
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    unit_seams(ImageDraw.Draw(img), size, COURSES, cols, color=(30, 20, 10),
               width=max(2, size // 220))
    return np.asarray(img.filter(ImageFilter.GaussianBlur(0.3)), np.float32)


# ---------------------------------------------------------------- Slate
def slate_face(size, seed, face):
    rng = np.random.default_rng(seed)
    mott = fbm(size, seed, octaves=4, base=10) * 0.16 + 0.84
    hue = fbm(size, seed + 7, octaves=3, base=5)          # purple/green cast
    v = np.clip(mott, 0, 1.05)
    rgb = tint(v, (48, 54, 66), (104, 112, 128))          # blue-grey
    rgb[..., 0] += (hue - 0.5) * 22                        # subtle purple/green shift
    rgb[..., 1] += (0.5 - hue) * 14
    cols = 6
    for ci, (y0, y1) in enumerate(course_bounds(size)):
        off = 0.5 if ci % 2 else 0.0
        for j in range(cols + 1):
            x0 = int(size * (j - 1 + off) / cols)
            x1 = int(size * (j + off) / cols)
            jt = 0.90 + 0.18 * rng.random()
            rgb[y0:y1, max(0, x0):max(0, x1)] *= jt
    rgb = lip_shadow(rgb, size, drop=0.5)
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    unit_seams(ImageDraw.Draw(img), size, COURSES, cols, color=(20, 24, 32),
               width=max(2, size // 230))
    return np.asarray(img.filter(ImageFilter.GaussianBlur(0.35)), np.float32)


# ---------------------------------------------------------------- StoneSlab
def stone_slab_face(size, seed, face):
    rng = np.random.default_rng(seed)
    rough = fbm(size, seed, octaves=6, base=7) * 0.34 + 0.72
    moss = fbm(size, seed + 5, octaves=4, base=9)         # greenish weathering
    v = np.clip(rough, 0, 1.1)
    rgb = tint(v, (86, 84, 78), (150, 148, 138))          # warm grey stone
    mossmask = np.clip((moss - 0.62) * 3, 0, 1)[..., None]
    rgb = rgb * (1 - 0.35 * mossmask) + mossmask * np.array([70, 84, 54], np.float32) * 0.35
    cols = 4                                              # big irregular slabs
    for ci, (y0, y1) in enumerate(course_bounds(size)):
        off = 0.5 if ci % 2 else 0.0
        for j in range(cols + 1):
            x0 = int(size * (j - 1 + off) / cols)
            x1 = int(size * (j + off) / cols)
            jt = 0.88 + 0.22 * rng.random()
            rgb[y0:y1, max(0, x0):max(0, x1)] *= jt
    rgb = lip_shadow(rgb, size, drop=0.45)
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    unit_seams(ImageDraw.Draw(img), size, COURSES, cols, color=(44, 42, 38),
               width=max(3, size // 150))
    return np.asarray(img.filter(ImageFilter.GaussianBlur(0.5)), np.float32)


def main():
    os.makedirs(OUT, exist_ok=True)
    save_set("thatch", thatch_face, 9000)
    save_set("clay_tile", clay_tile_face, 4100)
    save_set("wood_shingle", wood_shingle_face, 4200)
    save_set("slate", slate_face, 4300)
    save_set("stone_slab", stone_slab_face, 4400)
    print("Roof family regen complete @512: thatch, clay_tile, wood_shingle, slate, stone_slab.")


if __name__ == "__main__":
    main()
