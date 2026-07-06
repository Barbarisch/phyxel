#!/usr/bin/env python3
"""leaf_forge — voxel-native cutout masks for the foliage leaf cards.

The foliage card system (FoliageRenderPipeline + foliage.vert/frag) renders each exposed leaf
voxel as a handful of billboard cards. Until now every card was a flat ELLIPSE (a math discard
in foliage.frag) — "flat circles". This tool bakes per-species leaf-cluster silhouettes into the
ALPHA channel of the existing leaf albedo textures; foliage.frag alpha-tests them, so cards get
real see-through leaf clusters instead of solid ovals.

Voxel aesthetic by construction: masks are authored on a chunky 32x32 cell grid and upscaled
NEAREST to the texture size — every hole and lobe is a hard-edged square cell, so the cutouts
read as micro-voxel clusters, not smooth organic sprites (user direction 2026-07-05).

Per-species silhouettes (6 face textures per species = 6 mask variants, further rotated/mirrored
per card in the shader):
  oak / autumn — lobed blob clusters          birch — sparse small leaves
  spruce       — radiating needle tufts       jungle — big notched fronds

RGB is left untouched (cards keep sampling the same albedo); alpha 255 = leaf, 0 = gap.
Deterministic per (species, face): same run -> byte-identical PNGs. Re-run after changing
recipes; rebuild is picked up by the BC7 atlas cache hash on next engine launch.
"""

import os
import random

from PIL import Image

GRID = 32                      # mask cells per axis (chunky: texture_size/GRID px per cell)
SPECIES_FILES = {
    "oak":    ["leaf_top", "leaf_bottom", "leaf_side_n", "leaf_side_s", "leaf_side_e", "leaf_side_w"],
    "autumn": ["leaf_autumn_top", "leaf_autumn_bottom", "leaf_autumn_side_n",
               "leaf_autumn_side_s", "leaf_autumn_side_e", "leaf_autumn_side_w"],
    "birch":  ["leaf_birch_top", "leaf_birch_bottom", "leaf_birch_side_n",
               "leaf_birch_side_s", "leaf_birch_side_e", "leaf_birch_side_w"],
    "spruce": ["leaf_spruce_top", "leaf_spruce_bottom", "leaf_spruce_side_n",
               "leaf_spruce_side_s", "leaf_spruce_side_e", "leaf_spruce_side_w"],
    "jungle": ["leaf_jungle_top", "leaf_jungle_bottom", "leaf_jungle_side_n",
               "leaf_jungle_side_s", "leaf_jungle_side_e", "leaf_jungle_side_w"],
}
CENTER = (GRID - 1) / 2.0
RADIUS = GRID * 0.47           # keep silhouettes inside a rough circle (card corners stay clear)


def _blank():
    return [[0] * GRID for _ in range(GRID)]


def _inside(x, y):
    return 0 <= x < GRID and 0 <= y < GRID and \
        (x - CENTER) ** 2 + (y - CENTER) ** 2 <= RADIUS ** 2


def _stamp(g, x, y, w, h):
    for j in range(y, y + h):
        for i in range(x, x + w):
            if _inside(i, j):
                g[j][i] = 1


def _cluster_point(rng, spread=0.55):
    """Random cell biased toward the centre."""
    while True:
        x = rng.gauss(CENTER, GRID * spread * 0.5)
        y = rng.gauss(CENTER, GRID * spread * 0.5)
        xi, yi = int(x), int(y)
        if _inside(xi, yi):
            return xi, yi


def mask_oak(rng):
    """Lobed blob clusters: chunky 3-5 cell cores with square lobes attached. Dense mass with
    ragged see-through gaps (~55-65% of the card circle)."""
    g = _blank()
    for _ in range(42):
        x, y = _cluster_point(rng)
        s = rng.choice((3, 3, 4, 5))
        _stamp(g, x - s // 2, y - s // 2, s, s)
        for _ in range(rng.randint(3, 6)):           # lobes
            dx, dy = rng.choice(((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, 1)))
            _stamp(g, x + dx * (s // 2 + 1), y + dy * (s // 2 + 1), 2, 2)
    return g


def mask_birch(rng):
    """Small leaves in loose sprays: airiest species (~35-45%), lots of sky through the crown."""
    g = _blank()
    for _ in range(85):
        x, y = _cluster_point(rng, spread=0.65)
        s = rng.choice((1, 2, 2, 2, 3))
        _stamp(g, x, y, s, s)
    return g


def mask_spruce(rng):
    """Needle tufts: dense elongated runs radiating from many cluster points (~50%)."""
    g = _blank()
    for _ in range(38):
        x, y = _cluster_point(rng)
        for _ in range(rng.randint(3, 5)):           # needles per tuft
            dx, dy = rng.choice(((1, 0), (0, 1), (1, 1), (1, -1)))
            n = rng.randint(3, 6)
            w = rng.choice((1, 1, 2))
            for k in range(n):
                _stamp(g, x + dx * k, y + dy * k, w, w)
    return g


def mask_jungle(rng):
    """Big overlapping fronds: wide serrated bars over a heavy centre mass (~60-70%)."""
    g = _blank()
    _stamp(g, int(CENTER) - 5, int(CENTER) - 5, 11, 11)
    for _ in range(9):
        x, y = _cluster_point(rng, spread=0.5)
        horiz = rng.random() < 0.5
        ln = rng.randint(11, 16)
        wd = rng.randint(4, 6)
        for k in range(ln):
            notch = (k % 2 == 1)                     # serrated edge
            if horiz:
                _stamp(g, x - ln // 2 + k, y - wd // 2 + (1 if notch else 0),
                       1, wd - (1 if notch else 0))
            else:
                _stamp(g, x - wd // 2 + (1 if notch else 0), y - ln // 2 + k,
                       wd - (1 if notch else 0), 1)
    return g


MASKS = {"oak": mask_oak, "autumn": mask_oak, "birch": mask_birch,
         "spruce": mask_spruce, "jungle": mask_jungle}


def apply_mask(png_path, grid):
    img = Image.open(png_path).convert("RGBA")
    scale = img.width // GRID
    px = img.load()
    opaque = 0
    for y in range(img.height):
        gy = min(y // scale, GRID - 1)
        row = grid[gy]
        for x in range(img.width):
            r, g_, b, _ = px[x, y]
            a = 255 if row[min(x // scale, GRID - 1)] else 0
            px[x, y] = (r, g_, b, a)
            opaque += a != 0
    img.save(png_path)
    return opaque / (img.width * img.height)


def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src = os.path.join(repo, "resources", "textures", "source")
    for species, files in SPECIES_FILES.items():
        for face_i, stem in enumerate(files):
            path = os.path.join(src, stem + ".png")
            if not os.path.exists(path):
                print(f"SKIP {stem}.png (missing)")
                continue
            rng = random.Random(f"leaf_forge:{species}:{face_i}")
            cov = apply_mask(path, MASKS[species](rng))
            print(f"{stem}.png: {species} mask, coverage {cov:.0%}")


if __name__ == "__main__":
    main()
