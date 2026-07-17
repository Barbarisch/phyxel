#!/usr/bin/env python3
"""leaf_forge — voxel-native RGBA leaf textures for the foliage card system.

SOLE AUTHOR of the leaf_*.png sources (gen_nature_textures.py no longer touches
leaves). Earlier versions only baked an alpha mask into pre-colored albedo, so the
mask holes didn't line up with the painted leaves and the space BETWEEN leaves was
an opaque dark-green backing plane. Now RGB and alpha are authored together:

  * alpha 255 exactly where a leaf cell is drawn, 0 everywhere else — the negative
    space between leaves is genuinely see-through (foliage.frag discards a<0.5,
    voxel.frag discards a<0.1, so every render path honors it);
  * each species has its own SILHOUETTE (shape generators below) and its own
    multi-tone coloring — per-cluster tone from the species ramp(s), per-cell
    jitter, and cheap top-lit rim shading — so species differ in shape, not just
    hue. Autumn mixes rust/orange/gold ramps per cluster; jungle fronds carry a
    lighter midrib.

Voxel aesthetic by construction (user direction 2026-07-05): everything is drawn
on a chunky 32x32 cell grid and upscaled NEAREST to 512px — hard-edged square
cells, micro-voxel clusters, never smooth organic sprites.

RGB under transparent cells is dilated outward from the nearest drawn leaf (and
backfilled with the ramp midpoint) so bilinear/BC7 edge sampling never pulls in a
dark fringe.

Per species: 6 face textures (further flipped/swapped into 8 orientations per
card by foliage.vert = 48 variants). Deterministic per (species, face): same run
-> byte-identical PNGs. Re-run after changing recipes; the BC7 atlas cache hash
picks the new PNGs up on next engine launch.
"""

import os
import random

from PIL import Image

GRID = 32                      # cells per axis (512px / GRID = 16px per cell)
TEX_SIZE = 512
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

# Species color ramps (dark, light). A cluster picks ONE ramp + a tone, so autumn
# reads as mixed-hue fall foliage rather than a single recolor.
RAMPS = {
    "oak":    [((26, 74, 18),  (110, 196, 66))],
    "birch":  [((54, 96, 26),  (162, 212, 98))],
    "spruce": [((14, 52, 34),  (66, 128, 92))],
    "jungle": [((16, 88, 20),  (84, 210, 58))],
    "autumn": [((104, 40, 10), (232, 148, 40)),    # orange
               ((92, 24, 14),  (206, 74, 34)),     # rust red
               ((110, 66, 12), (240, 190, 70))],   # gold
}


def _lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def _blank():
    return [[None] * GRID for _ in range(GRID)]


def _inside(x, y):
    return 0 <= x < GRID and 0 <= y < GRID and \
        (x - CENTER) ** 2 + (y - CENTER) ** 2 <= RADIUS ** 2


class Painter:
    """Stamps colored cells; owns the per-species ramp choice + jitter."""

    def __init__(self, rng, species):
        self.rng = rng
        self.ramps = RAMPS[species]
        self.grid = _blank()
        self.cluster_color = None

    def new_cluster(self):
        """Pick this cluster's base tone: one ramp + a tone biased light (the dark
        end is shadow accent, not a backing plane)."""
        dark, light = self.rng.choice(self.ramps)
        t = 0.35 + 0.65 * self.rng.random()
        self.cluster_color = _lerp(dark, light, t)

    def stamp(self, x, y, w, h, lighten=0.0):
        base = self.cluster_color
        for j in range(y, y + h):
            for i in range(x, x + w):
                if _inside(i, j):
                    jit = 1.0 + lighten + self.rng.uniform(-0.08, 0.08)
                    self.grid[j][i] = tuple(min(255, max(0, int(c * jit))) for c in base)


def _cluster_point(rng, spread=0.55):
    while True:
        x = rng.gauss(CENTER, GRID * spread * 0.5)
        y = rng.gauss(CENTER, GRID * spread * 0.5)
        xi, yi = int(x), int(y)
        if _inside(xi, yi):
            return xi, yi


def draw_oak(p):
    """Lobed blob clusters: chunky 3-5 cell cores with square lobes attached.
    Dense mass with ragged see-through gaps (~55-65% of the card circle)."""
    for _ in range(42):
        p.new_cluster()
        x, y = _cluster_point(p.rng)
        s = p.rng.choice((3, 3, 4, 5))
        p.stamp(x - s // 2, y - s // 2, s, s)
        for _ in range(p.rng.randint(3, 6)):           # lobes, slightly lighter tips
            dx, dy = p.rng.choice(((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, 1)))
            p.stamp(x + dx * (s // 2 + 1), y + dy * (s // 2 + 1), 2, 2, lighten=0.10)


def draw_birch(p):
    """Small leaves in loose sprays: airiest species (~35-45%), lots of sky
    through the crown."""
    for _ in range(130):
        p.new_cluster()
        x, y = _cluster_point(p.rng, spread=0.65)
        s = p.rng.choice((1, 2, 2, 2, 3))
        p.stamp(x, y, s, s)


def draw_spruce(p):
    """Needle tufts: dense elongated runs radiating from many cluster points (~50%)."""
    for _ in range(52):
        p.new_cluster()
        x, y = _cluster_point(p.rng)
        for _ in range(p.rng.randint(3, 5)):           # needles per tuft
            dx, dy = p.rng.choice(((1, 0), (0, 1), (1, 1), (1, -1)))
            n = p.rng.randint(3, 6)
            w = p.rng.choice((1, 1, 2))
            for k in range(n):
                p.stamp(x + dx * k, y + dy * k, w, w, lighten=0.10 * (k / max(1, n - 1)))


def draw_jungle(p):
    """Big overlapping fronds: wide serrated bars over a heavy centre mass
    (~60-70%), each frond with a lighter midrib along its axis."""
    p.new_cluster()
    p.stamp(int(CENTER) - 5, int(CENTER) - 5, 11, 11)
    for _ in range(18):
        p.new_cluster()
        x, y = _cluster_point(p.rng, spread=0.5)
        horiz = p.rng.random() < 0.5
        ln = p.rng.randint(11, 16)
        wd = p.rng.randint(4, 6)
        for k in range(ln):
            notch = (k % 2 == 1)                       # serrated edge
            if horiz:
                p.stamp(x - ln // 2 + k, y - wd // 2 + (1 if notch else 0),
                        1, wd - (1 if notch else 0))
            else:
                p.stamp(x - wd // 2 + (1 if notch else 0), y - ln // 2 + k,
                        wd - (1 if notch else 0), 1)
        # midrib: a lighter 1-cell line along the frond axis
        for k in range(ln):
            if horiz:
                p.stamp(x - ln // 2 + k, y, 1, 1, lighten=0.22)
            else:
                p.stamp(x, y - ln // 2 + k, 1, 1, lighten=0.22)


DRAWERS = {"oak": draw_oak, "autumn": draw_oak, "birch": draw_birch,
           "spruce": draw_spruce, "jungle": draw_jungle}


def _rim_shade(grid):
    """Cheap top-lit form: cells whose upper neighbour is open get lighter, cells
    whose lower neighbour is open get darker — clusters read as lit lumps."""
    out = _blank()
    for y in range(GRID):
        for x in range(GRID):
            c = grid[y][x]
            if c is None:
                continue
            f = 1.0
            if y == 0 or grid[y - 1][x] is None:
                f *= 1.14
            if y == GRID - 1 or grid[y + 1][x] is None:
                f *= 0.90
            out[y][x] = tuple(min(255, int(ch * f)) for ch in c)
    return out


def _dilate_rgb(grid, mid):
    """RGB for transparent cells: flood outward from drawn leaves (then ramp
    midpoint) so bilinear/BC7 edge sampling never blends toward a dark fringe.
    Returns a full GRID x GRID color grid + the alpha grid."""
    color = [[grid[y][x] for x in range(GRID)] for y in range(GRID)]
    alpha = [[grid[y][x] is not None for x in range(GRID)] for y in range(GRID)]
    frontier = [(x, y) for y in range(GRID) for x in range(GRID) if alpha[y][x]]
    while frontier:
        nxt = []
        for x, y in frontier:
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < GRID and 0 <= ny < GRID and color[ny][nx] is None:
                    color[ny][nx] = color[y][x]
                    nxt.append((nx, ny))
        frontier = nxt
    for y in range(GRID):
        for x in range(GRID):
            if color[y][x] is None:
                color[y][x] = mid
    return color, alpha


def render(species, face_i):
    rng = random.Random(f"leaf_forge:{species}:{face_i}")
    p = Painter(rng, species)
    DRAWERS[species](p)
    shaded = _rim_shade(p.grid)
    dark, light = RAMPS[species][0]
    color, alpha = _dilate_rgb(shaded, _lerp(dark, light, 0.6))

    img = Image.new("RGBA", (TEX_SIZE, TEX_SIZE))
    px = img.load()
    scale = TEX_SIZE // GRID
    opaque = 0
    for y in range(TEX_SIZE):
        gy = min(y // scale, GRID - 1)
        for x in range(TEX_SIZE):
            gx = min(x // scale, GRID - 1)
            a = 255 if alpha[gy][gx] else 0
            px[x, y] = color[gy][gx] + (a,)
            opaque += a != 0
    return img, opaque / (TEX_SIZE * TEX_SIZE)


def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src = os.path.join(repo, "resources", "textures", "source")
    for species, files in SPECIES_FILES.items():
        for face_i, stem in enumerate(files):
            img, cov = render(species, face_i)
            img.save(os.path.join(src, stem + ".png"))
            print(f"{stem}.png: {species}, coverage {cov:.0%}")


if __name__ == "__main__":
    main()
