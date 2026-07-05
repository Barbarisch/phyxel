#!/usr/bin/env python3
"""Parametric voxel tree generator — token-free biome flora (v2, sub-voxel).

*** DEPRECATED (2026-07-05) — superseded by tools/tree_forge.py ***
Biome flora pools (resources/biomes.json) now stamp forge_* templates from
tools/forge_library.json (one space-colonization algorithm, per-voxel multi-res,
detail-by-default). This generator and its tree_* templates remain ONLY because
existing worlds' persisted recipes (world.db world_meta) reference the old
template names. Do not add new archetypes here — extend tree_forge presets.

Generates trees as .voxel templates using ALL THREE voxel resolutions:
  C  full cube  (1.0)   — canopy interior mass, lower trunk
  S  subcube    (1/3)   — dithered canopy shells, branches, trunk taper,
                          root flares, palm fronds, willow strands
  M  microcube  (1/9)   — twig tips and leaf sprigs on the canopy surface

Geometry is computed in SUB-space (1 unit = 1/3 cube) and compressed on
write-out: any 3x3x3 block of same-material subs that fills a whole cube is
emitted as one C line, so interiors stay cheap while silhouettes stay organic.

Deterministic per seed: same args -> same tree; new --seed -> natural variation.

Archetypes (silhouette + default materials):
  oak      blob canopy, sub-branches, root flare      Log       + Leaf
  autumn   oak silhouette, fall colors                Log       + LeafAutumn
  birch    slim tapering trunk, small high canopy     LogBirch  + LeafBirch
  spruce   smooth sub-resolution cone                 LogSpruce + LeafSpruce
  pine     CRISP whorled tiers, bare lower bole       Log       + LeafSpruce
  fir      CRISP narrow continuous spire cone         LogSpruce + LeafSpruce
  jungle   tall trunk, buttress roots, wide crown     Log       + LeafJungle
  acacia   sub-diagonal kinked trunk, flat pads       Log       + Leaf
  palm     curved slender trunk, thin fronds          Log       + LeafJungle
  willow   dome + hanging 1/3-thick strands           Log       + Leaf
  dead     bare tapering branches, micro twigs        LogSpruce (no leaves)
  bush     dithered leaf ball with sprigs             Leaf

Edge control (--edge, batch key "edge"): "fuzzy" (default) keeps the dithered
SOLID..FUZZ shell rim; "crisp" disables dither + per-layer jitter for a hard
silhouette (honored by spruce; pine/fir are crisp by construction).

Examples:
  python tools/gen_tree.py --type oak --height 7 --name tree_oak_m
  python tools/gen_tree.py --type spruce --height 12 --fullness 0.85 --seed 4
  python tools/gen_tree.py --type pine --height 12 --seed 3 --preview
  python tools/gen_tree.py --batch tools/tree_library.json
"""

import argparse
import datetime
import json
import math
import os
import random
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE_DIR = os.path.join(REPO, "resources", "templates")
CATALOG = os.path.join(TEMPLATE_DIR, "template_catalog.json")

ARCHETYPES = {
    "oak":    {"log": "Log",       "leaf": "Leaf",       "height": 7},
    "autumn": {"log": "Log",       "leaf": "LeafAutumn", "height": 7},
    "birch":  {"log": "LogBirch",  "leaf": "LeafBirch",  "height": 8},
    "spruce": {"log": "LogSpruce", "leaf": "LeafSpruce", "height": 9},
    "pine":   {"log": "Log",       "leaf": "LeafSpruce", "height": 12},
    "fir":    {"log": "LogSpruce", "leaf": "LeafSpruce", "height": 14},
    "jungle": {"log": "Log",       "leaf": "LeafJungle", "height": 13},
    "acacia": {"log": "Log",       "leaf": "Leaf",       "height": 6},
    "palm":   {"log": "Log",       "leaf": "LeafJungle", "height": 8},
    "willow": {"log": "Log",       "leaf": "Leaf",       "height": 8},
    "dead":   {"log": "LogSpruce", "leaf": None,         "height": 6},
    "bush":   {"log": None,        "leaf": "Leaf",       "height": 2},
    # Megaflora (Increment B): wide + short broad-canopy world-trees (heights in cubes = metres).
    "redwood":   {"log": "Log", "leaf": "Leaf", "height": 48},
    "elder_oak": {"log": "Log", "leaf": "Leaf", "height": 36},
}

# Shell band: subs with normalized canopy distance <= SOLID are always kept
# (interiors compress to C); SOLID..FUZZ is the dithered organic rim.
SOLID, FUZZ = 0.92, 1.06


class Tree:
    """Sub-space voxel accumulator. Logs always win over leaves."""

    def __init__(self):
        self.sub = {}     # (sx,sy,sz) -> (mat, is_log)
        self.micro = {}   # (sx,sy,sz,mx,my,mz) -> mat

    def put(self, sx, sy, sz, mat, is_log):
        if sy < 0:
            return
        k = (sx, sy, sz)
        cur = self.sub.get(k)
        if cur is None or (is_log and not cur[1]):
            self.sub[k] = (mat, is_log)

    def log(self, sx, sy, sz, mat):
        self.put(sx, sy, sz, mat, True)

    def leaf(self, sx, sy, sz, mat):
        self.put(sx, sy, sz, mat, False)

    def sprig(self, sx, sy, sz, mx, my, mz, mat):
        self.micro[(sx, sy, sz, mx, my, mz)] = mat

    def fill_cube(self, cx, cy, cz, mat, is_log=True):
        for dx in range(3):
            for dy in range(3):
                for dz in range(3):
                    self.put(cx * 3 + dx, cy * 3 + dy, cz * 3 + dz, mat, is_log)


# ------------------------------------------------------------ shape helpers

def shell_keep(rng, d, fullness):
    """Dithered keep-probability across the SOLID..FUZZ canopy rim."""
    if d <= SOLID:
        return True
    t = (d - SOLID) / (FUZZ - SOLID)          # 0 at solid edge, 1 at fuzz edge
    return rng.random() < fullness * (1.0 - 0.8 * t)


def ellipsoid_canopy(t, rng, cx, cy, cz, rx, ry, mat, fullness, sprigs=True):
    """Center and radii in SUB units. Solid core + dithered shell + sprigs."""
    rzx = rx
    for sx in range(int(cx - rx - 2), int(cx + rx + 3)):
        for sy in range(int(cy - ry - 2), int(cy + ry + 3)):
            for sz in range(int(cz - rzx - 2), int(cz + rzx + 3)):
                d = math.sqrt(((sx - cx) / rx) ** 2 + ((sy - cy) / ry) ** 2 +
                              ((sz - cz) / rzx) ** 2)
                if d > FUZZ:
                    continue
                if shell_keep(rng, d, fullness):
                    t.leaf(sx, sy, sz, mat)
                    # occasional microcube sprig poking out of the surface
                    if sprigs and d > 0.97 and rng.random() < 0.05:
                        ox = sx + (1 if sx > cx else -1 if sx < cx else 0)
                        oy = sy + (1 if sy > cy else 0)
                        oz = sz + (1 if sz > cz else -1 if sz < cz else 0)
                        t.sprig(ox, oy, oz, 1, 1, 1, mat)


def disc_canopy(t, rng, cy, r, mat, fullness, thick=2, cx=0, cz=0, sprigs=True):
    """Flat pad in SUB units: dithered rim, micro sprigs on the edge."""
    for sx in range(int(cx - r - 2), int(cx + r + 3)):
        for sz in range(int(cz - r - 2), int(cz + r + 3)):
            d = math.hypot(sx - cx, sz - cz) / r
            if d > FUZZ:
                continue
            for dy in range(thick):
                if shell_keep(rng, d, fullness):
                    t.leaf(sx, cy + dy, sz, mat)
            if sprigs and 0.9 < d <= FUZZ and rng.random() < 0.08:
                ox = sx + (1 if sx > cx else -1)
                oz = sz + (1 if sz > cz else -1)
                t.sprig(ox, cy, oz, 1, 2, 1, mat)


def cube_trunk(t, h_cubes, mat, taper_from=0.62, root_flare=True, rng=None):
    """Trunk centered on cube (0,*,0): full cubes below, plus-shaped taper above.

    The taper cross-section is a symmetric plus (center sub + 4 edge subs) so
    the narrower upper trunk stays centered on the lower cube — a 2x2 column
    can't center on the 3-sub grid and reads as a sideways kink.
    """
    taper_y = max(1, round(h_cubes * taper_from))
    for cy in range(taper_y):
        t.fill_cube(0, cy, 0, mat)
    for sy in range(taper_y * 3, h_cubes * 3):
        for (dx, dz) in ((1, 1), (0, 1), (2, 1), (1, 0), (1, 2)):
            t.log(dx, sy, dz, mat)
    if root_flare and rng:
        for (fx, fz) in [(-1, 0), (3, 0), (0, -1), (0, 3),
                         (-1, -1), (3, 3), (-1, 3), (3, -1)]:
            if rng.random() < 0.6:
                hgt = rng.randint(1, 2)
                for sy in range(hgt):
                    t.log(fx if fx >= 0 else -1, sy, fz if fz >= 0 else -1, mat)
    return h_cubes * 3                           # trunk top in subs


def sub_branch(t, rng, start, direction, length, mat, thickness=1,
               droop=0.0, rise=0.35, twig=True):
    """Random-walk branch in SUB space; returns end point."""
    x, y, z = start
    dx, dz = direction
    for i in range(length):
        r = rng.random()
        if r < 0.42:
            x += dx
        elif r < 0.84:
            z += dz
        else:
            y += 1 if rng.random() < rise else -1
        if droop and rng.random() < droop:
            y -= 1
        th = thickness if i < length * 0.6 else 1
        for bx in range(th):
            for by in range(th):
                t.log(x + bx, y + by, z, mat)
    if twig:                                     # micro twig at the tip
        tx, tz = x + dx, z + dz
        t.sprig(tx, y, tz, 1, 1, 1, mat)
        t.sprig(tx + dx, y + 1, tz, 0, 0, 1, mat)
    return (x, y, z)


# ----------------------------------------------------- branch-driven canopy
# Real crowns get their silhouette from the BRANCH architecture, not a sphere:
# limbs fork outward+upward with partial randomness, and foliage is deposited as
# small blobs ADJACENT to the twigs. The union of those blobs is an organic,
# partially-random crown instead of a machined ellipsoid.

def _norm(v):
    m = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) or 1.0
    return (v[0] / m, v[1] / m, v[2] / m)


def perturb_dir(rng, d, spread, up_bias=0.0):
    """Randomly bend a unit direction: lateral spread + optional upward bias."""
    return _norm((d[0] + rng.uniform(-spread, spread),
                  d[1] + rng.uniform(-spread, spread) + up_bias,
                  d[2] + rng.uniform(-spread, spread)))


def leaf_cluster(t, rng, cx, cy, cz, r, leaf, fullness, sprigs=True):
    """Small organic leaf blob centered on a twig end (sub units)."""
    ir = int(r) + 2
    for sx in range(cx - ir, cx + ir + 1):
        for sy in range(cy - ir, cy + ir + 1):
            for sz in range(cz - ir, cz + ir + 1):
                d = math.sqrt((sx - cx) ** 2 + (sy - cy) ** 2 +
                              (sz - cz) ** 2) / max(r, 0.5)
                if d > FUZZ:
                    continue
                if shell_keep(rng, d, fullness):
                    t.leaf(sx, sy, sz, leaf)
                    if sprigs and d > 0.95 and rng.random() < 0.06:
                        t.sprig(sx + (1 if sx > cx else -1), sy + 1,
                                sz + (1 if sz > cz else -1), 1, 1, 1, leaf)


def grow_branch(t, rng, pos, direction, length, thickness, depth,
                log, leaf, fullness, clusters, spread=0.7, up_bias=0.12,
                taper=0.66, fork=(2, 3)):
    """Recursive 3D branch walk. Lays log voxels along each segment, forks into
    child limbs with partial-random spread, and records leaf-cluster centers at
    the terminal twigs (and along the outer half of leafy limbs)."""
    x, y, z = float(pos[0]), float(pos[1]), float(pos[2])
    dx, dy, dz = direction
    for i in range(length):
        x += dx
        y += dy
        z += dz
        bx, by, bz = round(x), round(y), round(z)
        for ox in range(thickness):
            for oz in range(thickness):
                t.log(bx + ox, by, bz + oz, log)
        # Foliage hugs the OUTER portion of every limb (not just the tip) so the
        # branch wood stays buried under leaves instead of looking skeletal. The
        # inner limb near the trunk is left bare (realistic).
        if i >= length * 0.45 and i % 2 == 0:
            clusters.append((bx, by, bz))
    end = (round(x), round(y), round(z))
    if depth <= 0 or length <= 2:
        clusters.append(end)         # twig tip -> a leaf blob
        return
    clusters.append(end)             # leaf the fork joints too
    for _ in range(rng.randint(*fork)):
        cdir = perturb_dir(rng, (dx, dy, dz), spread, up_bias)
        clen = max(2, int(length * rng.uniform(taper - 0.1, taper + 0.12)))
        grow_branch(t, rng, end, cdir, clen, max(1, thickness - 1), depth - 1,
                    log, leaf, fullness, clusters, spread, up_bias, taper, fork)


# ---------------------------------------------------------------- archetypes

def branched_crown(t, rng, base, top, rc, log, leaf, fullness,
                   n_limbs=(4, 6), crown_lo=0.5, up=(0.35, 0.7),
                   out=(0.9, 1.4), depth=2, blob=(2.6, 4.0), leader=True):
    """Build an organic crown from forking limbs: primary limbs fan out (azimuth-
    spread) from the upper trunk, recurse with partial randomness, and deposit
    leaf blobs ADJACENT to every twig. The union of blobs is the silhouette."""
    cx, cz = base
    crown_lo_y = int(top * crown_lo)
    clusters = []
    nl = rng.randint(*n_limbs)
    for k in range(nl):
        by = rng.randint(crown_lo_y, top - 1)
        ang = 2 * math.pi * k / nl + rng.uniform(-0.5, 0.5)
        o = rng.uniform(*out)
        d0 = _norm((math.cos(ang) * o, rng.uniform(*up), math.sin(ang) * o))
        grow_branch(t, rng, (cx, by, cz), d0, rng.randint(int(rc * 1.4), rc * 2 + 1),
                    3, depth, log, leaf, fullness, clusters)
    if leader:                                   # gentle central leader
        grow_branch(t, rng, (cx, top, cz),
                    _norm((rng.uniform(-0.25, 0.25), 1.0, rng.uniform(-0.25, 0.25))),
                    max(2, int(rc * 1.5)), 2, max(1, depth - 1),
                    log, leaf, fullness, clusters)
    for (lx, ly, lz) in clusters:
        leaf_cluster(t, rng, lx, ly, lz, rng.uniform(*blob), leaf, fullness)


def gen_oak(t, rng, h, radius, fullness, log, leaf, edge="fuzzy"):
    rc = radius or max(2, round(h * 0.5))                  # canopy radius, cubes
    top = cube_trunk(t, h, log, taper_from=0.45, rng=rng)
    branched_crown(t, rng, (1, 1), top, rc, log, leaf, fullness)


def gen_birch(t, rng, h, radius, fullness, log, leaf, edge="fuzzy"):
    rc = radius or max(2, round(h * 0.32))
    top = cube_trunk(t, h, log, taper_from=0.4, root_flare=False, rng=rng)
    # slim, taller, sparser crown: fewer limbs, more vertical, smaller blobs
    branched_crown(t, rng, (1, 1), top, rc, log, leaf, fullness,
                   n_limbs=(2, 3), crown_lo=0.6, up=(0.6, 0.95),
                   out=(0.6, 1.0), depth=2, blob=(2.2, 3.2))


def gen_spruce(t, rng, h, radius, fullness, log, leaf, edge="fuzzy"):
    base_r = (radius or max(2, round(h * 0.38))) * 3
    if edge == "crisp":
        # Continuous hard-edged cone (same builder as fir; fuller-bottomed via
        # exp=1.1 to keep the classic broad-based spruce silhouette).
        crisp_cone(t, rng, h, base_r, log, leaf, fullness, crown_lo_frac=0.22,
                   exp=1.1)
        return
    top = cube_trunk(t, h, log, taper_from=0.5, root_flare=True, rng=rng)
    lo = max(5, round(top * 0.22))
    # smooth fuzzy cone: per-sub-layer radius from base_r to 1, dithered shell
    for sy in range(lo, top + 3):
        f = (sy - lo) / max(1, (top + 2 - lo))
        r = max(1.4, base_r * (1.0 - f) ** 1.1)
        r += rng.uniform(-0.4, 0.4)             # per-layer jitter (un-machined)
        for sx in range(int(1 - r - 1), int(1 + r + 2)):
            for sz in range(int(1 - r - 1), int(1 + r + 2)):
                d = math.hypot(sx - 1, sz - 1) / max(r, 0.1)
                if d <= FUZZ and shell_keep(rng, d, fullness):
                    t.leaf(sx, sy, sz, leaf)
    t.leaf(1, top + 3, 1, leaf)                            # tip spike
    t.sprig(1, top + 4, 1, 1, 0, 1, leaf)


def crisp_disc(t, cy, r, mat, cx=1, cz=1, rng=None, fullness=1.0):
    """Hard-edged foliage disc (no dither). The outer annulus (d in (r-1, r]) is
    ALWAYS filled, so the silhouette edge rasterizes fully and stays crisp — the
    crisp-conifer building block. `fullness` thins only the INTERIOR (d <= r-1):
    at fullness<1 interior cells drop out deterministically, so the crown reads
    sparser/see-through without ever eroding the edge. rim ring is untouched, so
    per-row rim_fill stays 1.0 at any fullness."""
    ir = int(r) + 1
    rim_lo = r - 1.5                              # always-solid rim band (>1 cell
                                                 # wide so it fully covers the edge
                                                 # metric's annulus at any fullness)
    for sx in range(cx - ir, cx + ir + 1):
        for sz in range(cz - ir, cz + ir + 1):
            d = math.hypot(sx - cx, sz - cz)
            if d > r:
                continue
            if d > rim_lo or fullness >= 1.0 or rng is None:
                t.leaf(sx, cy, sz, mat)          # edge band: always solid
            elif rng.random() < fullness:
                t.leaf(sx, cy, sz, mat)          # interior: density-thinned


def crisp_cone(t, rng, h, base_r, log, leaf, fullness, crown_lo_frac=0.22,
               exp=1.0):
    """Continuous hard-edged conifer cone (fir / crisp spruce). Stout bole to the
    crown base, then a SINGLE-sub leader through the crown so the narrow upper
    cone never collapses into the trunk (a plus-shaped r=1 trunk eats leaf cells
    at d<=1 via 'log wins' and leaves a bare pole — the defect this avoids). Each
    layer is a crisp_disc; radius tapers base_r -> ~1.4 with shape exponent `exp`
    (1.0 = straight fir cone, >1 = fuller-bottomed spruce). Returns cone top y."""
    top = h * 3
    lo = max(3, round(top * crown_lo_frac))
    for cy in range(max(1, lo // 3)):
        t.fill_cube(0, cy, 0, log)
    for sy in range(max(1, lo // 3) * 3, top):
        t.log(1, sy, 1, log)
    for (fx, fz) in [(-1, 0), (3, 0), (0, -1), (0, 3)]:
        if rng.random() < 0.5:                   # light root flare
            t.log(fx if fx >= 0 else -1, 0, fz if fz >= 0 else -1, log)
    for sy in range(lo, top + 3):
        f = (sy - lo) / max(1, (top + 2 - lo))
        crisp_disc(t, sy, max(1.4, base_r * (1.0 - f) ** exp), leaf,
                   rng=rng, fullness=fullness)
    t.leaf(1, top + 3, 1, leaf)                   # spire tip
    t.sprig(1, top + 4, 1, 1, 0, 1, leaf)
    return top


def gen_pine(t, rng, h, radius, fullness, log, leaf, edge="crisp"):
    """Whorled pine (models Scots pine, Pinus sylvestris). Bare lower bole,
    discrete crisp foliage tiers, spire tip.

    Grounding (1 cube = 1 m):
    - Whorled tiers: Scots pine produces ONE branch whorl per year — a real,
      cited growth habit (USFS Silvics of North America, Scotch Pine:
      research.fs.usda.gov/silvics/scotch-pine). The tier SPACING here is
      step=3 subs = 1 cube: a legibility/grid choice (tiers need >=1 sub of gap
      to read as separate whorls at voxel scale), NOT a claim that annual leader
      growth is 1 m — real leader increment is ~0.15-1.0 m and varies with
      age/site, so this is deliberately stylized, not measured.
    - Bare lower bole: pines self-prune shaded lower branches in closed stands
      (general silvics; USFS Scotch Pine notes lower-branch die-back). crown_lo=
      0.45 (crown base at 45% of height) is a STYLIZATION in that spirit — no
      single Scots-pine live-crown-ratio figure is pinned to it (NEEDS-RESEARCH).
    - Narrow conic crown, radius factor 0.15 (width ~0.3 x height) — closed-stand
      (not open-grown ornamental) form; qualitative, not pinned to one figure.
    fullness thins only the crown INTERIOR (via crisp_disc), never the silhouette
    radius, so the edge stays crisp at any fullness.
    """
    top = h * 3
    crown_lo = round(top * 0.45)
    # Stout bole below the crown; plus-taper through the lower crown; single-sub
    # leader through the upper crown so small top whorls stay visible around it.
    for cy in range(max(1, crown_lo // 3)):
        t.fill_cube(0, cy, 0, log)
    mid = crown_lo + (top - crown_lo) // 2
    for sy in range(max(1, crown_lo // 3) * 3, top):
        if sy < mid:
            for (dx, dz) in ((1, 1), (0, 1), (2, 1), (1, 0), (1, 2)):
                t.log(dx, sy, dz, log)
        else:
            t.log(1, sy, 1, log)
    for (fx, fz) in [(-1, 0), (3, 0), (0, -1), (0, 3),
                     (-1, -1), (3, 3), (-1, 3), (3, -1)]:
        if rng.random() < 0.6:                 # root flare (as cube_trunk)
            for sy in range(rng.randint(1, 2)):
                t.log(fx if fx >= 0 else -1, sy, fz if fz >= 0 else -1, log)
    base_r = (radius * 3) if radius else max(4, round(h * 0.15 * 3))
    step = 3                                   # 1 tier / cube: grid-legibility gap
    thick = 2 if fullness >= 0.7 else 1
    span = max(1, top - crown_lo)
    # Tiers stop short of the tip (spire above); radius floor 1.4 keeps the
    # smallest whorls visible around the thin leader.
    for ty in range(crown_lo, top - 1, step):
        f = (ty - crown_lo) / span
        r = max(1.4, base_r * (1.0 - f))
        # whorl branches: radial log spokes in the bare gap row under the tier
        if r >= 2.5:
            n = 4 + int(r) % 2
            for k in range(n):
                ang = 2 * math.pi * k / n + rng.uniform(-0.3, 0.3)
                for i in range(1, int(r)):
                    t.log(1 + round(math.cos(ang) * i), ty - 1,
                          1 + round(math.sin(ang) * i), log)
        for dy in range(thick):
            crisp_disc(t, ty + dy, r, leaf, rng=rng, fullness=fullness)
        # needle sprigs standing on the tier rim (accents; never widen the edge)
        for k in range(3):
            ang = 2 * math.pi * (k + rng.random()) / 3
            sx = 1 + round(math.cos(ang) * (r - 0.5))
            sz = 1 + round(math.sin(ang) * (r - 0.5))
            t.sprig(sx, ty + thick, sz, 1, 0, 1, leaf)
    t.leaf(1, top, 1, leaf)                    # spire continues the trunk top
    t.leaf(1, top + 1, 1, leaf)
    t.sprig(1, top + 2, 1, 1, 0, 1, leaf)


def gen_fir(t, rng, h, radius, fullness, log, leaf, edge="crisp"):
    """Fir (models balsam fir, Abies balsamea): continuous crisp spire cone,
    crown reaching low on the trunk.

    Grounding (1 cube = 1 m):
    - Continuous low crown: true firs are shade-tolerant and hold live branches
      deep down the bole; balsam fir vigorous trees keep live-crown ratio >=0.7,
      i.e. crown base <=30% of height (USFS Silvics of North America, Balsam Fir:
      research.fs.usda.gov/silvics/balsam-fir). lo=0.22 -> crown base at 22%.
    - Very narrow spire: radius factor 0.125 (width ~0.25 x height) — a narrow
      forest (not open-grown ornamental) form. The exact width:height ratio is a
      generation choice, not pinned to one silvics figure (NEEDS-RESEARCH).
    fullness thins only the crown INTERIOR (crisp_disc), never the edge.
    """
    base_r = (radius * 3) if radius else max(3, round(h * 0.125 * 3))
    crisp_cone(t, rng, h, base_r, log, leaf, fullness, crown_lo_frac=0.22, exp=1.0)


def gen_jungle(t, rng, h, radius, fullness, log, leaf, edge="fuzzy"):
    r = (radius or max(3, round(h * 0.35))) * 3
    big = h >= 14
    if big:
        for cy in range(h):                                # 2x2-cube trunk
            for cx in range(2):
                for cz in range(2):
                    t.fill_cube(cx, cy, cz, log)
        c, top = 3, h * 3
    else:
        top = cube_trunk(t, h, log, taper_from=0.75, rng=rng)
        c = 1
    # buttress roots: tall thin sub-wedges
    for (fx, fz) in [(-1, c // 2), (c * 2 if big else 3, c // 2),
                     (c // 2, -1), (c // 2, c * 2 if big else 3)]:
        for sy in range(rng.randint(3, 5)):
            t.log(fx, sy, fz, log)
    # branch arms with leaf blobs partway up
    for _ in range(rng.randint(2, 3)):
        by = rng.randint(round(top * 0.5), top - 4)
        end = sub_branch(t, rng, (c, by, c),
                         (rng.choice([-1, 1]), rng.choice([-1, 1])),
                         rng.randint(4, 7), log, thickness=2, rise=0.45)
        ellipsoid_canopy(t, rng, end[0], end[1] + 2, end[2], 6, 4, leaf,
                         fullness, sprigs=False)
    ellipsoid_canopy(t, rng, c, top + 1, c, r, max(5, round(r * 0.5)),
                     leaf, fullness)


def gen_acacia(t, rng, h, radius, fullness, log, leaf, edge="fuzzy"):
    r = (radius or max(4, round(h * 0.85))) * 3
    # kinked trunk at SUB resolution: 2x2-sub column drifting diagonally
    x = z = 0
    drift_x = rng.choice([-1, 1])
    drift_z = rng.choice([-1, 0, 1])
    kink = rng.randint(5, max(6, h * 3 - 8))
    top = h * 3
    for sy in range(top):
        if sy >= kink and sy % 2 == 0:
            x += drift_x
            z += drift_z
        for bx in range(2):
            for bz in range(2):
                t.log(x + bx, sy, z + bz, log)
    # base: widen to 3x3 subs for footing
    for bx in range(-1, 3):
        for bz in range(-1, 3):
            t.log(bx, 0, bz, log)
    disc_canopy(t, rng, top, r, leaf, fullness, thick=2, cx=x + 1, cz=z + 1)
    if rng.random() < 0.5:
        disc_canopy(t, rng, max(6, top - 7), max(5, r // 2), leaf, fullness,
                    thick=1, cx=-x, cz=-z)
    # short support branches under the pad rim
    for ang in (45, 135, 225, 315):
        fx = math.cos(math.radians(ang))
        fz = math.sin(math.radians(ang))
        bx, bz = x + 1 + round(fx * r * 0.5), z + 1 + round(fz * r * 0.5)
        for i in range(3):
            t.log(round(x + 1 + fx * r * 0.5 * i / 3),
                  top - 3 + i, round(z + 1 + fz * r * 0.5 * i / 3), log)
        t.log(bx, top - 1, bz, log)


def gen_palm(t, rng, h, radius, fullness, log, leaf, edge="fuzzy"):
    r = (radius or max(3, round(h * 0.55))) * 3
    # curved slender trunk: 2x2 subs, lean grows quadratically
    lean = rng.choice([-1, 1])
    top_y = h * 3
    pts = []
    for sy in range(top_y):
        off = round(lean * 3.0 * (sy / top_y) ** 2)
        for bx in range(2):
            for bz in range(2):
                t.log(off + bx, sy, bz, log)
        pts.append(off)
    tx, tz = pts[-1], 0
    # fronds: drooping arcs rasterized as CONTINUOUS 3D lines (the quadratic
    # droop drops several subs per step at the tip — interpolate every segment
    # so fronds never break into floating dots), thicker base, micro tip
    for ang in range(0, 360, 40):
        fx, fz = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        px, py, pz = tx + 1, top_y + 1, tz + 1
        for i in range(1, r + 1):
            lx = tx + 1 + round(fx * i)
            lz = tz + 1 + round(fz * i)
            ly = top_y + 1 - max(0, round((i * i) / (r * 1.2)))
            steps = max(abs(lx - px), abs(ly - py), abs(lz - pz), 1)
            for s in range(1, steps + 1):
                ix = round(px + (lx - px) * s / steps)
                iy = round(py + (ly - py) * s / steps)
                iz = round(pz + (lz - pz) * s / steps)
                t.leaf(ix, iy, iz, leaf)
                if i <= r // 2:                   # thicker frond base half
                    t.leaf(ix, iy + 1, iz, leaf)
            px, py, pz = lx, ly, lz
        t.sprig(px + (1 if fx > 0 else -1), py - 1, pz + (1 if fz > 0 else -1),
                1, 2, 1, leaf)
    # coconut cluster: micros under the crown
    for _ in range(3):
        t.sprig(tx + rng.randint(0, 1), top_y - 1, rng.randint(0, 1),
                rng.randint(0, 2), 0, rng.randint(0, 2), log)
    t.leaf(tx + 1, top_y + 1, tz + 1, leaf)


def gen_willow(t, rng, h, radius, fullness, log, leaf, edge="fuzzy"):
    r = (radius or max(3, round(h * 0.55))) * 3
    top = cube_trunk(t, h, log, taper_from=0.55, rng=rng)
    for _ in range(rng.randint(2, 3)):
        by = rng.randint(round(top * 0.6), top - 2)
        sub_branch(t, rng, (1, by, 1),
                   (rng.choice([-1, 1]), rng.choice([-1, 1])),
                   rng.randint(3, 6), log, thickness=1, rise=0.55)
    cy = top - round(r * 0.2)
    ellipsoid_canopy(t, rng, 1, cy, 1, r, max(4, round(r * 0.55)), leaf, fullness)
    # hanging strands: 1/3-thick leaf columns from the rim, micro tips
    for ang in range(0, 360, 24):
        fx, fz = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        lx = 1 + round(fx * (r - 1))
        lz = 1 + round(fz * (r - 1))
        drop = rng.randint(round(top * 0.45), max(3, cy - 2))
        wob = rng.choice([-1, 0, 0, 1])
        for d in range(drop):
            wx = lx + (wob if d > drop * 0.6 else 0)
            t.leaf(wx, cy - d, lz, leaf)
        t.sprig(lx, cy - drop, lz, 1, 0, 1, leaf)


def gen_dead(t, rng, h, radius, fullness, log, leaf, edge="fuzzy"):
    top = cube_trunk(t, h, log, taper_from=0.4, rng=rng)
    for _ in range(rng.randint(3, 5)):
        by = rng.randint(round(top * 0.4), top - 1)
        end = sub_branch(t, rng, (1, by, 1),
                         (rng.choice([-1, 1]), rng.choice([-1, 1])),
                         rng.randint(4, 8), log, thickness=2, rise=0.55)
        # secondary twig off the branch end
        sub_branch(t, rng, end, (rng.choice([-1, 1]), rng.choice([-1, 1])),
                   rng.randint(2, 3), log, thickness=1, rise=0.5)
    t.log(1, top, 1, log)
    t.sprig(1, top + 1, 1, 1, 0, 1, log)


def gen_bush(t, rng, h, radius, fullness, log, leaf, edge="fuzzy"):
    # Lumpy multi-lobe shrub: several overlapping leaf blobs at jittered centers
    # read as an organic bush instead of one machined ball. Radii in SUB units.
    rc = (radius or 2) * 3
    centers = [(0, max(2, rc - 1), 0)]
    for _ in range(rng.randint(2, 4)):
        centers.append((rng.randint(-rc + 1, rc - 1),
                        rng.randint(max(1, rc - 3), rc),
                        rng.randint(-rc + 1, rc - 1)))
    for (cx, cy, cz) in centers:
        leaf_cluster(t, rng, cx, cy, cz, rng.uniform(rc * 0.45, rc * 0.7),
                     leaf, fullness)


# ---------------------------------------------------------------- giant flora
# Megaflora (ProceduralTreeExpansionPlan Increment B) — enchanted-forest scale trees. These are
# built in CUBE space (fill_cube) at CUBE resolution on purpose: the chunk mesher greedy-merges
# and face-culls solid cube regions, so a solid canopy costs only its SURFACE faces. Sub/micro
# shells are NOT greedy-merged (render is the engine's #1 open issue), so a giant sub-shell would
# explode the face count. Sub/micro are reserved here for sparse surface accents (glow veins).
# Canopy radius is capped at 24 cubes — the flora-margin ceiling (ObjectTemplateManager
# kFloraMarginCap) beyond which a canopy clips at chunk seams.

GIANT_CANOPY_CAP = 40   # matches engine kFloraMarginCap; broad world-tree canopies


def cube_ellipsoid(t, ccx, ccy, ccz, rx, ry, rz, mat, is_log=False, hollow=0.0):
    """Ellipsoid of whole CUBES (radii in cubes). hollow=0 → solid. hollow>0 → keep only a SHELL
    (normalized distance in [hollow, 1.0]); the mesher renders only the surface anyway, so a solid
    interior is wasted cubes — hollowing cuts the per-tree stamp cost ~3-4x (critical for streaming
    giant forests) while the outside looks identical. Overlapping crowns in a dense forest bury each
    other's inner shells, so cavities don't read from outside."""
    lo2 = hollow * hollow
    for cx in range(int(math.floor(ccx - rx)), int(math.ceil(ccx + rx)) + 1):
        for cy in range(int(math.floor(ccy - ry)), int(math.ceil(ccy + ry)) + 1):
            for cz in range(int(math.floor(ccz - rz)), int(math.ceil(ccz + rz)) + 1):
                d2 = (((cx - ccx) / rx) ** 2 + ((cy - ccy) / ry) ** 2 + ((cz - ccz) / rz) ** 2)
                if d2 <= 1.0 and (hollow <= 0.0 or d2 >= lo2):
                    t.fill_cube(cx, cy, cz, mat, is_log)


def cube_trunk_column(t, half, y0, y1, mat, cx=0, cz=0, taper=0.0):
    """Filled vertical cylinder of cubes, radius `half` cubes, rows [y0,y1). taper>0 narrows the
    radius linearly toward the top (0 = straight column)."""
    for cy in range(y0, y1):
        f = (cy - y0) / max(1, (y1 - y0))
        hh = max(1, round(half * (1.0 - taper * f)))
        for dx in range(-hh, hh + 1):
            for dz in range(-hh, hh + 1):
                if dx * dx + dz * dz <= hh * hh + hh:   # +hh rounds the disc out a touch
                    t.fill_cube(cx + dx, cy, cz + dz, mat, is_log=True)


def buttress_roots(t, rng, half, mat, n=(4, 6), rise=None):
    """Wide tapering root flares splaying from the trunk base — how giant trunks actually meet the
    ground. Cube resolution."""
    count = rng.randint(*n)
    rise = rise or (half + 2)
    for k in range(count):
        ang = 2 * math.pi * k / count + rng.uniform(-0.3, 0.3)
        dx, dz = math.cos(ang), math.sin(ang)
        for i in range(half + 3):
            hgt = max(1, int(rise * (1.0 - i / (half + 3))))
            bx, bz = round(dx * (half + i)), round(dz * (half + i))
            for cy in range(hgt):
                t.fill_cube(bx, cy, bz, mat, is_log=True)


def dense_crown(t, rng, cx, cy, cz, rh, rv, leaf, n_bumps=14):
    """A COHESIVE dense canopy, not a cluster of separate spheres. One SOLID oblate ellipsoid
    (horizontal radius rh, vertical rv) is the gap-free canopy body; many small bumps straddle its
    rim and top for an organic lumpy silhouette. Because every bump OVERLAPS the solid base, the
    union reads as a single canopy — the earlier "few big lobes" left sky gaps between them that
    read as individual circles. Cube resolution. Keep rh so bump reach (~1.3*rh) stays within
    GIANT_CANOPY_CAP."""
    # HOLLOW shell body — a thick (~5-6 cube) opaque canopy surface, ~2-3x cheaper than solid. Then a
    # ring of HOLLOW bumps straddling the rim for organic edge, overlapping the shell so no cavity
    # reads through. Everything hollow keeps a wide (76-cube) canopy affordable to stamp densely.
    cube_ellipsoid(t, cx, cy, cz, rh, rv, rh, leaf, hollow=0.82)
    for k in range(n_bumps):
        ang = 2 * math.pi * k / n_bumps + rng.uniform(-0.2, 0.2)
        rr = rh * rng.uniform(0.3, 0.42)
        py = cy + round(rng.uniform(-0.3, 0.35) * rv)
        cube_ellipsoid(t, cx + round(math.cos(ang) * rh * 0.85), py,
                       cz + round(math.sin(ang) * rh * 0.85), rr, rr * 0.8, rr, leaf, hollow=0.65)
    for _ in range(max(3, n_bumps // 3)):                          # top-fill bumps (domed, no bald top)
        ang = rng.uniform(0, 2 * math.pi)
        rr = rh * rng.uniform(0.28, 0.38)
        cube_ellipsoid(t, cx + round(math.cos(ang) * rh * 0.4), cy + round(rv * 0.62),
                       cz + round(math.sin(ang) * rh * 0.4), rr, rr * 0.8, rr, leaf, hollow=0.6)


def cube_grow_branch(t, rng, pos, direction, length, thickness, depth, log, clusters,
                     origin, max_r, max_y, spread=0.45, up_bias=0.08, taper=0.68, fork=(2, 2)):
    """Recursive CUBE-space branch (the giant analogue of grow_branch). Lays thick log limbs, forks
    with partial-random spread, and records FOLIAGE-CLUSTER centers along the outer half + at every
    tip so the caller can grow leaves on the branches. HARD-CLAMPED on BOTH the horizontal radius
    (`max_r` from the trunk axis — the footprint cap) AND the height (`max_y` — the dome top, so
    up-biased branches can't shoot to the sky). Either limit stops the branch."""
    ox0, oz0 = origin
    x, y, z = float(pos[0]), float(pos[1]), float(pos[2])
    dx, dy, dz = direction
    for i in range(length):
        x += dx; y += dy; z += dz
        bx, by, bz = round(x), round(y), round(z)
        if math.hypot(bx - ox0, bz - oz0) > max_r or by > max_y:   # crown edge / dome top -> stop
            break
        th = max(0, int(thickness))
        for ax in range(-th, th + 1):
            for az in range(-th, th + 1):
                if ax * ax + az * az <= th * th + th:
                    t.fill_cube(bx + ax, by, bz + az, log)      # thick limb segment
        if i >= length * 0.35 and i % 2 == 0:                   # foliage hugs the outer limb
            clusters.append((bx, by, bz))
    end = (round(x), round(y), round(z))
    clusters.append(end)                                        # tip -> a leaf blob
    if (depth <= 0 or length <= 2 or math.hypot(end[0] - ox0, end[2] - oz0) >= max_r
            or end[1] >= max_y):
        return
    for _ in range(rng.randint(*fork)):
        cdir = perturb_dir(rng, (dx, dy, dz), spread, up_bias)   # gentle upward bias
        clen = max(2, int(length * rng.uniform(taper - 0.1, taper + 0.08)))
        cube_grow_branch(t, rng, end, cdir, clen, thickness - 1, depth - 1,
                         log, clusters, origin, max_r, max_y, spread, up_bias, taper, fork)


def cube_branched_crown(t, rng, cx, cz, trunk_top, crown_r, log, leaf,
                        n_limbs=(9, 13), blob_r=(6, 9), depth=3, fullness=1.0,
                        crown_h_frac=0.9, leader=True):
    """A 3D-DOMED branch armature (not a flat skirt). Each limb is aimed at a point distributed across
    the upper hemisphere of a dome — varied azimuth AND elevation — so branches CLIMB and fill the
    crown volume at many heights, then fork (up-biased) and carry foliage blobs at every cluster. The
    union fills a rounded dome held up by visible limbs. Radius-clamped to crown_r (footprint cap);
    blobs clamped so their edge stays within crown_r."""
    clusters = []
    nl = rng.randint(*n_limbs)
    br_max = max(blob_r)
    limb_r = max(4, crown_r - int(br_max))              # branch reach; blobs extend to crown_r
    crown_base = int(trunk_top * 0.5)
    center_y = trunk_top                                # dome springs from the trunk top
    dome_top = trunk_top + int(crown_r * crown_h_frac)  # ... and reaches this high
    for k in range(nl):
        theta = 2 * math.pi * k / nl + rng.uniform(-0.35, 0.35)
        phi = rng.uniform(0.12, 1.0) ** 0.8             # dome elevation (0=out/low, 1=straight up)
        horiz = limb_r * math.cos(phi * math.pi / 2)
        tx = cx + math.cos(theta) * horiz               # target on the dome surface
        tz = cz + math.sin(theta) * horiz
        ty = center_y + (dome_top - center_y) * math.sin(phi * math.pi / 2)
        start_y = int(crown_base + (trunk_top - crown_base) * (0.3 + 0.7 * phi)) + rng.randint(-2, 2)
        start_y = max(crown_base, min(trunk_top, start_y))
        d0 = _norm((tx - cx, ty - start_y, tz - cz))
        limb_len = int(math.sqrt((tx - cx) ** 2 + (ty - start_y) ** 2 + (tz - cz) ** 2) * 1.25) + 2
        cube_grow_branch(t, rng, (cx, start_y, cz), d0, limb_len, 3, depth, log, clusters,
                         (cx, cz), limb_r, dome_top, up_bias=0.1, fork=(2, 3))
    if leader:                                          # central ascending leader with lateral forks
        cube_grow_branch(t, rng, (cx, trunk_top, cz),
                         _norm((rng.uniform(-0.2, 0.2), 1.0, rng.uniform(-0.2, 0.2))),
                         max(3, dome_top - trunk_top), 2, depth, log, clusters,
                         (cx, cz), limb_r, dome_top, up_bias=0.08, fork=(2, 3))
    for (lx, ly, lz) in clusters:                       # leaves on the branches
        r = rng.uniform(*blob_r)
        dxr, dzr = lx - cx, lz - cz
        dist = math.hypot(dxr, dzr)
        if dist + r > crown_r and dist > 0.1:           # keep blob edge within crown_r (footprint cap)
            s = (crown_r - r) / dist
            lx, lz = cx + round(dxr * s), cz + round(dzr * s)
        cube_ellipsoid(t, lx, ly, lz, r, r * 0.85, r, leaf, hollow=0.45)


def gen_redwood(t, rng, h, radius, fullness, log, leaf, edge="crisp"):
    """Coast-redwood-class giant (Sequoia sempervirens). 1 cube = 1 m. CUBE resolution.

    Grounding:
    - Height: xl=115 ≈ Hyperion, the tallest living tree, 115.55 m verified (Sillett 2006, per
      Wikipedia "Hyperion (tree)"), ~116 m today. redwood_l=90 is an interpolated old-growth size,
      NOT a single named specimen.
    - Trunk diameter factor 0.18 (clamp 6-12): a thick, sturdy bole proportional to the broad crown
      (real redwood DBH 3-8.9 m; here fantasy-thickened so the trunk reads at a distance).
    - Branch-driven crown: limbs fork from the upper trunk and carry the foliage (leaves on branches),
      radius factor 0.5 — a big spreading canopy tree, not a spire.
    """
    dia = min(12, max(6, round(h * 0.18)))         # THICK trunk (proportional to the wide crown)
    half = max(3, dia // 2)
    cube_trunk_column(t, half, 0, h, log, taper=0.25)
    buttress_roots(t, rng, half, log, n=(5, 6))
    cr = min(30, max(18, radius or round(h * 0.5)))
    cube_branched_crown(t, rng, 0, 0, h, cr, log, leaf, n_limbs=(9, 12),
                        blob_r=(6, 9), depth=3, crown_h_frac=0.85)


def gen_elder_oak(t, rng, h, radius, fullness, log, leaf, edge="fuzzy",
                  glow="glow_green"):
    """Fantasy world-tree with a sequoia-scale TRUNK and a deliberately broadened crown.
    CUBE resolution; crown radius capped at the flora-margin ceiling (24).

    Grounding (1 cube = 1 m; anchor: General Sherman, 83.8 m tall / 11.1 m base dia / first branch
    ~40 m up / crown spread ~32 m — NPS):
    - Thick trunk (dia factor 0.34, clamp 8-16) — a massive bole proportional to the broad canopy so
      the trunk reads at forest scale. Sherman's real ratio is 0.13; this is fantasy-thickened.
    - Branch-driven crown: heavy limbs fork off the upper trunk and fan out to carry the foliage
      (leaves grow on the branches), spreading ~0.85x height wide — a vast world-tree canopy held up
      by visible limbs, not a floating sphere.
    """
    dia = min(16, max(8, round(h * 0.34)))         # MASSIVE trunk (proportional to the wide crown)
    half = max(4, dia // 2)
    cube_trunk_column(t, half, 0, h, log, taper=0.2)
    buttress_roots(t, rng, half, log, n=(6, 8))
    cr = min(30, max(20, radius or round(h * 0.85)))
    cube_branched_crown(t, rng, 0, 0, h, cr, log, leaf, n_limbs=(10, 14),
                        blob_r=(7, 10), depth=3, crown_h_frac=0.9)
    # Glowing root veins climbing the lower trunk surface (enchanted accent; subs, cheap in low count).
    for _ in range(rng.randint(6, 10)):
        ang = rng.uniform(0, 2 * math.pi)
        sx = round(math.cos(ang) * (half * 3 + 1)) + 1   # just outside the cube trunk, in sub space
        sz = round(math.sin(ang) * (half * 3 + 1)) + 1
        for sy in range(0, int(h * rng.uniform(0.3, 0.55)) * 3, 2):
            t.leaf(sx, sy, sz, glow)


GENERATORS = {
    "oak": gen_oak, "autumn": gen_oak, "birch": gen_birch, "spruce": gen_spruce,
    "pine": gen_pine, "fir": gen_fir,
    "jungle": gen_jungle, "acacia": gen_acacia, "palm": gen_palm,
    "willow": gen_willow, "dead": gen_dead, "bush": gen_bush,
    "redwood": gen_redwood, "elder_oak": gen_elder_oak,
}

# Archetypes whose silhouette is hard-edged by construction (edge param moot).
CRISP_TYPES = {"pine", "fir"}


# --------------------------------------------------------------- cleanup

def prune_floaters(t):
    """Remove voxels not attached to the tree. Subs are kept only if 26-connected
    to the trunk base (so floating leaf islands vanish); microcubes are kept only
    if a face-adjacent sub is filled (so twig sprigs touch the canopy instead of
    hovering slightly off it)."""
    subs = t.sub
    if not subs:
        return
    miny = min(k[1] for k in subs)
    keep = set(k for k in subs if k[1] <= miny + 1)     # seed: trunk base
    stack = list(keep)
    while stack:
        sx, sy, sz = stack.pop()
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    if dx or dy or dz:
                        nk = (sx + dx, sy + dy, sz + dz)
                        if nk in subs and nk not in keep:
                            keep.add(nk)
                            stack.append(nk)
    t.sub = {k: v for k, v in subs.items() if k in keep}

    # Erode lonely LEAF subs: every leaf must share a FACE with another voxel, or
    # it reads as floating (26-connectivity keeps corner-only bits so diagonal
    # branch chains survive, but a corner-only leaf looks detached). Logs are
    # always kept so branches stay contiguous.
    face = ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))
    lonely = [k for k, (_, is_log) in t.sub.items()
              if not is_log and not any((k[0] + d[0], k[1] + d[1], k[2] + d[2]) in t.sub
                                        for d in face)]
    for k in lonely:
        del t.sub[k]

    # Microcubes: keep only if they actually TOUCH a filled sub. A 1/9 microcube
    # at micro-pos (mx,my,mz) inside an empty sub-cell only contacts a neighbor sub
    # across the face it sits on (e.g. mx==2 -> the +X neighbor). Center-positioned
    # sprigs (1,1,1) touch nothing and hover — drop them.
    def micro_attached(sx, sy, sz, mx, my, mz):
        if (sx, sy, sz) in t.sub:
            return False                                 # embedded & hidden -> drop
        return ((mx == 2 and (sx + 1, sy, sz) in t.sub) or
                (mx == 0 and (sx - 1, sy, sz) in t.sub) or
                (my == 2 and (sx, sy + 1, sz) in t.sub) or
                (my == 0 and (sx, sy - 1, sz) in t.sub) or
                (mz == 2 and (sx, sy, sz + 1) in t.sub) or
                (mz == 0 and (sx, sy, sz - 1) in t.sub))
    t.micro = {k: v for k, v in t.micro.items() if micro_attached(*k)}


# --------------------------------------------------------------- emission

def emit(tree):
    """Compress sub-space to C/S/M lines. Returns (lines, counts, bounds)."""
    by_cube = {}
    for (sx, sy, sz), (mat, _) in tree.sub.items():
        ck = (sx // 3, sy // 3, sz // 3)
        by_cube.setdefault(ck, {})[(sx % 3, sy % 3, sz % 3)] = mat

    micro_cubes = [(k[0] // 3, k[1] // 3, k[2] // 3) for k in tree.micro]
    all_cubes = list(by_cube) + micro_cubes
    ox = min(c[0] for c in all_cubes)
    oy = min(c[1] for c in all_cubes)
    oz = min(c[2] for c in all_cubes)
    mx = max(c[0] for c in all_cubes)
    my = max(c[1] for c in all_cubes)
    mz = max(c[2] for c in all_cubes)
    bounds = (mx - ox + 1, my - oy + 1, mz - oz + 1)

    c_lines, s_lines, m_lines = [], [], []
    for (cx, cy, cz), subs in sorted(by_cube.items(),
                                     key=lambda kv: (kv[0][1], kv[0][0], kv[0][2])):
        rx, ry, rz = cx - ox, cy - oy, cz - oz
        mats = set(subs.values())
        if len(subs) == 27 and len(mats) == 1:
            c_lines.append(f"C {rx} {ry} {rz} {next(iter(mats))}")
        else:
            for (ux, uy, uz), mat in sorted(subs.items()):
                s_lines.append(f"S {rx} {ry} {rz} {ux} {uy} {uz} {mat}")

    for (sx, sy, sz, ux, uy, uz), mat in sorted(tree.micro.items()):
        rx, ry, rz = sx // 3 - ox, sy // 3 - oy, sz // 3 - oz
        if ry < 0:
            continue
        m_lines.append(f"M {rx} {ry} {rz} {sx % 3} {sy % 3} {sz % 3} "
                       f"{ux} {uy} {uz} {mat}")

    counts = (len(c_lines), len(s_lines), len(m_lines))
    return c_lines + s_lines + m_lines, counts, bounds


def write_voxel(name, display, desc, lines, counts, bounds, materials, args_str):
    nc, ns, nm = counts
    w, hh, d = bounds
    header = [
        "# ==========================================================",
        "# ASSET METADATA",
        f"# name:         {name}",
        f"# display_name: {display}",
        f"# description:  {desc}",
        "# category:     nature",
        "# subcategory:  trees",
        "# tags:         tree, nature, biome, forest, procedural",
        f"# materials:    {', '.join(sorted(materials))}",
        "# facing:       +Z",
        f"# bounds:       {w}W x {hh}H x {d}D cubes",
        f"# primitives:   {nc} C + {ns} S + {nm} M = {nc + ns + nm}",
        f"# generator:    gen_tree.py {args_str}",
        "# ==========================================================",
        "",
    ]
    path = os.path.join(TEMPLATE_DIR, f"{name}.voxel")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(header + lines) + "\n")
    return path


def register_catalog(name, display, desc, counts, materials, height):
    nc, ns, nm = counts
    with open(CATALOG, encoding="utf-8") as f:
        cat = json.load(f)
    cat[name] = {
        "display_name": display,
        "description": desc,
        "prompt": None,
        "model": "procedural/gen_tree.py",
        "material": sorted(materials)[0],
        "materials": sorted(materials),
        "size": float(height),
        "cubes": nc,
        "subcubes": ns,
        "microcubes": nm,
        "total": nc + ns + nm,
        "cost": 0.0,
        "created": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "category": "nature",
        "subcategory": "trees",
        "tags": ["tree", "nature", "biome", "forest", "procedural"],
    }
    with open(CATALOG, "w", encoding="utf-8", newline="\n") as f:
        json.dump(cat, f, indent=2)
        f.write("\n")


def preview_tree(t):
    """Print front (X-Y) and side (Z-Y) silhouettes for fast shape iteration
    in the terminal — no engine round-trip needed. '|' = log, '*' = leaf."""
    if not t.sub:
        print("  (empty)")
        return
    xs = [k[0] for k in t.sub]
    ys = [k[1] for k in t.sub]
    zs = [k[2] for k in t.sub]
    miny, maxy = min(ys), max(ys)

    def project(axis):
        amin = min(xs) if axis == 0 else min(zs)
        amax = max(xs) if axis == 0 else max(zs)
        grid = {}
        for (sx, sy, sz), (_, is_log) in t.sub.items():
            a = sx if axis == 0 else sz
            key = (a, sy)
            if is_log or key not in grid:
                grid[key] = is_log
        rows = []
        for sy in range(maxy, miny - 1, -1):
            rows.append("".join("|" if grid.get((a, sy)) is True
                                else "*" if (a, sy) in grid else " "
                                for a in range(amin, amax + 1)))
        return rows

    print(f"  front (X-Y), side (Z-Y)  [{maxy - miny + 1} tall]:")
    for fr, sd in zip(project(0), project(2)):
        print(f"    {fr:<28}  {sd}")


def generate_one(ttype, height=None, radius=None, fullness=0.85, seed=0,
                 name=None, display=None, register=True, preview=False,
                 edge="fuzzy", log_mat=None, leaf_mat=None):
    spec = ARCHETYPES[ttype]
    h = height or spec["height"]
    # Seed key: keep the ORIGINAL form for default (no material override) so the existing library
    # stays byte-reproducible; only extend it when an override is actually given. (Appending the
    # material suffix unconditionally silently reshaped every default tree — determinism regression.)
    key = f"{ttype}:{h}:{radius}:{fullness}:{seed}"
    if log_mat is not None or leaf_mat is not None:
        key += f":{log_mat}:{leaf_mat}"
    rng = random.Random(key)
    # Optional material overrides (e.g. a glowing enchanted-understory bush via --leaf glow_green).
    log = log_mat if log_mat is not None else spec["log"]
    leaf = leaf_mat if leaf_mat is not None else spec["leaf"]

    t = Tree()
    GENERATORS[ttype](t, rng, h, radius, fullness, log, leaf,
                      edge="crisp" if ttype in CRISP_TYPES else edge)
    if not t.sub:
        raise SystemExit(f"{ttype}: generated zero voxels?!")
    prune_floaters(t)                  # drop detached leaf islands / hovering sprigs
    if preview:
        preview_tree(t)

    lines, counts, bounds = emit(t)
    name = name or f"tree_{ttype}_{h}_s{seed}"
    display = display or f"{ttype.capitalize()} Tree (h{h})"
    desc = (f"Procedural {ttype} tree, height {h}, fullness {fullness}, "
            f"seed {seed}.")
    materials = {m for m, _ in t.sub.values()} | set(t.micro.values())
    args_str = (f"--type {ttype} --height {h} --radius {radius or 'auto'} "
                f"--fullness {fullness} --seed {seed}"
                + (f" --edge {edge}" if edge != "fuzzy" else ""))
    path = write_voxel(name, display, desc, lines, counts, bounds,
                       materials, args_str)
    if register:
        register_catalog(name, display, desc, counts, materials, h)
    nc, ns, nm = counts
    print(f"  {name}: {nc}C + {ns}S + {nm}M, bounds "
          f"{bounds[0]}x{bounds[1]}x{bounds[2]} -> {os.path.relpath(path, REPO)}")
    return name


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--type", choices=sorted(ARCHETYPES), help="tree archetype")
    ap.add_argument("--height", type=int, help="trunk height in cubes")
    ap.add_argument("--radius", type=int, help="canopy radius in cubes (default: from height)")
    ap.add_argument("--fullness", type=float, default=0.85, help="leaf density 0..1 (default 0.85)")
    ap.add_argument("--edge", choices=("fuzzy", "crisp"), default="fuzzy",
                    help="silhouette edge: fuzzy = dithered rim (default), crisp = hard edge")
    ap.add_argument("--leaf", dest="leaf_mat", help="override foliage material (e.g. glow_green)")
    ap.add_argument("--log", dest="log_mat", help="override trunk material")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--name", help="template name (default tree_<type>_<h>_s<seed>)")
    ap.add_argument("--display-name", dest="display")
    ap.add_argument("--no-catalog", action="store_true", help="skip catalog registration")
    ap.add_argument("--batch", help="JSON manifest: [{type, height, fullness, seed, name, display}, ...]")
    ap.add_argument("--preview", action="store_true", help="print ASCII silhouettes (fast shape iteration)")
    args = ap.parse_args()

    if args.batch:
        with open(args.batch, encoding="utf-8") as f:
            entries = json.load(f)
        entries = [e for e in entries if "type" in e]   # skip _comment markers
        print(f"Batch: {len(entries)} trees")
        for e in entries:
            generate_one(e["type"], e.get("height"), e.get("radius"),
                         e.get("fullness", 0.85), e.get("seed", 0),
                         e.get("name"), e.get("display"),
                         register=not args.no_catalog, preview=args.preview,
                         edge=e.get("edge", "fuzzy"),
                         log_mat=e.get("log"), leaf_mat=e.get("leaf"))
        return 0

    if not args.type:
        ap.error("--type or --batch required")
    generate_one(args.type, args.height, args.radius, args.fullness, args.seed,
                 args.name, args.display, register=not args.no_catalog,
                 preview=args.preview, edge=args.edge,
                 log_mat=args.log_mat, leaf_mat=args.leaf_mat)
    return 0


if __name__ == "__main__":
    sys.exit(main())
